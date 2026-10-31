// ==== practice tab (movement -> practice) =======================================================
// Every line of this file is a CONSOLE COMMAND sent through IVEngineClient::ClientCmd_Unrestricted
// (slot 106 -- the road skins.cpp and fakemove.inl already take). Nothing here writes server
// memory, so the whole requirement is the one you have when you type the command yourself: you
// are the host of the server and sv_cheats is 1. Online it does nothing.
//
// The commands, and what the SDK says each one does:
//   impulse 101      CTFPlayer::CheatImpulseCommands -- 1000 of every ammo type, metal, every
//                    clip refilled, health topped, rage / charge meters full. Sent twice a second.
//   god              CC_God_f (client.cpp) -- a TOGGLE with no argument, so the checkbox sends it
//                    on every flip and this file remembers which way it left it.
//   mp_timelimit 0   the map never ends. The value that was there comes back when the box goes off.
//   noclip           CC_Noclip, server side, so it also survives being stuck.
//   setpos / setang  client.cpp. NEITHER touches velocity (setpos is SetAbsOrigin, setpos_exact is
//                    Teleport(pos, NULL, NULL)) -- which is why the next line exists.
//   sv_maxvelocity 0 CGameMovement::CheckVelocity clamps EVERY axis of m_vecVelocity to it, and it
//                    runs in StartGravity / FinishGravity / WalkMove, i.e. every tick. One tick at
//                    0 IS "kill all velocity". Put back k_prVelKillMs later by the delayed pump.
//   ent_create <cls> TeamNum <n> targetname <n> [keys]
//                    CC_Ent_Create: KeyValue for each pair, DispatchSpawn, drop where the player is
//                    looking, then Activate(). For a building that Activate is the whole trick:
//                    CBaseEntity::Activate does ChangeTeam(m_iInitialTeamNum) ("TeamNum" is the
//                    DEFINE_INPUT keyfield) and CBaseObject::Activate then runs
//                    InitializeMapPlacedObject -> full health, team skin, FinishedBuilding.
//   ent_fire <name> Disable   CBaseObject input (tf_obj.cpp DATADESC). A sentry CANNOT be spawned
//                    empty -- m_iAmmoShells is neither a keyfield nor an input, so no command can
//                    set it -- and Disable is the one command that stops it shooting:
//                    CObjectSentrygun::FindTarget returns false while IsDisabled(). Same result as
//                    no ammo (it never fires at you), it just looks disabled instead of dry.
//   ent_fire <name> AddOutput "origin x y z" / "angles 0 y 0"
//                    CBaseEntity::InputAddOutput (baseentity.cpp:7633): a name that is not an
//                    output falls through to KeyValue, and KeyValue's "origin" / "angles"
//                    (baseentity_shared.cpp:415/427) are SetAbsAngles / SetAbsOrigin. This is how
//                    a building ends up where the GAME would have put it instead of where
//                    ent_create's own crosshair trace + UTIL_DropToFloor dumped it.
//   bot -team -class -name -frozen -teleport   tf_bot_temp.cpp: makes the fake client, and with
//                    -teleport forces its team + class + ForceRespawn and drops it on the SERVER's
//                    own crosshair trace. -frozen (EFL_BOT_FROZEN) keeps it standing where put.
//   bot_teleport <name> x y z pitch yaw roll   our OWN placement, and it is sent k_prBotFixMs later
//                    because the bot is not alive on the frame `bot` runs (BotPutInServer sets
//                    m_flJoinTeamTime = curtime + 0.3). This is the pass that puts an enemy on a
//                    friendly's HEAD instead of inside him.
#include <cstdarg>
#include "buildplace.h"   // the SDK's own placement rules, engine-free (tests/buildplace_test.cpp)

static constexpr unsigned k_prBotFixMs  = 600;   // bot joins team at +0.3 s, so correct it after
static constexpr unsigned k_prVelKillMs = 150;   // ~10 ticks of sv_maxvelocity 0 -- one is enough
static constexpr unsigned k_prAmmoMs    = 500;   // impulse 101 rate while "infinite ammo" is on

// -class takes the NON-localized class names (tf_shareddefs.cpp g_aPlayerClassNames_NonLocalized,
// stricmp), in that array's own order. g_prBotClass indexes this, 0 = scout.
static const char* const k_prClassArg[9] =
    { "scout", "sniper", "soldier", "demoman", "medic", "heavy", "pyro", "spy", "engineer" };

