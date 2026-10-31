# botox (CS:GO) menu -> exact visual spec for porting to botox-tf2

Source read from `C:\Users\huber\Desktop\botox\csgo-sdk\`:
- `hacks\menu\menu.cpp`, `menu_internal.h`, `menu.h`
- `globals\render\render.cpp` (style setup, fonts, on_end_scene)
- `globals\fonts\fonts.cpp` (custom-font disk scan, NOT the menu font itself)
- `dependencies\imgui\imgui_widgets.cpp` (heavily modified: Checkbox, Slider, Combo, MultiCombo,
  Keybind, OptionPopup all custom-drawn), `dependencies\imgui\imgui.h` (ImAnimationHelper,
  RenderFadedGradientLine, ImGuiCol_Accent)

**IMPORTANT for the builder**: this is not a stock ImGui skin. The look comes from a *modified
ImGui core* (custom draw code inside `Checkbox`, `SliderScalar`, `BeginCombo`, `Keybind`,
`MultiCombo`, `OptionPopup` in `imgui_widgets.cpp`, plus new helpers `GetRowFrameLeft/Right/Width`,
`ImAnimationHelper`, `RenderFadedGradientLine`, and a new `ImGuiCol_Accent` style color in
`imgui.h`). botox-tf2's `src/dependencies/imgui/imgui_widgets.cpp` is presumably still close to stock. To get
pixel parity you must port these ImGui-core changes first, then the menu.cpp-level code on top.
Section 9 lists exactly what to copy vs adapt.

---

## 1. Window

- Size: fixed `ImVec2(750.f, 640.f - (background_height - bottom_band_height))` where
  `background_height = menu_band_height = 25.f`, `bottom_band_height = menu_bottom_band_height = 12.f`.
  So final size = `750 x 627`. Set every frame with `ImGuiCond_Always` (not resizable).
- Position: never set explicitly -> ImGui's default window position (first-use position), i.e.
  whatever imgui.ini / default placement gives it. No custom centering code in `on_end_scene`.
- Window name: `"botox-ui"`.
- Window flags: `ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize`,
  plus `ImGuiWindowFlags_NoInputs` while the open/close fade (`m_anim_progress`) is below `0.999f`.
- Background: window itself uses `ImGuiCol_WindowBg = (10,10,10,255)` (see style, section 5). On
  top of that the code manually paints two solid bands with `draw_list->AddRectFilled`:
  - **top band** (title bar), rect `[pos, pos+size.x, pos.y+25]`, color `ImColor(25/255,25/255,25/255,1.0)` = `#191919`,
    rounded with `ImGui::GetStyle().WindowRounding` (`5.f`), flags `ImDrawFlags_RoundCornersTop`. Clipped to that rect first.
  - **bottom band** (12 px), rect `[pos.y+size.y-12, pos.y+size.y]`, same color `#191919`, rounded
    `ImDrawFlags_RoundCornersBottom`, same rounding value.
  - Window border: `draw_list->AddRect(pos, pos+size, ImColor(50,50,50,255), WindowRounding)` i.e. `#323232` full alpha, 1px (no explicit thickness -> default 1.f).
  - A 1px "faded gradient" accent line directly under the top band (`position.y+background_height-1`)
    and directly above the bottom band, both via `RenderFadedGradientLine` (center-bright, fades to
    transparent at both edges horizontally — see function body below), colored with the live accent
    (`ImGuiCol_Accent`).
- Rounding: `WindowRounding = 5.f` (global style, see section 5).
- Border: `BorderSize` style vars are `0.f` everywhere (no native ImGui border rendering) — the
  50,50,50,255/50,50,50,100 borders you see are **hand-drawn** `AddRect` calls per element, not
  the `ImGuiCol_Border` style color (that color is set to fully transparent, `(0,0,0,0)`).
- Shadow/blur: none. No blur, no drop shadow anywhere in this code.
- DPI: the menu window is laid out entirely in ImGui logical pixels (cursor/`GetContentRegionAvail`
  etc.) so it rides the existing "dpi scale" system for free — **do not** read screen px
  (`g_ctx.m_width/m_height`) anywhere in the menu; follow the project rule in
  `feedback-new-panel-dpi-scale.md`: an `ImGui::Begin` window scales automatically as long as you
  never size/position from screen px.
- Title/logo: no image/logo — it's drawn text. Title text `"botox"` in
  `font_name_verdana_bd_11`, color solid white `ImColor(1,1,1,1)`. Immediately after it (no gap,
  same baseline), the build date string `" | <d> <mon> <year>"` (lowercase 3-letter month) in
  `font_name_verdana_11`, colored with the live accent color. Both centered as a unit:
  `title_x = pos.x + (size.x - title_w - date_w)/2`, `title_y = pos.y + (25 - title_h)/2`. The date
  comes from the PE timestamp of the module (`IMAGE_NT_HEADERS.FileHeader.TimeDateStamp`) via
  `localtime_s`, falling back to the `__DATE__` macro if that read fails — see
  `build_date_text()` in `menu.cpp` lines 22-45, copy verbatim if you want the same "today's build
  date" behavior, or just hardcode a label/version string if that's overkill for a personal DLL.

