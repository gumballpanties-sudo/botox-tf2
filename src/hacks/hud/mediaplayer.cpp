#include "mediaplayer.h"
#include "watermark.h"          // g_wmBox -- botox anchors the player under the watermark
#include "watermark_styles.h"   // Watermark_MediaLook / Watermark_MediaFrame -- the style's colours + box
#include "mp_art.h"      // cover art scale + centre crop (gate tested, no winrt in it)
#include "mp_time.h"     // timeline -> position + length (gate tested, no winrt in it)
#include "../../dependencies/imgui/imgui_internal.h"   // GImGui->HoveredWindow: a menu window over the player eats the click

// botox csgo-sdk/globals/media_player + misc.cpp draw_media_player, ported: the winrt half is
// the same GlobalSystemMediaTransportControlsSessionManager poll on a background thread,
// because those calls block for milliseconds and must never sit on the render thread.
//
// ONE CHANGE from the original. The thumbnail is decoded HERE (winrt BitmapDecoder ->
// SoftwareBitmap Bgra8) instead of by D3DXCreateTextureFromFileInMemory: this dll links d3d9
// only, and pulling in the legacy d3dx9 redist for one PNG decode is a dependency for nothing.
// The render thread then does the part that must be its: CreateTexture + LockRect + a row copy.
// The panel itself is botox's: fonts, colours and frame follow the watermark style (g_wmStyle),
// buttons show while the menu is open. Lyrics + waveform visualizer are not ported (no lyrics
// fetch, no audio capture thread here).
#pragma comment(lib, "windowsapp")

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace wf  = winrt::Windows::Foundation;
namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;

// ==== shared state ==============================================================================
// Written by the poll thread, read by the render thread. Everything but the two atomics is
// under s_mtx; the copies taken out of it are small (two short strings, one pixel vector that
// only moves on a track change).
static std::mutex s_mtx;
static std::string s_title, s_artist;

static std::vector<unsigned char> s_px;        // BGRA8, s_pxW * s_pxH * 4
static int  s_pxW = 0, s_pxH = 0;
static bool s_pxDirty = false;                 // -> the render thread rebuilds the texture

static long long s_totalMs = 0, s_curMs = 0;
static double s_rate = 1.0;                    // playback speed, for the render-side slide
static std::chrono::steady_clock::time_point s_stamp{};

static std::atomic<bool> s_has{ false };       // a session with a title is up
static std::atomic<bool> s_playing{ false };

// unit E (media_ex.cpp): in-game transport control. Poll thread picks this up and clears it
// back to -1 -- a request that lands between polls waits at most one poll interval (1s).
static std::atomic<int>  s_pendingCmd{ -1 };

// Render thread only.
static IDirect3DTexture9* s_tex = nullptr;

static HANDLE s_thread = nullptr;

// ==== poll thread ===============================================================================
static std::string ToUtf8(const winrt::hstring& text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out((size_t)size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size, nullptr, nullptr);
    return out;
}

// Thumbnail stream -> straight-alpha BGRA8 bytes, always kArt x kArt.
//
// Straight and not premultiplied: ImGui's dx9 backend blends SRCALPHA/INVSRCALPHA, which is
// the straight-alpha equation.
//
// The decoder does the scale AND the crop (mp_art.h, gate test tests/mediaplayer_test.cpp).
// 64 because it is a power of two: a d3d9 device that only advertises
// D3DPTEXTURECAPS_NONPOW2CONDITIONAL has rules about the rest (no wrap addressing, no mips),
// and the cheapest way not to care is to never hand it a non-pow2 texture.
static constexpr uint32_t kArt = 64;

static bool DecodeThumb(const wss::IRandomAccessStreamWithContentType& stream,
                        std::vector<unsigned char>& out, int& w, int& h)
{
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
}

