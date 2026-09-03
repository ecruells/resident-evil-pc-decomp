// ComputerArms.cpp - em1014 / em1015, the player's forearms at the lab terminal.
//
// Entity types 20 and 21 (0x00427330 and 0x0040b760 in the original) are not
// enemies: they are the two hands you see typing while the room-5060 computer
// state is up. They are spawned into enemy slots 0 and 1 by the room script and
// driven entirely by ComputerLab.cpp, which writes a command into
// Entity::behavior_flags and polls bit 0x20 for "command finished".
//
// Command protocol (behavior_flags):
//   bit 0x80  hidden / parked - the driver does nothing
//   bit 0x40  new command; the driver consumes it, clears it, and resets the
//             sub-state so the command starts from the top
//   bit 0x20  set BY the arm when the command completes; the terminal polls it
//   bits 0-3  the command index
//
// Dispatch. The original has one table at 0x004ba5a0 for the entity state
// (only 0 = init and 1 = run exist), a 16-entry command table at 0x004ba5a8,
// and a shared step chain at 0x004ba5c8. Commands 1..7 each point at a tiny
// stub that re-dispatches on the sub-state into the chain at a DIFFERENT
// BASE - verified from the jump instructions:
//   cmd 1 -> [ecx*4 + 0x4ba5c8]   chain + 0
//   cmd 2 -> [ecx*4 + 0x4ba5d0]   chain + 2
//   cmd 3 -> [ecx*4 + 0x4ba5d8]   chain + 4
//   cmd 4 -> [ecx*4 + 0x4ba5e0]   chain + 6
//   cmd 5 -> [ecx*4 + 0x4ba5f0]   chain + 10   (Jill skips this command)
//   cmd 6 -> [ecx*4 + 0x4ba600]   chain + 14
//   cmd 7 -> [ecx*4 + 0x4ba608]   chain + 16
// so a command is just an entry point into one long sequence of steps, and the
// odd chain slots are the shared "interpolate and wait for the animation" step.
//
// em1014's command table stops at index 7: 0x004ba5a8 + 8*4 is exactly
// 0x004ba5c8, the first entry of the chain, so an index of 8 falls off the end
// of the table into the chain. That is NOT a ninth command - em1014 is only ever
// sent commands 0..7. Command 8 belongs to em1015, which has its own ninth
// entry and its own 3-step chain; see s_armLeverChain.
//
// Slot assignment. The two arms are NOT interchangeable: the terminal sends
// command 8 to slot 0 only, and only em1015 implements it, so slot 0 is em1015
// (entity id 21) and slot 1 is em1014 (id 20).
// ============================================================================
#include "../../Globals.h"
#include "../Types.h"
#include "../Entities.h"
#include "EntityCommon.h"

extern int  is_entity_in_switch_zone(VECTOR* position, void* zoneData);   // 0x00462d90
extern void ResetJointTransforms(void);                                   // EntityModelLoader.cpp
extern void play_sound_and_voice_effect(int bank, int id);

// Fields the generic Entity layout names for other enemies but that the arms
// use as fixed-point motion state. Addressed by offset on purpose - see the
// "offset writes, not nearest field" rule; the nearest named fields here
// (attacking_direction at 0x16C, splatter_flag at 0x174) are bytes.
#define ARM_VEL_X(e)  (*(int*)((unsigned char*)(e) + 0x16C))
#define ARM_VEL_Z(e)  (*(int*)((unsigned char*)(e) + 0x170))
#define ARM_POS_X(e)  (*(int*)((unsigned char*)(e) + 0x174))   // 16.16
#define ARM_POS_Z(e)  (*(int*)((unsigned char*)(e) + 0x178))   // 16.16
#define ARM_SCA_00(e) (*(unsigned int*)((unsigned char*)(e) + 0x1C))

