#pragma once
#include "../../globals/globals.h"

// botox watermark styles, ported 1:1: layout, colours, position, animation AND the donor's own
// font (render.cpp BuildFontAtlas = botox build_fonts, same face/px/FreeType flags per style).
// g_wmStyle = botox's own index so numbering matches the donor exactly:
//   0 default (watermark.cpp body, verdana 11 / verdana bd 11)
//   1 kamibebra  rubik 13          12 skebob      nunito 14
//   2 clarity    verdana bd 11     13 cumidere    tahoma 16 light
//   3 clarity v2 inter semibold 14 14 illusory    tahoma 13 mono+bold
//   4 delusional tahoma 12 mono    15 lumi        rubik 16 mono
//   5 interwebz  calibri 16 mono   16 billware    tahoma bd 12 mono
//   6 havoc-1337 tahoma 13 mono    17 dna         montserrat 15
//   7 onetap     tahoma 12 mono    18 dna clarity pt root bold 15 + icon 50
//   8 airflow    sf ui display 14  19 cucumber    tahoma 14 (verdana 14 measures)
//   9 ev0lve     segoe ui 16       20 howeweware  minecraft 14
//  10 legendware verdana 12 mono   21 gamesense   sf pro bold 15 @ 13
//  11 interium   droid bold 24 @ 16
// Shared knob set (feedback-new-wm-spec-same-knobs): label/fps/ping/loss/time + text colour;
// where botox reads its menu accent for a style, g_wmTextColor stands in. Donor menu-only
// extras (typing animations, per-style colour pickers, lumi rain design, dna/lumi glow on
// trick/spot -- no TF2 concept) are skipped.
// Any index this function does not implement falls through (returns false), same as index 0.
bool RenderWatermarkStyled();

// botox misc.h media_look_t + misc.cpp media_player_look / media_player_frame (+ the donor
// halves in spectator_list.cpp): what the media player borrows from the current watermark
// style -- box or bare, title/artist colours, text effect (0 none, 1 drop shadow, 2 outline,
// 3 diagonal shadow), and the style's box frame grown to the player's rect.
struct MediaLook
{
    bool  box    = true;
    ImU32 title  = IM_COL32(255, 255, 255, 255);
    ImU32 artist = IM_COL32(146, 144, 145, 255);
    int   fx     = 0;
};
MediaLook Watermark_MediaLook(int style);
void      Watermark_MediaFrame(ImDrawList* dl, ImVec2 min, ImVec2 max, int style);

// botox gamesense_frame + gamesense_bar, shared with the gamesense spectator list
void Watermark_GamesenseFrame(ImDrawList* dl, ImVec2 min, ImVec2 max, bool bar);
