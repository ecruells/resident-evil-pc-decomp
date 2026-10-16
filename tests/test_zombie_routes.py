"""CPU-only route optimization regressions. Never starts the game.

Run: python tests/test_zombie_routes.py
"""
from pathlib import Path
import argparse
import json
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import evaluate_zombie_routes as routes
import audit_zombie_puzzle_access as puzzle_audit

ASSETS = None
COMPILER = None

S, E, O = routes.START, routes.EXIT, (2, 0)
A, B, C = (5, 1), (5, 2), (6, 4)


def layout(edges, items):
    return {"seed": 1, "generated": True, "attempts": 1,
            "doors": [[*a, *b, mask] for a, b, mask in edges],
            "items": [[*room, item, i] for i, (room, item) in enumerate(items)]}


def crests(room):
    return [(room, item) for item in routes.ITEM_IDS[4:]]


class RouteTests(unittest.TestCase):
    def test_optional_battery_and_disk_rewards(self):
        if ASSETS is None:
            self.skipTest("Pass --assets to check native optional reward slots")
        production = (routes.ROOT / "src/game/mods/ZombieRandom.cpp").read_text()
        supplies = "static bool rnd_is_tier3(" + routes.between(
            production, "static bool rnd_is_tier3(", "// Where a key item may lie")
        weapons = "static bool rnd_is_key_item(" + routes.between(
            production, "static bool rnd_is_key_item(", "static void rnd_weapons_refresh(")
        source = routes.adapter_source().replace("static void generate(unsigned int seed) {",
            '#include <cassert>\nvoid dbg_printf(const char*,...) {}\n'
            'const char* DebugRoom_Name(int,int) { return "fixture"; }\n' + supplies + weapons +
            "static void generate(unsigned int seed) {")
        checks = r'''
    assert(ok);
    s_rngWeapons=s_rng;
    for(int barry=0;barry<2;++barry) {
        rnd_place_weapons(barry!=0);
        int rewards=0, puzzles=0, magnumAmmo=0;
        int broken=0, axe=0, notes=0, chemical=0;
        for(int i=0;i<s_spotCount;++i) {
            const RndSpot& s=s_spots[i];
            if(s.pool && s.id==ITEM_BROKEN_SHOTGUN) ++broken;
            if(s.pool && s.id==ITEM_PICK_AXE) ++axe;
            if(s.id==ITEM_MUSIC_NOTES) { ++notes; assert(s.pool); }
            if(s.id==ITEM_CHEMICAL) { ++chemical; assert(s.pool); }
            if(s.id==ITEM_MAGNUM_ROUNDS) ++magnumAmmo;
            // The heavy weapons lie only in the puzzles' reward slots.
            if(s.puzzle) { ++puzzles; assert(s.id==s_puzzleWeapon[s.puzzle-1] && (s.overridden || (s.id==s.origId && s.qty==s.origQty))); }
            else assert(!rnd_is_tier3(s.id));
            if(s.rewardOnly) {
                ++rewards; assert(s.pool && s.overridden);
                assert(s.id && s.qty && !rnd_item_bit(s.id));
                assert(s.id!=ITEM_BATTERY && s.id!=ITEM_MO_DISK);
                assert(s.stage==6 && ((s.room==0x19 && s.flag==0x39) ||
                                     (s.room==0x17 && s.flag==0x4d)));
            }
        }
        assert(rewards==2);
        assert(broken==1 && axe==1 && notes==1 && chemical==1);
        assert(puzzles==RND_PUZZLES);
        if(barry) assert(s_t3AmmoCount>0 && s_t3Ammo[s_t3AmmoCount-1]==ITEM_MAGNUM_ROUNDS);
    }
'''
        source = source.replace('    printf("{\\"seed\\":', checks + '    printf("{\\"seed\\":')
        self.assertIn(checks, source)
        with tempfile.TemporaryDirectory(prefix="re1-optional-rewards-") as temp:
            with patch.object(routes, "adapter_source", return_value=source):
                exe = routes.build_adapter(Path(temp), COMPILER)
            subprocess.run([str(exe), str(ASSETS.resolve())],
                           input="".join(f"{seed}\n" for seed in range(1, 101)),
                           text=True, stdout=subprocess.DEVNULL, check=True)

    def test_desk_rewards_accessible_to_all_survivors(self):
        production = (routes.ROOT / "src/game/PlayerAnimations.cpp").read_text()
        helper = "int check_desk(unsigned char* entry)" + routes.between(
            production, "int check_desk(unsigned char* entry)",
            "// 0x004885a0")
        fixture = r'''
#include <cassert>
#include <cstring>
#define MSF_MENU_PENDING 1
#define ITEM_DESK_KEY 0x3d
#define SCENARIO_FLAG_HAS_LOCKPICK 0x7c
static bool armed;
bool zombie_mode_armed() { return armed; }
static int g_desk_check_state, g_main_state_flags, g_message_flags=0x40;
static struct { int id, isBeingAttackedFlag; } g_playerEntity;
static unsigned char g_RoomActionTable[24], *g_pRoomActionEntry;
static unsigned int g_roomItemsFlags[8], g_LocksFlags[8], g_ScenarioFlags[8];
static void* g_item_model_table[1], *g_CurrentRdtDataTypePtr;
static struct { void* cam_switch_zones; } rdt, *g_RdtPointer=&rdt;
static int g_cutId, g_roomCameraId, message, keySlot=-1;
int Flg_ck(int bank, int) {
    return bank==(int)g_roomItemsFlags ? g_roomItemsFlags[0] :
           bank==(int)g_LocksFlags ? g_LocksFlags[0] : g_ScenarioFlags[0];
}
void set_message_display(int id,int) { message=id; }
int get_item_slot(int) { return keySlot; }
void play_sfx(int,int,int) {}
void StMask(int,int) {}
int main();
'''
        checks = r'''
int main() {
    unsigned char entry[12]={}, model[0xA4]={}, zones[0x28]={};
    *(unsigned short*)(entry+4)=1;
    g_item_model_table[0]=model; rdt.cam_switch_zones=zones;
    for (int mode=0;mode<2;++mode) for(int id=0;id<8;++id)
    for(int unlocked=0;unlocked<2;++unlocked) for(int key=0;key<2;++key)
    for(int pick=0;pick<2;++pick) for(int item=0;item<2;++item) {
        armed=mode; g_playerEntity.id=id; g_LocksFlags[0]=unlocked;
        keySlot=key?0:-1; g_ScenarioFlags[0]=pick; g_roomItemsFlags[0]=item;
        g_desk_check_state=0; g_message_flags=0x40; message=0; model[0]=0;
        check_desk(entry);
        int state=0, msg=0;
        if(item) {
            if(!mode && (id&3)==3) msg=0xd7;
            else if(!mode && !unlocked) {
                if(!key && !pick) msg=0xd8; else state=1;
            } else state=35;
        }
        assert(g_desk_check_state==state); assert(message==msg);
        assert(model[0]==(state==35?1:0));
        assert(g_LocksFlags[0]==(unsigned)unlocked);
    }
}
'''
        with tempfile.TemporaryDirectory(prefix="re1-desk-test-") as temp:
            with patch.object(routes, "adapter_source", return_value=fixture + helper + checks):
                exe = routes.build_adapter(Path(temp), COMPILER)
            subprocess.run([str(exe)], check=True)

    def test_unnecessary_keys_do_not_extend_escape(self):
        data = layout([(S, E, 0xF0), (E, O, 0)], crests(S) + [(A, 0x33)])
        result = routes.evaluate(data)
        self.assertEqual(result["door_crossings"], 2)
        self.assertEqual(result["keys_collected"], [])
        self.assertEqual(result["route"][0]["collect"], list(routes.ITEM_NAMES[4:]))

    def test_sequential_keys_force_backtracking(self):
        data = layout([(S, A, 0), (A, S, 0), (S, B, 1), (B, S, 0),
                       (S, C, 2), (C, E, 0xF0), (E, O, 0)],
                      [(A, 0x33), (B, 0x34)] + crests(C))
        result = routes.evaluate(data)
        self.assertEqual(result["door_crossings"], 7)
        self.assertEqual(result["keyless_crossings"], 3)
        self.assertEqual(result["key_gate_extra_crossings"], 4)
        self.assertEqual(result["repeated_room_entries"], 2)
        self.assertEqual([s["new_items"] for s in result["progression_spheres"]],
                         [["sword key"], ["armor key"], list(routes.ITEM_NAMES[4:])])
        self.assertEqual([r["room"] for r in result["route"]],
                         [routes.room_label(r) for r in (S, A, S, B, S, C, E, O)])

    def test_collect_all_crests_then_return_to_exit(self):
        data = layout([(S, A, 0), (A, S, 0), (S, B, 0), (B, S, 0),
                       (S, E, 0xF0), (E, O, 0)],
                      [(A, 0x29), (A, 0x2C), (B, 0x2D), (B, 0x2E)])
        result = routes.shortest_escape(data)
        self.assertEqual(result["door_crossings"], 6)
        self.assertEqual(result["repeated_room_entries"], 2)

    def test_unreachable_key_is_not_assumed_owned(self):
        data = layout([(S, A, 1), (A, E, 0xF0), (E, O, 0)], [(A, 0x33)] + crests(A))
        self.assertIsNone(routes.shortest_escape(data))
        self.assertEqual(routes.evaluate(data)["status"], "no_escape_in_model")

    def test_edges_are_directed_and_no_exit_teleport(self):
        data = layout([(S, A, 0), (S, E, 0xF0), (E, O, 0)], crests(A))
        self.assertIsNone(routes.shortest_escape(data))

    def test_external_detour_cannot_bypass_mansion(self):
        data = layout([(S, O, 0), (O, E, 0), (E, O, 0)], crests(S))
        self.assertIsNone(routes.shortest_escape(data))

    def test_missing_crest_and_missing_exit(self):
        data = layout([(S, E, 0), (E, O, 0)], crests(S)[:-1])
        self.assertIsNone(routes.shortest_escape(data))
        data = layout([(S, E, 0xF0)], crests(S))
        self.assertIsNone(routes.shortest_escape(data))

    def test_generation_failure_remains_visible(self):
        result = routes.evaluate({"seed": 2, "generated": False, "attempts": 200})
        summary = routes.summarize([result], 10)
        self.assertEqual(summary["solved"], 0)
        self.assertEqual(summary["failures"], [result])
        self.assertEqual(summary["hardest_found"], [])

    def test_removed_original_item_cannot_rescue_final_layout(self):
        data = layout([(S, A, 1), (A, E, 0xF0), (E, O, 0)], crests(A))
        data["proof_only_items"] = [[*S, 0x33, 200]]
        result = routes.evaluate(data)
        self.assertEqual(result["status"], "no_escape_in_model")
        self.assertTrue(result["escape_with_removed_original_items"])
        self.assertEqual(result["progression_spheres"], [])

    def test_extraction_fails_on_changed_production_boundary(self):
        with self.assertRaises(RuntimeError):
            routes.between("start body", "start", "missing")
        self.assertIn("rnd_generate_once", routes.adapter_source())


