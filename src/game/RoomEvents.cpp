// RoomEvents.cpp - SCD room event system (decompiled from Ghidra)
// Implements room_events_check and all direct dependencies.
#include "../Globals.h"
#include <cstring>

// Forward declaration
void run_command_functions(unsigned short* scd_opcodes);

// ============================================================================
// ScdEventEntry_Init (0x0041d620)
// Initializes an SCD event entry: sets active, clears state, loads script
// pointer from the event scripts table, and assigns the current entity.
// ============================================================================
static void ScdEventEntry_Init(ScdEventEntry* entry, int scriptIndex)
{
    entry->active = 1;
    entry->state = 0;
    entry->scriptPtr = ((unsigned char**)g_EvtScripts)[scriptIndex];
    entry->stackDepth = 0xFF;
    entry->entity = ENTITY;
}

// ============================================================================
// ScdEventEntry_Create (0x0041d650)
// Creates a new SCD event entry. If slot > 7, finds the first free slot.
// ============================================================================
static void ScdEventEntry_Create(unsigned int slot, int scriptIndex)
{
    if (slot > 7) {
        slot = 0;
        if (g_ScdEventTable[0].active != 0) {
            unsigned char* pActive = &g_ScdEventTable[0].active;
            do {
                if (pActive == &g_ScdEventTable[7].active) break;
                pActive += sizeof(ScdEventEntry);
                slot++;
            } while (*pActive != 0);
        }
    }
    ScdEventEntry_Init(&g_ScdEventTable[slot], scriptIndex);
}

// ============================================================================
// scd_event_cmd_set_entity (0x0041e520) - SCD opcode 0x04, state 0
// Sets the current entity pointer for the event.
// ============================================================================
static void scd_event_cmd_set_entity(void)
{
    g_pScdEventCurrent->scriptPtr++;
    unsigned char* p = g_pScdEventCurrent->scriptPtr;
    switch (*p) {
    case 0: // Player entity
        g_pScdEventCurrent->entity = (Entity*)&g_playerEntity;
        break;
    case 1: // Enemy entity
        g_pScdEventCurrent->entity = &g_EnemiesList[p[1]];
        break;
    case 2: // Item box/cover
        g_pScdEventCurrent->entity = (Entity*)g_itemboxes_covers_table[p[1]];
        break;
    case 3: // Desk object
        g_pScdEventCurrent->entity = (Entity*)g_desks_pointers_table[p[1]];
        break;
    }
    g_pScdEventCurrent->scriptPtr += 2;
}

// ============================================================================
// scd_event_cmd_create (0x0041e5d0) - SCD opcode 0x05, state 0
// Creates a new event entry from script data.
// ============================================================================
static void scd_event_cmd_create(void)
{
    g_pScdEventCurrent->scriptPtr++;
    ScdEventEntry_Create(
        *g_pScdEventCurrent->scriptPtr,
        g_pScdEventCurrent->scriptPtr[1]
    );
    g_pScdEventCurrent->scriptPtr += 3;
}

// ============================================================================
// scd_event_cmd_init (0x0041e600) - SCD opcode 0x08, state 0
// Re-initializes the current event entry with a new script.
// ============================================================================
static void scd_event_cmd_init(void)
{
    ScdEventEntry_Init(g_pScdEventCurrent, g_pScdEventCurrent->scriptPtr[1]);
}

// ============================================================================
// run_command_functions (0x00473f60)
// Main SCD script interpreter. Processes blocks of SCD opcodes from the room
// initialization script. Each block starts with a size word. Opcodes are
// dispatched through the script_command_funcs_table.
// ============================================================================
static unsigned int DAT_00bf0808[8]; // SCD call stack storage

void run_command_functions(unsigned short* scd_opcodes)
{
    typedef int (*ScdCmdFunc)(void);

    g_CmdOpcodesPointer = DAT_00bf0808;
    g_ScriptContinueFlag = 0;
    unsigned short blockSize = *scd_opcodes;

    while (blockSize != 0) {
        g_ScdOpcodes = (unsigned char*)(scd_opcodes + 1);

        while (true) {
            int cmdResult;
            do {
                cmdResult = ((ScdCmdFunc*)script_command_funcs_table)[*g_ScdOpcodes]();
            } while (cmdResult != 0);

            if (g_ScriptContinueFlag == 0) break;

            g_CmdOpcodesPointer--;
            g_ScdOpcodes = (unsigned char*)*g_CmdOpcodesPointer;
            g_ScriptContinueFlag--;
        }

        scd_opcodes = (unsigned short*)((unsigned char*)scd_opcodes + (unsigned int)blockSize);
        blockSize = *scd_opcodes;
    }
    g_ScdOpcodes = (unsigned char*)scd_opcodes;
}

