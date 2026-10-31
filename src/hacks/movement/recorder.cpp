#include "recorder.h"
#include "prediction.h"   // EntV3
#include <direct.h>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <filesystem>

// local IN_* copies -- same confirmed bit values movement.cpp defines file-local
static constexpr int IN_JUMP      = 1 << 1;
static constexpr int IN_DUCK      = 1 << 2;
static constexpr int IN_FORWARD   = 1 << 3;
static constexpr int IN_BACK      = 1 << 4;
static constexpr int IN_MOVELEFT  = 1 << 9;
static constexpr int IN_MOVERIGHT = 1 << 10;
static constexpr int kWasdMask = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_JUMP;

// IVEngineClient::GetLevelName, slot 51 -- same confirmed slot psassist.inl's EngineLevelName
// uses (anchored against Time=14 / SetViewAngles=20). Duplicated here rather than exported:
// psassist.inl's copy is file-local static, same as this one.
static const char* RecLevelName()
{
    if (!g_engine) return "";
    const char* n = nullptr;
    __try {
        void** vt = *(void***)g_engine;
        n = ((const char*(__fastcall*)(void*))vt[51])(g_engine);
    } __except (EXCEPTION_EXECUTE_HANDLER) { n = nullptr; }
    return n ? n : "";
}

// m_hMyWeapons[slot] -> EHANDLE; & 0xFFFF is this codebase's own confirmed x64 decode
// (psassist.inl AirDash's Atomizer check uses the same mask). Index IS the weapon slot.
static int ActiveWeaponSlot(void* pLocal)
{
    if (g_offMyWeapons <= 0 || g_offActiveWeapon <= 0) return -1;
    const unsigned active = *(unsigned*)((uintptr_t)pLocal + g_offActiveWeapon);
    if (active == 0xFFFFFFFF) return -1;
    for (int i = 0; i < 48; ++i)
    {
        const unsigned h = *(unsigned*)((uintptr_t)pLocal + g_offMyWeapons + i * 4);
        if (h == active) return i;
    }
    return -1;
}

// selects the weapon sitting in m_hMyWeapons[slot] for the NEXT cmd -- pCmd->weaponselect is
// the standard CUserCmd field the engine reads it off (same struct this port already verified
// for everything else in UserCmd_t, not a new assumption).
static void ForceWeaponSlot(void* pLocal, UserCmd_t* pCmd, int slot)
{
    if (g_offMyWeapons <= 0 || slot < 0 || slot >= 48) return;
    const unsigned h = *(unsigned*)((uintptr_t)pLocal + g_offMyWeapons + slot * 4);
    const int idx = (int)(h & 0xFFFF);
    if (h == 0xFFFFFFFF || idx < 1 || idx >= 2048) return;
    pCmd->weaponselect = idx;
}

struct RecClip { std::string file; std::string name; };

static std::mutex              s_lock;
static bool                    s_recording = false;
static bool                    s_playing   = false;
static bool                    s_approaching = false;
static std::vector<RecFrame>   s_recordBuf;
static std::vector<RecFrame>   s_playBuf;
static size_t                  s_playIdx   = 0;
static int                     s_trimStart = 0, s_trimEnd = 0;
static std::vector<RecClip>    s_clips;
static int                     s_selected  = -1;
static std::string             s_currentMap;

static std::string SafeName(const char* s)
{
    std::string out = (s && s[0]) ? s : "unknown";
    for (char& c : out) if (c == '/' || c == '\\' || c == ':') c = '_';
    return out;
}

static std::string ClipPath(const std::string& map, const std::string& name)
{
    return std::string(kRecorderDir) + "\\" + SafeName(map.c_str()) + "__" + SafeName(name.c_str()) + ".rec";
}

// ---- menu-facing controls ----------------------------------------------------------------

void Recorder_StartRecord()
{
    std::lock_guard<std::mutex> l(s_lock);
    if (s_playing) return;   // one owner of the cmd at a time
    s_recordBuf.clear();
    s_recording = true;
    DebugLog("REC: record start");
}

void Recorder_StopRecord()
{
    std::lock_guard<std::mutex> l(s_lock);
    s_recording = false;
    s_trimStart = 0;
    s_trimEnd   = (int)s_recordBuf.size();
    DebugLog("REC: record stop (%d frames)", (int)s_recordBuf.size());
}

void Recorder_StartPlay()
{
    std::lock_guard<std::mutex> l(s_lock);
    if (s_recording || s_selected < 0 || s_selected >= (int)s_clips.size()) return;
    std::vector<uint8_t> bytes;
    FILE* f = fopen(s_clips[s_selected].file.c_str(), "rb");
    if (!f) { DebugLog("REC: play failed, cannot open %s", s_clips[s_selected].file.c_str()); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }
    bytes.resize((size_t)sz);
    fread(bytes.data(), 1, (size_t)sz, f);
    fclose(f);
    if (!RecDeserialize(bytes, s_playBuf) || s_playBuf.empty())
    {
        DebugLog("REC: play failed, bad clip data %s", s_clips[s_selected].file.c_str());
        return;
    }
    s_playIdx = 0;
    s_playing = true;
    s_approaching = g_recLockGoingToStart;
    DebugLog("REC: play start (%d frames)", (int)s_playBuf.size());
}