## 2. Tab bar — left sidebar, boxed buttons + nested sub-tab rows

Not a top tab bar: a **left sidebar** of stacked box buttons, one per top-level tab, with a tiny
flyout list of sub-pages opening under whichever tab is currently selected.

```cpp
constexpr float tab_width   = 100.f;
constexpr float tab_height  = 24.f;
constexpr float tab_gap     = 6.f;
constexpr float page_height = 17.f;

struct menu_tab_t { const char* m_name; std::vector<const char*> m_pages; };

static const menu_tab_t tabs[] = {
    { "aimbot", {} },
    { "visuals", { "esp", "world", "screen" } },
    { "movement", { "main", "indicators", "calculator", "recorder" } },
    { "misc", { "main", "player list" } },
    { "inventory", { "weapons", "player" } },
    { "fonts", { "indicators", "esp", "chud hud", "default hud" } },
    { "settings", {} },
};
```
(For TF2 rename the tab/page labels to whatever this DLL's feature set needs — the LAYOUT and
visual behavior below is what must be ported 1:1, not these exact CS:GO page names.)

- Tab buttons stack vertically starting at `tab_x = window content-left`, `tab_y = pos.y + 25`
  (right under the title band), each `100x24`, with a `6px` gap between buttons (gap applied
  *after* drawing: `tab_y = tab_max.y + tab_gap`).
- Tab box look: fill `ImColor(25/255,25/255,25/255,1)` = `#191919`, border `ImColor(50,50,50,100)`
  (i.e. `#323232` at ~39% alpha), both rounded with `WindowRounding - 2.f` (so `3.f`). Label is
  centered in the box, font `font_name_verdana_bd_11`.
- Hover/selected color blend: label color is `ImColor::Blend(white-at-hover-alpha, accent, lit)`
  where `lit` is a selected-animation value (0..1). Two independent `ImAnimationHelper` springs per
  tab: a *hover* spring (speed `2.f`, clamp `[0.5,1]`) drives label brightness, a *selected* spring
  (speed `2.f`, clamp `[0,1]`) drives the accent blend amount `lit`. When `lit>0` a 1px accent
  "faded gradient" underline is drawn across the bottom edge of the box
  (`RenderFadedGradientLine`), alpha scaled by `lit`.
- Sub-pages ("pages"): only drawn under the currently-open tab, as small centered text rows,
  `17px` tall + `1px` gap, directly below that tab's box (continuing the same `tab_y` cursor, which
  then pushes every tab below it further down while open). An **open/close slide animation**
  (`ImAnimationHelper`, id = hash(tab name)+hash("pages-open-animation"), speed `3.f`) drives both
  (a) a clip rect height (`clip_top..clip_bot = clip_top + page_count*(18)*open`) so the flyout
  visually grows/shrinks, and (b) each row's own hover/selected text-blend animations, with each
  row's final alpha multiplied by `open` so text fades in as the flyout opens. Font
  `font_name_verdana_11`. Selected page color = `ImColor::Blend(white, accent, page_selected_anim)`.
  Clicking a page only registers once `open >= 1.f`. `last_page[tab]` remembers which sub-page was
  last open per tab so re-opening a tab restores it.
- `eject` button: same exact box look (`tab_button` helper reused, `selected=false` always), but
  pinned to the sidebar's bottom: `tab_y = pos.y + size.y - WindowPadding.y - bottom_band_height - tab_height`.
  Clicking it opens a confirm popup (`save_popup`, see section 3b) with `yes`/`no` buttons; `yes`
  closes the menu (`g_menu.m_opened = false`) then sets an eject-request flag consumed by another
  thread (never tear down D3D state from inside this d3d-thread callback).
- Clicking anywhere requires `ImGui::IsWindowHovered()` to gate input (`can_click`), and is
  additionally blocked entirely while the window's open-animation hasn't fully finished
  (`window_flags |= ImGuiWindowFlags_NoInputs` while `m_anim_progress < 0.999f`).
- Page content area: `ImGui::Indent(tab_width + ItemSpacing.x)` (108px) then
  `SetCursorPosY(GetCursorPosY() + 25.f)`, then `ImGui::PushID(tab*16 + subtab)` so every page gets
  its own independent scroll state / group heights, then dispatch to the tab's draw function via a
  switch, then `PopID()` / `Unindent()`.
- Icons: none — tabs are text only, no glyph icons in the sidebar.
- Fonts used in the tab bar: `font_name_verdana_bd_11` (tab names, bold 11px), `font_name_verdana_11`
  (sub-page names, regular 11px).

## 3. Groups / sections

### 3a. Two-column auto-height group layout (`menu_internal.h` + `menu.cpp`, copy verbatim)

