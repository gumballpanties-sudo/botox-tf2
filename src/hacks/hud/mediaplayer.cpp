#include "mediaplayer.h"
#include "watermark.h"          // g_wmBox -- botox anchors the player under the watermark
#include "watermark_styles.h"   // Watermark_MediaLook / Watermark_MediaFrame -- the style's colours + box
#include "mp_art.h"      // cover art scale + centre crop (gate tested, no winrt in it)
#include "mp_time.h"     // timeline -> position + length (gate tested, no winrt in it)
#include <ole2.h>        // WIN32_LEAN_AND_MEAN drops it; media_file.h's propvarutil.h needs V_VT
#include "kopuz.h"       // botox globals/media_player/kopuz.h, verbatim
#include "media_file.h"  // botox globals/media_player/media_file.h, verbatim
#include "../../dependencies/imgui/imgui_internal.h"   // GImGui->HoveredWindow: a menu window over the player eats the click

// botox csgo-sdk/globals/media_player/media_player.cpp on_update + run_command + main.cpp
// media_player_thread, 1:1: playing SMTC session > playing kopuz > audible app (song file it has
// open, else its window title; discord + apps with a mic stream skipped) > paused kopuz > paused
// session. Controls: SMTC session / kopuz rpc / WM_APPCOMMAND to the app, media key last.
//
// ONE CHANGE: art. botox hands the encoded bytes to D3DXCreateTextureFromFileInMemory; this dll
// links d3d9 only, so the same bytes go through winrt BitmapDecoder -> BGRA8 here and the render
// thread does CreateTexture + LockRect. Lyrics + visualizer are not ported (panel has neither).
#pragma comment(lib, "windowsapp")
#pragma comment(lib, "ole32")
#pragma comment(lib, "shell32")

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace wf  = winrt::Windows::Foundation;
namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;

using Session = wmc::GlobalSystemMediaTransportControlsSession;
using Manager = wmc::GlobalSystemMediaTransportControlsSessionManager;

// ==== shared state ==============================================================================
// Written by the poll thread, read by the render thread. Everything but the atomics is under s_mtx.
static std::mutex s_mtx;
static std::string s_title, s_artist;

static std::vector<unsigned char> s_px;        // BGRA8, s_pxW * s_pxH * 4
static int  s_pxW = 0, s_pxH = 0;
static bool s_pxDirty = false;                 // -> the render thread rebuilds the texture

static long long s_totalMs = 0, s_curMs = 0;
static double s_rate = 1.0;                    // playback speed, for the render-side slide
static std::chrono::steady_clock::time_point s_stamp{};

static std::atomic<bool> s_has{ false };       // botox m_has_media
static std::atomic<bool> s_playing{ false };   // botox m_is_playing

// botox m_source_app / m_source_kind: session's AppUserModelId (no session: audible app's exe path)
enum Source : int { source_none, source_session, source_kopuz, source_app };
static std::wstring s_source;
static Source s_kind = source_none;

// botox e_command: 1 previous, 2 toggle, 3 next. 0 = none
static std::atomic<int> s_pendingCmd{ 0 };

// poll thread only. an audible-only app ( no SMTC ) goes silent once paused, keep it shown as paused
static bool s_appPaused = false;
static std::chrono::steady_clock::time_point s_appPausedAt{};
static std::vector<unsigned char> s_thumb;     // encoded bytes currently shown (botox m_thumbnail)
static std::wstring s_thumbKey;
static int s_thumbWait = 0;

// Render thread only.
static IDirect3DTexture9* s_tex = nullptr;

static HANDLE s_thread = nullptr;

// ==== poll thread ===============================================================================
static std::string ToUtf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), size, nullptr, nullptr);
    return out;
}

// Encoded image (jpg / png / bmp / webp ..) -> straight-alpha BGRA8, always kArt x kArt.
// Straight, not premultiplied: ImGui's dx9 backend blends SRCALPHA/INVSRCALPHA. Scale + crop in
// the decoder (mp_art.h, gate test tests/mediaplayer_test.cpp). 64 = pow2, no NONPOW2 rules.
static constexpr uint32_t kArt = 64;

static bool DecodeArt(const std::vector<unsigned char>& bytes, std::vector<unsigned char>& out, int& w, int& h)
{
    if (bytes.empty()) return false;
    try {
        wss::Buffer in((uint32_t)bytes.size());
        std::copy(bytes.begin(), bytes.end(), in.data());
        in.Length((uint32_t)bytes.size());
        wss::InMemoryRandomAccessStream stream;
        stream.WriteAsync(in).get();
        stream.Seek(0);

        const auto decoder = wgi::BitmapDecoder::CreateAsync(stream).get();
        const uint32_t sw = decoder.PixelWidth(), sh = decoder.PixelHeight();
        if (!sw || !sh) return false;

        const mp::ArtCrop c = mp::ArtCropFor(sw, sh, kArt);
        wgi::BitmapTransform tr;
        tr.InterpolationMode(wgi::BitmapInterpolationMode::Fant);
        tr.ScaledWidth(c.scaledW);
        tr.ScaledHeight(c.scaledH);
        wgi::BitmapBounds b{};
        b.X = c.x; b.Y = c.y; b.Width = c.w; b.Height = c.h;
        tr.Bounds(b);

        const auto bmp = decoder.GetSoftwareBitmapAsync(wgi::BitmapPixelFormat::Bgra8,
                                                        wgi::BitmapAlphaMode::Straight, tr,
                                                        wgi::ExifOrientationMode::IgnoreExifOrientation,
                                                        wgi::ColorManagementMode::DoNotColorManage).get();
        if (!bmp) return false;
        w = (int)bmp.PixelWidth();
        h = (int)bmp.PixelHeight();
        if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return false;

        wss::Buffer buf((uint32_t)(w * h * 4));
        bmp.CopyToBuffer(buf);
        if (buf.Length() < (uint32_t)(w * h * 4)) return false;
        out.assign(buf.data(), buf.data() + (size_t)w * h * 4);
        return true;
    } catch (...) {
        return false;
    }
}