#define ARM_X(e)      ((e)->scaMatrixData.localMatrix.t[0])
#define ARM_Z(e)      ((e)->scaMatrixData.localMatrix.t[2])
#define ARM_HOME_X(e) ((e)->scd_pos_x)      // 0xCC
#define ARM_HOME_Z(e) ((e)->scd_pos_z)      // 0xD4
#define ARM_SUB(e)    ((e)->ignore_player_flag)   // 0x85
#define ARM_STEP(e)   ((e)->action_behavior)      // 0x86
#define ARM_TIMER(e)  (*(short*)&(e)->action_ticks_counter)  // 0xC4, read signed

// Is the player Jill? Several steps have a different reach and a different
// voice line for her. 0x00be62e5 is g_playerEntity.id.
static inline bool arm_is_jill(void) { return (g_playerEntity.id & 1) != 0; }

// ============================================================================
// 0x00427e80 - aim the fixed-point velocity so the hand covers (from -> to) in
// `frames` frames.
// ============================================================================
static void arm_set_velocity(Entity* e, int fromX, int fromZ, int toX, int toZ,
                             unsigned int frames)
{
    const int n = (int)(frames & 0xffff);
    if (n == 0) { ARM_VEL_X(e) = 0; ARM_VEL_Z(e) = 0; return; }
    ARM_VEL_X(e) = ((toX - fromX) * 0x10000) / n;
    ARM_VEL_Z(e) = ((toZ - fromZ) * 0x10000) / n;
}

// ============================================================================
// 0x00427da0 - start a 15-frame reach to (home + dx, home + dz) and play the
// matching arm animation. Animations 2 and 3 are the two "reach across"
// variants; if the arm is already in one of them the other is chosen so the
// hand does not snap.
// ============================================================================
static void arm_begin_reach(Entity* e, int dx, int dz, char anim)
{
    const char cur = (char)e->animationId;
    if (anim == 2 && cur == 1)      anim = 3;
    else if (anim == 3 && cur == 3) anim = 2;

    e->animationId        = (unsigned char)anim;
    e->animation_frame_id = 0;
    e->timing_control     = 0;
    e->blend_counter      = 7;

    ARM_VEL_X(e) = ((ARM_HOME_X(e) - ARM_X(e) + dx) * 0x10000) / 15;
    ARM_VEL_Z(e) = ((ARM_HOME_Z(e) - ARM_Z(e) + dz) * 0x10000) / 15;
    arm_set_velocity(e, ARM_X(e), ARM_Z(e),
                     ARM_HOME_X(e) + dx, ARM_HOME_Z(e) + dz, 15);
}

// 0x00427ed0 - advance the 16.16 position by the current velocity.
static void arm_integrate(Entity* e)
{
    ARM_POS_X(e) += ARM_VEL_X(e);
    ARM_POS_Z(e) += ARM_VEL_Z(e);
    ARM_X(e) = ARM_POS_X(e) >> 16;
    ARM_Z(e) = ARM_POS_Z(e) >> 16;
}

static inline unsigned int arm_joint_move(Entity* e, short blend)
{
    return Joint_move(0, e->animHeader, e->animBase, blend);
}

static void arm_set_anim(Entity* e, unsigned char id, unsigned char frame,
                         unsigned char blend)
{
    e->animationId        = id;
    e->animation_frame_id = frame;
    e->timing_control     = 0;
    e->blend_counter      = blend;
}

static void arm_command_done(Entity* e)
{
    e->behavior_flags |= 0x20;
    ARM_SUB(e) = 0;
}

// ============================================================================
// The shared step chain (0x004ba5c8)
// ============================================================================

// chain[0] (0x004275e0) - reach left-and-forward.
static void arm_step_reach_a(Entity* e)
{
    ARM_SUB(e)++;
    arm_begin_reach(e, -0x28, 0x28, 1);
}

// chain[2] (0x004276f0) - reach right.
static void arm_step_reach_b(Entity* e)
{
    ARM_SUB(e)++;
    arm_begin_reach(e, 0x28, 0, (char)(arm_is_jill() ? 2 : 3));
}

