"""CPU-only reconnect transport regressions. Never launches the game.
Run in an x86 VS prompt, or use --compiler g++ on Linux.
Compiles the production packet handlers against an in-memory UDP transport.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^(?:static )?(?:void|bool|int|unsigned int|unsigned char|const char\*) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


FIXTURE = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <deque>
enum { ZM_NET_OFF, ZM_NET_ZOMBIE, ZM_NET_SURVIVOR };
enum { ZM_NET_IDLE, ZM_NET_HOSTING, ZM_NET_JOINING, ZM_NET_CONNECTED, ZM_NET_LOST, ZM_NET_FAILED, ZM_NET_EXPIRED };
enum { ZM_NET_MAX_PLAYERS=4, ZM_NET_DIRECTOR=0, ZM_NET_ALL=255, ZM_NET_JOINTS=15,
       ZM_CHAR_CHRIS=0, ZM_CHAR_COUNT=6, ZM_VOTE_NONE=0, ZM_VOTE_ACCEPT=1, ZM_LOBBY_JOIN=0,
       ZM_EV_WIN=9, ZM_EV_REVIVE=21, ZM_EV_PICKUP=30, ZM_EV_SHOTGUN=31, ZM_EV_PIANO=32, ZM_EV_TIMEOUT=34, ZM_EV_BOSS=35, ZM_EV_DROP=18, ZM_EV_DROP_TAKE=19,
       ZM_EV_ROSTER=4, ZM_EV_BOX=16, ZM_END_ESCAPE=0, ITEM_SHOTGUN=3, ZM_RECONNECT_BLOB_MAX=65536 };
struct BioCardLayout { unsigned char totalHeldItems, stageId, roomId, equippedItemId; };
struct ItemSlot { unsigned char Id,qty; };
struct ZmReconnectPlayer { unsigned int sequence; BioCardLayout card; int x,y,z; short health; unsigned short boxToken; unsigned char inventory[16]; };
struct ZmDeathPlayback { unsigned char bytes[64]; };
struct ZmMonsterAppearance { unsigned char bytes[24]; };
struct ZmYawnBody { unsigned char bytes[86]; };
struct PlatNetAddr { unsigned int ip; unsigned short port; };
struct Pose { int x,y,z; short angle; unsigned char jointCount; short root[3], rot[15][3]; };
struct ZmNetPeerState {
    bool valid,dead,spectating,transitioning,attacked,hasZombie,roomPairBusy;
    unsigned char stage,room,viewStage,viewRoom,camera,entranceDoor,weapon,zombieSlot,jointCount;
    int x,y,z; short angle,health,root[3],rot[15][3]; unsigned int receivedMs; Pose zombie;
};
static unsigned int now=1000;
static bool matchOver;
static unsigned int plat_time_ms() { return now; }
static bool zombie_mode_match_over() { return matchOver; }
static bool zombie_mode_armed() { return true; }
static bool zombie_mode_live_menu() { return false; }
static void zombie_mode_room_entry_sound() {}
static unsigned char g_stageId=5,g_roomId=1;
static const char* GetSaveRoot() { return "."; }
static bool plat_mkdir(const char*) { return true; }
static int credentialWrites;
static bool plat_file_write_atomic(const char*,const void*,size_t) { credentialWrites++; return true; }
static void dbg_printf(const char*,...) {}
static int imported, abandoned, reconciled;
static unsigned char built[1810];
static int zm_reconnect_build(void* out,int cap,const ZmReconnectPlayer*) {
    assert(cap>=sizeof(built)); for(int i=0;i<sizeof(built);i++) built[i]=(unsigned char)(i*7);
    memcpy(out,built,sizeof(built)); return sizeof(built);
}
static bool zm_reconnect_import(const void* data,int bytes) {
    assert(bytes==sizeof(built) && !memcmp(data,built,bytes)); imported++; return true;
}
static void zm_reconnect_forget() {}
static void zm_reconnect_route_events() {}
static void zm_pickups_reconnect_reconcile(int,ZmReconnectPlayer*) { reconciled++; }
static void zm_box_reconnect_reconcile(int,ZmReconnectPlayer*) {}
static void zm_box_checkpoint(int,unsigned short) {}
static void zm_box_take(const short*,int) {}
static void zm_box_snapshot_take(unsigned int,const void*) {}
static void zm_drops_reconnect_reconcile(int,ZmReconnectPlayer*) {}
static void zm_spec_reconnect_reconcile(int,ZmReconnectPlayer*) {}
static void zm_shotgun_reconcile(int,ZmReconnectPlayer*) {}
static void zm_shotgun_take(const short*,int) {}
static void zm_piano_take(const short*,int) {}
static void zm_yawn_take(const short*,int) {}
static void zm_timeout_take(const short*,int) {}
static bool zm_timeout_active(unsigned int) { return false; }
static void zm_drops_reconnect_abandon(const ZmReconnectPlayer*) { abandoned++; }
static int zm_world_pack_flags(unsigned char* out) { memset(out,255,72); return 72; }
static void zm_world_merge_flags(const unsigned char*) {}
static bool zm_match_take_win(const short*,int) { return true; }
static void zm_pickups_take(const short*,int) {}
static void zm_drops_take_event(int,const short*,int) {}
static bool zm_revive_host_claim(int,int,int) { return false; }
static void zm_world_apply_remote(const short*,int) {}
static unsigned char* g_ItemSlotsPointer;
static int zm_net_char(int) { return -1; }   // seat 0 is the human director here
static void net_log_drop(const PlatNetAddr*,const char*,int,int) {}
static void net_take_enemies(int,const unsigned char*,int) {}
struct Datagram { PlatNetAddr addr; std::vector<unsigned char> bytes; };
static std::vector<Datagram> wire;
static std::deque<Datagram> incoming;
static void plat_net_send(const PlatNetAddr* addr,const void* data,int bytes) {
    const unsigned char* p=(const unsigned char*)data; wire.push_back({*addr,{p,p+bytes}});
}
static int plat_net_recv(void* out,int cap,PlatNetAddr* from) {
    if(incoming.empty()) return 0;
    Datagram d=incoming.front(); incoming.pop_front(); assert(d.bytes.size()<=cap);
    memcpy(out,d.bytes.data(),d.bytes.size()); *from=d.addr; return (int)d.bytes.size();
}
static unsigned int zm_game_time_ms();
'''

CHECKS = r'''
static PlatNetAddr addr1={0x7F000001,1111}, addr2={0x7F000001,2222};
static void reset_host() {
    net_reset(); now=1000; matchOver=false; wire.clear(); incoming.clear();
    s_role=ZM_NET_ZOMBIE; s_status=ZM_NET_HOSTING; s_self=0; s_seed=1234;
    s_started=s_lobbyGo=true;
    NetLink& L=s_links[1]; L.used=true; L.addr=addr1; L.epoch=11;
    L.token[0]=0xCAFE; L.token[1]=0xDEAD;
    L.lastHeardMs=now; L.nextEventId=1; L.checkpointHave=true;
    L.checkpoint.sequence=100; L.checkpoint.card.stageId=5; L.checkpoint.card.roomId=1;
    s_ready[1]=true; s_chars[1]=0; s_states[1].valid=true;
    abandoned=reconciled=imported=0;
}
static Datagram packet(int type,unsigned int epoch,const PlatNetAddr& from,const void* body=nullptr,int bytes=0) {
    NetHeader h={}; h.magic=ZM_NET_MAGIC; h.version=ZM_NET_VERSION; h.type=type; h.epoch=epoch;
    Datagram d={from,std::vector<unsigned char>(sizeof(h)+bytes)};
    memcpy(d.bytes.data(),&h,sizeof(h)); if(bytes) memcpy(d.bytes.data()+sizeof(h),body,bytes);
    return d;
}
static void deliver(const Datagram& d) { net_handle(d.bytes.data(),(int)d.bytes.size(),&d.addr); }
static Datagram last_type(int type) {
    for(int i=(int)wire.size()-1;i>=0;i--) if(((NetHeader*)wire[i].bytes.data())->type==type) return wire[i];
    assert(false); return {};
}
int main() {
    reset_host();
    NetStateBody leased={};leased.stage=5;leased.room=1;leased.flags=1|0x40;
    net_take_state(1,(const unsigned char*)&leased,sizeof(leased));
    assert(s_states[1].roomPairBusy);
    leased.flags=1;net_take_state(1,(const unsigned char*)&leased,sizeof(leased));
    assert(!s_states[1].roomPairBusy);

    // Five seconds of silence starts one 30-second hold, retaining the corpse/
    // survivor for ownership and the host's all-dead check until expiry.
    reset_host(); unsigned int before=zm_game_time_ms(); now+=5001; zm_net_poll();
    assert(s_links[1].graceAt && s_states[1].valid && zm_net_pause_active());
    unsigned int frozen=zm_game_time_ms(); now+=12000; zm_net_poll();
    assert(zm_game_time_ms()==frozen && zm_net_pause_seconds()==18);
    // The same process resumes its reliable stream without resetting the seat.
    deliver(packet(PKT_PING,11,addr1)); zm_net_poll();
    assert(!zm_net_pause_active() && !s_links[1].graceAt && s_states[1].valid);
    assert(zm_game_time_ms()==frozen); now+=100; assert(zm_game_time_ms()==frozen+100);
    // A different endpoint requires the saved token and same match seed.
    reset_host(); now+=6000; zm_net_poll();
    NetHelloBody hello={}; hello.seed=s_seed; hello.token[0]=0xCAFE; hello.token[1]=0xBAD;
    deliver(packet(PKT_HELLO,99,addr2,&hello,sizeof(hello)));
    assert(s_links[1].epoch==11 && ((NetHeader*)last_type(PKT_REJECT).bytes.data())->epoch==99);
    hello.token[1]=0xDEAD;
    deliver(packet(PKT_HELLO,99,addr2,&hello,sizeof(hello)));
    assert(s_links[1].epoch==99 && s_links[1].addr.port==addr2.port && s_links[1].recovering);
    assert(s_links[1].pendingCount==0 && s_links[1].lastReceived==0 && reconciled==1);
    Datagram welcome=last_type(PKT_GO);
    NetLobbyBody lobby=*(NetLobbyBody*)(welcome.bytes.data()+sizeof(NetHeader));
    assert(lobby.index==1 && lobby.recoveryBytes==sizeof(built));
    unsigned int lastHeard=s_links[1].lastHeardMs; now+=100;
    deliver(packet(PKT_PING,11,addr2)); assert(s_links[1].lastHeardMs==lastHeard);
    // Stop-and-wait chunks can be retried and are bounded at the last chunk.
    std::vector<Datagram> chunks;
    for(unsigned int offset=0;offset<sizeof(built);offset+=900) {
        deliver(packet(PKT_RECOVERY_GET,99,addr2,&offset,sizeof(offset)));
        chunks.push_back(last_type(PKT_RECOVERY_CHUNK));
        deliver(packet(PKT_RECOVERY_GET,99,addr2,&offset,sizeof(offset)));
        assert(last_type(PKT_RECOVERY_CHUNK).bytes==chunks.back().bytes);
    }
    deliver(packet(PKT_RECOVERY_READY,99,addr2));
    assert(!s_links[1].recovering && !s_links[1].graceAt && !zm_net_pause_active());
    // Expired seats cannot reclaim a new grace period; loot is preserved once.
    reset_host(); now+=6000; zm_net_poll(); now+=30000; zm_net_poll();
    assert(s_links[1].expired && !s_states[1].valid && !zm_net_pause_active() && abandoned==1);
    deliver(packet(PKT_PING,11,addr1)); zm_net_poll(); assert(abandoned==1 && s_links[1].expired);
    // Client download rejects out-of-order/stale chunks and old pause notices.
    net_reset(); now=1000; s_role=ZM_NET_SURVIVOR; s_status=ZM_NET_JOINING;
    s_rejoining=true; s_self=-1; s_links[0].used=true; s_links[0].addr=addr2; s_links[0].epoch=99;
    deliver(welcome); assert(s_self==1 && s_status==ZM_NET_CONNECTED && s_downloadTotal==sizeof(built));
    deliver(chunks[1]); assert(s_downloadOffset==0);
    deliver(chunks[0]); deliver(chunks[0]); assert(s_downloadOffset==900);
    deliver(chunks[1]); deliver(chunks[2]); assert(s_downloaded && imported==1);
    NetPauseBody pause={10,30000,2,2};
    deliver(packet(PKT_PAUSE,99,addr2,&pause,sizeof(pause))); assert(zm_net_pause_active());
    pause.serial=9; pause.remainingMs=0;
    deliver(packet(PKT_PAUSE,99,addr2,&pause,sizeof(pause))); assert(zm_net_pause_active());
    pause.serial=11; pause.missing=0; s_rejoinLoadReady=true;
    deliver(packet(PKT_PAUSE,99,addr2,&pause,sizeof(pause))); assert(!zm_net_pause_active() && !s_rejoining);
    // Client-only loss freezes locally, then resumes or ends after 30 seconds.
    s_started=true; s_links[0].lastHeardMs=now;
    now+=5001; zm_net_poll(); assert(s_status==ZM_NET_LOST && zm_net_pause_active());
    now+=1000; deliver(packet(PKT_PING,99,addr2)); assert(s_status==ZM_NET_CONNECTED && !s_localLostAt);
    now+=6000; zm_net_poll(); now+=30000; zm_net_poll();
    assert(s_status==ZM_NET_EXPIRED && !zm_net_pause_active());
    // Incompatible packets are diagnosed using the stable six-byte prefix,
    // even when an older peer's full header is shorter. Ignore strangers.
    net_reset();s_role=ZM_NET_SURVIVOR;s_status=ZM_NET_JOINING;
    s_links[0].used=true;s_links[0].addr=addr2;s_links[0].epoch=99;s_links[0].lastHeardMs=now;
    Datagram old=packet(PKT_WELCOME,99,addr1);old.bytes[4]--;old.bytes.resize(6);
    deliver(old);assert(s_status==ZM_NET_JOINING);
    old.addr=addr2;deliver(old);assert(s_status==ZM_NET_FAILED);
    assert(!strcmp(zm_net_status_text(),"GAME VERSION MISMATCH")&&!strcmp(zm_net_status_hint(),"UPDATE EVERY PLAYER TO THE SAME BUILD"));
    // A host responds to an incompatible HELLO without assigning a seat.
    reset_host();old=packet(PKT_HELLO,99,addr2);old.bytes[4]--;old.bytes.resize(6);deliver(old);
    assert(((const NetHeader*)wire.back().bytes.data())->type==PKT_VERSION_REJECT&&!s_links[2].used);
    // Explicit rejection also works without matching a connection epoch.
    net_reset();s_role=ZM_NET_SURVIVOR;s_status=ZM_NET_JOINING;
    s_links[0].used=true;s_links[0].addr=addr2;s_links[0].epoch=99;s_links[0].lastHeardMs=now;
    deliver(packet(PKT_VERSION_REJECT,0,addr2));assert(s_status==ZM_NET_FAILED);
    // An old host that silently drops packets cannot report its version;
    // timeout guidance explains all plausible connection problems instead.
    net_reset();s_role=ZM_NET_SURVIVOR;s_status=ZM_NET_JOINING;
    s_links[0].used=true;s_links[0].addr=addr2;s_links[0].lastHeardMs=now;
    now+=10001;zm_net_poll();assert(s_status==ZM_NET_FAILED);
    assert(!strcmp(zm_net_status_hint(),"CHECK ADDRESS, PORT AND GAME VERSION"));
    puts("Reconnect transport, expiry, version-mismatch diagnostics and timeout guidance checks passed.");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="cl")
    args = parser.parse_args()
    net = (ROOT / "src/game/mods/ZombieNet.cpp").read_text(encoding="utf-8")
    constants = net[net.index("#define ZM_NET_MAGIC"):net.index("#pragma pack(push")]
    types = net[net.index("#pragma pack(push"):net.index("#define ZM_NET_MAX_PENDING")]
    globals_ = net[net.index("#define ZM_NET_MAX_PENDING"):net.index("const char* zm_char_name")]
    source = FIXTURE + constants + types + globals_
    for name in ["zm_net_status_text", "zm_net_status_hint", "zm_net_active", "net_grant_char", "net_send_link", "net_send_all", "net_queue",
                 "net_route", "net_send_lobby", "net_take_state", "net_take_event", "net_link_of",
                 "net_pause_broadcast", "net_begin_grace", "net_reject", "net_expire_link",
                 "net_accept_rejoin", "net_handle", "net_take_local", "net_drain_loopback",
                 "zm_net_poll", "net_reset"]:
        source += function(net, name)
    with tempfile.TemporaryDirectory(prefix="re1-reconnect-") as temp:
        directory = Path(temp)
        cpp, exe = directory / "test.cpp", directory / "test.exe"
        cpp.write_text(source + CHECKS, encoding="utf-8")
        if Path(args.compiler).stem.lower() == "cl":
            command = [args.compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe" + str(exe)]
        else:
            command = [args.compiler, "-m32", "-std=c++17", str(cpp), "-o", str(exe)]
        subprocess.run(command, cwd=directory, check=True)
        subprocess.run([str(exe)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
