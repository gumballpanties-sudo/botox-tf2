#pragma once
// Fake movement seam gate, engine-free (gate test: tests/fmseam_test.cpp).
//
// A real pixel surf / wall bug catches on a SEAM: two brushes whose faces lie in the same plane
// and butt together. The collision BSP keeps the mapper's brushes as authored, so "2+ brushes
// with a face on the wall's plane, reaching into the hull's contact window" is exactly a seam
// under the player. Displacements and props are not brushes and never count here (wall jump
// lets them through on its own, fakemove.inl FM_JumpSeam).
//
// Brush planes as IEngineTrace::GetBrushInfo returns them: Vector4D (nx, ny, nz, dist),
// normal pointing OUT of the brush, solid where dot(n, p) < dist on every plane.
#include <cmath>

struct FmPlane { float nx, ny, nz, d; };

// box [mn, mx] reaches inside the brush? Separating test on the brush planes only: the engine's
// list includes the axial bevels, so this rejects the AABB false positives of GetBrushesInAABB.
// Touching (box flush against the outside) does NOT count.
inline bool FmSeam_BoxInBrush(const FmPlane* p, int count, const float mn[3], const float mx[3])
{
    for (int i = 0; i < count; ++i)
    {
        const float nearest = p[i].nx * (p[i].nx >= 0.f ? mn[0] : mx[0])
                            + p[i].ny * (p[i].ny >= 0.f ? mn[1] : mx[1])
                            + p[i].nz * (p[i].nz >= 0.f ? mn[2] : mx[2]);
        if (nearest - p[i].d >= 0.f) return false;
    }
    return count > 0;
}

// brush has a face on the hit plane (same outward normal, same distance)
inline bool FmSeam_HasFace(const FmPlane* p, int count, float hx, float hy, float hz, float hd)
{
    for (int i = 0; i < count; ++i)
        if (p[i].nx * hx + p[i].ny * hy + p[i].nz * hz > 0.999f && (p[i].d - hd) * (p[i].d - hd) < 0.25f)
            return true;
    return false;
}

// one brush counts toward the seam
inline bool FmSeam_BrushCounts(const FmPlane* p, int count, const float mn[3], const float mx[3],
                               float hx, float hy, float hz, float hd)
{
    return FmSeam_HasFace(p, count, hx, hy, hz, hd) && FmSeam_BoxInBrush(p, count, mn, mx);
}

// ---- head surf: brush bottom EDGE at the hull top ----
// Upside-down pixel surf. A pixel surf rides a seam under the hull; this rides a brush BOTTOM at
// the hull top whose underside ENDS at the hull's side: a horizontal seam in a wall beside you
// (the upper brush's bottom), or the lip of an overhang you are a sliver under. A bottom that
// covers more of the hull than `sliver` is a ceiling, not a seam, and never counts.
// hmn/hmx = hull footprint xy, pad = how far past its side the edge may sit.
// Flat bottom plane = normal (0,0,-1), d = -z. Writes its z when this brush counts with the
// bottom inside [zlo, zhi], plus the edge's outward xy normal (nx, ny): the side plane the hull
// center is furthest outside of, i.e. the face looking at the hull. 0,0 = none found.
inline bool FmSeam_BottomEdge(const FmPlane* p, int count, const float hmn[2], const float hmx[2],
                              float pad, float sliver, float zlo, float zhi, float& bz,
                              float& nx, float& ny)
{
    for (int i = 0; i < count; ++i)
    {
        if (p[i].nz > -0.999f) continue;
        const float z = p[i].d / p[i].nz;
        if (z < zlo || z > zhi) continue;
        // half-unit slab right above the bottom face = the brush's underside
        const float gmn[3] = { hmn[0] - pad, hmn[1] - pad, z + 0.01f };
        const float gmx[3] = { hmx[0] + pad, hmx[1] + pad, z + 0.5f };
        const float smn[3] = { hmn[0] + sliver, hmn[1] + sliver, z + 0.01f };
        const float smx[3] = { hmx[0] - sliver, hmx[1] - sliver, z + 0.5f };
        if (!FmSeam_BoxInBrush(p, count, gmn, gmx) || FmSeam_BoxInBrush(p, count, smn, smx)) return false;
        bz = z;

        const float cx = (hmn[0] + hmx[0]) * 0.5f, cy = (hmn[1] + hmx[1]) * 0.5f, cz = z + 0.25f;
        float best = -1e30f;
        nx = ny = 0.f;
        for (int j = 0; j < count; ++j)
        {
            if (std::fabs(p[j].nz) >= 0.7f) continue;   // top / bottom
            const float s = p[j].nx * cx + p[j].ny * cy + p[j].nz * cz - p[j].d;
            if (s > best) { best = s; nx = p[j].nx; ny = p[j].ny; }
        }
        const float l = std::sqrt(nx * nx + ny * ny);
        if (l > 1e-3f) { nx /= l; ny /= l; } else nx = ny = 0.f;
        return true;
    }
    return false;
}

// ---- head surf stick, post-move ----
// The ride holds the hull where it caught the edge. n = edge outward normal (unit xy), w = the
// engine's air wish xy (any length), d = post - pre move xy, v = velocity (edited).
// Into the brush never moves or keeps speed: strafing into the edge keeps you surfing on it
// instead of sliding under. Away is killed too unless the wish points away (within ~60 deg),
// so only a real strafe off the edge lets go. Returns dn = how far to pull the origin back
// along n (0 = keep the move).
constexpr float k_fmStickAway = 0.5f;
inline float FmSeam_Stick(float nx, float ny, float wx, float wy, float dx, float dy, float& vx, float& vy)
{
    const float wl = std::sqrt(wx * wx + wy * wy);
    const bool leave = wl > 1e-3f && (wx * nx + wy * ny) >= k_fmStickAway * wl;
    const float vn = vx * nx + vy * ny;
    if (vn < 0.f || (vn > 0.f && !leave)) { vx -= nx * vn; vy -= ny * vn; }
    const float dn = dx * nx + dy * ny;
    return (dn < 0.f || (dn > 0.f && !leave)) ? dn : 0.f;
}
