"""CPU checks of greenhouse spawn/lifetime hooks and chemical leaf placement.
Never launches the game. Run: python tests/test_zombie_greenhouse.py
"""
import json
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import evaluate_zombie_routes as routes

# Anchor the polarity to the shipped script instead of a guessed boolean:
# cmd_bit_test returns flag XOR condition, so condition 1 takes the live
# enemy branch when A6 is CLEAR. Chemical event 1 uses bit_op operation 0
# (SET), establishing the permanent solved state.
rdt = (routes.ROOT / "bin/Release/USA/stage6/room60c0.rdt").read_bytes()
init_offset = struct.unpack_from("<I", rdt, 0x60)[0]
frame_offset = struct.unpack_from("<I", rdt, 0x64)[0]
init = rdt[init_offset:frame_offset]
assert bytes.fromhex("04 01 a6 01 1b 0f 01") in init
event_table = struct.unpack_from("<I", rdt, 0x68)[0]
event1, event2 = struct.unpack_from("<II", rdt, event_table + 4)
assert bytes.fromhex("05 01 a6 00") in rdt[event_table + event1:event_table + event2]

# Both body families must actually contain the hold/release frames used by
# player_plant_hold_01/02, rather than just supplying nonzero cache pointers.
enemy_files = {p.name.lower(): p for p in (routes.ROOT / "bin/Release/USA/enemy").iterdir()}
for filename in ("em100f.emd", "em110f.emd"):
    emd = enemy_files[filename].read_bytes()
    damage_base = struct.unpack_from("<I", emd, (len(emd) & ~3) - 20)[0]
    assert 0 < damage_base < len(emd)
    for animation in (2, 3):
        frames, offset = struct.unpack_from("<HH", emd, damage_base + animation * 4)
        assert frames > 0x27, (filename, animation)
        assert damage_base + (offset & ~3) + frames * 4 <= len(emd)

mode = (routes.ROOT / "src/game/mods/ZombieMode.cpp").read_text()
spawn = "bool zombie_mode_enemy_spawn(" + routes.between(
    mode, "bool zombie_mode_enemy_spawn(", "static void zm_shared_after_update")
vine = "static bool zm_greenhouse_vine(" + routes.between(
    mode, "static bool zm_greenhouse_vine(", "void zombie_mode_after_entity_update")
# Exercise the production early hook without unrelated combat dependencies.
before = "bool zombie_mode_before_entity_update(" + routes.between(
    mode, "bool zombie_mode_before_entity_update(", "    if (zm_entry_idle(e))") + "return false; }\n"
select = "bool zombie_mode_damage_anim_select(" + routes.between(
    mode, "bool zombie_mode_damage_anim_select(", "int zombie_mode_player_weapon_block(")
body = "int zombie_mode_player_body(void)" + routes.between(
    mode, "int zombie_mode_player_body(void)", "bool zombie_mode_network_match(void)")
plant = (routes.ROOT / "src/game/entities/MonsterPlant.cpp").read_text()
pick = "inline unsigned char mp_pick(" + routes.between(
    plant, "inline unsigned char mp_pick(", "// ---------------------------------------------------------------------------\n// monster_plant_idle_fidget")
grab = "void mp_step_00(void)\n{" + routes.between(
    plant, "void mp_step_00(void)\n{", "// 0x0045b7c0")