namespace
{
    template<typename t>
    void safe_release(t*& pointer)
    {
        if (pointer) { pointer->Release(); pointer = nullptr; }
    }

    std::wstring lower(std::wstring text)
    {
        for (auto& c : text) c = (wchar_t)std::towlower(c);
        return text;
    }

    // "C:\x\Chrome.EXE" / "Spotify.exe" / "MSEdge" -> "chrome" / "spotify" / "msedge"
    std::wstring exe_stem(std::wstring name)
    {
        name = lower(std::move(name));
        if (const auto slash = name.find_last_of(L"\\/"); slash != std::wstring::npos) name.erase(0, slash + 1);
        if (name.size() > 4 && name.ends_with(L".exe")) name.resize(name.size() - 4);
        return name;
    }

    // apple music desktop sends "artist — album": cut at em dash
    std::wstring strip_album(std::wstring artist)
    {
        const auto dash = artist.find(L'\u2014');
        if (dash == std::wstring::npos) return artist;
        artist.resize(dash);
        while (!artist.empty() && std::iswspace(artist.back())) artist.pop_back();
        return artist;
    }

    // a hung app must not freeze the media thread: null on timeout / error
    template<typename op_t>
    auto await_for(const op_t& op) -> decltype(op.GetResults())
    {
        if (op.wait_for(std::chrono::seconds(2)) == wf::AsyncStatus::Completed) return op.GetResults();
        op.Cancel();
        return nullptr;
    }

    bool is_playing(const Session& session)
    {
        try {
            const auto info = session.GetPlaybackInfo();
            return info && info.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        } catch (...) {
            return false;
        }
    }

    // GetCurrentSession = what windows last focused, often null or a paused tab while another app plays
    Session pick_session(const Manager& manager)
    {
        const auto current = manager.GetCurrentSession();
        if (current && is_playing(current)) return current;

        const auto sessions = manager.GetSessions();
        for (const auto& session : sessions)
            if (is_playing(session)) return session;

        if (current) return current;
        return sessions.Size() ? sessions.GetAt(0) : nullptr;
    }

    std::vector<unsigned char> read_thumbnail(const wmc::GlobalSystemMediaTransportControlsSessionMediaProperties& info)
    {
        try {
            const auto reference = info.Thumbnail();
            if (!reference) return {};

            const auto stream = await_for(reference.OpenReadAsync());
            if (!stream || !stream.Size() || stream.Size() > 16u * 1024u * 1024u) return {};

            wss::Buffer buffer((uint32_t)stream.Size());
            const auto read = await_for(stream.ReadAsync(buffer, buffer.Capacity(), wss::InputStreamOptions::ReadAhead));
            if (!read) return {};
            return { read.data(), read.data() + read.Length() };
        } catch (...) {
            return {};
        }
    }

    std::wstring process_path(const DWORD pid)
    {
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        const bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
        CloseHandle(process);
        return ok ? lower(path) : std::wstring{};
    }

