// Gate test for the whole-map pixel surf candidate math (src/hacks/movement/ps_map.h): a brush's wall
// face top edge, its ends, the wedge/ramp refusal, and the merge that turns a row of brushes back
// into one surf.
// Build + run:  cmake --build build --config Release --target psmap_test && build\Release\psmap_test.exe
#include "../src/hacks/movement/ps_map.h"
#include <cstdio>
#include <vector>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)

using Brush = std::vector<FmPlane>;

// engine box brush order (enginetrace GetBrushInfo IsBox): +x +y +z maxs, -x -y -z -mins
static Brush Box(float x0, float y0, float z0, float x1, float y1, float z1)
{
    return { { 1, 0, 0, x1 }, { 0, 1, 0, y1 }, { 0, 0, 1, z1 },
             { -1, 0, 0, -x0 }, { 0, -1, 0, -y0 }, { 0, 0, -1, -z0 } };
}

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

static std::vector<PsMapSeam> Seams(const Brush& b)
{
    std::vector<PsMapSeam> v;
    PsMap_BrushSeams(b.data(), (int)b.size(), v);
    return v;
}

// the seam whose normal is (nx, ny), if the brush produced one
static bool Face(const std::vector<PsMapSeam>& v, float nx, float ny, PsMapSeam& out)
{
    for (const PsMapSeam& s : v)
        if (Near(s.nx, nx, 0.01f) && Near(s.ny, ny, 0.01f)) { out = s; return true; }
    return false;
}

static void TestBoxFaces()
{
    // a plain wall brush: 16 thick, 200 long, top at z = 64
    const Brush wall = Box(100, -100, 0, 116, 100, 64);
    const std::vector<PsMapSeam> v = Seams(wall);

    float zTop = 0.f;
    CHECK(PsMap_BrushTop(wall.data(), (int)wall.size(), zTop) && Near(zTop, 64.f));

    // four wall faces, all at the top, and NOT the top/bottom planes (|nz| = 1)
    CHECK(v.size() == 4);
    for (const PsMapSeam& s : v) { CHECK(Near(s.z, 64.f)); CHECK(std::fabs(s.nx) + std::fabs(s.ny) > 0.9f); }

    // the -x face (the side a player standing at x < 100 rides): edge runs the brush's whole
    // 200u length at x = 100
    PsMapSeam s;
    CHECK(Face(v, -1.f, 0.f, s));
    CHECK(Near(s.ax, 100.f) && Near(s.bx, 100.f));
    CHECK(Near(s.Len(), 200.f));
    CHECK(Near(std::min(s.ay, s.by), -100.f) && Near(std::max(s.ay, s.by), 100.f));

    // the +y end cap is only 16 wide: shorter than k_psMapMinLen 12? no -- 16 > 12, it stays
    CHECK(Face(v, 0.f, 1.f, s));
    CHECK(Near(s.Len(), 16.f) && Near(s.ay, 100.f) && Near(s.by, 100.f));

    // ...but an 8u thick wall's end caps are too short to ride and are dropped
    const std::vector<PsMapSeam> thin = Seams(Box(100, -100, 0, 108, 100, 64));
    CHECK(thin.size() == 2);                       // the two 200u faces only
    CHECK(Face(thin, 1.f, 0.f, s) && Face(thin, -1.f, 0.f, s));
    CHECK(!Face(thin, 0.f, 1.f, s) && !Face(thin, 0.f, -1.f, s));
}

static void TestRefusals()
{
    PsMapSeam s;
    float zTop = 0.f;

    // ramp: no flat top, so no edge a hull stands a sliver of itself on
    Brush ramp = { { 0, 0, -1, 0 }, { -1, 0, 0, 0 }, { 0, 1, 0, 64 }, { 0, -1, 0, 0 },
                   { 0.7071f, 0, 0.7071f, 45.25f } };
    CHECK(!PsMap_BrushTop(ramp.data(), (int)ramp.size(), zTop));
    CHECK(Seams(ramp).empty());

    // a 45 degree slab face is not a wall: k_psMapMaxNz refuses it even with a flat top
    Brush slab = Box(0, 0, 0, 64, 64, 64);
    slab.push_back({ 0.7071f, 0.f, 0.7071f, 45.25f });
    const std::vector<PsMapSeam> sl = Seams(slab);
    for (const PsMapSeam& e : sl) CHECK(std::fabs(e.nx - 0.7071f) > 0.01f);

    // the clip is real: a face the brush's other planes cut away entirely produces nothing.
    // A wedge (triangle in xy) has an axial bevel plane touching only one vertical EDGE.
    Brush wedge = { { 0, 0, 1, 64 }, { 0, 0, -1, 0 },
                    { -1, 0, 0, 0 }, { 0, -1, 0, 0 },
                    { 0.7071f, 0.7071f, 0, 70.71f },   // the diagonal face
                    { 1, 0, 0, 100 }, { 0, 1, 0, 100 } };   // bevels, tangent at the corners
    const std::vector<PsMapSeam> wg = Seams(wedge);
    // the diagonal + the two real sides ride; the bevels are single points, not segments
    CHECK(Face(wg, 0.7071f, 0.7071f, s) && Near(s.Len(), 141.42f, 0.05f));
    CHECK(!Face(wg, 1.f, 0.f, s));
    CHECK(!Face(wg, 0.f, 1.f, s));
}