static Vec3 s_prSavePos{}, s_prSaveAng{};
static bool s_prSaveHave = false;
static bool s_prGodSent  = false;        // which way this file last flipped `god`
static bool s_prTimeSent = false;
static float s_prTimeOld = 0.f;          // mp_timelimit before we zeroed it
static uint64_t s_prAmmoMs = 0;
static int s_prBotSeq = 0, s_prObjSeq = 0;

static void PrCmd(const char* fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    DebugLog("practice: %s", buf);
    if (!g_engine) return;
    __try {
        void** vt = *reinterpret_cast<void***>(g_engine);
        if (reinterpret_cast<uintptr_t>(vt[106]) > 0x10000)
            ((void(__fastcall*)(void*, const char*))vt[106])(g_engine, buf);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Commands that have to land LATER, not now: the sv_maxvelocity restore and the bot placement.
// Both are "send this string in N ms", so both use one pump, run once per cmd.
struct PrLater_t { uint64_t due; char cmd[160]; };
static PrLater_t s_prLater[8] = {};

static void PrCmdLater(unsigned ms, const char* fmt, ...)
{
    char buf[160];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const uint64_t due = GetTickCount64() + ms;
    for (auto& s : s_prLater)
        if (!s.due) { s.due = due; snprintf(s.cmd, sizeof(s.cmd), "%s", buf); return; }
    PrCmd("%s", buf);   // 8 presses inside one window: send it now rather than drop it
}

static void PrPumpLater()
{
    const uint64_t now = GetTickCount64();
    for (auto& s : s_prLater)
        if (s.due && now >= s.due) { s.due = 0; PrCmd("%s", s.cmd); }
}

static int PrTeam(void* pLocal)
{
    return (pLocal && g_offTeamNum > 0) ? EntI(pLocal, g_offTeamNum) : 0;
}

// WHERE THE CROSSHAIR POINTS, and the one rule the user asked for: a player under the crosshair
// gives his HEAD, not his chest. RcAimPlayerHead already answers exactly that (vHit.z = the top of
// the target's bbox + 1/32, i.e. where your feet stop when you stand on him) and it fills iHit for
// a TEAMMATE too -- it only returns false for one because a teammate is not solid to the route
// calculator. Here he is: placing an enemy on a friendly's head is the whole point.
static bool PrAimPos(void* pLocal, UserCmd_t* pCmd, Vec3& out)
{
    if (g_offOrigin <= 0 || g_offViewOffset <= 0) return false;
    Vec3 vForward; AngleVectors(pCmd->viewangles, &vForward);
    const Vec3 vEye = EntV3(pLocal, g_offOrigin) + EntV3(pLocal, g_offViewOffset);

    GameTrace_t tr{};
    const bool bWorld = RcRay(pLocal, vEye, vForward, tr);      // one ray, no marker fan
    const float flWorldT = bWorld ? tr.fraction * 8192.f : 8192.f;

    Vec3 vHead, oHead; int iHead = 0; bool bMate = false;
    RcAimPlayerHead(pLocal, vEye, vForward, flWorldT, vHead, iHead, oHead, bMate);
    if (iHead) { out = vHead; return true; }                    // on his head, either team
    if (!bWorld) return false;
    out = tr.endpos + tr.plane.normal * 2.f;                    // 2u off the face, never inside it
    return true;
}

static void PrTeleport()
{
    if (!s_prSaveHave) { DebugLog("practice: teleport -- no position saved yet"); return; }
    float flMaxVel = CvarFloat("sv_maxvelocity", 3500.f);
    if (flMaxVel < 1.f) flMaxVel = 3500.f;   // pressed again inside a kill window: read the default
    PrCmd("setpos %.4f %.4f %.4f;setang %.3f %.3f 0;sv_maxvelocity 0",
          s_prSavePos.x, s_prSavePos.y, s_prSavePos.z, s_prSaveAng.x, s_prSaveAng.y);
    PrCmdLater(k_prVelKillMs, "sv_maxvelocity %g", flMaxVel);
}

// ==== BUILDINGS =================================================================================
// ent_create on its own drops a building wherever the SERVER's crosshair line trace landed, +12u,
// then UTIL_DropToFloor (baseentity.cpp:8563). That is not building, it is dropping: two presses
// on the same spot stack two sentries inside each other, a wall hit leaves half the box inside the
// brush, and a respawn room is as good a spot as any. So the placement the game itself runs is
// ported in buildplace.h and answered HERE, on the client, before the command goes out:
//
//   ground      CalculatePlacementPos sweeps the footprint down the z window and takes the first
//               floor with room for the box above it -- that is what makes the sentry stand on the
//               crate you aimed at instead of hovering at the pixel you aimed at.
//   slope/edge  VerifyCorner: all four corners need ground within 32u and flatter than 45 degrees.
//   fit         one box trace at the final spot with everything solid: players, props, and every
//               building already placed. This is "they don't place on top of each other".
//   sight       a 2u hull from your eye to the spot, so a map seam is not a way to build through
//               a wall.
//   objects     IsPlacementPosValid's own 24/32 box against every other building's AABB, which
//               catches the stairs case a box trace misses.
//   respawn     EstimateValidBuildPos asks PointInRespawnRoom + PointsCrossRespawnRoomVisualizer.
//               Of those two the client only has the visualizers for certain (func_respawnroom is
//               a trigger, and triggers carry EF_NODRAW -> FL_EDICT_DONTSEND, triggers.cpp:363),
//               so the scan below takes whichever of the two classes the entity list actually has
//               and says in the log which it saw. func_nobuild and trigger_hurt are triggers too
//               and are simply not available to a client -- nothing to port.
//
// ponytail: the object box overlap uses the engine's own surrounding AABB, so a rotated brush
// entity reads as its bounding box. Exact brush volumes would need the collision model.
struct PrObj_t
{
    const char* cls;
    Vec3 bMins, bMaxs;      // build box = collision box grown 4u in x/y (tf_obj.cpp:895)
    Vec3 cMins, cMaxs;      // collision box, the thing that has to fit
    const char* keys;
    bool disable;
};

// tf_obj_sentrygun.cpp:48, tf_obj_dispenser.cpp:24, tf_obj_teleporter.cpp:34
static const PrObj_t k_prObj[3] = {
    { "obj_sentrygun",  Vec3(-24,-24,0), Vec3(24,24,66), Vec3(-20,-20,0), Vec3(20,20,66), nullptr, true  },
    { "obj_dispenser",  Vec3(-24,-24,0), Vec3(24,24,55), Vec3(-20,-20,0), Vec3(20,20,55), nullptr, false },
    { "obj_teleporter", Vec3(-28,-28,0), Vec3(28,28,12), Vec3(-24,-24,0), Vec3(24,24,12), "teleporterType 1", false },
};
static constexpr int k_prSentry = 0, k_prDispenser = 1, k_prTeleporter = 2;

// The game anchors the search to the builder's own height. A bind aims with the crosshair, so the
// anchor is the aimed point and this is the slack around it: one step, the height Source already
// calls "the same floor".
static constexpr float k_prAnchorHalfZ = 18.f;

// MASK_PLAYERSOLID_BRUSHONLY = MASK_PLAYERSOLID without CONTENTS_MONSTER: what a building stands
// on is world and brush entities, never a player. MASK_SOLID is that plus monsters, minus
// playerclip -- the mask IsPlacementPosValid uses for "does the object fit".
static constexpr unsigned MASK_PR_BRUSHONLY = MASK_PLAYERSOLID & ~0x2000000u;
static constexpr unsigned MASK_PR_SOLID     = 0x1 | 0x4000 | 0x2 | 0x2000000 | 0x8;

// The fit test is the one trace here that must hit EVERYTHING -- UTIL_TraceEntity passes no
// ignore entity and COLLISION_GROUP_PLAYER, so players and friendly buildings are solid to it.
struct TraceFilterAnything
{
    void** vfptr;
    static bool __fastcall sHit(TraceFilterAnything*, void*, int) { return true; }
    static int  __fastcall sType(TraceFilterAnything*) { return 0; }   // TRACE_EVERYTHING
    void Setup() { static void* vt[2] = { (void*)&sHit, (void*)&sType }; vfptr = vt; }
};

// network class name of an entity index (IClientEntityList slot 0 -> IClientNetworkable slot 2,
// the walk routecalc.inl's RcFloorKind already uses)
static const char* PrNetClass(int idx)
{
    if (!g_entityList) return nullptr;
    void* pNet = VCall<void*>(g_entityList, 0, idx);
    if (!pNet) return nullptr;
    const ClientClass* cc = VCall<ClientClass*>(pNet, 2);
    const char* name = nullptr;
    __try { name = (cc && cc->m_pNetworkName) ? cc->m_pNetworkName : nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { name = nullptr; }
    return name;
}

// The AABB the engine's spatial partition keeps for an entity -- exactly what UTIL_EntitiesInBox
// compares against, and the only way to get the bounds of a BRUSH entity (a respawn room's size
// lives in its model, not in a netvar). IClientUnknown::GetCollideable is slot 3 (IHandleEntity
// owns 0-2, and GetRefEHandle at 2 is the one this project already calls);
// ICollideable::WorldSpaceSurroundingBounds is slot 17, counted off the x64 interface in
// features/aim/defs.h.
static bool PrEntBounds(void* pEnt, Vec3& mn, Vec3& mx)
{
    if (!pEnt) return false;
    void* pColl = VCall<void*>(pEnt, 3);
    if (!pColl) return false;
    Vec3 a(0.f, 0.f, 0.f), b(0.f, 0.f, 0.f);
    VCall<void>(pColl, 17, &a, &b);
    if (a.x > b.x || a.y > b.y || a.z > b.z) return false;     // never written / garbage
    mn = a; mx = b;
    return true;
}

// One pass over the entity list for the three rules that need entities rather than traces.
static BpResult PrScanEnts(const Vec3& vOrigin, const Vec3& vCenter, const Vec3& vFrom, const Vec3& vTo)
{
    Vec3 tmn(0.f, 0.f, 0.f), tmx(0.f, 0.f, 0.f);
    BpObjectTestBox(vOrigin, tmn, tmx);

    BpResult r = BP_OK;
    int nRooms = 0, nViz = 0;
    for (int i = 1; i < 2048; i++)
    {
        void* pEnt = GetClientEntity(i);
        if (!pEnt) continue;
        const char* n = PrNetClass(i);
        if (!n) continue;

        const bool bObj  = !strcmp(n, "CObjectSentrygun") || !strcmp(n, "CObjectDispenser")
                        || !strcmp(n, "CObjectTeleporter");
        const bool bRoom = !strcmp(n, "CFuncRespawnRoom");
        const bool bViz  = !strcmp(n, "CFuncRespawnRoomVisualizer");
        if (!bObj && !bRoom && !bViz) continue;
        if (bRoom) nRooms++;
        if (bViz)  nViz++;

        Vec3 mn(0.f, 0.f, 0.f), mx(0.f, 0.f, 0.f);
        if (!PrEntBounds(pEnt, mn, mx)) continue;

        if (bObj && BpBoxesOverlap(tmn, tmx, mn, mx)) { r = BP_OBJECT; break; }
        if (bRoom && (BpPointIn(vOrigin, mn, mx) || BpPointIn(vCenter, mn, mx))) { r = BP_RESPAWN; break; }
        if (bViz && BpSegHitsBox(vFrom, vTo, mn, mx)) { r = BP_RESPAWN; break; }
    }
    DebugLog("practice: build scan -- %d respawn room(s), %d visualizer(s) visible to the client, %s",
             nRooms, nViz, BpWhy(r));
    return r;
}

static void PrBuildRefused(const char* cls, const char* why)
{
    DebugLog("practice: %s refused -- %s", cls, why);
    char buf[160];
    snprintf(buf, sizeof(buf), "botox: cannot build there -- %s\n", why);
    ConsoleColorPrint(255, 140, 140, buf);
}

static void PrBuild(void* pLocal, UserCmd_t* pCmd, int iObj)
{
    const PrObj_t& o = k_prObj[iObj];
    if (g_offOrigin <= 0 || g_offViewOffset <= 0) { PrBuildRefused(o.cls, "netvars not resolved"); return; }

    // where you aim, pushed 2u off the face so the spot is never inside the surface itself
    Vec3 vFwd(0.f, 0.f, 0.f);
    AngleVectors(pCmd->viewangles, &vFwd);
    const Vec3 vEye = EntV3(pLocal, g_offOrigin) + EntV3(pLocal, g_offViewOffset);
    GameTrace_t aim{};
    if (!RcRay(pLocal, vEye, vFwd, aim)) { PrBuildRefused(o.cls, "nothing under the crosshair"); return; }
    const Vec3 vAim = aim.endpos + aim.plane.normal * 2.f;

    TraceFilterWorldProps world; world.Setup(pLocal);
    auto hull = [&](const Vec3& a, const Vec3& b, const Vec3& mn, const Vec3& mx)
    {
        GameTrace_t tr{};
        TraceHull(a, b, mn, mx, MASK_PR_BRUSHONLY, &world, &tr);
        BpHit<Vec3> h;
        h.frac = tr.fraction; h.end = tr.endpos; h.n = tr.plane.normal;
        h.startSolid = tr.startsolid || tr.allsolid;
        return h;
    };
    auto line = [&](const Vec3& a, const Vec3& b)
    {
        GameTrace_t tr{};
        TraceLine(a, b, MASK_PR_BRUSHONLY, &world, &tr);
        BpHit<Vec3> h;
        h.frac = tr.fraction; h.end = tr.endpos; h.n = tr.plane.normal;
        h.startSolid = tr.startsolid || tr.allsolid;
        return h;
    };

    Vec3 vOrigin(0.f, 0.f, 0.f), vCenter(0.f, 0.f, 0.f);
    BpResult r = BpCalcPlacementPos(hull, line, vAim, k_prAnchorHalfZ, o.bMins, o.bMaxs, vOrigin, vCenter);

    if (r == BP_OK)   // the whole object has to fit: players and other buildings included
    {
        TraceFilterAnything all; all.Setup();
        GameTrace_t tr{};
        TraceHull(vOrigin, vOrigin, o.cMins, o.cMaxs, MASK_PR_SOLID, &all, &tr);
        if (tr.DidHit()) r = BP_BLOCKED;
    }
    if (r == BP_OK)   // and you have to be able to see the spot from where you stand
    {
        GameTrace_t tr{};
        Vec3 vTo = vOrigin; vTo.z += o.bMaxs.z * 0.5f;
        TraceHull(vEye, vTo, Vec3(-2.f, -2.f, -2.f), Vec3(2.f, 2.f, 2.f), MASK_PR_BRUSHONLY, &world, &tr);
        if (tr.fraction < 1.f) r = BP_NO_LOS;
    }
    if (r == BP_OK)
    {
        const Vec3 vMid = EntV3(pLocal, g_offOrigin)
                        + (EntV3(pLocal, g_offMins) + EntV3(pLocal, g_offMaxs)) * 0.5f;  // WorldSpaceCenter
        r = PrScanEnts(vOrigin, vCenter, vMid, vCenter + vFwd * 8.f);   // SDK: the far edge, + 8u
    }
    if (r != BP_OK) { PrBuildRefused(o.cls, BpWhy(r)); return; }

    const int iTeam = (PrTeam(pLocal) == TF_TEAM_RED_MV) ? TF_TEAM_BLUE_MV : TF_TEAM_RED_MV;
    const int id = ++s_prObjSeq;
    // a targetname per building, because ent_fire resolves its target with FindEntityByName
    // (cbase.cpp CEventQueue::ServiceEvents) -- a classname would not match, and one shared name
    // would disable every sentry placed so far instead of this one
    PrCmd("ent_create %s TeamNum %d targetname botoxb%d%s%s", o.cls, iTeam, id,
          o.keys ? " " : "", o.keys ? o.keys : "");
    // ...then put it where the placement said, facing the way you face (CalculatePlacementPos
    // takes nothing but the builder's yaw). ent_create already dropped it at the server's own
    // crosshair trace, so these two undo that drop on the next server frame.
    PrCmd("ent_fire botoxb%d AddOutput \"angles 0 %.2f 0\"", id, pCmd->viewangles.y);
    PrCmd("ent_fire botoxb%d AddOutput \"origin %.2f %.2f %.2f\"", id, vOrigin.x, vOrigin.y, vOrigin.z);
    if (o.disable) PrCmd("ent_fire botoxb%d Disable", id);
    DebugLog("practice: %s placed at %.1f %.1f %.1f yaw %.1f (aim %.1f %.1f %.1f)",
             o.cls, vOrigin.x, vOrigin.y, vOrigin.z, pCmd->viewangles.y, vAim.x, vAim.y, vAim.z);
}

static void PrBot(void* pLocal, UserCmd_t* pCmd, bool bEnemy)
{
    Vec3 vPlace;
    if (!PrAimPos(pLocal, pCmd, vPlace))
    {
        DebugLog("practice: bot refused -- nothing under the crosshair");
        return;
    }
    // unassigned / spectator reads as "not red", so an enemy comes out red and a friendly blue
    const bool bRed = (PrTeam(pLocal) == TF_TEAM_RED_MV) != bEnemy;
    const int  iClass = std::clamp(g_prBotClass, 0, 8);
    const int  id = ++s_prBotSeq;
    char name[24];
    snprintf(name, sizeof(name), "botoxbot%d", id);
    PrCmd("bot -team %s -class %s -name %s -frozen -teleport",
          bRed ? "red" : "blue", k_prClassArg[iClass], name);

    // turn him to face you, so a placed dummy is a dummy looking at you
    const Vec3 vMe = EntV3(pLocal, g_offOrigin);
    const float flYaw = NormalizeAngle(atan2f(vMe.y - vPlace.y, vMe.x - vPlace.x) * 57.2957795f);
    PrCmdLater(k_prBotFixMs, "bot_teleport %s %.3f %.3f %.3f 0 %.2f 0",
               name, vPlace.x, vPlace.y, vPlace.z, flYaw);
}

// Called once per user command, BEFORE the pipeline's dead / noclip / blocked returns: a practice
// bind has to answer while you are noclipping or dead, which is most of the time you are using it.
static void Practice_Tick(UserCmd_t* pCmd)
{
    PrPumpLater();

    // The menu thread only flips the bool; the command goes out from here, the cmd thread, because
    // the console buffer is not the render thread's to write. Sent on the CHANGE only.
    if (g_prInfHealth != s_prGodSent) { s_prGodSent = g_prInfHealth; PrCmd("god"); }
    if (g_prInfTime != s_prTimeSent)
    {
        s_prTimeSent = g_prInfTime;
        if (g_prInfTime) { s_prTimeOld = CvarFloat("mp_timelimit", 0.f); PrCmd("mp_timelimit 0"); }
        else             { PrCmd("mp_timelimit %g", s_prTimeOld); }
    }

    if (!pCmd) return;
    const int idx = GetLocalPlayerIndex();
    void* pLocal = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!pLocal) return;

    // dead, you are a camera: your own origin and view offset are not a place to aim or save
    // from, so everything that reads them waits until you are alive again. noclip does not.
    const bool bAlive = EntAlive(pLocal);

    const uint64_t now = GetTickCount64();
    if (g_prInfAmmo && bAlive && now - s_prAmmoMs >= k_prAmmoMs)
    {
        s_prAmmoMs = now;
        PrCmd("impulse 101");
    }

    // edge press, the rule every NoMode bind in this file uses (MovementKeyOK also holds the
    // is_active_app gate, so nothing fires while the game is not the foreground window)
    auto Pressed = [](int key, bool& held)
    {
        const bool down = key != 0 && MovementKeyOK(key, 1);
        const bool edge = down && !held;
        held = down;
        return edge;
    };
    static bool hNoclip = false, hSave = false, hTp = false, hEnemy = false, hMate = false,
                hSentry = false, hDisp = false, hTele = false;

    if (Pressed(g_prNoclipKey, hNoclip)) PrCmd("noclip");
    if (Pressed(g_prSaveKey, hSave) && bAlive && g_offOrigin > 0)
    {
        s_prSavePos = EntV3(pLocal, g_offOrigin);
        s_prSaveAng = pCmd->viewangles;
        s_prSaveHave = true;
        DebugLog("practice: saved %.2f %.2f %.2f / %.1f %.1f",
                 s_prSavePos.x, s_prSavePos.y, s_prSavePos.z, s_prSaveAng.x, s_prSaveAng.y);
    }
    if (Pressed(g_prTpKey, hTp) && bAlive)           PrTeleport();
    if (Pressed(g_prBotEnemyKey, hEnemy) && bAlive)  PrBot(pLocal, pCmd, true);
    if (Pressed(g_prBotMateKey, hMate) && bAlive)    PrBot(pLocal, pCmd, false);
    if (Pressed(g_prSentryKey, hSentry) && bAlive)   PrBuild(pLocal, pCmd, k_prSentry);
    if (Pressed(g_prDispenserKey, hDisp) && bAlive)  PrBuild(pLocal, pCmd, k_prDispenser);
    if (Pressed(g_prTeleporterKey, hTele) && bAlive) PrBuild(pLocal, pCmd, k_prTeleporter);
}