```cpp
constexpr float menu_band_height        = 25.f;
constexpr float menu_bottom_band_height = 12.f;

void menu_columns_begin( )
{
	const float width = ImFloor( ( ImGui::GetContentRegionAvail( ).x - ImGui::GetStyle( ).ItemSpacing.x ) / 2.f );
	ImGui::BeginChild( "left column", ImVec2( width, ImGui::GetContentRegionAvail( ).y - menu_bottom_band_height ), false, 0, false );
}

void menu_columns_next( )
{
	ImGui::EndChild( );
	ImGui::SameLine( );
	ImGui::BeginChild( "right column", ImVec2( 0.f, ImGui::GetContentRegionAvail( ).y - menu_bottom_band_height ), false, 0, false );
}

void menu_columns_end( )
{
	ImGui::EndChild( );
}

bool menu_group_begin( const char* name )
{
	const float height = ImGui::GetStateStorage( )->GetFloat( ImGui::GetID( name ), 40.f );
	return ImGui::BeginChild( name, ImVec2( 0.f, height ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse, true );
}

void menu_group_end( )
{
	ImGuiWindow* const group = ImGui::GetCurrentWindow( );

	/* clipped group skips items, its cursor never moves: keep the old height */
	if ( !group->SkipItems )
		group->ParentWindow->StateStorage.SetFloat(
			group->ChildId, ImMax( group->DC.CursorMaxPos.y - group->DC.CursorStartPos.y + group->WindowPadding.y * 2.f, 1.f ) );

	ImGui::EndChild( );
}
```

How a page uses it (`tab_aimbot` example, copy the pattern):
```cpp
menu_columns_begin( );
if ( menu_group_begin( "main" ) ) { /* widgets */ }
menu_group_end( );
menu_columns_next( );
if ( menu_group_begin( "configuration" ) ) { /* widgets */ }
menu_group_end( );
/* a 3rd group can be stacked in the right column by just calling group_begin/end again
   without another columns_next() call — see "grenade aimbot" group in tab_aimbot */
menu_columns_end( );
```
- Group = a bordered, header-less `BeginChild(name, size=(0, remembered_height), border=true,
  NoScrollbar|NoScrollWithMouse)`. There is no drawn "header" text/bar at all — RULE from
  `proj-menu-layout-1008.md`: **no small sub-headers, few big boxes** (user explicitly asked to
  remove separator headers). The child border itself (native ImGui child border, uses
  `ImGuiCol_Border`, which is transparent... actually note: `ChildBg` style color IS set, see
  section 5, so the group reads as a slightly-lighter-than-window rounded box, no visible
  separator line beyond that fill + whatever the native border draws).
- Height is NOT fixed: each group remembers its own last-frame content height in `StateStorage`
  keyed by the child's hashed id, and reuses that height next frame (auto-sizing child workaround,
  since ImGui children can't be true auto-height easily). Minimum height floor is `40.f` the first
  frame.
- Columns: exactly 2, each `floor((avail.x - ItemSpacing.x)/2)` wide, each column is its own
  scrolling (but scrollbar-less, mouse-wheel-scrollable) `BeginChild`, height = available height
  minus the 12px bottom band. A column can hold multiple stacked groups (just call
  `menu_group_begin/end` more than once inside one column before calling `menu_columns_next()` or
  `menu_columns_end()`).
- **RULE** (from `proj-menu-layout-1008.md` line 22): nothing inside a group may size itself off
  `GetContentRegionAvail().y` — that creates a feedback loop with the remembered-height mechanism.
- Padding: no custom padding override inside a group; it uses whatever `style.WindowPadding` is
  for the child (`ImVec2(8,8)`, see section 5) since `menu_group_begin` doesn't push a
  `ImGuiStyleVar_WindowPadding`.

### 3b. Popups used for settings sub-groups: `save_popup` / `OptionPopup`

Both are mini-popup-windows with the exact same chrome as the main window's top band (20px title
strip, `#191919` fill rounded top corners only, full-window border `#323232`-ish, accent faded
gradient line under the title strip, centered bold-11 title text that fades in/out with a hover
spring). `save_popup` is used for yes/no confirms (e.g. eject); `OptionPopup` is used for the
little gear-icon-triggered settings flyouts next to a checkbox (icon `font_name_icon_13`,
`cog_icon = "\xef\x80\x93"`, positioned at the row's right edge, right-click OR left-click opens
it). Copy both functions verbatim — see section 4 code blocks below (`OptionPopup` is in
`imgui_widgets.cpp`, `save_popup` is in `menu.cpp`).

## 4. Widget styles — copy verbatim

All of the following live in `imgui_widgets.cpp` and are **replacements of the stock ImGui
functions of the same name** (not new functions) — i.e. the project patched ImGui itself. See
section 9 for exactly what this means for the port.

