#include "menu_misc.h"
#include "menu.h"   // MenuSection / MenuGroupToggle -- shared, exported wrappers only

// Every widget misc HUD used to draw here (hit marker, death particles, score popup,
// spectator list, watermark style, media player, discord rpc, console color, debug
// menu) now lives inline in menu.cpp's Page_MiscMain -- botox's own tab_misc is one
// function, not a unit-per-row call chain, so the botox group boxes/cogs for "overlays"
// and "game" needed single-function ownership of their checkboxes. Kept as a no-op so
// the MENU_SLOT call site stays valid.
void Menu_Misc_Main()
{
}
