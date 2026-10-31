#pragma once
// DETECTIONS, the pure half. Engine free: tests/detections_test.cpp covers this, detections.cpp
// calls it instead of repeating it. A "stack" is N tricks landing without a miss-gap wider than
// `grace` ticks -- edgebugs chained back to back read as one streak, not N separate announcements.

struct DetStack { int count = 0, sinceHit = 0; };

// Call once per real tick (caller dedups on MvInd_t::tick) with whether the trick hit THIS tick.
// Returns the streak length the instant it breaks (sinceHit exceeds grace after >=1 hit), 0
// otherwise -- the caller announces on a nonzero return and resets nothing itself.
inline int DetStackUpdate(DetStack& st, bool hit, int grace)
{
    if (hit) { ++st.count; st.sinceHit = 0; return 0; }
    if (st.count == 0) return 0;
    if (++st.sinceHit > grace)
    {
        const int broke = st.count;
        st.count = 0; st.sinceHit = 0;
        return broke;
    }
    return 0;
}