### Checkbox (`imgui_widgets.cpp:1226`)
```cpp
bool ImGui::Checkbox( const char* label, bool* v )
{
	ImGuiWindow* window = GetCurrentWindow( );
	if ( window->SkipItems )
		return false;

	ImGuiContext& g         = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id        = window->GetID( label );
	const ImVec2 label_size = CalcTextSize( label, NULL, true );

	const float square_sz = GetFrameHeight( );
	const ImVec2 pos      = window->DC.CursorPos;
	const ImRect total_bb( pos, pos + ImVec2( square_sz + ( label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f ) + 2.f,
	                                          label_size.y + style.FramePadding.y * 2.0f ) );
	ItemSize( total_bb, style.FramePadding.y );
	if ( !ItemAdd( total_bb, id ) ) {
		IMGUI_TEST_ENGINE_ITEM_INFO( id, label,
		                             g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | ( *v ? ImGuiItemStatusFlags_Checked : 0 ) );
		return false;
	}

	bool hovered, held;
	bool pressed = ButtonBehavior( total_bb, id, &hovered, &held );
	if ( pressed ) { *v = !( *v ); MarkItemEdited( id ); }

	const ImRect check_bb( pos, pos + ImVec2( square_sz, square_sz ) );
	RenderNavHighlight( total_bb, id );

	auto frame_animation = ImAnimationHelper( id, ImGui::GetIO( ).DeltaTime );
	frame_animation.Update( *v ? 5.f : -5.f, 1.f, 0.f, 1.f );

	const bool render_decorations = frame_animation.AnimationData->second != 1.f;
	if ( render_decorations )
		RenderFrame( check_bb.Min, check_bb.Max, ImColor( 25 / 255.f, 25 / 255.f, 25 / 255.f ), true, style.FrameRounding );

	const ImColor accent_color = ImGui::GetColorU32( ImGuiCol_::ImGuiCol_Accent );
	RenderFrame( check_bb.Min, check_bb.Max,
	             ImColor( accent_color.Value.x, accent_color.Value.y, accent_color.Value.z, 1.f * frame_animation.AnimationData->second ), true,
	             style.FrameRounding );

	window->DrawList->AddRect( check_bb.Min, check_bb.Max, ImColor( 50, 50, 50, 100 ), style.FrameRounding );

	auto text_animation = ImAnimationHelper( id + 1u, ImGui::GetIO( ).DeltaTime );
	text_animation.Update( hovered || *v ? 4.f : -4.f, 1.f, 0.5f, 1.f );

	const ImColor text_color = GetColorU32( ImGuiCol_Text );
	PushStyleColor( ImGuiCol_Text,
	                ImVec4( text_color.Value.x, text_color.Value.y, text_color.Value.z, text_color.Value.w * text_animation.AnimationData->second ) );
	ImVec2 label_pos = ImVec2( check_bb.Max.x + style.ItemInnerSpacing.x + 2.f, check_bb.Min.y + style.FramePadding.y - 1.f );
	if ( label_size.x > 0.0f ) RenderText( label_pos, label );
	PopStyleColor( );

	return pressed;
}
```
Box fades between `#191919` (off) and the accent color (on), label text fades between 50% and
100% alpha on hover/checked. A static `#323232`-ish border (`50,50,50,100`) is always drawn on top.

### Slider (float/int share `SliderScalar`, `imgui_widgets.cpp:3665` — key excerpt, full body is
~150 lines, copy the whole function)
- Bar occupies the **full row frame width** (`GetRowFrameLeft()..GetRowFrameRight()`), height
  `GetFrameHeight()-2`. The row's *label* sits directly above the bar (own line), not beside it.
- Idle: `RenderFrame(frame_bb, ImColor(25/255,25/255,25/255,1), rounded=style.FrameRounding)` +
  `AddRect(frame_bb, ImColor(50,50,50,100), same rounding)`.
- The current-value text is drawn at the row's top-right as a clickable hit-box; clicking it (not
  the bar) switches into text-edit mode, which keeps the bar+fill visible underneath the typed
  input box and fills the bar up to the last known width using the accent color
  (`ImGuiCol_Accent`), animated width stored in a `static std::map<ImGuiID,float> old_value`.
- Normal (non-typing) drag: standard `SliderBehavior` computes `grab_bb`/`percent`; the fill from
  `frame_bb.Min` to `grab_bb` is painted with the accent color (see the `accent_color` pulled via
  `ImGui::GetColorU32(ImGuiCol_Accent)` at line 3790) — this part of the function continues past
  what's excerpted above; copy the full function body verbatim from
  `C:\Users\huber\Desktop\botox\csgo-sdk\dependencies\imgui\imgui_widgets.cpp` lines 3665-3880ish.
- No grab "knob" circle — it's a filled bar (accent) inside the `#191919`-filled, `#323232`-bordered
  track, i.e. a flat progress-bar-style slider, not a classic circular-handle slider.

