// Gate test for the menu's group box flow (src/hacks/menu/menu_chrome.h) against a REAL
// ImGui context -- no window, no device, no game: a context, a font atlas, and the same
// call sequence RenderMenu makes, run for a few frames.
//
// The layout math has its own test (menuflow_test). What only shows up once ImGui is in
// the loop is everything this one checks:
//   * the frame is balanced -- every BeginChild, PushStyleColor, PushStyleVar and
//     PushItemWidth the flow takes gets given back. ImGui asserts on a leak at End(),
//     so a frame that finishes at all is the check (asserts are live: NDEBUG is off).
//   * the cached heights CONVERGE. A box is sized from last frame's measurement, so a
//     measure that disagrees with what it measures next frame oscillates forever.
//   * a box's height really tracks its content: more rows in, taller box out.
//   * a box that GREW is corrected on the following frame (the one-frame clip).
//   * boxes never overlap, and never leave the content panel on either side.
//   * a box scrolled out of view (SkipItems, nothing submitted) keeps its cached height
//     instead of collapsing to an empty strip and staying there.
//
// Build + run: cmake --build build --config Release --target menuchrome_test && build\Release\menuchrome_test.exe
#include "../src/hacks/menu/menu_chrome.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

ImFont* g_fontBold = nullptr;   // the dll gets this from dllmain.cpp; here the default font
ImFont* g_menuReg  = nullptr;   // botox menu stage 2: verdana 11 chrome fonts, same deal
ImFont* g_menuBold = nullptr;

static int s_failed = 0, s_checks = 0;
#define CHECK(cond) do { ++s_checks; if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++s_failed; } } while (0)
static bool Near(float a, float b, float eps = 0.01f) { return (a - b) <= eps && (b - a) <= eps; }

struct Box { std::string title; int rows; };

static std::vector<mf::Rect> s_lastDrawn;   // what the flow placed on the last frame
static mf::Rect s_lastContent{};

// One menu-shaped frame: window -> panel -> header strip -> content child -> the flow.
// The rect arithmetic is RenderMenu's, so a change there that breaks the frame shows up
// here too.
static int s_preambleRows = 0;   // widgets a tab body draws above its first box

static void Frame(const std::vector<Box>& boxes, float winW = 700.f, float winH = 520.f, float scrollY = -1.f)
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(40.f, 30.f));
    ImGui::SetNextWindowSize(ImVec2(winW, winH));
    ImGui::Begin("dih", NULL, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 wz = ImGui::GetWindowSize();
    const float fh = ImGui::GetFrameHeight();
    const mf::Rect panel{ mf::kPanelPad, mf::kPanelPad,
                          wz.x - mf::kPanelPad * 2.f, wz.y - mf::kPanelPad * 2.f };
    const mf::Rect header{ panel.x + mf::kInset, panel.y + mf::kInset,
                           panel.w - mf::kInset * 2.f, mf::kHeaderH };
    const float contentTop = header.bottom() + mf::kInset;
    const float contentAsk = panel.bottom() - mf::kInset - fh - mf::kInset - contentTop;
    const mf::Rect content{ header.x, contentTop, header.w, contentAsk > 40.f ? contentAsk : 40.f };
    s_lastContent = content;

    ImGui::SetCursorPos(ImVec2(content.x, content.y));
    ImGui::BeginChild(ImGui::GetID("body"), ImVec2(content.w, content.h), true);
    if (scrollY >= 0.f) ImGui::SetScrollY(scrollY);
    ImGui::PushID(0);
    FlowBegin();
    for (int i = 0; i < s_preambleRows; ++i)
        ImGui::TextUnformatted("a line above the boxes");
    for (const Box& b : boxes)
    {
        Section(b.title.c_str());
        for (int i = 0; i < b.rows; ++i)
        {
            static bool v = false;
            char lbl[64];
            std::snprintf(lbl, sizeof(lbl), "option %d##%s%d", i, b.title.c_str(), i);
            ImGui::Checkbox(lbl, &v);
        }
    }
    FlowEnd();
    s_lastDrawn = g_flow.drawn;
    ImGui::PopID();
    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();
}