    // exe paths with an active stream on the default `flow` device, `sounding` = peak above silence too
    std::vector<std::wstring> session_apps(IMMDeviceEnumerator* enumerator, const EDataFlow flow, const bool sounding)
    {
        std::vector<std::wstring> out{};
        IMMDevice* device                 = nullptr;
        IAudioSessionManager2* manager    = nullptr;
        IAudioSessionEnumerator* sessions = nullptr;
        int count                         = 0;

        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&manager))) &&
            SUCCEEDED(manager->GetSessionEnumerator(&sessions)) && SUCCEEDED(sessions->GetCount(&count))) {
            for (int i = 0; i < count; i++) {
                IAudioSessionControl* control   = nullptr;
                IAudioSessionControl2* control2 = nullptr;
                IAudioMeterInformation* meter   = nullptr;
                AudioSessionState state         = AudioSessionStateInactive;
                DWORD pid                       = 0;
                float peak                      = 0.f;

                if (SUCCEEDED(sessions->GetSession(i, &control)) && SUCCEEDED(control->GetState(&state)) && state == AudioSessionStateActive &&
                    SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&control2))) && control2->IsSystemSoundsSession() == S_FALSE &&
                    SUCCEEDED(control2->GetProcessId(&pid)) && pid &&
                    (!sounding || (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&meter))) && SUCCEEDED(meter->GetPeakValue(&peak)) && peak > 0.001f))) {
                    if (auto path = process_path(pid); !path.empty()) out.push_back(std::move(path));
                }

                safe_release(meter);
                safe_release(control2);
                safe_release(control);
            }
        }

        safe_release(sessions);
        safe_release(manager);
        safe_release(device);
        return out;
    }

    std::wstring window_title(const std::wstring& path)
    {
        struct search_t {
            const std::wstring* m_path;
            std::wstring m_title;
        } search{ &path, {} };

        EnumWindows(
            [](HWND window, LPARAM param) -> BOOL {
                auto& search = *reinterpret_cast<search_t*>(param);

                // own windows skipped before GetWindowText: same-process text is a SendMessage to the game thread
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || pid == GetCurrentProcessId()) return TRUE;

                wchar_t text[512]{};
                if (!GetWindowTextW(window, text, 512) || process_path(pid) != *search.m_path) return TRUE;

                search.m_title = text;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(&search));

        return search.m_title;
    }

    // "12. Artist - Title.mp3 - VLC media player" / "Artist - Title [foobar2000]" -> "Artist", "Title"
    void split_title(std::wstring text, const std::wstring& stem, std::wstring& artist, std::wstring& title)
    {
        std::size_t last = 0;
        for (const wchar_t* cut : { L" - ", L" [", L" | " })
            if (const auto at = text.rfind(cut); at != std::wstring::npos) last = (std::max)(last, at);
        if (last && lower(text.substr(last)).find(stem) != std::wstring::npos) text.resize(last);

        std::size_t digits = 0;
        while (digits < text.size() && std::iswdigit(text[digits])) digits++;
        if (digits && text.compare(digits, 2, L". ") == 0) text.erase(0, digits + 2);

        if (const auto dot = text.rfind(L'.'); dot != std::wstring::npos) {
            static constexpr const wchar_t* k_media[]{ L".mp3", L".flac", L".wav", L".ogg", L".opus", L".m4a", L".aac", L".wma", L".mp4", L".mkv", L".webm" };
            const auto ext = lower(text.substr(dot));
            if (std::find_if(std::begin(k_media), std::end(k_media), [&](const wchar_t* e) { return ext == e; }) != std::end(k_media))
                text.resize(dot);
        }

        const auto at = text.find(L" - ");
        artist = at != std::wstring::npos ? text.substr(0, at) : std::wstring{};
        title  = at != std::wstring::npos ? text.substr(at + 3) : text;
    }

    /* no GSMTC player ( vlc 3, winamp, broken SMTC ): an app that makes sound and has no mic stream ( voice chat ).
       one with a song file open beats one with only a window title ( discord tab in a browser ) */
    bool find_audio_app(std::wstring& path, std::wstring& window, std::vector<std::wstring>& files)
    {
        IMMDeviceEnumerator* enumerator = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator))))
            return false;

        const auto playing = session_apps(enumerator, eRender, true);
        const auto talking = session_apps(enumerator, eCapture, false);
        enumerator->Release();

        const std::wstring self = process_path(GetCurrentProcessId());
        std::vector<std::wstring> titled{};
        for (const auto& app : playing) {
            // discord / ptb / canary: call + notification sounds, never a song
            if (app == self || exe_stem(app).starts_with(L"discord") || std::find(talking.begin(), talking.end(), app) != talking.end() ||
                std::find(titled.begin(), titled.end(), app) != titled.end())
                continue;

            if (files = n_media_file::open_media(app); !files.empty()) {
                path   = app;
                window = window_title(app);
                return true;
            }

            if (!window_title(app).empty()) titled.push_back(app);
        }

        if (titled.empty()) return false;

        path   = titled.front();
        window = window_title(path);
        return true;
    }

    // botox get_position_ms
    double position_ms()
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        auto current = (double)s_curMs;
        if (s_playing)
            current += s_rate * (double)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_stamp).count();
        return current;
    }
}