// chain[4] (0x00427740) - reach left-and-back.
static void arm_step_reach_c(Entity* e)
{
    ARM_SUB(e)++;
    arm_begin_reach(e, -0x14, -0x28, (char)(arm_is_jill() ? 2 : 3));
}

// chain[6] (caseD_e) - the log-in gesture: settle onto the keyboard and say
// the line. g_main_state_flags bit 0x20000 is the handshake with the terminal's
// logo/voice sequence; the arm raises it and waits for it to clear.
static void arm_step_login(Entity* e)
{
    ARM_SUB(e)++;
    ARM_STEP(e) = 0;
    arm_set_anim(e, 4, 0, 7);

    if (!arm_is_jill()) {
        arm_set_velocity(e, ARM_X(e), ARM_Z(e), ARM_X(e), ARM_HOME_Z(e) + 200, 0x12);
        play_sound_and_voice_effect(1, 0xb8);
    } else {
        play_sound_and_voice_effect(1, 0xb9);
    }
    g_main_state_flags |= MSF_VOICE_PLAYING;
}

// Forward declarations for the two steps chain[7] re-dispatches into.
static void arm_step_press(Entity* e);
static void arm_step_wait_voice(Entity* e);

// chain[7] (caseD_f) - Jill just plays the animation out; Chris re-dispatches
// on ARM_STEP into the press/wait pair.
static void arm_step_typing(Entity* e)
{
    if (arm_is_jill()) {
        if (ARM_STEP(e) == 0) {
            if (arm_joint_move(e, 0x200) != 0) ARM_STEP(e)++;
        } else if ((g_main_state_flags & MSF_VOICE_PLAYING) == 0) {
            arm_command_done(e);
        }
        return;
    }

    if (ARM_STEP(e) == 0) arm_step_press(e);
    else                  arm_step_wait_voice(e);
}

// chain[8] (0x00427870) - the keypress itself. Frames 0x15 and 0x22 are the
// two contact points in the animation.
static void arm_step_press(Entity* e)
{
    const unsigned char frame = e->animation_frame_id;
    if (frame < 0x12) arm_integrate(e);
    if (frame == 0x15 || frame == 0x22) play_sfx(2, 0x1a, 0);

    if (arm_joint_move(e, 0x200) != 0) {
        ARM_STEP(e)++;
        ARM_TIMER(e) = 4;
    }
}

// chain[9] (0x004278e0)
static void arm_step_wait_voice(Entity* e)
{
    if ((g_main_state_flags & MSF_VOICE_PLAYING) == 0) arm_command_done(e);
}

// chain[10] (0x00427980)
static void arm_step_lift(Entity* e)
{
    ARM_SUB(e)++;
    ARM_TIMER(e) = 4;
    arm_set_velocity(e, ARM_X(e), ARM_Z(e), ARM_HOME_X(e), ARM_HOME_Z(e) + 0x50, 4);
}

// chain[11] (0x004279d0)
static void arm_step_lift_wait(Entity* e)
{
    arm_integrate(e);
    ARM_TIMER(e)--;
    if (ARM_TIMER(e) == 0) {
        ARM_SUB(e)++;
        arm_set_anim(e, 5, 0, 7);
    }
}

// chain[12] (0x00427a30)
static void arm_step_withdraw(Entity* e)
{
    if (!arm_is_jill() && e->animation_frame_id == 0x0f) play_sfx(2, 0x1c, 0);

    if (arm_joint_move(e, 0x200) != 0) {
        ARM_SUB(e)++;
        ARM_TIMER(e) = 4;
        arm_set_velocity(e, ARM_X(e), ARM_Z(e), ARM_HOME_X(e), ARM_HOME_Z(e), 4);
        arm_set_anim(e, 0, 0, 7);
    }
}

// chain[13] (0x00427b00)
static void arm_step_settle(Entity* e)
{
    arm_integrate(e);
    arm_joint_move(e, 0x200);
    ARM_TIMER(e)--;
    if (ARM_TIMER(e) == 0) arm_command_done(e);
}

