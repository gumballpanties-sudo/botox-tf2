// small/medium movement tricks from botox csgo-sdk, adapted to TF2's gamemovement.
// Textually included by movement.cpp (same convention as movement_ex.inl, right after it) --
// shares every helper/static already defined above it in the same TU: On/EntV3/EntGround/
// TraceHull/TraceFilterWorldProps/IN_*/FL_*/MASK_PLAYERSOLID, and s_textureBugHit/s_headSurfHit/
// s_headBounceHit/s_airStuckHit/s_wallClimbHit/s_psAssistHopActive/s_shouldPixelSurf/s_psRidingNow
// (movement.cpp), s_ebSearchMode/s_ebDetected (edgebug.inl, included earlier in this file).
//
// fireman REMOVED (not ported): botox fireman.cpp is built on CGameMovement::LadderMove, and
// TF2 has no ladders -- there is no non-ladder analog worth re-deriving, so this is a dropped
// row, not a simplified one. See gap.md movement.
//
// Ported as OPTIONS of what already works, not as second engines, where the botox source is
// itself a full sim-planner or a second search algorithm:
//   (bounce assist REMOVED: useless in TF2; auto bounce is botox's own, autobounce.inl)
//   pixel finder headbounce/pixeljump -- two new probe/classify branches added to psassist.inl's
//                  existing scanner (own result lists, PF_GetExtraPts; render wiring not done
//                  this pass -- DebugLog confirms detection)
//   edgebug style presets -- a knob bundle on the existing search (edgebug.inl), not botox's
//                  separate delusional/dna search algorithms (each its own engine)
//   pixel surf fix -- a small duck-hold guard below
// Each is named BLOCKED-as-simplified in the header above, not silently claimed as a full port.

// ============================================================================
// PS FIX: a real pixel-surf ride (s_psRidingNow, movement.cpp) can be clipped by an inadvertent
// un-duck landing one tick late (a tap, or another feature's own duck edit arriving out of
// order): hold the duck bit for a couple ticks into a ride so the ladder's sub-unit phase
// (routesim.h) isn't disturbed by a bit that flickers.
// ============================================================================
static void PsSurfFixTick(void* pLocal, UserCmd_t* pCmd)
{
    static int s_holdTicks = 0;
    if (!g_psSurfFix || !pLocal) { s_holdTicks = 0; return; }
    if (s_psRidingNow && g_offDucking > 0 && EntB(pLocal, g_offDucking))
        s_holdTicks = 3;
    if (s_holdTicks > 0)
    {
        pCmd->buttons |= IN_DUCK;
        --s_holdTicks;
    }
}

// EB sound: fires on the edgebug ITSELF, not on the search latching a plan. Pre-prediction
// state = result of last cmd, so compare it to the one before: still airborne, was falling
// past the first-tick gravity value, and now sits exactly on it = vz got reset by an edge
// (same test as EbProbe's first-tick branch, on real state). One tick late (15ms) by design.
// Only counts when the EB assist plan was flying (s_ebDetected) in the last few cmds: the raw
// vz test alone also trips on corrections / slopes / unassisted hits.
// Mouse lock lives in edgebug.inl (EdgeBug_MouseInput).
// first wave + script volume of TFPlayer.CritHit / CritHitMini / DoubleDonk / Achievement.Earned /
// MVM.PlayerUpgraded (tf2_misc vpk). Raw wav through playvol so the volume slider works on stock too
// (playgamesound has no volume). ponytail: crit rndwave variants dropped, first wave only.
static const char* const kEbSounds[] = { "player/crit_hit.wav", "player/crit_hit_mini.wav",
    "player/doubledonk.wav", "misc/achievement_earned.wav", "mvm/mvm_bought_upgrade.wav" };
static constexpr float kEbSoundBaseVol[] = { 1.f, 1.f, 1.f, 1.f, 0.25f };
static constexpr int kEbSoundCustom = 5;   // menu combo index after kEbSounds