static void TestMerge()
{
    // a wall authored as four brushes side by side: one surf, 400u long
    std::vector<PsMapSeam> v;
    for (int i = 0; i < 4; ++i)
        PsMap_BrushSeams(Box(100.f, -200.f + i * 100.f, 0.f, 116.f, -100.f + i * 100.f, 64.f).data(), 6, v);
    CHECK(v.size() == 16);
    PsMap_Merge(v);
    PsMapSeam s;
    CHECK(Face(v, -1.f, 0.f, s) && Near(s.Len(), 400.f));
    CHECK(Near(std::min(s.ay, s.by), -200.f) && Near(std::max(s.ay, s.by), 200.f));
    // the seams between them are gone, the outer end caps are not
    CHECK(Face(v, 0.f, -1.f, s) && Near(s.Len(), 16.f));

    // a gap wider than the tolerance stays two surfs
    std::vector<PsMapSeam> gap;
    PsMap_BrushSeams(Box(100, -100, 0, 116, 0, 64).data(), 6, gap);
    PsMap_BrushSeams(Box(100, 40, 0, 116, 140, 64).data(), 6, gap);
    PsMap_Merge(gap);
    int nx = 0;
    for (const PsMapSeam& e : gap) if (Near(e.nx, -1.f)) ++nx;
    CHECK(nx == 2);

    // different heights never merge, even on the same plane
    std::vector<PsMapSeam> steps;
    PsMap_BrushSeams(Box(100, -100, 0, 116, 0, 64).data(), 6, steps);
    PsMap_BrushSeams(Box(100, 0, 0, 116, 100, 96).data(), 6, steps);
    PsMap_Merge(steps);
    int nz = 0;
    for (const PsMapSeam& e : steps) if (Near(e.nx, -1.f)) ++nz;
    CHECK(nz == 2);

    // sub-unit offset = the pixel surf itself: two brushes 0.03u apart in depth are the SAME
    // wall for marking purposes (FmSeam_HasFace calls 0.5u coplanar)
    std::vector<PsMapSeam> lip;
    PsMap_BrushSeams(Box(100.f,    -100, 0, 116, 0,   64).data(), 6, lip);
    PsMap_BrushSeams(Box(99.97f,      0, 0, 116, 100, 64).data(), 6, lip);
    PsMap_Merge(lip);
    int nl = 0;
    for (const PsMapSeam& e : lip) if (Near(e.nx, -1.f)) ++nl;
    CHECK(nl == 1);

    // ...BUT A SUB-UNIT DIFFERENCE IN HEIGHT IS NOT THE SAME THING. The probe owns the 1u unit
    // the height falls in, so two tops 0.2u apart that straddle an integer are two different
    // questions: merged, the whole run is probed at the first one's unit and every sample over
    // the second asks a unit that has no surf in it. Depth stays tolerant, height does not
    std::vector<PsMapSeam> off;
    PsMap_BrushSeams(Box(100, -100, 0, 116, 0,   63.9f).data(), 6, off);
    PsMap_BrushSeams(Box(100,    0, 0, 116, 100, 64.1f).data(), 6, off);
    PsMap_Merge(off);
    int no = 0;
    for (const PsMapSeam& e : off) if (Near(e.nx, -1.f)) ++no;
    CHECK(no == 2);

    // a real row still merges: same top plane, bit for bit, however many brushes it is built from
    std::vector<PsMapSeam> row;
    for (int i = 0; i < 6; ++i)
        PsMap_BrushSeams(Box(100.f, -300.f + i * 100.f, 0.f, 116.f, -200.f + i * 100.f, 63.9f).data(), 6, row);
    PsMap_Merge(row);
    int nr = 0;
    PsMapSeam rs{};
    for (const PsMapSeam& e : row) if (Near(e.nx, -1.f)) { ++nr; rs = e; }
    CHECK(nr == 1);
    CHECK(Near(rs.Len(), 600.f));
}