// chain[14] (0x00427b80)
static void arm_step_reach_far(Entity* e)
{
    ARM_SUB(e)++;
    arm_begin_reach(e, -0x14, 0xa0, (char)(arm_is_jill() ? 2 : 3));
}

// chain[16] (0x00427bd0)
static void arm_step_hold(Entity* e)
{
    ARM_SUB(e)++;
    ARM_TIMER(e) = 0x96;
    e->blend_counter = (unsigned char)((e->animationId == 0) ? 0 : 7);
    arm_set_anim(e, 0, 0, e->blend_counter);
}

// chain[17] (0x00427c30)
static void arm_step_hold_wait(Entity* e)
{
    arm_joint_move(e, 0x200);
    ARM_TIMER(e)--;
    if (ARM_TIMER(e) != 0) return;

    ARM_SUB(e)++;
    e->animationId        = 4;
    e->animation_frame_id = (unsigned char)(arm_is_jill() ? 0 : 0x28);
    e->timing_control     = 0;
    e->blend_counter      = 0x1f;
    ARM_TIMER(e)          = 0x18;
    arm_set_velocity(e, ARM_X(e), ARM_Z(e),
                     ARM_HOME_X(e) - 100,
                     ARM_HOME_Z(e) + 0x8c + (arm_is_jill() ? 0 : -0x46),
                     0x18);
}

// chain[18] (0x00427d10)
static void arm_step_hold_move(Entity* e)
{
    arm_joint_move(e, 0x80);
    if (!arm_is_jill())                    e->animation_frame_id = 0x28;
    else if (e->animation_frame_id > 5)    e->animation_frame_id = 5;

    ARM_TIMER(e)--;
    if (ARM_TIMER(e) != 0) { arm_integrate(e); return; }
    ARM_SUB(e)++;
}

// chain[19] (0x00427d90) - terminal state, nothing to do.
static void arm_step_idle_end(Entity* /*e*/) { }

// ============================================================================
// Command 8 - em1015 ONLY (0x0040c350 -> its own 3-step chain at 0x004b34b0).
//
// This is the arm reaching DOWN off the keyboard to the release lever after the
// door menu has been confirmed, and it is the command the terminal sends to
// slot 0 from cl_menu_wait_close when the SECOND door is picked.
//
// em1014's command table (0x004ba5a8) only has entries 0..7: index 8 lands on
// 0x004ba5c8, which is the FIRST ENTRY OF ITS SHARED STEP CHAIN, because the
// two tables abut. So on em1014 a command 8 re-runs arm_step_reach_a every
// frame forever and NEVER raises the 0x20 "done" bit. em1015's table has a real
// ninth entry followed by a NULL terminator, and its own chain, so command 8 is
// exclusively em1015's - which is what identifies slot 0 as em1015 (entity id
// 21) and slot 1 as em1014 (id 20): slot 0 is the only slot the terminal ever
// sends 0x48 to. Routing command 8 through em1014's tables is what hung the
// terminal on the second door.
// ============================================================================

// chain8[0] (0x0040c370) - start the reach. The 15-frame velocity that
// arm_begin_reach installs is immediately replaced by a 4-frame drop, so only
// the animation (6) and the blend it sets survive.
static void arm_step_lever_reach(Entity* e)
{
    ARM_SUB(e)++;
    arm_begin_reach(e, -0x28, 0, 6);
    arm_set_velocity(e, ARM_X(e), ARM_Z(e),
                     ARM_HOME_X(e) - 0x28, ARM_HOME_Z(e) - 0x8c, 4);
    ARM_TIMER(e) = 4;
}

// chain8[1] (0x0040bf70) - glide down, then restart animation 6 from frame 0
// as the pull proper.
static void arm_step_lever_move(Entity* e)
{
    arm_integrate(e);
    ARM_TIMER(e)--;
    if (ARM_TIMER(e) == 0) {
        ARM_SUB(e)++;
        arm_set_anim(e, 6, 0, 7);
    }
    arm_joint_move(e, 0x200);
}