// playvol precaches + plays at 0..1; volume > 1 stacks emits like botox.
static void EbPlayVol(const char* rel, float vol)
{
    for (float left = vol; left > 0.f; left -= 1.f)
        PrCmd("playvol \"%s\" %.2f", rel, std::min(left, 1.f));
}

// botox play_trick_sound custom branch. A file in C:\botox-tf2\sounds wins: the engine only opens
// sounds under its GAME search path, so copy it into tf\sound\botox\ (instead of botox's ".."
// climb) and play that. Else the path is taken as tf/sound-relative (tf/custom/*/sound works too).
static void EbPlayCustom()
{
    const char* name = g_edgeBugSoundPath;
    if (!name[0]) return;
    const char* base = name;
    for (const char* p = name; *p; ++p) if (*p == '\\' || *p == '/') base = p + 1;
    char rel[192];
    snprintf(rel, sizeof(rel), "%s", name);
    char src[MAX_PATH];
    snprintf(src, sizeof(src), "C:\\botox-tf2\\sounds\\%s", name);
    const DWORD attr = GetFileAttributesA(src);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY) && base[0])
    {
        // ponytail: recopies only when the name changes; edit the file in place = rename it or restart
        static char s_copied[128] = "";
        if (strcmp(s_copied, name))
        {
            char dst[MAX_PATH];   // client.dll = <tf>\bin\x64\client.dll
            if (!GetModuleFileNameA(GetModuleHandleA("client.dll"), dst, MAX_PATH)) return;
            for (int up = 0; up < 3; ++up) if (char* s = strrchr(dst, '\\')) *s = 0;
            strncat_s(dst, "\\sound\\botox", _TRUNCATE);
            CreateDirectoryA(dst, nullptr);
            strncat_s(dst, "\\", _TRUNCATE);
            strncat_s(dst, base, _TRUNCATE);
            if (!CopyFileA(src, dst, FALSE)) { DebugLog("eb sound: copy %s -> %s failed %lu", src, dst, GetLastError()); return; }
            snprintf(s_copied, sizeof(s_copied), "%s", name);
        }
        snprintf(rel, sizeof(rel), "botox/%s", base);
    }
    EbPlayVol(rel, g_edgeBugSoundVol);
}

static void EdgeBugSoundPre(void* pLocal)
{
    static float s_prevVz = 0.f;
    static bool  s_prevAir = false;
    static int   s_assistGrace = 0;   // cmds left since the plan last flew; EdgeBugPre ran first,
                                      // so a plan spent on the contact cmd is already dropped here
    const int assisted = s_assistGrace;
    s_assistGrace = s_ebDetected ? 3 : (s_assistGrace > 0 ? s_assistGrace - 1 : 0);
    if (!pLocal || g_offVelocity <= 0 || g_offFlags <= 0) { s_prevAir = false; return; }
    const float vz  = EntV3(pLocal, g_offVelocity).z;
    const bool  air = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntAlive(pLocal);
    constexpr float kEps = 0.5f;   // half a unit of vz, the old k_ebVzEps window
    const float flGravVel = -EbHalfGravTick(CvarFloat("sv_gravity", 800.f));
    const bool hit = s_prevAir && air && s_prevVz < flGravVel - kEps
                  && fabsf(vz - flGravVel) <= kEps;
    s_prevVz = vz; s_prevAir = air;
    if (!hit || !assisted || !g_edgeBugSound) return;
    s_assistGrace = 0;   // one sound per assisted EB
    if (g_edgeBugSoundId == kEbSoundCustom) { EbPlayCustom(); return; }
    const int i = std::clamp(g_edgeBugSoundId, 0, (int)(sizeof(kEbSounds) / sizeof(*kEbSounds)) - 1);
    EbPlayVol(kEbSounds[i], kEbSoundBaseVol[i] * g_edgeBugSoundVol);
}