void Recorder_StopPlay()
{
    std::lock_guard<std::mutex> l(s_lock);
    s_playing = false;
    s_approaching = false;
}

void Recorder_Clear()
{
    std::lock_guard<std::mutex> l(s_lock);
    s_recordBuf.clear();
    s_trimStart = s_trimEnd = 0;
}

bool Recorder_IsRecording() { std::lock_guard<std::mutex> l(s_lock); return s_recording; }
bool Recorder_IsPlaying()   { std::lock_guard<std::mutex> l(s_lock); return s_playing; }
int  Recorder_FrameCount()  { std::lock_guard<std::mutex> l(s_lock); return (int)s_recordBuf.size(); }

void Recorder_Refresh()
{
    std::lock_guard<std::mutex> l(s_lock);
    s_currentMap = RecLevelName();
    s_clips.clear();
    s_selected = -1;
    _mkdir(kRecorderDir);
    const std::string prefix = SafeName(s_currentMap.c_str()) + "__";
    std::error_code ec;
    if (!std::filesystem::exists(kRecorderDir, ec)) return;
    for (const auto& ent : std::filesystem::directory_iterator(kRecorderDir, ec))
    {
        if (ec || !ent.is_regular_file()) continue;
        const std::string fn = ent.path().filename().string();
        if (fn.rfind(prefix, 0) != 0) continue;              // not this map
        if (fn.size() < 4 || fn.substr(fn.size() - 4) != ".rec") continue;
        RecClip c;
        c.file = ent.path().string();
        c.name = fn.substr(prefix.size(), fn.size() - prefix.size() - 4);
        s_clips.push_back(std::move(c));
    }
    std::sort(s_clips.begin(), s_clips.end(), [](const RecClip& a, const RecClip& b) { return a.name < b.name; });
}

int Recorder_ClipCount() { std::lock_guard<std::mutex> l(s_lock); return (int)s_clips.size(); }

const char* Recorder_ClipName(int index)
{
    std::lock_guard<std::mutex> l(s_lock);
    if (index < 0 || index >= (int)s_clips.size()) return "";
    return s_clips[index].name.c_str();
}

void Recorder_Save(const char* name)
{
    std::lock_guard<std::mutex> l(s_lock);
    if (!name || !name[0] || s_recordBuf.empty()) return;
    const int start = std::clamp(s_trimStart, 0, (int)s_recordBuf.size());
    const int end   = std::clamp(s_trimEnd > 0 ? s_trimEnd : (int)s_recordBuf.size(), start, (int)s_recordBuf.size());
    if (end <= start) return;

    std::vector<RecFrame> trimmed(s_recordBuf.begin() + start, s_recordBuf.begin() + end);
    std::vector<uint8_t> bytes;
    if (!RecSerialize(trimmed, bytes)) { DebugLog("REC: save failed, serialize"); return; }

    _mkdir(kRecorderDir);
    const std::string map = RecLevelName();
    const std::string path = ClipPath(map, name);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { DebugLog("REC: save failed, cannot open %s", path.c_str()); return; }
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    DebugLog("REC: saved %s (%d frames)", path.c_str(), (int)trimmed.size());
}

void Recorder_Delete(int index)
{
    std::lock_guard<std::mutex> l(s_lock);
    if (index < 0 || index >= (int)s_clips.size()) return;
    remove(s_clips[index].file.c_str());
    s_clips.erase(s_clips.begin() + index);
    if (s_selected == index) s_selected = -1;
    else if (s_selected > index) --s_selected;
}

void Recorder_SetSelected(int index) { std::lock_guard<std::mutex> l(s_lock); s_selected = index; }
int  Recorder_GetSelected()          { std::lock_guard<std::mutex> l(s_lock); return s_selected; }

bool Recorder_Load(int) { return true; } // StartPlay() reads s_selected directly; kept for API symmetry

void Recorder_SetTrim(int start, int end)
{
    std::lock_guard<std::mutex> l(s_lock);
    s_trimStart = start; s_trimEnd = end;
}

void Recorder_GetTrim(int& start, int& end, int& maxTicks)
{
    std::lock_guard<std::mutex> l(s_lock);
    start = s_trimStart; end = s_trimEnd; maxTicks = (int)s_recordBuf.size();
}

// ---- game thread --------------------------------------------------------------------------

