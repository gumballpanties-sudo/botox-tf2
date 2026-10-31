// Gate test for the route calculator core (src/hacks/movement/routesim.h): TF2 z physics against the SDK's
// own numbers and against hops recorded in game (C:\dih\routes\maps_plr_hightower.bsp.*), dih's
// AutoJumpbug button rule, then the DAG search against an independent brute force.
// Build + run:  cmake --build build --config Release --target routesim_test && build\Release\routesim_test.exe
#include "../src/hacks/movement/routesim.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace rs;

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
#define NEAR(a, b, eps) CHECK(std::fabs((double)(a) - (double)(b)) <= (eps))

static const float kFloor = -255.96875f;   // hightower floor: plane -256 + 1/32, as recorded

static State Hover(float z) { State s; s.z = z; s.ground = true; return s; }

struct Arc { std::vector<State> st; std::vector<TickInfo> ti; };

// one button sequence off a floor, flown until it lands back on that floor
template <class F>
static Arc FlyFn(F cmdAt, State s, float floorEff, int maxTicks = 200, const Params& p = Params())
{
    Arc a;
    World w; w.floor = true; w.floorEff = floorEff;
    for (int i = 0; i < maxTicks; i++)
    {
        Cmd c = cmdAt(i, s, w);
        TickInfo ti;
        Tick(s, c, w, p, &ti);
        a.st.push_back(s); a.ti.push_back(ti);
        if (s.ground && i > 0) break;
    }
    return a;
}
static Arc FlyOnFloor(int sc, State s, float floorEff, int maxTicks = 200, const Params& p = Params())
{
    ScriptMem m;
    return FlyFn([&](int i, const State& st, const World& w) { return ScriptCmd(sc, i, st, m, w, p); },
                 s, floorEff, maxTicks, p);
}

static float Apex(const Arc& a, float from)
{
    float top = -1e9f;
    for (const State& s : a.st) top = std::max(top, s.z);
    return top - from;
}

// the free-fall part of the arc, no floor under it: does a falling tick land in the surf window?
static bool CatchesSurf(int sc, float launchZ, float seam, bool* ducked = nullptr)
{
    const Params p;
    State s = Hover(launchZ); ScriptMem m;
    World dep; dep.floor = true; dep.floorEff = kFloor;
    bool falling = false;
    for (int i = 0; i < 200; i++)
    {
        World w = falling ? World() : dep;
        Cmd c = ScriptCmd(sc, i, s, m, w, p);
        TickInfo ti;
        Tick(s, c, w, p, &ti);
        if (ti.moveVz < 0.f) falling = true;
        if (falling && ti.moveVz < 0.f)
        {
            const float d = seam - s.z;
            if (d > kSurfLo && d < kSurfHi) { if (ducked) *ducked = s.ducked; return true; }
        }
        if (falling && s.z < seam - 30.f) break;
    }
    return false;
}

static Result Solve(const std::vector<Point>& pts, const Start& st, const Options& o, const Params& p = Params())
{
    Solver sv; sv.Begin(pts, st, p, o);
    while (!sv.Step(1000)) {}
    return sv.Out();
}
static bool Has(const Result& r, const std::string& text)
{
    for (const Combo& c : r.combos) if (c.text == text) return true;
    return false;
}
static bool HasSub(const Result& r, const std::string& sub)
{
    for (const Combo& c : r.combos) if (c.text.find(sub) != std::string::npos) return true;
    return false;
}

// ================================================================================================
//  BRUTE FORCE -- every plan of every leg, no merging, no DAG, no pruning past 60u under a target.
//  Written from movement.cpp's statics and call order (not from Solver): shares only the engine
//  (Tick), the button scripts and the jumpbug rule, which sections 1-9 test on their own.
// ================================================================================================
// dih's feature statics, by their movement.cpp names, plus the server clock
struct BFeat
{
    bool userDuck = false;        // the player's own duck key (crouch walk off)
    bool psAirDuckHeld = false;   // s_psAirDuckHeld
    bool shouldPixelSurf = false; // s_shouldPixelSurf
    bool psShouldDuck = false;    // s_psShouldDuck
    int  psPinTick = 0;           // s_psPinTick
    int  pixelSurfTicks = 0;      // s_pixelSurfTicks
    bool psRidingNow = false;     // s_psRidingNow
    bool prevSurfVel = false;     // s_prevSurfVel
    bool mjHoldDuck = false;      // s_mjHoldDuck
    int  now = 1000;              // gpGlobals->tickcount
    bool onSeam = false;
};
struct BPath { State s; BFeat f; std::string text; };

static float BHalf(const Params& p) { return -(p.gravity * p.dt * 0.5f); }
static bool BSurfVel(float vz, const Params& p) { return std::fabs(vz - BHalf(p)) < 1.5f; }

static void BruteLeg(const Params& p, const BPath& from, int layer, const Start& st, const std::vector<Point>& pts,
                     unsigned gmoves, std::vector<BPath>& out)
{
    const Point& T = pts[layer];
    const bool last = layer + 1 == (int)pts.size();
    const bool tFloor = T.type == PT_GROUND || T.type == PT_EDGEBUG;
    bool depFloor; float depZ; unsigned allowed; bool depPlayer;
    const char* depOn;
    if (layer == 0) { depFloor = !st.air; depZ = st.floorEff; allowed = gmoves & st.moves; depOn = OnName(st.player, st.building); depPlayer = st.player; }
    else { depFloor = pts[layer - 1].type == PT_GROUND; depZ = pts[layer - 1].z; allowed = gmoves & pts[layer - 1].moves & kMovesPress;
           depOn = OnName(pts[layer - 1].player, pts[layer - 1].building); depPlayer = pts[layer - 1].player; }
    bool land = true, jb = false;
    if (T.type == PT_GROUND)
    {
        land = (gmoves & T.moves & kMovesPress) != 0;
        jb = (gmoves & T.moves & (1u << MB_JUMPBUG)) != 0;
    }
    auto join = [&](std::string a, const std::string& b) { if (b.empty()) return a; return a.empty() ? b : a + " -> " + b; };
    auto stance = [](const State& x) { return std::string(x.ducked ? " (ducked)" : " (stand)"); };

    // one leg, one plan, flown cmd by cmd the way the cmd hook runs (movement.cpp ~5020):
    // keys, AutoJump/AutoJumpbug, prediction, LongJump, MiniJump, PixelSurf, then the engine
    struct Out { State s; BFeat f; std::string dep, arr; };
    auto fly = [&](int sc, bool useJb, State s, BFeat F, std::vector<Out>& res)
    {
        const bool walk = sc == SC_WALKOFF || sc == SC_WALKOFFDUCK;
        const bool press = sc == SC_JUMP || sc == SC_MINI || sc == SC_LONG;
        World g; g.floor = depFloor; g.floorEff = depZ;
        if (press && s.ground && (s.fDuck || s.ducking))
        {
            F.userDuck = false;                       // Bunnyhop: jump held, duck let go
            bool stood = false;
            for (int k = 0; k < 60 && !stood; k++)
            {
                Cmd c; c.jump = true; Tick(s, c, g, p); F.now++;
                if (!s.ground || s.edgy) return;
                stood = !s.fDuck && !s.ducking;
            }
            if (!stood) return;
            Tick(s, Cmd(), g, p); F.now++;
            if (!s.ground || s.edgy) return;
        }
        if (sc == SC_WALKOFFDUCK)
        {
            F.userDuck = true;
            for (int k = 0; k < 40 && !(s.fDuck && !s.ducking); k++)
            {
                Cmd c; c.duck = true; TickInfo ti; Tick(s, c, g, p, &ti); F.now++;
                if (!s.ground || s.edgy || ti.duckStripped) return;
            }
            if (!(s.fDuck && !s.ducking)) return;
        }
        if (sc == SC_WALKOFF && (s.fDuck || s.ducking)) return;
        const bool srcGround = s.ground;
        const std::string dep = srcGround ? WithOn(MoveName(ScriptBit(sc)), depOn) : "";
        bool falling = false, left = false, above = false;
        int  tbSeen = 0;                       // texturebug: straddling ticks this leg has offered
        for (int i = 0; i < 400; i++)
        {
            if (tFloor && s.z >= T.z - 0.001f) above = true;
            World w;
            if (!left && depFloor) { w.floor = true; w.floorEff = depZ; w.floorPlayer = depPlayer; }
            if (tFloor && (falling || above) && (!w.floor || T.z > w.floorEff)) { w.floor = true; w.floorEff = T.z; w.floorPlayer = T.player; }
            if (T.type == PT_HEADBANG) { w.ceil = true; w.ceilEff = T.z; }
            const bool walkTick = walk && i == 0 && srcGround;
            if (walkTick) w.floor = false;

            // keys
            Cmd c;
            c.jump = press && i == 0;
            c.duck = F.userDuck;
            // AutoJumpbug (pre): hands the tick back to a live ride (g_jbYieldSurf)
            bool band = false; float mg = 0.f;
            if (useJb && !(F.psRidingNow || F.shouldPixelSurf)) JumpbugCmd(s, w, p, c, band, mg);
            // prediction
            State pr = s; Tick(pr, c, w, p);
            // LongJump: IN_DUCK on the takeoff cmd and the next
            if (sc == SC_LONG && i <= 1) c.duck = true;
            // MiniJump
            if (s.ground && pr.ground) F.mjHoldDuck = false;
            if (sc == SC_MINI && i == 0) { c.duck = true; F.mjHoldDuck = true; }
            else if (F.mjHoldDuck && !s.ground)
            {
                const bool flDuck = s.fDuck;
                const float fall = s.vz < 0.f ? -s.vz * p.dt : 0.f;
                float raw = 48.f;
                if (w.floor && !w.floorPlayer && s.z >= w.floorEff - 0.001f) raw = std::min(std::max(s.z - w.floorEff, 0.f), 48.f);
                const bool room = raw - fall >= (s.vz > 0.f ? 26.f : 21.f);
                const bool landing = raw <= fall + 2.f;
                if (!flDuck && !s.ducking) F.mjHoldDuck = false;
                else if (s.ducking) {}
                else if (landing) F.mjHoldDuck = false;
                else if (!room) c.duck = true;
                else F.mjHoldDuck = false;
            }
            // PixelSurf
            {
                const bool surfVel = BSurfVel(s.vz, p) && (!s.ground || F.shouldPixelSurf);
                F.psRidingNow = surfVel && F.prevSurfVel;
                F.prevSurfVel = surfVel;
                if (s.ground || pr.ground)
                {
                    F.shouldPixelSurf = false; F.psShouldDuck = false; F.psAirDuckHeld = false; F.psPinTick = 0;
                }
                else
                {
                    if (!F.shouldPixelSurf)
                    {
                        if (T.type == PT_SURF)
                        {
                            bool found = false;
                            for (int pass = 0; pass < 2 && !found; pass++)
                            {
                                const bool duckPass = F.psAirDuckHeld || pass == 1;
                                if (F.psAirDuckHeld && pass == 1) break;
                                State q = s; Cmd qc = c; qc.duck = duckPass;
                                for (int z = 0; z < p.psHorizon; z++)
                                {
                                    TickInfo qt; Tick(q, qc, w, p, &qt);
                                    if (q.ground) break;
                                    const float d = T.z - q.z;
                                    if (qt.moveVz < 0.f && d > kSurfLo && d < kSurfHi)
                                    {
                                        F.shouldPixelSurf = true; F.psShouldDuck = duckPass;
                                        F.pixelSurfTicks = F.now + z + 16; F.psPinTick = F.now + z;
                                        found = true; break;
                                    }
                                }
                            }
                        }
                    }
                    else if (F.now > F.pixelSurfTicks && !BSurfVel(pr.vz, p))
                    {
                        F.shouldPixelSurf = false; F.psShouldDuck = false; F.psPinTick = 0;
                    }
                    const bool pinDue = F.psPinTick <= 0 || F.now >= F.psPinTick - 1;
                    if ((F.shouldPixelSurf && F.psShouldDuck && pinDue) || F.psAirDuckHeld)
                    {
                        c.duck = true; F.psAirDuckHeld = true;
                    }
                }
            }
            const State pre = s;
            TickInfo ti; Tick(s, c, w, p, &ti); F.now++;
            if (band && ti.jumped)
            {
                if (ti.unducked && !s.edgy && w.floor && w.floorEff == T.z)
                {
                    BFeat J = F; J.userDuck = false;
                    res.push_back({ s, J, dep, "jumpbug" });
                }
                return;
            }
            if (s.edgy) return;
            if (i == 0 && srcGround && press && !ti.jumped) return;
            const bool planDuck = (sc == SC_LONG && i <= 1) || (sc == SC_MINI && i == 0) || sc == SC_WALKOFFDUCK;
            if (planDuck && ti.duckStripped) return;
            if (!s.ground) { if (ti.moveVz < 0.f) falling = left = true; else if (ti.moveVz > 0.f) falling = false; }
            if (walkTick) left = true;
            const bool ok = s.ducked ? T.allowDuck : T.allowStand;
            const bool onT = s.ground && w.floor && w.floorEff == T.z;
            if (T.type == PT_SURF && !s.ground && ti.moveVz < 0.f)
            {
                const float d = T.z - s.z;
                if (d > kSurfLo && d < kSurfHi)
                {
                    const bool miniOnly = s.fDuck && F.mjHoldDuck && !F.psAirDuckHeld && !F.userDuck;
                    if (ok && !miniOnly)
                    {
                        State a = s; a.vz = BHalf(p);
                        BFeat A = F; A.onSeam = true;
                        res.push_back({ a, A, dep, "pixelsurf" + stance(s) });
                    }
                    return;
                }
            }
            if (T.type == PT_HEADBANG && ti.ceilHit)
            {
                if (ok) res.push_back({ s, F, dep, "headbang" + stance(s) });
                return;
            }
            if (T.type == PT_EDGEBUG && onT)
            {
                if (ti.moveVz < 0.f && ok)
                {
                    State a = s; a.ground = false; a.vz = BHalf(p); a.airDucked = pre.airDucked;
                    if (!ti.landHit) a.tick += 1;
                    res.push_back({ a, F, dep, "edgebug" + stance(s) });
                }
                return;
            }
            // texturebug: the hull's side spans the wall seam, the two-plane clip zeroes vz and
            // the move leaves z where the tick started. Every straddling tick is its own catch,
            // so the arc is NOT stopped here -- it flies on and can be caught lower down
            if (T.type == PT_TEXTUREBUG && !s.ground && !ti.duckedNow && !ti.unducked)
            {
                const float hull = (pre.ducked ? 62.f : 82.f) * p.scale;
                if (pre.z + kDistEps < T.z && pre.z + hull - kDistEps > T.z)
                {
                    if (ok)
                    {
                        State a = s; a.z = pre.z; a.vz = BHalf(p); a.ground = false;
                        BFeat A = F; A.onSeam = true;
                        res.push_back({ a, A, dep, "texturebug" + stance(s)
                                        + (tbSeen ? " +" + std::to_string(tbSeen) + " ticks" : std::string()) });
                        // (the solver charges +tbSeen to the cost so the earliest catch of each
                        // press sorts first; cost is not part of what this brute force compares)
                    }
                    tbSeen++;
                }
            }
            if (T.type == PT_GROUND && onT)
            {
                if (!useJb && !(walk && srcGround && std::fabs(s.z - depZ) < 0.01f))
                    res.push_back({ s, F, dep, last ? "land" + stance(s) : "" });
                return;
            }
            if (s.ground) return;
            // a texturebug is caught anywhere along the hull's side, so the band is a standing
            // hull tall -- cut later than the 60u the other types need (still looser than the
            // solver's own bound, which is what an independent check has to be)
            if (falling && s.z < T.z - (T.type == PT_TEXTUREBUG ? 100.f : 60.f)) return;
        }
    };

    auto plansFrom = [&](const State& s, const BFeat& F, std::vector<Out>& res)
    {
        for (int v = 0; v < 2; v++)
        {
            const bool useJb = v == 1;
            if (useJb ? !jb : !land) continue;
            if (!s.ground) { fly(SC_FALL, useJb, s, F, res); continue; }
            const int scs[] = { SC_JUMP, SC_MINI, SC_LONG, SC_WALKOFF, SC_WALKOFFDUCK };
            for (int sc : scs)
            {
                if (!(allowed & (1u << ScriptBit(sc)))) continue;
                if ((sc == SC_WALKOFF || sc == SC_WALKOFFDUCK) && layer != 0) continue;
                fly(sc, useJb, s, F, res);
            }
        }
    };

    if (!from.f.onSeam)
    {
        std::vector<Out> res;
        plansFrom(from.s, from.f, res);
        for (const Out& o : res) out.push_back({ o.s, o.f, join(join(from.text, o.dep), o.arr) });
        return;
    }
    // off a pixelsurf: ride r cmds on the seam (z held, vz on -halfgrav), every r to 17; print an
    // outcome with the shortest ride that reaches it
    struct Got { Out o; int r; };
    std::vector<Got> all;
    for (int r = 0; r <= 17; r++)
    {
        State s = from.s; BFeat F = from.f; F.onSeam = false;
        for (int k = 0; k < r; k++)
        {
            // one ride cmd: PixelSurf's bookkeeping on a -halfgrav pre-velocity
            const bool surfVel = true;
            F.psRidingNow = surfVel && F.prevSurfVel;
            F.prevSurfVel = surfVel;
            if (F.shouldPixelSurf && F.now > F.pixelSurfTicks && !BSurfVel(BHalf(p), p)) F.shouldPixelSurf = false;
            s.tick++; F.now++;
            if (s.duckTime > 0.f) s.duckTime = std::max(0.f, s.duckTime - 1000.f * p.dt);
            s.oldJump = false;
            s.oldDuck = F.userDuck || F.psAirDuckHeld || F.mjHoldDuck;
        }
        std::vector<Out> res;
        plansFrom(s, F, res);
        for (const Out& o : res) all.push_back({ o, r });
    }
    std::vector<char> gone(all.size(), 0);
    auto same = [&](const Got& a, const Got& b)
    {
        return a.o.dep == b.o.dep && a.o.arr == b.o.arr && a.o.s.z == b.o.s.z && a.o.s.vz == b.o.s.vz
            && a.o.s.ground == b.o.s.ground && a.o.s.fDuck == b.o.s.fDuck && a.o.s.ducked == b.o.s.ducked
            && a.o.s.ducking == b.o.s.ducking && a.o.s.airDucked == b.o.s.airDucked && a.o.s.oldDuck == b.o.s.oldDuck
            && a.o.s.oldJump == b.o.s.oldJump
            && a.o.f.psAirDuckHeld == b.o.f.psAirDuckHeld && a.o.f.shouldPixelSurf == b.o.f.shouldPixelSurf
            && a.o.f.userDuck == b.o.f.userDuck && a.o.f.mjHoldDuck == b.o.f.mjHoldDuck
            && a.o.f.psRidingNow == b.o.f.psRidingNow && a.o.f.prevSurfVel == b.o.f.prevSurfVel
            && a.o.f.psShouldDuck == b.o.f.psShouldDuck && a.o.f.onSeam == b.o.f.onSeam
            && (!a.o.f.shouldPixelSurf || std::max(a.o.f.pixelSurfTicks - a.o.f.now, -1) == std::max(b.o.f.pixelSurfTicks - b.o.f.now, -1));
    };
    for (size_t a = 0; a < all.size(); a++)
    {
        if (gone[a]) continue;
        size_t best = a;
        for (size_t b = a + 1; b < all.size(); b++)
            if (!gone[b] && same(all[a], all[b])) { gone[b] = 1; if (all[b].r < all[best].r) best = b; }
        const Got& g = all[best];
        std::string dep = g.o.dep;
        if (g.r > 0) dep = (dep.empty() ? "" : dep + ", ") + "ride " + std::to_string(g.r) + "+ ticks";
        out.push_back({ g.o.s, g.o.f, join(join(from.text, dep), g.o.arr) });
    }
}