### Combo (`BeginCombo`, `imgui_widgets.cpp:1810`)
```cpp
// box: #191919 fill (style.PopupRounding corners), #323232@~39% border (style.FrameRounding corners)
window->DrawList->AddRectFilled( frame_bb.Min, ImVec2( value_x2, frame_bb.Max.y ), ImColor( 25 / 255.f, 25 / 255.f, 25 / 255.f ), style.PopupRounding );
window->DrawList->AddRect( frame_bb.Min, ImVec2( value_x2, frame_bb.Max.y ), ImColor( 50, 50, 50, 100 ), style.FrameRounding );

// hand-drawn chevron arrow (2 lines forming a V), animates between down (closed) and up (open)
// via an ImAnimationHelper spring (speed 50, range [-r,r]), colored with the live accent
auto RenderArrow = [ & ]( ImVec2 pos, ImU32 col ) {
	const float h = 15 * 1.00f;
	float r       = h * 0.40f * 1.f;
	ImVec2 center = pos + ImVec2( h * 0.50f, h * 0.50f );
	auto arrow_animation = ImAnimationHelper( id, ImGui::GetIO( ).DeltaTime );
	arrow_animation.Update( popup_open ? 50.f : -50.f, 1.f, -r, r );
	ImVec2 a = ImVec2( +0.00f, -0.350f ) * arrow_animation.AnimationData->second;
	ImVec2 b = ImVec2( -0.666f, +0.350f ) * arrow_animation.AnimationData->second;
	ImVec2 c = ImVec2( +0.666f, +0.350f ) * arrow_animation.AnimationData->second;
	window->DrawList->AddLine( center + a, center + b, col );
	window->DrawList->AddLine( center + a, center + c, col );
};

// preview text fades between 50%/100% alpha on hover/popup-open, same pattern as checkbox label
// popup window itself: PopupBg pushed to #191919 (25/255), 0 scrollbar, accent-less #323232@39% border
```
Dropdown list items are stock `Selectable` (no extra skin beyond the popup background override
above). Combo frame height is `GetFrameHeight()+6` (taller than the default combo box).

### MultiCombo (`imgui_widgets.cpp:1974`, copy verbatim) — checkbox-list combo
Builds a comma-joined preview string of every checked item (word-wrapped with `...` if it
overflows the row width), falls back to `"none"` when nothing is selected; opens the same combo
chrome as above, and every entry inside is a `Selectable` with
`ImGuiSelectableFlags_DontClosePopups` so multiple can be toggled without the popup closing.

### Keybind (`imgui_widgets.cpp:2051`, copy verbatim incl. the 256-entry `key_names[]` table at
line 2031) — right-aligned on the SAME row as the row's label/checkbox (`SameLine(0,0)` then the
cursor is snapped to `GetRowFrameRight() - text_width`), shows `[key]` / `[...]` while capturing /
`[none]` when unbound, text fades between 50% alpha and the accent color via an
`ImAnimationHelper` keyed on active-id. Right-click opens a 3-option style popup (`always on` /
`hold` / `toggle`) sized `61x65`, positioned just right of the keybind text, bordered `#323232@39%`.

### Button (used for e.g. tab boxes and the yes/no inside `save_popup`) — this project does NOT
override `ImGui::Button` itself; tab boxes are hand-drawn (section 2), everything else is the
stock `ImGui::Button` using the style colors in section 5 (`ImGuiCol_Button = #141414`). **Project
rule**: every ordinary menu button is full-width `ImVec2(-1.f, 15.f)` on its own row, never two
buttons side by side via `SameLine` — see `feedback-menu-buttons-stacked.md`. The one allowed
exception already in the code is the yes/no pair inside `save_popup` confirm boxes.

### ColorEdit4 — two overloads exist (`imgui_widgets.cpp:5875` raw float[4], `:6055` wrapping the
project's own `c_color` type). Flags used everywhere in the menu:
```cpp
constexpr int color_picker_alpha_flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoOptions |
                                         ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip |
                                         ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder;
constexpr int color_picker_no_alpha_flags = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_NoInputs |
                                            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar |
                                            ImGuiColorEditFlags_NoBorder;
```
i.e. a small swatch button (no numeric input fields, no drag/drop, no tooltip, no built-in border
— box look matches the other widgets' `#191919`/`#323232` treatment via the same shared
RenderFrame styling used elsewhere) that opens a hue-bar picker popup on click.

### InputText — not overridden; stock ImGui `InputText` with the global style colors (there is a
custom `TempInputScalar`-based inline text box used only inside the slider's "typing" mode, which
IS custom, see the Slider section above).

## 5. Full ImGui style (`globals\render\render.cpp`, inside `on_end_scene`, `!m_initialised` block
— copy verbatim, runs once at startup)