static bool Overlap(const mf::Rect& a, const mf::Rect& b)
{
    return a.x < b.right() - 0.01f && b.x < a.right() - 0.01f &&
           a.y < b.bottom() - 0.01f && b.y < a.bottom() - 0.01f;
}

static float HeightOf(const std::vector<mf::Rect>& v, size_t i) { return i < v.size() ? v[i].h : -1.f; }

// `menuchrome_test dump` prints the converged layout as text, for looking at the shape
// the flow actually produces without starting the game.
static void Dump(const std::vector<Box>& boxes, const std::vector<mf::Rect>& r)
{
    std::printf("\ncontent panel %.0fx%.0f at (%.0f,%.0f)\n",
                s_lastContent.w, s_lastContent.h, s_lastContent.x, s_lastContent.y);
    for (size_t i = 0; i < r.size(); ++i)
        std::printf("  %-14s col %d  x %5.0f  y %5.0f  %4.0fx%-4.0f  rows %2d\n",
                    i < boxes.size() ? boxes[i].title.c_str() : "?",
                    r[i].x < s_lastContent.w * 0.4f ? 0 : 1,
                    r[i].x, r[i].y, r[i].w, r[i].h,
                    i < boxes.size() ? boxes[i].rows : -1);
    float bot[2] = { 0.f, 0.f };
    for (const mf::Rect& b : r)
    {
        const int c = b.x < s_lastContent.w * 0.4f ? 0 : 1;
        if (b.bottom() > bot[c]) bot[c] = b.bottom();
    }
    std::printf("  columns end at %.0f / %.0f (panel is %.0f tall)\n\n",
                bot[0], bot[1], s_lastContent.h);
}

