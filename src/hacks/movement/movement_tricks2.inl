#include "movement_tricks2_pure.h"   // Unit A3: pure decisions (bounce tangent/tung band/eb style), gate-tested

// Unit A3 port: small/medium movement tricks from botox csgo-sdk, adapted to TF2's gamemovement.
// Textually included by movement.cpp (same convention as movement_ex.inl, right after it) --
// shares every helper/static already defined above it in the same TU: On/EntV3/EntGround/
// TraceHull/TraceFilterWorldProps/IN_*/FL_*/MASK_PLAYERSOLID, and s_textureBugHit/s_headSurfHit/
// s_headBounceHit/s_airStuckHit/s_wallClimbHit/s_psAssistHopActive/s_shouldPixelSurf/s_psRidingNow
// (movement.cpp), s_ebSearchMode/s_ebDetected (edgebug.inl, included earlier in this file).
//
// fireman REMOVED (not ported): botox fireman.cpp is built on CGameMovement::LadderMove, and
// TF2 has no ladders -- there is no non-ladder analog worth re-deriving, so this is a dropped
// row, not a simplified one. See gap.md Unit A.
//
// Ported as OPTIONS of what already works, not as second engines, where the botox source is
// itself a full sim-planner or a second search algorithm:
//   tung surf   -- a relaxed-tolerance knob on TextureBug (texturebug.inl), not botox's own
//                  ~700-line sim/replay/learn engine (itself flagged UNTESTED + low priority)
//   bounce / auto bounce -- one real-time wall-tangent redirect below, not botox's sim-searched
//                  point planner (n_assist::run/programs/budgets) -- no points file this pass
//   pixel finder headbounce/pixeljump -- two new probe/classify branches added to psassist.inl's
//                  existing scanner (own result lists, PF_GetExtraPts; render wiring not done
//                  this pass -- DebugLog confirms detection)
//   edgebug style presets -- a knob bundle on the existing search (edgebug.inl), not botox's
//                  separate delusional/dna search algorithms (each its own engine)
//   pixel surf fix -- a small duck-hold guard below
// Each is named BLOCKED-as-simplified in the header above, not silently claimed as a full port.

// ============================================================================
// BOUNCE: auto bounce + bounce assist. Real-time wall-tangent redirect while airborne and
// falling with speed -- the part of botox's auto_bounce.cpp/bounce_assist.cpp that is actually
// a per-tick steer (their own trailing loop, same idea: trace outward, if the hit is a steep
// wall bend the wish to run ALONG it instead of INTO it, so the wall does not just eat the
// speed). bounceAssist adds one thing on top: a catch-jump the tick contact is close, trading
// the clip for a hop instead of a dead stop (botox's own "gain over your own xy" idea, simplified
// to one trace instead of a budgeted sim search over saved points).
// ============================================================================
static void AutoBounce(void* pLocal, UserCmd_t* pCmd)
{
    const bool bAuto   = On(g_autoBounce, g_autoBounceKey, g_autoBounceMode);
    const bool bAssist = On(g_bounceAssist, g_bounceAssistKey, g_bounceAssistMode);
    if (!bAuto && !bAssist) return;
    if (EntMoveType(pLocal) != MOVETYPE_WALK || EntGround(pLocal)) return;
    // everyone who already owns the cmd outranks a redirect -- this never fights a live ride
    if (s_textureBugHit || s_headSurfHit || s_headBounceHit || s_airStuckHit || s_wallClimbHit
        || s_psAssistHopActive || s_shouldPixelSurf || s_psRidingNow)
        return;

    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    if (vel.z >= 0.f) return;                       // falling only -- a rising hull has nothing to gain
    if (vel.Length2D() < g_bounceMinSpeed) return;   // too slow to be worth bending

    const Vec3 origin = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    Vec3 dir = vel; dir.z = 0.f;
    if (dir.Length() < 0.1f) return;
    dir.Normalize();

    const float flReach = std::clamp(g_bounceReach, 8.f, 128.f);
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(origin, origin + dir * flReach, mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
    if (!tr.DidHit() || (tr.dispFlags & DISPSURF_SURFACE)) return;   // no wall, or a displacement (same family TB hands off)
    if (fabsf(tr.plane.normal.z) > 0.3f) return;     // need a steep wall, not a floor/ramp

    // redirect: keep the view, bend the WISH to run along the wall tangent (the side of the
    // normal the player is already heading toward, so it never reverses the player's own turn)
    const Vec3& n = tr.plane.normal;
    Vec3 tangent(-n.y, n.x, 0.f);
    if (BounceTangentShouldFlip(n.x, n.y, dir.x, dir.y)) tangent = tangent * -1.f;
    Vec3 wishAngle = VectorAngles(tangent);
    ClampAnglesMv(wishAngle);
    Vec3 wish = pCmd->viewangles; wish.y = wishAngle.y;
    CorrectMovement(pCmd, wish, pCmd->viewangles);
    DebugLog("BNC: redirect nz=%.2f spd=%.0f", n.z, vel.Length2D());

    if (bAssist)
    {
        const float flDist = tr.fraction * flReach;
        if (flDist < 24.f)
        {
            pCmd->buttons |= IN_JUMP;   // catch the contact as a hop, not a stop
            DebugLog("BNC: assist jump dist=%.1f", flDist);
        }
    }
}

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
// Mouse lock lives in edgebug.inl (EdgeBug_MouseInput).
static const char* const kEbSounds[] = { "TFPlayer.CritHit", "TFPlayer.CritHitMini",
    "TFPlayer.DoubleDonk", "Achievement.Earned", "MVM.PlayerUpgraded" };

static void EdgeBugSoundPre(void* pLocal)
{
    static float s_prevVz = 0.f;
    static bool  s_prevAir = false;
    if (!pLocal || g_offVelocity <= 0 || g_offFlags <= 0) { s_prevAir = false; return; }
    const float vz  = EntV3(pLocal, g_offVelocity).z;
    const bool  air = !(EntI(pLocal, g_offFlags) & FL_ONGROUND) && EntAlive(pLocal);
    constexpr float kEps = 0.5f;   // half a unit of vz, the old k_ebVzEps window
    const float flGravVel = -EbHalfGravTick(CvarFloat("sv_gravity", 800.f));
    const bool hit = s_prevAir && air && s_prevVz < flGravVel - kEps
                  && fabsf(vz - flGravVel) <= kEps;
    s_prevVz = vz; s_prevAir = air;
    if (!hit || !g_edgeBugSound) return;
    const int i = std::clamp(g_edgeBugSoundId, 0, (int)(sizeof(kEbSounds) / sizeof(*kEbSounds)) - 1);
    PrCmd("playgamesound %s", kEbSounds[i]);   // client EmitSound on local player, no cheat flag
}