// botox impl_t::on_update
static void OnUpdate(const Manager& manager)
{
    const auto clear = []() {
        s_has     = false;
        s_playing = false;
        std::lock_guard<std::mutex> lock(s_mtx);
        s_source.clear();
        s_kind = source_none;
    };

    struct now_t {
        std::string m_title{}, m_artist{};
        std::wstring m_source{};
        Source m_kind{};
        std::optional<std::vector<unsigned char>> m_thumbnail{}; // nullopt = keep
        long long m_total_ms{}, m_current_ms{};
        std::chrono::steady_clock::time_point m_stamp{ std::chrono::steady_clock::now() };
        double m_rate{ 1.0 };
        bool m_playing{};
    };

    const auto publish = [](now_t& now) {
        // art changed: decode here, outside the lock (botox: render thread D3DX, see top)
        bool artChanged = now.m_thumbnail && *now.m_thumbnail != s_thumb;
        std::vector<unsigned char> px;
        int pw = 0, ph = 0;
        if (artChanged) {
            s_thumb = std::move(*now.m_thumbnail);
            if (!DecodeArt(s_thumb, px, pw, ph)) { px.clear(); pw = ph = 0; }
        }
        {
            std::lock_guard<std::mutex> lock(s_mtx);

            s_title  = std::move(now.m_title);
            s_artist = std::move(now.m_artist);
            s_source = std::move(now.m_source);
            s_kind   = now.m_kind;
            if (now.m_kind != source_app) s_appPaused = false;

            if (artChanged) {
                s_px = std::move(px);
                s_pxW = pw; s_pxH = ph;
                s_pxDirty = true;
            }

            s_totalMs = now.m_total_ms;
            s_curMs   = now.m_current_ms;
            s_stamp   = now.m_stamp;
            s_rate    = now.m_rate;
        }

        s_playing = now.m_playing;
        s_has     = true;
    };

    /* art on track change, again the next 5 updates ( a skip shows the new title with the old or no art first ), then every 3rd
       while still missing. a failed re-read never wipes shown art, a bad thumbnail never hides the player */
    const auto thumbnail = [](const std::wstring& key, const auto& read, const bool retry = true) -> std::optional<std::vector<unsigned char>> {
        if (key != s_thumbKey) {
            s_thumbKey  = key;
            s_thumbWait = 0;
            return read();
        }

        const int wait = ++s_thumbWait;
        if (!retry || (wait > 5 && (!s_thumb.empty() || wait % 3))) return std::nullopt;

        auto art = read();
        if (art.empty() && !s_thumb.empty()) return std::nullopt;
        return art;
    };

    const auto from_session = [&](const Session& session) {
        const auto info = await_for(session.TryGetMediaPropertiesAsync());
        if (!info) return;

        now_t now{};
        now.m_source    = session.SourceAppUserModelId();
        now.m_kind      = source_session;
        now.m_title     = ToUtf8(info.Title());
        now.m_artist    = ToUtf8(strip_album(std::wstring{ info.Artist() }));
        now.m_thumbnail = thumbnail(now.m_source + L'\n' + std::wstring{ info.Title() } + L'\n' + std::wstring{ info.Artist() },
                                    [&] { return read_thumbnail(info); });

        const auto timeline = session.GetTimelineProperties();

        /* Position is at LastUpdatedTime, not now (browsers only push it on play / pause / seek).
           zero or future stamp = never filled, treat as fresh. */
        auto age = winrt::clock::now() - timeline.LastUpdatedTime();
        if (timeline.LastUpdatedTime().time_since_epoch().count() == 0 || age.count() < 0) age = {};

        now.m_total_ms   = std::chrono::duration_cast<std::chrono::milliseconds>(timeline.EndTime() - timeline.StartTime()).count();
        now.m_current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeline.Position() - timeline.StartTime()).count();
        now.m_stamp -= std::chrono::duration_cast<std::chrono::steady_clock::duration>(age);

        if (const auto playback = session.GetPlaybackInfo()) {
            now.m_playing = playback.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            if (const auto rate = playback.PlaybackRate(); rate && rate.Value() > 0.0) now.m_rate = rate.Value();
        }

        publish(now);
    };

    const auto from_kopuz = [&](n_kopuz::state_t& kopuz) {
        now_t now{};
        now.m_source     = kopuz.m_app;
        now.m_kind       = source_kopuz;
        now.m_title      = std::move(kopuz.m_title);
        now.m_artist     = std::move(kopuz.m_artist);
        now.m_total_ms   = kopuz.m_total_ms;
        now.m_current_ms = kopuz.m_position_ms;
        now.m_playing    = kopuz.m_playing;
        now.m_thumbnail  = thumbnail(L"kopuz\n" + std::to_wstring(kopuz.m_art_version) + L'\n' + std::wstring(kopuz.m_art_request.begin(), kopuz.m_art_request.end()),
                                     [&] { return n_kopuz::get_artwork(kopuz.m_art_request); });
        publish(now);
    };

    const auto from_audio = [&]() {
        static std::wstring s_app{}, s_file{};
        static n_media_file::tags_t s_tags{};
        static std::chrono::steady_clock::time_point s_file_start{};
        static bool s_file_timed{}, s_watching{};
        static int s_quiet{};

        std::wstring path{}, window{};
        std::vector<std::wstring> files{};
        if (!find_audio_app(path, window, files)) {
            // paused from here: stays until the app closes, gap between songs: hold what's shown a few updates
            if (s_appPaused && !s_app.empty() && s_has && !window_title(s_app).empty()) return true;

            if (++s_quiet > 3) s_app.clear();
            return !s_app.empty() && s_has.load();
        }

        s_quiet = 0;

        // sounding again ( resumed in the app itself ). the pause needs a moment to go quiet, so not right after it
        if (s_appPaused && path == s_app && std::chrono::steady_clock::now() - s_appPausedAt > std::chrono::seconds(2)) s_appPaused = false;
        if (s_appPaused && path == s_app) return true;

        // two updates in a row, a notification ding is shorter
        if (path != s_app) {
            s_app      = path;
            s_watching = false;
            s_file.clear();
            return false;
        }

        /* the song file it has open beats the window title. one still open stays ( gapless players open the next early ).
           position only known for a file that showed up while watching */
        if (std::find(files.begin(), files.end(), s_file) == files.end()) {
            const std::wstring file = files.empty() ? std::wstring{} : files.front();
            s_file_timed = s_watching && !file.empty();
            s_file_start = std::chrono::steady_clock::now();
            s_file       = file;
            s_tags       = {};
            if (!file.empty()) n_media_file::read_tags(file, s_tags);
        }
        s_watching = true;

        std::wstring artist{}, title{};
        if (!s_file.empty()) {
            artist = s_tags.m_artist;
            title  = s_tags.m_title;
            if (title.empty()) split_title(s_file.substr(s_file.find_last_of(L"\\/") + 1), exe_stem(path), artist, title);
        } else
            split_title(window, exe_stem(path), artist, title);

        now_t now{};
        now.m_source  = path;
        now.m_kind    = source_app;
        now.m_title   = ToUtf8(title);
        now.m_artist  = ToUtf8(artist);
        now.m_playing = true;
        if (s_file_timed) {
            now.m_total_ms = s_tags.m_total_ms;
            now.m_stamp    = s_file_start;
        }
        now.m_thumbnail = thumbnail(s_file, [&] { return s_file.empty() ? std::vector<unsigned char>{} : n_media_file::read_cover(s_file); }, false);
        publish(now);
        return true;
    };

    /* playing beats paused, SMTC beats the rest. kopuz = own pipe api, its SMTC is broken.
       audible window ( vlc 3, winamp ) before anything paused */
    try {
        const auto session = pick_session(manager);
        if (session && is_playing(session)) return from_session(session);

        n_kopuz::state_t kopuz{};
        const bool has_kopuz = n_kopuz::get_state(kopuz);
        if (has_kopuz && kopuz.m_playing) return from_kopuz(kopuz);

        if (from_audio()) return;

        if (has_kopuz) return from_kopuz(kopuz);

        if (session) return from_session(session);

        clear();
    } catch (...) {
        clear();
    }
}

