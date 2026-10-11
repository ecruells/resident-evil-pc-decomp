// Difficulty.h - ini-driven difficulty modes for the NEW GAME submenu.
//
// Modes are read from difficulty.ini (see data/difficulty.ini). The title
// screen builds its submenu from this list, and the game code asks the
// accessors below for the numbers to apply. With the default ini the first
// three entries are the Director's Cut trio (STANDARD / TRAINING / ADVANCED),
// so the DC build behaves exactly as before.
//
// In the default (OG/USA) build the chosen mode is stored in g_DifficultyId,
// NOT in g_DcDifficulty / g_DcGameMode, because the OG save loader refuses
// saves whose dcGameMode is non-zero.
#pragma once

#define DIFF_MAX_MODES 8
#define DIFF_NAME_LEN  24

struct DifficultyMode {
    char name[DIFF_NAME_LEN]; // text shown in the menu (upper-cased)
    int  dcDifficulty;        // DC_DIFFICULTY_* used in DC mode (0..2)
    int  healthChris;         // starting health, absolute; 0 = leave as the game sets it
    int  healthJill;          // same, for Jill
    int  weaponDamagePct;     // player weapon damage, 100 = unchanged
    int  enemyDamagePct;      // damage taken by the player, 100 = unchanged (not hooked yet)
    int  ammoPct;             // starting handgun ammo, 100 = unchanged
};

// Selected mode (index into the list). Saved with the game in OG builds.
extern int g_DifficultyId;

// Load difficulty.ini (searched in the asset folders). Safe to call again;
// falls back to the built-in STANDARD/TRAINING/ADVANCED trio if the file is
// missing or has no valid modes.
void Difficulty_Reload(void);

// 1 if the submenu should be shown ([Menu] Enabled=1 and more than one mode).
int  Difficulty_MenuEnabled(void);

int  Difficulty_Count(void);
const DifficultyMode* Difficulty_Get(int id);   // NULL if out of range
const DifficultyMode* Difficulty_Active(void);  // mode for g_DifficultyId (never NULL)
int  Difficulty_DefaultId(void);                // [Menu] Default, clamped

// Save-file field. The mode index rides in the otherwise unused tail of the
// original 0x800 block (0x43D..0x7FF is zero-filled on save and discarded on
// load), so the save file keeps its 0xA82-byte size and size-checking tools
// (save_editor.html) keep working. Layout: 'D' 'I' 'F' <mode index>.
#define DIFF_SAVE_OFFSET 0x7FC

// Write the field into a save image (call after the block is assembled).
void Difficulty_WriteSave(char* fileBuffer);

// Set g_DifficultyId from a loaded save image. A save without the field (made
// before it existed) loads as mode 0, or in the DC build as the first mode
// whose Dc= matches the save's own DC difficulty, so an old TRAINING save keeps
// TRAINING health.
void Difficulty_ReadSave(const char* fileBuffer, int fileSize);

// Values for game code. All fall back to "no change" when no mode applies.
int  Difficulty_StartHealth(int charId);        // 0 = leave the game's value; charId 0 = Chris, 1 = Jill
int  Difficulty_ScaleWeaponDamage(int dmg);
int  Difficulty_ScaleAmmo(int qty);
int  Difficulty_EnemyDamagePct(void);