// ============================================================================
// scd_event_cmd_run_scd (0x0041e620) - SCD opcode 0x06, state 0
// Runs inline SCD commands via run_command_functions.
// ============================================================================
static void scd_event_cmd_run_scd(void)
{
    run_command_functions((unsigned short*)(g_pScdEventCurrent->scriptPtr + 2));
    g_pScdEventCurrent->scriptPtr += g_pScdEventCurrent->scriptPtr[1];
}

// ============================================================================
// scd_event_cmd_exec (0x0041e650) - SCD opcode 0x07, state 0
// Executes a single SCD command from the event script.
// Sets up g_ScdOpcodes from the event script data, advances the script
// pointer past the command block, and dispatches through the SCD command
// function table.
// ============================================================================
static void scd_event_cmd_exec(void)
{
    unsigned char* scriptData = g_pScdEventCurrent->scriptPtr;
    unsigned short header = *(unsigned short*)scriptData;

    g_ScdOpcodes = scriptData + 2;
    g_pScdEventCurrent->scriptPtr = scriptData + (header >> 8);

    typedef int (*ScdCmdFunc)(void);
    ((ScdCmdFunc*)script_command_funcs_table)[*g_ScdOpcodes]();
}

// ============================================================================
// scd_event_state3_set_behavior (0x0041e150) - State 3 handler
// Sets the entity's action behavior from the script byte.
// ============================================================================
static int scd_event_state3_set_behavior(void)
{
    g_PlayerDpadHeld = 0xFF;
    if (*g_pScdEventCurrent->scriptPtr != g_pScdEventCurrent->entity->action_behavior) {
        g_pScdEventCurrent->entity->action_state = 0;
    }
    g_pScdEventCurrent->entity->action_behavior = *g_pScdEventCurrent->scriptPtr;
    g_pScdEventCurrent->scriptPtr += 2;
    return 1;
}

