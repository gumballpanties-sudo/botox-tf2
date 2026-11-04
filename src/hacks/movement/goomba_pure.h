#pragma once
// Auto goomba timing, pure (tests/goomba_test.cpp). No engine types.
//
// Why "contact right after launch" is the max boost: when a player leaves the hull they stand
// on, CGameMovement::SetGroundEntity (gamemovement.cpp:3624) adds that ground's abs velocity
// to their base velocity (z = ground vz), and CPlayerMove::CheckMovingGround
// (player_command.cpp:115) turns it into a real impulse next cmd. Landing on us sets
// base.z = our vz too (:3618). So whatever is above gets OUR vz at the contact tick. Our vz
// peaks the tick we launch (jump = 289 set, tf_gamemovement.cpp:1330) and only falls after,
// so fire so the contact lands on the first tick after the launch.

#include <cmath>
#include <initializer_list>

namespace gmb {

// height gained t seconds after leaving with vz0. Exact for the engine's half-gravity
// StartGravity/FinishGravity split: the per-tick sum telescopes to this closed form.
inline float Rise(float vz0, float g, float t) { return vz0 * t - 0.5f * g * t * t; }

// First tick k in [1, maxK] where their feet reach our head if we launch NOW.
// gap = their feet z - our head z (> 0 = still above). myVz = our vz right after the cmd:
// 289 for a jump, our current vz for a rocket / sticky (the blast lands later, the caller's
// lead covers that delay). myFalls = we are airborne after it (jumped, or already in air);
// false = standing still on the floor, no gravity on our head.
// -1 = no contact inside the window, or already touching (gap <= 0: they stand on us,
// nothing to time).
inline constexpr float kTouch = 2.f;

// our head rise k ticks after the fire cmd starts. kick = vz a blast adds at the END of the
// fire tick: a sticky goes off in the det cmd's PostThink (SecondaryAttack ->
// DetonateRemotePipebombs -> Detonate, tf_weapon_pipebomblauncher.cpp:566), which runs after
// that cmd's ProcessMovement (player_command.cpp RunCommand). So tick 1 moves on the old vz
// (the jump of a 0x806 press), every tick after on old + kick. kick 0 = one closed form.
inline float MyRise(float myVz, bool myFalls, float kick, float g, float dt, int k)
{
    const float t = dt * (float)k;
    if (kick == 0.f || k <= 1) return myFalls ? Rise(myVz, g, t) : myVz * t;
    const float first = myFalls ? Rise(myVz, g, dt) : myVz * dt;
    return first + Rise((myFalls ? myVz - g * dt : myVz) + kick, g, t - dt);
}

// our vz right after the fire tick: what a landing on us hands them (gamemovement.cpp:3622)
inline float LaunchVz(float myVz, bool myFalls, float kick, float g, float dt)
{
    return (myFalls ? myVz - g * dt : myVz) + kick;
}

inline int ContactTick(float gap, float theirVz, float myVz, bool myFalls, float g, float dt, int maxK, float kick = 0.f)
{
    if (gap <= 0.f || dt <= 0.f) return -1;
    for (int k = 1; k <= maxK; ++k)
    {
        const float t = dt * (float)k;
        // touch = their ground probe reaching our head: CategorizePosition traces 2u down
        // (tf_gamemovement.cpp:2373). Heroin's goomba assist uses a 1.2u window the same way
        if (gap + Rise(theirVz, g, t) - MyRise(myVz, myFalls, kick, g, dt, k) <= kTouch)
            return k;
    }
    return -1;
}

// ---- simulated target path (aim/autorocketjump.cpp Aim_GoombaPath) -----------------------
// Heroin's goomba assist (FUN_180075a10) runs the real ProcessMovement on its target instead
// of a parabola: walls, ceilings, ledges and OUR hull stop them. p[k-1] = their feet +
// velocity after k ticks past the server time our cmd runs at.
struct PathPt { float x = 0.f, y = 0.f, z = 0.f, vx = 0.f, vy = 0.f, vz = 0.f; };

// ContactTick on that path. me/myVel = our feet + velocity now, myHull = our height,
// reach = per-axis touch distance (half widths summed). First k whose feet are within kTouch
// of our (moving) head with the hulls overlapping in xy; -1 = they never land on us inside n
// (a ledge caught them, they slid off, or they passed beside us).
inline int PathContact(const PathPt* p, int n, float meX, float meY, float meZ, float myVx, float myVy,
                       float myHull, float myVz, bool myFalls, float kick, float reach, float g, float dt)
{
    for (int k = 1; k <= n; ++k)
    {
        const float t = dt * (float)k;
        const float feet = meZ + MyRise(myVz, myFalls, kick, g, dt, k);
        const PathPt& q = p[k - 1];
        const bool over = fabsf(q.x - (meX + myVx * t)) < reach && fabsf(q.y - (meY + myVy * t)) < reach;
        if (over && q.z - (feet + myHull) <= kTouch)
            return k;
        if (q.z < feet) return -1;   // their feet under ours: went past us
    }
    return -1;
}

// fire this cmd: contact lands within 1 + lead ticks. lead = launch delay (rocket travel)
// plus ping, the in-game tuning knob.
inline bool ShouldFire(int contactTick, int lead) { return contactTick >= 1 && contactTick <= 1 + lead; }

// the cmd before the fire one: release jump so the fire cmd's IN_JUMP is a fresh press
// (CheckJumpButton refuses held jump, tf_gamemovement.cpp:1235).
inline bool ShouldPrime(int contactTick, int lead) { return contactTick == 2 + lead; }

// ---- blast model: the rocket aim search scores every candidate impact with this ----------
struct V
{
    float x = 0.f, y = 0.f, z = 0.f;
    V operator+(const V& o) const { return { x + o.x, y + o.y, z + o.z }; }
    V operator-(const V& o) const { return { x - o.x, y - o.y, z - o.z }; }
    V operator*(float f)    const { return { x * f, y * f, z * f }; }
    float Len() const { return sqrtf(x * x + y * y + z * z); }
};

// a player at one instant. org = feet (abs origin), half = hull half width, hullZ = hull height
struct Body { V org, vel; float half = 24.f, hullZ = 82.f; };

// velocity impulse a blast at src gives b.
// CTFRadiusDamageInfo::ApplyToEntity (tf_gamerules.cpp:5755): distance = the nearer of
// WorldSpaceCenter and abs origin, damage = RemapValClamped(dist, 0, radius, dmg, dmg*falloff).
// CTFPlayer::ApplyPushFromDamage (tf_player.cpp:10232) + DamageForce (:8661): force =
// damage * 82/sizeZ * scale, capped at 1000, along -(src - 10z - center) (OnTakeDamage :9349).
// scale: enemy 6 (tf_damageforcescale_other), soldier self 10 in air / 5 grounded.
// forceSizeZ: 82 standing, 62 ducked enemy, 55 for a ducked self (the :10252 override).
inline V BlastImpulse(V src, const Body& b, float dmg, float radius, float falloff, float scale, float forceSizeZ)
{
    const V center = b.org + V{ 0.f, 0.f, b.hullZ * 0.5f };
    const float dist = fminf((src - center).Len(), (src - b.org).Len());
    if (dist > radius || radius <= 0.f || forceSizeZ <= 0.f) return {};
    const float d = dmg + (dmg * falloff - dmg) * (dist / radius);
    const float force = fminf(d * (82.f / forceSizeZ) * scale, 1000.f);
    V dir = src - V{ 0.f, 0.f, 10.f } - center;
    const float l = dir.Len();
    if (l < 1e-3f) return { 0.f, 0.f, force };   // dead centre: straight up, the engine's normalize of 0 is 0
    return dir * (-force / l);
}

// our launch off our own sticky: self pipebomb damage x0.75 (tf_gamerules.cpp:5826), half
// falloff, scale 9 (DAMAGE_FORCE_SCALE_SELF, tf_player.cpp:147 / :10297), crouch hull counts
// as 55 (:10252). Each sticky is its own explosion: the caller sums them.
inline V SelfStickyPush(V src, const Body& me, float dmg, float radius, bool ducked)
{
    return BlastImpulse(src, me, dmg * 0.75f, radius, 0.5f, 9.f, ducked ? 55.f : 82.f);
}

// what the target leaves with after a blast. me / them = both bodies the instant the blast
// lands (impulses already added). Both fly ballistic; the first tick their feet come within
// kTouch of our head with the hulls overlapping is the landing, and a landing hands them our
// vz (gamemovement.cpp:3618 -> player_command.cpp:115). No landing = they keep their own vz.
struct Outcome { float vz = -1e9f; int contact = -1; float myVz = 0.f, theirVz = 0.f; };
inline Outcome Fly(Body me, Body them, float g, float dt, int maxT)
{
    Outcome o;
    o.theirVz = them.vel.z;
    o.vz = them.vel.z;   // push only
    if (them.org.z - (me.org.z + me.hullZ) <= 0.f) return o;   // not above us: no landing to time
    const float reach = me.half + them.half;
    for (int k = 1; k <= maxT; ++k)
    {
        for (Body* b : { &me, &them })
        {
            b->org = b->org + b->vel * dt + V{ 0.f, 0.f, -0.5f * g * dt * dt };
            b->vel.z -= g * dt;
        }
        const float gap = them.org.z - (me.org.z + me.hullZ);
        if (gap <= kTouch && gap > -them.hullZ
            && fabsf(them.org.x - me.org.x) < reach && fabsf(them.org.y - me.org.y) < reach)
        {
            o.contact = k;
            o.myVz = me.vel.z;
            o.vz = fmaxf(o.theirVz, me.vel.z);
            return o;
        }
        if (them.org.z < me.org.z) break;   // passed us: no landing
    }
    return o;
}

} // namespace gmb