// chain8[2] (0x0040c040) - play it out. Note this step raises 0x20 WITHOUT
// clearing the sub-state (0x0040c06b is a bare `or byte [eax+2],0x20`, not the
// arm_command_done pair) - arm_state_run resets it when the next 0x40 arrives.
static void arm_step_lever_pull(Entity* e)
{
    if (arm_joint_move(e, 0x200) != 0) e->behavior_flags |= 0x20;
    if (e->animation_frame_id == 9) play_sfx(2, 0x18, 0);
}

// chain[odd] (0x00427600) - the shared "interpolate and wait for the reach
// animation to finish" step. Completing the animation ends the command.
static void arm_step_move(Entity* e)
{
    ARM_POS_X(e) += ARM_VEL_X(e);
    ARM_POS_Z(e) += ARM_VEL_Z(e);
    ARM_X(e) = ARM_POS_X(e) >> 16;
    ARM_Z(e) = ARM_POS_Z(e) >> 16;

    if (arm_joint_move(e, 0x200) != 0) arm_command_done(e);

    // The keystroke click, on whichever animation frame makes contact.
    if (!arm_is_jill()) {
        if (e->animation_frame_id == 8) play_sfx(2, 0x18, 0);
    } else if (e->animation_frame_id == 3 || e->animation_frame_id == 13) {
        play_sfx(2, 0x18, 0);
    }
}

typedef void (*ArmStep)(Entity*);

// 0x004ba5c8. The odd slots between the three opening reaches, plus slot 15,
// are all the shared move step.
static const ArmStep s_armChain[20] = {
    arm_step_reach_a,     //  0
    arm_step_move,        //  1
    arm_step_reach_b,     //  2
    arm_step_move,        //  3
    arm_step_reach_c,     //  4
    arm_step_move,        //  5
    arm_step_login,       //  6
    arm_step_typing,      //  7
    arm_step_press,       //  8
    arm_step_wait_voice,  //  9
    arm_step_lift,        // 10
    arm_step_lift_wait,   // 11
    arm_step_withdraw,    // 12
    arm_step_settle,      // 13
    arm_step_reach_far,   // 14
    arm_step_move,        // 15
    arm_step_hold,        // 16
    arm_step_hold_wait,   // 17
    arm_step_hold_move,   // 18
    arm_step_idle_end,    // 19
};

// 0x004b34b0 - em1015's command-8 chain, indexed by the sub-state.
static const ArmStep s_armLeverChain[3] = {
    arm_step_lever_reach,   // 0
    arm_step_lever_move,    // 1
    arm_step_lever_pull,    // 2
};

// Entry point into s_armChain per command, from the dispatch stubs' jump
// instructions. -1 means "not a chained command".
static const signed char s_armCmdEntry[8] = {
    -1,   //  0  handled separately (return to rest)
     0,   //  1
     2,   //  2
     4,   //  3
     6,   //  4
    10,   //  5  Jill returns immediately
    14,   //  6
    16,   //  7
};

