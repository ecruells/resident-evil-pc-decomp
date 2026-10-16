#include "ZombieModeInternal.h"

// ROOM60C0's chemical event becomes a shared visual transition. Its persistent
// solved flag is set at USE, rather than after the single-player cutscene.
// Every loaded copy supplies its own water effects, model tint and vine death;
// no camera, player pose, position or control flags are changed.
extern int cmd_effect_clear_typed(void);
extern int cmd_effect_spawn(void);
extern void scd_model_tint_apply(short, short, short, unsigned short, unsigned short, unsigned char);

static bool s_started;
static bool s_solvedOnEntry;
static unsigned int s_startedAt;
static int s_waterSteps;

void zm_greenhouse_room(void)
{
    s_started = false;
    s_waterSteps = 0;
    s_solvedOnEntry = Flg_ck((int)g_ScenarioFlags2, 0xA6) != 0;
}

void zm_greenhouse_frame(void)
{
    if (!zombie_mode_armed() || g_stageId != STAGE_MANSION_RETURN_1F ||
        g_roomId != ROOM_GREENHOUSE || Flg_ck((int)g_ScenarioFlags2, 0xA6) == 0) return;
    if (g_roomTransitionBusy || (g_main_state_flags & (MSF_ROOM_TRANSITION | MSF_MENU_ACTIVE))) return;
    if (!s_started) {
        s_started = true;
        s_startedAt = zm_game_time_ms();
        // Native event 1's two red water effects, without clearing unrelated
        // room effects or changing the camera/player. Commands retain the
        // original effect parents, flags and positions.
        unsigned char clearA[] = { 0x42, 0x13, 0, 0 };
        unsigned char clearB[] = { 0x42, 0x1B, 0, 0 };
        unsigned char waterA[] = { 0x2A, 0x13, 8, 0, 3, 0x1F, 0x26, 0xF8, 0x38, 0x15, 0, 0 };
        unsigned char waterB[] = { 0x2A, 0x1B, 8, 0, 4, 0x1F, 0xEA, 0xF7, 0x4A, 0x15, 0, 0 };
        unsigned char* saved = g_ScdOpcodes;
        g_ScdOpcodes = clearA; cmd_effect_clear_typed();
        g_ScdOpcodes = clearB; cmd_effect_clear_typed();
        g_ScdOpcodes = waterA; cmd_effect_spawn();
        g_ScdOpcodes = waterB; cmd_effect_spawn();
        g_ScdOpcodes = saved;
        // The native poison variant's final colour, on all six vines. Their
        // regular controllers then run the withering sequence independently.
        scd_model_tint_apply(2, -3, 0, 0, 0x100, 0x0F);
    }
    // Native water ramp: ten +2 red steps eight 33 ms frames apart. Already
    // solved on arrival: restore its final appearance immediately.
    unsigned int elapsed = zm_game_time_ms() - s_startedAt;
    int steps = s_solvedOnEntry ? 10 : (int)(elapsed / (8u * 33u)) + 1;
    if (steps > 10) steps = 10;
    while (s_waterSteps < steps) {
        scd_model_tint_apply(2, 0, 0, 0, 0xFF, 0x80);
        s_waterSteps++;
    }
}
