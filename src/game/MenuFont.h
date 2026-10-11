// MenuFont - optional bitmap font for the title menus.
//
// If DATA\MENUFONT.FNT exists (JPN\Data\ or USA\Data\, or inside the DC / an
// add-on tree - the normal asset layering finds it) the ini-driven difficulty
// menu is drawn with it. Build the file from the Director's Cut's own
// BT367OAB.TIM with tools/make_menu_font.py. Without the file nothing changes:
// the menu is drawn by the engine's normal 8x14 text routine.
//
// The sheet only contains the letters the DC prints, so a string is drawn with
// this font only when EVERY character of it is available; otherwise the caller
// falls back to the normal routine for that string.
#pragma once

bool MenuFont_Load(void);                    // call once after the title textures are up
bool MenuFont_Loaded(void);
bool MenuFont_CanDraw(const char* text);     // case-insensitive; spaces always ok
int  MenuFont_Width(const char* text);       // pixels
// x = left edge, y = top, in the same screen units as the DC title sprites
// (320x240, origin centre-left as UpdateTitleTextSprite uses). brightness 0x80 = normal.
void MenuFont_Draw(int x, int y, const char* text, unsigned char brightness);