// ============================================================================
// scd_event_state2_movement (0x0041e1a0) - State 2 handler
// Processes entity movement/position commands from the SCD event script.
// ============================================================================
static void scd_event_state2_movement(void)
{
    unsigned short* opcodes = (unsigned short*)g_pScdEventCurrent->scriptPtr;
    Entity* ent = g_pScdEventCurrent->entity;

    switch ((unsigned char)*opcodes) {
    case 0x00: // NOP
        g_pScdEventCurrent->scriptPtr++;
        return;

    case 0x01: // Return to state 0
        g_pScdEventCurrent->state = 0;
        g_pScdEventCurrent->scriptPtr++;
        return;

    case 0x02: // Apply speed to position
        ent->scaMatrixData.localMatrix.t[0] += (int)ent->speed.x;
        ent->scaMatrixData.localMatrix.t[1] += (int)ent->speed.y;
        ent->scaMatrixData.localMatrix.t[2] += (int)ent->speed.z;
        g_pScdEventCurrent->scriptPtr++;
        return;

    case 0x03: // Apply rotation steps
        ent->position.pad += ent->move_step_x;
        *(short*)&ent->angle = *(short*)&ent->angle + ent->move_step_z;
        *((short*)&ent->angle + 1) = *((short*)&ent->angle + 1) + *(short*)&ent->state;
        g_pScdEventCurrent->scriptPtr++;
        return;

    case 0x04: // Apply speed AND rotation steps
        ent->scaMatrixData.localMatrix.t[0] += (int)ent->speed.x;
        ent->scaMatrixData.localMatrix.t[1] += (int)ent->speed.y;
        ent->scaMatrixData.localMatrix.t[2] += (int)ent->speed.z;
        ent->position.pad += ent->move_step_x;
        *(short*)&ent->angle = *(short*)&ent->angle + ent->move_step_z;
        *((short*)&ent->angle + 1) = *((short*)&ent->angle + 1) + *(short*)&ent->state;
        g_pScdEventCurrent->scriptPtr++;
        return;

    case 0x05: // Set speed (3 signed bytes)
        ent->speed.x = (short)(signed char)g_pScdEventCurrent->scriptPtr[1];
        ent->speed.y = (short)(signed char)g_pScdEventCurrent->scriptPtr[2];
        ent->speed.z = (short)(signed char)g_pScdEventCurrent->scriptPtr[3];
        g_pScdEventCurrent->scriptPtr += 4;
        return;

    case 0x06: // Set rotation steps + state (4 signed bytes)
        ent->move_step_x = (short)(signed char)g_pScdEventCurrent->scriptPtr[1];
        ent->move_step_z = (short)(signed char)g_pScdEventCurrent->scriptPtr[2];
        {
            unsigned char b = g_pScdEventCurrent->scriptPtr[3];
            ent->state = (unsigned char)(short)(signed char)b;
            ent->ignore_player_flag = (unsigned char)((unsigned short)(short)(signed char)b >> 8);
        }
        g_pScdEventCurrent->scriptPtr += 4;
        return;

    case 0x07: // Set absolute position (3 shorts)
        ent->scaMatrixData.localMatrix.t[0] = (int)(short)opcodes[1];
        ent->scaMatrixData.localMatrix.t[1] = (int)*(short*)(g_pScdEventCurrent->scriptPtr + 4);
        ent->scaMatrixData.localMatrix.t[2] = (int)*(short*)(g_pScdEventCurrent->scriptPtr + 6);
        g_pScdEventCurrent->scriptPtr += 8;
        return;

    case 0x08: // Set single transform component
        {
            int component = g_pScdEventCurrent->scriptPtr[1];
            *(int*)((char*)ent->scaMatrixData.localMatrix.t + component) = (int)(short)opcodes[1];
        }
        g_pScdEventCurrent->scriptPtr += 3;
        return;

    case 0x09: // Set hit_state
        ent->hit_state = g_pScdEventCurrent->scriptPtr[1];
        g_pScdEventCurrent->scriptPtr += 2;
        return;

    case 0x0A: // Set position/health/field from opcodes (parametric)
    case 0x0B:
        {
            int* target;
            switch (*opcodes >> 8) {
            case 0: target = &ent->scaMatrixData.localMatrix.t[0]; break;
            case 1: target = &ent->scaMatrixData.localMatrix.t[1]; break;
            case 2: target = &ent->scaMatrixData.localMatrix.t[2]; break;
            case 3:
                ent->health = opcodes[1];
                goto advance4;
            case 4:
                ent->unk_c6 = opcodes[1];
                goto advance4;
            case 5:
                ent->unk_c8 = opcodes[1];
                goto advance4;
            default:
                goto advance4;
            }
            *target = (int)(short)opcodes[1];
        }
    advance4:
        g_pScdEventCurrent->scriptPtr += 4;
        if ((unsigned char)*opcodes == 0x0B) {
            ent->position.pad = opcodes[1];
            *(unsigned short*)&ent->angle = *(unsigned short*)(g_pScdEventCurrent->scriptPtr + 4);
            *((unsigned short*)&ent->angle + 1) = *(unsigned short*)(g_pScdEventCurrent->scriptPtr + 6);
            g_pScdEventCurrent->scriptPtr += 8;
        }
        return;

    default:
        return;
    }
}