int main(int argc, char** argv)
{
    const bool dump = (argc > 1 && std::strcmp(argv[1], "dump") == 0);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920.f, 1080.f);
    io.DeltaTime = 1.f / 60.f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    io.Fonts->TexID = (ImTextureID)(intptr_t)1;   // no renderer: any non-null id will do
    CHECK(io.Fonts->IsBuilt());

    // Style: botox_menu_spec.md section 5, verbatim (render.cpp SetupImGuiStyle) -- the
    // real values the flow is measured against now, not this fork's old tighter ones.
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowPadding = ImVec2(8.f, 8.f);
    st.ItemSpacing = ImVec2(8.f, 8.f);
    st.FramePadding = ImVec2(0.f, 0.f);
    st.ChildBorderSize = 1.f;

    const std::vector<Box> boxes{
        { "watermark", 4 }, { "media player", 2 }, { "indicators", 9 },
        { "velocity", 5 }, { "crosshair", 14 }, { "killfeed", 1 }, { "scoreboard", 3 },
    };

    // ---- frame 1: nothing is measured yet -----------------------------------------
    // botox's own menu_group_begin default (botox_menu_spec.md section 3a) is 40.f, not
    // this fork's old 120 first-frame guess -- Section() now opens the box through that
    // literal function, so it inherits its literal default.
    Frame(boxes);
    CHECK(s_lastDrawn.size() == boxes.size());
    for (const mf::Rect& r : s_lastDrawn)
        CHECK(Near(r.h, 40.f));                       // menu_group_begin's own default, every box


    // ---- frame 2: every box is its measured height ---------------------------------
    Frame(boxes);
    const std::vector<mf::Rect> f2 = s_lastDrawn;
    CHECK(f2.size() == boxes.size());

    // ---- frame 3: and it does not move again (the cache converged) ------------------
    Frame(boxes);
    const std::vector<mf::Rect> f3 = s_lastDrawn;
    CHECK(f3.size() == f2.size());
    for (size_t i = 0; i < f3.size() && i < f2.size(); ++i)
    {
        CHECK(Near(f3[i].h, f2[i].h, 0.51f));
        CHECK(Near(f3[i].x, f2[i].x, 0.51f));
        CHECK(Near(f3[i].y, f2[i].y, 0.51f));
    }

    if (dump) Dump(boxes, f3);

    // ---- the geometry of the converged frame ---------------------------------------
    {
        // every box inside the content panel, in one of the two columns.
        const float margin = st.WindowPadding.x;
        const float colW = mf::ColumnWidth(s_lastContent.w - st.WindowPadding.x * 2.f);
        for (const mf::Rect& r : f3)
        {
            CHECK(r.w > 0.f);
            CHECK(Near(r.w, colW, 1.01f));
            CHECK(r.x >= margin - 0.01f);
            CHECK(r.right() <= s_lastContent.w - margin + 1.01f);
            CHECK(r.h >= mf::kSecMinH);
        }
        // no two boxes share a pixel
        for (size_t i = 0; i < f3.size(); ++i)
            for (size_t j = i + 1; j < f3.size(); ++j)
                CHECK(!Overlap(f3[i], f3[j]));
        // both columns used, and the taller one is not runaway (masonry balance)
        float bottom[2] = { 0.f, 0.f };
        int used[2] = { 0, 0 };
        for (const mf::Rect& r : f3)
        {
            const int c = (r.x < margin + colW * 0.5f) ? 0 : 1;
            used[c]++;
            if (r.bottom() > bottom[c]) bottom[c] = r.bottom();
        }
        CHECK(used[0] > 0 && used[1] > 0);

        // Height tracks content, and does so LINEARLY in the row count. The row pitch is
        // read off two of the boxes rather than assumed, so the check survives a font or
        // a spacing change and still fails if a box stops measuring what is in it.
        const float h1 = HeightOf(f3, 5);             // "killfeed",    1 row
        const float h9 = HeightOf(f3, 2);             // "indicators",  9 rows
        const float h14 = HeightOf(f3, 4);            // "crosshair",  14 rows
        const float pitch = (h9 - h1) / 8.f;
        CHECK(pitch > 10.f && pitch < 30.f);          // one checkbox row, whatever the font
        CHECK(Near(h14, h1 + pitch * 13.f, 1.01f));
        CHECK(Near(HeightOf(f3, 0), h1 + pitch * 3.f, 1.01f));    // "watermark", 4 rows
        // a one-row box is small -- a loose ceiling, not a tight one: no notch to budget
        // for any more (botox's group box has no title strip, spec section 3a), but the
        // exact overhead is the checkbox row's own frame metrics, not asserted here.
        CHECK(h1 < pitch * 2.f + st.WindowPadding.y * 2.f + 10.f);
    }

    // ---- a box that grows is corrected on the NEXT frame ---------------------------
    {
        std::vector<Box> grown = boxes;
        grown[5].rows = 12;                            // killfeed: 1 -> 12 rows
        const float before = HeightOf(f3, 5);
        Frame(grown);                                  // drawn at the stale height
        const float during = HeightOf(s_lastDrawn, 5);
        Frame(grown);                                  // and corrected here
        const float after = HeightOf(s_lastDrawn, 5);
        CHECK(Near(during, before, 1.51f));            // one frame of the old height
        CHECK(after > before + 10.f * 10.f);           // then the real one
        Frame(grown);
        CHECK(Near(HeightOf(s_lastDrawn, 5), after, 1.51f));   // stable again
    }

    // ---- a culled box keeps its height ---------------------------------------------
    // Thirty boxes in a short window: whatever is past the bottom edge is clipped by the
    // content child, has SkipItems set and submits nothing. Such a box must keep the
    // height it last measured instead of caching itself down to an empty strip -- which
    // is what the `visible` guard in SectionClose is for. A box that has never been in
    // view yet is a different thing (it sits at the first-frame guess until it is), so
    // the run below scrolls the whole list into view first and only then culls it again.
    {
        std::vector<Box> many;
        for (int i = 0; i < 30; ++i)
        {
            char t[32];
            std::snprintf(t, sizeof(t), "box %d", i);
            many.push_back(Box{ t, 3 + (i % 5) });
        }
        // SetScrollY lands on the NEXT frame, so each position gets two.
        const float kBottom = 4000.f;                  // past the end: ImGui clamps it
        for (int i = 0; i < 2; ++i) Frame(many, 700.f, 520.f, 0.f);
        for (int i = 0; i < 3; ++i) Frame(many, 700.f, 520.f, kBottom);
        for (int i = 0; i < 3; ++i) Frame(many, 700.f, 520.f, 0.f);

        const std::vector<mf::Rect> measured = s_lastDrawn;
        CHECK(measured.size() == many.size());
        for (const mf::Rect& r : measured)
            CHECK(r.h > mf::kSecMinH + 10.f);          // every box has a real height by now

        // cull the far half again, come back, and nothing may have moved
        for (int i = 0; i < 2; ++i) Frame(many, 700.f, 520.f, kBottom);
        for (int i = 0; i < 3; ++i) Frame(many, 700.f, 520.f, 0.f);
        CHECK(s_lastDrawn.size() == measured.size());
        for (size_t i = 0; i < s_lastDrawn.size() && i < measured.size(); ++i)
        {
            if (!Near(s_lastDrawn[i].h, measured[i].h, 0.51f))
                std::printf("  box %d: was %.1f now %.1f (y %.1f, col x %.1f)\n",
                            (int)i, measured[i].h, s_lastDrawn[i].h, measured[i].y, measured[i].x);
            CHECK(Near(s_lastDrawn[i].h, measured[i].h, 0.51f));
            CHECK(Near(s_lastDrawn[i].y, measured[i].y, 0.51f));
        }
    }

    // ---- the minimum window still lays out --------------------------------------
    // 520x380 is what RenderMenu's SetNextWindowSizeConstraints allows, so this is the
    // narrowest column the menu can ever be asked to draw.
    {
        Frame(boxes, 520.f, 380.f);
        Frame(boxes, 520.f, 380.f);
        CHECK(s_lastDrawn.size() == boxes.size());
        for (const mf::Rect& r : s_lastDrawn)
        {
            CHECK(r.w > 200.f);                        // a column still holds a slider + value
            CHECK(r.right() <= s_lastContent.w + 1.01f);
            CHECK(r.h >= mf::kSecMinH);
        }
    }

    // ---- a tab body may draw above its first box -----------------------------------
    // The combat tab's "not loaded" line and the skins tab's weapon picker sit there.
    // The columns start at the first Section, so the boxes land UNDER the preamble
    // instead of over it.
    {
        Frame(boxes);
        const float topY = HeightOf(s_lastDrawn, 0) > 0.f ? s_lastDrawn[0].y : -1.f;
        s_preambleRows = 3;
        Frame(boxes);
        Frame(boxes);
        CHECK(s_lastDrawn.size() == boxes.size());
        CHECK(s_lastDrawn[0].y > topY + 10.f);          // pushed down by the three lines
        for (size_t i = 0; i < s_lastDrawn.size(); ++i)
            for (size_t j = i + 1; j < s_lastDrawn.size(); ++j)
                CHECK(!Overlap(s_lastDrawn[i], s_lastDrawn[j]));
        // a tab that draws no box at all must not be touched by the flow either
        s_preambleRows = 1;
        Frame(std::vector<Box>{});
        CHECK(s_lastDrawn.empty());
        s_preambleRows = 0;
    }

    // ---- Section outside a flow falls back to the plain header ---------------------
    // render.cpp's ps point window calls it that way, and it must not open a child that
    // nothing will close.
    {
        ImGui::NewFrame();
        ImGui::Begin("point", NULL, ImGuiWindowFlags_NoTitleBar);
        const size_t before = g_flow.drawn.size();
        Section("point");                              // g_flow.inFlow is false here
        ImGui::Checkbox("something", new bool(false));
        CHECK(g_flow.drawn.size() == before);          // no box was placed
        CHECK(!g_flow.open);
        ImGui::End();
        ImGui::Render();
    }

    // ---- RenderMenu's own numbers (botox_menu_spec.md section 1/2/9) ----------------
    // Pure arithmetic, no ImGui frame needed -- these are exactly the formulas
    // menu.cpp's RenderMenu uses for the window, the top band, the first sidebar tab,
    // the tab pitch and the content indent.
    {
        const float winW = 750.f, winH = 640.f - (25.f - 12.f);
        CHECK(Near(winW, 750.f));
        CHECK(Near(winH, 627.f));
        CHECK(Near(mf::kHeaderH, 25.f));                         // top band: y 0..25
        CHECK(Near(mf::kTabW, 100.f) && Near(mf::kTabH, 24.f));  // tab box 100x24
        CHECK(Near(mf::kTabH + mf::kTabGapY, 30.f));              // tab pitch, spaced 30

        // first tab box: x = WindowPadding.x, y = 25 + WindowPadding.y
        const float tabX = st.WindowPadding.x;
        const float tabY = mf::kHeaderH + st.WindowPadding.y;
        CHECK(Near(tabX, 8.f));
        CHECK(Near(tabY, 33.f));

        // content x = WindowPadding.x + (tab_width + ItemSpacing.x) = WindowPadding.x + 108
        const float contentX = st.WindowPadding.x + (mf::kTabW + st.ItemSpacing.x);
        CHECK(Near(contentX, st.WindowPadding.x + 108.f));
        CHECK(Near(contentX, 116.f));

        // two columns, each floor((avail - ItemSpacing.x) / 2)
        const float availW = winW - contentX - st.WindowPadding.x;
        const float colW = mf::ColumnWidth(availW);
        CHECK(Near(colW, floorf((availW - st.ItemSpacing.x) * 0.5f)));
    }

    // ---- RenderMenu's sidebar -> content order, as a real frame ---------------------
    // Regression: SidebarTab_Draw is an InvisibleButton (moves the cursor), eject is drawn
    // last at the window floor, and the content used to start from wherever that left the
    // cursor -- past the bottom of the 627px window, every tab blank. RenderMenu now saves
    // the cursor before the sidebar and restores it; this replays that sequence.
    {
        const float winW = 750.f, winH = 627.f;
        static const char* kPages[3] = { "esp", "world", "screen" };
        float childTop = -1.f, childH = -1.f, afterEject = -1.f;
        for (int f = 0; f < 3; ++f)   // flyout springs settle over frames; layout must not care
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(100.f, 100.f));
            ImGui::SetNextWindowSize(ImVec2(winW, winH));
            ImGui::Begin("botox", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollWithMouse);
            const ImVec2 contentStart = ImGui::GetCursorPos();
            float tabY = mf::kHeaderH + st.WindowPadding.y;
            int page = 2, noPage = -1;
            for (int i = 0; i < 7; ++i)
                SidebarTab_Draw(SidebarTab{ "tab", i == 1 ? kPages : nullptr, i == 1 ? 3 : 0 }, i, i == 1,
                                i == 1 ? page : noPage, st.WindowPadding.x, tabY, io.DeltaTime);
            float ejectY = winH - st.WindowPadding.y - 12.f - mf::kTabH;
            SidebarTab_Draw(SidebarTab{ "eject", nullptr, 0 }, 7, false, noPage, st.WindowPadding.x, ejectY, io.DeltaTime);
            afterEject = ImGui::GetCursorPosY();

            ImGui::SetCursorPos(contentStart);
            ImGui::Indent(mf::kTabW + st.ItemSpacing.x);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + mf::kHeaderH);
            ImGui::BeginChild("##content", ImVec2(0.f, ImGui::GetContentRegionAvail().y - 12.f), false,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
            childTop = ImGui::GetWindowPos().y - 100.f;
            childH = ImGui::GetWindowSize().y;
            ImGui::EndChild();
            ImGui::Unindent(mf::kTabW + st.ItemSpacing.x);
            ImGui::End();
            ImGui::Render();
        }
        CHECK(afterEject > winH - 12.f - mf::kTabH);              // the trap: sidebar leaves the cursor at the floor
        CHECK(Near(childTop, st.WindowPadding.y + mf::kHeaderH));  // content starts under the title band (y 33)
        CHECK(childH > winH * 0.8f);                               // and has the window's height to draw in
        CHECK(childTop + childH <= winH - 12.f + 0.01f);           // clipped above the bottom band
    }

    // ---- named BeginChild draws nothing and shifts nothing -------------------------------
    // Regression: the fork's BeginChildEx used to render the child's name + a rule line and push
    // the cursor down/right (Indent 10) -- every menu child (##content, ##mc_left/right, each
    // group) got a stray line, a top gap and a 10px left shift.
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(50.f, 50.f));
        ImGui::SetNextWindowSize(ImVec2(400.f, 300.f));
        ImGui::Begin("probe", NULL, ImGuiWindowFlags_NoTitleBar);
        ImGui::BeginChild("##probechild", ImVec2(200.f, 100.f), false, 0);
        const ImVec2 cur = ImGui::GetCursorScreenPos(), org = ImGui::GetWindowPos();
        const int cmds = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        CHECK(Near(cur.x, org.x) && Near(cur.y, org.y));   // borderless child: content at its origin
        CHECK(cmds == 0);                                   // and nothing drawn by BeginChild itself
    }

    // ---- botox widget bodies (imgui fork) --------------------------------------------
    {
        const float grabBackup = st.GrabMinSize;
        st.GrabMinSize = 0.f;   // render.cpp SetupImGuiStyle / botox: 0
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(50.f, 50.f));
        ImGui::SetNextWindowSize(ImVec2(300.f, 300.f));
        ImGui::Begin("widgets", NULL, ImGuiWindowFlags_NoTitleBar);

        // Regression: float sliders drew no fill. GrabMinSize 0 made grab_bb zero-wide and the
        // fill was gated on its width. botox fills from the 0-100 percent instead.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImRect track(p, ImVec2(p.x + 200.f, p.y + 10.f));
        const float lo = 0.f, hi = 1.f;
        float v = 1.f; int pct = -1; ImRect gb;
        ImGui::SliderBehavior(track, ImGui::GetID("s"), ImGuiDataType_Float, &v, &lo, &hi, "%.2f", 0, &gb, &pct);
        CHECK(pct == 100);
        CHECK(gb.GetWidth() > 199.f);                     // grab rect = the fill, track start -> value
        v = 0.5f;
        ImGui::SliderBehavior(track, ImGui::GetID("s"), ImGuiDataType_Float, &v, &lo, &hi, "%.2f", 0, &gb, &pct);
        CHECK(pct == 50);
        const float zlo = 3.f, zhi = 3.f; float z = 3.f;   // empty range: no divide by zero
        ImGui::SliderBehavior(track, ImGui::GetID("z"), ImGuiDataType_Float, &z, &zlo, &zhi, "%.2f", 0, &gb, &pct);
        CHECK(pct == 0);

        // Regression: a 3-arg ColorEdit4 drew imgui's R:/G:/B:/A: drag readouts. botox's is one
        // text row (label left, swatch right) -- the inputs would make the row a frame tall.
        float c[4] = { 1.f, 0.05f, 0.05f, 0.6f };
        const float y0 = ImGui::GetCursorPosY();
        ImGui::ColorEdit4("enemy color##t", c);
        CHECK(Near(ImGui::GetCursorPosY() - y0, ImGui::GetTextLineHeight() + st.ItemSpacing.y));

        // botox Button: -1 wide = row frame width - 4, bb starts 2px in (frame drawn 2px outside)
        const float rowW = ImGui::GetRowFrameWidth(), rowL = ImGui::GetRowFrameLeft();
        ImGui::Button("full##t", ImVec2(-1.f, 15.f));
        CHECK(Near(ImGui::GetItemRectSize().x, rowW - 4.f));
        CHECK(Near(ImGui::GetItemRectMin().x, rowL + 2.f));

        ImGui::End();
        ImGui::Render();
        st.GrabMinSize = grabBackup;
    }

    ImGui::DestroyContext();

    std::printf(s_failed ? "menuchrome_test: %d/%d FAILED\n" : "menuchrome_test: %d/%d ok\n",
                s_failed ? s_failed : s_checks, s_checks);
    return s_failed ? 1 : 0;
}