static std::vector<BPath> Brute(const std::vector<Point>& pts, const Start& st, unsigned gmoves, const Params& p = Params())
{
    BFeat F;
    F.userDuck = (st.s.hold & 1) != 0;
    F.psAirDuckHeld = (st.s.hold & 2) != 0;
    F.mjHoldDuck = (st.s.hold & 4) != 0;
    if (st.s.psAge < 17)
    {
        F.shouldPixelSurf = true;
        F.psPinTick = F.now - st.s.psAge - 1;
        F.pixelSurfTicks = F.psPinTick + 16;
    }
    F.psRidingNow = st.s.psRiding;
    F.prevSurfVel = st.s.psPrevSurf;
    std::vector<BPath> cur{ { st.s, F, "" } };
    for (int layer = 0; layer < (int)pts.size() && !cur.empty(); layer++)
    {
        std::vector<BPath> next;
        for (const BPath& b : cur) BruteLeg(p, b, layer, st, pts, gmoves, next);
        cur.swap(next);
    }
    return cur;
}

// press sequences, ride steps dropped: what the panel prints one line for
static std::set<std::string> BruteSet(const std::vector<BPath>& b)
{
    std::set<std::string> s;
    for (const BPath& x : b) s.insert(StripRide(x.text));
    return s;
}
static std::set<std::string> SolvedSet(const Result& r)
{
    std::set<std::string> s;
    for (const Combo& c : r.combos) s.insert(StripRide(c.text));
    return s;
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash must not eat the section that caused it
    const Params p;

    // ---- 1. the float/double order of the two gravity halves ----
    {
        State s; s.vz = 0.f;
        StartGravity(s, p);
        CHECK(s.vz == -6.f);                 // 400 * (double)0.015f rounds back to 6 in float
        State d; d.vz = 268.3281572999747f;
        StartGravity(d, p);
        const float want = (float)((double)268.3281572999747f - 800.0 * 0.5 * (double)0.015f);
        CHECK(d.vz == want);
    }

    // ---- 2. launch tick, straight off CheckJumpButton ----
    {
        Arc j = FlyOnFloor(SC_JUMP, Hover(0.03125f), 0.03125f);
        CHECK(j.ti[0].jumped);
        CHECK(j.st[0].vz == 271.f);                          // -6 +289 -6 (move 277) -6
        NEAR(j.st[0].z, 0.03125f + 0.015f * 277.f, 1e-5);
        Arc l = FlyOnFloor(SC_LONG, Hover(0.03125f), 0.03125f);
        CHECK(l.ti[0].jumped);
        CHECK(l.st[0].vz == 277.f);                          // ducking: SET 289, -6 (move 283) -6
        NEAR(l.st[0].z, 0.03125f + 0.015f * 283.f, 1e-5);
        CHECK(l.ti[1].duckedNow);                            // air FinishDuck, +20
        CHECK(l.ti[2].unducked);                             // LongJump lets go on the 3rd cmd
        CHECK(l.st[2].airDucked == 1);
    }

    // ---- 3. apex heights (dih measured +50.1 jump / +52.2 longjump live) ----
    {
        const float F = 0.03125f;
        NEAR(Apex(FlyOnFloor(SC_JUMP, Hover(F), F), F), 50.04, 0.002);
        NEAR(Apex(FlyOnFloor(SC_LONG, Hover(F), F), F), 52.20, 0.002);
        NEAR(Apex(FlyOnFloor(SC_MINI, Hover(F), F), F), 32.20, 0.002);   // stamped duck, -20 before apex
        // raw engine regressions for the presses the calculator no longer offers
        NEAR(Apex(FlyFn([](int i, const State&, const World&) { Cmd c; c.jump = i == 0; c.duck = i >= 1; return c; },
                        Hover(F), F), F), 70.04, 0.002);                   // crouch jump
        NEAR(Apex(FlyFn([](int i, const State&, const World&) { Cmd c; c.jump = i == 0; c.duck = true; return c; },
                        Hover(F), F), F), 72.20, 0.002);                   // longjump holding duck
        Arc mj = FlyOnFloor(SC_MINI, Hover(F), F);
        CHECK(mj.st[1].fDuck && !mj.ti[1].duckedNow);                     // FL_DUCKING for free
        int unduckTick = -1;
        for (int i = 0; i < (int)mj.ti.size(); i++) if (mj.ti[i].unducked) { unduckTick = i; break; }
        CHECK(unduckTick > 1);
        CHECK(unduckTick >= 0 && mj.st[unduckTick].airDucked == 2);       // two releases: no more ducks
    }

    // ---- 4. bhop launch ladder == the heights he was recorded leaving from ----
    {
        const float recJump[] = { -255.263763f, -254.558777f };
        const float recLong[] = { -255.248749f, -254.528748f };
        for (int pass = 0; pass < 2; pass++)
        {
            const int sc = pass == 0 ? SC_JUMP : SC_LONG;
            State s = Hover(kFloor);
            for (int hop = 0; hop < 3; hop++)
            {
                Arc a = FlyOnFloor(sc, s, kFloor);
                s = a.st.back();
                CHECK(s.ground);
                if (hop < 2) NEAR(s.z, pass == 0 ? recJump[hop] : recLong[hop], 5e-5);
                else         NEAR(s.z, kFloor, 1e-6);            // third hop overshoots the band: hits
            }
        }
    }

    // ---- 5. the recorded catches at the -207.972092 seam (7 of 7 / 0 of 13 / jump 0 of 37) ----
    {
        const float seam = -207.972092f;
        const float ljCaught[] = { -254.528748f, -254.543762f, -254.538757f, -254.533752f, -254.543747f };
        const float ljMissed[] = { -255.248749f, -255.263763f, -255.968750f, -255.973755f, -255.978760f,
                                   -255.253754f, -255.258759f };
        const float jumpAll[]  = { -255.968750f, -255.263763f, -254.558777f, -255.248749f };
        for (float z : ljCaught) CHECK(CatchesSurf(SC_LONG, z, seam));
        for (float z : ljMissed) CHECK(!CatchesSurf(SC_LONG, z, seam));
        for (float z : jumpAll)  CHECK(!CatchesSurf(SC_JUMP, z, seam));
        bool ducked = true;
        CHECK(CatchesSurf(SC_LONG, -254.528748f, seam, &ducked) && !ducked);   // standing catch
    }

    // ---- 6. jumpbug tick in the engine: CanUnduck needs 20u, CategorizePosition takes 2u ----
    {
        const float F = 0.03125f;
        World w; w.floor = true; w.floorEff = F;
        auto Air = [&](float z) { State s; s.z = z; s.vz = -300.f; s.fDuck = s.ducked = true; s.oldDuck = true; return s; };
        Cmd jb; jb.jump = true; jb.duck = false;
        {
            State s = Air(F + 21.f); TickInfo ti;
            Tick(s, jb, w, p, &ti);
            CHECK(ti.unducked && ti.jumped);
            CHECK(s.vz == 271.f);
            NEAR(s.z, F + 1.f + 0.015f * 277.f, 1e-4);
        }
        {
            State s = Air(F + 22.5f); TickInfo ti;
            Tick(s, jb, w, p, &ti);
            CHECK(ti.unducked && !ti.jumped);                  // dropped 20 but still 2.5 up
        }
        {
            State s = Air(F + 19.9f); TickInfo ti;
            Tick(s, jb, w, p, &ti);
            CHECK(!ti.unducked && !ti.jumped && s.fDuck);      // no room to unduck
        }
    }

    // ---- 6b. AutoJumpbug's buttons (movement.cpp:868): arm 22 + max(3 ticks of fall, 8), band [20, 22) ----
    {
        World w; w.floor = true; w.floorEff = 0.f;
        auto At = [&](float z, float vz, bool duck) { State s; s.z = z; s.vz = vz; s.fDuck = s.ducked = duck; return s; };
        Cmd c; bool band = false; float mg = 0.f;
        CHECK(!JumpbugCmd(At(36.f, -300.f, false), w, p, c, band, mg));         // arm = 22 + 13.5 = 35.5
        c = Cmd(); CHECK(JumpbugCmd(At(35.f, -300.f, false), w, p, c, band, mg) && c.duck && !c.jump && !band);
        c = Cmd(); CHECK(!JumpbugCmd(At(30.5f, -100.f, false), w, p, c, band, mg));   // slow: arm = 22 + 8
        c = Cmd(); CHECK(JumpbugCmd(At(21.f, -300.f, true), w, p, c, band, mg) && band && c.jump && !c.duck);
        NEAR(mg, 1.0, 1e-6);
        c = Cmd(); CHECK(JumpbugCmd(At(20.f, -300.f, true), w, p, c, band, mg) && band);         // 20 is in
        c = Cmd(); CHECK(JumpbugCmd(At(22.f, -300.f, true), w, p, c, band, mg) && !band && c.duck); // 22 is out
        c = Cmd(); CHECK(JumpbugCmd(At(19.9f, -300.f, true), w, p, c, band, mg) && !band && c.duck);
        c = Cmd(); CHECK(JumpbugCmd(At(21.f, -300.f, false), w, p, c, band, mg) && !band && c.duck); // not ducked yet
        c = Cmd(); CHECK(JumpbugCmd(At(55.f, -300.f, true), w, p, c, band, mg) && c.duck && !band);  // ducked: arm + 20
        c = Cmd(); CHECK(!JumpbugCmd(At(56.f, -300.f, true), w, p, c, band, mg));
        c = Cmd(); CHECK(!JumpbugCmd(At(21.f, 10.f, true), w, p, c, band, mg));                   // rising
        State g = At(21.f, -300.f, true); g.ground = true;
        c = Cmd(); CHECK(!JumpbugCmd(g, w, p, c, band, mg));                                       // grounded
        World none;
        c = Cmd(); CHECK(!JumpbugCmd(At(21.f, -300.f, true), none, p, c, band, mg));              // no floor
        Params big = p; big.scale = 1.5f;                                                          // 30u unduck
        c = Cmd(); CHECK(JumpbugCmd(At(31.f, -300.f, true), w, big, c, band, mg) && band);
    }

    // ---- 7. duck timer: a grounded duck inside 0.3s of a release is eaten ----
    {
        State s = Hover(0.03125f); s.tick = 100; s.releaseTick = 90;
        World w; w.floor = true; w.floorEff = 0.03125f;
        ScriptMem m;
        Cmd c = ScriptCmd(SC_LONG, 0, s, m, w, p);
        TickInfo ti;
        Tick(s, c, w, p, &ti);
        CHECK(ti.duckStripped && ti.jumped && s.vz == 271.f);  // the standing branch, not a longjump
        State e = Hover(0.03125f); e.tick = 110; e.releaseTick = 90;
        ScriptMem m2; Cmd c2 = ScriptCmd(SC_LONG, 0, e, m2, w, p); TickInfo t2;
        Tick(e, c2, w, p, &t2);
        CHECK(t2.duckStripped && e.edgy);                      // exactly 0.3s: float boundary, flagged
        State f = Hover(0.03125f); f.tick = 111; f.releaseTick = 90;
        ScriptMem m3; Cmd c3 = ScriptCmd(SC_LONG, 0, f, m3, w, p); TickInfo t3;
        Tick(f, c3, w, p, &t3);
        CHECK(!t3.duckStripped && f.vz == 277.f);
    }

    // ---- 7b. ducked start: every jump is refused (FL_DUCKING), a crouch walk off is not ----
    {
        const float F = 0.03125f;
        World w; w.floor = true; w.floorEff = F;
        State s = Hover(F); s.fDuck = s.ducked = true; s.oldDuck = true;
        Cmd j; j.jump = true; j.duck = true;
        TickInfo ti;
        Tick(s, j, w, p, &ti);
        CHECK(!ti.jumped && s.oldJump);                        // refused, and latched anyway
        std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = F - 100.f;
        Start st; st.s = Hover(F); st.s.fDuck = st.s.ducked = true; st.s.oldDuck = true; st.floorEff = F;
        Options o; o.topK = 50;
        const Result r = Solve(pts, st, o);
        CHECK(Has(r, "crouch walk off -> land (ducked)") && Has(r, "crouch walk off -> jumpbug"));
        CHECK(!Has(r, "walk off -> land (stand)"));                 // a ducked hull cannot walk off standing
        // Bunnyhop stands you up first (0.2s), so the jump goes; the longjump / minijump duck lands
        // inside the 0.3s duck timer of that release and is eaten
        CHECK(Has(r, "jump -> land (stand)"));
        CHECK(!HasSub(r, "longjump") && !HasSub(r, "minijump"));
    }

    // ---- 8. headbang: the head stops 1/32 under the ceiling, vz ends on -halfgrav ----
    {
        const float F = 0.03125f, C = F + 82.f + 30.f;
        State s = Hover(F); ScriptMem m;
        World w; w.floor = true; w.floorEff = F; w.ceil = true; w.ceilEff = C;
        bool hit = false;
        for (int i = 0; i < 60 && !hit; i++)
        {
            Cmd c = ScriptCmd(SC_JUMP, i, s, m, w, p);
            TickInfo ti; Tick(s, c, w, p, &ti);
            if (ti.ceilHit) { hit = true; NEAR(s.z + 82.f, C, 1e-4); CHECK(s.vz == -6.f); }
        }
        CHECK(hit);
        std::vector<Point> pts(1); pts[0].type = PT_HEADBANG; pts[0].z = C;
        Start st; st.s = Hover(F); st.floorEff = F;
        Options o; o.topK = 50;
        const Result r = Solve(pts, st, o);
        CHECK(Has(r, "jump -> headbang (stand)") && Has(r, "longjump -> headbang (stand)"));
    }

    // ---- 9. walk off: a grounded tick with no floor, then free fall ----
    {
        State s = Hover(0.03125f); World none; TickInfo ti;
        Tick(s, Cmd(), none, p, &ti);
        CHECK(!s.ground && s.vz == -6.f);
        NEAR(s.z, 0.03125f, 1e-6);
        Tick(s, Cmd(), none, p, &ti);
        CHECK(s.vz == -18.f);
        NEAR(s.z, 0.03125f - 0.18f, 1e-5);
    }

    // ---- 9b. walk off / crouch walk off from a standing start: stance kept to the landing ----
    {
        const float F = 0.03125f;
        std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = F - 64.f;
        Start st; st.s = Hover(F); st.floorEff = F;
        Options o; o.topK = 50;
        const Result r = Solve(pts, st, o);
        CHECK(Has(r, "walk off -> land (stand)"));
        CHECK(Has(r, "crouch walk off -> land (ducked)"));
        CHECK(Has(r, "jump -> land (stand)") && Has(r, "longjump -> land (stand)"));
        o.moves = kMovesPress | (1u << MB_JUMPBUG);            // walk offs switched off
        const Result r2 = Solve(pts, st, o);
        CHECK(!HasSub(r2, "walk off"));
        std::vector<Point> same(1); same[0].type = PT_GROUND; same[0].z = F;
        o.moves = kMovesAll;
        const Result r3 = Solve(same, st, o);
        CHECK(!HasSub(r3, "walk") && Has(r3, "jump -> land (stand)"));      // a walk that never falls is no move
    }

    // ---- 10. solver: the seam he rides answers "longjump", never "jump" ----
    {
        std::vector<Point> pts(1);
        pts[0].type = PT_SURF; pts[0].z = -207.972092f;
        Start st; st.s = Hover(-254.528748f); st.floorEff = kFloor;
        Options o; o.moves = kMovesPress; o.topK = 100;
        const Result r = Solve(pts, st, o);
        CHECK(Has(r, "longjump -> pixelsurf (stand)"));
        CHECK(!Has(r, "jump -> pixelsurf (stand)"));
        CHECK(r.total == (double)r.combos.size());
    }

    // ---- 11. jumpbug sweeps: walk off / crouch walk off / jump / longjump / minijump into a floor
    //          below, 400 depths. Solver == a plain tick loop with AutoJumpbug's buttons, and the
    //          band has to come up for some depths and miss for others ----
    {
        const float F = 0.03125f;
        const int scs[] = { SC_WALKOFF, SC_WALKOFFDUCK, SC_JUMP, SC_LONG, SC_MINI };
        int fired[5] = {}, agree = 0, runs = 0;
        for (int k = 0; k < 5; k++)
            for (int d = 0; d < 80; d++)
            {
                const float Tz = F - 30.f - (float)d * 1.37f;
                // plain loop: same buttons the solver would press, no DAG
                State s = Hover(F);
                bool ok = true;
                if (scs[k] == SC_WALKOFFDUCK)
                {
                    World g; g.floor = true; g.floorEff = F;
                    for (int t = 0; t < 40 && !(s.fDuck && !s.ducking); t++) { Cmd c; c.duck = true; Tick(s, c, g, p); }
                }
                ScriptMem m; bool falling = false, bug = false;
                for (int i = 0; i < 400 && ok; i++)
                {
                    World w;
                    if (!falling) { w.floor = true; w.floorEff = F; }
                    else          { w.floor = true; w.floorEff = Tz; }
                    const bool walk = scs[k] == SC_WALKOFF || scs[k] == SC_WALKOFFDUCK;
                    if (walk && i == 0) w.floor = false;
                    Cmd c = ScriptCmd(scs[k], i, s, m, w, p);
                    bool band = false; float mg;
                    if (JumpbugCmd(s, w, p, c, band, mg)) m.mjHold = false;
                    TickInfo ti; Tick(s, c, w, p, &ti);
                    if (band && ti.jumped) { bug = ti.unducked && w.floorEff == Tz; break; }
                    if ((c.duck && ti.duckStripped) || s.ground) break;
                    if (ti.moveVz < 0.f || (walk && i == 0)) falling = true;
                }
                std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = Tz; pts[0].moves = 1u << MB_JUMPBUG;
                Start st; st.s = Hover(F); st.floorEff = F;
                Options o; o.moves = 1u << ScriptBit(scs[k]) | (1u << MB_JUMPBUG); o.topK = 10;
                const Result r = Solve(pts, st, o);
                const bool solved = r.total > 0.0;
                runs++;
                if (solved == bug) agree++;
                else std::printf("  jumpbug sweep mismatch: %s to %.3f solver %d loop %d\n", MoveName(ScriptBit(scs[k])), Tz, solved, bug);
                if (bug) fired[k]++;
                if (solved) CHECK(r.combos[0].text == std::string(MoveName(ScriptBit(scs[k]))) + " -> jumpbug");
            }
        CHECK(agree == runs);
        for (int k = 0; k < 4; k++) CHECK(fired[k] > 5 && fired[k] < 75);   // the band is a coin, not a given
        CHECK(fired[4] == 0);   // minijump released its duck twice rising: the jumpbug's duck is stripped
        std::printf("  jumpbug sweep: fired walk off %d, crouch walk off %d, jump %d, longjump %d, minijump %d of 80\n",
                    fired[0], fired[1], fired[2], fired[3], fired[4]);
    }

    // ---- 11b. jumpbug is not a landing, a landing is not a jumpbug: per-marker moves pick ----
    {
        const float F = 0.03125f;
        // find a floor the walk-off jumpbug hits
        float Tz = 0.f;
        for (int d = 0; d < 200 && Tz == 0.f; d++)
        {
            std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = F - 40.f - d * 0.9f;
            Start st; st.s = Hover(F); st.floorEff = F;
            Options o; o.moves = (1u << MB_WALKOFF) | (1u << MB_JUMPBUG);
            if (Solve(pts, st, o).total > 0.0) Tz = pts[0].z;
        }
        CHECK(Tz != 0.f);
        std::vector<Point> pts(2);
        pts[0].type = PT_GROUND; pts[0].z = Tz;
        pts[1].type = PT_GROUND; pts[1].z = Tz;
        Start st; st.s = Hover(F); st.floorEff = F;
        Options o; o.topK = 200;
        pts[0].moves = 1u << MB_JUMPBUG;
        Result r = Solve(pts, st, o);
        CHECK(Has(r, "walk off -> jumpbug -> land (stand)"));
        CHECK(!Has(r, "walk off -> jump -> land (stand)"));            // no landing on marker 1
        pts[0].moves = 1u << MB_JUMP;
        r = Solve(pts, st, o);
        CHECK(Has(r, "walk off -> jump -> land (stand)"));
        CHECK(!HasSub(r, "walk off -> jumpbug"));                     // marker 1 cannot be jumpbugged
        pts[1].moves = 1u << MB_JUMPBUG;                                // last marker: jumpbug only
        r = Solve(pts, st, o);
        CHECK(!HasSub(r, "land"));
    }

    // ---- 12. solver == brute force: 400 random routes over the whole vocabulary ----
    {
        std::mt19937 rng(18);
        std::uniform_real_distribution<float> U(0.f, 1.f);
        int nonEmpty = 0, match = 0, n = 0;
        double totalCombos = 0.0;
        for (int it = 0; it < 1500; it++)
        {
            const float F = 0.03125f + std::floor(U(rng) * 64.f) * 0.5f;
            const int len = 1 + (int)(U(rng) * 3.f);
            std::vector<Point> pts(len);
            float base = F;
            for (int i = 0; i < len; i++)
            {
                const float r = U(rng);
                pts[i].type = r < 0.42f ? PT_GROUND : r < 0.70f ? PT_SURF : r < 0.84f ? PT_EDGEBUG
                            : r < 0.92f ? PT_HEADBANG : PT_TEXTUREBUG;
                // A seam is caught on EVERY tick the hull spans it and then the arc flies on, so
                // one texturebug layer fans a path out ~10 ways and the ride after it multiplies
                // that by up to 18. The SOLVER merges the states back together; this brute force
                // materialises every path by design, and three stacked seams is tens of millions
                // of them. Keep it to short routes here -- sections 23-24 check seams properly,
                // against this same brute force, where the numbers stay countable.
                if (pts[i].type == PT_TEXTUREBUG && len > 2) pts[i].type = PT_HEADBANG;
                pts[i].allowDuck = U(rng) < 0.8f;
                pts[i].allowStand = !pts[i].allowDuck || U(rng) < 0.8f;
                if (pts[i].type == PT_GROUND)
                {
                    pts[i].z = base - 80.f + U(rng) * 110.f;
                    pts[i].moves = 1u + (unsigned)(U(rng) * 15.f);
                    if (U(rng) < 0.5f) pts[i].moves = kMovesMarker;
                    pts[i].player = U(rng) < 0.15f;
                    pts[i].building = !pts[i].player && U(rng) < 0.15f;
                    base = pts[i].z;
                }
                else if (pts[i].type == PT_SURF)    pts[i].z = base - 60.f + U(rng) * 110.f;
                else if (pts[i].type == PT_EDGEBUG) pts[i].z = base - 80.f + U(rng) * 110.f;
                // a seam is caught anywhere on the hull's SIDE, so its band sits over the feet
                else if (pts[i].type == PT_TEXTUREBUG) pts[i].z = base - 20.f + U(rng) * 130.f;
                else                                pts[i].z = base + 90.f + U(rng) * 60.f;
            }
            Start st; st.s = Hover(F); st.floorEff = F;
            if (U(rng) < 0.2f) { st.s.fDuck = st.s.ducked = true; st.s.oldDuck = true; }
            st.building = U(rng) < 0.2f;
            if (U(rng) < 0.2f)
            {
                // mid-air start: any duck owner, any PixelSurf latch age / ride memory
                st.air = true; st.hasFloor = false; st.building = false;
                st.s.ground = false; st.s.vz = -300.f * U(rng) + 100.f;
                st.s.hold = (int)(U(rng) * 8.f) & 7;
                if (st.s.hold) { st.s.fDuck = st.s.ducked = true; st.s.oldDuck = true; }
                st.s.psAge = U(rng) < 0.5f ? 17 : (int)(U(rng) * 20.f) - 3;
                if (st.s.psAge > 17) st.s.psAge = 17;
                st.s.psRiding = U(rng) < 0.3f; st.s.psPrevSurf = U(rng) < 0.5f;
                st.s.airDucked = (int)(U(rng) * 2.f);
            }
            Options o; o.topK = 100000;
            o.moves = U(rng) < 0.5f ? kMovesAll : 1u + (unsigned)(U(rng) * 62.f);
            const Result r = Solve(pts, st, o);
            const std::vector<BPath> b = Brute(pts, st, o.moves);
            std::set<std::string> bs = BruteSet(b), ss = SolvedSet(r);
            n++;
            if (bs == ss && r.total == (double)bs.size()) match++;
            else if (n - match <= 3)
            {
                std::printf("  brute mismatch on route %d (%zu brute, %.0f solver)\n", it, b.size(), r.total);
                for (auto& s : bs) if (!ss.count(s)) std::printf("    brute only:  %s\n", s.c_str());
                for (auto& s : ss) if (!bs.count(s)) std::printf("    solver only: %s\n", s.c_str());
            }
            if (!b.empty()) nonEmpty++;
            totalCombos += (double)bs.size();
        }
        CHECK(match == n);
        CHECK(nonEmpty > 400);
        std::printf("  brute force: %d of %d random routes identical, %d non-empty, %.0f combinations\n",
                    match, n, nonEmpty, totalCombos);
    }

    // ---- 13. his saved hightower route: ground, ground, seam -- the chain he recorded catching ----
    {
        // C:\dih\routes\maps_plr_hightower.bsp.txt: two floor markers at -255.968750 then the seam,
        // started from where he stands. The longjump caught only off the SECOND bhop hover (+1.44),
        // so from a standing start "longjump -> longjump -> longjump -> pixelsurf" must be there.
        std::vector<Point> pts(3);
        pts[0].type = PT_GROUND; pts[0].z = kFloor;
        pts[1].type = PT_GROUND; pts[1].z = kFloor;
        pts[2].type = PT_SURF;   pts[2].z = -207.972092f;
        Start st; st.s = Hover(kFloor); st.floorEff = kFloor;
        Options o; o.topK = 200;
        const auto t0 = std::chrono::steady_clock::now();
        const Result r = Solve(pts, st, o);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(Has(r, "longjump -> longjump -> longjump -> pixelsurf (stand)"));
        std::printf("  hightower [ground, ground, seam]: %.0f combination(s), %lld ticks, %.2f ms, layers",
                    r.total, r.ticks, ms);
        for (size_t n : r.layerNodes) std::printf(" %zu", n);
        std::printf("\n");
        for (size_t i = 0; i < r.combos.size() && i < 8; i++) std::printf("    %s\n", r.combos[i].text.c_str());
    }

    // ---- 14. his saved tr_walkway route: ground, ground, seam 133u below ----
    {
        std::vector<Point> pts(3);
        pts[0].type = PT_GROUND; pts[0].z = 76.03125f;
        pts[1].type = PT_GROUND; pts[1].z = 76.03125f;
        pts[2].type = PT_SURF;   pts[2].z = -56.972092f;
        Start st; st.s = Hover(76.03125f); st.floorEff = 76.03125f;
        Options o; o.topK = 400;
        const Result r = Solve(pts, st, o);
        const std::vector<BPath> b = Brute(pts, st, o.moves);
        CHECK(r.total == (double)BruteSet(b).size() && SolvedSet(r) == BruteSet(b));
        // dihrc17 listed these 8 (and 24 more through presses that are gone): PixelSurf ducks
        // onto this seam, it never catches standing
        CHECK(Has(r, "longjump -> longjump -> longjump -> pixelsurf (ducked)"));
        CHECK(Has(r, "jump -> jump -> longjump -> pixelsurf (ducked)"));
        CHECK(!HasSub(r, "pixelsurf (stand)"));
        std::printf("  walkway [ground, ground, seam]: %.0f combination(s)\n", r.total);
        for (size_t i = 0; i < r.combos.size() && i < 4; i++) std::printf("    %s\n", r.combos[i].text.c_str());
    }

    // ---- 15. speed: the worst case is a long bhop chain on one floor -- every landing height is a
    //          new state -- then the seam. Budget: 100 ms offline (in game it runs in chunks) ----
    {
        std::vector<Point> pts(10);
        for (int i = 0; i < 9; i++) { pts[i].type = PT_GROUND; pts[i].z = kFloor; }
        pts[9].type = PT_SURF; pts[9].z = -207.972092f;
        Start st; st.s = Hover(kFloor); st.floorEff = kFloor;
        Options o; o.topK = 32;
        const auto t0 = std::chrono::steady_clock::now();
        const Result r = Solve(pts, st, o);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms < 100.0);
        CHECK(r.total > 0.0 && !r.capped);
        std::printf("  10-marker bhop chain: %.0f combination(s), %lld ticks, %.2f ms, dead %d, layers",
                    r.total, r.ticks, ms, r.deadPoint);
        for (size_t n : r.layerNodes) std::printf(" %zu", n);
        std::printf("\n");
        for (size_t i = 0; i < r.combos.size() && i < 4; i++) std::printf("    %s\n", r.combos[i].text.c_str());
    }

    // ---- 16. three pixelsurfs in one route: each ride's fall starts from the last catch ----
    {
        const float F = 0.03125f;
        // build the seams off the same fall the solver will fly: walk off, catch A at fall tick 10,
        // then from A's ride (vz -halfgrav) catch B 14 ticks down, then C 9 ticks under that
        State s = Hover(F);
        World none;
        std::vector<float> seams;
        const int waits[] = { 10, 14, 9 };
        Tick(s, Cmd(), none, p);                              // the walk-off tick
        for (int k = 0; k < 3; k++)
        {
            for (int t = 0; t < waits[k]; t++) Tick(s, Cmd(), none, p);
            seams.push_back(s.z + 0.01f);
            s.vz = -6.f;                                      // caught: the ride leaves on -halfgrav
        }
        std::vector<Point> pts(3);
        for (int k = 0; k < 3; k++) { pts[k].type = PT_SURF; pts[k].z = seams[k]; }
        Start st; st.s = Hover(F); st.floorEff = F;
        Options o; o.topK = 500;
        const Result r = Solve(pts, st, o);
        CHECK(Has(r, "walk off -> pixelsurf (stand) -> pixelsurf (stand) -> pixelsurf (stand)"));
        const std::vector<BPath> b = Brute(pts, st, o.moves);
        CHECK(r.total == (double)BruteSet(b).size() && SolvedSet(r) == BruteSet(b));
        // a ducked catch is held through the next rides (PixelSurf never lets go in the air)
        bool heldAll = false;
        for (const Combo& c : r.combos)
            if (c.text.find("pixelsurf (ducked) -> pixelsurf (ducked) -> pixelsurf (ducked)") != std::string::npos) heldAll = true;
        CHECK(!HasSub(r, "pixelsurf (ducked) -> pixelsurf (stand)"));
        std::printf("  3 pixelsurfs: %.0f combination(s)%s\n", r.total, heldAll ? ", ducked chain present" : "");
        for (size_t i = 0; i < r.combos.size() && i < 4; i++) std::printf("    %s\n", r.combos[i].text.c_str());
    }

    // ---- 17. off a building, off a player's head: a floor to the engine, only the name differs ----
    {
        const float F = 0.03125f;
        std::vector<Point> pts(2);
        pts[0].type = PT_GROUND; pts[0].z = F - 30.f; pts[0].player = true;
        pts[1].type = PT_GROUND; pts[1].z = F - 60.f;
        Start st; st.s = Hover(F); st.floorEff = F; st.building = true;
        Options o; o.topK = 500;
        const Result r = Solve(pts, st, o);
        CHECK(Has(r, "jump off building -> jump off player -> land (stand)"));
        CHECK(Has(r, "walk off building -> longjump off player -> land (stand)"));
        CHECK(!HasSub(r, "jump -> "));                         // every press names what it left
        Start plain = st; plain.building = false;
        std::vector<Point> flat = pts; flat[0].player = false;
        const Result r2 = Solve(flat, plain, o);
        // same engine, but dih's traces skip players: no AutoJumpbug onto a head
        CHECK(r2.total >= r.total && !HasSub(r, "building -> jumpbug") && HasSub(r2, "jump -> jumpbug"));
        CHECK(SolvedSet(r) == BruteSet(Brute(pts, st, o.moves)));
        CHECK(Has(r2, "jump -> jump -> land (stand)"));
    }

    // ---- 18. marker file lines: rc18 round trip, dihrc17 / dihrc16 migration ----
    {
        MarkerLine m;
        CHECK(ParseMarkerLine("1 2 3.5 0 5 1 0 1 1 0 1\n", true, m));
        CHECK(m.type == PT_GROUND && m.moves == 5u && m.building && !m.player && m.z == 3.5f);
        CHECK(ParseMarkerLine("1 2 3 3 15 0 1 1 0 1\n", true, m) && m.type == PT_HEADBANG && !m.enabled && m.player);
        CHECK(!ParseMarkerLine("rc18\n", true, m));
        // dihrc17: type 4 jumpbug -> ground, jumpbug only; type 5 pixelbounce ducked only -> jumpbug
        CHECK(ParseMarkerLine("0 0 0 4 8191 1 0 1 1 0\n", false, m) && m.type == PT_GROUND && m.moves == (1u << MB_JUMPBUG));
        CHECK(ParseMarkerLine("0 0 0 5 8191 1 0 0 1 0\n", false, m) && m.type == PT_GROUND && m.moves == (1u << MB_JUMPBUG));
        CHECK(ParseMarkerLine("0 0 0 5 8191 1 0 1 0 0\n", false, m) && m.moves == (1u << MB_JUMP));
        // dihrc17 ground, only "longjump (hold duck)" on: longjump kept, jumpbug default on
        CHECK(ParseMarkerLine("0 0 0 0 32 1 0 1 1 0\n", false, m) && m.moves == ((1u << MB_LONG) | (1u << MB_JUMPBUG)));
        // dihrc16 line (8 fields), jump + minijump only
        CHECK(ParseMarkerLine("0 0 0 0 5 1 0 1\n", false, m) && m.moves == ((1u << MB_JUMP) | (1u << MB_MINI) | (1u << MB_JUMPBUG)));
        CHECK(ParseMarkerLine("0 0 0\n", false, m) && m.moves == kMovesMarker && m.type == PT_GROUND);
        CHECK(ParseMarkerLine("0 0 0 1 3 1 1 0 0 0\n", false, m) && m.type == PT_SURF && m.stand && m.duck);   // never both off
    }

    // ---- 19. ducked landing, then a press: Bunnyhop stands up first, only the jump survives ----
    {
        const float F = 0.03125f;
        std::vector<Point> pts(2);
        pts[0].type = PT_GROUND; pts[0].z = F - 64.f; pts[0].moves = kMovesPress;
        pts[1].type = PT_GROUND; pts[1].z = F - 64.f;
        Start st; st.s = Hover(F); st.floorEff = F;
        Options o; o.moves = (1u << MB_CROUCHWALKOFF) | kMovesPress; o.topK = 50;
        const Result r = Solve(pts, st, o);
        CHECK(Has(r, "crouch walk off -> jump -> land (stand)"));
        CHECK(!Has(r, "crouch walk off -> longjump -> land (stand)") && !Has(r, "crouch walk off -> minijump -> land (stand)"));
        // the stand-up takes the engine's 0.2s: the launch is at least 14 ticks after the landing
        State s = Hover(F - 64.f); s.fDuck = s.ducked = true; s.oldDuck = true; s.hold = 1;
        World g; g.floor = true; g.floorEff = F - 64.f;
        int k = 0;
        for (; k < 60 && (s.fDuck || s.ducking); k++) { Cmd c; c.jump = true; Tick(s, c, g, p); }
        CHECK(k >= 14 && k <= 16);
    }

    // ---- 20. PixelSurf's hold after a ducked ride: let go on the cmd the release lands ----
    {
        const float F = 0.03125f;
        // ducked, falling from a ride onto a floor 100 below: some depth sweeps put the release
        // tick's -20 inside the 2u ground band -> lands standing; others touch down ducked
        int stand = 0, ducked = 0;
        for (int d = 0; d < 60; d++)
        {
            std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = F - 100.f - d * 0.37f;
            Start st; st.air = true; st.hasFloor = false;
            st.s.z = F; st.s.vz = -6.f; st.s.fDuck = st.s.ducked = true; st.s.oldDuck = true; st.s.hold = 2;
            st.s.airDucked = 0;
            Options o; o.moves = kMovesPress; o.topK = 10;
            const Result r = Solve(pts, st, o);
            if (Has(r, "land (stand)")) stand++;
            if (Has(r, "land (ducked)")) ducked++;
            CHECK(r.total == 1.0);
            const std::vector<BPath> b = Brute(pts, st, o.moves);
            CHECK(BruteSet(b) == SolvedSet(r));
        }
        CHECK(stand > 0 && ducked == 0);    // a ducked fall always crosses 20..22u: the release lands it
        std::printf("  pixelsurf hold release: %d of 60 depths land standing\n", stand);
    }

    // ---- 21. a ride, then a jumpbug: PixelSurf's hold / latch decide it ----
    {
        const float F = 0.03125f;
        State s = Hover(F); World none;
        Tick(s, Cmd(), none, p);
        for (int t = 0; t < 10; t++) Tick(s, Cmd(), none, p);
        const float seam = s.z + 0.01f;
        int ducked = 0, stand = 0, rideLbl = 0, n = 0;
        for (int d = 0; d < 60; d++)
        {
            std::vector<Point> pts(2);
            pts[0].type = PT_SURF;   pts[0].z = seam;
            pts[1].type = PT_GROUND; pts[1].z = seam - 22.f - d * 1.3f; pts[1].moves = 1u << MB_JUMPBUG;
            Start st; st.s = Hover(F); st.floorEff = F;
            Options o; o.topK = 50;
            const Result r = Solve(pts, st, o);
            const std::vector<BPath> b = Brute(pts, st, o.moves);
            n++;
            CHECK(r.total == (double)BruteSet(b).size() && SolvedSet(r) == BruteSet(b));
            if (HasSub(r, "walk off -> pixelsurf (ducked) -> jumpbug") && !HasSub(r, "crouch walk off -> pixelsurf (ducked) -> jumpbug")) ducked++;
            if (HasSub(r, "jump -> pixelsurf (ducked) -> jumpbug")) ducked++;   // PixelSurf's duck pass: its hold
            if (HasSub(r, "pixelsurf (stand) -> jumpbug")) stand++;
            if (HasSub(r, "pixelsurf (stand) -> ride ")) rideLbl++;
        }
        CHECK(ducked == 0);            // a duck PixelSurf pressed is re-held every cmd until the feet touch
        CHECK(stand > 0);              // standing: AutoJumpbug waits out the latch, then works
        CHECK(rideLbl > 0);            // a short fall needs the ride to have used up the latch
        std::printf("  ride -> jumpbug: %d of %d depths, %d need a longer ride\n", stand, n, rideLbl);
    }

    // ---- 22. a mid-air start keeps whoever holds the duck; nobody holding = it unducks now ----
    {
        const float F = 0.03125f;
        std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = F - 200.f; pts[0].moves = kMovesPress;
        Options o; o.moves = kMovesPress; o.topK = 10;
        for (int h = 0; h < 3; h++)
        {
            Start st; st.air = true; st.hasFloor = false;
            st.s.z = F; st.s.vz = -100.f; st.s.fDuck = st.s.ducked = true; st.s.oldDuck = true; st.s.hold = h;
            const Result r = Solve(pts, st, o);
            CHECK(r.total == 1.0);
            // player hold: lands ducked. PixelSurf: lets go onto the floor. nobody: unducks now
            CHECK(Has(r, h == 1 ? "land (ducked)" : "land (stand)"));
            const std::vector<BPath> b = Brute(pts, st, o.moves);
            CHECK(BruteSet(b) == SolvedSet(r));
        }
    }

    // ---- 23. TEXTUREBUG: a wall seam freezes z where the tick started, vz on -halfgrav ----
    // The whole claim the route calculator makes about a seam, checked against a hand-walked
    // free fall: the catch band is exactly the ticks whose hull SIDE spans the seam, the state
    // it hands on has the feet where that tick began, and the vz is the pin's -halfgrav.
    {
        const float F = 0.03125f;
        // a standing jump apexes +50.04, so its head reaches +132 and its feet +50: a seam at
        // +100 sits inside the hull's side for several ticks up and several more coming down
        const float seam = F + 100.f;
        std::vector<Point> pts(1); pts[0].type = PT_TEXTUREBUG; pts[0].z = seam;
        Options o; o.moves = 1u << MB_JUMP; o.topK = 1000;
        Start st; st.s = Hover(F); st.floorEff = F;
        const Result r = Solve(pts, st, o);

        // hand-walk the same jump and count the straddling ticks + their start heights
        Arc j = FlyOnFloor(SC_JUMP, Hover(F), F);
        std::vector<float> want;                   // the feet z each catch must report
        float prevZ = F;
        for (size_t i = 0; i < j.st.size(); i++)
        {
            const bool ducked = i ? j.st[i - 1].ducked : false;
            const float hull = (ducked ? 62.f : 82.f);
            if (!j.st[i].ground && !j.ti[i].duckedNow && !j.ti[i].unducked
                && prevZ + kDistEps < seam && prevZ + hull - kDistEps > seam)
                want.push_back(prevZ);
            prevZ = j.st[i].z;
        }
        CHECK(want.size() >= 3);                   // a jump really does span it for several ticks
        CHECK(r.total == (double)want.size());
        CHECK(Has(r, "jump -> texturebug (stand)"));
        CHECK(Has(r, "jump -> texturebug (stand) +1 ticks"));
        // a later catch costs more, so the panel leads with the earliest one and the list is
        // ordered: no press ever buries another under fifteen heights of itself
        CHECK(!r.combos.empty() && r.combos[0].text == "jump -> texturebug (stand)");
        for (size_t k = 1; k < r.combos.size(); k++) CHECK(r.combos[k].cost >= r.combos[k - 1].cost);
        // the feet never move on a pin: the seam is always inside the hull it froze
        for (float z : want) { CHECK(z < seam); CHECK(z + 82.f > seam); }
        CHECK(SolvedSet(r) == BruteSet(Brute(pts, st, o.moves)));

        // a seam over the standing head is unreachable, and the miss is reported by the HEAD
        std::vector<Point> far(1); far[0].type = PT_TEXTUREBUG; far[0].z = F + 400.f;
        const Result rf = Solve(far, st, o);
        CHECK(rf.total == 0.0 && rf.deadPoint == 0);
        CHECK(rf.highest[0] < 0.f);                                  // "nothing gets the head to it"
        NEAR(rf.highest[0], (F + 50.04f + 82.f) - (F + 400.f), 1.5);  // apex head, 82 over the feet
    }

    // ---- 24. texturebug stance filter + the ride out of one ----
    {
        const float F = 0.03125f;
        const float seam = F + 100.f;   // inside a standing hull AND a ducked (62u) one up there
        Start st; st.s = Hover(F); st.floorEff = F;

        // a longjump ducks in the air, so the same seam is met standing on the way up and
        // ducked later: "ducked only" must keep the ducked catches and drop the standing ones
        for (int mode = 0; mode < 3; mode++)
        {
            std::vector<Point> pts(1);
            pts[0].type = PT_TEXTUREBUG; pts[0].z = seam;
            pts[0].allowStand = mode != 1;
            pts[0].allowDuck  = mode != 0;
            Options o; o.moves = (1u << MB_JUMP) | (1u << MB_LONG) | (1u << MB_MINI); o.topK = 4000;
            const Result r = Solve(pts, st, o);
            CHECK(SolvedSet(r) == BruteSet(Brute(pts, st, o.moves)));
            for (const Combo& c : r.combos)
            {
                if (mode == 0) CHECK(c.text.find("(ducked)") == std::string::npos);
                if (mode == 1) CHECK(c.text.find("(stand)")  == std::string::npos);
            }
            if (mode == 2) CHECK(r.total > 0.0);
        }

        // a catch is a seam catch: it RIDES like a pixelsurf, so a second marker under it is
        // reached from the frozen height, and where the ride length changes the outcome the
        // step is labelled with it. A minijump is the case that shows it: it holds duck across
        // the catch (State::hold bit 4), so Ride's `oldDuck = hold != 0` makes r == 0 and r >= 1
        // land in different states. This also walks RideText with a seam that is NOT a pixelsurf,
        // which indexed an empty vector before -- the crash section 12 hit on route 42
        std::vector<Point> pts(2);
        pts[0].type = PT_TEXTUREBUG; pts[0].z = F + 60.f;   // inside a minijump's ducked hull
        pts[1].type = PT_GROUND;     pts[1].z = F;
        Options o; o.topK = 4000;
        o.moves = (1u << MB_JUMP) | (1u << MB_MINI) | (1u << MB_LONG) | (1u << MB_JUMPBUG);
        const Result r = Solve(pts, st, o);
        CHECK(r.total > 0.0);
        int ride = 0;
        for (const Combo& c : r.combos) if (c.text.find("ride ") != std::string::npos) ride++;
        CHECK(ride > 0);
        CHECK(SolvedSet(r) == BruteSet(Brute(pts, st, o.moves)));
        // ONE PRESS MUST NOT BURY THE OTHERS. A press costs 1-2 (PlanCost) and a catch n ticks
        // into the band costs +n, so the list interleaves instead of running one press down the
        // whole band: the head of it has to hold several different press sequences
        std::set<std::string> head;
        for (size_t k = 0; k < r.combos.size() && k < 6; k++) head.insert(StripRide(r.combos[k].text));
        CHECK(head.size() >= 3);
        std::printf("  texturebug -> ground: %.0f combination(s), %d with a ride length,"
                    " %d distinct press(es) in the first 6 lines\n", r.total, ride, (int)head.size());
    }

    // ---- 25. texturebug in a marker file: type 4 round trips, rc18 files are unchanged ----
    {
        MarkerLine m;
        CHECK(ParseMarkerLine("1 2 3 4 15 1 1 1 0 0 0\n", true, m) && m.type == PT_TEXTUREBUG
              && m.stand && !m.duck);
        // dihrc18 wrote 0..3 only, so every old line keeps the type it had
        CHECK(ParseMarkerLine("1 2 3 3 15 1 1 1 1 0 0\n", true, m) && m.type == PT_HEADBANG);
        CHECK(ParseMarkerLine("1 2 3 2 15 1 1 1 1 0 0\n", true, m) && m.type == PT_EDGEBUG);
        // and an old (pre-rc18) file's type 4 is still dihrc17's jumpbug marker, not a seam
        CHECK(ParseMarkerLine("1 2 3 4 15 1 1 1 1 0 0\n", false, m) && m.type == PT_GROUND
              && m.moves == (1u << MB_JUMPBUG));
        CHECK(std::string(PointTypeName(PT_TEXTUREBUG)) == "texturebug");
        CHECK(PT_COUNT == 5 && !FloorLike(PT_TEXTUREBUG) && SeamLike(PT_TEXTUREBUG));
    }

    // ---- 26. THE DUCK TIMER REFUSES THE TWO DUCKING PRESSES (dihrc21) ----
    // Every duck release sets m_flDuckTimer = curtime + 0.3 (tf_gamemovement.cpp:3295) and for
    // those 20 ticks DuckOverrides (:3194) strips IN_DUCK off every GROUNDED cmd. A minijump and a
    // longjump are both jump + duck on one grounded cmd, so inside the window they come out as a
    // plain jump -- that is the "it says minijump and I just jump twice" report, and the answer
    // must not offer either press while the clock is running.
    {
        const float F = 0.03125f;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = F;
        Options o; o.moves = kMovesPress;

        const Result open = Solve(pts, st, o);
        CHECK(Has(open, "jump -> land (stand)"));
        CHECK(HasSub(open, "minijump"));
        CHECK(HasSub(open, "longjump"));

        for (int since = 0; since <= 19; since++)          // 0..19 ticks after the release
        {
            Start s2 = st; s2.s.releaseTick = s2.s.tick - since;
            const Result r = Solve(pts, s2, o);
            CHECK(!HasSub(r, "minijump"));
            CHECK(!HasSub(r, "longjump"));
            CHECK(Has(r, "jump -> land (stand)"));          // the plain press is untouched
        }
        // tick 20 is the boundary DuckOverrides compares on: the port marks it edgy and refuses
        Start edge = st; edge.s.releaseTick = edge.s.tick - 20;
        CHECK(!HasSub(Solve(pts, edge, o), "minijump"));
        // and one tick later both are back
        Start past = st; past.s.releaseTick = past.s.tick - 21;
        const Result rp = Solve(pts, past, o);
        CHECK(HasSub(rp, "minijump"));
        CHECK(HasSub(rp, "longjump"));
        std::printf("  duck timer: minijump/longjump refused for 20 ticks, jump unaffected\n");
    }

    // ---- 27. THE DRAWN ARC IS THE ANSWER'S OWN ARC (dihrc21 TraceBest) ----
    {
        const float F = 0.03125f;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = F;
        Options o; o.moves = 1u << MB_JUMP;                 // one press, one answer, one arc

        Solver sv; sv.Begin(pts, st, p, o);
        while (!sv.Step(1000)) {}
        const Result before = sv.Out();
        CHECK(before.total > 0.0);

        std::vector<std::vector<float>> legs;
        CHECK(sv.TraceBest(legs));
        CHECK(legs.size() == 1);
        // tick for tick the same flight the port flies on its own
        const Arc a = FlyOnFloor(SC_JUMP, Hover(F), F);
        CHECK(legs.size() == 1 && legs[0].size() == a.st.size());
        if (!legs.empty())
            for (size_t i = 0; i < legs[0].size() && i < a.st.size(); i++)
                CHECK(legs[0][i] == a.st[i].z);
        float top = -1e9f;
        if (!legs.empty()) for (float z : legs[0]) top = std::max(top, z);
        NEAR(top - F, 50.04, 0.002);                         // section 3's jump apex, drawn
        // ends ON the marker's floor -- the LANDING tick grounds the hull wherever
        // CategorizePosition's 2u trace catches it and only the next tick snaps it flush
        // (gamemovement StayOnGround), so the arc's last point is 0..2u over the plane
        CHECK(!legs.empty() && legs[0].back() >= F - 1e-4f && legs[0].back() <= F + 2.01f);

        // a replay is a replay: same answer twice, and the search's own tallies are not touched
        std::vector<std::vector<float>> again;
        CHECK(sv.TraceBest(again));
        CHECK(again == legs);
        CHECK(sv.Out().ticks == before.ticks);
        CHECK(sv.Out().total == before.total);
        CHECK(sv.Out().combos.size() == before.combos.size());

        // two markers = two legs, each ending on its own marker
        std::vector<Point> two(2);
        two[0].type = two[1].type = PT_GROUND; two[0].z = two[1].z = F;
        Solver sv2; sv2.Begin(two, st, p, o);
        while (!sv2.Step(1000)) {}
        std::vector<std::vector<float>> legs2;
        CHECK(sv2.TraceBest(legs2));
        CHECK(legs2.size() == 2);
        for (const std::vector<float>& L : legs2)
        {
            CHECK(!L.empty());
            if (!L.empty()) CHECK(L.back() >= F - 1e-4f && L.back() <= F + 2.01f);
        }
        std::printf("  best-route arc: %d tick(s) for one jump, apex %.2fu, 2 legs for 2 markers\n",
                    legs.empty() ? 0 : (int)legs[0].size(), top - F);
    }

    // ---- 28. a pixelsurf route's arc: the ride is drawn, and it holds z still ----
    {
        const float F = 0.03125f;
        // the seam a plain jump catches on the way down (section 12's shape)
        float seam = 0.f;
        bool found = false;
        for (float t = 1.f; t < 60.f && !found; t += 0.001f)
            if (CatchesSurf(SC_JUMP, F, F + t)) { seam = F + t; found = true; }
        CHECK(found);
        if (found)
        {
            std::vector<Point> pts(1); pts[0].type = PT_SURF; pts[0].z = seam;
            Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
            Options o; o.moves = 1u << MB_JUMP;
            Solver sv; sv.Begin(pts, st, p, o);
            while (!sv.Step(1000)) {}
            CHECK(sv.Out().total > 0.0);
            std::vector<std::vector<float>> legs;
            CHECK(sv.TraceBest(legs));
            CHECK(legs.size() == 1);
            // the leg ends ON the seam (a catch, not a landing)
            if (!legs.empty() && !legs[0].empty())
                CHECK(std::fabs(legs[0].back() - seam) < 0.05f);
            std::printf("  pixelsurf arc: ends at %.4f, seam %.4f\n",
                        legs.empty() || legs[0].empty() ? 0.f : legs[0].back(), seam);
        }
    }

    // ---- 29. an arc ACROSS a seam: the ride is part of the drawn line ----
    // A leg that starts on a caught seam is flown from the ride the edge was built with (Edge::ride)
    // -- the one replay path that is not just "fly the script again". The ride holds z still, so it
    // draws as a flat run at the seam height before the arc carries on.
    {
        const float F = 0.03125f;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        const float seam = F + 100.f;      // high enough that the hull has to fly to reach it
        std::vector<Point> pts(2);
        pts[0].type = PT_TEXTUREBUG; pts[0].z = seam;
        pts[1].type = PT_GROUND;     pts[1].z = F;
        Options o; o.topK = 4000;
        o.moves = (1u << MB_JUMP) | (1u << MB_MINI) | (1u << MB_LONG) | (1u << MB_JUMPBUG);
        Solver sv; sv.Begin(pts, st, p, o);
        while (!sv.Step(1000)) {}
        CHECK(sv.Out().total > 0.0);
        std::vector<std::vector<float>> legs;
        CHECK(sv.TraceBest(legs));
        CHECK(legs.size() == 2);
        if (legs.size() == 2)
        {
            CHECK(!legs[0].empty() && !legs[1].empty());
            // leg 1 ends where the clip froze the hull: the seam is inside the hull's SIDE, so the
            // feet are under it and the head is over it (texturebug.inl's own window)
            if (!legs[0].empty())
            {
                CHECK(legs[0].back() + kDistEps < seam);
                CHECK(legs[0].back() + 82.f - kDistEps > seam);
                CHECK(legs[0].size() > 1);                 // it had to fly to get there
            }
            // leg 2 ends back on the floor
            if (!legs[1].empty()) CHECK(legs[1].back() >= F - 1e-4f && legs[1].back() <= F + 2.01f);
            std::printf("  seam arc: leg 1 %d tick(s) ending %.3f, leg 2 %d tick(s) ending %.3f\n",
                        (int)legs[0].size(), legs[0].back(), (int)legs[1].size(), legs[1].back());
        }
    }

    // ---- 30. THE ARC'S HORIZONTAL TICKS (dihrc22 TraceBest air flags) ----
    // The drawn line spreads the marker-to-marker distance over the AIRBORNE ticks only, so the
    // replay has to say which ticks those were. A grounded tick moves nobody: the stand-up before
    // a press, TIME_TO_DUCK before a crouch walk off, a pinned seam ride.
    {
        const float F = 0.03125f;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);

        // one plain jump onto a floor: leaves the ground on tick 0, lands on the last one
        std::vector<Point> one(1); one[0].type = PT_GROUND; one[0].z = F;
        Options o; o.moves = 1u << MB_JUMP;
        Solver sv; sv.Begin(one, st, p, o);
        while (!sv.Step(1000)) {}
        std::vector<std::vector<float>> legs;
        std::vector<std::vector<ArcTick>> air;
        CHECK(sv.TraceBest(legs, &air));
        CHECK(air.size() == legs.size());
        for (size_t i = 0; i < legs.size() && i < air.size(); i++) CHECK(air[i].size() == legs[i].size());
        if (!air.empty() && !air[0].empty())
        {
            CHECK(air[0].front().air);                     // the press is a tick in the air
            CHECK(!air[0].back().air);                     // the landing is a tick on the floor
            int nAir = 0; for (const ArcTick& f : air[0]) nAir += f.air ? 1 : 0;
            CHECK(nAir == (int)air[0].size() - 1);         // everything between is flight
            // a plain jump never changes stance, so it never teleports
            float dz = 0.f; for (const ArcTick& f : air[0]) dz += f.stanceDz;
            CHECK(dz == 0.f);
        }
        // the second arg is optional and changes nothing about the first
        std::vector<std::vector<float>> plain;
        CHECK(sv.TraceBest(plain));
        CHECK(plain == legs);

        // a crouch walk off ducks ON THE FLOOR first (TIME_TO_DUCK, no origin move): those ticks
        // are grounded, at the floor's own height, and the drop only starts after them
        std::vector<Point> drop(2);                      // section 19's route: off the ledge, then a press
        drop[0].type = PT_GROUND; drop[0].z = F - 64.f; drop[0].moves = kMovesPress;
        drop[1].type = PT_GROUND; drop[1].z = F - 64.f;
        Options ow; ow.moves = (1u << MB_CROUCHWALKOFF) | kMovesPress; ow.topK = 50;
        Start sd = st; sd.moves = 1u << MB_CROUCHWALKOFF;   // leg 1 can only be the crouch walk off
        Solver sw; sw.Begin(drop, sd, p, ow);
        while (!sw.Step(1000)) {}
        CHECK(sw.Out().total > 0.0);
        std::vector<std::vector<float>> wlegs;
        std::vector<std::vector<ArcTick>> wair;
        CHECK(sw.TraceBest(wlegs, &wair));
        CHECK(wlegs.size() == 2 && wair.size() == 2);
        int nPre = 0;
        if (!wair.empty())
        {
            while (nPre < (int)wair[0].size() && !wair[0][nPre].air) nPre++;
            CHECK(nPre > 0);                               // the crouch happened on the ground
            CHECK(nPre < (int)wair[0].size());             // and the fall happened after it
            for (int i = 0; i < nPre && i < (int)wlegs[0].size(); i++)
                CHECK(std::fabs(wlegs[0][i] - F) < 0.01f); // no origin move while it ducks
        }
        std::printf("  arc horizontal: crouch walk off holds still for %d grounded tick(s), "
                    "then %d airborne\n", nPre, wair.empty() ? 0 : (int)wair[0].size() - nPre);

        // a MINIJUMP ducks in the air: +20 on z in one tick, with no flight behind it. Raw, that
        // is a cliff in the drawn arc (what "the routes are jagged" was); the line routecalc
        // builds takes it out and puts the leg's net shift back along the leg
        Options om; om.moves = 1u << MB_MINI;
        Solver sm; sm.Begin(one, st, p, om);
        while (!sm.Step(1000)) {}
        CHECK(sm.Out().total > 0.0);
        std::vector<std::vector<float>> mlegs;
        std::vector<std::vector<ArcTick>> marc;
        CHECK(sm.TraceBest(mlegs, &marc));
        CHECK(mlegs.size() == 1 && marc.size() == 1);
        if (mlegs.size() == 1 && marc.size() == 1 && mlegs[0].size() > 2)
        {
            const std::vector<float>& L = mlegs[0];
            const std::vector<ArcTick>& A = marc[0];
            float rawStep = 0.f, netDz = 0.f;
            for (size_t k = 0; k < A.size(); k++) netDz += A[k].stanceDz;
            for (size_t k = 1; k < L.size(); k++) rawStep = std::max(rawStep, std::fabs(L[k] - L[k - 1]));
            // the cliff is really there, raw: 20u of stance minus that tick's own gravity move
            CHECK(rawStep > 15.f);
            CHECK(std::fabs(netDz) > 19.f || std::fabs(netDz) < 0.01f);   // +20 held, or ducked back

            // routecalc.inl's own line: z - (teleports so far) + net * (fraction of the leg flown)
            size_t nAir = 0;
            for (const ArcTick& f : A) nAir += f.air ? 1 : 0;
            if (nAir == 0) nAir = A.size();
            std::vector<float> drawn;
            float dz = 0.f; size_t moved = 0;
            for (size_t k = 0; k < L.size(); k++)
            {
                if (A[k].air) moved++;
                dz += A[k].stanceDz;
                drawn.push_back(L[k] - dz + netDz * ((float)moved / (float)nAir));
            }
            float drawnStep = 0.f;
            for (size_t k = 1; k < drawn.size(); k++)
                drawnStep = std::max(drawnStep, std::fabs(drawn[k] - drawn[k - 1]));
            CHECK(drawnStep < 8.f);                        // no cliff left: one tick of gravity, tops
            CHECK(std::fabs(drawn.back() - L.back()) < 0.01f);   // still lands on its marker
            std::printf("  arc stance: minijump raw step %.1fu (net %.1f) -> drawn step %.2fu\n",
                        rawStep, netDz, drawnStep);
        }
    }

    // ---- 31. EASIEST AT THE TOP: fewest binds, then fewest bind switches (dihrc23) ----
    // The panel used to lead with the cheapest path (PlanCost), which is not what is easiest to
    // play: what costs a hand is how many binds you hit and how often you swap between them.
    // Three ground markers on one floor: every press lands back on it, so the same three-press
    // sequences all exist and only the order of the list is under test.
    {
        const float F = 0.03125f;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        std::vector<Point> pts(3);
        for (Point& q : pts) { q.type = PT_GROUND; q.z = F; }
        Options o; o.moves = (1u << MB_JUMP) | (1u << MB_MINI) | (1u << MB_LONG); o.topK = 4000;
        const Result r = Solve(pts, st, o);
        CHECK(r.total > 0.0);

        auto at = [&](const std::string& pre)
        {
            for (size_t i = 0; i < r.combos.size(); i++)
                if (r.combos[i].text.compare(0, pre.size(), pre) == 0) return (int)i;
            return -1;
        };
        // "jump + lj + lj IS BETTER THAN lj + jump + lj": same three binds, one switch against two
        const int jll = at("jump -> longjump -> longjump");
        const int ljl = at("longjump -> jump -> longjump");
        CHECK(jll >= 0 && ljl >= 0);
        CHECK(jll >= 0 && ljl >= 0 && jll < ljl);
        // "mj mj mj is better than lj jb jump": one bind all the way down beats three
        const int mmm = at("minijump -> minijump -> minijump");
        const int lmj = at("longjump -> minijump -> jump");
        CHECK(mmm >= 0 && lmj >= 0);
        CHECK(mmm >= 0 && lmj >= 0 && mmm < lmj);
        // and the head of the list is the easiest thing there is: one bind, no switch
        CHECK(!r.combos.empty() && r.combos[0].switches == 0);
        for (size_t k = 1; k < r.combos.size(); k++)
        {
            const Combo& a = r.combos[k - 1];
            const Combo& b = r.combos[k];
            CHECK(a.presses < b.presses || (a.presses == b.presses && a.switches <= b.switches));
        }
        // the counts themselves: three presses per printed route, switches read off the text
        for (const Combo& c : r.combos)
        {
            int presses = 0, switches = 0;
            std::string last, step;
            for (size_t i = 0; i <= c.text.size(); i++)
            {
                if (i < c.text.size() && c.text.compare(i, 4, " -> ") != 0) { step += c.text[i]; continue; }
                if (step.compare(0, 4, "land") != 0 && step.compare(0, 5, "ride ") != 0)
                {
                    if (!last.empty() && last != step) switches++;
                    last = step; presses++;
                }
                step.clear(); i += 3;
            }
            CHECK(c.presses == presses && c.switches == switches);
        }
        std::printf("  easiest first: %.0f route(s), head \"%s\" (%d bind(s), %d switch(es)),"
                    " jump+lj+lj at #%d vs lj+jump+lj at #%d\n",
                    r.total, r.combos[0].text.c_str(), r.combos[0].presses, r.combos[0].switches,
                    jll + 1, ljl + 1);
    }

    // ---- 32. AIMING AT A DRAWN POINT: the marker snap rule (dihrc24 AimTanTo) ----
    // The four "it snaps to the wrong thing" reports were all this function's job, done the old
    // way: nearest PERPENDICULAR distance, every kind of point in one pool, and a point refused
    // when it sat more than 16u past the surface the ray happened to hit. What is under test is
    // the replacement rule, on its own: an angle, ranked so the crosshair decides.
    {
        struct V { float x, y, z; };
        const V eye{ 0.f, 0.f, 0.f };
        const V fwd{ 1.f, 0.f, 0.f };

        // dead centre, 300u out: 0 offset, accepted
        CHECK(AimTanTo(eye, fwd, V{ 300.f, 0.f, 0.f }) == 0.f);
        // behind you, and the 1u dead zone at your own eye
        CHECK(AimTanTo(eye, fwd, V{ -300.f, 0.f, 0.f }) < 0.f);
        CHECK(AimTanTo(eye, fwd, V{ 0.5f, 0.f, 0.f }) < 0.f);

        // the window is an ANGLE: at 300u tan(2.5deg) is ~13.1u of offset, so 12u is in and 15u out
        CHECK(AimTanTo(eye, fwd, V{ 300.f, 0.f, 12.f }) > 0.f);
        CHECK(AimTanTo(eye, fwd, V{ 300.f, 0.f, 15.f }) < 0.f);
        // ...floored at 2u point blank, so a 1u-spaced finder column is still separable up close
        CHECK(AimTanTo(eye, fwd, V{ 20.f, 0.f, 1.5f }) > 0.f);     // inside the 2u floor
        CHECK(AimTanTo(eye, fwd, V{ 20.f, 0.f, 3.f }) < 0.f);      // outside it (2.5deg = 0.87u)
        // ...and capped at 64u far out, or a far wall's points become a net
        CHECK(AimTanTo(eye, fwd, V{ 2000.f, 0.f, 60.f }) > 0.f);
        CHECK(AimTanTo(eye, fwd, V{ 2000.f, 0.f, 70.f }) < 0.f);   // 2.5deg would be 87u

        // RANKING IS ON SCREEN. Two points the same 3u off the ray, one at 100u one at 1000u:
        // the far one sits a tenth as far from the crosshair to look at, so it is the one being
        // aimed at and it wins. The old rule ranked raw perpendicular distance and called this a
        // tie -- which is how an aim straight at a far ring came back with a near one.
        const float near3 = AimTanTo(eye, fwd, V{ 100.f, 0.f, 3.f });
        const float far3  = AimTanTo(eye, fwd, V{ 1000.f, 0.f, 3.f });
        CHECK(near3 > 0.f && far3 > 0.f && far3 < near3);
        // and the centred one always wins its own column
        CHECK(AimTanTo(eye, fwd, V{ 300.f, 0.f, 0.2f }) < AimTanTo(eye, fwd, V{ 300.f, 0.f, 1.f }));

        // a finder column is 1u apart in z: at 300u each rung is a distinct score, so the pick is
        // the rung the crosshair is nearest and not "whichever came first in the vector"
        float last = -1.f;
        int rungs = 0;
        for (int i = 1; i <= 8; i++)
        {
            const float t = AimTanTo(eye, fwd, V{ 300.f, 0.f, (float)i });
            CHECK(t > last);
            last = t;
            if (t > 0.f) rungs++;
        }
        CHECK(rungs == 8);       // all 8 inside the window, all ordered

        // THE LIP THAT ATE THE SNAP (report 4). The rule has no term for the surface the ray hit,
        // so a point 200u past a lip the ray stops on is still picked. Distance along the ray is
        // the only depth term there is, and it only ever says "behind you".
        CHECK(AimTanTo(eye, fwd, V{ 500.f, 0.f, 2.f }) > 0.f);

        // off-axis rays (the aim is rarely axis-aligned): same answers rotated 45 degrees in xy
        const float s = 0.70710678f;
        const V d{ s, s, 0.f };
        CHECK(AimTanTo(eye, d, V{ 300.f * s, 300.f * s, 0.f }) == 0.f);
        NEAR(AimTanTo(eye, d, V{ 300.f * s, 300.f * s, 5.f }), 5.f / 300.f, 1e-5);
        std::printf("  aim pick: 12u/300u in, 15u out, 3u at 1000u beats 3u at 100u (%.5f vs %.5f),"
                    " 8 rungs ordered\n", far3, near3);
    }

    // ---- 33. HOPS ON YOUR OWN FLAT REACH PIXELS A SINGLE JUMP CANNOT ----
    // What the pixel calculator's hop passes are for (routecalc.inl, k_pcMaxHops): pass k puts k
    // ground markers at the floor you are standing on in front of the pixel, so the search may land
    // back on your flat and press again, or jumpbug off it (leaving the floor 20-22u up instead of
    // touching it). If every hop chain only re-launched the same arc the feature would be a longer
    // way to the same answers, so the claim under test is the one that matters: there are pixel
    // heights nothing reaches off a single press and a hop chain does.
    {
        const float F = kFloor;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        Options o; o.moves = (1u << MB_JUMP) | (1u << MB_MINI) | (1u << MB_LONG) | (1u << MB_JUMPBUG);

        auto Total = [&](int hops, float seam)
        {
            std::vector<Point> pts;
            for (int h = 0; h < hops; h++)
            {
                Point g; g.type = PT_GROUND; g.z = F; g.moves = kMovesMarker;
                pts.push_back(g);
            }
            Point s; s.type = PT_SURF; s.z = seam;
            pts.push_back(s);
            return Solve(pts, st, o, p);
        };

        int onlyHop = 0, onlySingle = 0, both = 0, jbChains = 0;
        float firstOnlyHop = 0.f;
        std::string firstOnlyHopText;
        for (float seam = F + 1.f; seam < F + 90.f; seam += 0.25f)
        {
            const double one = Total(0, seam).total;
            const Result hop = Total(1, seam);
            if (hop.total > 0.0 && HasSub(hop, "jumpbug")) jbChains++;
            if (one > 0.0 && hop.total > 0.0) both++;
            else if (one > 0.0) onlySingle++;
            else if (hop.total > 0.0)
            {
                if (!onlyHop) { firstOnlyHop = seam - F; firstOnlyHopText = hop.combos[0].text; }
                onlyHop++;
            }
        }
        // the point of the feature: pixel heights only a hop chain catches
        CHECK(onlyHop > 0);
        // ...and jumpbugging off your own flat is one of the ways it gets there
        CHECK(jbChains > 0);
        std::printf("  hops on your flat: %d height(s) only a hop reaches (first +%.2fu: %s),"
                    " %d both, %d single only, %d with a jumpbug chain\n",
                    onlyHop, firstOnlyHop, firstOnlyHopText.c_str(), both, onlySingle, jbChains);

        // a second hop keeps answering (the pass is not dead weight), and every hop route really
        // does land on / bug off the flat first: its combos are two presses or more
        int two = 0;
        for (float seam = F + 1.f; seam < F + 90.f && two < 1; seam += 0.25f)
            if (Total(2, seam).total > 0.0) two++;
        CHECK(two > 0);
        const Result chain = Total(1, F + firstOnlyHop);
        CHECK(chain.total > 0.0);
        for (const Combo& c : chain.combos) CHECK(c.presses >= 2);
    }

    // ---- 34. A NEAR MISS SAYS WHICH WAY (Result::closestDz -> where the search steps next) ----
    // A panel that says how far a surf marker missed its window and never which side of it cannot
    // form a "put it here instead" sentence at all. closestDz is the z shift that lands the same
    // falling tick on the window edge, and half a window past that edge is its middle. It is what
    // rs::FindHelpers snaps a LAUNCH by (tests/helperpt_test.cpp: the suggestion is an extra marker
    // in front of the dead one, never a new height for the dead one) -- the same shift read from
    // the other end, since a marker moved down is a launch moved up. Both halves are checked here
    // the only way that means anything: take a seam the jump really catches, move the marker off
    // it, and put it back with the number the solver handed over
    {
        const float F = 0.03125f;
        float seam = 0.f;
        bool found = false;
        for (float t = 1.f; t < 60.f && !found; t += 0.001f)
            if (CatchesSurf(SC_JUMP, F, F + t)) { seam = F + t; found = true; }
        CHECK(found);

        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        Options o; o.moves = 1u << MB_JUMP; o.topK = 32;
        auto SolveSeam = [&](float z)
        {
            std::vector<Point> pts(1); pts[0].type = PT_SURF; pts[0].z = z;
            return Solve(pts, st, o);
        };
        CHECK(SolveSeam(seam).total > 0.0);                  // the seam itself is catchable

        // the window is 0.04u wide, so a miss of a quarter unit is already a long way out
        float flWorst = 0.f;
        for (const float off : { -12.f, -6.f, -2.f, -1.f, -0.25f, 0.25f, 1.f, 2.f, 6.f, 12.f })
        {
            const Result r = SolveSeam(seam + off);
            CHECK(r.total == 0.0 && r.deadPoint == 0);
            CHECK(r.closest[0] < 1e8f);
            // the shift IS the reported distance, with a side on it
            NEAR(r.closest[0], std::fabs(r.closestDz[0]), 0.001);
            // ...and it is never further than half a fall step: past a unit or two the nearest
            // falling tick is not the one you meant to hit any more but the NEXT one down, so the
            // suggestion is "the nearest height this fall can catch", which is the useful answer
            // and not the one that assumes what you were aiming at
            CHECK(std::fabs(r.closestDz[0]) < 12.f);
            flWorst = std::max(flWorst, std::fabs(r.closestDz[0]));
            // inside a unit the nearest tick IS the one the marker was moved off, so the shift
            // points straight back at it: marker moved up -> the suggestion is down
            if (std::fabs(off) <= 1.f)
            {
                CHECK((off > 0.f) == (r.closestDz[0] < 0.f));
                CHECK(std::fabs(std::fabs(r.closestDz[0]) - std::fabs(off)) <= 0.05f);
            }
            // THE SHIFT ITSELF: marker z + it + half a window is a marker that solves. Every
            // offset, near or far. FindHelpers spends this on the launch instead of the marker,
            // and a snap that does not land where it points would spin its whole budget
            const float dz = r.closestDz[0] + (r.closestDz[0] > 0.f ? 0.02f : -0.02f);
            const Result fixed = SolveSeam(seam + off + dz);
            CHECK(fixed.total > 0.0);
            CHECK(fixed.deadPoint < 0);
        }
        std::printf("  suggestion: 10 seam misses, worst nudge %.3fu, all 10 solve where it points\n",
                    flWorst);

        // the other half of what the search reads: highest[] is how far under a floor marker the
        // best arc peaked, so that apex is the ceiling on any ledge in front of it (nothing over
        // the apex can be landed on on the way), and anything under it is fair. A jump apexes
        // +50.04, so a ground marker at +80 is 30 short and a marker at the apex is not
        {
            std::vector<Point> high(1); high[0].type = PT_GROUND; high[0].z = F + 80.f;
            const Result r = Solve(high, st, o);
            CHECK(r.total == 0.0 && r.deadPoint == 0 && r.highest[0] < 0.f);
            NEAR(r.highest[0], 50.04f - 80.f, 0.05);
            std::vector<Point> at(1); at[0].type = PT_GROUND; at[0].z = F + 80.f + r.highest[0];
            const Result rf = Solve(at, st, o);
            CHECK(rf.total > 0.0);                            // the suggested spot is landed on
            std::printf("  suggestion: ground marker %.2fu short -> z %.2f lands\n",
                        -r.highest[0], at[0].z);
        }

        // a HEADBANG is measured off the head, not the feet, and the panel says so in those
        // words. The apex is the BOUNDARY of the range, not a spot that bangs: a ceiling exactly
        // level with the top of the head is touched and the move is never blocked by it. That is
        // why the line reads "at z X or lower" -- everything under the apex is met by the head on
        // the way up, and the ring marks the edge
        {
            std::vector<Point> high(1); high[0].type = PT_HEADBANG; high[0].z = F + 140.f;
            const Result r = Solve(high, st, o);
            CHECK(r.total == 0.0 && r.deadPoint == 0 && r.highest[0] < 0.f);
            NEAR(r.highest[0], (50.04f + 82.f) - 140.f, 0.05);      // apex head, 82 over the feet
            const float zApex = F + 140.f + r.highest[0];
            int hit = 0, tried = 0;
            for (const float slack : { -0.5f, -2.f, -6.f, -20.f })
            {
                std::vector<Point> at(1); at[0].type = PT_HEADBANG; at[0].z = zApex + slack;
                tried++;
                if (Solve(at, st, o).total > 0.0) hit++;
            }
            CHECK(hit == tried);                                    // "or lower" means all of them
            std::vector<Point> exact(1); exact[0].type = PT_HEADBANG; exact[0].z = zApex;
            CHECK(Solve(exact, st, o).total == 0.0);                // ...and the apex is the edge
            std::printf("  suggestion: headbang %.2fu short -> apex z %.2f is the edge, %d of %d"
                        " heights under it bang\n", -r.highest[0], zApex, hit, tried);
        }
    }

    // ---- 35. duckApex: the LAST ground marker only -- a standing jump apexes +50.04u, and a
    // forced duck right at the apex adds FinishDuck's +20u air-duck teleport on top of it. A
    // marker up to 20u over the standing apex should land DUCKED where standing alone fails;
    // past 20u neither stance reaches it; and a marker already reached standing never also prints
    // the ducked fallback (CS:GO's own rule, proj-rc-jb-crouch-1007: "crouch must be visible,
    // never silent")
    {
        const float F = 0.03125f;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        Options o; o.moves = 1u << MB_JUMP;
        const float apex = F + 50.04f;

        std::vector<Point> near(1); near[0].type = PT_GROUND; near[0].z = apex + 15.f;
        const Result rNear = Solve(near, st, o);
        CHECK(!Has(rNear, "jump -> land (stand)"));
        CHECK(Has(rNear, "jump -> land (ducked)"));

        std::vector<Point> far(1); far[0].type = PT_GROUND; far[0].z = apex + 25.f;
        const Result rFar = Solve(far, st, o);
        CHECK(rFar.total == 0.0 && rFar.deadPoint == 0);
        CHECK(!Has(rFar, "jump -> land (ducked)"));

        std::vector<Point> easy(1); easy[0].type = PT_GROUND; easy[0].z = F + 10.f;
        const Result rEasy = Solve(easy, st, o);
        CHECK(Has(rEasy, "jump -> land (stand)"));
        CHECK(!Has(rEasy, "jump -> land (ducked)"));   // reached standing -- no silent duck beside it

        // not the last marker: the fallback never fires mid-route (only the final leg forces it)
        std::vector<Point> mid(2);
        mid[0].type = PT_GROUND; mid[0].z = near[0].z; mid[0].moves = kMovesPress;
        mid[1].type = PT_GROUND; mid[1].z = F + 10.f;
        Options o2; o2.moves = 1u << MB_JUMP;
        const Result rMid = Solve(mid, st, o2);
        CHECK(rMid.total == 0.0 && rMid.deadPoint == 0);   // marker 1 missed, and it is not the last

        std::printf("  duckApex: +15u over the apex lands ducked, +25u reaches neither stance, "
                    "already-standing prints no ducked twin\n");
    }

    // ---- 36. split / memory budget: maxEdges bounds growth instead of letting it run OOM. A tiny
    // cap on a route that would normally produce many combos must finish (not hang), report
    // capped, and never grow past the cap -- the edges counter is per LAYER (one per marker), so
    // the bound holds at every point in the route, not just the end
    {
        const float F = 0.03125f;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        std::vector<Point> pts(1); pts[0].type = PT_GROUND; pts[0].z = F + 10.f;
        Options o; o.moves = (1u << MB_JUMP) | (1u << MB_MINI) | (1u << MB_LONG); o.topK = 4000;
        o.maxEdges = 2;
        const Result r = Solve(pts, st, o);
        CHECK(r.capped);
        CHECK(r.total <= 2.0);     // never more combinations than the edge cap allowed in
        // the same route without the cap finds more than 2 -- the cap is really doing something,
        // not just always true
        Options full; full.moves = o.moves; full.topK = 4000;
        const Result rFull = Solve(pts, st, full);
        CHECK(rFull.total > r.total);
        std::printf("  split budget: maxEdges 2 capped %.0f combination(s) vs %.0f uncapped\n",
                    r.total, rFull.total);
    }

    // ---- 37. ForceTimeout: the worker's wall-clock ceiling, not a search outcome. Stopping mid
    // layer must report timedOut and must NOT claim a dead marker (deadPoint stays -1) -- Finish()
    // never ran, so there is nothing truthful to blame
    {
        const float F = 0.03125f;
        Start st; st.hasFloor = true; st.floorEff = F; st.s = RestState(F);
        std::vector<Point> pts(2);
        pts[0].type = PT_GROUND; pts[0].z = F + 10.f; pts[0].moves = kMovesPress;
        pts[1].type = PT_GROUND; pts[1].z = F + 20.f;
        Options o; o.moves = kMovesPress;
        Solver sv;
        sv.Begin(pts, st, Params(), o);
        sv.Step(1);             // partway through layer 0, well short of Done()
        CHECK(!sv.Done());
        sv.ForceTimeout();
        CHECK(sv.Done());
        CHECK(sv.Out().timedOut);
        CHECK(sv.Out().deadPoint < 0);
        CHECK(sv.Out().combos.empty());
        std::printf("  ForceTimeout: stops mid-layer, timedOut set, no dead-marker claim\n");
    }

    std::printf("%s: %d of %d checks passed\n", s_failed ? "FAILED" : "OK", s_checks - s_failed, s_checks);
    return s_failed ? 1 : 0;
}