// ============================================================================
// scd_event_state1_anim (0x0041da30) - State 1 handler
// Processes animation/movement commands while entity is animating.
// Returns 1 to continue processing, 0 to stop (wait for animation).
// ============================================================================
static int scd_event_state1_anim(void)
{
    unsigned short* opcodes = (unsigned short*)g_pScdEventCurrent->scriptPtr;
    Entity* ent = g_pScdEventCurrent->entity;

    switch ((unsigned char)*opcodes) {
    case 0x00: // Advance script pointer
        g_pScdEventCurrent->scriptPtr++;
        return 1;

    case 0x80: // End animation wait + clear ignore_player_flag
        ent->ignore_player_flag = 0;
        // fall through
    case 0x8B: // End animation wait
        g_pScdEventCurrent->pad_01 = 0;
        g_pScdEventCurrent->scriptPtr++;
        g_pScdEventCurrent->state = 0;
        break;

    case 0x81: // Set entity behavior type and target
        {
            unsigned short val = *opcodes;
            g_pScdEventCurrent->scriptPtr = (unsigned char*)(opcodes + 1);
            ent->lookAtFlags = (unsigned char)(val >> 8);

            if ((ent->lookAtFlags & 0xF) != 0) {
                g_pScdEventCurrent->scriptPtr += 8;

                if (ent->lookAtFlags == 0x93) {
                    // Target mode: set scd_target_ptr based on type
                    unsigned short* params = (unsigned short*)g_pScdEventCurrent->scriptPtr;
                    switch (*(opcodes + 1)) {
                    case 0:
                        ent->scd_target_ptr = (unsigned int)&g_playerEntity;
                        break;
                    case 1:
                        ent->scd_target_ptr = (unsigned int)&g_EnemiesList[params[0]];
                        break;
                    case 2:
                        ent->scd_target_ptr = (unsigned int)g_itemboxes_covers_table[params[0]];
                        break;
                    case 3:
                        ent->scd_target_ptr = (unsigned int)g_desks_pointers_table[params[0]];
                        break;
                    }
                } else {
                    // Position mode: set scd_pos_x/y/z
                    if ((ent->lookAtFlags & 0x20) == 0) {
                        ent->scd_pos_x = (int)(short)*(opcodes + 1);
                        ent->scd_pos_y = (int)(short)opcodes[2];
                    } else {
                        int x = (int)(short)*(opcodes + 1);
                        if (x < 0) x += 0x1000;
                        ent->scd_pos_x = x;
                        int y = (int)(short)opcodes[2];
                        if (y < 0) y += 0x1000;
                        ent->scd_pos_y = y;
                    }
                    ent->scd_pos_z = (int)(short)opcodes[3];
                }

                // Set step size and flags
                unsigned char step = g_pScdEventCurrent->scriptPtr[0];
                ent->lookAtYawStep = step ? step : 0xC0;

                unsigned short stepFlags = opcodes[4];
                if ((stepFlags & 0xFF00) != 0) {
                    ent->lookAtPitchStep = (unsigned char)(stepFlags >> 8);
                } else {
                    ent->lookAtPitchStep = 0x40;
                }
                return 1;
            }
        }
        break;

    case 0x82: // Clear behavior type bit 4
        ent->lookAtFlags &= ~0x10;
        g_pScdEventCurrent->scriptPtr++;
        return 1;

    case 0x83: // Set entity state + animation with full params
        ent->state = 8;
        ent->ignore_player_flag = 0;
        {
            unsigned short val = *opcodes;
            unsigned short hi = val >> 8;
            if ((val & 0x1000) == 0) {
                ent->action_behavior = (unsigned char)hi;
                ent->action_state = (unsigned char)(hi >> 8);
                ent->collisionFlags &= 0x7F;
            } else {
                if ((ent->collisionFlags & 0x80) == 0) {
                    ent->action_behavior = (unsigned char)(hi & 0xF);
                    ent->action_state = (unsigned char)((hi & 0xF) >> 8);
                    ent->collisionFlags |= 0x80;
                } else {
                    ent->action_behavior = (unsigned char)(hi & 0xF);
                }
            }
        }
        g_pScdEventCurrent->scriptPtr += 2;
        // Read animation parameters (3 more pairs of bytes)
        ent->unk_c6 = g_pScdEventCurrent->scriptPtr[0] | (g_pScdEventCurrent->scriptPtr[1] << 8);
        g_pScdEventCurrent->scriptPtr += 2;
        ent->unk_c8 = g_pScdEventCurrent->scriptPtr[0] | (g_pScdEventCurrent->scriptPtr[1] << 8);
        g_pScdEventCurrent->scriptPtr += 2;
        ent->scd_anim_param = *g_pScdEventCurrent->scriptPtr;
        g_pScdEventCurrent->scriptPtr++;
        ent->scd_timer_lo = 0x28;
        ent->scd_timer_hi = 0;
        ent->scd_entity_flags = 0;
        return 1;

    case 0x84: // Set entity state + simple animation
        ent->state = 8;
        ent->ignore_player_flag = 0;
        ent->action_behavior = 1;
        ent->action_state = 0;
        {
            unsigned short animParam = *(unsigned short*)g_pScdEventCurrent->scriptPtr;
            g_pScdEventCurrent->scriptPtr += 2;
            unsigned short animData = *(unsigned short*)g_pScdEventCurrent->scriptPtr;
            g_pScdEventCurrent->scriptPtr += 2;
            ent->animationId = (unsigned char)(animParam >> 8);
            ent->scd_anim_param = (unsigned char)animData;
            ent->scd_entity_flags = (unsigned short)((animData >> 6) & 0x3FC);
            ent->scd_timer_lo = 0;
        }
        return 1;

    case 0x85: // Set entity state + behavior + animation
        {
            unsigned short val = *opcodes;
            g_pScdEventCurrent->scriptPtr = (unsigned char*)(opcodes + 1);
            unsigned short animParam = *(unsigned short*)g_pScdEventCurrent->scriptPtr;
            g_pScdEventCurrent->scriptPtr += 2;
            ent->state = 8;
            ent->ignore_player_flag = 0;
            ent->action_behavior = (unsigned char)(val >> 8);
            ent->action_state = (unsigned char)((val >> 8) >> 8);
            ent->animationId = (unsigned char)animParam;
            ent->scd_anim_param = (unsigned char)(animParam >> 8);
            ent->scd_timer_lo = 0;
            ent->scd_timer_hi = 0;
            ent->scd_entity_flags = 0;
        }
        return 1;

    case 0x86: // Set entity state to idle
        ent->state = 1;
        ent->ignore_player_flag = 0;
        ent->action_behavior = 0;
        ent->action_state = 0;
        ent->hit_state = 0;
        g_pScdEventCurrent->scriptPtr++;
        return 1;

    case 0x87: // Set entity flags (OR/SET/XOR)
        {
            unsigned short val = *opcodes;
            g_pScdEventCurrent->scriptPtr = (unsigned char*)(opcodes + 1);
            unsigned short mode = val >> 8;
            unsigned short flagVal = *(unsigned short*)g_pScdEventCurrent->scriptPtr;
            g_pScdEventCurrent->scriptPtr += 2;
            if (mode == 0) {
                ent->scd_entity_flags |= flagVal;
            } else if (mode == 1) {
                ent->scd_entity_flags = flagVal;
            } else if (mode == 2) {
                ent->scd_entity_flags ^= flagVal;
            }
        }
        return 1;

    case 0x88: // Set animation timer bytes
        g_pScdEventCurrent->scriptPtr = (unsigned char*)(opcodes + 1);
        ent->scd_timer_lo = g_pScdEventCurrent->scriptPtr[0];
        ent->scd_timer_hi = g_pScdEventCurrent->scriptPtr[1];
        g_pScdEventCurrent->scriptPtr += 2;
        return 1;

    case 0x89: // Set animation frame with entity-specific behavior
        {
            unsigned short val = *opcodes;
            g_pScdEventCurrent->scriptPtr = (unsigned char*)(opcodes + 1);
            ent->animation_frame_id = (unsigned char)(val >> 8);
            ent->timing_control = 0;
            ent->blend_counter = 7;
            ent->move_speed_current = 0;
            if ((ent->scd_entity_flags & 0x20) != 0) {
                ent->blend_counter = 0;
            }
            if (ent->id < 0x20) {
                if (ent->animationId > 0x0F) {
                    ent->action_state = 3;
                } else {
                    // Lookup table at 0x004bec80: pairs of (base_anim, action_state_offset)
                    extern const unsigned char DAT_004bec80[];
                    ent->action_state = DAT_004bec80[(unsigned int)ent->animationId * 2] + 1;
                    ent->animationId = DAT_004bec80[(unsigned int)ent->animationId * 2 + 1];
                }
            } else {
                ent->action_state = 1;
            }
        }
        return 1;

    case 0x8A: // Set animation frame (simple, no lookup)
        {
            unsigned short val = *opcodes;
            g_pScdEventCurrent->scriptPtr = (unsigned char*)(opcodes + 1);
            ent->animation_frame_id = (unsigned char)(val >> 8);
            ent->timing_control = 0;
            ent->blend_counter = 7;
            ent->move_speed_current = 0;
            if ((ent->scd_entity_flags & 0x20) != 0) {
                ent->blend_counter = 0;
                return 1;
            }
        }
        return 1;

    default:
        break;
    }
    return 1;
}

