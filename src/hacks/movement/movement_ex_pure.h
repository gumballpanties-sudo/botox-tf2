#pragma once
// the handful of pure-math decisions out of movement_ex.inl / LongJump's
// adaptive-cancel append, pulled out so they're testable without the engine (see
// tests/movementex_test.cpp). No engine types, no includes -- plain in/out.

// Hsw: which way the sideways press goes. side: 0 = right-biased, 1 = left-biased.
// Matches botox movement.cpp:570 (`s = (side==0?1:-1) * (grounded?1:-1)`).
inline int HswSign(int side, bool grounded)
{
    return (side == 0 ? 1 : -1) * (grounded ? 1 : -1);
}

// Delay hop: true once `wait` ticks of air time have passed since the last air tick.
// Matches botox movement.cpp:635 (`cmd->tick_count > last_air_tick + wait`).
inline bool DelayHopShouldJump(int tickNow, int lastAirTick, int wait)
{
    return tickNow > lastAirTick + wait;
}

// Long jump adaptive cancel: true the first tick any ENABLED stop reason fires.
// Matches botox movement.cpp:1710-1727 (none ticked = landed, the hard fallback that
// keeps the cancel from running forever is handled by the caller passing stopLanded=true
// when the vector has nothing else set).
inline bool LjAdaptiveShouldCancel(bool stopLetGo, bool stopLanded, bool stopTimed,
                                    bool keyUp, bool groundedNow, bool timeUp)
{
    return (stopLetGo && keyUp) || (stopLanded && groundedNow) || (stopTimed && timeUp);
}
