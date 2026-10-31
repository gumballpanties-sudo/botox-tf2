#pragma once
#include <QtGlobal>

// libs/global/kis_global.h, the one helper the blend maths uses
template<typename T>
constexpr inline const T &kisBoundFast(const T &min, const T &val, const T &max)
{
    return qMax(min, qMin(max, val));
}

#define CLAMP(x, l, h) (((x) > (h)) ? (h) : (((x) < (l)) ? (l) : (x)))
