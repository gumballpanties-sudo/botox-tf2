#include "../../globals/globals.h"
#include "../../globals/pinned_thread.h"   // routecalc.inl solver worker
#include "movement.h"
#include "../hud/avatar_cache.h"   // Avatar_Pump piggybacks this game-thread tick
#include "../../hooks/crashlog.h"   // CrashSpan: stutter probe on the whole pipeline
#include "../lua/lua.h"            // lua create_move / pre_prediction (no-aim CreateMove path)
#include "prediction.h"   // engine prediction core (Pred_*, restore, sim pool, snapshots)
#include "recorder.h"      // movement recorder (record/play owns the cmd before features run)
#include "eb_stance.h"    // edge bug stance filter (pure, gate-tested)
#include "airlean.h"      // AirAccelerate geometry: best-gain / free-turn wish angle (gate-tested)
#include "routesim.h"     // z-only CTFGameMovement port: AutoJumpbug's policy + its look-ahead
#include "ps_surf.h"      // "is this pin a surf or a stuck hull" -- the ps indicator's rule (gate-tested)
#include "ps_map.h"       // whole-map scan candidates: a brush wall face's top edge (gate-tested)
#include "mv_scanbound.h" // MV_DetectWallLines broadphase box (pure, gate-tested)
#include "movement_ex_pure.h" // pure decisions (hsw/delay hop/lj adaptive cancel), gate-tested
#include "movement_edge_pure.h" // pure decisions (edge skip duck gate, blockbot wish/sign), gate-tested
#include "detections.h"   // botox chat detections, fed the cmd start state at the publish point
#include "../hud/points.h" // g_esFired: edge skip's lip landing, the keybind indicator particle count
#include <mutex>
#include <atomic>
#include <thread>
#include <cfloat>
#include <climits>

// Aletherium Misc.cpp movement port (core set) + EnginePrediction port.
// All engine access = netvar offsets + vtable slots confirmed against the x64 client.

// ==== buttons / flags / etc ====
static constexpr int IN_ATTACK    = 1 << 0;
static constexpr int IN_JUMP      = 1 << 1;
static constexpr int IN_DUCK      = 1 << 2;
static constexpr int IN_FORWARD   = 1 << 3;
static constexpr int IN_BACK      = 1 << 4;
static constexpr int IN_MOVELEFT  = 1 << 9;
static constexpr int IN_MOVERIGHT = 1 << 10;
static constexpr int IN_ATTACK2   = 1 << 11;

static constexpr int FL_ONGROUND = 1 << 0;
static constexpr int FL_DUCKING  = 1 << 1;
// const.h:150-181, the FL_ANIMDUCKING branch (the one TF2 builds). Read by the fast-probe
// tick guard: each of these adds or replaces a force the closed-form air tick does not model
static constexpr int FL_ANIMDUCKING  = 1 << 2;
static constexpr int FL_WATERJUMP    = 1 << 3;
static constexpr int FL_FROZEN       = 1 << 6;
static constexpr int FL_INWATER      = 1 << 10;
static constexpr int FL_SWIM         = 1 << 12;
static constexpr int FL_CONVEYOR     = 1 << 13;
static constexpr int FL_BASEVELOCITY = 1 << 24;

static constexpr int MOVETYPE_WALK     = 2;
static constexpr int MOVETYPE_FLY      = 3;
static constexpr int MOVETYPE_NOCLIP   = 8;
static constexpr int MOVETYPE_OBSERVER = 10;

static constexpr unsigned MASK_ALL_MV   = 0xFFFFFFFF;
static constexpr unsigned short SURF_TRIGGER_MV = 0x40;   // trigger brush surface
static constexpr int DISPSURF_SURFACE   = 1 << 0;         // trace.dispFlags: displacement hit
static constexpr int FSOLID_NOT_SOLID_MV = 0x4;           // m_usSolidFlags
static constexpr int FSOLID_TRIGGER_MV   = 0x8;

// MASK_PLAYERSOLID = SOLID|MOVEABLE|PLAYERCLIP|WINDOW|MONSTER|GRATE
static constexpr unsigned MASK_PLAYERSOLID = 0x1 | 0x4000 | 0x10000 | 0x2 | 0x2000000 | 0x8;

// TF team collision. tf_shareddefs.h TF_TEAM_RED/BLUE; the collision group index is the
// const.h Collision_Group_t enum (20 shared entries) + tf_shareddefs.h's own list:
// 20 GRENADES, 21 OBJECT, 22 OBJECT_SOLIDTOPLAYERMOVEMENT, ... 25 RESPAWNROOMS
static constexpr int TF_TEAM_RED_MV  = 2;
static constexpr int TF_TEAM_BLUE_MV = 3;
static constexpr int TFCOLL_OBJECT_SOLIDTOPLAYER_MV = 22;

// TF conds (m_nPlayerCond bits): taunting=7, shield charge=17
static constexpr int COND_BLOCK_MOVEMENT = (1 << 7) | (1 << 17);

// ==== vtable slots (confirmed x64) ====
static constexpr int kEngine_SetViewAngles   = 20; // IVEngineClient::SetViewAngles(QAngle&)
// kTrace_TraceRay, GameTrace_t, Ray_mv, cplane_mv, csurface_mv moved to movement.h --
// backstab.cpp traces too, and two copies of an engine struct is layout drift waiting

// TraceType 3 = TRACE_EVERYTHING_FILTER_PROPS. World brushes always trace; filter
// decides entities. Port of CTraceFilterWorldAndPropsOnly, class check simplified:
// hit static props (serial 1<<15); reject players (entity list compare); reject
// trigger / non-solid brush volumes (m_usSolidFlags) so eb/ps/aa/jumpbug/wallclimb
// never false-detect off resupply / capture / hurt zones; hit the rest (doors/props).
struct TraceFilterWorldProps
{
    void** vfptr;
    void*  pSkip;
    int    nSkipTeam;   // local player's team -- brushes on it are not solid to us
    bool   bEnemies = false;   // opt-in: hit enemy players (solid to our movement, teammates are not)