```cpp
auto& style = ImGui::GetStyle( );

style.WindowRounding    = 5.f;
style.ChildRounding     = 0.f;
style.FrameRounding     = 2.f;
style.GrabRounding      = 3.f;
style.PopupRounding     = 3.f;
style.ScrollbarRounding = 0.f;

style.FrameBorderSize  = 0.f;
style.WindowBorderSize = 0.f;
style.PopupBorderSize  = 0.f;
style.ScrollbarSize    = 8.f;
style.GrabMinSize      = 0.f;

style.WindowPadding   = ImVec2( 8, 8 );
style.FramePadding    = ImVec2( 0, 0 );
style.ButtonTextAlign = ImVec2( 0.5f, 0.5f );
style.ItemSpacing     = ImVec2( 8, 8 );

style.AntiAliasedFill        = true;
style.AntiAliasedLines       = true;
style.AntiAliasedLinesUseTex = true;

style.Colors[ ImGuiCol_WindowBg ]  = ImVec4( 10 / 255.f, 10 / 255.f, 10 / 255.f, 1.f );   // #0A0A0A
style.Colors[ ImGuiCol_ChildBg ]   = ImVec4( 15 / 255.f, 15 / 255.f, 15 / 255.f, 1.f );   // #0F0F0F
style.Colors[ ImGuiCol_FrameBg ]   = ImVec4( 25 / 255.f, 25 / 255.f, 25 / 255.f, 1.f );   // #191919
style.Colors[ ImGuiCol_PopupBg ]   = ImVec4( 20 / 255.f, 20 / 255.f, 20 / 255.f, 1.f );   // #141414
style.Colors[ ImGuiCol_CheckMark ] = ImVec4( 0 / 255.f, 0 / 255.f, 0 / 255.f, 1.f );      // unused (custom-drawn checkbox doesn't use CheckMark)
style.Colors[ ImGuiCol_Button ]    = ImVec4( 20 / 255.f, 20 / 255.f, 20 / 255.f, 1.f );   // #141414

style.Colors[ ImGuiCol_Border ]       = ImVec4( 0 / 255.f, 0 / 255.f, 0 / 255.f, 0.f );   // transparent — all visible borders are hand-drawn AddRect calls
style.Colors[ ImGuiCol_BorderShadow ] = ImVec4( 0 / 255.f, 0 / 255.f, 0 / 255.f, 0.f );
```
Everything else is whatever `ImGui::StyleColorsDark()` (or whatever the project's existing default
is) leaves in place — only the above lines are explicitly touched. All remaining colors (Text,
TextDisabled, ScrollbarGrab, etc.) are stock dark-theme defaults; if botox-tf2's current style
differs materially from stock dark, diff against a fresh `StyleColorsDark()` call to find exactly
what else needs overriding, but these are the only lines the botox CS:GO menu itself sets.

### New style slot: `ImGuiCol_Accent`
`imgui.h` adds a new enum value `ImGuiCol_Accent` to `ImGuiCol_` (right after the standard ImGui
colors, before `ImGuiCol_COUNT`) purely so the live user-configurable accent color can be pushed
through the normal `GetColorU32(ImGuiCol_Accent)` / `style.Colors[]` plumbing. It is written every
frame in `on_end_scene`:
```cpp
ImGui::GetStyle( ).Colors[ ImGuiCol_::ImGuiCol_Accent ] = GET_VARIABLE( g_variables.m_accent, c_color ).get_vec4( );
```
Porting this requires adding the same enum slot to botox-tf2's `imgui.h` `ImGuiCol_` enum (it must
sit before `ImGuiCol_COUNT` so `style.Colors[]`, sized by `ImGuiCol_COUNT`, has room for it) — see
section 9.

## 6. Fonts

Menu text uses exactly 3 embedded fonts (all via `AddFontFromMemoryCompressedTTF`, stb-compressed
binary blobs stored as byte arrays in `dependencies\imgui\helpers\fonts.h` in the botox repo —
copy that header, or at minimum the `verdana_compressed_data`/`verdana_compressed_size`,
`verdana_bold_compressed_data`/`verdana_bold_compressed_size`, and
`icon_font_compressed_data`/`icon_font_compressed_size` symbols it defines):

| enum | file / array | size | used for |
|---|---|---|---|
| `font_name_verdana_11` | `verdana_compressed_data` | 11px | sub-page labels, date text, popup body text |
| `font_name_verdana_bd_11` | `verdana_bold_compressed_data` | 11px | title "botox", tab names, popup titles |
| `font_name_icon_13` | `icon_font_compressed_data` | 13px, custom glyph range `{0xe005,0xf8ff,0}` | gear/cog icon only (`cog_icon = "\xef\x80\x93"`), used by `OptionPopup` |

Build call pattern (`globals\render\render.cpp` `build_fonts()`):
```cpp
ImFontConfig verdana_font_config = { };
verdana_font_config.FontBuilderFlags =
	ImGuiFreeTypeBuilderFlags_Monochrome | ImGuiFreeTypeBuilderFlags_MonoHinting;

fonts[ font_name_verdana_11 ] =
	atlas->AddFontFromMemoryCompressedTTF( verdana_compressed_data, verdana_compressed_size, 11.f, &verdana_font_config );

fonts[ font_name_verdana_bd_11 ] =
	atlas->AddFontFromMemoryCompressedTTF( verdana_bold_compressed_data, verdana_bold_compressed_size, 11.f, &verdana_font_config );

static const ImWchar icon_ranges[] = { 0xe005, 0xf8ff, 0 };
fonts[ font_name_icon_13 ] =
	atlas->AddFontFromMemoryCompressedTTF( icon_font_compressed_data, icon_font_compressed_size, 13.f, 0, icon_ranges );
```
The `merge_extended_glyphs(...)` calls after the verdana fonts pull in system fallback fonts
(segoeui/arial/tahoma/etc for non-ASCII glyph coverage) — not required for a pure-ASCII TF2 menu
unless you want the same unicode robustness; skip them for a lean port.

Font atlas setup: `atlas->TexDesiredWidth = 4096; atlas->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;`
then at the end `ImGuiFreeType::BuildFontAtlas(atlas, 0x0)` (the project uses the FreeType backend,
not stb_truetype, for sharper hinting — confirm botox-tf2 already links `imgui_freetype.cpp`/`.h`;
if not, stb_truetype's default rasterizer is an acceptable fallback but glyphs will look slightly
different, especially with the `Monochrome|MonoHinting` flags which only affect FreeType).

