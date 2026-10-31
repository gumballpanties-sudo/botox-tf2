#pragma once
#include "../../globals/globals.h"

// botox spectator list styles, ported 1:1 (layout/colours/animation AND the donor's own font:
// render.cpp BuildFontAtlas = botox build_fonts, each Draw* names its botox font_name_*),
// g_specStyle = the order the styles appear in botox's spectator_list.cpp:
//   31 gamesense -- DONE (frame/bar/font shared with the gamesense watermark)
//   32 kamidere  -- not ported (donor: gif + blur + per-row avatar panel), falls to default
//   0 default (n_misc::impl_t::draw_spectator_list)    -- DONE, accordion slide+fade panel
//   1 delusional (draw_spectator_list_delusional)      -- DONE, bare rows left/right of wm
//   2 interwebz (draw_spectator_list_interwebz)         -- DONE, retro beveled frame
//     (drag-to-reposition skipped -- menu-only interaction, not a drawn knob; position
//      falls back to g_specPos like every other style)
//   3 winxp (draw_spectator_list_winxp)                 -- DONE, Notepad skeuomorph
//   4 clarity (draw_spectator_list_clarity)              -- DONE, dark gradient panel
//   9 cumhacck, 11 airplane, 13 millionware, 14 aimware, 15 havoc, 16 onetap,
//   19 legendware, 20 interium, 23 cumidere, 24 illusory, 25 lumi (interwebz sub-style
//   only -- lumi_new needs avatar+blur), 26 billware, 27 inkabanium, 28 dna,
//   29 cucumber -- DONE, draw-list only (no ImGui::Begin windows, no drag-to-reposition,
//   same AnchorPos as every other style). All of botox's "all spectators, not just
//   mine" collection, per-row obs_mode text (fp/tp/roam), and text_color_two branches
//   collapse to text_color_one here -- CollectSpectators only has the local-observer
//   scan (see above), so every row this DLL ever draws IS a local-watcher row.
//   5 chillware, 6 lobotomy, 7 kamibebra, 8 eyes, 10 clarity_v2, 12 bhopcheat,
//   17 airflow, 18 evolve, 22 skebob, 30 howeweware -- DONE, CollectSpectatorRows()
//   (avatar/gif/blur infra: avatar_cache.h, gif_tex.h, blur_rect.h). Donor's bezier link
//   lines (chillware), per-row procedural eye texture (eyes) and "all players" target
//   column are simplified away -- see each Draw*'s own comment.
//   21 chillware_v2 -- DONE, the one real ImGui::Begin() window (auto-resize, draggable
//   while the menu is open) -- every other style here stays a background-draw-list panel.
// Netvars: m_iObserverMode/m_hObserverTarget on DT_BasePlayer (source-sdk-2013-master,
// game/server/player.cpp: 8184-8185), resolved lazily, self-contained in this file.
void RenderSpectatorList();