static void PollOnce(wmc::GlobalSystemMediaTransportControlsSessionManager& mgr)
{
    try {
        const auto session = mgr.GetCurrentSession();
        if (session == nullptr) { s_has = false; s_playing = false; return; }

        // unit E in-game media control: fire-and-forget, errors ignored -- a control call on a
        // session with nothing playing (e.g. a browser tab with no active media) is a no-op
        // the transport API already handles, not this thread's problem.
        const int cmd = s_pendingCmd.exchange(-1);
        if (cmd == 0)      { try { session.TryTogglePlayPauseAsync().get(); } catch (...) {} }
        else if (cmd == 1) { try { session.TrySkipNextAsync().get(); } catch (...) {} }
        else if (cmd == 2) { try { session.TrySkipPreviousAsync().get(); } catch (...) {} }

        const auto info = session.TryGetMediaPropertiesAsync().get();

        std::string title  = ToUtf8(info.Title());
        std::string artist = ToUtf8(info.Artist());

        // Decode on a TRACK CHANGE only. This runs once a second and the decode plus the texture
        // upload behind it are the expensive half; the track name is what the art belongs to, so
        // it is the key. A failed decode does not take the key, so the next poll retries it.
        std::vector<unsigned char> px;
        int pw = 0, ph = 0;
        bool haveArt = false, dropArt = false;
        static std::string s_artKey;   // poll thread only
        const std::string artKey = title + "\n" + artist;
        if (!info.Thumbnail()) {
            // New track with no art at all: the last track's cover must not stay on screen.
            dropArt = !s_artKey.empty();
            s_artKey.clear();
        } else if (artKey != s_artKey) {
            const auto stream = info.Thumbnail().OpenReadAsync().get();
            haveArt = DecodeThumb(stream, px, pw, ph);
            if (haveArt) s_artKey = artKey;
            else         DebugLog("media: thumbnail decode failed");
        }

        bool playing = false;
        double rate = 1.0;
        if (const auto playback = session.GetPlaybackInfo()) {
            playing = playback.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            if (const auto r = playback.PlaybackRate()) rate = r.Value();   // IReference, often null
        }

        // Position is dated LastUpdatedTime, NOT the moment we read it -- see mp_time.h. Age the
        // snapshot by hand or the bar snaps back to the last push every poll.
        const auto timeline = session.GetTimelineProperties();
        long long ageMs = 0;
        if (const auto lastUpd = timeline.LastUpdatedTime(); lastUpd.time_since_epoch().count() > 0)
            ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(winrt::clock::now() - lastUpd).count();

        const auto ms = [](const wf::TimeSpan& t) {
            return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t).count(); };
        const mp::Track track = mp::TrackAt(ms(timeline.StartTime()), ms(timeline.EndTime()),
                                            ms(timeline.MinSeekTime()), ms(timeline.MaxSeekTime()),
                                            ms(timeline.Position()), ageMs, rate, playing);

        {
            std::lock_guard<std::mutex> lock(s_mtx);
            s_title  = title;
            s_artist = artist;
            if (haveArt) {
                s_px = std::move(px);
                s_pxW = pw; s_pxH = ph;
                s_pxDirty = true;
            } else if (dropArt) {
                s_px.clear();
                s_pxW = s_pxH = 0;
                s_pxDirty = true;   // the tick below releases the texture and builds nothing
            }
            s_totalMs = track.totalMs;
            s_curMs   = track.curMs;
            s_rate    = rate;
            s_stamp   = std::chrono::steady_clock::now();
        }

        s_playing = playing;
        s_has     = !title.empty();
    } catch (...) {
        s_has = false;
        s_playing = false;
    }
}

static DWORD WINAPI MediaThread(LPVOID)
{
    // Nothing in the game waits on this, and every call in it blocks -- same priority the other
    // background workers run at so it takes a spare core instead of an engine thread's.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    // Fresh thread has no apartment; winrt throws CO_E_NOTINITIALIZED without this.
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}

    std::optional<wmc::GlobalSystemMediaTransportControlsSessionManager> mgr;

    while (!g_eject) {
        if (g_mpEnable) {
            if (!mgr.has_value()) {
                // Deferred, not done at load: RequestAsync is the slowest call here and a user
                // who never turns the feature on should never pay it.
                try { mgr = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get(); }
                catch (...) { mgr.reset(); }
                if (!mgr.has_value()) {
                    DebugLog("media: session manager unavailable");
                    for (int i = 0; i < 20 && !g_eject; ++i) Sleep(250);   // 5s before retrying
                    continue;
                }
            }
            PollOnce(*mgr);
        }

        // 1s of poll interval, checked for eject 4x inside it.
        for (int i = 0; i < 4 && !g_eject; ++i) Sleep(250);
    }
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
void   MediaPlayer_RequestControl(int cmd) { s_pendingCmd = cmd; }