void Recorder_OnCreateMove(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_recEnable || !pLocal || !pCmd) return;

    // raw input, read before anything below (recorder or any later feature) touches the cmd --
    // stop-on-move has to see the player's OWN press, not a press the recorder itself injected
    const int rawButtons = pCmd->buttons;

    // record/play binds -- same toggle/hold/always-on runtime every other feature's bind uses
    static bool s_recKeyWasDown = false, s_playKeyWasDown = false;
    const bool recKeyDown  = MovementKeyOK(g_recRecordKey, g_recRecordMode);
    const bool playKeyDown = MovementKeyOK(g_recPlayKey, g_recPlayMode);
    if (recKeyDown && !s_recKeyWasDown) { if (Recorder_IsRecording()) Recorder_StopRecord(); else Recorder_StartRecord(); }
    if (playKeyDown && !s_playKeyWasDown) { if (Recorder_IsPlaying()) Recorder_StopPlay(); else Recorder_StartPlay(); }
    s_recKeyWasDown = recKeyDown;
    s_playKeyWasDown = playKeyDown;

    std::lock_guard<std::mutex> l(s_lock);

    if (s_recording)
    {
        RecFrame f{};
        f.pitch = pCmd->viewangles.x; f.yaw = pCmd->viewangles.y;
        f.forward = pCmd->forwardmove; f.side = pCmd->sidemove; f.up = pCmd->upmove;
        f.buttons = pCmd->buttons; f.mouseDx = pCmd->mousedx; f.mouseDy = pCmd->mousedy;
        f.weaponSlot = ActiveWeaponSlot(pLocal);
        const Vec3 pos = g_offOrigin   > 0 ? EntV3(pLocal, g_offOrigin)   : Vec3();
        const Vec3 vel = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity) : Vec3();
        f.posX = pos.x; f.posY = pos.y; f.posZ = pos.z;
        f.velX = vel.x; f.velY = vel.y; f.velZ = vel.z;
        s_recordBuf.push_back(f);
        if ((int)s_recordBuf.size() >= kRecMaxTick) { s_recording = false; DebugLog("REC: record cap hit"); }
    }

    if (!s_playing) return;
    if (s_playBuf.empty()) { s_playing = false; return; }

    if (!s_approaching && g_recStopOnMove && (rawButtons & kWasdMask) != 0)
    {
        s_playing = false;
        DebugLog("REC: playback cancelled, manual input");
        return;
    }

    if (s_approaching)
    {
        const Vec3 pos = g_offOrigin > 0 ? EntV3(pLocal, g_offOrigin) : Vec3();
        const RecFrame& start = s_playBuf.front();
        Vec3 to(start.posX - pos.x, start.posY - pos.y, 0.f);
        const float dist = to.Length2D();
        if (dist <= g_recApproachDist)
        {
            s_approaching = false;   // close enough -- fall through to replay this same tick
        }
        else
        {
            const float yaw = atan2f(to.y, to.x) * (180.f / MV_PI);
            if (g_recLockVa) { pCmd->viewangles.x = 0.f; pCmd->viewangles.y = yaw; }
            const float yawRad = DEG2RADF(pCmd->viewangles.y);
            const Vec3 fwd(cosf(yawRad), sinf(yawRad), 0.f), right(-sinf(yawRad), cosf(yawRad), 0.f);
            const float f = to.Dot(fwd), s = to.Dot(right);
            const float norm = std::max(fabsf(f), fabsf(s));
            const float scale = norm > 1.f ? 1.f / norm : 1.f;
            pCmd->forwardmove = f * scale * 450.f;
            pCmd->sidemove    = s * scale * 450.f;
            pCmd->upmove      = 0.f;
            pCmd->buttons &= ~IN_JUMP;
            return;
        }
    }

    const RecFrame& fr = s_playBuf[s_playIdx];
    if (g_recLockVa) { pCmd->viewangles.x = fr.pitch; pCmd->viewangles.y = fr.yaw; }
    pCmd->forwardmove = fr.forward; pCmd->sidemove = fr.side; pCmd->upmove = fr.up;
    pCmd->buttons   = fr.buttons;
    pCmd->mousedx   = fr.mouseDx;
    pCmd->mousedy   = fr.mouseDy;
    if (g_recForceWeapon) ForceWeaponSlot(pLocal, pCmd, fr.weaponSlot);

    ++s_playIdx;
    if (s_playIdx >= s_playBuf.size()) { s_playing = false; DebugLog("REC: playback finished"); }
}

void Recorder_RenderStatus()
{
    bool rec, play, appr;
    size_t frames;
    { std::lock_guard<std::mutex> l(s_lock); rec = s_recording; play = s_playing; appr = s_approaching; frames = s_recordBuf.size(); }
    if (!rec && !play) return;

    char text[64];
    if (rec) snprintf(text, sizeof(text), "REC %d", (int)frames);
    else     snprintf(text, sizeof(text), appr ? "REC: approaching" : "REC: playing");

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* font = g_fontBold ? g_fontBold : ImGui::GetFont();
    const float fs = font->FontSize;
    const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
    const ImVec2 p(roundf(io.DisplaySize.x * 0.5f - sz.x * 0.5f), 8.f);
    const ImU32 col = rec ? IM_COL32(255, 70, 70, 255) : IM_COL32(255, 255, 255, 255);
    dl->AddText(font, fs, ImVec2(p.x + 1.f, p.y + 1.f), IM_COL32(0, 0, 0, 255), text);
    dl->AddText(font, fs, p, col, text);
}
