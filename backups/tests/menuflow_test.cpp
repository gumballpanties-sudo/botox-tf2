// Gate test for the menu's layout math (src/hacks/menu/menu_flow.h) -- the numbers the reworked
// catalyst-style menu places its panels, group boxes, accent rules and header tabs with.
//
// What it is here to catch, one case each:
//   * a group box whose notch does not cover the body's top border (the notch is the only
//     thing that cuts that border open -- miss it by a pixel and every box has a line through
//     its title)
//   * a notch wider than the box it sits in (a long section title used to run past the right
//     border and out of the panel)
//   * the accent rule at a narrow width: fade*2 > w makes the middle segment NEGATIVE, which
//     draws as an inverted rect across the whole panel
//   * a tab strip that does not end flush with the panel's right padding, or whose labels
//     overlap -- the strip is laid out right to left, so an off-by-one there shifts every tab
//   * a section measured from a body height and back (SectionHeight/SectionBodyHeight are the
//     two directions of ONE relation and the flow calls both every frame)
//
// Build + run: cmake --build build --config Release --target menuflow_test && build\Release\menuflow_test.exe
#include "../src/hacks/menu/menu_flow.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

int main()
{
    using namespace mf;

    // ---- two columns ---------------------------------------------------------------
    // Both columns plus the gap fill the panel exactly, so nothing hangs over the right
    // border and no dead strip is left down the middle.
    {
        const float avail = 636.f;                       // 680 window - 2*10 pad - 2*6 inset - 2*1 border
        const float cw = ColumnWidth(avail);
        CHECK(Near(cw * 2.f + kColGap, avail));
        CHECK(cw > 0.f);
        // a panel narrower than the gap cannot give a negative column
        CHECK(ColumnWidth(4.f) == 0.f);
        CHECK(ColumnWidth(0.f) == 0.f);
    }

    // ---- masonry ------------------------------------------------------------------
    {
        CHECK(PickColumn(0.f, 0.f) == 0);                // empty: left first
        CHECK(PickColumn(120.f, 40.f) == 1);             // right is shorter
        CHECK(PickColumn(40.f, 120.f) == 0);
        CHECK(PickColumn(80.f, 80.f) == 0);              // tie: left, and it stays left
        // A run of boxes must never leave one column more than the tallest box behind.
        float col[2] = { 0.f, 0.f };
        const float hs[] = { 90.f, 140.f, 60.f, 200.f, 75.f, 110.f, 45.f };
        float tallest = 0.f;
        for (float h : hs)
        {
            const int c = PickColumn(col[0], col[1]);
            col[c] += h + kSecGap;
            if (h > tallest) tallest = h;
        }
        CHECK(std::fabs(col[0] - col[1]) <= tallest + kSecGap);
    }

    // ---- group box: notch vs body border ------------------------------------------
    {
        const Rect sec{ 16.f, 44.f, 314.f, 160.f };
        const Rect body = SectionBody(sec);

        // body sits half a title down and ends with the section -- the box's own bottom
        CHECK(Near(body.y, sec.y + kSecTitleH * 0.5f));
        CHECK(Near(body.bottom(), sec.bottom()));
        CHECK(Near(body.x, sec.x) && Near(body.w, sec.w));

        // the notch has to straddle that top border line: start above it, end below it
        const Rect notch = SectionNotch(sec, 52.f);
        CHECK(notch.y < body.y);
        CHECK(notch.bottom() > body.y);
        // and stay inside the box horizontally
        CHECK(notch.x > sec.x);
        CHECK(notch.right() < sec.right());
        // the title itself is drawn at the panel padding, INSIDE the notch's pad
        CHECK(notch.x < sec.x + kPanelPad);
        CHECK(notch.right() > sec.x + kPanelPad + 52.f);

        // a title longer than the box is clamped, not run out of the panel
        const Rect wide = SectionNotch(sec, 4000.f);
        CHECK(wide.right() <= sec.right() + 0.01f);
        // and a box too narrow for any title gives a zero-width notch, not a negative one
        const Rect tiny = SectionNotch(Rect{ 0.f, 0.f, 8.f, 40.f }, 30.f);
        CHECK(tiny.w >= 0.f);
    }

    // ---- section height round trip -------------------------------------------------
    {
        for (float bodyH : { 40.f, 77.f, 120.5f, 400.f })
        {
            const float secH = SectionHeight(bodyH);
            CHECK(Near(SectionBodyHeight(secH), bodyH));
            // a box is always taller than the body it wraps, by exactly the half title
            CHECK(Near(secH - bodyH, kSecTitleH * 0.5f));
        }
        // a box with nothing in it still has to be tall enough to draw its own notch
        CHECK(SectionHeight(0.f) >= kSecTitleH);
        CHECK(SectionBodyHeight(0.f) > 0.f);             // never a negative child height
    }

    // ---- accent rule ---------------------------------------------------------------
    {
        const FadeLine l = AccentLine(20.f, 300.f);
        CHECK(Near(l.fadeW, 300.f * kFadeRatio));
        CHECK(Near(l.solidX, 20.f + l.fadeW));
        CHECK(Near(l.fadeW * 2.f + l.solidW, 300.f));    // the three pieces are the width
        CHECK(Near(l.endX + l.fadeW, 320.f));            // and they end at the right edge

        // narrow panel: the two fades would overlap. No negative middle, still ends flush.
        for (float w : { 6.f, 2.f, 1.f, 0.5f })
        {
            const FadeLine n = AccentLine(0.f, w);
            CHECK(n.solidW >= 0.f);
            CHECK(n.fadeW * 2.f <= w + 0.01f);
            CHECK(Near(n.endX + n.fadeW, w));
        }
        // zero width draws nothing at all
        const FadeLine z = AccentLine(50.f, 0.f);
        CHECK(z.fadeW == 0.f && z.solidW == 0.f);

        // the sub-tab underline is the same rule at a wider fade
        const FadeLine u = Underline(10.f, 80.f);
        CHECK(Near(u.fadeW, 80.f * 0.3f));
        CHECK(Near(u.fadeW * 2.f + u.solidW, 80.f));
    }

    // ---- header tab strip ----------------------------------------------------------
    {
        // dih's own six, measured at 12px tahoma (order = the tab enum)
        const float w[6] = { 20.f, 40.f, 56.f, 40.f, 24.f, 28.f };
        const int n = 6;
        const float right = 654.f;                       // panel right edge minus padding

        // last tab ends flush with the right edge, first starts a strip's width back
        CHECK(Near(TabX(right, w, n, n - 1) + w[n - 1], right));
        CHECK(Near(TabX(right, w, n, 0), right - TabStripWidth(w, n)));

        // in order, one gap apart, never overlapping
        for (int i = 1; i < n; ++i)
        {
            const float prevEnd = TabX(right, w, n, i - 1) + w[i - 1];
            CHECK(TabX(right, w, n, i) > prevEnd);
            CHECK(Near(TabX(right, w, n, i) - prevEnd, kTabGap));
        }

        // one tab on its own is just right-aligned
        const float one[1] = { 33.f };
        CHECK(Near(TabX(right, one, 1, 0), right - 33.f));
        CHECK(Near(TabStripWidth(one, 1), 33.f));
        CHECK(TabStripWidth(one, 0) == 0.f);
    }

    // ---- the whole frame fits -------------------------------------------------------
    // Window -> panel (inset) -> header + content, the way RenderMenu lays it out. The
    // point of the check is that the content panel cannot come out with a negative
    // height at the minimum window size the menu allows.
    {
        const float winW = 580.f, winH = 440.f;          // catalyst's own min size
        const float footerH = 26.f;
        const float panelW = winW - kPanelPad * 2.f;
        const float panelH = winH - kPanelPad * 2.f;
        const float innerW = panelW - kInset * 2.f;
        const float contentH = panelH - kInset * 3.f - kHeaderH - footerH - kInset;
        CHECK(innerW > 0.f);
        CHECK(contentH > 0.f);
        const float cw = ColumnWidth(innerW - kPanelPad * 2.f);
        CHECK(cw > 120.f);                               // a column still fits a slider + value
    }

    std::printf(s_failed ? "menuflow_test: %d/%d FAILED\n" : "menuflow_test: %d/%d ok\n",
                s_failed ? s_failed : s_checks, s_checks);
    return s_failed ? 1 : 0;
}