If embedding the exact same Verdana binary is undesirable, note Verdana is a Windows system font —
the botox repo also has a `system_font_path("verdana.ttf")`-based fallback path used elsewhere
(`merge_extended_glyphs`) proving the DLL can load system Verdana directly via
`AddFontFromFileTTF` instead of the embedded compressed blob, if copying the binary blob is not
wanted.

## 7. Accent color, animations, cursor, open/close key

- **Accent color**: single user-configurable `c_color g_variables.m_accent`, written into
  `style.Colors[ImGuiCol_Accent]` once per frame (section 5). Every hover/selected/active state
  across every widget blends between white-ish and this one accent color — there is no second
  accent/secondary color anywhere in the menu.
- **Animation primitive**: `ImAnimationHelper` (`imgui.h:180-205`) — a tiny spring stored in a
  global `std::map<unsigned int, float> mapAnimations` keyed by a widget id (so it persists frame
  to frame without extra state). `Update(speed, scale=1, min=0, max=1)` moves the stored float by
  `speed * scale * deltaTime` per frame, clamped to `[min,max]`, and returns the new value. Every
  hover-fade / selected-fade / open-close effect in this spec uses this exact helper — copy it
  verbatim into the TF2 ImGui fork's `imgui.h`.
- **Gradient line primitive**: `RenderFadedGradientLine` (`imgui.h:3790`) — an
  `AddRectFilledMultiColor` split into two halves (transparent->solid->transparent) to make a
  horizontal line that's brightest in the middle and fades to nothing at both ends; used for every
  accent underline/divider in the menu (top-band divider, bottom-band divider, tab selected
  underline, popup title divider). Copy verbatim.
- **Menu open/close fade**: `n_menu::impl_t::m_anim_progress`, linear in real time,
  `step = min(deltaTime, 1/30) / menu_anim_duration` where `menu_anim_duration = 0.12f`, moves
  toward `1` while `m_opened` else toward `0`, clamped `[0,1]`. The whole window (and every window
  it "owns" — popups opened from inside it) is rendered into separate draw-list buckets and
  re-submitted through `ImGui_ImplDX9_RenderDrawDataFaded` at `fade = m_anim_progress` so the
  entire menu (not just alpha-blended per-widget) fades in/out as one unit, including nested
  popups which get their own independent fade-out "ghost" draw list so a popup that was open when
  you close the menu doesn't just vanish — it fades out of its last drawn state
  (`popup_fade_t`/`fade_draw_lists` machinery in `menu.cpp` lines 463-572; this is the most complex
  single piece of code in the file — copy verbatim if the open/close fade-through-popups behavior
  matters, otherwise a plain window-alpha fade without the popup-ghost handling is an 80%-as-good
  simplification: ponytail note — the ghost-popup fade is a real corner case (closing the menu
  while a settings-gear popup is open) that most users will never trigger; a straight `m_opened ?
  fade in : fade out` on just the root window covers the common path for far less code, add the
  ghost machinery back only if testing shows popups "popping" shut looks bad).
  Eject is a special case: it snaps `m_anim_progress` to `0` instantly (no fade) because the DLL is
  about to unload and must not render a menu frame while unloading starts.
- **Cursor**: no custom cursor code found in the read files — menu relies on whatever
  `ImGui_ImplWin32`/the project's normal cursor handling already does; nothing menu-specific to
  port here.
- **Open/close key**: not in any of the files read for this spec (`menu.cpp`/`menu_internal.h`
  only define `m_opened`/`m_tab`/`m_subtab` state and the `on_end_scene` draw, not the key-bind
  that flips `m_opened`). If botox-tf2 needs the exact same keybind, grep
  `g_menu.m_opened\s*=` across the wider `csgo-sdk` tree (outside the 7 files reviewed here) to
  find the toggle hook before assuming a specific key.

## 8. Assets

- **No image assets** are used by the menu chrome itself (no logo PNG, no background texture). The
  only `D3DXCreateTextureFromFileInMemory` calls near the menu init code load `terrorist_avatar_data`
  / `counter_terrorist_avatar_data` (CS:GO team default avatars) for an unrelated feature (likely
  player-list/ESP avatars), not the menu UI — do not port those for a TF2 menu.
- Every visual element in the menu (title text, tab boxes, dividers, checkbox/slider/combo chrome)
  is hand-drawn via `ImDrawList` primitives (`AddRectFilled`, `AddRect`, `AddText`, `AddLine`,
  `AddRectFilledMultiColor`) and fonts — zero bitmap assets.
- Embedded font binaries live in `dependencies\imgui\helpers\fonts.h` (verdana/verdana-bold/icon
  font compressed arrays) — this is the one "asset" file to copy, see section 6.

## 9. Copy wholesale vs adapt