static void TestSamples()
{
    // one sample on anything shorter than the stride, and the ends are inset
    CHECK(PsMap_SampleCount(8.f, 32.f, 64) == 0);         // under k_psMapMinLen: nothing rides it
    CHECK(PsMap_SampleCount(16.f, 32.f, 64) == 1);
    // 200 - 16 = 184 usable. floor(184/32) + 1 = 6 leaves 184/5 = 36.8u between samples, WIDER
    // than the stride it was asked for -- the gap is the thing that covers the seam, so one more
    // sample is added whenever the spacing would overrun. 7 samples = 30.7u apart
    CHECK(PsMap_SampleCount(200.f, 32.f, 64) == 7);
    CHECK(PsMap_SampleCount(192.f + 16.f, 32.f, 64) == 7); // exactly 192 usable = 6 x 32: no extra
    CHECK(PsMap_SampleCount(100000.f, 32.f, 64) == 64);    // capped

    // the promise, over every length: no two neighbouring samples further apart than the stride
    for (float len = 16.f; len < 4096.f; len += 7.3f)
    {
        const int n = PsMap_SampleCount(len, 32.f, 512);
        if (n < 2) continue;
        const float gap = (PsMap_SampleT(1, n, len) - PsMap_SampleT(0, n, len)) * len;
        CHECK(gap <= 32.f + 1e-3f);
    }

    CHECK(Near(PsMap_SampleT(0, 1, 200.f), 0.5f));
    CHECK(Near(PsMap_SampleT(0, 6, 200.f), 0.04f));       // 8u inset on 200u
    CHECK(Near(PsMap_SampleT(5, 6, 200.f), 0.96f));
    // a short segment insets by a quarter, so both samples stay well inside the brush
    CHECK(Near(PsMap_SampleT(0, 2, 20.f), 0.25f));
    CHECK(Near(PsMap_SampleT(1, 2, 20.f), 0.75f));
    for (int n = 1; n <= 8; ++n)
        for (int i = 0; i < n; ++i)
        {
            const float t = PsMap_SampleT(i, n, 128.f);
            CHECK(t >= 0.0624f && t <= 0.9376f);
        }
}

// what the overlay actually draws: a run of confirmed samples -> one segment
static void TestRunSegment()
{
    // a 200u seam on the x = 100 wall, sampled 6 times (stride 32)
    PsMapSeam s; s.nx = -1.f; s.ny = 0.f; s.z = 64.f;
    s.ax = 100.f; s.ay = -100.f; s.bx = 100.f; s.by = 100.f;
    const int n = PsMap_SampleCount(s.Len(), 32.f, 64);
    CHECK(n == 7);

    float ax, ay, bx, by;
    // every sample confirmed -> the WHOLE seam is marked, clamped to its real ends
    PsMap_RunSegment(s, 0, n - 1, n, 32.f, ax, ay, bx, by);
    CHECK(Near(ax, 100.f) && Near(bx, 100.f));
    CHECK(Near(ay, -100.f) && Near(by, 100.f));

    // one sample in the middle -> 32u of line around it, not the whole seam
    PsMap_RunSegment(s, 3, 3, n, 32.f, ax, ay, bx, by);
    CHECK(Near(ay, -100.f + 200.f * (0.04f + 0.92f * 0.5f) - 16.f, 0.01f));
    CHECK(Near(by - ay, 32.f, 0.01f));

    // a run at one end only: marked there, and never past the brush's own end
    PsMap_RunSegment(s, 0, 1, n, 32.f, ax, ay, bx, by);
    CHECK(Near(ay, -100.f));                       // clamped, not 16u short of the brush
    CHECK(by < 0.f && by > -60.f);                 // ...and it stops well before the far end

    // a seam shorter than one stride: one sample, one segment, still inside the seam
    PsMapSeam t = s; t.ay = -8.f; t.by = 8.f;      // 16u long
    const int nt = PsMap_SampleCount(t.Len(), 32.f, 64);
    CHECK(nt == 1);
    PsMap_RunSegment(t, 0, 0, nt, 32.f, ax, ay, bx, by);
    CHECK(Near(ay, -8.f) && Near(by, 8.f));

    // reversed seam (b before a on the axis): the segment follows the seam, never inverts
    PsMapSeam r = s; r.ay = 100.f; r.by = -100.f;
    PsMap_RunSegment(r, 0, 0, n, 32.f, ax, ay, bx, by);
    CHECK(ay > by);
    CHECK(Near(ay, 100.f));
}

int main()
{
    TestBoxFaces();
    TestRunSegment();
    TestRefusals();
    TestMerge();
    TestSamples();
    std::printf("psmap_test: %d checks, %d failed\n", s_checks, s_failed);
    return s_failed ? 1 : 0;
}