class ProductionAdapterTests(unittest.TestCase):
    def test_hole_and_elevator_states_and_mode_guards(self):
        production = (routes.ROOT / "src/game/mods/ZombieRandom.cpp").read_text()
        commands = (routes.ROOT / "src/game/CmdFunctions.cpp").read_text()
        helper = "void zombie_mode_room_prepare(void)" + routes.between(
            production, "void zombie_mode_room_prepare(void)", "static void rnd_open_piano_room(void)")
        flag_on = "void Flg_on(int baseAddr, unsigned int bitIndex)" + routes.between(
            commands, "void Flg_on(int baseAddr, unsigned int bitIndex)",
            "// FUN_00473f10")
        # Flg_on's counterpart (CmdFunctions.cpp FUN_00473f10): MSB-first in the dword.
        flag_off = r'''
static void FUN_00473f10(int* baseAddr, unsigned int bitIndex)
{
    unsigned int* word = (unsigned int*)((unsigned char*)baseAddr + ((bitIndex & 0xFFFFFFE7u) >> 3));
    *word &= ~(0x80000000u >> (bitIndex & 0x1F));
}
'''
        fixture = r'''
#include <cassert>
#include <cstring>
enum { RND_STAGE_2F=6, ROOM_LESSON_ROOM=12, ROOM_MANSION_B1_PASSAGE_1=26,
       ROOM_MANSION_KITCHEN=28 };
static int g_stageId, g_roomId;
static bool armed, holeOpen;
static unsigned int g_ScenarioFlags[8];
static bool zombie_mode_armed() { return armed; }
static bool zm_yawn_hole_open() { return holeOpen; }
'''
        checks = r'''
int main() {
    for (int open=0; open<2; ++open)
    for (int mode=0; mode<2; ++mode)
        for (int stage=0; stage<8; ++stage)
            for (int room=0; room<29; ++room) {
                memset(g_ScenarioFlags,0,sizeof(g_ScenarioFlags));
                g_ScenarioFlags[0]=0x80000001; g_ScenarioFlags[7]=0x1000;
                armed=mode!=0; holeOpen=open!=0; g_stageId=stage; g_roomId=room;
                // The hole's bits left over (an old state) are cleared while it is shut.
                if (mode && stage==6 && (room==12 || room==26) && !open) g_ScenarioFlags[1]=0x01800000;
                zombie_mode_room_prepare();
                unsigned int expected=0;
                if (mode && stage==6) {
                    // The hole: bits 0x27, 0x28 once Yawn 2 is beaten, else clear.
                    if ((room==12 || room==26) && open) expected=0x01800000;
                    if (room==28) expected=0x00001000; // bit 0x33
                }
                assert(g_ScenarioFlags[0]==0x80000001 && g_ScenarioFlags[1]==expected);
                for (int i=2;i<7;++i) assert(g_ScenarioFlags[i]==0);
                assert(g_ScenarioFlags[7]==0x1000);
                zombie_mode_room_prepare(); assert(g_ScenarioFlags[1]==expected);
                // Restored/re-entered room initialization reapplies the state.
                g_ScenarioFlags[1]=0; zombie_mode_room_prepare();
                assert(g_ScenarioFlags[1]==expected);
            }
}
'''
        with tempfile.TemporaryDirectory(prefix="re1-room-access-test-") as temp:
            with patch.object(routes, "adapter_source", return_value=fixture + flag_on + flag_off + helper + checks):
                exe = routes.build_adapter(Path(temp), COMPILER)
            subprocess.run([str(exe)], check=True)
        room_init = (routes.ROOT / "src/game/RoomInit.cpp").read_text()
        self.assertLess(room_init.index("zombie_mode_room_prepare();"),
                        room_init.index("run_command_functions((unsigned short*)g_RoomInitScd);"))

    def test_piano_wall_open_and_mode_guards(self):
        production = (routes.ROOT / "src/game/mods/ZombieRandom.cpp").read_text()
        helper = "static void rnd_open_piano_room(void)" + routes.between(
            production, "static void rnd_open_piano_room(void)", "void zm_random_room_loaded(void)")
        fixture = r'''
#include <cassert>
#include <cstring>
enum { RND_STAGE_1F=5, ROOM_MANSION_BAR=15 };
static bool s_active, piano;
static bool zm_piano_open() { return piano; }
static int g_stageId, g_roomId;
static struct Rdt { int omodel_slot_count; } rdt, *g_RdtPointer;
static void* g_omodel_table[1];
'''
        checks = r'''
int main() {
    unsigned char wall[0xA4], saved[0xA4];
    memset(wall, 0x35, sizeof(wall)); memcpy(saved, wall, sizeof(wall));
    g_stageId=5; g_roomId=15; rdt.omodel_slot_count=1;
    g_RdtPointer=&rdt; g_omodel_table[0]=wall;
    // Original game, another room/stage, and incomplete loads remain unchanged.
    rnd_open_piano_room(); assert(!memcmp(wall,saved,sizeof(wall)));
    // The wall stands until the piano is played (ZombiePiano.cpp).
    s_active=true; rnd_open_piano_room(); assert(!memcmp(wall,saved,sizeof(wall)));
    piano=true; g_stageId=0; rnd_open_piano_room(); assert(wall[0]==0x35);
    g_stageId=5; g_roomId=14; rnd_open_piano_room(); assert(wall[0]==0x35);
    g_roomId=15; g_RdtPointer=nullptr; rnd_open_piano_room(); assert(wall[0]==0x35);
    g_RdtPointer=&rdt; rdt.omodel_slot_count=0; rnd_open_piano_room(); assert(wall[0]==0x35);
    rdt.omodel_slot_count=1; g_omodel_table[0]=nullptr; rnd_open_piano_room();
    g_omodel_table[0]=wall; rnd_open_piano_room();
    saved[0]&=~1; assert(!memcmp(wall,saved,sizeof(wall)));
    // Emblem animations moving the inactive model cannot restore collision.
    memset(wall+0x34,0xFF,12); rnd_open_piano_room(); assert(!(wall[0]&1));
    // Fresh room initialization closes the model; the hook opens it on re-entry.
    wall[0]=0x35; rnd_open_piano_room(); assert(wall[0]==0x34);
}
'''
        with tempfile.TemporaryDirectory(prefix="re1-piano-test-") as temp:
            with patch.object(routes, "adapter_source", return_value=fixture + helper + checks):
                exe = routes.build_adapter(Path(temp), COMPILER)
            subprocess.run([str(exe)], check=True)

    def test_piano_scripts_do_not_reactivate_wall(self):
        if ASSETS is None:
            self.skipTest("Pass --assets to inspect the piano RDT")
        mode = (routes.ROOT / "src/game/mods/ZombieMode.cpp").read_text()
        import re
        widths = [int(v) for v in re.findall(r"-?\d+", routes.between(
            mode, "static const signed char kScdCmdWidth[0x51] = {", "};"))]
        paths = {p.relative_to(ASSETS).as_posix().lower(): p for p in ASSETS.rglob("*") if p.is_file()}
        result = puzzle_audit.inspect(paths["stage6/room60f0.rdt"].read_bytes(), widths)
        self.assertTrue(any(m["object"] == 0 for m in result["object_movement"]))
        for command in result["object_flag_changes"]:
            op = command["bytes"]
            self.assertFalse(op[1] == 0 and op[2] == 0 and op[3] & 1,
                             "A piano script can reactivate the secret wall")

    def production_table(self, name):
        """Rows of a ZombieRandom.cpp table, with RND_/ROOM_ names resolved."""
        import re
        production = (routes.ROOT / "src/game/mods/ZombieRandom.cpp").read_text()
        types = (routes.ROOT / "src/game/Types.h").read_text()
        values = {"RND_STAGE_1F": 5, "RND_STAGE_2F": 6}
        values.update((n, int(v, 0)) for n, v in re.findall(r"#define (ROOM_\w+)\s+(0x[0-9A-Fa-f]+|\d+)", types))
        body = routes.between(production, name, "};")
        rows = []
        for row in re.findall(r"\{([^{}]*)\}", body):
            fields = [f.strip() for f in row.split(",")]
            rows.append(tuple(values.get(f, f) if not re.fullmatch(r"0x[0-9A-Fa-f]+|\d+", f) else int(f, 0)
                              for f in fields))
        return rows

    def adapter_layouts(self, seeds):
        with tempfile.TemporaryDirectory(prefix="re1-route-gates-") as temp:
            exe = routes.build_adapter(Path(temp), COMPILER)
            output = subprocess.check_output([str(exe), str(ASSETS.resolve())],
                                             input="".join(f"{seed}\n" for seed in seeds), text=True)
        return [json.loads(line) for line in output.splitlines()]

    def test_door_gates_reviewed_and_match_audit(self):
        if ASSETS is None:
            self.skipTest("Pass --assets to check the gated doors against the RDTs")
        data = self.adapter_layouts([1])[0]
        self.assertEqual(data["unreviewed_gates"], 0, "A gated door is missing from kDoorGates")
        gated = {(a, b, slot): usable for a, b, c, d, slot, usable in data["gated_doors"]}
        # Two independent decoders must find the same gated doors.
        import re
        mode = (routes.ROOT / "src/game/mods/ZombieMode.cpp").read_text()
        widths = [int(v) for v in re.findall(r"-?\d+", routes.between(
            mode, "static const signed char kScdCmdWidth[0x51] = {", "};"))]
        paths = {p.relative_to(ASSETS).as_posix().lower(): p for p in ASSETS.rglob("*") if p.is_file()}
        audited = set()
        for stage in (5, 6):
            for room in range(0x1D):
                file_stage = 0 if stage == 5 and room in (0x1A, 0x15, 0x16) else stage
                rdt = paths[f"stage{file_stage + 1}/room{file_stage + 1}{room:02x}0.rdt"].read_bytes()
                if len(rdt) < 0x100:
                    continue
                for door in puzzle_audit.inspect(rdt, widths)["doors"]:
                    if door["gated"]:
                        audited.add((stage, room, door["slot"]))
        self.assertEqual(set(gated), audited)
        # No stale review entries, and the states the route relies on.
        table = {(stage, room, slot): state for stage, room, slot, state in self.production_table("kDoorGates[] = {")}
        self.assertEqual(set(table), set(gated))
        for key, state in table.items():
            self.assertEqual(gated[key], state == "RND_GATE_OPEN", key)
        closed = {key for key, usable in gated.items() if not usable}
        self.assertEqual(closed, {(5, 0x12, 1), (6, 0x0E, 3), (6, 0x16, 2)})
        edges = {(a, b, c, d) for a, b, c, d, *_ in data["doors"]}
        for a, b, c, d in ((5, 0x12, 5, 0x1C), (6, 0x0E, 5, 0), (6, 0x16, 6, 0x18)):
            self.assertNotIn((a, b, c, d), edges)
        for a, b, c, d in ((6, 0, 6, 0x1C), (6, 0x1C, 5, 0x10), (6, 0x0C, 6, 0x1A), (5, 5, 5, 6)):
            self.assertIn((a, b, c, d), edges)
        # The back area's ways in (ZombieKeypad.cpp): the elevator's two doors
        # need the battery, the keypad door - and the rough passage's door
        # back, which the keypad builds - the note.
        requirements = {(a, b, c, d): r for a, b, c, d, r, *_ in data["doors"]}
        self.assertEqual(requirements[6, 0x1C, 6, 0], 0x1000)
        self.assertEqual(requirements[6, 0x13, 6, 0], 0x1000)
        self.assertEqual(requirements[6, 0x01, 6, 0x14], 0x2000)
        self.assertEqual(requirements[6, 0x14, 6, 0x01], 0x2000)

    def test_progression_avoids_unverified_rooms(self):
        if ASSETS is None:
            self.skipTest("Pass --assets to check placements against the furniture rooms")
        unverified = set(self.production_table("kUnverifiedRooms[][2] = {"))
        # Every room whose events move an object is unverified or handled:
        # the piano wall is removed, the hole state prepared, and the shotgun
        # rooms take no progression.
        # The bathroom's tub stays off the floor anchors and is play-tested.
        handled = {(5, 0x0F), (5, 0x13), (5, 0x15), (5, 0x16), (6, 0x0C)}
        report = json.loads((routes.ROOT / "tools/zombie_puzzle_access_report.json").read_text())
        for room in report["rooms"]:
            stage, number = room["room"].split(":")
            key = (int(stage) - 1, int(number, 16))
            if room["object_movement"]:
                self.assertIn(key, unverified | handled, f"{room['room']} moves furniture")
        for data in self.adapter_layouts(range(1, 201)):
            self.assertTrue(data["generated"])
            for stage, room, item, flag, *_ in data["items"] + data["tools"]:
                self.assertNotIn((stage, room), unverified, f"seed {data['seed']} item {item:02X}")
            self.assertIsNotNone(routes.shortest_escape(data), f"seed {data['seed']}")

    def test_route_time_estimate(self):
        if ASSETS is None:
            self.skipTest("Pass --assets to estimate route times over the real mansion")
        import estimate_zombie_route_time as timing
        geometry = timing.room_geometry(ASSETS.resolve())
        # Door arrival points sit next to the way back in the room they lead to.
        near = 0
        total = 0
        for (stage, room), rooms in geometry.items():
            for slot, (zone, arrival) in rooms["doors"].items():
                total += 1
                for other in geometry.values():
                    for zone2, _ in other["doors"].values():
                        x, z, w, d = zone2
                        if x - 1000 <= arrival[0] <= x + w + 1000 and z - 1000 <= arrival[1] <= z + d + 1000:
                            near += 1
                            break
                    else:
                        continue
                    break
        self.assertGreater(near / total, 0.9)
        args = argparse.Namespace(detour=1.35, door=0.75, pickup=2.0, crests=16.0)
        for data in self.adapter_layouts([1, 11]):
            model = timing.Model(data, geometry, args)
            solo, held = model.solo()
            self.assertIsNotNone(solo)
            self.assertEqual(held & 0xF0, 0xF0)
            team, _ = model.team(3, held)
            self.assertGreater(solo, 60)
            self.assertLess(solo, 20 * 60)
            # Slower doors can only make the run longer.
            slow = timing.Model(data, geometry, argparse.Namespace(detour=1.35, door=3.0, pickup=2.0, crests=16.0))
            self.assertGreater(slow.solo()[0], solo)

    def test_reach_needs_a_way_back_and_a_live_trigger(self):
        source = routes.adapter_source().replace("int main(int argc, char** argv) {",
                                                 "int adapter_main(int argc, char** argv) {")
        checks = r'''
#include <cassert>
static void door(int a, int b, bool usable = true) {
    RndDoor& d = s_doors[s_doorCount++];
    memset(&d, 0, sizeof(d));
    d.fromStage = d.toStage = RND_STAGE_1F;
    d.fromRoom = (unsigned char)a; d.toRoom = (unsigned char)b; d.usable = usable;
}
static void crest(int room) {
    RndSpot& s = s_spots[s_spotCount++];
    memset(&s, 0, sizeof(s));
    s.stage = RND_STAGE_1F; s.room = (unsigned char)room; s.pool = true; s.id = ITEM_WIND_CREST;
}
int main() {
    bool reach[RND_ROOMS * 2];
    const int hall = ROOM_MAIN_HALL;
    // A one-way drop into room 1: reached, but nothing there counts.
    door(hall, 1); crest(1);
    assert(rnd_reach(0, true, reach) == 0 && !reach[1] && reach[hall]);
    // A way back makes the room and its crest count.
    door(1, 2); door(2, hall);
    assert(rnd_reach(0, true, reach) == 0x10 && reach[1] && reach[2]);
    // A dead trigger is no way back.
    s_doorCount = 0; door(hall, 1); door(1, hall, false);
    assert(rnd_reach(0, true, reach) == 0 && !reach[1]);
    // ... nor a way in.
    s_doorCount = 0; door(hall, 1, false); door(1, hall);
    assert(rnd_reach(0, true, reach) == 0 && !reach[1]);
}
'''
        with tempfile.TemporaryDirectory(prefix="re1-reach-test-") as temp:
            with patch.object(routes, "adapter_source", return_value=source + checks):
                exe = routes.build_adapter(Path(temp), COMPILER)
            subprocess.run([str(exe)], check=True)

    def test_real_assets_replay_and_seed_zero_alias(self):
        if ASSETS is None:
            self.skipTest("Pass --assets to compile and verify real production layouts")
        with tempfile.TemporaryDirectory(prefix="re1-route-test-") as temp:
            exe = routes.build_adapter(Path(temp), COMPILER)
            output = subprocess.check_output([str(exe), str(ASSETS.resolve())],
                                             input="1\n11\n1\n0\n1592594996\n", text=True)
        layouts = [json.loads(line) for line in output.splitlines()]
        self.assertEqual(len(layouts), 5)
        self.assertEqual(layouts[0], layouts[2])
        self.assertEqual(layouts[3], layouts[4])  # 0x5EED1234
        for data in layouts:
            self.assertTrue(data["generated"])
            self.assertIsNotNone(routes.shortest_escape(data),
                                 f"Final pickup graph cannot escape for seed {data['seed']}")
            self.assertEqual(len(data["items"]), 10)
            self.assertEqual(len({tuple(i[:2]) for i in data["items"]}), 10)
            self.assertEqual({i[2] for i in data["items"]}, set(routes.ITEM_IDS + routes.ACCESS_IDS))
            self.assertEqual(sorted(i[2] for i in data["tools"]), [0x1C, 0x23, 0x26, 0x4C])
            # Every key item in a room of its own, in a leaf room - but the
            # battery, which keeps out of them, and the back area's crests.
            back = {(6, 0x13), (6, 0x14), (6, 0x16), (6, 0x17), (6, 0x18), (6, 0x19)}
            leaves = {tuple(l) for l in data["leaves"]}
            rooms = [tuple(i[:2]) for i in data["items"] + data["tools"]]
            self.assertEqual(len(rooms), len(set(rooms)))
            leafy = {tuple(i[:2]) for i in data["items"] + data["tools"]
                     if i[2] != 0x27 and tuple(i[:2]) not in back}
            self.assertTrue(leafy <= leaves, data["seed"])
            # A crest always lies in the back area, no key or way in does,
            # and either way in alone reaches the exit.
            self.assertTrue(any(tuple(i[:2]) in back for i in data["items"] if i[2] in routes.ITEM_IDS[4:]))
            for stage, room, item, *_ in data["items"]:
                if item in routes.ITEM_IDS[:4] + routes.ACCESS_IDS:
                    self.assertNotIn((stage, room), back, data["seed"])
            for gone in routes.ACCESS_IDS:
                alone = {**data, "items": [i for i in data["items"] if i[2] != gone]}
                self.assertIsNotNone(routes.shortest_escape(alone), (data["seed"], gone))
            alone = {**data, "items": [i for i in data["items"] if i[2] not in routes.ACCESS_IDS]}
            self.assertIsNone(routes.shortest_escape(alone), data["seed"])
            for stage, room, item, flag, *_ in data["tools"]:
                self.assertNotIn((stage, room), ((5, 21), (5, 22)))
            # The puzzles' weapons: two light, two Pythons, one rocket
            # launcher, by cost; the piano never takes the rocket launcher.
            weapons = [w for _, _, _, w in data["puzzles"]]
            self.assertEqual(sorted(weapons, key=[0x06, 0x07, 0x05, 0x0A].index)[2:], [0x05, 0x05, 0x0A])
            self.assertEqual(len([w for w in weapons if w in (0x06, 0x07)]), 2)
            self.assertNotEqual(weapons[0], 0x0A)
            tier = {0x06: 0, 0x07: 0, 0x05: 1, 0x0A: 2}
            costs = [c if c >= 0 else 1 << 30 for _, _, c, _ in data["puzzles"]]
            for p in range(5):
                for q in range(5):
                    if costs[p] < costs[q] and 0 not in (p, q):
                        self.assertLessEqual(tier[weapons[p]], tier[weapons[q]], data["seed"])
            for a, b, c, d, requirement, *_ in data["doors"]:
                self.assertIn(requirement, (0, 1, 2, 4, 8, 240, 0x1000, 0x2000))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--compiler")
    args, remaining = parser.parse_known_args()
    ASSETS, COMPILER = args.assets, args.compiler
    unittest.main(argv=[sys.argv[0], *remaining])