**Copy wholesale (byte-for-byte, these are ImGui-core patches, not menu-specific code):**
- `ImAnimationHelper` struct + `mapAnimations` (add to `imgui.h`, near the other helper structs).
- `RenderFadedGradientLine` (add to `imgui.h`, near other inline render helpers).
- New `ImGuiCol_Accent` enum value in `ImGuiCol_` (add to `imgui.h`, before `ImGuiCol_COUNT`).
- `ImGui::GetRowLabelOffset / GetRowFrameLeft / GetRowFrameRight / GetRowFrameWidth / IndentRow /
  Label` (add to `imgui_widgets.cpp` near the existing layout helpers — these are new functions,
  not stock-ImGui overrides, so they're pure additions, lowest risk).
- `ImGui::Checkbox` override (replace the fork's existing `Checkbox` body with this one).
- `ImGui::SliderScalar` override (replace; also update `SliderScalarN`/vertical-slider call sites
  if they exist, they're unaffected since this port only touches the scalar path).
- `ImGui::BeginCombo` override (replace).
- `ImGui::MultiCombo` (new function — copy, plus its `std::vector<bool>`/`std::vector<const
  char*>` signature; requires `<vector>`/`<string>` includes already in the stock widgets file).
- `ImGui::Keybind` + the 256-entry `key_names[]` table (new function — but the TF2 fork needs its
  own `key_bind_t` / `EKeyStyles` types; check botox-tf2's existing keybind storage type before
  copying — this will need adapting to whatever key-bind struct botox-tf2 already uses, since the
  function reads/writes `key_data->m_key` / `m_key_style` directly).
- `ImGui::OptionPopup` (new function — copy verbatim, needs `std::function<void()>` include).
- `ImGui::ColorEdit4` flags constants (`color_picker_alpha_flags` / `color_picker_no_alpha_flags`)
  — copy into the TF2 menu's internal header.
- `menu_columns_begin/next/end`, `menu_group_begin/end` — copy verbatim into TF2's `menu.cpp` /
  `menu_internal.h`-equivalent (`menu_chrome.h`/`menu_flow.h` per the task description).
- `save_popup` — copy verbatim (depends on `ImAnimationHelper`/`RenderFadedGradientLine`/
  `ImGuiCol_Accent`/font lookups above).
- The style-setup block in section 5 — copy verbatim into TF2's `SetupImGuiStyle` in `render.cpp`.
- The 3-font build calls in section 6 — copy verbatim (adjust the embedded font header path to
  wherever TF2 places it).

**Adapt (same shape, different names/types/content):**
- The `tabs[]` table in `on_end_scene` — same struct shape and exact layout constants
  (`tab_width/height/gap`, `page_height`), but TF2's tab/page names and `switch` dispatch to
  TF2's own tab-draw functions (`tab_aimbot`/`tab_visuals`/etc -> whatever TF2's feature tabs are).
- `build_date_text()` — optional; cosmetic, can be dropped or kept as-is (it's self-contained, no
  CS:GO-specific dependencies).
- The window-background/title/band drawing block inside `on_end_scene` (lines ~593-831 in
  `menu.cpp`) — same constants and same draw calls, but ported into TF2's `menu.cpp`
  structure/own window name instead of `"botox-ui"`.
- The open/close fade machinery (`popup_live`, `fade_draw_lists`, `menu_owns_window`,
  `window_drawn`) — same algorithm, needs TF2's own root-window name
  (`ImGui::FindWindowByName("botox-ui")` -> TF2's menu window name) and its own render-submit path
  (`ImGui_ImplDX9_RenderDrawDataFaded` — confirm this DX9-fade variant already exists in
  botox-tf2's `imgui_impl_dx9.cpp`/`.h`; if not, it needs porting too since it's required for the
  whole-window fade to work. It's referenced in `render.cpp:1766` — grep
  `ImGui_ImplDX9_RenderDrawDataFaded` in the CS:GO repo's `imgui_impl_dx9.cpp` if this function body
  is needed and wasn't captured by this read).
- `n_variables`-style `GET_VARIABLE(...)` macro usage throughout every tab's widget calls — this is
  botox's generic variable-store plumbing; TF2's `menu.cpp` should keep using whatever
  variable-access pattern it already has (direct globals, its own macro, etc.) — only the ImGui
  *call* (`ImGui::Checkbox("label", &GET_VARIABLE(...))`) and its surrounding layout matter for
  visual parity, not the storage macro.
- `chams_dropdown`, `modulation_row`, `draw_color_correction_curves`, `reset_color_correction`,
  `image_pick_apply` — these are feature-specific widgets (CS:GO chams materials, color-correction
  curve editor, etc.), not generic menu chrome. Port only if TF2 has an equivalent feature; the
  *patterns* they demonstrate (custom combo preview building, `GetRowFrameWidth`-sized custom
  canvas widgets with accent-colored plot lines) are reusable templates for any future TF2-side
  custom widget, not something to port 1:1.

**Do not port:**
- `terrorist_avatar_data`/`counter_terrorist_avatar_data` texture loads (CS:GO team avatars,
  unrelated to menu chrome).
- Anything inside `tab_aimbot`'s weapon-config table (CS:GO weapon categories) — TF2 has entirely
  different weapon/class structure; only the *widget layout pattern* (group with a combo selecting
  a sub-config block, conditional extra sliders/multicombo below it) is reusable.