namespace
{
    bool session_command(const Manager& manager, const std::wstring& source, const std::wstring& title, const int command, bool& found)
    {
        // browsers share one id across tabs: the shown title picks the tab
        Session pick{ nullptr };
        for (const auto& session : manager.GetSessions()) {
            if (std::wstring_view{ session.SourceAppUserModelId() } != source) continue;

            const auto info = await_for(session.TryGetMediaPropertiesAsync());
            if (!pick || (info && std::wstring_view{ info.Title() } == title)) pick = session;
        }

        found = static_cast<bool>(pick);
        if (!pick) return false;

        const auto run = [](const auto& op) {
            if (op.wait_for(std::chrono::seconds(2)) == wf::AsyncStatus::Completed) return op.GetResults();
            op.Cancel();
            return false;
        };

        if (command == 1) return run(pick.TrySkipPreviousAsync());
        if (command == 3) return run(pick.TrySkipNextAsync());

        // some apps only expose play + pause, no toggle
        return run(pick.TryTogglePlayPauseAsync()) || run(is_playing(pick) ? pick.TryPauseAsync() : pick.TryPlayAsync());
    }

    // no SMTC ( vlc 3, winamp, mpc ): the player's own windows, the way a keyboard media key reaches them
    bool app_command(const std::wstring& path, const int command)
    {
        static constexpr short k_commands[]{ 0, APPCOMMAND_MEDIA_PREVIOUSTRACK, APPCOMMAND_MEDIA_PLAY_PAUSE, APPCOMMAND_MEDIA_NEXTTRACK };

        struct search_t {
            const std::wstring* m_path;
            std::vector<HWND> m_windows;
        } search{ &path, {} };

        EnumWindows(
            [](HWND window, LPARAM param) -> BOOL {
                auto& search = *reinterpret_cast<search_t*>(param);
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                if (IsWindowVisible(window) && !GetWindow(window, GW_OWNER) && pid != GetCurrentProcessId() && process_path(pid) == *search.m_path)
                    search.m_windows.push_back(window);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));

        for (const HWND window : search.m_windows) {
            DWORD_PTR handled = 0;
            if (SendMessageTimeoutW(window, WM_APPCOMMAND, reinterpret_cast<WPARAM>(window), MAKELPARAM(0, k_commands[command]), SMTO_ABORTIFHUNG, 500, &handled) &&
                handled)
                return true;
        }
        return false;
    }

    // last resort: a real media key, windows routes it to whatever answers media keys
    void media_key(const int command)
    {
        static constexpr WORD k_keys[]{ 0, VK_MEDIA_PREV_TRACK, VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK };

        INPUT input[2]{};
        for (int i = 0; i < 2; i++) {
            input[i].type       = INPUT_KEYBOARD;
            input[i].ki.wVk     = k_keys[command];
            input[i].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | (i ? KEYEVENTF_KEYUP : 0);
        }
        SendInput(2, input, sizeof(INPUT));
    }
}

// botox impl_t::run_command. true = ran one
static bool RunCommand(const Manager& manager)
{
    const int command = s_pendingCmd.exchange(0);
    if (command < 1 || command > 3) return false;

    std::wstring source{}, title{};
    Source kind{};
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        source = s_source;
        kind   = s_kind;
        title  = winrt::to_hstring(s_title).c_str();
    }

    static constexpr const char* k_kopuz[]{ "", "Previous", "Toggle", "Next" };

    try {
        bool found = false;
        const bool sent =
            (kind == source_session && (session_command(manager, source, title, command, found) || found)) ||
            (kind == source_kopuz && !n_kopuz::detail::call(k_kopuz[command], {}).empty()) || (kind == source_app && app_command(source, command));
        if (!sent) media_key(command);
    } catch (...) {
        media_key(command);
    }

    if (kind == source_app) {
        s_appPaused = command == 2 && s_playing;
        if (s_appPaused) {
            const auto frozen = (long long)position_ms();
            std::lock_guard<std::mutex> lock(s_mtx);
            s_curMs     = frozen;
            s_stamp     = std::chrono::steady_clock::now();
            s_appPausedAt = s_stamp;
            s_playing   = false;
        }
    }

    return true;
}

// botox main.cpp media_player_thread
static DWORD WINAPI MediaThread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    /* fresh thread, so it has no apartment yet - winrt throws CO_E_NOTINITIALIZED without this */
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}

    std::optional<Manager> manager;
    try { manager = Manager::RequestAsync().get(); } catch (...) {}
    if (!manager.has_value()) {
        DebugLog("media: failed to initialise media player");
        return 0;
    }

    while (!g_eject) {
        if (g_mpEnable) OnUpdate(*manager);

        // a control click refreshes 250 ms later, the app needs that long to switch track
        for (int i = 0; i < 20 && !g_eject; i++) {
            if (RunCommand(*manager)) {
                Sleep(250);
                break;
            }
            Sleep(50);
        }
    }

    // eject: drop the proxy on its own MTA thread while the process is healthy
    try { manager.reset(); } catch (...) {}
    return 0;
}

void MediaPlayer_Start()
{
    if (s_thread) return;
    s_thread = CreateThread(nullptr, 0, MediaThread, nullptr, 0, nullptr);
}

void MediaPlayer_Shutdown()
{
    if (s_thread) {
        // The loop tests g_eject at most 250ms apart, but a winrt call already in flight can
        // hold it longer -- 3s, then the handle goes and the thread dies with the process.
        WaitForSingleObject(s_thread, 3000);
        CloseHandle(s_thread);
        s_thread = nullptr;
    }
    MediaPlayer_ReleaseTexture();
}

// ==== render thread =============================================================================
void MediaPlayer_ReleaseTexture()
{
    if (s_tex) { s_tex->Release(); s_tex = nullptr; }
    // The decoded pixels outlive the texture, so a device change rebuilds from them rather than
    // waiting for the next track.
    std::lock_guard<std::mutex> lock(s_mtx);
    if (!s_px.empty()) s_pxDirty = true;
}