fixture = r'''
#include <cassert>
#include <cstdio>
enum { STAGE_MANSION_RETURN_1F=5, ROOM_GREENHOUSE=12, NPC_ENTITIES_IDS=16 };
enum { ZM_CHAR_CHRIS, ZM_CHAR_JILL, ZM_CHAR_BARRY, ZM_CHAR_REBECCA, ZM_CHAR_RICHARD, ZM_CHAR_ENRICO };
struct VECTOR { int t[3]; };
struct Entity { unsigned char id=15, state=1, behavior_flags=1, animationId=8,
 animation_frame_id=0, timing_control=0, blend_counter=0, ignore_player_flag=0;
 unsigned int animHeader=0, animBase=0; };
static Entity* ENTITY;
static int MP_ANIM_END=0, MP_DIST=1000, MP_HOLDOFF=0, MP_STEP=0, MP_TIMER_A=0;
void mp_idle_fidget() {}
int Joint_move(int, unsigned int, unsigned int, int) { return 0; }
int turn_toward_target(VECTOR*, int) { return 0; }
static bool s_targetSwapped=false; static int s_targetIdx=0, character=0;
int zm_net_char(int) { return character; }
int zombie_mode_player_skin(int) { return character; }
static bool s_zombieModeArmed=true, live=true;
static int g_stageId=5, g_roomId=12, g_ScenarioFlags2;
static unsigned int s_damageHeader[16]={}, s_damageBase[16]={};
static struct { int health=100, isBeingAttackedFlag=0, animationId=1, id=0, action_behavior=0;
 struct { VECTOR localMatrix; } scaMatrixData;
 unsigned int emdScratchPtr1=0, emdScratchPtr2=0; } g_playerEntity;
void dbg_printf(const char*, ...) {}
int Flg_ck(int, int flag) { assert(flag==0xA6); return !live; }
bool zm_world_restore_enemy(Entity*, unsigned char, unsigned char) { return true; }
'''
checks = r'''
int main() {
 Entity e;
 ENTITY=&e;
 const int expectedBodies[6]={0,1,0,1,0,0};
 const int expectedVoices[6]={0,1,0,3,0,0};
 for (character=0;character<6;++character) {
   assert(zombie_mode_player_body()==expectedBodies[character]);
   assert(zombie_mode_player_voice(0)==expectedVoices[character]);
   assert(zombie_mode_player_voice(4)==(expectedVoices[character] | 4));
   // Scenario ID remains Chris on every copy, regardless of character.
   g_playerEntity.id=0; e.animationId=8; MP_STEP=0;
   mp_step_00();
   assert(MP_STEP==1 && e.animationId==(expectedBodies[character] ? 10 : 4));
   s_damageHeader[15]=expectedBodies[character] ? 0x110F : 0x100F;
   s_damageBase[15]=s_damageHeader[15]+0x1000;
   g_playerEntity.emdScratchPtr1=g_playerEntity.emdScratchPtr2=0;
   assert(zombie_mode_before_entity_update(&e));
   assert(g_playerEntity.emdScratchPtr1==s_damageHeader[15] &&
          g_playerEntity.emdScratchPtr2==s_damageBase[15]);
 }
 s_damageHeader[15]=s_damageBase[15]=0;
 puts("Vine grab pose and hit animation selection passed for all six characters");
 // Jill's plant EMD was cached by the loader; choose it before a vine can
 // enter the player's hold/hurt controller with null or stale pointers.
 assert(!zombie_mode_before_entity_update(&e)); // no reaction data: no attack
 s_damageHeader[15]=0x110F; s_damageBase[15]=0x210F;
 assert(zombie_mode_before_entity_update(&e));
 assert(g_playerEntity.emdScratchPtr1==0x110F && g_playerEntity.emdScratchPtr2==0x210F);
 // A hit/grab already running must keep its latched animation pointers.
 g_playerEntity.isBeingAttackedFlag=1; g_playerEntity.animationId=6;
 g_playerEntity.emdScratchPtr1=0x1234; g_playerEntity.emdScratchPtr2=0x5678;
 assert(zombie_mode_before_entity_update(&e));
 assert(g_playerEntity.emdScratchPtr1==0x1234 && g_playerEntity.emdScratchPtr2==0x5678);
 g_playerEntity.isBeingAttackedFlag=0; g_playerEntity.animationId=1;
 for (int slot=0; slot<6; ++slot) assert(zombie_mode_enemy_spawn(&e,slot,15));
 assert(!zombie_mode_enemy_spawn(&e,6,0)); // director still supplies other monsters
 assert(zm_greenhouse_vine(&e));
 assert(zombie_mode_before_entity_update(&e) && e.behavior_flags==1);
 // Completion received from another survivor kills already-loaded vines.
 live=false;
 assert(zombie_mode_before_entity_update(&e) && (e.behavior_flags & 0x40));
 for (int slot=0; slot<6; ++slot) assert(!zombie_mode_enemy_spawn(&e,slot,15));
 // A pending init must run before requesting death.
 e.state=0; e.behavior_flags=1;
 assert(zombie_mode_before_entity_update(&e) && e.behavior_flags==1);
 e.state=1; assert(zombie_mode_before_entity_update(&e) && (e.behavior_flags & 0x40));
 g_roomId=13; assert(!zm_greenhouse_vine(&e));
 assert(!zombie_mode_enemy_spawn(&e,0,15));
 s_zombieModeArmed=false;
 for (int bank=0;bank<8;++bank) assert(zombie_mode_player_voice(bank)==bank);
 assert(zombie_mode_enemy_spawn(&e,0,15)); // USA path
 puts("Greenhouse spawn and shared completion hooks OK");
}
'''
with tempfile.TemporaryDirectory(prefix="re1-greenhouse-tests-") as temp:
    with patch.object(routes, "adapter_source", return_value=fixture + body + pick + grab + spawn + vine + select + before + checks):
        executable = routes.build_adapter(Path(temp))
    subprocess.run([str(executable)], check=True)
    executable = routes.build_adapter(Path(temp))
    output = subprocess.check_output(
        [str(executable), str(routes.ROOT / "bin/Release/USA")],
        input="".join(f"{seed}\n" for seed in range(1, 201)), text=True)
    for line in output.splitlines():
        layout = json.loads(line)
        assert layout["generated"], layout["seed"]
        chemicals = [item for item in layout["tools"] if item[2] == 0x26]
        assert len(chemicals) == 1, layout["seed"]
        assert chemicals[0][:2] in layout["leaves"], layout["seed"]
    print("Chemical occupies a leaf room in all 200 production scenarios")