    static bool __fastcall sShouldHit(TraceFilterWorldProps* self, void* pEnt, int /*mask*/)
    {
        if (!pEnt || pEnt == self->pSkip) return false;
        unsigned int h = 0xFFFFFFFF;
        __try {
            unsigned int* pH = VCall<unsigned int*>((void*)pEnt, 2); // IHandleEntity::GetRefEHandle
            if (pH) h = *pH;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if ((h >> 16) == (1u << 15)) return true;           // static prop
        int idx = (int)(h & 0xFFFF);                        // ENT_ENTRY_MASK (16 bit)
        if (idx >= 1 && idx <= 64 && GetClientEntity(idx) == pEnt && !self->bEnemies)
            return false;                                   // player slot -> skip (bEnemies: team rule below)
        if (g_offSolidFlags > 0)                            // pass-through trigger volumes
        {
            __try {
                unsigned short fl = *reinterpret_cast<unsigned short*>(reinterpret_cast<uintptr_t>(pEnt) + g_offSolidFlags);
                if (fl & (FSOLID_TRIGGER_MV | FSOLID_NOT_SOLID_MV))
                    return false;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        // ---- TEAM BRUSHES ARE NOT SOLID TO THEIR OWN TEAM -------------------------------
        // Setup-gate force fields and respawn-room visualizers are plain solid brush ents on
        // the client -- FSOLID_TRIGGER / FSOLID_NOT_SOLID are CLEAR on them, so the check
        // above lets them through and every feature read one as a wall. The engine does not:
        // CTFGameMovement::PlayerSolidMask (tf_gamemovement.cpp:274) puts the ENEMY team's
        // contents bit in the move mask, and C_FuncForceField / C_FuncRespawnRoomVisualizer
        // ShouldCollide return false without it. Our traces carry a bare MASK_PLAYERSOLID and
        // never called ShouldCollide -> a friendly barrier looked like a wall the sim then
        // walked straight through. Same rule the game runs: our team -> pass through.
        // One exception (baseobject_shared.cpp:468): the solid-to-player-movement collision
        // group is solid to BOTH teams, so a friendly sentry/dispenser stays a wall.
        if (g_offTeamNum > 0
            && (self->nSkipTeam == TF_TEAM_RED_MV || self->nSkipTeam == TF_TEAM_BLUE_MV))
        {
            __try {
                const int entTeam = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + g_offTeamNum);
                if (entTeam == self->nSkipTeam)
                {
                    int grp = 0;
                    if (g_offCollGroup > 0)
                        grp = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + g_offCollGroup);
                    if (grp != TFCOLL_OBJECT_SOLIDTOPLAYER_MV)
                        return false;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        return true;                                        // props / doors / world ents
    }
    static int __fastcall sTraceType(TraceFilterWorldProps* /*self*/) { return 3; }

    void Setup(void* skip)
    {
        static void* vt[2] = { (void*)&sShouldHit, (void*)&sTraceType };
        vfptr = vt;
        pSkip = skip;
        nSkipTeam = 0;
        bEnemies = false;
        if (skip && g_offTeamNum > 0)
        {
            __try { nSkipTeam = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(skip) + g_offTeamNum); }
            __except (EXCEPTION_EXECUTE_HANDLER) { nSkipTeam = 0; }
        }
    }
};

// ==== interfaces / sigs (Movement_Install) ====
static void* g_engineTrace  = nullptr; // EngineTraceClient003

// wave 2: engine predicted-frame restore + abs origin write (both direct fns, Aletherium sigs)
using SetAbsOrigin_t = void(__fastcall*)(void* ent, const Vec3&);  // CBaseEntity::SetAbsOrigin
static SetAbsOrigin_t g_fnSetAbsOrigin = nullptr;
// CBaseEntity::SetAbsVelocity: SetupMove reads GetAbsVelocity (sdk prediction.cpp:623), so a sim
// started off a m_vecVelocity netvar write alone runs the OLD velocity. Optional, nothing gates on it
static SetAbsOrigin_t g_fnSetAbsVelocity = nullptr;

// mov/lea [rip+disp32]: disp at +3, insn len 7
static uint8_t* RelToAbs(uint8_t* addr)
{
    return addr + 7 + *reinterpret_cast<int32_t*>(addr + 3);
}

void Movement_Install()
{
    if (g_predReady) return;

    if (!g_engineTrace)  g_engineTrace  = GetInterface("engine.dll", "EngineTraceClient003");
    if (!g_prediction)   g_prediction   = GetInterface("client.dll", "VClientPrediction001");
    if (!g_gameMovement) g_gameMovement = GetInterface("client.dll", "GameMovement001");

    HMODULE hClient = GetModuleHandleA("client.dll");
    HMODULE hEngine = GetModuleHandleA("engine.dll");
    if (!hClient || !hEngine) return;

    __try {
        if (!g_moveHelper) {
            // Aletherium IMoveHelper sig: mov rcx,[rip] -> &g_pMoveHelper, deref once
            uint8_t* a = PatternScan(hClient, "48 8B 0D ? ? ? ? 48 8B 01 FF 50 ? 0F B7 D7");
            if (a) g_moveHelper = *reinterpret_cast<void**>(RelToAbs(a));
        }
        if (!g_globalVars) {
            // Aletherium CGlobalVarsBase sig: lea rax,[rip+gpGlobals]; ret
            uint8_t* a = PatternScan(hEngine, "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B CA");
            if (a) g_globalVars = reinterpret_cast<GlobalVars_t*>(RelToAbs(a));
        }
        if (!g_randomSeed) {
            // Aletherium RandomSeed sig
            uint8_t* a = PatternScan(hClient, "0F B6 1D ? ? ? ? 89 9D");
            if (a) g_randomSeed = reinterpret_cast<int*>(RelToAbs(a));
        }
        if (!g_fnTransferData) {
            // Aletherium CPredictionCopy::TransferData sig (direct function)
            uint8_t* a = PatternScan(hClient, "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 8B 3D");
            if (a) g_fnTransferData = reinterpret_cast<TransferData_t>(a);
        }
        if (!g_fnRestoreFrame) {
            // Aletherium CPrediction_RestoreEntityToPredictedFrame sig (direct fn: this, frame)
            uint8_t* a = PatternScan(hClient, "40 55 48 83 EC ? 8B EA");
            if (a) g_fnRestoreFrame = reinterpret_cast<RestoreFrame_t>(a);
        }
        if (!g_fnSetAbsOrigin) {
            // Aletherium CBaseEntity_SetAbsOrigin sig (direct fn: this, const Vec3&)
            uint8_t* a = PatternScan(hClient, "48 89 5C 24 ? 57 48 83 EC ? 48 8B FA 48 8B D9 E8 ? ? ? ? F3 0F 10 83");
            if (a) g_fnSetAbsOrigin = reinterpret_cast<SetAbsOrigin_t>(a);
        }
        if (!g_fnSetAbsVelocity) {
            // Amalgam CBaseEntity_SetAbsVelocity sig (aim/defs.h), unique in client.dll on disk 11-03
            uint8_t* a = PatternScan(hClient, "48 89 5C 24 ? 57 48 83 EC ? F3 0F 10 81 ? ? ? ? 48 8B DA 0F 2E 02");
            if (a) g_fnSetAbsVelocity = reinterpret_cast<SetAbsOrigin_t>(a);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    g_predReady = g_prediction && g_gameMovement && g_moveHelper && g_globalVars
               && g_randomSeed && g_fnTransferData;
    g_frameReady = g_predReady && g_fnRestoreFrame && g_fnSetAbsOrigin;

    DebugLog("movement: trace=%p pred=%p gm=%p mh=%p gv=%p seed=%p xfer=%p restore=%p setabs=%p setvel=%p ready=%d frame=%d",
             g_engineTrace, g_prediction, g_gameMovement, g_moveHelper,
             g_globalVars, g_randomSeed, (void*)g_fnTransferData,
             (void*)g_fnRestoreFrame, (void*)g_fnSetAbsOrigin, (void*)g_fnSetAbsVelocity, (int)g_predReady, (int)g_frameReady);
}


// ==== entity field reads (netvar offsets from netvars.cpp) ====
static bool EntGround(void* e)
{
    bool byHandle = g_offGroundEntity > 0 && EntI(e, g_offGroundEntity) != -1;
    bool byFlags  = g_offFlags > 0 && (EntI(e, g_offFlags) & FL_ONGROUND);
    return byHandle || byFlags;
}
static bool EntDucking(void* e) { return g_offFlags > 0 && (EntI(e, g_offFlags) & FL_DUCKING); }
static int  EntMoveType(void* e){ return g_offWaterLevel > 0 ? EntB(e, g_offWaterLevel - 4) : MOVETYPE_WALK; }
static bool EntSwimming(void* e){ return g_offWaterLevel > 0 && EntB(e, g_offWaterLevel) >= 2; }
static bool EntCondBlocked(void* e) { return g_offCond > 0 && (EntI(e, g_offCond) & COND_BLOCK_MOVEMENT); }

// Cached ConVar float read. `def` is returned for a var this build does not have, so a
// missing name is a documented default rather than a zero nobody notices.
static float CvarFloat(const char* name, float def)
{
    if (!g_cvar) return def;
    // 16, not 8. The movement half asks for ten distinct names (sv_gravity, sv_airaccelerate,
    // sv_accelerate, sv_maxvelocity, sv_stepsize, tf_clamp_airducks, cl_forwardspeed,
    // cl_sidespeed, m_yaw, sensitivity), so an 8-slot cache left two of them permanently on the
    // "cache full" path -- an ICvar dictionary lookup by string, every call, every command.
    // Headroom, not a guess: if this ever fills again the same silent cost comes back.
    static struct { const char* n; ConVar* v; } cache[16] = {};
    ConVar* cv = nullptr;
    bool inCache = false;
    for (auto& c : cache) {
        // pointer test first: callers pass literals, /O2 pools them, so strcmp almost never runs
        if (c.n && (c.n == name || strcmp(c.n, name) == 0)) { cv = c.v; inCache = true; break; }
        if (!c.n) { c.n = name; c.v = cv = g_cvar->FindVar(name); inCache = true; break; }
    }
    if (!inCache) cv = g_cvar->FindVar(name); // cache full: slow lookup, not silent default
    if (!cv) return def;
    float r = def;
    __try { r = *reinterpret_cast<float*>(ConVarParent(cv) + k_cvFloat); }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = def; }
    return r;
}


// Not VCall: the engine takes `Vec3&`, and VCall's by-value parameter pack would hand it
// a copy instead of the caller's object.
static void EngineSetViewAngles(Vec3& ang)
{
    if (!g_engine) return;
    using SetVA_t = void(__fastcall*)(void*, Vec3&);
    __try {
        void** vt = *(void***)g_engine;
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000)
            ((SetVA_t)vt[kEngine_SetViewAngles])(g_engine, ang);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// IVEngineClient::GetViewAngles sits immediately BEFORE SetViewAngles in the interface
// (cdll_int.h:263/265), so it is the confirmed slot 20 minus one.
static constexpr int kEngine_GetViewAngles = kEngine_SetViewAngles - 1;

static bool EngineGetViewAngles(Vec3& ang)
{
    if (!g_engine) return false;
    using GetVA_t = void(__fastcall*)(void*, Vec3&);
    __try {
        void** vt = *(void***)g_engine;
        ((GetVA_t)vt[kEngine_GetViewAngles])(g_engine, ang);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}

// ==== bind runtime (delusional menu::checkkey) ====
// Every gate used to hit user32 on EVERY ask, and a cmd asks ~30 times: needPred alone
// tests 7 features, then each feature re-tests its own bind, and PS assist tests two more.
// That is ~30 GetForegroundWindow + ~30 GetAsyncKeyState per tick, ~4000/s at 66.7.
// Worse, it was not even self-consistent: a key released between two asks inside ONE cmd
// let feature A run on "pressed" while feature B saw "released" -- a half-applied tick.
// Resolve both once per cmd and hand every ask the same answer.
static bool s_bindAppActive = true;
static bool s_bindTyping = false;
struct BindMemo_t { int key; int mode; bool val; };
static BindMemo_t s_bindMemo[16] = {};
static int  s_bindMemoN = 0;

static void MovementBinds_NewCmd()
{
    s_bindAppActive = !g_hwnd || GetForegroundWindow() == g_hwnd; // is_active_app gate
    s_bindTyping = Binds_Blocked();   // chat / console / menu text box: held keys are letters
    s_bindMemoN = 0;
}

static bool BindKeyDown(int key, int mode)
{
    return (mode == 1) ? !s_bindTyping && (GetAsyncKeyState(key) & 0x8000) != 0   // on hotkey
                       : Binds_Toggled(key);                                     // toggle, typed presses taken out
}

bool MovementKeyOK(int key, int mode)
{
    if (!s_bindAppActive) return false;
    if (mode != 1 && mode != 2) return true;   // 0 = always on; unknown modes stay on
    if (key == 0) return false;

    for (int i = 0; i < s_bindMemoN; ++i)
        if (s_bindMemo[i].key == key && s_bindMemo[i].mode == mode) return s_bindMemo[i].val;

    const bool v = BindKeyDown(key, mode);
    // memo full (more distinct binds held than slots): answer live, never guess
    if (s_bindMemoN < (int)(sizeof(s_bindMemo) / sizeof(s_bindMemo[0])))
        s_bindMemo[s_bindMemoN++] = { key, mode, v };
    return v;
}
static bool On(bool enabled, int key, int mode) { return enabled && MovementKeyOK(key, mode); }

// ==== trace helper ====
// COLDTIRES 80th -- the trace budget is the REAL tick cost near a wall, not the sims:
// one wall scan is 16 dirs x (1 hull + ~11-73 line traces), and a seam tick ran it twice.
// Counted here, printed as tr= in the TB line, so "faster" is a measurement not a claim
static int s_tracesThisCmd = 0;

static void TraceHull(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs,
                      unsigned mask, void* filter, GameTrace_t* tr)
{
    ++s_tracesThisCmd;
    if (!g_engineTrace) { tr->fraction = 1.f; return; }
    Ray_mv ray;
    ray.Init(start, end, mins, maxs);
    using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
    __try {
        void** vt = *(void***)g_engineTrace;
        ((TraceRay_t)vt[kTrace_TraceRay])(g_engineTrace, ray, mask, filter, tr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { tr->fraction = 1.f; }
}

static bool s_predicted = false; // this cmd got a Start/Simulate (End pending)



// Diag throttle. TB and AS each printed a ~250 byte line EVERY tick, and DebugLog is a
// synchronous WriteFile under a critical section on the CreateMove thread -- ~130
// syscalls/s in the movement path, and TB alone was 2175 of one log's 2500 lines, which
// buried everything else. A tick where something HAPPENED still prints every time; a
// quiet tick prints 1 in 33, the rate the PS reject diag already used.
static bool DiagOk(int& lastTick, bool bInteresting)
{
    const int now = g_globalVars ? g_globalVars->tickcount : 0;
    if (bInteresting || now < lastTick || now - lastTick >= 33) { lastTick = now; return true; }
    return false;
}

// ---- THE WISH THAT EXACTLY SATURATES ONE TICK OF AIR ACCELERATION ----
// AirAccelerate adds `accelspeed = sv_airaccelerate * wishspeed * frametime * surfaceFriction`
// and caps it at `addspeed = GetAirSpeedCap() - dot(velocity, wishdir)` (gamemovement.cpp:1720-
// 1745, cap 30 in gamemovement.h:104 and CTFGameMovement::GetAirSpeedCap for the normal case).
// With the wish perpendicular to velocity the dot is 0, so the cap is the full 30 and the wish
// that reaches it is 30 / (airaccel * dt).
// IT WAS WRITTEN AS THE CONSTANT 200, and 200 is that arithmetic at 1/66.7 s -- 192 at CSGO's 64
// tick, 198 at TF2's 66, and anything at all on a server that runs neither. Both readers use it
// as a UNIT (AS drives in at 1x, the EB lip steer asks for a fraction of it), so a wrong unit is
// a wrong fraction on every tick, not merely a slow approach. Derived per call now: the cvars
// are cached lookups and this runs once per steer, not once per trace.
static float AirWishSaturate()
{
    const float flAccel = CvarFloat("sv_airaccelerate", 10.f);
    const float flPerUnit = flAccel * TickInterval();
    if (flPerUnit < 1e-4f) return 450.f;   // airaccel 0 -> no wish buys anything; ask for max
    return std::clamp(30.f / flPerUnit, 1.f, 450.f);
}

// AS wall-detect reach FLOOR. An earlier build set this to 8 and passed pVel = nullptr, because detect's
// lookahead (`max(0, dot(vel,dir) * TickInterval())`) was then the WHOLE reach: 2u standing,
// ~9.5u at 500 u/s. Making it constant fixed the slow case and broke the fast one -- the log read:
// `nowall=321 ran=651` = HALF of AS's ticks exit "no wall found", every one of them at
// xy 250-320 u/s. That is "graze a wall very fast and the bind does nothing": at 300 u/s the
// hull crosses 4.5u per tick, so a wall 9u out is out of reach this tick and behind us the next.
// 8u FLOOR + the per-direction lookahead ON TOP is both cases at once: standing still keeps the
// full 8 (an earlier build's fix intact), 500 u/s into the wall reaches ~15.8. The sweep sims a whole tick
// forward, so a wall one tick's travel away is a wall it can legitimately work
static constexpr float k_asWallReach = 8.f;


// ==== CMisc::CorrectMovement port ====
// SDK::FixMovement lived here too and had zero call sites (/W4 C4505) -- CorrectMovement
// is what every silent-move path in this file actually uses. Deleted, not commented out.
void CorrectMovement(UserCmd_t* pCmd, Vec3 vWishAngle, Vec3 vOldAngles)
{
    if (vOldAngles.x == vWishAngle.x && vOldAngles.y == vWishAngle.y && vOldAngles.z == vWishAngle.z)
        return;

    Vec3 vWishForward, vWishRight, vWishUp;
    Vec3 vCmdForward, vCmdRight, vCmdUp;
    Vec3 vMoveData(pCmd->forwardmove, pCmd->sidemove, pCmd->upmove);

    AngleVectors(vWishAngle, &vWishForward, &vWishRight, &vWishUp);
    AngleVectors(vOldAngles, &vCmdForward, &vCmdRight, &vCmdUp);

    float flWishForwardLen = vWishForward.Length2D();
    float flWishRightLen = vWishRight.Length2D();
    float flWishUpLen = fabsf(vWishUp.z);
    if (flWishForwardLen == 0.f || flWishRightLen == 0.f || flWishUpLen == 0.f) return;

    Vec3 vWishForwardNorm(vWishForward.x / flWishForwardLen, vWishForward.y / flWishForwardLen, 0.f);
    Vec3 vWishRightNorm(vWishRight.x / flWishRightLen, vWishRight.y / flWishRightLen, 0.f);
    Vec3 vWishUpNorm(0.f, 0.f, vWishUp.z / flWishUpLen);

    float flCmdForwardLen = vCmdForward.Length2D();
    float flCmdRightLen = vCmdRight.Length2D();
    float flCmdUpLen = fabsf(vCmdUp.z);
    if (flCmdForwardLen == 0.f || flCmdRightLen == 0.f || flCmdUpLen == 0.f) return;

    Vec3 vCmdForwardNorm(vCmdForward.x / flCmdForwardLen, vCmdForward.y / flCmdForwardLen, 0.f);
    Vec3 vCmdRightNorm(vCmdRight.x / flCmdRightLen, vCmdRight.y / flCmdRightLen, 0.f);
    Vec3 vCmdUpNorm(0.f, 0.f, vCmdUp.z / flCmdUpLen);

    Vec3 vCorrectMove;
    vCorrectMove.x = vCmdForwardNorm.x * (vWishForwardNorm.x * vMoveData.x + vWishRightNorm.x * vMoveData.y) +
        vCmdForwardNorm.y * (vWishForwardNorm.y * vMoveData.x + vWishRightNorm.y * vMoveData.y);
    vCorrectMove.y = vCmdRightNorm.x * (vWishForwardNorm.x * vMoveData.x + vWishRightNorm.x * vMoveData.y) +
        vCmdRightNorm.y * (vWishForwardNorm.y * vMoveData.x + vWishRightNorm.y * vMoveData.y);
    vCorrectMove.z = vCmdUpNorm.z * vWishUpNorm.z * vMoveData.z;

    vCorrectMove.x = std::clamp(vCorrectMove.x, -450.f, 450.f);
    vCorrectMove.y = std::clamp(vCorrectMove.y, -450.f, 450.f);
    vCorrectMove.z = std::clamp(vCorrectMove.z, -320.f, 320.f);

    pCmd->forwardmove = vCorrectMove.x;
    pCmd->sidemove = vCorrectMove.y;
    pCmd->upmove = vCorrectMove.z;
}

// ============================================================================
// wave 2 helpers (Aletherium TB_* primitives + engine predicted-frame anchor)
// ============================================================================


static void ClampAnglesMv(Vec3& v)
{
    v.x = std::clamp(NormalizeAngle(v.x), -89.f, 89.f);
    v.y = NormalizeAngle(v.y);
    v.z = 0.f;
}

// line trace (no extents)
static void TraceLine(const Vec3& start, const Vec3& end, unsigned mask, void* filter, GameTrace_t* tr)
{
    ++s_tracesThisCmd;
    if (!g_engineTrace) { tr->fraction = 1.f; return; }
    Ray_mv ray;
    ray.Init(start, end);
    using TraceRay_t = void(__fastcall*)(void*, const Ray_mv&, unsigned, void*, GameTrace_t*);
    __try {
        void** vt = *(void***)g_engineTrace;
        ((TraceRay_t)vt[kTrace_TraceRay])(g_engineTrace, ray, mask, filter, tr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { tr->fraction = 1.f; }
}

// ---- the two questions a trace cannot answer, asked of the BSP directly --------------------
// IEngineTrace slot 0 GetPointContents and slot 16 PointOutsideWorld (src/public/engine/
// IEngineTrace.h, in that order after the confirmed TraceRay 4 / GetBrushesInAABB 13 /
// GetBrushInfo 15 this file already calls). Both walk one leaf, so they cost a fraction of the
// hull trace they replace, and they answer what a swept trace CANNOT: a hull trace reports where
// a sweep stopped, never whether the point it started from is buried in a brush or sitting in the
// void outside every playable area. The whole-map scan asks both before it spends a sim.
static int MV_PointContents(const Vec3& p)
{
    if (!g_engineTrace) return 0;
    using Contents_t = int(__fastcall*)(void*, const Vec3&, void*);
    __try {
        void** vt = *(void***)g_engineTrace;
        return ((Contents_t)vt[0])(g_engineTrace, p, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// "Tests a point to see if it's outside any playable area" -- the engine's own words. True for
// the solid void a sealed map is wrapped in, which is where half the junk a whole-map brush walk
// turns up lives (the OUTSIDE faces of the brushes that do the sealing).
static bool MV_PointOutsideWorld(const Vec3& p)
{
    if (!g_engineTrace) return false;
    using Outside_t = bool(__fastcall*)(void*, const Vec3&);
    __try {
        void** vt = *(void***)g_engineTrace;
        return ((Outside_t)vt[16])(g_engineTrace, p);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// SLOT CHECK, ONCE PER SESSION. PointOutsideWorld sits one index past the last slot of this
// interface anything here has confirmed in the wild (TraceRay 4, GetBrushesInAABB 13,
// GetBrushInfo 15 all work), and TF2 ships EngineTraceClient004 against the SDK header's 003. A
// wrong slot would not crash -- the __try sees to that -- it would answer nonsense, and nonsense
// here means silently throwing away every spot the finder found, which is the worst kind of bug
// to ship into a search. So it is asked about a point we KNOW is inside the map (the local
// player, who is standing in it) before it is believed, and switched off for the session if it
// gets that wrong. GetPointContents is slot 0 and needs no such check: a vtable's first entry is
// the one thing a version bump cannot move.
static int s_mvOutsideOk = -1;            // -1 not asked yet, 0 do not trust, 1 trusted
static void MV_ValidateOutsideWorld(const Vec3& vKnownInside)
{
    if (s_mvOutsideOk >= 0 || !g_engineTrace) return;
    s_mvOutsideOk = MV_PointOutsideWorld(vKnownInside) ? 0 : 1;
    DebugLog("MV: PointOutsideWorld self-check at %.0f %.0f %.0f -> %s", vKnownInside.x,
             vKnownInside.y, vKnownInside.z,
             s_mvOutsideOk ? "ok, out-of-bounds gate armed"
                           : "it called the player's own hull OUTSIDE the map -- wrong slot on "
                             "this build, gate OFF (CONTENTS_SOLID still applies)");
}

// a spot no player can ever occupy: buried in a brush, or off the edge of the map
static bool MV_PointUnusable(const Vec3& p)
{
    if ((MV_PointContents(p) & 0x1) != 0) return true;      // CONTENTS_SOLID
    return s_mvOutsideOk == 1 && MV_PointOutsideWorld(p);
}

// ---- which BSP AREA a point sits in (-1 = could not ask) ---------------------------------
// IVEngineClient GetBSPTreeQuery 42 + GetLeavesArea 57 (cdll_int.h order, == Amalgam x64
// IVEngineClient.h, counted from the confirmed GetLevelName 51) and ISpatialQuery slot 1
// EnumerateLeavesAtPoint (bsptreedata.h:71: LeafCount 0, then the enumerators, no dtor). The
// whole-map scan asks it to tell the 3D skybox (its own area) from the map.
// Validated once like PointOutsideWorld: the tree's LeafCount (its slot 0) has to equal the
// engine's LevelLeafCount (41). Anything else = not the interface we think, gate stays off.
struct MvLeafCatch   // ISpatialLeafEnumerator: one virtual, first leaf wins, stop
{
    virtual bool EnumerateLeaf(int leaf, intptr_t ctx) { *reinterpret_cast<int*>(ctx) = leaf; return false; }
};
static MvLeafCatch s_mvLeafCatch;
static int s_mvAreaOk = -1;               // -1 not asked yet, 0 do not trust, 1 trusted
static int MV_PointArea(const Vec3& p)
{
    if (!g_engine) return -1;
    __try {
        void** ve = *(void***)g_engine;
        void* q = ((void*(__fastcall*)(void*))ve[42])(g_engine);
        if (!q) return -1;
        void** vq = *(void***)q;
        if (s_mvAreaOk < 0)
        {
            const int nTree = ((int(__fastcall*)(void*))vq[0])(q);
            const int nLvl  = ((int(__fastcall*)(void*))ve[41])(g_engine);
            s_mvAreaOk = (nTree > 0 && nTree == nLvl) ? 1 : 0;
            DebugLog("MV: BSP area query self-check: tree leaves %d, level leaves %d -> %s",
                     nTree, nLvl, s_mvAreaOk ? "ok" : "MISMATCH, area gate OFF");
        }
        if (s_mvAreaOk != 1) return -1;
        int leaf = -1;
        ((bool(__fastcall*)(void*, const Vec3&, void*, intptr_t))vq[1])(q, p, &s_mvLeafCatch, (intptr_t)&leaf);
        if (leaf < 0) return -1;
        return ((int(__fastcall*)(void*, int*, int))ve[57])(g_engine, &leaf, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// WorldProps now rejects trigger / non-solid volumes itself -> the two filters are
// identical. Alias kept so wall-detect / texbug call sites read as brush-only intent.
using TraceFilterWorldBrush = TraceFilterWorldProps;


// target catch vz: -(sv_gravity * interval * 0.5) ~= -6.0 @66.67 tick (Aletherium
// GetTargetPredictZVelocity -- the "landed on a surface this tick" pin velocity).
// cached per cmd: IsTargetVz runs 100s of times inside TB/AS sweep sims -- cvar
// walk (strcmp cache + parent deref) each call is pure waste, gravity is fixed mid-cmd
static float s_targetPredictZVel = -6.f;
static void RefreshTargetPredictZVel()
{
    s_targetPredictZVel = -(CvarFloat("sv_gravity", 800.f) * TickInterval() * 0.5f);
}
static float TargetPredictZVel() { return s_targetPredictZVel; }
static bool IsTargetVz(float vz, float eps) { return fabsf(vz - TargetPredictZVel()) <= eps; }


// projection of the player half-extents onto the wall normal
static float TB_WallSupportDistance(void* pLocal, const Vec3& vWallNormal)
{
    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    const Vec3 vHalf(fmaxf(fabsf(vMins.x), fabsf(vMaxs.x)),
                     fmaxf(fabsf(vMins.y), fabsf(vMaxs.y)),
                     fmaxf(fabsf(vMins.z), fabsf(vMaxs.z)));
    return fabsf(vWallNormal.x) * vHalf.x + fabsf(vWallNormal.y) * vHalf.y + fabsf(vWallNormal.z) * vHalf.z;
}

// ---- shared wall-scan / support helpers (creator texturebug base, 2026-07-12) ----

// 108th: how far off the ride plane's own offset a hit may sit and still count as that plane.
// 2u covers a seam step and brush float; a parallel wall across a gap never does
static constexpr float k_tbLockPlaneSlack = 2.f;

// per-Z line-trace scan for nearest facing wall (16 dirs). hull prefilter per angle --
// hull can only hit MORE than the lines, pure perf. hull hit with facing near-vertical
// normal also rescues short walls sitting between the scan lines.
//
// pVel = 1-tick lookahead: each ray is extended by the distance the player covers
// along THAT direction in one tick. The sweeps sim a tick ahead, so a detect that only
// sees walls already touching hands them a wall one tick after the variant that would
// have caught it -- the "delay before it activates on a wall". Extension is per-angle
// and clamped at 0, so a wall you are drifting away from never reaches further than
// extra_reach and cold/slow ticks keep the base 2u window (no early steering/duck)
//
// pRequireNormal = plane lock: ignore hits that aren't the given plane. Within one scan
// angle `best` is just the min fraction over every z ray, with nothing tying it to the
// plane we were on last tick -- a neighbouring brush poking out a hair at ANY z along
// the hull wins it, and then angle_wall moves and `sign` can flip, aiming the whole
// sweep at the wrong plane. That is the random stop at seams between brushes. Riders
// pass their ride plane here; callers fall back to an unlocked scan so leaving the
// plane for real still works
// COLDTIRES 120th -- the unlocked answer a FAILED locked scan already had in hand. A
// locked scan traces every direction the unlocked retry would trace, so keeping its first
// any-plane hit turns the seam tick's SECOND full scan into zero traces
struct TbWallHit_t
{
    GameTrace_t trace{};
    float angle = 0.f;
    bool  valid = false;
};

// the locked pass needs n.N >= 0.99 AND n.dir <= -0.1, so N.dir must be <= ~0.0413 for any
// match at all -- past that bound no hit in that direction can ever be returned, so the whole
// direction (hull + column + lines) is skippable
static constexpr float k_tbLockDirBound = 0.04f;

static bool MV_DetectWallLines(void* pLocal, const Vec3& start_pos, const Vec3& mins, const Vec3& maxs,
                               float scan_start, GameTrace_t& out_trace, float& out_angle,
                               float max_normal_z = 0.35f, float extra_reach = 2.f,
                               const Vec3* pVel = nullptr, const Vec3* pRequireNormal = nullptr,
                               const float* pRequirePlaneD = nullptr,
                               TbWallHit_t* pOutFallback = nullptr)
{
    const float max_radius = MV_PI * 2.f;
    const float step = max_radius / 16.f;
    const float z_step = g_tbScanStep > 0.5f ? g_tbScanStep : 1.f;
    const float half_x = fmaxf(fabsf(mins.x), fabsf(maxs.x));
    const float half_y = fmaxf(fabsf(mins.y), fabsf(maxs.y));
    const float lookahead_dt = pVel ? TickInterval() : 0.f;
    // FALL LOOKAHEAD, and it is the other half of the SHORT-LEDGE fix. The scan only ever
    // looked ahead HORIZONTALLY, so it saw a face that is level with the hull right now -- but
    // the sweeps sim a tick FORWARD, and at 400 u/s the hull drops ~6u in that tick. A lip 6u
    // below the feet is a face the sim can absolutely clip on, and the scan answered "no wall",
    // so TB never swept the one tick that could have caught it. Falling only (the direction of
    // travel), one tick's worth, capped at 32u so a terminal-velocity fall cannot turn the scan
    // into a floor detector. Callers that pass no velocity (WC/AS) are untouched
    const float fall_look = (pVel && pVel->z < 0.f) ? fminf(-pVel->z * lookahead_dt, 32.f) : 0.f;
    const Vec3  scan_mins(mins.x, mins.y, mins.z - fall_look);
    TraceFilterWorldBrush fil; fil.Setup(pLocal);
    TbWallHit_t fallback{};

    // ---- BROADPHASE: ONE TRACE INSTEAD OF SIXTEEN ------------------------------------------
    // Every one of the 16 directions below opens with a hull trace before anything can reject
    // it, so standing in open air cost 16 hull traces to answer "no wall" -- and TB's ladder
    // (texturebug.inl) runs the whole scan twice on a full miss, so 32 per tick, every tick the
    // bind is held. AutoAlign / WallClimb / AirStuck pay it per tick too.
    // The predictive sweep is the site that hurts most: it runs one scan per predicted path
    // step against k_tbPredTraceCap = 400, so in open air the cap was spent after ~25 steps.
    // At 1 trace per empty step instead of 16 the same budget reaches far further, which is a
    // RANGE win for the sweep on top of the tick cost.
    // The box is a superset of every trace the loop can run (mv_scanbound.h derives it, and
    // tests/mvscan_test.cpp brute-forces that claim against the loop's own arithmetic), so a
    // miss here cannot hide a wall the loop would have found.
    {
        const float reach_xy = mv::ScanReachXY(half_x, half_y, extra_reach,
                                               pVel ? pVel->Length2D() : 0.f, lookahead_dt);
        GameTrace_t wide{};
        TraceHull(start_pos, start_pos,
                  Vec3(-reach_xy, -reach_xy, scan_mins.z), Vec3(reach_xy, reach_xy, maxs.z),
                  MASK_PLAYERSOLID, &fil, &wide);
        if (!wide.DidHit())
        {
            if (pOutFallback) *pOutFallback = fallback;   // empty, as a full miss leaves it
            return false;
        }
    }

    // 0.99 ~= 8 deg: coplanar neighbours (dot 1.0) stay on the ride, a genuinely
    // different face doesn't. Displacements/brush staircases lose the lock and come
    // back through the caller's unlocked rescan
    // 108th -- normals alone are NOT a plane. A PARALLEL wall (other side of a doorway, the
    // far face of the same brush) passes a normal-only lock, and the ride then measures its
    // support against one 24u away on alternate ticks: minsup flips -0.03 / 7.97 / -0.03
    // while lock=1. Pin the ride to the plane's OFFSET too (the plane's own dist, not endpos:
    // the hull prefilter's endpos sits a support distance short of the surface)
    const auto plane_ok = [&](const cplane_mv& p) -> bool
    {
        if (!pRequireNormal) return true;
        if ((p.normal.x * pRequireNormal->x + p.normal.y * pRequireNormal->y
             + p.normal.z * pRequireNormal->z) < 0.99f) return false;
        if (pRequirePlaneD && fabsf(p.dist - *pRequirePlaneD) > k_tbLockPlaneSlack) return false;
        return true;
    };

    for (float a = scan_start; a < scan_start + max_radius; a += step)
    {
        const float angle = fmodf(a, max_radius);
        const float dir_x = cosf(angle);
        const float dir_y = sinf(angle);

        // locked pass: this direction cannot hold the ride plane, and the unlocked answer it
        // would otherwise feed is already in hand -> nothing here can ever be returned
        if (pRequireNormal && fallback.valid
            && (pRequireNormal->x * dir_x + pRequireNormal->y * dir_y) > k_tbLockDirBound)
            continue;

        const float ax = fmaxf(fabsf(dir_x), 1e-6f);
        const float ay = fmaxf(fabsf(dir_y), 1e-6f);
        const float lookahead = pVel
            ? fmaxf(0.f, (pVel->x * dir_x + pVel->y * dir_y) * lookahead_dt) : 0.f;
        // the hull's own support along this direction: centre -> the corner the sweep leads with
        const float support = fminf(half_x / ax, half_y / ay);
        // rays leave the CENTRE, so they need the support on top to clear the hull's own width.
        // reach past the hull surface = extra_reach + lookahead, which is the contract
        const float ray_length = support + extra_reach + lookahead;
        const Vec3  ray_end(start_pos.x + dir_x * ray_length, start_pos.y + dir_y * ray_length, start_pos.z);

        // ---- THE PREFILTER WAS SWEEPING A WHOLE HULL PAST THE END OF ITS OWN REACH ----
        // "wall reach" is meant to be how far past the hull a face still counts, and every LINE
        // here honours that (they start at the centre, so ray_length carries the support). The
        // prefilter is a HULL trace, and a hull trace already occupies the support before it moves
        // an inch -- sweeping it ray_length reaches support + extra_reach + lookahead PAST THE
        // FACE, i.e. 24u on an axis and 34u on a diagonal of pure over-reach that the slider does
        // not appear in. At the slider's floor (8) the extra 24-34 is three to four times the
        // whole setting, which is exactly "the reach slider does nothing and it grabs walls I am
        // nowhere near": with the centre column empty (a wall off to one side) the prefilter is
        // the ONLY thing that can answer, and it answers with that far face, and AirStuck drives
        // at every face the scan returns. Sweep it the reach and nothing else -- then the hull,
        // the column and the lines all mean the same thing by "reach".
        const float pre_length = fmaxf(extra_reach + lookahead, 0.f);
        const Vec3  pre_end(start_pos.x + dir_x * pre_length, start_pos.y + dir_y * pre_length,
                            start_pos.z);

        GameTrace_t pre{};
        TraceHull(start_pos, pre_end, scan_mins, maxs, MASK_PLAYERSOLID, &fil, &pre);
        if (!pre.DidHit())
            continue;

        // THE COLUMN SWEEP, and it is the whole SHORT-LEDGE fix. The z grid samples lines
        // g_tbScanStep apart (the user's own cfg runs 8), so any lip shorter than the step can
        // sit ENTIRELY BETWEEN two lines and the scan never sees it -- the only rescue was the
        // full-width hull prefilter below, whose nearest hit is any face out to 24u either side.
        // This is one box sweep with ZERO x/y extents over the hull's full z span: the player's
        // own centre column, swept along dir, at byte-identical geometry to the lines. It crosses
        // every face the lines cross AND every face between them. Three wins at once:
        //   miss  -> the whole column is empty, skip ~11-73 line traces
        //   hit   -> it IS the nearest facing face in the column, so it is the answer (short
        //            ledges included) and it bounds how near any line could come
        //   bound -> the line loop stops the instant the answers it still owes are at the bound
        GameTrace_t seg{};
        TraceHull(start_pos, ray_end, Vec3(0.f, 0.f, scan_mins.z), Vec3(0.f, 0.f, maxs.z),
                  MASK_PLAYERSOLID, &fil, &seg);
        const bool  column_empty = !seg.DidHit();
        const float column_bound = seg.fraction + 1e-4f;   // float slack

        GameTrace_t best_match{}, best_any{};
        best_match.fraction = best_any.fraction = 1.f;
        bool have_match = false, have_any = false;

        // `any` ignores the plane lock (= what the unlocked retry would pick here), `match`
        // obeys it. Two answers per direction, one set of traces
        const auto consider = [&](const GameTrace_t& tr, bool to_any, bool to_match)
        {
            if (tr.fraction >= 1.f) return;
            if (fabsf(tr.plane.normal.z) > max_normal_z) return;
            if (tr.plane.normal.x * dir_x + tr.plane.normal.y * dir_y > -0.1f) return;
            if (to_any && (!have_any || tr.fraction < best_any.fraction))
                { best_any = tr; have_any = true; }
            if (to_match && plane_ok(tr.plane)
                && (!have_match || tr.fraction < best_match.fraction))
                { best_match = tr; have_match = true; }
        };
        const auto column_settled = [&]() -> bool
        {
            if (!have_any || best_any.fraction > column_bound) return false;
            if (pRequireNormal && (!have_match || best_match.fraction > column_bound)) return false;
            return true;
        };

        if (!column_empty)
            consider(seg, true, true);

        for (float z = start_pos.z + scan_mins.z; !column_empty && z <= start_pos.z + maxs.z; z += z_step)
        {
            if (column_settled()) break;
            GameTrace_t tr{};
            TraceLine(Vec3(start_pos.x, start_pos.y, z), Vec3(ray_end.x, ray_end.y, z),
                      MASK_PLAYERSOLID, &fil, &tr);
            consider(tr, true, true);
        }

        // hull rescue, unchanged in intent: a face neither the centre column nor the lines reach
        // (a lip off to one side). Feeds only the answer that came up empty
        if (!have_any || !have_match)
            consider(pre, !have_any, !have_match);

        const bool want_match = pRequireNormal != nullptr;
        if (want_match ? have_match : have_any)
        {
            out_trace = want_match ? best_match : best_any;
            out_angle = a;
            if (pOutFallback) *pOutFallback = fallback;
            return true;
        }
        if (!fallback.valid && have_any)
            { fallback.trace = best_any; fallback.angle = a; fallback.valid = true; }
    }
    if (pOutFallback) *pOutFallback = fallback;
    return false;
}

// distance hull face -> wall along -normal at wall-hit height (fraction*32 minus
// half-extent projection). ~0 = flush against the band. call while entity holds
// PREDICTED state (right after Pred_Simulate)
// how many sims this tick had to fall back to plane math (diag: supf=)
static int s_tbSupportFallback = 0;

// pOrigin = measure a state the entity is NOT standing in (the analytic probe's end position,
// which was never simulated). Null = read the live predicted origin, exactly as before
static float MV_SupportDistance(void* pLocal, const GameTrace_t& wall, const Vec3* pOrigin = nullptr)
{
    const Vec3 origin = pOrigin ? *pOrigin : EntV3(pLocal, g_offOrigin);
    const Vec3 flat(wall.plane.normal.x, wall.plane.normal.y, 0.f);

    // THE 32 WAS AN AXIS-ALIGNED HARDCODE, and it is the "wall not aligned with the
    // player bounding box" bug. The hull's support along a wall normal is |nx|*24 + |ny|*24
    // (TB_WallSupportDistance): 24.0 when the normal lies on x or y, 33.94 at 45 deg. 32 is
    // exactly 24 + 8 = the AXIS-ALIGNED half plus the same 8u margin every other reach here
    // uses -- so on a diagonal wall the ray stopped 1.94u SHORT of the face it was sent to
    // measure. Worse, the ray was built as normal * -32 and then flattened by the TraceLine
    // (both endpoints sit at wall.endpos.z), so its real xy length was 32 * |normal.xy| --
    // shorter still on anything that is not perfectly vertical. Either way the face is out of
    // reach: fraction comes back 1.0 and every diagonal tick falls to the plane-math branch,
    // or some nearer brush answers instead and the caller gets a support distance measured
    // against the wrong surface.
    // half + 8 reproduces 32 EXACTLY on an axis-aligned vertical wall (24 + 8), so the whole
    // axis-aligned population -- every minsup=-0.031 in the log -- is arithmetically unchanged.
    // Only off-axis normals move, which is the population that is broken
    const float flat_len = flat.Length2D();
    if (flat_len < 1e-4f) return 0.f;              // face has no horizontal normal at all
    const Vec3  dir = flat * (1.f / flat_len);     // unit, in the plane the TraceLine works in
    // and an earlier fix corrected the RAY but left the SUPPORT measured off the same broken vector.
    // TB_WallSupportDistance is |n.x|*hx + |n.y|*hy + |n.z|*hz, which is the box's support only
    // for a UNIT n; it was handed `flat`, the normal with z zeroed and NOT renormalized, whose
    // length is sqrt(1-nz^2). So on a tilted face `half` came back short by that factor -- 6% at
    // nz 0.35, 24% at nz 0.65 (AirStuck's limit) = up to 5.8u -- and the returned support is
    // `hit_distance - half`, so a hull sitting FLUSH on a slanted wall reported itself 5.8u off
    // it. Nothing that gates on flush (WC's creep < 0.2, TB's minsup, AS's align) could ever fire
    // there, so the feature kept pressing in forever: the "stuck on slanted walls" half.
    // dir is the unit vector the ray already uses, and on a vertical wall (nz 0) dir == flat, so
    // every axis-aligned and every diagonal-yaw number in the logs is unchanged to the bit
    const float half = TB_WallSupportDistance(pLocal, dir);
    const float ray_len = half + 8.f;

    TraceFilterWorldBrush fil; fil.Setup(pLocal);
    GameTrace_t tr{};
    TraceLine(Vec3(origin.x, origin.y, wall.endpos.z),
              Vec3(origin.x - dir.x * ray_len, origin.y - dir.y * ray_len, wall.endpos.z),
              MASK_ALL_MV, &fil, &tr);
    if (tr.fraction < 1.f)
        return tr.fraction * ray_len - half;

    // THE 8.000 SENTINEL, and C:otox-tf2\debug.log is nothing but it: one center LINE at
    // wall.endpos.z, 32u long. Miss it and this returned 32 - half = 8.000 EXACTLY on a TF2 hull
    // (half 24) -- every tick of every ride in the log, pin ticks included. The line misses on
    // real geometry the detect DOES see: a hull-prefilter hit (endpos.z is the feet, the face is
    // a lip further up) and any face the perpendicular from the center does not reach at that
    // one z. 8.000 is > every support gate in TB, so the CREEP -- "THE hold, keeps the eps band
    // alive so the next seam tick re-pins" -- could never fire, every no-pin tick fell through to
    // the bare drive-in (pbr=6, drv climbing 1..11), the hull bled ~1.3u of z per pin cycle and
    // the ride died the moment one cycle missed its seam.
    // The plane is KNOWN, so measure against it: signed distance origin -> plane, minus the same
    // hull half-extent. Identical quantity, no ray to miss. No "sticks to nothing" risk -- the
    // detect already proved this brush is inside the hull sweep before the sweep ever runs
    ++s_tbSupportFallback;
    const float plane_dist = origin.x * wall.plane.normal.x + origin.y * wall.plane.normal.y
                           + origin.z * wall.plane.normal.z - wall.plane.dist;
    // this branch measures along the FULL 3D normal (plane_dist carries the z term), so
    // its support has to be the 3D one too; `half` above is the horizontal ray's, which drops the
    // |n.z|*hz column. Identical on a vertical face (n.z 0), so no logged number moves
    return plane_dist - TB_WallSupportDistance(pLocal, wall.plane.normal);
}


// movement input projected into world space points outward along the wall normal?
static bool TB_SteeringAwayFromWall(const UserCmd_t* pCmd, const Vec3& vWallNormal,
                                    float flForwardMove, float flSideMove, float flMinDot = 0.3f)
{
    if (flForwardMove == 0.f && flSideMove == 0.f)
        return false;

    Vec3 vForward, vRight;
    AngleVectors(Vec3(0.f, pCmd->viewangles.y, 0.f), &vForward, &vRight, nullptr);

    Vec3 vWish = vForward * flForwardMove + vRight * flSideMove;
    vWish.z = 0.f;
    const float flWishLen = vWish.Length2D();
    if (flWishLen < 1.f)
        return false;
    vWish *= (1.f / flWishLen);

    Vec3 vNormal = vWallNormal;
    vNormal.z = 0.f;
    const float flNormLen = vNormal.Length2D();
    if (flNormLen < 0.001f)
        return false;
    vNormal *= (1.f / flNormLen);

    return (vWish.x * vNormal.x + vWish.y * vNormal.y) > flMinDot;
}


// ==== feature state ====
static bool s_rawJumpHeld = false;   // real IN_JUMP before AutoJump strips it
// the WHOLE cmd as the player sent it, captured on the same line as s_rawJumpHeld and
// for the same reason: by the time RunPost is reached, AutoJump/PS assist/WallClimb have all
// written buttons, so pCmd there answers "what will the server run", never "what did he press".
// The route calculator's live recorder needs the second question and nothing else does.
static int  s_rawButtons = 0;
static float s_rawFwd = 0.f, s_rawSide = 0.f;   // botox m_user_*_move_raw: the keys before any feature (wall climb's "you leave")
// IN_JUMP of the cmd we actually SENT last tick = the engine's m_nOldButtons bit, mirrored.
// CheckJumpButton refuses a jump while that bit is set (tf_gamemovement.cpp:1235) and ONLY a
// jumpless cmd clears it (gamemovement.cpp:1441), so this is the one honest answer to "will
// the engine take a press this tick". Written at the very end of Movement_RunImpl, after every
// feature has authored the cmd -- reading pCmd mid-pipeline would answer for a half-built cmd
static bool s_prevCmdJump = false;
static bool s_prevCmdDuck = false;   // last cmd's final IN_DUCK = m_nOldButtons for the route calc
static bool s_edgeJumpGround = false;
// edge jump injected IN_JUMP on THIS cmd, and the engine will honour it (fresh press
// off a grounded start). Set post-pred by EdgeJump, read by MiniJump three lines later. Needed
// because that press lands AFTER Pred_Start, so the predicted velocity mini jump reads is the
// walk-off (vz ~ -6), never the launch (vz 271) -- see the takeoff gate in MiniJump
static bool s_edgeJumpFired = false;
static bool s_strafeWeaveLeft = false;
static int  s_prePredFlags = 0;      // m_fFlags captured before prediction (long/mini jump)
// mini jump decides its duck for THIS cmd, and the engine runs Duck() at the START of it --
// so the state that decides the press is the one BEFORE prediction, not the predicted end
static bool s_prePredDucking = false;   // m_Local.m_bDucking at cmd start
static Vec3 s_prePredOrigin = {};       // feet at cmd start
static Vec3 s_prePredVel = {};       // velocity before prediction (pixel surf ride detect, mini jump)
// the REAL tickcount for this cmd, sampled with the rest of the pre-pred snapshot.
// g_globalVars->tickcount is rewritten by Pred_Start's sims, so a feature running INSIDE the
// predicted window reads a tick one ahead of the one Movement_RunImpl's tail sees after
// Pred_End. an earlier build's MJ: lines were stamped with the inflated tick and MJT: with the restored
// one, so `REBOUND after 5 ticks` and `MJT: t=4` were the same cmd under two different names --
// an hour of log reading to notice. Both now stamp from here.
static int  s_prePredTick = 0;

static bool s_miniShouldDuck    = false;   // mini jump holds the takeoff duck

// ==== features (Aletherium CMisc ports) ====

static bool FakeMove_WallJumpReady(void* pLocal);   // fakemove.inl

// Bunnyhop -- hold jump, re-press only on the grounded non-duck landing tick.
static void AutoJump(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_bhop) return; // plain on/off, no bind (user pick)

    static bool bStaticJump = false, bStaticGrounded = false, bLastAttempted = false;
    const bool bLastJump = bStaticJump, bLastGrounded = bStaticGrounded;
    const bool bCurJump = bStaticJump = (pCmd->buttons & IN_JUMP) != 0;
    const bool bCurGrounded = bStaticGrounded = EntGround(pLocal);

    if (bCurJump && bLastJump && (bCurGrounded ? !EntDucking(pLocal) : true))
    {
        if (!(bCurGrounded && !bLastGrounded))
            pCmd->buttons &= ~IN_JUMP;
        if (!(pCmd->buttons & IN_JUMP) && bCurGrounded && !bLastAttempted)
            pCmd->buttons |= IN_JUMP;
    }
    // fake wall jump: held jump passes on air ticks with a wall in reach. The tick before was
    // stripped, so the server reads a fresh press; staying at the wall keeps it held (no repeat),
    // leaving reach strips again so the next wall fires too
    if (bCurJump && !bCurGrounded && FakeMove_WallJumpReady(pLocal))
        pCmd->buttons |= IN_JUMP;
    bLastAttempted = (pCmd->buttons & IN_JUMP) != 0;
}

// ---- pixel surf ride state (declared here, written by PixelSurf / PS_AssistTick further down) ----
// These used to live down with the rest of the per-cmd state, below every pre-prediction feature.
// AutoJumpbug is pre-pred and needs them, so they moved up: a plain bool has no init order to
// break. Read from a pre-pred feature they hold LAST cmd's value (PixelSurf runs post-pred), which
// is the right question anyway -- a ride that was live a tick ago is a ride not to stomp
static bool s_shouldPixelSurf = false;   // PixelSurf ride latched (ducked OR standing)
static bool s_psShouldDuck = false;      // ...and this ride needs the crouch (false = standing surf)
static bool s_psAirDuckHeld = false;     // the surf duck is pressed and holds until the feet land
static bool s_psRidingNow = false;       // 2-tick sustained surf ride (Aletherium m_bPixelSurfingNow; PS assist gates on it)
// ...and what that ride LOOKS like, for the indicator only (ps_surf.h). s_psRidingNow is the vz
// signature, which an air stuck / texture bug / wedged hull share -- PixelSurf measures the shape
// of the thing holding us into this, and the publish at the tail asks ps::IsSurf once
static ps::SurfEvidence s_psEv{};
// were PixelSurf function statics; file scope so the route calculator can read a live
// ride latch (s_pixelSurfTicks = pin + 16) and the ride-velocity memory for a mid-air start
static bool s_prevSurfVel = false;
static int  s_pixelSurfTicks = 0;
static bool s_psAssistHopActive = false; // assist hop fired/pending -- arc is sim-validated, AirStuck must yield
static bool s_psAssistHopPj = false;     // ...and pixeljump planned it (indicator split)
// the ARC part only (hop in flight / launch pending, not the ride hold): every tick of it is the sim's
// proven cmd, so AirStuck + PixelSurf (both run after PS_AssistTick) stay off its move and duck
static bool s_psAssistArcOwns = false;
// botox `wall_detected`: AutoAlign's flat-wall find, or pixel surf's own 16-ray probe when
// AutoAlign is off (pixelsurf.inl PsWallDetectFallback). Pixel surf's hunt gate reads it
static bool s_aaWallDetected = false;
// AutoAlignStick wrote this cmd's move (drift hold; on sv_wallbug 0 any proven press too). PixelSurf's hold keeps it as is: its 450 gain
// strafe / wall press re-clips the hull on the face, and every clip adds the DIST_EPSILON drift
static bool s_aaStickOwns = false;
// the assist's own catch crouch, held until the feet land. Declared here (not in
// psassist.inl, which is #included far below) so WallClimb can yield to it -- see PS_AssistTick
static bool s_psAssistDuckHeld = false;
// the assist is reproducing PixelFinder's into-wall press at the seam. Declared up here
// for the same reason as the crouch -- AutoAlign runs after PS_AssistTick and writes the same
// two fields, so it has to be able to yield to it
static bool s_psAssistPressIn = false;

// ==== the live hull as a routesim state ======================================================
// One reader for every feature that wants to ask routesim.h a question about the CURRENT hull.
// It is the state the engine will start the NEXT simulated tick from, so it is read
// pre-prediction: m_nOldButtons is last cmd's committed pair (s_prevCmdDuck/Jump), not a
// half-built one. routecalc.inl's RcReadState is this function.
static rs::State MV_ReadSimState(void* pLocal, int tickNow)
{
    rs::State s;
    s.z        = EntV3(pLocal, g_offOrigin).z;
    s.vz       = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity).z : 0.f;
    const int fl = EntI(pLocal, g_offFlags);
    s.ground   = (fl & FL_ONGROUND) != 0;
    s.fDuck    = (fl & FL_DUCKING) != 0;
    s.ducking  = g_offDucking > 0 && EntB(pLocal, g_offDucking) != 0;
    s.ducked   = g_offDucked  > 0 && EntB(pLocal, g_offDucked)  != 0;
    s.duckTime = g_offDuckTime > 0 ? EntF(pLocal, g_offDuckTime) : 0.f;
    s.airDucked = g_offAirDucked > 0 ? EntI(pLocal, g_offAirDucked) : 0;
    s.oldDuck  = s_prevCmdDuck;              // m_nOldButtons: what the last cmd actually sent
    s.tick     = 0;
    // m_flDuckTimer is a curtime: turn "strips until" into the release tick that would give it
    if (g_offDuckTimer > 0 && g_globalVars)
    {
        const float flNow  = TickInterval() * float(tickNow);
        const float flLeft = EntF(pLocal, g_offDuckTimer) - flNow;
        if (flLeft > 0.f)
            s.releaseTick = (int)ceilf(flLeft / TickInterval()) - (int)lroundf(0.3f / TickInterval());
    }
    return s;
}

// the engine knobs routesim.h integrates against, read live off the cvars + this hull
static rs::Params MV_SimParams(void* pLocal)
{
    rs::Params p;
    p.dt      = TickInterval();
    p.gravity = CvarFloat("sv_gravity", 800.f);
    p.maxVel  = CvarFloat("sv_maxvelocity", 3500.f);
    p.clampAirDucks  = CvarFloat("tf_clamp_airducks", 1.f) != 0.f;
    p.duckTimerTicks = (int)lroundf(0.3f / p.dt);
    if (g_offModelScale > 0)
    {
        const float sc = EntF(pLocal, g_offModelScale);
        if (sc > 0.1f && sc < 10.f) p.scale = sc;
    }
    return p;
}

// Jumpbug -- hold jump while falling: auto-crouch the descent, then unduck + jump on the tick the
// floor sits in the [20u, 22u] band (FinishUnDuck teleports the origin down ~20u, CategorizePosition
// snaps, and the same cmd's press is taken off the fresh ground).
// The policy itself is rs::JumpbugCmd: one function, shared with the route calculator, so the bind
// and the calculator that promises the bind can never drift apart.
static bool s_jumpbugHit = false;   // the unduck+jump fired this tick (indicator flash)
static void AutoJumpbug(void* pLocal, UserCmd_t* pCmd)
{
    s_jumpbugHit = false;   // cleared every cmd: the flash is one tick, not a state
    if (!On(g_jumpbug, g_jumpbugKey, g_jumpbugMode) || !s_rawJumpHeld || EntGround(pLocal))
        return;

    // ---- DON'T HINDER A PIXEL SURF ----
    // Jumpbug writes the two buttons a surf cannot survive: the arm FORCES IN_DUCK and STRIPS
    // IN_JUMP, and the band tick does the opposite. A surf ride is a fall held beside a wall, so
    // any pixel low enough to have a floor inside the arm trace armed jumpbug the whole way down,
    // and the unduck drops the origin 20u -- the ride's catch band gone in one tick.
    if (g_jbYieldSurf && (s_psRidingNow || s_shouldPixelSurf || s_psAssistHopActive))
        return;
    if (g_offVelocity <= 0 || g_offOrigin <= 0 || g_offMins <= 0 || g_offMaxs <= 0 || !g_engineTrace)
        return;

    const Vec3 vel = EntV3(pLocal, g_offVelocity);
    if (vel.z >= 0.f) return; // must be descending

    const rs::Params p = MV_SimParams(pLocal);
    const float flUnduckHeight = 20.f * p.scale;

    const Vec3 vOrigin = EntV3(pLocal, g_offOrigin);
    const Vec3 vMins = EntV3(pLocal, g_offMins);
    const Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldProps filter; filter.Setup(pLocal);

    // ONE trace, not two. The old code traced the arm distance and then the band distance, but a
    // hull trace stops DIST_EPSILON off the plane it hit -- so its endpos IS routesim's floorEff,
    // and every distance the policy asks about is (origin.z - floorEff) arithmetic off that one
    // answer. Ducked the arm reaches flUnduckHeight further, because the air duck lifted the feet
    // 20u: without it the cmd after the duck sits outside the arm, IN_DUCK drops, the engine
    // unducks, and the second air release spends tf_clamp_airducks -- no jumpbug at all.
    const float flPerTickFall = fabsf(vel.z) * p.dt;
    const float flArmExtra = flPerTickFall * 3.f;
    const float flArmDistance = flUnduckHeight + 2.f + (flArmExtra > 8.f ? flArmExtra : 8.f)
                              + (EntDucking(pLocal) ? flUnduckHeight : 0.f);

    GameTrace_t armTrace{};
    TraceHull(vOrigin, vOrigin - Vec3(0, 0, flArmDistance), vMins, vMaxs, MASK_PLAYERSOLID, &filter, &armTrace);
    if (!armTrace.DidHit()) return;

    rs::World w;
    w.floor = true;
    w.floorEff = armTrace.endpos.z;   // where a landing leaves the origin = plane + 1/32

    const rs::State s = MV_ReadSimState(pLocal, g_globalVars ? g_globalVars->tickcount : 0);

    rs::Cmd c; bool bBand = false; float flMargin = 0.f;
    if (!rs::JumpbugCmd(s, w, p, c, bBand, flMargin))
        return;                       // floor inside the trace but outside the arm

    // ---- WILL IT HIT? IF NOT, KEEP YOUR HANDS OFF THE BHOP ----
    // The arm is destructive on a miss: IN_DUCK held and IN_JUMP stripped all the way down means
    // touching the floor crouched with no press, so the bhop the player was holding is eaten by a
    // jumpbug that was never going to happen. The band is 2u wide and one tick of fall at 500 u/s
    // is 7.5u, so stepping clean over it is the COMMON case. Fly the same policy forward against
    // routesim's engine port first and only press when a band tick really lands before the floor.
    // Free: no traces, no engine sims, w is the floor the arm trace already paid for.
    if (!bBand && g_jbPredict && !rs::JumpbugWillHit(s, w, p, s_rawJumpHeld))
    {
        static int s_jbDiagTick = 0;
        if (DiagOk(s_jbDiagTick, false))
            DebugLog("JB: no band ahead -- cmd left alone (floor %.2fu under, vz %.0f, airducks %d)",
                     s.z - w.floorEff, s.vz, s.airDucked);
        return;
    }

    // the two buttons ARE rs::JumpbugCmd's answer -- writing them out again here is how the bind
    // and the calculator drifted apart the last time. The band tick is the unduck + jump; every
    // other armed tick is the crouch with the press eaten.
    if (c.duck) pCmd->buttons |= IN_DUCK;  else pCmd->buttons &= ~IN_DUCK;
    if (c.jump) pCmd->buttons |= IN_JUMP;  else pCmd->buttons &= ~IN_JUMP;
    s_jumpbugHit = bBand;
}

// Auto strafe -- steer-to-look perfect strafe, silent wish angle via CorrectMovement.
static void AutoStrafe(void* pLocal, UserCmd_t* pCmd)
{
    if (!On(g_autoStrafe, g_autoStrafeKey, g_autoStrafeMode)) return;
    if (g_offVelocity <= 0 || g_offFlags <= 0) return;

    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP) return;
    if (EntI(pLocal, g_offFlags) & FL_ONGROUND) return;

    const Vec3 vVelocity = EntV3(pLocal, g_offVelocity);
    const float flSpeed = vVelocity.Length2D();
    if (flSpeed <= 1.f) return;

    const bool bBack    = (pCmd->buttons & IN_BACK) != 0;
    const bool bForward = (pCmd->buttons & IN_FORWARD) != 0;
    const bool bRight   = (pCmd->buttons & IN_MOVERIGHT) != 0;
    const bool bLeft    = (pCmd->buttons & IN_MOVELEFT) != 0;

    float flAngleOffset = 0.f;
    if (bBack)      { flAngleOffset = -180.f; if (bLeft) flAngleOffset -= 45.f; else if (bRight) flAngleOffset += 45.f; }
    else if (bLeft) { flAngleOffset = 90.f;   if (bForward) flAngleOffset -= 45.f; }
    else if (bRight){ flAngleOffset = -90.f;  if (bForward) flAngleOffset += 45.f; }

    // avoid wall collisions -- flip the strafe side if the heading it's about
    // to weave into is blocked within g_autoStrafeAvoidDist (botox movement.cpp avoid_walls).
    if (g_autoStrafeAvoidWalls && g_offOrigin > 0 && g_offMins > 0 && g_offMaxs > 0 && g_engineTrace)
    {
        const float flHeadYaw = NormalizeAngle(pCmd->viewangles.y + flAngleOffset);
        const float flHeadRad = DEG2RADF(flHeadYaw);
        const Vec3 vFrom = EntV3(pLocal, g_offOrigin);
        const Vec3 vTo = vFrom + Vec3(cosf(flHeadRad), sinf(flHeadRad), 0.f) * g_autoStrafeAvoidDist;
        TraceFilterWorldProps avoidFilter; avoidFilter.Setup(pLocal);
        GameTrace_t avoidTr{};
        TraceHull(vFrom, vTo, EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs),
                  MASK_PLAYERSOLID, &avoidFilter, &avoidTr);
        if (avoidTr.DidHit())
            flAngleOffset = NormalizeAngle(flAngleOffset + 180.f);
    }

    const Vec3 vOldAngles = pCmd->viewangles;
    const float flDesiredYaw = NormalizeAngle(vOldAngles.y + flAngleOffset);
    const float flVelYaw = RAD2DEGF(atan2f(vVelocity.y, vVelocity.x));
    const float flErr = NormalizeAngle(flVelYaw - flDesiredYaw);

    s_strafeWeaveLeft = flErr < 0.f;
    const bool bTurnLeft = s_strafeWeaveLeft;

    // ONE magnitude for both axes: the cos/sin below is a direction, and splitting it across
    // cl_forwardspeed and cl_sidespeed skews that direction the moment the two differ (the old
    // code did exactly that). The engine scales the pair down to m_flMaxspeed anyway.
    const float flMoveSpeed = (std::max)(CvarFloat("cl_sidespeed", 450.f), CvarFloat("cl_forwardspeed", 450.f));

    // the wish speed the ENGINE will use: AirMove clamps the move magnitude to m_flMaxSpeed
    // before AirAccelerate, and CheckParameters already pinned that to m_flMaxspeed. The old
    // code fed the raw 636 in and overstated accelspeed ~1.6x.
    const float flMaxSpeed = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f;
    const float flWishSpeed = (flMaxSpeed > 1.f && flMaxSpeed < flMoveSpeed) ? flMaxSpeed : flMoveSpeed;
    const airlean::Lean lean = airlean::Solve(flSpeed, flWishSpeed,
                                              CvarFloat("sv_airaccelerate", 10.f), TickInterval(), 30.f);

    // LEAN BACK WHILE THE HEADING IS STILL OFF. cosTurn is the furthest the wish can sit behind
    // perpendicular and still cost ZERO speed (airlean.h): at scout speed that is 8.6 deg/tick of
    // rotation against 4.3 at the pure gain angle, for free, because leaning back raises addspeed
    // so the full 60u of accelspeed lands instead of 30u. Fold back to cosGain as the error closes
    // -- under ~5 deg of heading error there is nothing left to steer, so the steady state is
    // the max-speed strafe it always was (full lean from 30 deg out).
    const float flLean = std::clamp((fabsf(flErr) - 5.f) / 25.f, 0.f, 1.f);
    const float flCosLean = lean.cosGain + (lean.cosTurn - lean.cosGain) * flLean;
    const float flSinLean = sqrtf(1.f - flCosLean * flCosLean > 0.f ? 1.f - flCosLean * flCosLean : 0.f);

    Vec3 vWishAngle = vOldAngles;
    vWishAngle.y = flVelYaw;
    pCmd->forwardmove = flMoveSpeed * flCosLean;
    pCmd->sidemove = (bTurnLeft ? -flMoveSpeed : flMoveSpeed) * flSinLean;

    CorrectMovement(pCmd, vWishAngle, vOldAngles);
    pCmd->viewangles = vOldAngles;
}

// Strafe optimizer -- perfects YOUR strafes: steers view yaw so the wish direction
// sits at the air-accel-optimal angle off velocity. Visible on purpose (real view moves).
//
// Old version had two faults, both fixed here:
//   1. BACKWARDS BROKE. It required forwardmove == 0, so S / S+A / W+A were dead, and it
//      inferred "backward" from a HARD 90 deg view-vs-velocity threshold that flipped the
//      target axis AND the sidemove sign at once -- one degree of view either side of that
//      line swapped the goal by 180 deg, which is the yank. The wish angle is now read from
//      the cmd's own move vector (same convention as AutoStrafe: wishYaw = viewYaw +
//      atan2(-sidemove, forwardmove)), so every key combo is one continuous case and there
//      is no threshold left to cross.
//   2. SNAPPY. gain% of the whole error was applied EVERY tick with no rate limit, so even a
//      small gain ate a big error in a few ticks, and roundf() to the mouse step threw away
//      every sub-step remainder instead of carrying it (drop, drop, drop, whole-step jump).
//      Now: per-tick step is capped by gain^2, and the leftover fraction is carried in
//      s_soResidual so low gain slews smoothly instead of stepping.
static void StrafeOptimizer(void* pLocal, UserCmd_t* pCmd)
{
    static float flPrevYaw = 0.f;      // post-correction yaw of last cmd -> this tick's raw mouse delta
    static float s_soResidual = 0.f;   // sub-mouse-step correction carried between ticks
    static float s_soCap = 0.f;        // per-tick step ceiling, ramps open on engage (see below)
    static bool  s_soActive = false;   // engaged last tick (residual is only valid while engaged)

    const float flYawNow = pCmd->viewangles.y;
    const float flMouseDelta = NormalizeAngle(flYawNow - flPrevYaw);   // + = user turning left
    flPrevYaw = flYawNow;

    auto Disengage = [&]() { s_soResidual = 0.f; s_soCap = 0.f; s_soActive = false; };

    if (!g_strafeOpt) { Disengage(); return; }                          // plain on/off, no bind (user pick)
    if (On(g_autoStrafe, g_autoStrafeKey, g_autoStrafeMode)) { Disengage(); return; }  // AutoStrafe wins
    if (g_offVelocity <= 0 || g_offFlags <= 0) { Disengage(); return; }

    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP || mt == MOVETYPE_OBSERVER) { Disengage(); return; }
    if (EntI(pLocal, g_offFlags) & FL_ONGROUND) { Disengage(); return; }

    const Vec3 vVelocity = EntV3(pLocal, g_offVelocity);
    const float flSpeed = vVelocity.Length2D();
    if (flSpeed <= g_strafeOptMinSpeed || flSpeed <= 1.f) { Disengage(); return; }

    // wish direction, view-relative. atan2(-side, fwd): A (side -450) -> +90 (left),
    // D -> -90, S -> 180, S+A -> +135. No move pressed = nothing to optimize.
    const float flFwd = pCmd->forwardmove, flSide = pCmd->sidemove;
    const float flWishMag = sqrtf(flFwd * flFwd + flSide * flSide);
    if (flWishMag < 1.f) { Disengage(); return; }
    const float flWishOffset = RAD2DEGF(atan2f(-flSide, flFwd));

    // m_yaw may be negative (inverted look, or flip world holding the sign): the step is its size,
    // and the mousedx written below carries the sign the engine will multiply back in.
    const float flYawStepSigned = CvarFloat("sensitivity", 1.f) * CvarFloat("m_yaw", 0.022f);
    const float flYawStep = fabsf(flYawStepSigned);
    if (flYawStep <= 0.f) { Disengage(); return; }

    // ideal angle between velocity and wish -- same physics AutoStrafe uses (airlean.h). In TF2
    // accelspeed (accel*wishspeed*tick) far exceeds the 30u add cap, so this lands on a flat
    // 90 deg (perpendicular wish = max gain); it is computed, not assumed, so odd
    // sv_airaccelerate / slowed-class cases lean in on their own. The wish magnitude is clamped
    // to m_flMaxspeed first, the way AirMove clamps it before AirAccelerate ever sees it.
    const float flMaxSpeed = g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f;
    const float flWishSpeed = (flMaxSpeed > 1.f && flMaxSpeed < flWishMag) ? flMaxSpeed : flWishMag;
    const float flIdeal = RAD2DEGF(acosf(airlean::Solve(flSpeed, flWishSpeed,
        CvarFloat("sv_airaccelerate", 10.f), TickInterval(), 30.f).cosGain));

    const float flVelYaw = RAD2DEGF(atan2f(vVelocity.y, vVelocity.x));
    const float flWishYaw = NormalizeAngle(flYawNow + flWishOffset);
    const float flWishOffVel = NormalizeAngle(flWishYaw - flVelYaw);   // + = wish left of velocity

    // the user must already be turning, and the wish must lead on the side they are turning
    // -- that is the generalized form of the old "turning left AND holding A" pair, and it is
    // what keeps this from steering a straight-line accel (wish on velocity) sideways.
    if (fabsf(flMouseDelta) < 0.001f) { Disengage(); return; }
    const float flSideSign = flMouseDelta > 0.f ? 1.f : -1.f;
    if (flSideSign * flWishOffVel <= 0.f) { Disengage(); return; }

    // error = how far the wish is from the optimal lead. The old 75 deg bound was a CLIFF: at
    // 74 deg the full gain*err landed, at 76 nothing did. Falling backwards is where that bit --
    // S held while the view swings puts the wish 40-60 deg off the lead, every tick, and the view
    // got dragged most of the way there in one tick. Window is 60 deg now, and the take fades
    // with the SQUARE of how far out the wish is, so only a wish already near the lead (<=35 deg)
    // is corrected at full gain and the edge is a fade, not a step.
    const float flErr = NormalizeAngle(flSideSign * flIdeal - flWishOffVel);
    const float flErrAbs = fabsf(flErr);
    if (flErrAbs > 60.f) { Disengage(); return; }
    const float flTaper = std::clamp((60.f - flErrAbs) / 25.f, 0.f, 1.f);

    // and fade in over the 60 u/s above the speed floor. Near the floor the 2D velocity is a
    // small remainder of a mostly-vertical fall, so its yaw (and with it the lead this chases)
    // swings tens of degrees per tick -- that is the other half of the falling-backwards spin.
    const float flSpeedFade = std::clamp((flSpeed - (std::max)(g_strafeOptMinSpeed, 1.f)) / 60.f, 0.f, 1.f);

    const float flGain = std::clamp(g_strafeOptGain / 100.f, 0.f, 1.f);
    if (flGain <= 0.f) { Disengage(); return; }

    // gain^2 rate cap: 100% keeps the old instant snap (180 deg/tick is no cap at all),
    // 50% -> 45 deg/tick, 25% -> 11 deg/tick, 10% -> 1.8 deg/tick. Squared so the low half
    // of the slider is where the smoothing actually lives.
    const float flMaxStep = 180.f * flGain * flGain;

    if (!s_soActive) { s_soResidual = 0.f; s_soCap = 0.f; }
    s_soActive = true;

    // ATTACK RAMP. The old code put the WHOLE gain*err on the FIRST tick it engaged -- that one
    // tick is the snap, and every re-engage paid it again. The ceiling now starts at zero and
    // opens by gain^2 deg per tick, so a 40 deg error at 100% gain goes 6.5 / 13 / 19.5 / 1
    // instead of 40 / 0 and is still closed inside 4 ticks. Ramping the CEILING and not the
    // value is deliberate: the step itself stays proportional to the CURRENT error, so this
    // cannot overshoot and ring the way smoothing the applied angle does (measured: an EMA in
    // this loop settles in 5 ticks but swings 4 deg past the lead on the way).
    s_soCap = (std::min)(flMaxStep, s_soCap + 0.5f + 6.f * flGain * flGain);

    float flWant = flErr * flGain * flTaper * flTaper * flSpeedFade;
    flWant = std::clamp(flWant, -s_soCap, s_soCap);
    if (flErrAbs < 0.5f) flWant = 0.f;        // deadzone: stop hunting the last half degree
    if (flWant == 0.f) return;

    // quantize to whole mouse steps, CARRY the remainder (this is the smoothness fix)
    s_soResidual += flWant;
    const float flSteps = truncf(s_soResidual / flYawStep);
    if (flSteps == 0.f) return;

    const float flApplied = flSteps * flYawStep;
    s_soResidual -= flApplied;

    pCmd->viewangles.y = NormalizeAngle(flYawNow + flApplied);
    flPrevYaw = pCmd->viewangles.y;
    // engine: yaw -= m_yaw*sens*mousedx, so mousedx = -applied / (m_yaw*sens)
    pCmd->mousedx = (short)std::clamp(flYawStepSigned < 0.f ? flSteps : -flSteps, -32767.f, 32767.f);
    EngineSetViewAngles(pCmd->viewangles);
}

// Edge jump active: own enable OR piggybacked on mini/long jump ("edgejump on
// mj/lj" -- runs whenever that jump type is active, edge jump itself may be off).
static bool EdgeJumpOn()
{
    return On(g_edgeJump, g_edgeJumpKey, g_edgeJumpMode)
        || (g_miniJumpEdge && On(g_miniJump, g_miniJumpKey, g_miniJumpMode))
        || (g_longJumpEdge && On(g_longJump, g_longJumpKey, g_longJumpMode));
}

// Edge jump -- pre: remember real ground; post (predicted state): about to leave
// ground this cmd -> press jump. Needs engine prediction.
static void EdgeJump(void* pLocal, UserCmd_t* pCmd, bool bPost)
{
    if (!EdgeJumpOn()) return;
    if (g_offGroundEntity <= 0) return;

    if (!bPost)
        s_edgeJumpGround = EntI(pLocal, g_offGroundEntity) != -1;
    else if (s_predicted && s_edgeJumpGround && EntI(pLocal, g_offGroundEntity) == -1)
    {
        pCmd->buttons |= IN_JUMP;
        // will the engine actually TAKE it? Grounded at cmd start is s_edgeJumpGround; the only
        // other refusal is the no-pogo latch (m_nOldButtons & IN_JUMP), mirrored by s_prevCmdJump.
        // Space already held -> this OR changes nothing and mini jump must not claim a takeoff
        s_edgeJumpFired = !s_prevCmdJump;
    }
}

// Long jump -- hold the bind, run off an edge: auto-jumps the walk-off + holds duck 2 ticks.
// Post-prediction: pre-pred grounded, predicted airborne -> inject jump + open duck window.
// (Aletherium CMisc::LongJump; assist choreography not ported so JT_NONE gate is always true.)
static void LongJump(void* pLocal, UserCmd_t* pCmd)
{
    static int  longjumpTick = 0;
    static bool ljbool       = false;

    if (!On(g_longJump, g_longJumpKey, g_longJumpMode) || !g_globalVars || g_offFlags <= 0)
    {
        ljbool = false; longjumpTick = 0; // reset so a mid-air toggle leaves no stuck duck
        return;
    }
    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP) return;

    const bool bGroundPre  = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool bGroundPost = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;

    if (bGroundPre && !bGroundPost)
    {
        pCmd->buttons |= IN_JUMP;
        ljbool       = true;
        longjumpTick = g_globalVars->tickcount + 2;
    }

    // adaptive cancel -- end the duck hold on the first reason the user picked,
    // instead of always waiting out the fixed 2-tick window (botox movement.cpp:1682 adaptive).
    if (ljbool && g_longJumpAdaptive)
    {
        const bool bKeyUp = !MovementKeyOK(g_longJumpKey, g_longJumpMode);
        const bool bTimeUp = g_globalVars->tickcount - (longjumpTick - 2)
                           >= (int)std::round(g_longJumpAdaptiveTime / TickInterval());
        if (LjAdaptiveShouldCancel(g_longJumpAdaptiveLetGo, g_longJumpAdaptiveLanded, g_longJumpAdaptiveTimed,
                                    bKeyUp, bGroundPost, bTimeUp))
            ljbool = false;
    }

    if (ljbool)
    {
        if (g_globalVars->tickcount < longjumpTick)
            pCmd->buttons |= IN_DUCK;
        else
            ljbool = false;
    }
}

// ==== MINI JUMP (the -20 hop restored; an earlier build's +20 WAS a long jump) ===============
// ported the reference dll 1:1: duck on the grounded takeoff cmd AND on the first
// AIRBORNE cmd. That second press is the whole fault. On cmd B the ground transition is still
// open (m_bDucking=1), so Duck() takes OnDuck's bInAir arm -> FinishDuck -> origin +20, and the
// release two ticks later hands the 20 straight back. +20 then -20 = a CROUCH JUMP: full apex,
// feet high for three ticks. That is also, button for button, what LongJump() does (inject
// IN_JUMP, hold IN_DUCK 2 ticks) -- so mj and lj came out as the same jump. Twice reported in
// the same words: "performs a longjump instead of a minijump" .
//
// THE MINI JUMP IS THE UNPAID -20. One press on the ground, never on cmd B:
//   cmd A  grounded, the engine TOOK a jump -> press IN_DUCK. PlayerMove runs Duck() BEFORE
//          FullWalkMove, so on the ground this only STARTS the transition (m_bDucking=1, no
//          FL_DUCKING, nothing moves) and CheckJumpButton still jumps -- down the ducking arm,
//          which SETS vz = 289 (tf_gamemovement.cpp:1330).
//   cmd B  first AIRBORNE cmd -- DO NOT PRESS. A press here is the +20 above. Released, the
//          engine's OnUnDuck finds CanUnduck refused (~4u of clearance) and takes its "still
//          under something" branch: m_bDucked + FL_DUCKING stamped for FREE, no origin change.
//   cmd C+ FL_DUCKING set, m_bDucking=0 -> PRESS AND HOLD. OnDuck skips both branches (the press
//          is not an edge, the transition is closed), so it does nothing to our state -- it only
//          keeps Duck() out of OnUnDuck, which stops the engine retrying CanUnduck and taking
//          the drop on a tick of its own choosing (19u one hop, 22u the next -- sub-unit
//          geometry, an earlier build's coin flip: 6 of 11 hops died inside the floor on it).
//   release once a down-trace says the drop clears -> OnUnDuck -> FinishUnDuck: the feet fall
//          20u out of the arc with room underneath. Apex ~46 -> ~26.
// The -20 is a fixed teleport, so taking it anywhere before apex costs the same 20u -- the
// release tick buys safety, not height. The +20 is never paid, so this can never be a crouch
// jump, and lj stays lj.
//
// TWO ENGINE REFUSALS THE HOP CAN STILL LOSE TO, logged by name rather than guessed at:
//   FL_DUCKING at cmd start -- CheckJumpButton refuses the jump outright (non-scout).
//   m_flDuckTimer -- CTFGameMovement::DuckOverrides STRIPS a GROUNDED IN_DUCK for
//     TF_TIME_TO_DUCK = 0.3s (20 ticks) after ANY duck release. A pure mj chain never strips
//     itself (our release lands mid-air, well before the next takeoff), but "hold duck", a
//     jumpbug, a pixel-surf ride duck or the player's own crouch all poison the next hop.
//     Nothing client-side beats it -- tf_clamp_airducks is FCVAR_REPLICATED.
static constexpr float k_mjLaunchVz = 200.f;   // a real TF2 launch reads ~277 post-pred

static bool  s_miniHit    = false;   // takeoff duck went out this tick (indicator flash)
static bool  s_mjHoldDuck = false;   // holding the engine off until the -20 drop is SAFE

static constexpr float k_mjRiseClear = 26.f;   // rising: release only with 6u to spare
static constexpr float k_mjFallClear = 21.f;   // falling: 20u is all the engine itself asks
// CategorizePosition's own down-trace. Feet inside this band of a floor = FL_ONGROUND and vz
// zeroed, whichever way the player was moving -- that is what killed the hops (the -20
// left the feet 0.1u up and the engine grounded a player still rising at 229) and it is also
// the honest test for "this cmd really can touch down".
static constexpr float k_mjLandSlack    = 2.f;

// free space straight down under the hull, capped at flMax (flMax = nothing in the way)
static float MJ_ClearBelow(void* pLocal, const Vec3& vOrg, float flMax)
{
    if (g_offMins <= 0 || g_offMaxs <= 0 || !g_engineTrace) return 0.f;
    TraceFilterWorldProps filter; filter.Setup(pLocal);
    GameTrace_t tr{};
    TraceHull(vOrg, vOrg - Vec3(0, 0, flMax), EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs),
              MASK_PLAYERSOLID, &filter, &tr);
    return flMax * tr.fraction;
}

static void MiniJump(void* pLocal, UserCmd_t* pCmd)
{
    s_miniHit = false;
    if (!On(g_miniJump, g_miniJumpKey, g_miniJumpMode) || !g_globalVars || g_offFlags <= 0)
    {
        s_miniShouldDuck = false;
        s_mjHoldDuck     = false;
        return;
    }

    const int  iFlags      = EntI(pLocal, g_offFlags);
    // the state the cmd STARTS on -- the engine runs Duck() then CheckJumpButton off exactly
    // this, and Pred_Start has already moved the live one on 
    const bool bGroundPre  = (s_prePredFlags & FL_ONGROUND) != 0;
    const bool bGroundPost = (iFlags & FL_ONGROUND) != 0;
    const Vec3 vVel        = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity) : Vec3();
    const Vec3 vOrg        = g_offOrigin   > 0 ? EntV3(pLocal, g_offOrigin)   : Vec3();

    // Touched ground again -> drop everything we were holding.
    if (bGroundPre && bGroundPost)
    {
        s_miniShouldDuck = false;
        s_mjHoldDuck     = false;
    }

    // ---- hold the engine off the unduck retry -- and NEVER press on cmd B -------------------
    // THE GATE WAS READING THE OUTCOME IT EXISTS TO PREVENT. An earlier build opened this
    // block on the POST-prediction ground flag. Pred_Start runs at the top of the post-pred
    // section, BEFORE MiniJump, so the cmd is always simulated with our IN_DUCK still absent. On
    // the one tick where clearance crosses the engine's own 20u CanUnduck bar, that duck-free sim
    // takes FinishUnDuck, drops the feet 20u onto the floor and comes back FL_ONGROUND -- so the
    // gate answered "we already landed" and skipped the press that would have prevented it.
    // Ground is asked of the state the cmd STARTS on; a post-pred landing is believed only when
    // the floor is actually reachable this cmd (k_mjLandSlack); velocity comes from the pre-pred
    // snapshot too (the predicted vz reads 0.0 on the artifact cmd and would call a player rising
    // at 229 a faller, then use the wrong clearance bar).
    if (s_mjHoldDuck && !bGroundPre)
    {
        const bool  bFlDuck = (s_prePredFlags & FL_DUCKING) != 0;
        const float flVz    = s_prePredVel.z;
        const float flFall  = flVz < 0.f ? -flVz * TickInterval() : 0.f;
        const float flRaw   = MJ_ClearBelow(pLocal, s_prePredOrigin, 48.f);
        // the release edge is seen on the NEXT cmd, so test the clearance there: rising gains
        // a tick, falling loses one
        const float flClear = flRaw - flFall;
        const bool  bRoom   = flClear >= (flVz > 0.f ? k_mjRiseClear : k_mjFallClear);
        // the floor is further off than this tick's fall can carry us: no button set lands us
        // here, so a predicted FL_ONGROUND is the -20 teleport and nothing else
        const bool  bLanding = flRaw <= flFall + k_mjLandSlack;

        if (!bFlDuck && !s_prePredDucking)
            s_mjHoldDuck = false;          // engine already took the -20 (or never stamped)
        else if (s_prePredDucking)
        {
            // cmd B: the GROUND transition is still open. A press here goes to OnDuck's bInAir
            // arm -> FinishDuck -> +20 -> a crouch jump, which is the bug in one line.
            // Say nothing this cmd and let OnUnDuck stamp FL_DUCKING for free.
        }
        else if (bLanding)
        {
            // genuinely about to touch down. Let go: a ducked landing stalls the next jump ~13
            // ticks (DuckOverrides), and there is no height left to save this close to the floor
            s_mjHoldDuck = false;
        }
        else if (!bRoom)
            pCmd->buttons |= IN_DUCK;      // hold the engine off until the drop is safe
        else
        {
            s_mjHoldDuck = false;          // release: FinishUnDuck with room under the hull
        }
    }

    // ---- optional hold duck: pre-landing unduck ---------------------------------------------
    // Holding the duck into the air is NOT a mini jump -- the transition finishes airborne
    // (FinishDuck, feet +20) and the release pays the -20 back, so the arc is a crouch jump.
    // Kept as the option it always was; the trace stands the player up ~5 ticks out so the
    // landing is not ducked (a ducked landing stalls the next jump ~13 ticks).
    if (s_miniShouldDuck && !bGroundPost && g_offVelocity > 0 && g_offOrigin > 0
        && g_offMins > 0 && g_offMaxs > 0)
    {
        if (vVel.z < 0.f)
        {
            const float flLook = -vVel.z * TickInterval() * 5.f + 16.f;
            TraceFilterWorldProps filter; filter.Setup(pLocal);
            GameTrace_t tr{};
            TraceHull(vOrg, vOrg - Vec3(0, 0, flLook), EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs),
                      MASK_PLAYERSOLID, &filter, &tr);
            if (tr.fraction < 1.f)
                s_miniShouldDuck = false; // ground ~5 ticks away -- stand up now
        }
    }

    // ---- takeoff: the engine TOOK a jump on this cmd ----------------------------------------
    // vz is the test: a refused press (the no-pogo latch), a walk-off and a slope-drop all read
    // ~0 here, and every one of them used to start a duck that the next tick's 20u teleport
    // turned into a landing -- the "jumps twice off the ground" report.
    // EDGE JUMP IS INVISIBLE TO vz. EdgeJump writes IN_JUMP into a cmd Pred_Start has
    // ALREADY simulated without it, so the predicted vz is the bare walk-off (~-6), never the 271
    // launch, and every mj edge jump came out full height. The engine still takes that jump (it
    // runs the cmd from the grounded start: CategorizePosition -> Duck() -> CheckJumpButton all
    // fire before WalkMove crosses the lip), so trust EdgeJump's own press instead of the sim:
    // s_edgeJumpFired is set only inside that press and only when the no-pogo latch was open.
    if (bGroundPre && !bGroundPost && (vVel.z > k_mjLaunchVz || s_edgeJumpFired))
    {
        const bool  bDuckFlag = (iFlags & FL_DUCKING) != 0;
        const float flTimer   = g_offDuckTimer > 0 ? EntF(pLocal, g_offDuckTimer) : 0.f;
        const bool  bStripped = flTimer > g_globalVars->curtime;   // DuckOverrides eats it

        if (!bDuckFlag && !bStripped)
        {
            pCmd->buttons |= IN_DUCK;
            s_miniHit   = true;
            // legacy "hold duck" keeps its own pre-landing release; otherwise the duck is held
            // only as far as the safe drop above
            if (g_miniJumpHoldDuck) s_miniShouldDuck = true;
            else                    s_mjHoldDuck = true;
        }
        else
        {
            // THE TAKEOFF WENT OUT AS A PLAIN JUMP. Nothing can be done about it here --
            // tf_clamp_airducks is FCVAR_REPLICATED and DuckOverrides (tf_gamemovement.cpp:3194)
            // strips IN_DUCK off every grounded cmd until m_flDuckTimer expires -- but it is the
            // whole "I pressed minijump and just jumped" report, so it is said out loud once per
            // takeoff. 0.3s after ANY duck release, which is why it bites right after climbing
            // onto something with a crouch jump. The route calculator prints the same number.
            DebugLog("MJ: no mini this takeoff -- %s (duck timer %.0f ms left)",
                     bDuckFlag ? "already FL_DUCKING" : "duck timer live, DuckOverrides strips IN_DUCK",
                     bStripped ? (flTimer - g_globalVars->curtime) * 1000.f : 0.f);
        }
    }
    // left the ground WITHOUT a launch -- the old code ducked here and that is the whole
    // double-jump bug. Nothing is done about it on purpose.

    if (s_miniShouldDuck)
        pCmd->buttons |= IN_DUCK;
}

// RIDE ENDS, LAND, MJ = A PLAIN JUMP. The surf duck lets go on the landing tick (PSU ground drop),
// that release arms m_flDuckTimer (OnUnDuck, +0.3s) and DuckOverrides strips the grounded takeoff
// duck until it runs out -- same for any duck < 0.3s before a mj takeoff. Nothing beats the timer,
// so the jump waits it out (<= 0.3s) and the takeoff comes out mini. Pre-prediction: the cmd is
// simulated as sent. While FL_DUCKING CheckJumpButton refuses the jump anyway, so that part of the
// wait is free, and the strip is the release a held space needs to fire again.
// After DelayHop, before AutoGoomba (its press is the last word). EdgeJump's post-pred press can't
// wait (the lip is now), so it still goes out plain and MiniJump logs it
static void MiniJumpWaitTimer(void* pLocal, UserCmd_t* pCmd)
{
    if (!(pCmd->buttons & IN_JUMP) || !On(g_miniJump, g_miniJumpKey, g_miniJumpMode)
        || g_offFlags <= 0 || g_offDuckTimer <= 0 || g_offTickBase <= 0)
        return;
    if (!(EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
        return;
    // the curtime this cmd runs at; one tick of margin so a tickbase off-by-one waits, never strips
    const float flNow = EntI(pLocal, g_offTickBase) * TickInterval();
    if (EntF(pLocal, g_offDuckTimer) > flNow - TickInterval())
        pCmd->buttons &= ~IN_JUMP;
}

// Fast stop / fast accelerate (Aletherium FastMovement; duck speed + kart +
// doubletap/antiaim gates dropped).
static void FastMovement(void* pLocal, UserCmd_t* pCmd)
{
    if (!EntGround(pLocal)) return;
    if (g_offVelocity <= 0 || g_offMaxspeed <= 0) return;

    const float flSpeed = EntV3(pLocal, g_offVelocity).Length2D();
    float maxSpd = EntF(pLocal, g_offMaxspeed) * 0.9f;
    if (maxSpd > 520.f) maxSpd = 520.f;
    const int flMaxSpeed = (int)maxSpd - 10;
    const int iRun = (!pCmd->forwardmove && !pCmd->sidemove) ? 0 : (flSpeed < flMaxSpeed ? 1 : 2);

    switch (iRun)
    {
    case 0:
    {
        if (!g_fastStop || !flSpeed) return;
        // bhop landing tick = grounded w/ no move keys; braking here eats hop speed
        if (s_rawJumpHeld || (pCmd->buttons & IN_JUMP)) return;

        Vec3 vDirection = VectorAngles(EntV3(pLocal, g_offVelocity));
        vDirection.y = pCmd->viewangles.y - vDirection.y;
        Vec3 vFwd; AngleVectors(vDirection, &vFwd);
        Vec3 vNegatedDirection = vFwd * -flSpeed;
        pCmd->forwardmove = vNegatedDirection.x;
        pCmd->sidemove = vNegatedDirection.y;
        break;
    }
    case 1:
    {
        if (EntDucking(pLocal)) return; // duck speed removed
        if (!g_fastAccel) return;
        if (pCmd->buttons & IN_ATTACK) return;
        if (g_globalVars && (g_globalVars->tickcount % 2)) return;
        if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT))) return;

        Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove, 0.f };
        Vec3 vAngMoveReverse = VectorAngles(vMove * -1.f);
        pCmd->forwardmove = -vMove.Length();
        pCmd->sidemove = 0.f;
        // silent: only the sent cmd changes (GetUserCmd/ValidateUserCmd hooks keep it)
        pCmd->viewangles.y = fmodf(pCmd->viewangles.y - vAngMoveReverse.y, 360.f);
        pCmd->viewangles.z = 270.f;
        break;
    }
    }
}

// ============================================================================
// wave 2 features (Aletherium ports): edge bug, pixel surf, auto align,
// texture bug, head surf, wall climb, air stuck.
// Skipped extras (first pass): edge bug sound/chat/visualize/mouse lock,
// texture bug choke ticks (needs sendpacket ptr), pixel surf assist family.
// ============================================================================

static bool s_textureBugHit = false; // TB catch owns pCmd this tick (HeadSurf/WallClimb/AirStuck yield)
static bool s_tbAlignDead = false;   // TBC_Align this cmd: START pinned, no press 0..100 / steer / gain pins the next
                                     // tick = the ride ends here (surf stick skips its latch: 15:29 log, 0 of 122 held)
static bool s_tbOwnsCmd = false;     // TB steered pCmd this tick WITHOUT a hit (creep/drive/probe = band
                                     // setup) -- AirStuck must not stomp it with its coarse sweep move
// TB ride coyote ticks left (texturebug.inl s_ride_grace), as of the END of the last TB call.
// > 0 = TB is mid-ride, including the free-fall ticks between two pins. PixelSurf (runs before
// TB) and WallClimb yield on it, coldtires rule: a tb ride owns the wall
static int  s_tbRideHold = 0;
static bool s_headSurfHit = false;
static bool s_headBounceHit = false; // HB clipped a crease this tick (one-tick catch, no hold)
static bool s_wallClimbHit = false;    // botox g_wall_climb.caught(): wc's catch (land / hold / jump fix) is this cmd
static bool s_wallClimbActive = false; // botox g_wall_climb.active(): wc ran an air wall tick this cmd (pixel surf yields)
static bool s_wcLanded = false;        // botox g_wall_climb.m_hit: a land went out (indicator + detection)
static bool s_wcSyncWant = false;      // wc key held: AS_Sync keeps cl_pred_optimize armed for it too
static bool s_tbSyncWant = false;      // botox-style tb armed: same (botox sync_arm owner k_sync_tb)
// cmd right before TextureBug. botox runs wc BEFORE tb and tb keeps only a real catch on a wc tick
// (texturebug.cpp:159); here tb runs first, so wc starts its air wall ticks from this instead
static float s_wcPreTbFwd = 0.f, s_wcPreTbSide = 0.f;
static int   s_wcPreTbButtons = 0;
static bool s_airStuckHit = false;
static bool s_pixelHopHit = false;   // pixeljump's pixel hop wrote this cmd (land / hop): the air movers after it yield
static bool s_pixelHopOn  = false;   // PH key active this cmd: pixel surf stands down (webz 18000c0b0 skips wall stuck)
static bool s_phSyncWant  = false;   // ph key held: a pin landing is exact-float, START must be the server END too
// s_shouldPixelSurf / s_psRidingNow / s_psAssistHopActive moved ABOVE AutoJumpbug --
// jumpbug is a PRE-prediction feature and has to read the ride state too
// indicator snapshot, written once per cmd at the end of Movement_RunImpl
static MvInd_t s_mvInd{};

// ---- Edge bug: the search, the replay and the overlay snapshot ----
#include "edgebug.inl"

// ---- Pixel surf (maintain the surf by ducking; self-contained duck/stand prediction) ----
// write a WORLD-space 2D wish into the cmd's view-relative forward/side pair. Every
// feature in this file did this inline (AutoAlign 4x, the PixelSurf hold, TB); the guide and the
// speed brake need it twice more, and getting the sin sign wrong steers into the opposite wall
static void MV_WriteWishWorld(UserCmd_t* pCmd, const Vec3& vDir, float flMag)
{
    Vec3 vFlat(vDir.x, vDir.y, 0.f);
    const float flLen = vFlat.Length2D();
    if (flLen < 0.0001f || flMag <= 0.f)
        { pCmd->forwardmove = 0.f; pCmd->sidemove = 0.f; return; }
    Vec3 vAngle = VectorAngles(vFlat);
    ClampAnglesMv(vAngle);
    const float flRot = DEG2RADF(vAngle.y - pCmd->viewangles.y);
    pCmd->forwardmove = cosf(flRot) * flMag;
    pCmd->sidemove = -sinf(flRot) * flMag;
}

// and the same for the prop test, which lives in fakemove.inl (#included lower still):
// entity whose model is a studio model = a prop / a building, not a brush
static bool FM_StudioEnt(void* ent);

// ---- A PROP IS NOT A SURF ------------------------------------------------------------
// Every part of pixel surf -- the hunt's wall gate, the guide's steer, the park's brake, the
// stick's 10u press -- took ANY upright non-displacement face the trace returned, and a prop
// answers that description: crates, barrels, fences, signs and dispensers all trace as flat
// steep faces. They are not brush planes though. A static prop's collision is a convex hull
// from its .phy, at whatever angle the mapper dropped it at, so the into-wall press meets it
// at a hull CORNER instead of flush -- the corner digs past the face, the hull wedges, and the
// press keeps feeding it: "auto pixel surf gets stuck on props". There is no sub-unit lip to
// ride on one either, so nothing is lost by refusing them outright.
// Two shapes to refuse (sdk gametrace.h:71 -- "If hEnt points at the world entity, then this is
// the static prop index"): a static prop reports the WORLD as m_pEnt with the prop index in
// hitbox, and a dynamic prop / building reports itself with a studio model. Brush entities
// (func_brush, doors, force fields) are NOT refused: those are real brush planes, and the trace
// filter already drops the ones the engine lets us through
static bool MV_TraceIsProp(const GameTrace_t& tr)
{
    void* pWorld = GetClientEntity(0);
    if (tr.hitbox > 0 && (!tr.m_pEnt || tr.m_pEnt == pWorld)) return true;   // static prop
    // world hit, or no world to compare against: a brush face, and NOT a question for
    // FM_StudioEnt -- worldspawn's model name is "maps/<name>.bsp", which passes its "no
    // leading *" test and would report the whole map as a prop
    if (!pWorld || !tr.m_pEnt || tr.m_pEnt == pWorld) return false;
    return FM_StudioEnt(tr.m_pEnt);                                          // prop / building
}

// ---- AND THE PIN ITSELF HAS TO REST ON A BRUSH ---------------------------------------
// MV_TraceIsProp gates the faces pixel surf goes LOOKING for, and the log says that gate
// fires constantly -- prop=9887 refusals. The same log has latch=116 next to it, because NO PIN
// IS EVER ASKED WHAT CLIPPED IT. The hunt's gate only proves a brush wall stands somewhere within
// 10u of the LIVE hull; the pin test downstream is velocity-only, and a crate standing against
// that wall (every TF2 map) clips vz to -halfgrav exactly the same way. So the wall passes the
// gate and the prop takes the ride: "auto ps still counts some props as pixelsurfs".
// At the pinned tick the hull is flush against whatever held it, so one fan AT THAT ORIGIN
// answers it -- nearest brush face vs nearest prop face, and a prop closer than any brush IS the
// thing we are resting on. Ties pass (both in contact = a real seam with a crate beside it).
// Displacements are neither: real geometry, they do hold rides, so they are skipped and this can
// only ever refuse less. Horizontal fan on purpose -- floors are the ONGROUND break's job
static bool MV_PinOnProp(const Vec3& origin, const Vec3& mins, const Vec3& maxs,
                         TraceFilterWorldProps& filter)
{
    constexpr float flRange = 6.f;   // contact only: a pinned hull rests ~0 off its own face
    constexpr int   kRays  = 16;   // the ring is a COUNT, not a float accumulator (see below)
    constexpr float flStep = (MV_PI * 2.f) / float(kRays);
    float flBrush = flRange * 2.f, flProp = flRange * 2.f;
    for (int i = 0; i < kRays; i++)
    {
        const float a = flStep * float(i);
        GameTrace_t tr{};
        TraceHull(origin, origin + Vec3(cosf(a) * flRange, sinf(a) * flRange, 0.f),
                  mins, maxs, MASK_PLAYERSOLID, &filter, &tr);
        if (tr.fraction >= 1.f || (tr.surface.flags & SURF_TRIGGER_MV)) continue;
        if (tr.dispFlags & DISPSURF_SURFACE) continue;
        float& flBest = MV_TraceIsProp(tr) ? flProp : flBrush;
        flBest = std::min(flBest, tr.fraction * flRange);
    }
    return flProp < flBrush;
}

// ---- Pixel surf: botox pixel_surf( ) 1:1 (pixelsurf.inl) ----
static bool AutoBounceOwnsCmd();   // autobounce.inl (included later): botox m_auto_bounce_data.m_owns_cmd
#include "pixelsurf.inl"

// The player hull is an AABB, so only a face square to +-X or +-Y meets it FLAT. On anything
// else -- a rotated brush, a displacement wall, a bevel -- contact is a single hull CORNER, and
// auto align presses straight in against it: the corner digs past the face and the hull wedges
// = stuck. 0.999 ~= 2.6 deg off square, which is float noise on a real axis-aligned brush and
// nothing else. Ramps are already out via the z term (a wall normal has no z at all)
static constexpr float k_mvAxisAlignDot = 0.999f;
static bool MV_IsHullFlatWall(const Vec3& n)
{
    if (fabsf(n.z) > 0.001f) return false;
    return fabsf(n.x) >= k_mvAxisAlignDot || fabsf(n.y) >= k_mvAxisAlignDot;
}

// ---- DRIFT HOLD: keep a pin on sv_wallbug 0 by NOT touching the face -------------------------
// Valve Oct 2026 ClipVelocity: backoff = min(backoff,0) - DIST_EPSILON, so every clip leaves the hull
// moving 0.03125 u/s OFF the plane it hit. 11-08 log, every pixel surf that fell: pin vz -5.969 (the
// top-plane clip, same epsilon, self-sustaining), and the origin across the wall creeping off it
// 0.00047u a tick = 0.03125 * dt until ~0.005u later the lip is gone. A press cannot fix it: the hull
// is inside the trace's epsilon shell, so pressing in only re-clips (fraction 0) and puts the
// +0.03125 back -- press 10 and press 450 drifted at the same slope. The one ride that held 235
// ticks had zero across-face velocity. Same story for an air stuck: a flat face can never nail the
// hull any more, only a lip / seam pin can hold it, and only while the hull stops drifting.
// So: solve the wish that leaves the across-face velocity at k_awayTarget (a hair AWAY, never toward:
// toward = a clip = the drift again), and with stopAlong also the along-face velocity at 0 (a hang).
// SDK AirAccelerate: dv = airaccel * wish * frametime * friction along wishdir (gamemovement.cpp:1734),
// the 30 cap never bites (addspeed = 30 - v.wishdir, and wishdir points against v). The gain is
// MEASURED (a bare tick + a half step), so duck crop / friction / clip offsets are the engine's.
// Proof = closed loop over `horizon` ticks: every simulated tick re-solves from its own velocity.
// cmd: its fwd/side are scratch (left on the first tick's solve); buttons / view are flown as given
// anchor (optional): steer the ORIGIN back onto a spot that pinned for real instead of holding a
// hair away. SDK brush clip skips a brush whose side plane the hull is in front of and not closing
// on (d1 > 0 && d2 >= d1), so a pure "away" target can cost the seam brush's top plane = the pin.
// Per tick: across target = (anchor - origin).n / dt, capped at one clip's DIST_EPSILON. The caller
// flies both and keeps the longer hold: the sim decides, not this comment
// creep (optional, wins over anchor): close HALF the gap to the face's trace shell every tick.
// Same SDK rule, other half: a lip brush sticking out under the face is only entered while the
// hull closes on it (d2 < d1), and the face above must not be reached first (its clip = the
// +DIST_EPSILON push = hull moving away = lip skipped). Half the gap never reaches the shell and
// always closes. Gap under 2 ulps of origin = no room to close: take the one clip (gap + 1 ulp)
// and let its push hand the room back
struct MvDrift
{
    int   held = 0;              // ticks the closed loop kept the pin
    float fwd = 0.f, side = 0.f; // the first tick's move, exact bits
    Vec3  wish{};                // ...as a world wish (dir * magnitude)
    float k = 0.f;               // measured gain, u/s per unit wish
    float vaNone = 0.f;          // across-face velocity a bare tick ends with
    float doff = 0.f;            // origin moved along n over the loop (+ = off the face)
};
static MvDrift MV_DriftHold(void* pLocal, UserCmd_t* cmd, int frame, const Vec3& n, bool stopAlong,
                            int horizon, int& simsUsed, const Vec3* anchor = nullptr, bool creep = false)
{
    MvDrift r;
    constexpr float k_awayTarget = 0.0002f;   // u/s left drifting off: 0.000003u a tick
    const float pin = TargetPredictZVel();
    // slide (!stopAlong): xy < 1 = TryPlayerMove zeroed the move (air stuck), vz still reads as the pin.
    // a hang (stopAlong) is xy ~0 by design
    const auto on_pin = [&]() {
        const Vec3 v = EntV3(pLocal, g_offVelocity);
        return !(EntI(pLocal, g_offFlags) & FL_ONGROUND)
            && fabsf(v.z - pin) < as::k_pinTol && (stopAlong || v.Length2D() >= 1.f);
    };
    const auto velXY = [&]() { const Vec3 v = EntV3(pLocal, g_offVelocity); return Vec3(v.x, v.y, 0.f); };
    // across-face velocity this tick should end with (reads the entity's origin as it stands)
    const float dt = TickInterval();
    const auto target = [&]() -> float {
        if (creep) {
            const Vec3 o = EntV3(pLocal, g_offOrigin);
            TraceFilterWorldProps fil; fil.Setup(pLocal);
            GameTrace_t tr{};
            TraceHull(o, o - n * 2.f, EntV3(pLocal, g_offMins), EntV3(pLocal, g_offMaxs), MASK_PLAYERSOLID, &fil, &tr);
            const float gap = tr.startsolid ? 0.f : tr.fraction * 2.f;
            const float ulp = std::max(nextafterf(fabsf(o.x), INFINITY) - fabsf(o.x),
                                       nextafterf(fabsf(o.y), INFINITY) - fabsf(o.y));
            return gap > 2.f * ulp ? -0.5f * gap / dt : -(gap + ulp) / dt;
        }
        if (!anchor) return k_awayTarget;
        const Vec3 o = EntV3(pLocal, g_offOrigin);
        const float e = (anchor->x - o.x) * n.x + (anchor->y - o.y) * n.y;
        return std::clamp(e / dt, -MV_SURF_EPS, MV_SURF_EPS);
    };
    // the change a tick that would end at vNone still needs
    const auto want = [&](const Vec3& vNone) -> Vec3 {
        const float across = vNone.x * n.x + vNone.y * n.y;
        const float tgt = target();
        if (!stopAlong) return n * (tgt - across);
        return n * tgt - vNone;
    };
    const auto oneTick = [&](const Vec3& dir, float m) -> Vec3 {
        MV_WriteWishWorld(cmd, dir, m);
        RestoreToFrame(frame);
        Pred_Simulate(pLocal, cmd);
        ++simsUsed;
        const Vec3 v = velXY();
        RestoreToFrame(frame);
        return v;
    };
    if (SimsLeft() < horizon + 2) return r;

    RestoreToFrame(frame);
    const Vec3 v0    = velXY();
    const Vec3 vNone = oneTick(Vec3(), 0.f);
    const Vec3 offs  = vNone - v0;   // what a bare tick does on its own (clip epsilons)
    float k = CvarFloat("sv_airaccelerate", 10.f) * TickInterval();
    {
        const Vec3  d  = want(vNone);
        const float dl = d.Length2D();
        if (dl > 2.f * k_awayTarget)
        {
            // half the change: still short of the target, so no clip bends the measurement
            const Vec3  u     = d * (1.f / dl);
            const float mHalf = std::min(450.f, 0.5f * dl / k);
            const float got   = (oneTick(u, mHalf) - vNone).Dot(u);
            if (got > 0.2f * k * mHalf && got < 5.f * k * mHalf)
                k = got / mHalf;
        }
    }
    r.k = k;
    r.vaNone = vNone.x * n.x + vNone.y * n.y;

    RestoreToFrame(frame);
    const int  base = EntI(pLocal, g_offTickBase);
    const Vec3 o0   = EntV3(pLocal, g_offOrigin);
    for (int z = 0; z < horizon && SimsLeft() > 0; z++)
    {
        const Vec3  d = want(velXY() + offs);
        const float l = d.Length2D();
        const Vec3  u = l > 1e-7f ? d * (1.f / l) : Vec3();
        const float m = l > 1e-7f ? std::min(450.f, l / k) : 0.f;
        MV_WriteWishWorld(cmd, u, m);
        if (z == 0) { r.fwd = cmd->forwardmove; r.side = cmd->sidemove; r.wish = u * m; }
        EntSetI(pLocal, g_offTickBase, base + z);
        Pred_Simulate(pLocal, cmd);
        ++simsUsed;
        if (!on_pin()) break;
        r.held++;
    }
    const Vec3 o1 = EntV3(pLocal, g_offOrigin);
    r.doff = (o1.x - o0.x) * n.x + (o1.y - o0.y) * n.y;
    RestoreToFrame(frame);
    cmd->forwardmove = r.fwd;
    cmd->sidemove    = r.side;
    return r;
}

// ---- SURF STICK: a pin seen once is held like a TB ride ----------------------------------
// The flat-wall scan in AutoAlign re-earns its wall every tick (axis-aligned only, nearest face,
// 1u reach) and proves its press ONE tick ahead. On a live ride that is the wrong question: the
// pin already proved this face holds us. And since Valve's Oct 2026 ClipVelocity (backoff -=
// DIST_EPSILON) a hull that stops pressing drifts off the face and the ride is gone next tick.
// So: last tick ended pinned (vz = -halfgrav) against an upright brush face = latch that face,
// and every tick after press into it like TBC_Align does (least press first, but never zero
// before the rest). The player's along-wall input rides on top of the press. Lets go on ground /
// keys off the wall / face gone / the first tick no move pins (player's move handed back). PixelSurf runs
// after and keeps a cmd that pins.
// THE PROOF IS LONG, NOT 2 TICKS. 11-08 log: a 2-tick proof passed 3 ticks running (press 30 ->
// 50) and the ride still fell on the 4th -- greedy "pins the next 2" picks presses that walk the
// hull into a state no press can hold. Each candidate now flies k_stickHorizon ticks and the one
// that holds LONGEST wins (ties: harder press, then list order = steer kept); a full hold exits early,
// so a healthy ride costs one candidate.
// STANCE IS NOT OURS. The crouch is PixelSurf's (its flip hunt picks the tick to duck / stand, which
// a held-from-now flip cannot: 11-08 log, a "flip now" pass won 0 ticks out of every ride). The
// sims fly whatever buttons the cmd carries -- PixelSurf's latched duck is already on it -- and
// PixelSurf runs after us and flips when our press stops holding
static bool  s_aaStick      = false;
static Vec3  s_aaStickN{};           // latched face normal, xy only
static float s_aaStickPress = 10.f;  // last press that pinned: the no-proof fallback reuses it
static bool  s_aaStickPredPin = false;   // last cmd's pick said the next tick pins (div diag)
static int   s_aaStickRideTick = 0;      // ticks since latch, for the per-tick trace
static Vec3  s_aaStickAnchor{};          // origin of the latch tick (a real pin): drift hold 'a' steers back to it
// diag, per second: full = held the whole horizon, part = held some ticks, div = picked a move
// the sim said pins and the real tick did not (sim lie, not a dead end)
static int   s_aaStickTicks = 0, s_aaStickFull = 0, s_aaStickPart = 0, s_aaStickMissTot = 0;
static int   s_aaStickDiv = 0, s_aaStickBestSum = 0;

static void AutoAlignStickDrop(const char* why)
{
    if (s_aaStick)
        DebugLog("AA: stick release %s z=%.3f vz=%.2f xy=%.1f", why, s_prePredOrigin.z, s_prePredVel.z,
                 s_prePredVel.Length2D());
    s_aaStick = false;
    s_aaStickPredPin = false;
}

static bool TbBindOn()
{
    return On(g_textureBug, g_textureBugKey, g_textureBugMode);
}

// true = the stick owns this cmd (AutoAlign's scan path is skipped)
static bool AutoAlignStick(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_aaSurfStick)
        { AutoAlignStickDrop("off"); return false; }
    if ((s_prePredFlags & FL_ONGROUND) || !EntAlive(pLocal) || EntMoveType(pLocal) != MOVETYPE_WALK)
        { AutoAlignStickDrop("ground"); return false; }
    // NOT OUR RIDE. A texture bug crease, a head surf, an air stuck, a wall climb catch all read
    // as "pinned against a wall" too, and those features run AFTER us and read the cmd we leave as
    // the player's keys -- a stick that zeroes the move there breaks them. Their flags still
    // hold last cmd's answer here, which is the right question: whose ride is this
    if (s_textureBugHit || s_tbOwnsCmd || s_headSurfHit || s_headBounceHit || s_airStuckHit
        || s_wallClimbHit || s_wallClimbActive || AutoBounceOwnsCmd() || s_psAssistHopActive)
        { AutoAlignStickDrop("other"); return false; }

    const float pin = TargetPredictZVel();
    // as::k_pinTol: Valve's DIST_EPSILON clip ends a top-plane pin at -halfgrav + 0.03125.
    // xy >= 1 (as::SurfPinned): a zeroed move (air stuck) ends on the same vz
    const auto on_pin = [pin](const Vec3& v) { return as::SurfPinned(v.z, v.Length2D(), pin); };
    const bool pinned_now = on_pin(s_prePredVel);
    if (s_aaStick && s_aaStickPredPin && !pinned_now)
        s_aaStickDiv++;
    s_aaStickPredPin = false;

    const int frame = PredCommandsPredicted() - 1;
    RestoreToFrame(frame);
    const Vec3 org  = EntV3(pLocal, g_offOrigin);
    const Vec3 mins = EntV3(pLocal, g_offMins);
    const Vec3 maxs = EntV3(pLocal, g_offMaxs);
    TraceFilterWorldProps fil; fil.Setup(pLocal);

    if (!s_aaStick)
    {
        // "even for just a tick": one pinned tick in contact with a dead-vertical face
        // xy < 1 = a hang (air stuck / park), not a surf (as::SurfPinned): latching it would keep
        // pressing after AS lets go
        Vec3 n{};
        if (!pinned_now || s_prePredVel.Length2D() < 1.f || !PsWallTouch(pLocal, n, org))
            return false;
        // a crate against the wall clips vz the same way: never latch a prop (MV_PinOnProp)
        if (MV_PinOnProp(org, mins, maxs, fil))
            return false;
        s_aaStickN = Vec3(n.x, n.y, 0.f);
        s_aaStickN.Normalize();
        s_aaStick = true;
        s_aaStickPress = 10.f;
        s_aaStickRideTick = 0;
        s_aaStickAnchor = org;
        DebugLog("AA: stick latch n=%.3f,%.3f z=%.3f vz=%.2f xy=%.1f duck=%d", s_aaStickN.x, s_aaStickN.y,
                 org.z, s_prePredVel.z, s_prePredVel.Length2D(), (s_prePredFlags & FL_DUCKING) ? 1 : 0);
    }

    // the face this tick: short hull sweep straight into the latched normal. A bent seam is
    // followed (normal re-read when it stays within ~25 deg); nothing within 2u = ride over
    float flGap = 0.f;
    float flPlaneD = -1.f;   // hull to the face PLANE (not its 0.03125 trace shell): sv_wallbug 0 ride lives while < 0.03125
    {
        GameTrace_t tr{};
        TraceHull(org, org - s_aaStickN * 2.f, mins, maxs, MASK_PLAYERSOLID, &fil, &tr);
        if (tr.fraction >= 1.f || tr.startsolid)
            { AutoAlignStickDrop("wall"); return false; }
        flGap = tr.fraction * 2.f;
        {
            const Vec3& N = tr.plane.normal;   // box corner nearest the plane: mins where N > 0, maxs where N < 0
            const float sup = (N.x > 0.f ? mins.x : maxs.x) * N.x + (N.y > 0.f ? mins.y : maxs.y) * N.y
                            + (N.z > 0.f ? mins.z : maxs.z) * N.z;
            flPlaneD = org.x * N.x + org.y * N.y + org.z * N.z + sup - tr.plane.dist;
        }
        const Vec3 nn(tr.plane.normal.x, tr.plane.normal.y, 0.f);
        if (fabsf(tr.plane.normal.z) < 0.7f && nn.Length2D() > 0.5f)
        {
            Vec3 cand = nn; cand.Normalize();
            if (cand.Dot(s_aaStickN) > 0.9f)
            {
                // a bent seam: the old spot's across distance means nothing on the new face
                if (cand.Dot(s_aaStickN) < 0.9999f) s_aaStickAnchor = org;
                s_aaStickN = cand;
            }
        }
    }

    // keys off the face = the player is leaving (same 0.5 as PixelSurf's hold k_leave_dot)
    if (TB_SteeringAwayFromWall(pCmd, s_aaStickN, pCmd->forwardmove, pCmd->sidemove, 0.5f))
        { AutoAlignStickDrop("leave"); return false; }

    // the player's move, handed back untouched on any tick the stick cannot hold
    const float flUserFwd = pCmd->forwardmove, flUserSide = pCmd->sidemove;

    Vec3 vFwd, vRight;
    AngleVectors(Vec3(0.f, pCmd->viewangles.y, 0.f), &vFwd, &vRight, nullptr);
    Vec3 vWish = vFwd * pCmd->forwardmove + vRight * pCmd->sidemove;
    vWish.z = 0.f;
    const Vec3  vInto(-s_aaStickN.x, -s_aaStickN.y, 0.f);
    const Vec3  vTan(s_aaStickN.y, -s_aaStickN.x, 0.f);
    const float flTan = std::clamp(vWish.Dot(vTan), -450.f, 450.f);

    // how far a candidate is flown; a full hold this long ends the search
    const int k_stickHorizon = as::TicksScaled(8, TickInterval());
    // the press search's own share of the cmd's sims: a dying ride runs every candidate to its break
    // tick, and TB / AS / WC / PS still need theirs after us (SimsLeft guards the pool).
    // OWN counter, not simsUsed: the drift hold spends 3 x (horizon + 2) = 54 before the search, and a
    // shared 96 cap left ~5 of the 18 candidates -- 100 / 200 / 450 / 0 and the whole no-tangent pass
    // were never flown on exactly the ride that needed them. No-tangent is the slow-ride save: under ~30
    // u/s along the face your strafe keys really accelerate the hull and bend the across velocity, at
    // ride speed they add nothing (addspeed <= 0). 144 = both passes, every press, 8 ticks
    constexpr int k_stickPressCap = 144;
    int simsUsed = 0, pressSims = 0;

    // ticks the move keeps the pin, 0..horizon (same cmd every tick)
    const auto proof = [&](const Vec3& mv) -> int
    {
        MV_WriteWishWorld(pCmd, mv, mv.Length2D());
        RestoreToFrame(frame);
        const int base = EntI(pLocal, g_offTickBase);
        int held = 0;
        for (int z = 0; z < k_stickHorizon && SimsLeft() > 0; z++)
        {
            EntSetI(pLocal, g_offTickBase, base + z);
            Pred_Simulate(pLocal, pCmd);
            ++simsUsed;
            if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || !on_pin(EntV3(pLocal, g_offVelocity)))
                break;
            held++;
        }
        RestoreToFrame(frame);
        return held;
    };

    // ---- DRIFT HOLD first: stop the across-face drift, do not press (MV_DriftHold, the why is there) ----
    const int k_driftHorizon = as::TicksScaled(16, TickInterval());
    MvDrift dh = MV_DriftHold(pLocal, pCmd, frame, s_aaStickN, false, k_driftHorizon, simsUsed);
    // short of the horizon = try steering back onto the latch spot (anchor, why at MV_DriftHold).
    // A healthy ride never pays for it; ties keep the plain hold
    // then creep (close half the gap a tick, MV_DriftHold has the why), then the anchor
    char driftMode = 'd';
    if (dh.held < k_driftHorizon)
    {
        const MvDrift dc = MV_DriftHold(pLocal, pCmd, frame, s_aaStickN, false, k_driftHorizon, simsUsed,
                                        nullptr, true);
        if (dc.held > dh.held) { dh = dc; driftMode = 'c'; }
    }
    if (dh.held < k_driftHorizon)
    {
        const MvDrift da = MV_DriftHold(pLocal, pCmd, frame, s_aaStickN, false, k_driftHorizon, simsUsed,
                                        &s_aaStickAnchor);
        if (da.held > dh.held) { dh = da; driftMode = 'a'; }
    }
    const int   heldDrift  = dh.held;
    const float flDriftK   = dh.k, flVaNone = dh.vaNone, flDriftOff = dh.doff;
    const float flAway0    = s_prePredVel.x * s_aaStickN.x + s_prePredVel.y * s_aaStickN.y;

    // ---- PRESS SEARCH: fallback when the drift hold cannot keep the pin -----------------------
    // air accel caps the wish at 30 but scales accel by the full wish: into-wall gain per tick is
    // airaccel * press * dt up to 30 u/s, so 200 already = the cap and 450 only matters with a
    // tangent eating the cap. 0 last: a press is what keeps the hull on the face
    static constexpr float k_press[] = { 10.f, 20.f, 30.f, 50.f, 75.f, 100.f, 200.f, 450.f, 0.f };
    int   best = 0;
    float bestPress = s_aaStickPress;
    Vec3  bestMove = vInto * s_aaStickPress + vTan * flTan;   // no proof = keep pressing, keep steering
    const bool bDriftWins = heldDrift >= k_stickHorizon;
    for (int pass = 0; pass < 2 && best < k_stickHorizon && !bDriftWins; pass++)
    {
        // pass 0 keeps the player's along-wall input, pass 1 drops it (the ride beats the steer)
        const float t = pass == 0 ? flTan : 0.f;
        if (pass == 0 && fabsf(t) < 10.f)
            continue;
        for (const float m : k_press)
        {
            if (SimsLeft() < k_stickHorizon || pressSims + k_stickHorizon > k_stickPressCap)
                break;
            const Vec3 mv = vInto * m + vTan * t;
            const int s0 = simsUsed;
            const int h = proof(mv);
            pressSims += simsUsed - s0;
            if (h > best || (h > 0 && h == best && m > bestPress)) { best = h; bestMove = mv; bestPress = m; }   // tie = harder press
            if (best >= k_stickHorizon) break;
        }
    }
    // NOTHING PINS = NOT A SURF ANY MORE: let go THIS tick with the player's move back. The old
    // 4-tick miss window kept writing a press / a zero move over the fall, i.e. over whatever the
    // player or an earlier feature (edge bug, strafe) had in the cmd
    // simsUsed 0 = the pool was empty (TB ran first and spent it), nothing was flown: that is no
    // proof the ride is over, so keep the no-proof press (bestMove) instead of dropping a live ride
    if (heldDrift == 0 && best == 0 && simsUsed > 0)
    {
        pCmd->forwardmove = flUserFwd;
        pCmd->sidemove    = flUserSide;
        s_aaStickTicks++; s_aaStickMissTot++;
        AutoAlignStickDrop("lost");
        return false;
    }
    s_aaWallDetected = true;   // PixelSurf's hunt gate: we are on a wall

    // drift hold wins outright when it holds a press-search horizon, and on any tie below it
    const bool bDrift = heldDrift > 0 && (bDriftWins || heldDrift >= best);
    if (bDrift)
    {
        pCmd->forwardmove = dh.fwd;
        pCmd->sidemove    = dh.side;
        best = std::min(heldDrift, k_stickHorizon);   // stats stay on the press horizon
        bestPress = dh.wish.Dot(vInto);               // + = into the face, - = away
    }
    else
    {
        MV_WriteWishWorld(pCmd, bestMove, bestMove.Length2D());
        if (best > 0 && bestPress > 0.f)
            s_aaStickPress = bestPress;
    }
    // sv_wallbug 0: a press pick owns the cmd too. PixelSurf's hold would swap it for a 450 gain
    // strafe proven 2 ticks, and every gain strafe re-clips the face (+DIST_EPSILON off it): the
    // 8-tick proof loses to a 2-tick greedy one and the drift ends the ride a few ticks later.
    // No gain is possible there anyway: at ride speed only a near-across wish accelerates.
    // Cvar not on the client = Valve's default, 0
    s_aaStickOwns = bDrift || (best > 0 && CvarFloat("sv_wallbug", 0.f) < 0.5f);
    s_aaStickPredPin = best > 0;

    s_aaStickTicks++;
    s_aaStickBestSum += best;
    if (best >= k_stickHorizon) s_aaStickFull++;
    else if (best > 0) s_aaStickPart++;
    else s_aaStickMissTot++;

    // per-tick ride trace: gap = hull to face, off = origin across the face normal (drift off the
    // lip shows as off creeping), best = ticks the pick holds. First 32 ticks of a ride, then only
    // the ticks that see the end coming, so a 10s hold is not 660 lines
    // mode d = drift hold, c = creep (half the gap a tick), a = steering back to the latch spot (press = solved wish, dh = ticks the closed loop held of k_driftHorizon,
    // doff = origin move off the face over that loop, va = across-face vel now / after a bare tick,
    // k = measured gain), p = press search
    if (g_debugLogEnable && (s_aaStickRideTick < 32 || best < k_stickHorizon))
        DebugLog("AA: stick t=%d %c pin=%d best=%d press=%.4f dh=%d doff=%.5f va=%.5f/%.5f k=%.4f tan=%.0f "
                 "gap=%.4f off=%.4f anc=%.4f pd=%.5f life=%.1f vz=%.3f xy=%.1f duck=%d/%d sims=%d",
                 s_aaStickRideTick, bDrift ? driftMode : 'p', pinned_now ? 1 : 0, best, bestPress, heldDrift,
                 flDriftOff, flAway0, flVaNone, flDriftK, flTan, flGap,
                 org.x * s_aaStickN.x + org.y * s_aaStickN.y,
                 s_aaStickAnchor.x * s_aaStickN.x + s_aaStickAnchor.y * s_aaStickN.y,
                 flPlaneD, (MV_SURF_EPS - flPlaneD) / (MV_SURF_EPS * TickInterval()), s_prePredVel.z, s_prePredVel.Length2D(),
                 (s_prePredFlags & FL_DUCKING) ? 1 : 0, (pCmd->buttons & IN_DUCK) ? 1 : 0, simsUsed);
    s_aaStickRideTick++;
    return true;
}

// air stuck = TryPlayerMove zeroed the move (allFraction 0, 3+ planes, crease reversal, end-pos stuck box:
// gamemovement.cpp TryPlayerMove) = xy 0, and FinishGravity's half step makes vz read as a pin. a hull corner
// wedged in a prop / slanted face. our cmd stalls within 2 ticks where the one we got does not = hand it back
static int s_aaGuardRev = 0;   // reverts, per second on the AA: ticks line
// this tick's face can wedge the AABB (not square = corner contact; props are refused before any press). only
// then is an xy 0 pin a stuck: on a square face a head-on surf zeroes xy too (wall + lip clip, crease d <= 0)
static bool s_aaWedge = false;
static void AutoAlignGuard(void* pLocal, UserCmd_t* pCmd, const UserCmd_t& got)
{
    if (pCmd->forwardmove == got.forwardmove && pCmd->sidemove == got.sidemove && pCmd->buttons == got.buttons)
        return;
    if (SimsLeft() < 4)
        return;
    const int frame = PredCommandsPredicted() - 1;
    const auto stalls = [&](UserCmd_t c) -> bool {
        RestoreToFrame(frame);
        const int base = EntI(pLocal, g_offTickBase);
        for (int z = 0; z < 2; z++)
        {
            EntSetI(pLocal, g_offTickBase, base + z);
            Pred_Simulate(pLocal, &c);
            if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || EntMoveType(pLocal) != MOVETYPE_WALK)
                return false;
            // vz too: a slow creep / corner crease is xy < 1 with a live vz, a wedge sits on the pin
            const Vec3 v = EntV3(pLocal, g_offVelocity);
            if (v.Length2D() < 1.f && fabsf(v.z - TargetPredictZVel()) < as::k_pinTol)
                return true;
        }
        return false;
    };
    UserCmd_t rcv = *pCmd;
    rcv.forwardmove = got.forwardmove; rcv.sidemove = got.sidemove; rcv.buttons = got.buttons;
    const bool back = stalls(*pCmd) && !stalls(rcv);
    RestoreToFrame(frame);
    if (!back)
        return;
    pCmd->forwardmove = got.forwardmove;
    pCmd->sidemove    = got.sidemove;
    pCmd->buttons     = got.buttons;
    s_aaStickOwns    = false;   // PS hold / pixel hop would keep the stalled cmd as AA's
    s_aaStickPredPin = false;
    ++s_aaGuardRev;
}

// ---- Auto align (nudge movement into a nearby wall to set up / hold a pixel surf) ----
static void AutoAlignImpl(void* pLocal, UserCmd_t* pCmd)
{
    static float s_startCircle = 0.f;
    static int   s_aaPropRej = 0;   // faces refused for being a prop (diag, per second)

    s_aaStickOwns = false;   // per cmd: only the stick's pick below sets it
    if (!g_autoAlign) // plain on/off, no bind (user pick)
    {
        s_aaWallDetected = false;
        AutoAlignStickDrop("off");
        return;
    }

    // NO YIELD TO THE STICK, and had one. The stick is active on every ride tick
    // with a wall in 12u, so that yield switched AutoAlign off for the WHOLE of every ride and
    // put a flat 10u press in place of the detector-driven one -- which is "some pixelsurfs that
    // work with just auto align don't work with auto pixel surf", exactly. The stick is the
    // fallback for a ride AutoAlign is not holding, so it writes FIRST and AutoAlign, running
    // after, still has the last word on any wall it can see
    // and to the assist's seam press, for the same reason: it writes forwardmove /
    // sidemove reproducing PixelFinder's probe, and AutoAlign runs after it and assigns both
    if (s_psAssistPressIn)
        return;

    // stick diag, per second: full = held the whole horizon, part = some ticks, miss = none,
    // avg = mean ticks held, div = sim said pin and the real tick fell
    if (g_debugLogEnable && s_aaStickTicks > 0)
    {
        static int s_stickSec = -1;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (sec != s_stickSec)
        {
            s_stickSec = sec;
            DebugLog("AA: stick ticks=%d full=%d part=%d miss=%d avg=%.1f div=%d press=%.0f",
                     s_aaStickTicks, s_aaStickFull, s_aaStickPart, s_aaStickMissTot,
                     float(s_aaStickBestSum) / float(s_aaStickTicks), s_aaStickDiv, s_aaStickPress);
            s_aaStickTicks = s_aaStickFull = s_aaStickPart = s_aaStickMissTot = 0;
            s_aaStickDiv = s_aaStickBestSum = 0;
        }
    }
    // TB bind on = the stick runs AFTER TextureBug instead (AutoAlignStickAfterTb): run here it
    // latched the first pinned tick before TB could catch the crease, wrote a press over the
    // player's keys that TB then read as input, and spent the sims TB hunts with
    if (!TbBindOn() && AutoAlignStick(pLocal, pCmd))
    {
        s_aaWedge = !MV_IsHullFlatWall(s_aaStickN);
        return;
    }

    if (EntI(pLocal, g_offFlags) & FL_ONGROUND)
        return;

    const int mt = EntMoveType(pLocal);
    if (mt == MOVETYPE_NOCLIP)
    {
        s_aaWallDetected = false;
        return;
    }

    Vec3 vStartPos = EntV3(pLocal, g_offOrigin);
    Vec3 vMins = EntV3(pLocal, g_offMins);
    Vec3 vMaxs = EntV3(pLocal, g_offMaxs);
    const Vec3 vVel = EntV3(pLocal, g_offVelocity);

    GameTrace_t trace{};
    float flFoundAngle = 0.f;
    s_aaWallDetected = false;

    // THE SCAN IS MV_DetectWallLines NOW, the same one TB/AS/WC run. The hand-rolled
    // 16-ray circle this replaces was why AutoAlign read worse than TB on the same wall:
    //   * NO LOOKAHEAD. Its rays ended 1u from the centre, flat, so it only ever saw a face the
    //     hull was ALREADY touching. The detector adds dot(vel,dir)*tick per direction plus
    //     fall_look -- and this log runs 285 u/s median, 357 at p90, i.e. 4.3-5.4u of travel in
    //     the tick the sweeps simulate. AutoAlign was finding every wall exactly ONE TICK LATE,
    //     which is the "delay before it activates on a wall" written up at the detector's header.
    //   * THE LOOP NEVER WRAPPED: `for (a = s_startCircle; a < 2pi; ...)`. After a hit at angle a
    //     the next tick only searched [a, 2pi) -- a wall drifting to a LOWER angle went missing,
    //     reset the cache and cost a whole tick of steer. The detector runs
    //     [scan_start, scan_start + 2pi) through fmodf, so the cache is a fast path, not a blind
    //     spot. Same 16 directions either way.
    //   * hull-only, so a lip shorter than the z step was invisible; the detector's centre-column
    //     sweep crosses every face between its lines.
    // reach stays 1 -- "a face inside 1u + hull" is AutoAlign's contract vs psGuide's >10u, and
    // the lookahead rides on top of it (AS does the same), so the two stay disjoint by range.
    // z band 0.001 = exactly MV_IsHullFlatWall's own z test, so nothing about which planes count
    // as upright moves in this change
    if (MV_DetectWallLines(pLocal, vStartPos, vMins, vMaxs, s_startCircle,
                           trace, flFoundAngle, 0.001f, 1.f, &vVel))
    {
        // the detector filters neither of these -- no ramps, triggers, displacements, and no face
        // the player box cannot sit flush against (MV_IsHullFlatWall: corner contact = stuck).
        // It returns the NEAREST facing hit, so a rejection here ends the tick rather than
        // walking on to the next angle: one detector call is 120-230 traces and the nearest wall
        // is the one AutoAlign would hold anyway
        // ---- AND A PROP IS NOT A WALL (user: "auto align gets stuck on props") ----------------
        // Every other part of pixel surf refuses them already -- the hunt's gate, the pin test,
        // the finder -- for the reason written at MV_TraceIsProp: a prop's collision is a convex
        // hull out of its .phy at whatever angle the mapper dropped it, so the into-wall press
        // meets it at a hull CORNER instead of flush, the corner digs past the face and the hull
        // wedges. This function is the one that KEEPS PRESSING while that happens (0-100 u/s
        // straight down the normal, and a 10 u/s fallback press when no sim finds a park), so it
        // is the one that turns the wedge into a stick. MV_IsHullFlatWall was the guard for it and
        // it cannot be: it reads the returned PLANE, and a crate dropped square to the world
        // answers a perfectly axis-aligned normal while its hull still contacts on a corner.
        // Nothing is lost by refusing outright -- there is no sub-unit lip to ride on a prop
        if (MV_TraceIsProp(trace))
        {
            s_aaPropRej++;
        }
        // DIAGONAL BRUSH FACES COUNT AS A WALL. s_aaWallDetected is PixelSurf's hunt gate, and with
        // auto align off PsWallDetectFallback takes ANY upright face -- so turning auto align ON used
        // to switch the hunt off on every diagonal wall. The press below stays square-only on
        // sv_wallbug 1 (MV_IsHullFlatWall, corner contact); on sv_wallbug 0 a brush plane clips the
        // hull box exactly (no prop hull to dig a corner into, those are refused above), and the
        // DIST_EPSILON drift needs the press there as much as on a square face
        else if (fabsf(trace.plane.normal.z) <= 0.001f
            && !(trace.surface.flags & SURF_TRIGGER_MV) && !(trace.dispFlags & DISPSURF_SURFACE))
        {
            s_aaWallDetected = true;
            s_startCircle = flFoundAngle;
        }
    }

    if (!s_aaWallDetected)
    {
        s_startCircle = 0.f;
        return;
    }
    s_aaWedge = !MV_IsHullFlatWall(trace.plane.normal);
    // cvar not on the client = Valve's default, 0 (same read as the stick)
    const bool bWb0 = CvarFloat("sv_wallbug", 0.f) < 0.5f;
    if (!MV_IsHullFlatWall(trace.plane.normal) && !bWb0)
        return;   // diagonal on sv_wallbug 1: hunt gate only, keys untouched

    // seamless leave: actively steering off the wall hands control back this tick
    if (TB_SteeringAwayFromWall(pCmd, trace.plane.normal, pCmd->forwardmove, pCmd->sidemove))
    {
        s_aaWallDetected = false;
        return;
    }

    Vec3 vNormalPlane = Vec3(trace.plane.normal.x * -0.005f, trace.plane.normal.y * -0.005f, 0.f);
    Vec3 vWallAngle = VectorAngles(vNormalPlane);
    ClampAnglesMv(vWallAngle);

    float flRotation = DEG2RADF(vWallAngle.y - pCmd->viewangles.y);
    float flCosRot = cosf(flRotation);
    float flSinRot = sinf(flRotation);

    const float flSurfZ = CvarFloat("sv_gravity", 800.f) * TickInterval() * 0.5f;

    // pin the prediction frame (bare restore drifts one slot per Simulate)
    const int nBasePredicted = PredCommandsPredicted();
    const auto RestoreOriginal = [&]() { RestoreToFrame(nBasePredicted - 1); };

    // blend the player's along-wall (tangent) input into the hold candidates first
    const float flUserFwd = pCmd->forwardmove;
    const float flUserSide = pCmd->sidemove;

    Vec3 vFwdDir, vRightDir;
    AngleVectors(Vec3(0.f, pCmd->viewangles.y, 0.f), &vFwdDir, &vRightDir, nullptr);
    Vec3 vWishWorld = vFwdDir * flUserFwd + vRightDir * flUserSide;
    vWishWorld.z = 0.f;

    Vec3 vInto = Vec3(-trace.plane.normal.x, -trace.plane.normal.y, 0.f);
    vInto.Normalize();
    const Vec3 vTangentDir = Vec3(trace.plane.normal.y, -trace.plane.normal.x, 0.f);
    const float flTangentInput = std::clamp(vWishWorld.Dot(vTangentDir), -450.f, 450.f);

    bool bDetect = false;
    // ---- sv_wallbug 0: THE CATCH PICKS THE RIDE LENGTH -----------------------------------------
    // 11-11 log, 9 of 10 stick rides: every press drifted the hull off the face at the same 0.00047u a
    // tick (Valve's DIST_EPSILON clip, MV_DriftHold) and the pin broke after ~0.0044u of it, ~10 ticks,
    // whatever the stick pressed -- the ride's life is set on the CATCH tick (how deep the hull got onto
    // the lip), the stick can only spend it. (The 10th held 32: x = -8316, where one float ulp of
    // origin is 0.00098 > the drift, so it rounds back. Map luck, not a technique.)
    // botox's loop below takes the FIRST press that pins one tick = the shallowest catch on offer, and
    // PixelSurf's hold then swapped it for its own 2-tick-proof gain strafe anyway. Here every candidate
    // flies up to k_catchHorizon ticks and the one that HOLDS LONGEST wins; s_aaStickOwns makes PS
    // keep it. Ties: hardest press, then list order -- PS's own gain strafe (450) first, steer kept. 200 / 450 are in because 90 only
    // reaches 13.5 of the 30 u/s air accel cap. sv_wallbug 1 keeps botox's loop: no drift, every ride
    // holds, nothing to choose. CATCH TICKS ONLY: a cmd that starts pinned is a live ride -- the stick's
    // (with the TB bind on this scan still runs on ride ticks, the stick only after TB), and 160 sims a
    // ride tick on top of the stick's would starve TB / AS / PS
    const bool bCatch = bWb0 && fabsf(s_prePredVel.z - TargetPredictZVel()) >= as::k_pinTol;
    if (bCatch)
    {
        const int k_catchHorizon = as::TicksScaled(32, TickInterval());
        constexpr int k_catchSimCap = 160;   // a candidate that misses costs 1; only pins fly long
        const float pin = TargetPredictZVel();
        int sims = 0, cands = 0, bestHold = 0;
        float bestFwd = flUserFwd, bestSide = flUserSide, bestPress = 0.f;
        char bestKind = '-';
        const auto fly = [&]() -> int {
            RestoreOriginal();
            const int base = EntI(pLocal, g_offTickBase);
            int held = 0;
            for (int z = 0; z < k_catchHorizon && SimsLeft() > 0 && sims < k_catchSimCap; z++)
            {
                EntSetI(pLocal, g_offTickBase, base + z);
                Pred_Simulate(pLocal, pCmd);
                ++sims;
                // xy >= 1 on a wedge face only: a zeroed move (air stuck) ends on the same vz (s_aaWedge)
                const Vec3 v = EntV3(pLocal, g_offVelocity);
                if ((EntI(pLocal, g_offFlags) & FL_ONGROUND) || !as::SurfPinned(v.z, s_aaWedge ? v.Length2D() : 1.f, pin))
                    break;
                held++;
            }
            RestoreOriginal();
            return held;
        };
        // true = a full hold, stop looking
        const auto consider = [&](char kind, float press) -> bool {
            if (SimsLeft() < 1 || sims >= k_catchSimCap) return true;
            ++cands;
            const int h = fly();
            // tie = HARDEST press wins (user: push harder once caught). 11-12 log: every catch tied
            // hold=1/33 over all 24-25 candidates and list order handed it to press=0, the softest
            if (h > bestHold || (h > 0 && h == bestHold && press > bestPress))
            {
                bestHold = h; bestFwd = pCmd->forwardmove; bestSide = pCmd->sidemove;
                bestPress = press; bestKind = kind;
            }
            return bestHold >= k_catchHorizon;
        };
        bool done = false;
        // PS hold's gain strafe, under PS hold's own rule (g_psWallStrafe 2 = always, 1 = keys into the face)
        {
            const float wl = vWishWorld.Length2D();
            const bool keysIn = wl > 0.01f && vWishWorld.Dot(vInto) / wl > 0.25f;
            float gy = 0.f;
            if (On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode) && (g_psWallStrafe == 2 || (g_psWallStrafe == 1 && keysIn))
                && as::WallStrafeYaw(trace.plane.normal.x, trace.plane.normal.y, vVel.x, vVel.y,
                                     CvarFloat("sv_air_max_wishspeed", 30.f), gy))
            {
                as::MoveTowardYaw(pCmd->viewangles.y, gy, 450.f, pCmd->forwardmove, pCmd->sidemove);
                done = consider('g', 450.f);
            }
        }
        static constexpr float k_catchPress[] = { 0.f, 10.f, 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 90.f, 200.f, 450.f };
        for (int iPass = 0; iPass < 2 && !done; iPass++)
        {
            const float flTangent = iPass == 0 ? flTangentInput : 0.f;
            if (iPass == 0 && fabsf(flTangent) < 10.f)
                continue;
            for (const float m : k_catchPress)
            {
                const Vec3 vMove = vInto * m + vTangentDir * flTangent;
                MV_WriteWishWorld(pCmd, vMove, vMove.Length2D());
                if ((done = consider(iPass == 0 ? 't' : 'p', m))) break;
            }
        }
        pCmd->forwardmove = bestFwd;
        pCmd->sidemove    = bestSide;
        bDetect = bestHold > 0;
        s_aaStickOwns = bDetect;
        // hold = ticks the pick keeps the pin of the horizon: the catch depth, read straight off the sim.
        // kind g = PS gain strafe, t = press + your steer, p = bare press
        if (bDetect && g_debugLogEnable)
            DebugLog("AA: catch pick=%c press=%.0f hold=%d/%d cands=%d sims=%d xy=%.1f vz=%.2f", bestKind, bestPress,
                     bestHold, k_catchHorizon, cands, sims, vVel.Length2D(), vVel.z);
    }
    for (int iPass = 0; iPass < 2 && !bDetect && !bCatch; iPass++)
    {
        const float flTangent = iPass == 0 ? flTangentInput : 0.f;
        if (iPass == 0 && fabsf(flTangent) < 10.f)
            continue;

        // ride ticks: hardest first, first pin wins (botox went 0 -> 90 = softest pin, and 90 is only
        // 13.5 of the 30 u/s air accel cap; 200 / 450 reach it)
        static constexpr float k_ridePress[] = { 450.f, 200.f, 90.f, 80.f, 70.f, 60.f, 50.f, 40.f, 30.f, 20.f, 10.f, 0.f };
        for (const float flMultiplier : k_ridePress)
        {
            RestoreOriginal();

            const Vec3 vMove = vInto * flMultiplier + vTangentDir * flTangent;
            const float flMag = vMove.Length2D();
            if (flMag < 0.01f)
            {
                pCmd->forwardmove = 0.f;
                pCmd->sidemove = 0.f;
            }
            else
            {
                Vec3 vMoveAngle = VectorAngles(vMove);
                const float flMoveRot = DEG2RADF(vMoveAngle.y - pCmd->viewangles.y);
                pCmd->forwardmove = cosf(flMoveRot) * flMag;
                pCmd->sidemove = -sinf(flMoveRot) * flMag;
            }

            Pred_Simulate(pLocal, pCmd);

            // xy >= 1 on a wedge face only: a zeroed move (air stuck) ends on the same vz (s_aaWedge)
            const Vec3 vEnd = EntV3(pLocal, g_offVelocity);
            if (fabsf(vEnd.z + flSurfZ) < 1.5f && (!s_aaWedge || vEnd.Length2D() >= 1.f))
            {
                bDetect = true;
                break;
            }
        }
    }

    // hand the entity back un-simulated; pCmd keeps the winning move
    RestoreOriginal();

    if (!bDetect)
    {
        pCmd->forwardmove = flCosRot * 10.f;
        pCmd->sidemove = -flSinRot * 10.f;
    }
    // sv_wallbug 0 keeps botox's 10 u/s press too (= botox-css AA, same 1/32 clip nudge). The 11-08
    // creep fallback (half the gap a tick) was deleted 11-09: 08:02 log, every creep catch was shallow
    // (pd >= 0.0293, 0-1 tick rides) while PS hold's hard-press catches rode 10-15. Ride length = catch
    // depth (css RULE), and a ~0 closing speed approach is the shallow one

    // AutoAlign shipped with NO diag at all, so 6056 lines of debug.log had 3551 TB:
    // and zero AA:, and "it aligns worse than TB" had nothing to read. 120th like TB's ramp
    // hands-off: per second, not per session. det=0 is the case to watch -- that is the sim
    // failing to find a park and the fallback OVERWRITING the player's move with a 10 u/s push
    // (max wish is 450), which is what a mushy approach feels like
    if (g_debugLogEnable)
    {
        static int s_aaTicks = 0, s_aaDet = 0, s_aaSec = -1;
        ++s_aaTicks; s_aaDet += bDetect ? 1 : 0;
        const int sec = g_globalVars ? (int)g_globalVars->curtime : 0;
        if (sec != s_aaSec)
        {
            s_aaSec = sec;
            const Vec3 v = EntV3(pLocal, g_offVelocity);
            // prop= is the new one: ticks whose nearest face was a prop and was refused. A ride
            // that used to end in a wedge now reads as prop counting up with det holding
            // guard= cmds AutoAlignGuard handed back (ours stalled, yours did not)
            DebugLog("AA: ticks=%d det=%d prop=%d guard=%d ang=%.2f nx=%.2f ny=%.2f nz=%.3f frac=%.3f "
                     "tan=%.1f xy=%.0f vz=%.1f look=%.1f fwd=%.1f side=%.1f",
                     s_aaTicks, s_aaDet, s_aaPropRej, s_aaGuardRev, flFoundAngle, trace.plane.normal.x,
                     trace.plane.normal.y,
                     trace.plane.normal.z, trace.fraction, flTangentInput, v.Length2D(), v.z,
                     v.Length2D() * TickInterval(), pCmd->forwardmove, pCmd->sidemove);
            s_aaTicks = 0; s_aaDet = 0; s_aaPropRej = 0; s_aaGuardRev = 0;
        }
    }
}

static void AutoAlign(void* pLocal, UserCmd_t* pCmd)
{
    const UserCmd_t got = *pCmd;
    s_aaWedge = false;
    AutoAlignImpl(pLocal, pCmd);
    if (s_aaWedge)   // square face = native, no guard sims
        AutoAlignGuard(pLocal, pCmd, got);
}

// ---- surf stick with the TB bind on: runs right after TextureBug ----------------------------
// TB gets the cmd first with the player's own keys and the full sim pool. Any tick TB touched the
// cmd (ride, hug, wallstrafe, its own run-up align -- same pre/post compare as tbx's `acted`) is
// TB's, and the stick lets go; a pin TB did not claim (plain pixel surf) is still held. The
// stick's own "other" gate now reads THIS cmd's TB flags, not last cmd's
static void AutoAlignStickAfterTb(void* pLocal, UserCmd_t* pCmd)
{
    if (!g_autoAlign || s_psAssistPressIn || !TbBindOn())
        return;
    if (pCmd->forwardmove != s_wcPreTbFwd || pCmd->sidemove != s_wcPreTbSide || pCmd->buttons != s_wcPreTbButtons)
        { AutoAlignStickDrop("tb"); return; }
    // TB's align already flew the presses on this pinned START and none holds the next tick: a TB
    // crease ending. A fresh latch there re-proved the same thing with up to 96 sims on ticks TB
    // already spent 3-6 ms on (15:29 log: 122 latches, 122 'lost' the same tick). A live ride
    // still runs: the stick's wider search (drift / 450 / tangent) is what it is for
    if (!s_aaStick && s_tbAlignDead)
        return;
    const UserCmd_t got = *pCmd;
    if (AutoAlignStick(pLocal, pCmd) && !MV_IsHullFlatWall(s_aaStickN))   // square face = native, no guard sims
        AutoAlignGuard(pLocal, pCmd, got);
}

// TB pin tests, ceiling detect, the sweep helpers and HeadSurf all moved INTO
// texturebug.inl (user: "keep everything tb related inside the texturebug.inl").
// TB_WallSupportDistance + TB_SteeringAwayFromWall deliberately stay above: AutoAlign
// and WallClimb call them, and both run before this include point.
#include "texturebug.inl"


// ---- Air stuck: botox air_stuck.cpp 1:1 (airstuck.inl) ----
#include "airstuck.inl"

// ---- Wall climb: the pre-port disp sweep + armed hop (wallclimb.inl). After airstuck.inl: AsClock lives there and
// wc yields to AS's pins
#include "wallclimb.inl"

// the whole pixel finder + pixel surf assist moved OUT of movement.cpp into
// psassist.inl (user: "seperate the ps assist function into a different file"). Same
// include trick texturebug.inl uses -- the code keeps this file's static prediction
// helpers (Pred_Simulate / RestoreToFrame / SimsLeft / s_prePredFlags / s_prePredVel).
#include "psassist.inl"

// pixeljump's pixel hop (botox css SgPixelHop): needs psassist.inl's s_psAssistHopActive, airstuck.inl's
// AsUserCmd and texturebug.inl's TextureBugBotox, so after all three
#include "pixelhop.inl"

// the route calculator, and it is included AFTER psassist.inl on purpose -- it flies
// the assist's own choreography (PsHopInputs) and snaps its markers onto the finder's points,
// so it needs both of them declared. Same include trick, same file-static prediction helpers.
#include "routecalc.inl"

// fake movement tab: ProcessMovement hooks (server + client), local server only
#include "fakemove.inl"

// whole-map pixel surf scan. AFTER fakemove.inl on purpose: the map's brushes come from
// FM_Brushes (IEngineTrace GetBrushesInAABB / GetBrushInfo), and the sims come from
// psassist.inl's column scanner, so it needs both included first.
#include "psmap.inl"

// practice tab: console commands only (bots, buildings, noclip, save/teleport). AFTER routecalc.inl
// on purpose -- it aims with that file's RcRay and RcAimPlayerHead, which is what puts a placed
// bot on a player's HEAD instead of inside him.
#include "practice.inl"

// small tricks (nulls/hsw/delay hop/auto crouch/smooth
// demo view/jumpstats). After everything else -- it only reads helpers already defined above.
#include "movement_ex.inl"

// auto bounce = botox auto_bounce.cpp. After airstuck.inl (AsClock, AsSetOrigin), before
// movement_tricks2.inl
#include "autobounce.inl"

// bounce/pf modes/eb presets/ps fix. After movement_ex.inl (needs
// edgebug.inl's s_eb* statics and this file's own s_*Hit flags, all already defined above).
#include "movement_tricks2.inl"
// edge skip + blockbot. After movement_ex.inl -- reuses its AutoCrouch-shaped
// trace helpers conceptually but shares nothing directly, just the file's own statics.
#include "movement_edge.inl"

// ==== per-cmd pipeline (Aletherium RunPre -> prediction -> RunPost -> restore) ====
static void Movement_RunImpl(UserCmd_t* pCmd)
{
    // one line, once per session. What USED to be here was ~10 KB of build changelog stamped
    // into the log (and into the .rdata of every shipped DLL) on the first cmd. History belongs
    // in the handoff docs, not in the binary.
    static bool s_verStamped = false;
    if (!s_verStamped) { s_verStamped = true;
        DebugLog("movement: dih159 (movement.cpp + psassist/texturebug/routecalc/fakemove .inl, "
                 "physics in routesim.h)"); }

    // one is_active_app + one read per distinct bind, for the WHOLE cmd (see MovementKeyOK).
    // Must lead: every On() below, and every feature they call, reads this snapshot.
    MovementBinds_NewCmd();

    ResolveNetvars();
    Movement_ResolveExtraNetvars();
    FakeMove_Tick();   // before the dead/noclip returns: those ticks must clear its binds
    Practice_Tick(pCmd);   // same reason: a practice bind is pressed while noclipping or dead

    const int idx = GetLocalPlayerIndex();
    if (idx <= 0) return;
    void* pLocal = GetClientEntity(idx);
    const bool bAlive = pLocal && EntAlive(pLocal);
    // botox create_move: capture the player's own input, then lay the replay frame on the cmd so
    // every feature below + prediction sees it. Dead = reset. Its end half runs in Movement_Run.
    Recorder_CmdStart(bAlive ? pLocal : nullptr, pCmd);
    if (!bAlive) return;

    // a movement blocker (noclip, water, a stun/taunt cond) stops the MOVEMENT features.
    // It does not stop the MEASURING ones: the route calculator, the pixel calculator and the ps
    // finder answer off a hull teleported onto a spot, and flying around the map placing markers
    // is how they are actually used. Their sims run as a walking player (g_predWalkSims below),
    // or every leg of every solve is a FullNoClipMove that never falls and never lands.
    const int  iLocalMove = EntMoveType(pLocal);
    const bool bMoveBlocked = iLocalMove != MOVETYPE_WALK || EntSwimming(pLocal)
                           || EntCondBlocked(pLocal);
    // the last term is the overlay alone: "show points" draws off a heartbeat this function
    // publishes, so a blocked cmd has to reach it even when nothing is measuring
    const bool bCalcOn = g_routeCalc || g_pixelCalc || g_psFinder || s_pfBusy || s_rcBusy
                      || s_pmBusy || s_pmReq || g_psMapLines
                      || (g_psAssist && g_psAssistRender);
    if (bMoveBlocked && !bCalcOn) return;

    RefreshTargetPredictZVel();  // once per cmd -- IsTargetVz in sweep loops reads cache
    Pred_ResetCmd();            // shared sim pool + restore counters reset per cmd
    s_tracesThisCmd = 0;        // perf counters, printed as tr= / res= / resk= / fast=
    // a fresh cmd starts on the LIVE end-of-cmd state, not the restore base, so the first
    // restore of the tick always runs
    g_predDirty = true;
    TbBudgetStart();            // the clock the whole TB family searches against
    // no AS sim fence any more: botox air_stuck searches on its own wall clock (g_asBudget), so
    // the shared pool is everyone's
    g_simReserve = 0;

    // ---- pre-prediction ----
    s_rawJumpHeld = (pCmd->buttons & IN_JUMP) != 0;   // real press, before AutoJump strips it
    s_rawButtons  = pCmd->buttons;                    // ...and the rest of it
    s_rawFwd      = pCmd->forwardmove;
    s_rawSide     = pCmd->sidemove;
    s_edgeJumpFired = false;   // per-cmd: only the post-pred EdgeJump call sets it
    if (!bMoveBlocked)   // movement features only. The raw button reads above are shared
    {
        // blockbot PRE, before every other assist gets a say over forwardmove/sidemove
        BlockBot(pLocal, pCmd);
        EsPre(pLocal, pCmd);   // edge skip's own pre-prediction snapshot, same timing as EdgeBugPre
        EdgeBugPre(pLocal, pCmd);
        EdgeBugSoundPre(pLocal);   // real-state reset check, not tied to the search
        AutoJump(pLocal, pCmd);
        EdgeJump(pLocal, pCmd, false);
        AutoJumpbug(pLocal, pCmd);
        AutoStrafe(pLocal, pCmd);
        StrafeOptimizer(pLocal, pCmd);
        // Movement tricks port -- pure usercmd math/traces, same tier as the calls above
        Nulls(pLocal, pCmd);
        Hsw(pLocal, pCmd);
        DelayHop(pLocal, pCmd);
        MiniJumpWaitTimer(pLocal, pCmd);
        AutoGoomba(pLocal, pCmd);   // after the jump owners: its press/release is the last word
        AutoCrouch(pLocal, pCmd);
        DoubleJumpStrafe(pLocal, pCmd);   // after AutoStrafe: owns the wish on the dash tick
        SmoothDemoView(pLocal, pCmd);
    }

    // botox: pre features may have rewritten it -- predict + detect on the exact recorded inputs
    Recorder_CmdPrePred(pCmd);

    // ---- engine prediction (post features read predicted state; skip clean if sigs miss) ----
    s_prePredFlags = g_offFlags > 0 ? EntI(pLocal, g_offFlags) : 0;
    s_prePredVel   = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity) : Vec3();
    s_prePredOrigin  = g_offOrigin > 0 ? EntV3(pLocal, g_offOrigin) : Vec3();
    s_prePredDucking = g_offDucking > 0 && EntB(pLocal, g_offDucking) != 0;
    s_prePredTick    = g_globalVars->tickcount;   // before Pred_Start moves it
    PsCmdStart(pLocal);   // botox ps_cmd_start: a duck key flipped on a live ride bails pixel surf
    const bool probesOK = g_frameReady && g_offVelocity > 0 && g_offOrigin > 0
                       && g_offMins > 0 && g_offMaxs > 0 && g_offFlags > 0;
    // the movement half is asked for only on a cmd the movement features may act on --
    // a blocked cmd runs none of them, so starting prediction for them would buy one wasted sim
    const bool needPredMove = !bMoveBlocked
                       && (EdgeJumpOn()
                        || On(g_longJump, g_longJumpKey, g_longJumpMode)
                        || On(g_miniJump, g_miniJumpKey, g_miniJumpMode)
                        || (probesOK && (On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode)
                                      || g_autoAlign
                                      || On(g_textureBug, g_textureBugKey, g_textureBugMode)
                                      || On(g_wallClimb, g_wallClimbKey, g_wallClimbMode)
                                      || On(g_airStuck, g_airStuckKey, g_airStuckMode)
                                      // auto bounce runs in the probe block: no prediction = never called
                                      || On(g_autoBounce, g_autoBounceKey, g_autoBounceMode)
                                      || On(g_psAssist, g_psAssistKey, g_psAssistMode)
                                      || On(g_pixelJump, g_pixelJumpKey, g_pixelJumpMode))));
    const bool needPred = needPredMove
                       || (probesOK && (
                                     // finder: held OR just released (release tick runs the scan),
                                     // plus every later cmd a chunked scan is still running on
                                        On(g_psFinder, g_psFinderKey, g_psFinderMode)
                                     || s_psFinderHeld || s_pfBusy
                                     // the whole-map scan is the same scanner on a queue of
                                     // seams, and the REQUEST is in here for the same reason
                                     // the route calc's key is: the cmd that starts it has to
                                     // already be predicted or the button does nothing
                                     || s_pmBusy || s_pmReq
                                     // a solve spans commands, so prediction has to
                                     // stay up for every one of them -- resume into a dead
                                     // frame and every leg after it is a lie. The KEY is in
                                     // here too: the press tick is the one that seeds the
                                     // root snapshot, and it happens before s_rcBusy is set.
                                     // the pixel calculator's key answers on its OWN
                                     // feature flag -- it used to need the route calculator on
                                     // too, so a pixel-only user got "prediction is not available"
                                     || s_rcBusy
                                     || (g_routeCalc && g_rcSolveKey != 0
                                         && MovementKeyOK(g_rcSolveKey, 1))
                                     || (g_pixelCalc && g_pcSolveKey != 0
                                         && MovementKeyOK(g_pcSolveKey, 1))));
    s_predicted = needPred && Pred_Start(pLocal, pCmd);
    // AFTER Pred_Start on purpose: the cmd's OWN prediction tick stays honest (a
    // noclipping hull predicts as noclip, so the finder's anchor ray leaves the eye you are really
    // looking from), and only the probe sims that follow -- finder, route calc, pixel calc, which
    // teleport the hull somewhere else first anyway -- are flown as a walking player.
    // Cleared in Movement_Run, so an early return or the SEH there cannot leave it set
    g_predWalkSims = iLocalMove != MOVETYPE_WALK;

    // ---- post-prediction ----
    if (!bMoveBlocked)
    {
        EdgeJump(pLocal, pCmd, true);
        FastMovement(pLocal, pCmd);
        LongJump(pLocal, pCmd);
        MiniJump(pLocal, pCmd);
    }

    // PS assist sits before PixelSurf (Aletherium RunPost order); hop bookkeeping,
    // set/delete points and choreography run even un-predicted -- only the pickers sim
    // a blocked cmd runs no assist, but the overlay heartbeat is not the assist -- without
    // it the finder's points stop drawing 500ms into the noclip you are flying to look at them
    if (probesOK)
    {
        if (bMoveBlocked) PS_PublishAlive(pLocal);
        else              PS_AssistTick(pLocal, pCmd, s_predicted);
    }

    // probe features: need working prediction + frame-restore sigs (Aletherium RunPost order:
    // PixelSurf ducks first, AutoAlign steers with the crouch active, then TB -> HS -> WC -> AS)
    if (s_predicted && probesOK)
    {
        if (!bMoveBlocked)   // these MOVE you. The finder below only measures
        {
            PsRideDetect();   // this cmd's ride bit, for every reader below
            // botox on_create_move_pre:415 -- last cmd's pixel_surf latch is the crouch this cmd
            // flies with, applied before auto_align / air_stuck get the cmd.
            // botox movement.cpp:525 -- ground drops the latch. PixelSurfImpl returns on ground
            // without clearing it, so a low ps landed inside the 16-tick window ducked forever
            if ((s_prePredFlags & FL_ONGROUND) && (s_psShouldSurf || s_psShouldUnduck))
            {
                DebugLog("PSU: ground drop %s", s_psShouldSurf ? "duck" : "stand");
                s_psShouldSurf = s_psShouldUnduck = false;
            }
            // a ps assist arc owns IN_DUCK (PsHopInputs): last cmd's PSU latch flipping it = off the proven arc
            if (s_psAssistArcOwns)     s_psShouldSurf = s_psShouldUnduck = false;
            if (s_psShouldSurf)        pCmd->buttons |= IN_DUCK;
            else if (s_psShouldUnduck) pCmd->buttons &= ~IN_DUCK;
            PsSurfFixTick(pLocal, pCmd);   // hold duck a couple ticks into a fresh ride
            AutoAlign(pLocal, pCmd);
            s_wcPreTbFwd = pCmd->forwardmove; s_wcPreTbSide = pCmd->sidemove; s_wcPreTbButtons = pCmd->buttons;
            s_tbAlignDead = false;
            TextureBug(pLocal, pCmd);
            AutoAlignStickAfterTb(pLocal, pCmd);   // TB bind on only; off = it ran inside AutoAlign
            // cumidere style only: the botox style runs its own head_bounce inside TextureBug
            // and has no head surf (botox csgo texturebug.cpp has none)
            if (g_tbStyle == 0)
            {
                HeadSurf(pLocal, pCmd);
                // after HS on purpose (coldtires texturebug.cpp:212): a ceiling RIDE always beats a
                // one-tick clip, and HeadBounce returns outright when TB or HS owns the tick
                HeadBounce(pLocal, pCmd);
            }
            // botox css / sergowebz order (z.dll FUN_18001adc0): texture bug, pixel hop, then wall stuck (AS / PS)
            PixelHop(pLocal, pCmd);
            // AS moved ABOVE WallClimb. coldtires states the order outright -- "priority high ->
            // low: texture_bug, air_stuck, wall_climb, edge_bug" -- and ours had AS dead last,
            // yielding to WC and arriving at a drained sim pool. HS stays above it: HS is TB's
            // ceiling twin (shares the TB key, and TB->HS is a deliberate transition), not a rival
            AirStuckTick(pLocal, pCmd);
            WallClimb(pLocal, pCmd);
            AutoBounce(pLocal, pCmd);   // botox auto_bounce: yields to every s_*Hit flag above
            // botox on_create_move_post:1267 -- pixel_surf is the last mover: it yields to this
            // cmd's AS / TB / WC flags (not last tick's) and its hold re-proves the final move
            PixelSurf(pLocal, pCmd);
        }
        PixelFinder(pLocal, pCmd);
        PixelMapScan(pLocal, pCmd);   // whole-map scan: same scanner, a queue of brush seams
    }
    else
    {
        s_textureBugHit = false;
        s_tbOwnsCmd = false;
        s_headSurfHit = false;
        s_headBounceHit = false;
        s_wallClimbHit = s_wallClimbActive = s_wcLanded = s_wcSyncWant = s_phSyncWant = false;
        s_airStuckHit = false;
        s_pixelHopHit = s_pixelHopOn = false;
        s_asHolding = false;
        s_shouldPixelSurf = false;
        s_psShouldSurf = s_psShouldUnduck = false;
        s_ab.ownsCmd = false;   // botox movement.cpp:1260: stale true on a skipped tick
        // PixelFinder is the only owner of its key edge, and it did not run this cmd. Left alone,
        // s_psFinderHeld stays stuck true for good: needPred never drops, and the NEXT real press
        // reads as "already held" so no column is ever anchored again
        s_psFinderHeld = false;
        s_psFinderFirst = false;
        // same reason for the chunked scan : its cursor points at a prediction base that
        // no longer exists, so drop it instead of resuming into a dead frame
        if (s_pfBusy)
        {
            DebugLog("PF: scan dropped at step %d/%d -- prediction went away", s_pfNextI, s_pfSteps);
            PfCancelScan();
        }
        PmAbort();   // ...and the map scan's queue, for the same reason. Lines already found stay
    }

    // the route calculator. Markers and keys run on EVERY cmd (placing one needs a
    // trace, not a sim); the solver only when prediction is live, and it has to run before
    // Pred_End because its legs are engine sims inside this cmd's prediction scope.
    // the pixel calculator is its own feature and runs the same per-cmd entry point
    if (g_routeCalc || g_pixelCalc)
        RouteCalc_Tick(pLocal, pCmd, s_predicted && probesOK);

    if (s_predicted)
        Pred_End(pLocal, pCmd);
    s_predicted = false;

    // edge bug runs AFTER Pred_End (Aletherium: after EnginePrediction.End), probing the
    // ENGINE's predicted frames with its own re-established tick-time window
    // botox: recorder owns cmd = eb post over a desynced replay overwrites recorded duck timing;
    // and a plan latched by pre would hold the mouse lock, so drop it
    if (Recorder_OwnsCmd())
    {
        EbDropPlan("recorder");
        EbDonorReset();
        EsReset(true);
    }
    else if (probesOK && !bMoveBlocked)
    {
        EdgeBugPost(pLocal, pCmd);
        EdgeSkipPost(pLocal, pCmd);   // same timing: after Pred_End, re-establishes its own window
    }
    // botox create_move_proxy:578 -- the owned wallstrafe move goes back on last (texturebug.inl)
    if (!bMoveBlocked)
        TB_PreserveMove(pLocal, pCmd);

    // ---- publish the indicator snapshot (movement thread -> render thread) ----
    // Last thing in the pipeline so every feature's hit flag is final. Bind state is read
    // HERE and not on the render thread: On() goes through the per-cmd bind memo, which is
    // movement-thread-only state.
    {
        MvInd_t s{};
        s.ms       = GetTickCount64();
        s.onGround = (EntI(pLocal, g_offFlags) & FL_ONGROUND) != 0;
        s.speed    = g_offVelocity > 0 ? EntV3(pLocal, g_offVelocity).Length2D() : 0.f;
        s.tick     = g_globalVars ? g_globalVars->tickcount : 0;
        s.curtime  = g_globalVars ? g_globalVars->curtime : 0.f;
        s.wjSeq    = s_fmWjSeq;
        s.wjSpeed  = s_fmWjSpeed;
        s.tbOn    = On(g_textureBug, g_textureBugKey, g_textureBugMode);
        s.tbHit    = s_textureBugHit;
        s.tbOwn    = s_tbOwnsCmd;
        TB_GetInd(s.tbPhase, s.tbRide, s.tbSeams, s.tbD, s.tbVin, s.tbAim);
        s.asOn     = On(g_airStuck, g_airStuckKey, g_airStuckMode);
        s.asHit    = s_airStuckHit;
        s.wcOn     = On(g_wallClimb, g_wallClimbKey, g_wallClimbMode);
        s.wcHit    = s_wcLanded;   // botox m_wall_climb_now = m_hit
        s.psOn     = On(g_pixelSurf, g_pixelSurfKey, g_pixelSurfMode);
        // (user: "light up even if it didnt auto crouch when on a surf"). psHit used to
        // be the AUTO-CROUCH flag, so a standing surf -- a real ride, just one the hull already
        // fits into -- left the tag dark.
        // ...then it was `s_shouldPixelSurf || s_psRidingNow`, which lights on the HUNT'S
        // PREDICTION (a fall beside a wall latches it and nothing has to happen) and on every
        // other way the engine pins vz at -halfgrav -- an air stuck, a texture bug's crease, a
        // hull wedged on a crate all read identically. ps::IsSurf is the honest question: a
        // sustained pin, against a brush face we are flush with, whose face ENDS above our feet
        // (that is a lip being ridden, not a wall we are jammed in). Evidence measured in
        // PixelSurf; the two flags below are only final HERE, which is why the call is here
        s_psEv.otherOwns = s_airStuckHit || s_textureBugHit;
        s.psHit    = ps::IsSurf(s_psEv);
        // one measurement, one cmd: PixelSurf only runs with prediction up and the move
        // unblocked, so consuming the evidence here is what keeps a cmd it SKIPPED from lighting
        // the tag off the last tick it did run on
        s_psEv = ps::SurfEvidence{};
        // ps assist: delusional's HITGODA is exactly this flag -- true while a sim-validated
        // hop is pending or in flight, cleared the tick the arc ends
        s.astOn    = On(g_psAssist, g_psAssistKey, g_psAssistMode);
        s.astHit   = s_psAssistHopActive && !s_psAssistHopPj;
        s.pjOn     = On(g_pixelJump, g_pixelJumpKey, g_pixelJumpMode);
        s.pjHit    = (s_psAssistHopActive && s_psAssistHopPj) || s_pixelHopHit;
        s.ebOn     = On(g_edgeBug, g_edgeBugKey, g_edgeBugMode);
        s.ebHit    = s_ebDetected;           // publish runs after EdgeBugPost, so this is final
        // ...and so is the plan the overlay draws. One publish point, so every early return and
        // every drop inside EdgeBugPost is reflected without hooking each of them
        EbVizPublish(s.ebOn && s_ebDetected, s_ebCurrentTick);
        s.strafeOn = On(g_autoStrafe, g_autoStrafeKey, g_autoStrafeMode);
        s.ejOn     = On(g_edgeJump, g_edgeJumpKey, g_edgeJumpMode);
        s.ljOn     = On(g_longJump, g_longJumpKey, g_longJumpMode);
        s.mjOn     = On(g_miniJump, g_miniJumpKey, g_miniJumpMode);
        s.mjHit    = s_miniHit;   // the takeoff duck actually went out (not refused/stripped)
        s.jbOn     = On(g_jumpbug, g_jumpbugKey, g_jumpbugMode);
        // botox m_can_jb: lit for 0.2 s after the jb, so a one-tick hit survives the frame sampling
        static unsigned long long s_jbHitMs = 0;
        if (s_jumpbugHit) s_jbHitMs = s.ms;
        s.jbHit    = s_jbHitMs && s.ms - s_jbHitMs < 200;
        s.ebPred   = s_ebDetected && s_ebCurrentTick < s_ebLockTicks;
        s.dhOn     = On(g_delayHop, g_delayHopKey, g_delayHopMode);
        s.acOn     = On(g_autoCrouch, g_autoCrouchKey, g_autoCrouchMode);
        s.abOn     = On(g_autoBounce, g_autoBounceKey, g_autoBounceMode);
        s.hswOn    = On(g_hsw, g_hswKey, g_hswMode);
        s.esOn     = On(g_edgeSkip, g_edgeSkipKey, g_edgeSkipMode);
        s.esHit    = s_esActive;
        s.gmbOn    = On(g_goomba, g_goombaKey, g_goombaMode);
        // one-tick fire, held 0.2 s like jb so the frame sampling sees it
        static unsigned long long s_gmbHitMs = 0;
        if (s_goombaFired) s_gmbHitMs = s.ms;
        s_goombaFired = false;
        s.gmbHit   = s_gmbHitMs && s.ms - s_gmbHitMs < 200;
        s.buttons  = pCmd->buttons;
        s.mouseDx  = pCmd->mousedx;
        s.mouseDy  = pCmd->mousedy;
        s_mvInd    = s;

        // botox run_detections: cmd START state (pre-pred), hit flags final here
        DetTick d;
        d.onGround    = (s_prePredFlags & FL_ONGROUND) != 0;
        d.walk        = iLocalMove == MOVETYPE_WALK;
        d.jumpPressed = (pCmd->buttons & IN_JUMP) != 0;
        d.vz          = s_prePredVel.z;
        d.speed       = s_prePredVel.Length2D();
        d.gravity     = CvarFloat("sv_gravity", 800.f);
        d.interval    = TickInterval();
        d.tick        = s_prePredTick;
        d.tb = s_textureBugHit; d.as = s_airStuckHit; d.ps = s.psHit; d.wc = s_wcLanded;
        Detections_Tick(d);

        // botox jump_stats: same cmd start state; trick flags = botox get_trick_states set
        JsFrame jf;
        jf.ground  = d.onGround;
        jf.walk    = d.walk;
        jf.x = s_prePredOrigin.x; jf.y = s_prePredOrigin.y; jf.z = s_prePredOrigin.z;
        jf.vx = s_prePredVel.x;   jf.vy = s_prePredVel.y;   jf.vz = s_prePredVel.z;
        jf.airDash = g_offAirDash > 0 ? EntI(pLocal, g_offAirDash) : 0;
        JsCmd jc;
        jc.tick    = pCmd->tick_count;
        jc.duck    = (pCmd->buttons & IN_DUCK) != 0;
        jc.mouseDx = pCmd->mousedx;
        jc.trick   = s_ebDetected || s_textureBugHit || s_airStuckHit || s_wcLanded || s.psHit;
        Jumpstats_Tick(jf, jc, g_offMaxspeed > 0 ? EntF(pLocal, g_offMaxspeed) : 0.f);
    }
}

void MV_GetIndicators(MvInd_t& out) { out = s_mvInd; }

void Movement_Run(UserCmd_t* pCmd)
{
    if (g_eject || !pCmd) return;
    __try {
        Movement_RunImpl(pCmd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("movement: SEH in Movement_Run"); }
    }
    // botox records the FINAL cmd: out here so every early return in RunImpl (blocked move,
    // water, taunt) still steps/records. No-op unless RunImpl's Recorder_CmdStart began this cmd.
    Recorder_CmdEnd(pCmd);
    // Same reason as g_predWalkSims below, one level worse: a fault between Pred_Start and
    // Pred_End leaves gpGlobals->tickcount / curtime / frametime on the PREDICTION tick, and
    // the engine's CL_Move -> CL_SendMove -> CPrediction::Update run off those three the
    // instant we return. No-op on every normal cmd (Pred_End already cleared the flag).
    Pred_AbortIfOpen();
    // out here, not at the end of RunImpl: every early return and the SEH above have to
    // clear it too, or one blocked cmd leaves every later sim of the session forced to walk
    g_predWalkSims = false;
    g_predInSim    = false;
    // the IN_JUMP bit the server is about to see = m_nOldButtons for the NEXT cmd. Every
    // feature that presses jump on a ground tick (mini jump's chain, wall climb's launch, the
    // PS assist launch) has to know whether the engine will take that press or refuse it as a
    // pogo stick, and only a JUMPLESS cmd clears the latch. Recorded out here rather than at
    // the end of RunImpl so a dead / noclip / early-return tick still updates it
    s_prevCmdJump = (pCmd->buttons & IN_JUMP) != 0;
    s_prevCmdDuck = (pCmd->buttons & IN_DUCK) != 0;
}

// ============================================================================
// CHLClient::CreateMove hook + CInput plumbing (folded in from bhop.cpp).
// UserCmd_t (movement.h) IS the 72-byte TF2 x64 CUserCmd layout, so it doubles
// as the m_pCommands[] element type for stride/indexing.
// ============================================================================
static constexpr int MULTIPLAYER_BACKUP = 90;
static constexpr int kInputCmdsOff      = 264; // vtable(8) + pad0[256] -> CInput::m_pCommands

void*        g_input      = nullptr;
CreateMove_t o_CreateMove = nullptr;

// CInput::GetUserCmd (IInput vtable slot 8). Engine sends/predicts the cmd via GetUserCmd = a
// CRC-verified COPY snapshotted INSIDE CInput::CreateMove -- button edits after o_CreateMove never
// reach it. Return the live m_pCommands slot instead (Aletherium CInput_GetUserCmd hook).
//
// THE COMMAND-NUMBER CHECK IS NOT OPTIONAL. The original is four lines (in_main.cpp:1399):
//     usercmd = &m_pCommands[ sequence_number % MULTIPLAYER_BACKUP ];
//     if ( usercmd->command_number != sequence_number ) return NULL;  // overwritten by a newer cmd
// The ring is 90 slots, so asking for anything older wraps onto a LIVE command, and a hook that
// hands that back is telling the engine a command exists when it does not. Two callers pay:
//   * CPrediction::PerformPrediction (prediction.cpp:1623) walks incoming_acknowledged+1 ..
//     outgoing_command and breaks on `if ( !cmd )`. Kill the NULL and the loop runs to its only
//     other exit, i >= MULTIPLAYER_BACKUP -- up to 90 full RunSimulation passes PER FRAME, on
//     commands that are not the ones it asked for. That gap only opens when acks fall behind,
//     i.e. during loss: the first hitch hands the client a 90x prediction bill, the frame time
//     that buys makes the next ack later still, and the loss sustains itself. Exactly the
//     "it starts and then it stays" shape.
//   * CInput::WriteUsercmdDeltaToBuffer (:1353) delta-encodes against whatever came back, so a
//     stale baseline is sent as if it were the real one.
// Falling through to o_GetUserCmd rather than returning nullptr outright: the original repeats
// the same test on the same array and answers NULL itself, so this stays a pure passthrough on
// a path that is rare by construction.
using GetUserCmd_t = UserCmd_t*(__fastcall*)(void* rcx, int seq);
static GetUserCmd_t o_GetUserCmd = nullptr;

// body in _impl: uses __try, so the MiscHookGuard cannot live here (C2712)
static UserCmd_t* __fastcall h_GetUserCmd_impl(void* rcx, int seq)
{
    if (g_eject || !g_input) return o_GetUserCmd(rcx, seq);
    __try {
        static bool s_hit = false;
        if (!s_hit) { s_hit = true; DebugLog("bhop: GetUserCmd live path hit"); }
        UserCmd_t* pCmds = *reinterpret_cast<UserCmd_t**>(
            reinterpret_cast<uintptr_t>(g_input) + kInputCmdsOff);
        if (pCmds && seq >= 0) {
            UserCmd_t* pCmd = reinterpret_cast<UserCmd_t*>(
                reinterpret_cast<uintptr_t>(pCmds) + (size_t)(seq % MULTIPLAYER_BACKUP) * sizeof(UserCmd_t));
            if (pCmd->command_number == seq)
                return pCmd;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return o_GetUserCmd(rcx, seq);
}

static UserCmd_t* __fastcall h_GetUserCmd(void* rcx, int seq)
{
    MiscHookGuard _g;
    return h_GetUserCmd_impl(rcx, seq);
}

// CInput::ValidateUserCmd -- TF2 x64 re-checks each cmd vs the CRC-verified copy before send and
// reverts tampered ones. Aletherium kills it (empty hook) -- without this every post-CreateMove
// button edit is undone = no-op even with the live GetUserCmd.
using ValidateUserCmd_t = void(__fastcall*)(void* rcx, void* usercmd, int seq);
static ValidateUserCmd_t o_ValidateUserCmd = nullptr;

static void __fastcall h_ValidateUserCmd(void* rcx, void* usercmd, int seq)
{
    MiscHookGuard _g;
    if (g_eject) o_ValidateUserCmd(rcx, usercmd, seq);
    // else: skip -- validation defeated on purpose
}

static void InstallValidateUserCmdHook(HMODULE hClient)
{
    if (o_ValidateUserCmd) return;
    uint8_t* addr = PatternScan(hClient,
        "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B F9 41 8B D8"); // Aletherium CInput_ValidateUserCmd sig
    if (!addr) { DebugLog("bhop: ValidateUserCmd sig NOT FOUND"); return; }
    if (MH_CreateHook((LPVOID)addr, (LPVOID)h_ValidateUserCmd, (LPVOID*)&o_ValidateUserCmd) == MH_OK) {
        MH_EnableHook((LPVOID)addr);
        DebugLog("bhop: ValidateUserCmd hook installed (no-op)");
    }
}

static void InstallGetUserCmdHook()
{
    if (o_GetUserCmd || !g_input) return;
    __try {
        void** vt = *(void***)g_input;
        if (reinterpret_cast<uintptr_t>(vt) >= 0x10000 && vt[8]) {
            if (MH_CreateHook(vt[8], (LPVOID)h_GetUserCmd, (LPVOID*)&o_GetUserCmd) == MH_OK) {
                MH_EnableHook(vt[8]);
                DebugLog("bhop: GetUserCmd hook installed (slot 8)");
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DebugLog("bhop: SEH during GetUserCmd install");
    }
}

bool ResolveInput()
{
    if (g_input) { InstallGetUserCmdHook(); return true; }
    HMODULE hClient = GetModuleHandleA("client.dll");
    if (!hClient) return false;

    InstallValidateUserCmdHook(hClient);

    uint8_t* addr = PatternScan(hClient,
        "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 85 C0 0F 84 ? ? ? ? F3 0F 10 05");
    if (!addr) { DebugLog("bhop: CInput signature NOT FOUND"); return false; }

    __try {
        // mov rcx, [rip+disp32] : instruction len 7, disp at +3 -> &g_pInput, deref once -> CInput*
        int32_t   disp    = *reinterpret_cast<int32_t*>(addr + 3);
        uintptr_t pGlobal = reinterpret_cast<uintptr_t>(addr) + 7 + disp;
        g_input = *reinterpret_cast<void**>(pGlobal);
    } __except(EXCEPTION_EXECUTE_HANDLER) { g_input = nullptr; }

    if (g_input) DebugLog("bhop: CInput resolved %p", g_input);
    InstallGetUserCmdHook();
    return g_input != nullptr;
}

// Resolve the live cmd slot, hand it to Movement_Run (all feature logic there).
// body in _impl: uses __try, so the MiscHookGuard cannot live here (C2712)
static void __fastcall h_CreateMove_impl(void* rcx, int seq, float frametime, bool active)
{
    o_CreateMove(rcx, seq, frametime, active); // let engine fill the command first

    // g_predReady is Movement_Install's own done flag, and this hook goes in one line BEFORE
    // Movement_Install runs (thread.cpp PollThread), so the engine can reach us with the
    // trace/prediction interfaces still unresolved. Movement_RunGuarded -- the aim half's entry
    // to the same pipeline -- has always checked it; this path did not, and leaned on the SEH
    // inside Movement_Run to swallow the fault instead.
    if (g_eject || !g_input || !g_predReady || seq < 0) return;

    Avatar_Pump();   // game-thread Steam calls, rate-limited internally

    UserCmd_t* pCmd = nullptr;
    __try {
        UserCmd_t* pCmds = *reinterpret_cast<UserCmd_t**>(
            reinterpret_cast<uintptr_t>(g_input) + kInputCmdsOff);
        if (pCmds)
            pCmd = reinterpret_cast<UserCmd_t*>(
                reinterpret_cast<uintptr_t>(pCmds) + (size_t)(seq % MULTIPLAYER_BACKUP) * sizeof(UserCmd_t));
    } __except(EXCEPTION_EXECUTE_HANDLER) { pCmd = nullptr; }

    if (pCmd)
    {
        Movement_Run(pCmd);
        // botox create_move_proxy: lua pre_prediction (alive only) then create_move, last before the send.
        // This path has no send-packet pointer: cmd.send_packet reads true, writes are dropped.
        if (IsLocalPlayerAlive()) g_lua.on_create_move(pCmd, nullptr, true);
        Movement_AirStuckLastWriter(pCmd);   // nothing runs after us on this path
        g_lua.on_create_move(pCmd, nullptr, false);
    }
}

// Entry point for the aim half's CreateMove hook (CHLClient_CreateMove, src/hacks/aim/hooks.cpp),
// which owns the CHLClient::CreateMove slot when that half loads. That hook has the cmd
// already; all this adds is what dih's own hook wrapper added -- the eject drain counter and the
// g_eject early-out. Without the guard, MH_Uninitialize could free a trampoline while the
// movement pipeline is still running inside it.
// the LAST writer of the cmd: the aimbot (silent aim's CorrectMovement) runs after the movement
// pipeline and re-expresses view / forwardmove / sidemove, which breaks an exact-float pin.
// AS_FinalStamp (airstuck.inl) puts the cmd AS proved back ("use movement fix" decides how)
static void AS_LastWriterImpl(UserCmd_t* pCmd)
{
    const int idx = GetLocalPlayerIndex();
    void* pLocal = idx > 0 ? GetClientEntity(idx) : nullptr;
    if (!pLocal || !EntAlive(pLocal)) return;
    AS_FinalStamp(pCmd);
}
// __try and the guard object cannot share a function (C2712)
static void AS_LastWriterSeh(UserCmd_t* pCmd)
{
    __try {
        AS_LastWriterImpl(pCmd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; DebugLog("movement: SEH in Movement_AirStuckLastWriter"); }
    }
    Pred_AbortIfOpen();
}
void Movement_AirStuckLastWriter(UserCmd_t* pCmd)
{
    if (g_eject || !pCmd || !g_predReady) return;
    MiscHookGuard _g;
    AS_LastWriterSeh(pCmd);
}

void Movement_RunGuarded(UserCmd_t* pCmd)
{
    MiscHookGuard _g;
    // g_predReady is Movement_Install's own done flag. The aim CreateMove hook is installed
    // BEFORE Movement_Install runs (it has to be -- it claims the slot dih would otherwise take),
    // so for the first few commands the movement port's interfaces/sigs are not resolved yet.
    if (g_eject || !pCmd || !g_predReady) return;
    // aim owns CreateMove -> h_CreateMove never installed, its Avatar_Pump dead. Pump here instead.
    Avatar_Pump();
    Movement_Run(pCmd);
}

void __fastcall h_CreateMove(void* rcx, int seq, float frametime, bool active)
{
    MiscHookGuard _g;   // the whole movement pipeline runs under this -- drain it on eject
    CrashSpan _s("cm/move");   // routecalc / prediction / sims: the per-tick cost nothing else measures
    h_CreateMove_impl(rcx, seq, frametime, active);
}

// ============================================================================
// no push -- C_TFPlayer::AvoidPlayers (Unibox/Aletherium CTFPlayer_AvoidPlayers)
// Called from CTFPlayer::ClientThink -> AvoidPlayers(pCmd) each cmd. It sums a
// separation force off every teammate inside 72u and folds it into
// forwardmove/sidemove, so skipping it is the whole feature.
// ============================================================================
AvoidPlayers_t o_AvoidPlayers = nullptr;

void __fastcall h_AvoidPlayers(void* rcx, UserCmd_t* pCmd)
{
    MiscHookGuard _g;
    if (g_noPush && !g_eject) return;
    o_AvoidPlayers(rcx, pCmd);
}