void MediaPlayer_Tick(IDirect3DDevice9* dev)
{
    if (!dev || !g_mpEnable) return;

    std::vector<unsigned char> px;
    int w = 0, h = 0;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        if (!s_pxDirty) return;
        s_pxDirty = false;
        px = s_px;            // copy: LockRect below must not hold the poll thread out
        w = s_pxW; h = s_pxH;
    }

    if (s_tex) { s_tex->Release(); s_tex = nullptr; }
    if (px.empty() || w <= 0 || h <= 0) return;

    // DYNAMIC + DEFAULT, the exact pair imgui_impl_dx9 uses for its own font atlas -- and that
    // atlas is proof the combination binds in both passes (the vgui one draws every string we
    // have). MANAGED was the bug: the engine's device is D3D9Ex, where the managed pool does
    // not exist, so CreateTexture just failed and the art was silently never there. Aletherium
    // and fpsoverlay both hit it and both moved off MANAGED.
    // DEFAULT dies on a device Reset, so h_Reset releases it and the pixels above rebuild it.
    HRESULT hr = dev->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                    D3DPOOL_DEFAULT, &s_tex, nullptr);
    if (FAILED(hr)) { DebugLog("media: CreateTexture %dx%d failed hr=0x%08X", w, h, (unsigned)hr); s_tex = nullptr; return; }

    D3DLOCKED_RECT lr{};
    hr = s_tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD);
    if (FAILED(hr)) { DebugLog("media: LockRect failed hr=0x%08X", (unsigned)hr); s_tex->Release(); s_tex = nullptr; return; }
    for (int y = 0; y < h; ++y)
        memcpy((unsigned char*)lr.pBits + (size_t)y * lr.Pitch, px.data() + (size_t)y * w * 4, (size_t)w * 4);
    s_tex->UnlockRect(0);
}

// ==== botox misc.cpp draw_media_player ==========================================================
// The poll runs once a second; age the stored position by the time since it so the bar slides
// instead of stepping. Same function the poll uses, with the track already resolved.
static mp::Track CurrentTrack()
{
    std::lock_guard<std::mutex> lock(s_mtx);
    const long long ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - s_stamp).count();
    return mp::TrackAt(0, s_totalMs, 0, 0, s_curMs, ageMs, s_rate, s_playing.load());
}

// g_utilities.truncate_utf8: cut after N codepoints, never inside one
static std::string TruncateUtf8(const std::string& s, size_t maxChars)
{
    size_t chars = 0, i = 0;
    for (; i < s.size(); ++i) {
        if (((unsigned char)s[i] & 0xc0) != 0x80 && chars++ == maxChars) break;
    }
    return s.substr(0, i);
}

