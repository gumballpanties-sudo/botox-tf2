#pragma once

// ==== net settings: which convar may be written RIGHT NOW, and why not ==================
// Engine-free so tests/netcvar_test.cpp can brute-force it. netregion.cpp is the only user.
//
// Two things decide it, and they are NOT the same question:
//
//  1. WHAT the name is. Three tiers, every one taken from a flag in sdk-2013:
//     * kRate -- rate / cl_cmdrate / cl_updaterate. FCVAR_NOT_CONNECTED like the rest, but
//       these are the ones that cost BANDWIDTH: they are what the server reads to decide how
//       many snapshots to send you and how big they may be (player.cpp:3484). Raising them
//       does not make a thin connection carry more, it makes the server stop trimming to fit
//       and the drops move from the server's choke counter to the wire. Own tier, own opt-in.
//     * kConnection -- FCVAR_NOT_CONNECTED, refused by the engine while you are alive on a
//       team, so the set must WAIT. Proven per name:
//         cl_interp, cl_interp_ratio, cl_predict  cdll_bounded_cvars.cpp:103, 69, 29
//         cl_predictweapons, cl_lagcompensation   prediction.cpp:37, 38
//       The old table missed all three of the last group while every preset but klown sets
//       cl_lagcompensation -- so the pump spent its 20 retries on a set the engine was never
//       going to take and gave up on the WHOLE preset. That is what "net settings do nothing"
//       was.
//     * kFree -- no flag, applies instantly, mid-match included:
//         cl_smooth, cl_smoothtime           c_baseplayer.cpp:96, 98   (flags 0)
//         cl_pred_optimize                   prediction.cpp:46         (flags 0)
//         cl_interp_all                      c_baseentity.cpp:82       (flags 0)
//         cl_interpolate                     c_baseentity.cpp:3216     (USERINFO|DEVELOPMENTONLY,
//                                                                       no NOT_CONNECTED)
//       cl_interpolate was in the old "connection" list and did not belong there.
//
//  2. WHETHER the engine would take it. That is CTFGameRules::IsConnectedUserInfoChangeAllowed
//     (tf_gamerules.cpp:20561) -- the client half is what CHLClient::IsConnectedUserInfoChangeAllowed
//     hands the engine before it refuses a FCVAR_NOT_CONNECTED set (cdll_client_int.cpp:2627).
//     NOT "am I connected": you may change them dead, on spectator/unassigned, and for two
//     seconds after a respawn. The old code held on connected-at-all, which in a normal match
//     means forever.

namespace netcv {

// tf_shareddefs.h
inline constexpr int kTeamRed  = 2;
inline constexpr int kTeamBlue = 3;

enum Class { kFree = 0, kConnection, kRate };

inline bool NameIs(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

inline Class Classify(const char* n)
{
    if (!n) return kFree;
    if (NameIs(n, "rate") || NameIs(n, "cl_cmdrate") || NameIs(n, "cl_updaterate"))
        return kRate;
    if (NameIs(n, "cl_interp")     || NameIs(n, "cl_interp_ratio") ||
        NameIs(n, "cl_predict")    || NameIs(n, "cl_predictweapons") ||
        NameIs(n, "cl_lagcompensation"))
        return kConnection;
    return kFree;
}

// CTFGameRules::IsConnectedUserInfoChangeAllowed, client half, CONSERVATIVE SUBSET.
// The real one also allows a set within 2 s of a respawn/class change and has a
// prediction-suppression carve-out; leaving those out can only make us wait LONGER,
// never push a set the engine would refuse. Do not "complete" it without the spawn
// time to read -- guessing that window is how you end up writing mid-life again.
inline bool UserInfoChangeAllowed(bool bInGame, bool bAlive, int iTeam)
{
    if (!bInGame) return true;                                  // main menu: anything goes
    if (!bAlive)  return true;                                  // dead / respawning
    return iTeam != kTeamRed && iTeam != kTeamBlue;             // spectator, unassigned
}

enum Action {
    kSet = 0,   // write it now
    kHold,      // the engine would refuse: wait, do NOT burn a retry
    kSkip,      // not ours to touch at all (rates, opt-in off)
};

inline Action Decide(Class c, bool bRatesOptIn, bool bAllowed)
{
    if (c == kRate && !bRatesOptIn) return kSkip;
    if (c != kFree && !bAllowed)    return kHold;
    return kSet;
}

} // namespace netcv