// ============================================================================
// room_events_check (0x0041d6a0)
// Main SCD event processing loop. Called every frame from game_loop and once
// during room_set. Iterates through all 8 event entries and processes their
// SCD scripts based on current state.
//
// Control flow opcodes (0xF6-0xFF):
//   0xF6 - Push loop counter (fall through to F7)
//   0xF7 - Wait condition: exit if main_state_flags bit or menu active
//   0xF8 - Push counter with value from script
//   0xF9 - Decrement counter, skip if zero
//   0xFA - Loop start with counter
//   0xFB - Loop end (decrement/branch)
//   0xFC - Call subroutine
//   0xFD - Call SCD command from call stack
//   0xFE - Advance script pointer (NOP)
//   0xFF - Deactivate event
//
// State 0 opcodes (0x00-0x09):
//   0x00 - NOP
//   0x01 - Enter state 1 (wait animation)
//   0x02 - Enter state 2 (movement) + setup entity
//   0x03 - Enter state 2 (movement)
//   0x04 - Set current entity
//   0x05 - Create new event
//   0x06 - Run SCD commands inline
//   0x07 - Execute SCD command
//   0x08 - Reinitialize current event
//   0x09 - Deactivate another event
// ============================================================================
void room_events_check(void)
{
    int result;

    // 0x0041d6a0: Check if message display flag bit 7 is set
    if ((g_message_flags & 0x80) == 0) {
        return;
    }

    ScdEventEntry* entry = g_ScdEventTable;

    do {
        g_pScdEventCurrent = entry;

        if (entry->active != 0) {
            // Main event processing loop
            do {
                unsigned char* scriptByte = g_pScdEventCurrent->scriptPtr;

                // Process control flow opcodes (0xF6-0xFF)
                switch (*scriptByte) {
                case 0xF6: // Push loop counter (fall through to wait)
                    g_pScdEventCurrent->stackDepth++;
                    g_pScdEventCurrent->scriptPtr++;
                    // fall through
                case 0xF7: // Wait condition
                    if (((((unsigned char*)&g_main_state_flags)[1] & 2) == 0) &&
                        ((g_menu_choice_id & 0x80) == 0)) {
                        g_pScdEventCurrent->scriptPtr++;
                        g_pScdEventCurrent->stackDepth--;
                    }
                    goto event_next_entry;

                case 0xF8: // Push counter with value
                    g_pScdEventCurrent->stackDepth++;
                    g_pScdEventCurrent->counterStack[(signed char)g_pScdEventCurrent->stackDepth] =
                        *(short*)(g_pScdEventCurrent->scriptPtr + 2);
                    g_pScdEventCurrent->scriptPtr++;
                    // fall through
                case 0xF9: // Decrement counter
                    g_pScdEventCurrent->counterStack[(signed char)g_pScdEventCurrent->stackDepth]--;
                    if (g_pScdEventCurrent->counterStack[(signed char)g_pScdEventCurrent->stackDepth] == 0) {
                        g_pScdEventCurrent->scriptPtr += 3;
                        g_pScdEventCurrent->stackDepth--;
                    }
                    goto event_next_entry;

                case 0xFA: // Loop start with counter
                    g_pScdEventCurrent->stackDepth++;
                    g_pScdEventCurrent->counterStack[(signed char)g_pScdEventCurrent->stackDepth] =
                        *(short*)(g_pScdEventCurrent->scriptPtr + 2);
                    g_pScdEventCurrent->scriptPtr += 4;
                    g_pScdEventCurrent->returnStack[(signed char)g_pScdEventCurrent->stackDepth] =
                        (unsigned int)g_pScdEventCurrent->scriptPtr;
                    goto event_restart_switch;

                case 0xFB: // Loop end
                    g_pScdEventCurrent->counterStack[(signed char)g_pScdEventCurrent->stackDepth]--;
                    if (g_pScdEventCurrent->counterStack[(signed char)g_pScdEventCurrent->stackDepth] == 0) {
                        g_pScdEventCurrent->scriptPtr++;
                        g_pScdEventCurrent->stackDepth--;
                    } else {
                        g_pScdEventCurrent->scriptPtr =
                            (unsigned char*)g_pScdEventCurrent->returnStack[(signed char)g_pScdEventCurrent->stackDepth];
                    }
                    goto event_restart_switch;

                case 0xFC: // Call subroutine
                    g_pScdEventCurrent->stackDepth++;
                    g_pScdEventCurrent->callStack[(signed char)g_pScdEventCurrent->stackDepth] =
                        (unsigned int)(g_pScdEventCurrent->scriptPtr + 2);
                    g_pScdEventCurrent->scriptPtr =
                        g_pScdEventCurrent->scriptPtr + g_pScdEventCurrent->scriptPtr[1];
                    g_pScdEventCurrent->returnStack[(signed char)g_pScdEventCurrent->stackDepth] =
                        (unsigned int)g_pScdEventCurrent->scriptPtr;
                    goto event_restart_switch;

                case 0xFD: // Call SCD command from call stack
                    g_ScdOpcodes = (unsigned char*)g_pScdEventCurrent->callStack[(signed char)g_pScdEventCurrent->stackDepth];
                    typedef int (*ScdCmdFunc)(void);
                    result = ((ScdCmdFunc*)script_command_funcs_table)[*g_ScdOpcodes]();
                    if (result == 0) {
                        g_pScdEventCurrent->scriptPtr++;
                        g_pScdEventCurrent->stackDepth--;
                    } else {
                        g_pScdEventCurrent->scriptPtr =
                            (unsigned char*)g_pScdEventCurrent->returnStack[(signed char)g_pScdEventCurrent->stackDepth];
                    }
                    goto event_restart_switch;

                case 0xFF: // Deactivate event
                    g_pScdEventCurrent->active = 0;
                    // fall through
                case 0xFE: // Advance pointer (NOP)
                    g_pScdEventCurrent->scriptPtr++;
                    goto event_next_entry;
                }

                // Process state-specific opcodes
                switch (g_pScdEventCurrent->state) {
                case 0: {
                    switch (*scriptByte) {
                    case 0x00: // NOP
                        g_pScdEventCurrent->scriptPtr++;
                        break;
                    case 0x01: // Enter state 1 (wait animation)
                        g_pScdEventCurrent->state = 1;
                        g_pScdEventCurrent->scriptPtr++;
                        break;
                    case 0x02: // Enter state 2 + setup entity
                        g_pScdEventCurrent->state = 2;
                        g_pScdEventCurrent->scriptPtr++;
                        g_pScdEventCurrent->entity->ignore_player_flag = 2;
                        g_pScdEventCurrent->entity->action_behavior = 0;
                        g_pScdEventCurrent->entity->action_state = 0;
                        break;
                    case 0x03: // Enter state 2
                        g_pScdEventCurrent->state = 2;
                        g_pScdEventCurrent->scriptPtr++;
                        break;
                    case 0x04: // Set entity
                        scd_event_cmd_set_entity();
                        break;
                    case 0x05: // Create event
                        scd_event_cmd_create();
                        break;
                    case 0x06: // Run SCD inline
                        scd_event_cmd_run_scd();
                        break;
                    case 0x07: // Execute SCD command
                        scd_event_cmd_exec();
                        break;
                    case 0x08: // Reinitialize event
                        scd_event_cmd_init();
                        goto event_next_entry;
                    case 0x09: // Deactivate another event
                        g_ScdEventTable[scriptByte[1]].active = 0;
                        g_pScdEventCurrent->scriptPtr += 2;
                        break;
                    default: // Unknown opcode: deactivate
                        g_pScdEventCurrent->active = 0;
                        return;
                    }
                    goto event_restart_switch;
                }
                case 1: // Wait animation state
                    result = scd_event_state1_anim();
                    break;
                case 2: // Movement state
                    scd_event_state2_movement();
                    goto event_restart_switch;
                case 3: // Set behavior state
                    result = scd_event_state3_set_behavior();
                    break;
                default:
                    goto event_restart_switch;
                }
            } while (result != 0);

        event_next_entry:
            // Check for end-of-script marker
            if (*g_pScdEventCurrent->scriptPtr == 0xFF) {
                g_pScdEventCurrent->active = 0;
            }
        }

    event_restart_switch:
        entry++;
        // Termination: iterate all 8 entries
        if (entry > &g_ScdEventTable[7]) {
            return;
        }
    } while (true);
}