// ============================================================================
// Command 0 (0x004274a0) - return to rest and idle there.
// The original's command dispatch is a jump table at 0x00427483 whose cases
// are separate Ghidra functions; the port implements each as arm_cmd_* below:
//   caseD_1 0x004275c0  caseD_2 0x004276d0  caseD_3 0x00427720
//   caseD_4 0x00427770  caseD_5 0x00427960  caseD_6 0x00427b60
//   caseD_7 0x00427bb0
// ============================================================================
static void arm_cmd_rest(Entity* e)
{
    if (ARM_SUB(e) == 0) {
        ARM_SUB(e) = 1;
        e->blend_counter = (unsigned char)((e->animationId == 0) ? 0 : 7);
        e->animationId        = 0;
        e->animation_frame_id = 0;
        e->timing_control     = 0;
        ARM_TIMER(e)          = 8;
        arm_set_velocity(e, ARM_X(e), ARM_Z(e), ARM_HOME_X(e), ARM_HOME_Z(e), 8);

        // Already home (within 200 units, Manhattan): skip the glide.
        const int dz = ARM_Z(e) - ARM_HOME_Z(e);
        const int dx = ARM_X(e) - ARM_HOME_X(e);
        const int dist = (dz < 0 ? -dz : dz) + (dx < 0 ? -dx : dx);
        if (dist < 200) ARM_TIMER(e) = 0;
    }

    if (ARM_TIMER(e) != 0) {
        arm_integrate(e);
        ARM_TIMER(e)--;
    }
    arm_joint_move(e, 0x200);
}

// ============================================================================
// 0x00427380 - state 0, one-shot init.
// ============================================================================
static void arm_state_init(Entity* e)
{
    e->state++;
    ARM_SUB(e)  = 0;
    e->health   = 1;
    e->status_flags = (unsigned char)((e->status_flags & 0x1f) | 4);
    e->animationId        = 0;
    e->animation_frame_id = 0;
    e->timing_control     = 0;
    e->blend_counter      = 0;

    ResetJointTransforms();
    arm_joint_move(e, 0x200);

    ARM_HOME_X(e) = ARM_X(e);
    ARM_HOME_Z(e) = ARM_Z(e);
    ARM_POS_X(e)  = ARM_X(e) << 16;
    ARM_POS_Z(e)  = ARM_Z(e) << 16;
}

// ============================================================================
// 0x00427450 - state 1, the command driver.
// ============================================================================
static void arm_state_run(Entity* e)
{
    // 0x80 = parked, 0x20 = command already finished. Either way, idle.
    if ((e->behavior_flags & 0xa0) != 0) {
        ARM_SUB(e) = 0;
        return;
    }
    // 0x40 = a command the terminal has just issued; consume the latch.
    if ((e->behavior_flags & 0x40) != 0) {
        ARM_SUB(e) = 0;
        e->behavior_flags &= 0xbf;
    }

    const int cmd = e->behavior_flags & 0x0f;
    if (cmd == 0) { arm_cmd_rest(e); return; }

    // Jill has no separate animation for command 5.
    if (cmd == 5 && arm_is_jill()) return;

    // The reach for the release lever - em1015's table only. See the note above
    // s_armLeverChain.
    if (cmd == 8) {
        if (e->id != 21) return;                // em1014 has no command 8
        const int step = (int)ARM_SUB(e);
        if (step < 3) s_armLeverChain[step](e);
        return;
    }
    if (cmd > 8) return;                        // no command past 8 exists

    const int slot = s_armCmdEntry[cmd] + (int)ARM_SUB(e);
    if (slot < 0 || slot >= 20) return;
    s_armChain[slot](e);
}

// ============================================================================
// computer_arm_update (0x00427330 for em1014, 0x0040b760 for em1015)
//
// em1015 has its own tables at 0x004b3418 / 0x004b3420 / 0x004b3440 with the
// same shape (an init state, a driver, a command table and a shared chain).
// The port drives both arms through this one implementation; the left arm's
// per-step reach constants have NOT been diffed against 0x0040b7b0..0x0040c350
// yet, so its offsets are the right arm's mirrored by the model's own origin
// rather than by its own table.
// ============================================================================
void computer_arm_update(void)
{
    Entity* e = ENTITY;

    if (e->state == 0) arm_state_init(e);
    else               arm_state_run(e);

    ARM_SCA_00(e) = 0;

    if ((e->behavior_flags & 0x80) != 0) {
        e->has_enter_switch_zone = 0;
        return;
    }
    e->has_enter_switch_zone = (unsigned char)is_entity_in_switch_zone(
        (VECTOR*)&e->scaMatrixData.localMatrix.t[0], g_CurrentRdtDataTypePtr);
}