// botox media_time_text: "4:11 / 6:09", hours once a track runs past one. empty = length unknown
static std::string TimeText(const mp::Track& t)
{
    if (t.totalMs <= 0) return {};
    const auto format = [](long long ms) {
        const long long s = ms / 1000;
        char out[32];
        if (s >= 3600) snprintf(out, sizeof(out), "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
        else           snprintf(out, sizeof(out), "%lld:%02lld", s / 60, s % 60);
        return std::string(out);
    };
    return format(std::clamp(t.curMs, 0LL, t.totalMs)) + " / " + format(t.totalMs);
}

// botox media_player_fonts: title (bold) / artist (regular) face per watermark style, plus the
// draw size where the donor's atlas px and draw px differ (interium 24 -> 16, illusory 13 ->
// 12, gamesense 15 -> 13). Default = the menu's verdana bold 11 / verdana 11.
static void StyleFonts(int style, ImFont*& title, ImFont*& artist, float& size)
{
    title = artist = nullptr; size = 0.f;
    const auto pick = [&](ImFont* b, ImFont* r) { title = b; artist = r; };
    switch (style) {
    case 1:  pick(g_fontKamibebraBold13, g_fontKamibebra13); break;
    case 3:  pick(g_fontClarityInter, g_fontClarityInter); break;
    case 4:
    case 7:  pick(g_fontTahoma12, g_fontTahoma12); break;
    case 5:  pick(g_fontInterwebzCalibri, g_fontInterwebzCalibri); break;
    case 6:  pick(g_fontHavocTahoma13, g_fontHavocTahoma13); break;
    case 8:  pick(g_fontAirflow14, g_fontAirflow14); break;
    case 9:  pick(g_fontEvolveBold16, g_fontEvolve16); break;
    case 10: pick(g_fontLegendwareVerdana12, g_fontLegendwareVerdana12); break;
    case 11: pick(g_fontInteriumDroid24, g_fontInteriumDroid24); size = 16.f; break;
    case 12: pick(g_fontSkebobArial12, g_fontSkebobArial12); break;
    case 13: pick(g_fontCumidereTahoma16, g_fontCumidereTahoma16); break;
    case 14: pick(g_fontIllusoryTahoma13, g_fontIllusoryTahoma13); size = 12.f; break;
    case 15: pick(g_fontLumiRubik16, g_fontLumiRubik16); break;
    case 16: pick(g_fontTahomaBd12, g_fontTahomaBd12); break;
    case 17: pick(g_fontDnaMontserrat15, g_fontDnaMontserrat15); break;
    case 18: pick(g_fontDnaPtRootBold15, g_fontDnaPtRootBold15); break;
    case 19: pick(g_fontCucumberTahoma14, g_fontCucumberTahoma14); break;
    case 20: pick(g_fontHowewewareMinecraft14, g_fontHowewewareMinecraft14); break;
    case 21: pick(g_fontGamesenseSfBold15, g_fontGamesenseSfBold15); size = 13.f; break;
    default: break;
    }
    if (!title || !artist) {
        pick(g_menuBold ? g_menuBold : ImGui::GetFont(), g_menuReg ? g_menuReg : ImGui::GetFont());
        size = 0.f;
    }
}

void RenderMediaPlayer()
{
    if (!g_mpEnable || !s_has.load()) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.f || io.DisplaySize.y <= 0.f) return;

    std::string title, artist;
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        title = s_title; artist = s_artist;
    }
    title  = TruncateUtf8(title, 64);
    artist = TruncateUtf8(artist, 64);
    if (title.empty()) return;

    const int style = g_wmStyle;
    ImFont* titleFont; ImFont* artistFont; float sizeOverride;
    StyleFonts(style, titleFont, artistFont, sizeOverride);
    const float titlePx  = sizeOverride > 0.f ? sizeOverride : titleFont->FontSize;
    const float artistPx = sizeOverride > 0.f ? sizeOverride : artistFont->FontSize;

    // botox position 0..5 = tr/tl/tc/bc/bl/br -> align (0 left, 1 centre, 2 right) + from_bottom.
    // Ours is the WatermarkPos enum, same six corners.
    const bool fromBottom = g_mpPos >= WM_BOTTOM_LEFT;
    const int align = (g_mpPos == WM_TOP_LEFT || g_mpPos == WM_BOTTOM_LEFT) ? 0
                    : (g_mpPos == WM_TOP_CENTER || g_mpPos == WM_BOTTOM_CENTER) ? 1 : 2;
    const float screenW = io.DisplaySize.x, screenH = io.DisplaySize.y, midX = screenW * 0.5f;
    const ImVec4 wm = g_wmBox;
    const bool wmColumn = wm.z > wm.x && wm.y < screenH * 0.5f &&
                          (align == 2 ? wm.z > midX : align == 0 ? wm.x < midX : wm.x < midX && wm.z > midX);
    const bool underWm = !fromBottom && wmColumn;
    const float anchorX = align == 1 ? midX : align == 2 ? (underWm ? wm.z : screenW - 8.f) : (underWm ? wm.x : 8.f);
    const float anchorY = fromBottom ? screenH - 8.f : underWm ? wm.w + 4.f : 8.f;
    const auto leftOf = [&](float w) { return align == 0 ? anchorX : align == 1 ? anchorX - w * 0.5f : anchorX - w; };

    const MediaLook look  = Watermark_MediaLook(style);
    const bool background = g_mpBackground;
    const int frameStyle  = look.box ? style : 0;
    const int bareFx      = look.fx == 2 || look.fx == 3 ? look.fx : 1;
    const int fx          = background ? look.fx : bareFx;

    static constexpr ImVec2 outline[8] = { { -1.f, -1.f }, { 0.f, -1.f }, { 1.f, -1.f }, { -1.f, 0.f },
                                           { 1.f, 0.f },   { -1.f, 1.f }, { 0.f, 1.f },  { 1.f, 1.f } };
    const auto put = [](ImDrawList* list, int effect, ImFont* font, float px, ImVec2 pos, ImU32 color, const std::string& text) {
        const ImU32 black = color & IM_COL32_A_MASK;
        if (effect == 1)      list->AddText(font, px, ImVec2(pos.x + 1.f, pos.y + 1.f), black, text.c_str());
        else if (effect == 3) list->AddText(font, px, ImVec2(pos.x - 1.f, pos.y + 1.f), black, text.c_str());
        else if (effect == 2) for (const auto& o : outline) list->AddText(font, px, ImVec2(pos.x + o.x, pos.y + o.y), black, text.c_str());
        list->AddText(font, px, pos, color, text.c_str());
    };

    constexpr float artSize = 30.f, barHeight = 1.f, rowGap = 4.f;

    // prev / play-pause / next while the menu is open. drawn last: the hit test needs the whole
    // block's bounds
    const mp::Track track   = CurrentTrack();
    const bool buttons      = g_showMenu;
    const std::string timeNow = g_mpTime ? TimeText(track) : std::string{};
    const float icon        = std::clamp(floorf(artistPx * 0.7f), 7.f, 14.f);
    const float cell        = floorf(icon * 1.6f), cellGap = floorf(icon * 0.4f);
    const float buttonsW    = buttons ? cell * 3.f + cellGap * 2.f : 0.f;
    const ImU32 accent      = ImGui::GetColorU32(ImGuiCol_Accent);

    const auto drawButtons = [&](ImDrawList* list, float x, float midY, int effect) {
        const ImVec2 at = io.MousePos;
        const bool live  = !GImGui->HoveredWindow;   // a menu window over the player eats the click
        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const ImU32 hoverColor = look.title != look.artist ? look.title : accent;
        const float h = icon * 0.5f;
        static constexpr int cmds[3] = { 2, 0, 1 };   // MediaPlayer_RequestControl: 0 toggle, 1 next, 2 previous

        for (int b = 0; b < 3; b++) {
            const float cx = x + (float)b * (cell + cellGap) + cell * 0.5f;
            const bool hovered = live && fabsf(at.x - cx) <= cell * 0.5f + 1.f && fabsf(at.y - midY) <= cell * 0.5f + 1.f;
            if (hovered && click) MediaPlayer_RequestControl(cmds[b]);

            // clockwise on screen, the aa fringe goes outward
            const auto shape = [&](float sx, float sy, ImU32 color) {
                if (b == 0) {
                    list->AddTriangleFilled(ImVec2(sx - h, sy), ImVec2(sx, sy - h), ImVec2(sx, sy + h), color);
                    list->AddTriangleFilled(ImVec2(sx, sy), ImVec2(sx + h, sy - h), ImVec2(sx + h, sy + h), color);
                } else if (b == 2) {
                    list->AddTriangleFilled(ImVec2(sx + h, sy), ImVec2(sx, sy + h), ImVec2(sx, sy - h), color);
                    list->AddTriangleFilled(ImVec2(sx, sy), ImVec2(sx - h, sy + h), ImVec2(sx - h, sy - h), color);
                } else if (s_playing.load()) {
                    list->AddRectFilled(ImVec2(sx - h * 0.8f, sy - h), ImVec2(sx - h * 0.2f, sy + h), color);
                    list->AddRectFilled(ImVec2(sx + h * 0.2f, sy - h), ImVec2(sx + h * 0.8f, sy + h), color);
                } else
                    list->AddTriangleFilled(ImVec2(sx - h * 0.7f, sy - h), ImVec2(sx + h * 0.9f, sy), ImVec2(sx - h * 0.7f, sy + h), color);
            };

            const ImU32 color = hovered ? hoverColor : look.artist;
            if (effect) shape(cx + (effect == 3 ? -1.f : 1.f), midY + 1.f, color & IM_COL32_A_MASK);
            shape(cx, midY, color);
        }
    };

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (g_mpSimple) {
        std::string line = artist.empty() ? title : title + " - " + artist;
        if (!timeNow.empty()) line += "  " + timeNow;
        const ImVec2 lineSize = artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, line.c_str());
        const float totalW = lineSize.x + (buttons ? cellGap * 2.f + buttonsW : 0.f);

        const float lineY = fromBottom ? anchorY - lineSize.y : anchorY;
        const float lineX = floorf(leftOf(totalW));
        put(dl, bareFx, artistFont, artistPx, ImVec2(lineX, lineY), look.title, line);
        if (buttons) drawButtons(dl, lineX + lineSize.x + cellGap * 2.f, floorf(lineY + lineSize.y * 0.5f), bareFx);
        return;
    }

    const ImVec2 titleSize  = titleFont->CalcTextSizeA(titlePx, FLT_MAX, 0.f, title.c_str());
    const ImVec2 artistSize = artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, artist.c_str());

    const float padding  = background ? 10.f : 0.f;
    const float minWidth = background ? 140.f : 0.f;
    const bool progressBar = g_mpProgressBar;
    const bool art = g_mpCoverArt && s_tex;
    const float artGap = (std::max)(padding, 6.f);
    const float artOffsetX = art ? artSize + artGap : 0.f;

    const float textColW = (std::max)(titleSize.x, artistSize.x);
    const float textColH = titleSize.y + rowGap + artistSize.y;
    const float contentH = art ? (std::max)(artSize, textColH) : textColH;

    const float timeW = timeNow.empty() ? 0.f : artistFont->CalcTextSizeA(artistPx, FLT_MAX, 0.f, timeNow.c_str()).x;
    const bool controlRow = buttons || timeW > 0.f;
    const float controlH  = controlRow ? (std::max)(artistSize.y, cell) : 0.f;
    const float controlW  = buttonsW + timeW + (buttons && timeW > 0.f ? 8.f : 0.f);

    const float width  = (std::max)({ textColW + artOffsetX + padding * 2.f, controlW + padding * 2.f, minWidth });
    const float height = padding + contentH + (progressBar ? rowGap + barHeight : 0.f) + (controlRow ? rowGap + controlH : 0.f) + padding;

    const float posX = floorf(leftOf(width));
    const float posY = fromBottom ? anchorY - height : anchorY;

    if (background) Watermark_MediaFrame(dl, ImVec2(posX, posY), ImVec2(posX + width, posY + height), frameStyle);

    const bool leftSide = align == 0;
    const float artX = leftSide ? posX + padding : posX + width - padding - artSize;
    const float artY = posY + padding + (contentH - artSize) * 0.5f;
    if (art)
        dl->AddImageRounded((ImTextureID)s_tex, ImVec2(artX, artY), ImVec2(artX + artSize, artY + artSize), ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
                            IM_COL32_WHITE, 2.f);

    const float textLeft  = art ? artX + artSize + artGap : posX + padding;
    const float textRight = art ? artX - artGap : posX + width - padding;
    const float textTop   = posY + padding + (contentH - textColH) * 0.5f;

    const ImVec2 titlePos(leftSide ? textLeft : textRight - titleSize.x, textTop);
    const ImVec2 artistPos(leftSide ? textLeft : textRight - artistSize.x, textTop + titleSize.y + rowGap);

    put(dl, fx, titleFont, titlePx, titlePos, look.title, title);
    put(dl, fx, artistFont, artistPx, artistPos, look.artist, artist);

    if (progressBar) {
        const float barX = posX + padding;
        const float barY = posY + padding + contentH + rowGap;
        const float barWidth = posX + width - padding - barX;
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight), IM_COL32(60, 60, 60, 255), 1.f);
        dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth * mp::ProgressOf(track), barY + barHeight), accent, 1.f);
    }

    if (controlRow) {
        const float rowY = posY + height - padding - controlH;
        if (timeW > 0.f) {
            const float timeX = leftSide ? posX + width - padding - timeW : posX + padding;
            put(dl, fx, artistFont, artistPx, ImVec2(floorf(timeX), floorf(rowY + (controlH - artistSize.y) * 0.5f)), look.artist, timeNow);
        }
        if (buttons)
            drawButtons(dl, leftSide ? posX + padding : posX + width - padding - buttonsW, floorf(rowY + controlH * 0.5f), fx);
    }
}

bool   MediaPlayer_HasSession()    { return g_mpEnable && s_has.load(); }
// 0 toggle / 1 next / 2 previous -> botox e_command 2 / 3 / 1
void   MediaPlayer_RequestControl(int cmd)
{
    static constexpr int k_map[3] = { 2, 3, 1 };
    if (cmd >= 0 && cmd <= 2) s_pendingCmd = k_map[cmd];
}
