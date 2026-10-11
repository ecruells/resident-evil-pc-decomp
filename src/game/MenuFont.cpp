// MenuFont - see MenuFont.h. File layout is documented in tools/make_menu_font.py.
#include "MenuFont.h"
#include "../Globals.h"
#include "Types.h"
#include "FileLoader.h"
#include "../system/AssetPath.h"
#include <string.h>
#include <ctype.h>

// Texture slot / bank used for the font page. The title uses slots 12 and 13
// (banks 8 and 9); 14 / 10 are free on the title screen.
#define MENUFONT_SLOT 14
#define MENUFONT_BANK 10
#define MENUFONT_MAX_FILE (256 * 1024)

static bool          s_loaded = false;
static unsigned char s_cellW, s_cellH, s_first, s_count, s_tall, s_space;
static unsigned char s_adv[256];

bool MenuFont_Loaded(void) { return s_loaded; }

bool MenuFont_Load(void)
{
    static unsigned char buf[MENUFONT_MAX_FILE];
    s_loaded = false;

    size_t n = LoadFile(GAME_DATA_ROOT "data\\menufont.fnt", buf, 0x20);
    if (n == (size_t)-1 || n < 0x6C + 8 || memcmp(buf, "MFNT", 4) != 0) return false;

    s_cellW = buf[4]; s_cellH = buf[5]; s_first = buf[6]; s_count = buf[7];
    s_tall = buf[8];  s_space = buf[9];
    if (s_cellW == 0 || s_cellH == 0 || s_count == 0 || 0x0C + (size_t)s_count > n) return false;
    memset(s_adv, 0, sizeof s_adv);
    memcpy(s_adv, buf + 0x0C, s_count);

    // The TIM follows the advance table: LoadTexturePage takes it as-is.
    const size_t timOff = 0x0C + s_count;
    if (timOff + 8 > n) return false;
    LoadTexturePage(buf + timOff, MENUFONT_BANK, 0, MENUFONT_SLOT, 0, 0, 0, 0);
    s_loaded = true;
    return true;
}

static int GlyphIndex(char c)
{
    int code = toupper((unsigned char)c);
    if (code == ' ') return -2;
    int i = code - s_first;
    if (i < 0 || i >= (int)s_count || s_adv[i] == 0) return -1;
    return i;
}

bool MenuFont_CanDraw(const char* text)
{
    if (!s_loaded || text == NULL) return false;
    for (const char* p = text; *p; p++)
        if (GlyphIndex(*p) == -1) return false;
    return true;
}

int MenuFont_Width(const char* text)
{
    int w = 0;
    for (const char* p = text; *p; p++) {
        int i = GlyphIndex(*p);
        w += (i == -2) ? s_space : (i >= 0 ? s_adv[i] : 0);
    }
    return w;
}

void MenuFont_Draw(int x, int y, const char* text, unsigned char brightness)
{
    if (!s_loaded) return;
    TextureDesc* td = &g_TextureDesc;
    for (const char* p = text; *p; p++) {
        int i = GlyphIndex(*p);
        if (i == -2) { x += s_space; continue; }
        if (i < 0) continue;

        td->flags     = (brightness != 0x80) ? 0x40000000 : 0x10000000;
        td->texturePage = MENUFONT_BANK;
        td->texU      = (unsigned char)((i % 16) * s_cellW);
        td->texV      = (unsigned char)((i / 16) * s_cellH);
        td->width     = s_cellW;
        td->height    = s_cellH;
        td->screenX   = (short)x;
        td->screenY   = (short)y;
        td->colorMulR = td->colorMulG = td->colorMulB = brightness;
        td->clutX = 0; td->clutY = 0x1E0;
        td->pivotX = 0; td->pivotY = 0;
        display_texture(td, 2, MENUFONT_SLOT, 1);
        x += s_adv[i];
    }
}
