// Netplay.cpp - co-op over the network (port addition, not in the original).
// See Netplay.h.
#include "Netplay.h"
#include "Coop.h"
#include "../platform/platform.h"
#include "../system/AssetPath.h"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstddef>

int  g_netMode = NET_MODE_OFF;
char g_netPeer[128] = "";
int  g_netPort = 27015;
int  g_netDelay = 3;
int  g_netLocalInput = NET_INPUT_BOTH;

#define NETPLAY_PROTOCOL     1
#define NETPLAY_MAGIC        0x4E314552u   // "RE1N"
#define NETPLAY_RING         256           // ticks of input kept (power of two)
#define NETPLAY_SEND_SPAN    16            // inputs per packet (redundancy against loss)
#define NETPLAY_LOG_TICKS    300           // a full state line in the log every 10 seconds
#define NETPLAY_RESEND_MS    10
#define NETPLAY_STALL_LOG_MS 3000
#define NETPLAY_TIMEOUT_MS   10000

enum { PKT_HELLO = 1, PKT_WELCOME = 2, PKT_INPUT = 3, PKT_BYE = 4 };

#pragma pack(push, 1)
struct NetPacket {
	unsigned int   magic;
	unsigned char  type;
	unsigned char  count;          // inputs carried (PKT_INPUT)
	unsigned short protocol;
	unsigned int   settingsHash;   // HELLO / WELCOME: the session settings
	unsigned int   firstTick;      // tick of inputs[0]
	unsigned int   checkFirst;     // tick of checks[0]
	unsigned char  checkCount;
	unsigned char  pad[3];
	unsigned int   inputs[NETPLAY_SEND_SPAN];
	unsigned int   checks[NETPLAY_SEND_SPAN];   // state hashes at the start of those ticks
};
#pragma pack(pop)

static int          s_state = 0;       // 0 not started, 1 waiting, 2 active, -1 failed
static unsigned int s_tick = 0;        // the tick about to run
static unsigned int s_local[NETPLAY_RING];
static unsigned int s_remote[NETPLAY_RING];
static unsigned int s_remoteTick[NETPLAY_RING];   // which tick each s_remote slot holds (+1; 0 = none)
static unsigned int s_localNewest = 0;            // newest tick with a local input, +1
// Every tick's state, hashed and kept a while: the hashes travel with the
// inputs, and the snapshot of the first tick the two games disagree on goes in
// the log.
struct NetSnapshot {
	unsigned int tick, rand, timer;
	short        task[3][2];                    // state, sleep counter
	unsigned int msf, msf2;
	unsigned int msg, menuChoice;
	int          fade, fadeCounter;
	int          stage, room, camera;
	int          p1x, p1z, p1a, p1hp, p1anim, p1beh, p1frame;
	int          p2x, p2z, p2a, p2hp;
	int          enemies;
	unsigned int enemyHash;
	unsigned int hash;
};
static NetSnapshot  s_snap[NETPLAY_RING];
static unsigned int s_snapNewest = 0;                     // ticks snapshotted
static unsigned int s_remoteCheck[NETPLAY_RING];
static unsigned int s_remoteCheckTick[NETPLAY_RING];      // +1; 0 = none
static int          s_desynced = 0;
static DWORD        s_lastSend = 0;
static DWORD        s_lastHello = 0;

// ----------------------------------------------------------------------------
static void net_log(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	FILE* f = fopen("netplay.log", "a");
	if (f != NULL) {
		fputs(buf, f);
		fclose(f);
	}
	fputs(buf, stderr);
}

static unsigned int fnv(unsigned int h, const void* data, size_t size)
{
	const unsigned char* p = (const unsigned char*)data;
	for (size_t i = 0; i < size; i++) {
		h = (h ^ p[i]) * 16777619u;
	}
	return h;
}

// Everything both machines have to agree on before the first tick.
static unsigned int settings_hash(void)
{
	int values[] = {
		NETPLAY_PROTOCOL, g_netDelay, g_coopSkipOpening, (int)g_GameMode,
		g_DcDifficulty, GetAssetVersion(), (int)sizeof(PlayerEntity), (int)sizeof(Entity),
	};
	return fnv(2166136261u, values, sizeof(values));
}

int Netplay_Configured(void)
{
	return g_netMode != NET_MODE_OFF;
}

int Netplay_IsActive(void)
{
	return s_state == 2;
}

// The session is over (the other player left, or stopped answering): the two
// games can no longer agree, so this one closes too.
static void end_session(void)
{
	s_state = -1;
	plat_net_close();
	g_bQuitFlag = TRUE;
}

static void send_packet(unsigned char type)
{
	NetPacket pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.magic = NETPLAY_MAGIC;
	pkt.type = type;
	pkt.protocol = NETPLAY_PROTOCOL;
	pkt.settingsHash = settings_hash();
	if (type == PKT_INPUT) {
		unsigned int last = s_localNewest;   // one past the newest
		unsigned int first = (last > NETPLAY_SEND_SPAN) ? last - NETPLAY_SEND_SPAN : 0;
		pkt.firstTick = first;
		pkt.count = (unsigned char)(last - first);
		for (unsigned int t = first; t < last; t++) {
			pkt.inputs[t - first] = s_local[t & (NETPLAY_RING - 1)];
		}
		unsigned int clast = s_snapNewest;
		unsigned int cfirst = (clast > NETPLAY_SEND_SPAN) ? clast - NETPLAY_SEND_SPAN : 0;
		pkt.checkFirst = cfirst;
		pkt.checkCount = (unsigned char)(clast - cfirst);
		for (unsigned int t = cfirst; t < clast; t++) {
			pkt.checks[t - cfirst] = s_snap[t & (NETPLAY_RING - 1)].hash;
		}
	}
	plat_net_send(&pkt, sizeof(pkt));
	s_lastSend = plat_time_ms();
}

static void dump_snapshot(const NetSnapshot* n)
{
	net_log("[NET]   tick %u: rand %08x timer %u | tasks %d/%d %d/%d %d/%d | msf %08x msf2 %08x msg %04x "
	        "choice %02x | fade %d/%d | room %d/%02x cam %d\n",
	        n->tick, n->rand, n->timer, n->task[0][0], n->task[0][1], n->task[1][0], n->task[1][1],
	        n->task[2][0], n->task[2][1], n->msf, n->msf2, n->msg, n->menuChoice, n->fade, n->fadeCounter,
	        n->stage, n->room, n->camera);
	net_log("[NET]   P1 %d,%d a%d hp%d s%d b%d f%d | P2 %d,%d a%d hp%d | %d enemies %08x\n",
	        n->p1x, n->p1z, n->p1a, n->p1hp, n->p1anim, n->p1beh, n->p1frame,
	        n->p2x, n->p2z, n->p2a, n->p2hp, n->enemies, n->enemyHash);
}

static void compare_check(unsigned int t)
{
	unsigned int slot = t & (NETPLAY_RING - 1);
	if (s_desynced || t >= s_snapNewest || s_snap[slot].tick != t || s_remoteCheckTick[slot] != t + 1) {
		return;
	}
	if (s_remoteCheck[slot] != s_snap[slot].hash) {
		s_desynced = 1;
		net_log("[NET] DESYNC at tick %u (ours %08x, theirs %08x). Our state then, and the tick before - "
		        "compare with the other netplay.log:\n", t, s_snap[slot].hash, s_remoteCheck[slot]);
		if (t > 0) {
			dump_snapshot(&s_snap[(t - 1) & (NETPLAY_RING - 1)]);
		}
		dump_snapshot(&s_snap[slot]);
	}
}

static void receive_all(void)
{
	NetPacket pkt;
	int n;
	while ((n = plat_net_recv(&pkt, sizeof(pkt))) >= 0) {
		if (n != (int)sizeof(pkt) || pkt.magic != NETPLAY_MAGIC || pkt.protocol != NETPLAY_PROTOCOL) {
			continue;
		}
		if (pkt.type == PKT_HELLO || pkt.type == PKT_WELCOME) {
			if (pkt.settingsHash != settings_hash()) {
				net_log("[NET] the other game's settings differ (Delay, [Coop] SkipOpening, game mode or "
				        "data version) - make both config.ini match\n");
				s_state = -1;
				return;
			}
			if (g_netMode == NET_MODE_HOST && pkt.type == PKT_HELLO) {
				send_packet(PKT_WELCOME);
			}
			if (s_state == 1) {
				s_state = 2;
				net_log("[NET] connected (%s), input delay %d ticks\n",
				        g_netMode == NET_MODE_HOST ? "host, player 1" : "joined, player 2", g_netDelay);
			}
			continue;
		}
		if (pkt.type == PKT_BYE) {
			if (s_state == 2) {
				net_log("[NET] the other player left at tick %u\n", s_tick);
				end_session();
			}
			return;
		}
		if (pkt.type == PKT_INPUT && pkt.count <= NETPLAY_SEND_SPAN) {
			for (unsigned int i = 0; i < pkt.count; i++) {
				unsigned int t = pkt.firstTick + i;
				s_remote[t & (NETPLAY_RING - 1)] = pkt.inputs[i];
				s_remoteTick[t & (NETPLAY_RING - 1)] = t + 1;
			}
			if (pkt.checkCount <= NETPLAY_SEND_SPAN) {
				for (unsigned int i = 0; i < pkt.checkCount; i++) {
					unsigned int t = pkt.checkFirst + i;
					s_remoteCheck[t & (NETPLAY_RING - 1)] = pkt.checks[i];
					s_remoteCheckTick[t & (NETPLAY_RING - 1)] = t + 1;
					compare_check(t);
				}
			}
		}
	}
}

int Netplay_Connect(void)
{
	if (s_state == 0) {
		s_state = -1;
		if (!plat_net_open(g_netMode == NET_MODE_HOST ? (unsigned short)g_netPort : 0)) {
			net_log("[NET] could not open UDP port %d\n", g_netPort);
			return -1;
		}
		if (g_netMode == NET_MODE_JOIN &&
		    !plat_net_set_peer(g_netPeer, (unsigned short)g_netPort)) {
			net_log("[NET] could not find the host '%s'\n", g_netPeer);
			return -1;
		}
		// The first ticks run on empty input, the same on both machines.
		for (int t = 0; t < g_netDelay; t++) {
			s_local[t] = 0;
			s_remote[t] = 0;
			s_remoteTick[t] = (unsigned int)t + 1;
		}
		s_localNewest = (unsigned int)g_netDelay;
		s_state = 1;
		net_log("[NET] %s on port %d...\n",
		        g_netMode == NET_MODE_HOST ? "hosting, waiting for player 2" : "joining", g_netPort);
	}
	if (s_state != 1) {
		return s_state == 2 ? 1 : -1;
	}
	if (g_netMode == NET_MODE_JOIN && plat_time_ms() - s_lastHello > 250) {
		send_packet(PKT_HELLO);
		s_lastHello = plat_time_ms();
	}
	receive_all();
	return s_state == 2 ? 1 : (s_state == -1 ? -1 : 0);
}

// ----------------------------------------------------------------------------
// The state at the start of a tick: what diverges first when the two games
// stop agreeing.
static void take_snapshot(NetSnapshot* n)
{
	memset(n, 0, sizeof(*n));
	n->tick = s_tick;
	n->rand = re1_rand_state();
	n->timer = (unsigned int)Game_timer;
	for (int i = 0; i < 3; i++) {
		n->task[i][0] = g_TasksTable[i].state;
		n->task[i][1] = g_TasksTable[i].sleepCounter;
	}
	n->msf = g_main_state_flags;
	n->msf2 = g_main_state_flags2;
	n->msg = (unsigned short)g_message_flags;
	n->menuChoice = (unsigned char)g_menu_choice_id;
	n->fade = (short)g_fading_state;
	n->fadeCounter = g_fading_counter;
	n->stage = g_stageId;
	n->room = g_roomId;
	n->camera = g_roomCameraId;
	n->p1x = g_playerEntity.scaMatrixData.localMatrix.t[0];
	n->p1z = g_playerEntity.scaMatrixData.localMatrix.t[2];
	n->p1a = g_playerEntity.directionAngle;
	n->p1hp = g_playerEntity.health;
	n->p1anim = g_playerEntity.animationId;
	n->p1beh = g_playerEntity.action_behavior;
	n->p1frame = g_playerEntity.animation_frame_id;
	n->p2hp = g_coopP2.health;
	if (g_coopPartner != nullptr) {
		n->p2x = g_coopPartner->scaMatrixData.localMatrix.t[0];
		n->p2z = g_coopPartner->scaMatrixData.localMatrix.t[2];
		n->p2a = g_coopPartner->angle;
	}
	unsigned int eh = 2166136261u;
	for (int i = 0; i < 30; i++) {
		const Entity* e = &g_EnemiesList[i];
		if ((e->status_flags & 1) == 0) {
			continue;
		}
		n->enemies++;
		int en[] = { i, e->id, (int)e->scaMatrixData.localMatrix.t[0], (int)e->scaMatrixData.localMatrix.t[2],
		             e->health, e->state };
		eh = fnv(eh, en, sizeof(en));
	}
	n->enemyHash = eh;
	n->hash = fnv(2166136261u, n, offsetof(NetSnapshot, hash));
}

void Netplay_ExchangeInputs(DWORD localWord, DWORD* p1Word, DWORD* p2Word)
{
	// Our word for the tick `delay` ahead.
	s_local[s_localNewest & (NETPLAY_RING - 1)] = localWord;
	s_localNewest++;

	// The state at the start of this tick (both machines are at the same point
	// here: every earlier tick ran on the same inputs).
	NetSnapshot* snap = &s_snap[s_tick & (NETPLAY_RING - 1)];
	take_snapshot(snap);
	s_snapNewest = s_tick + 1;
	compare_check(s_tick);
	if (s_tick % NETPLAY_LOG_TICKS == 0) {
		net_log("[NET] tick %u, hash %08x\n", s_tick, snap->hash);
	}

	send_packet(PKT_INPUT);

	// Wait for theirs.
	unsigned int slot = s_tick & (NETPLAY_RING - 1);
	DWORD waitStart = plat_time_ms();
	DWORD lastLog = waitStart;
	while (s_remoteTick[slot] != s_tick + 1) {
		receive_all();
		if (s_remoteTick[slot] == s_tick + 1) {
			break;
		}
		DWORD now = plat_time_ms();
		if (now - s_lastSend >= NETPLAY_RESEND_MS) {
			send_packet(PKT_INPUT);
		}
		if (now - lastLog >= NETPLAY_STALL_LOG_MS) {
			net_log("[NET] waiting for the other player (tick %u, %u ms)...\n", s_tick, now - waitStart);
			lastLog = now;
		}
		bool closed = !plat_pump_events();
		if (closed) {
			net_log("[NET] window closed while waiting at tick %u\n", s_tick);
		} else if (now - waitStart >= NETPLAY_TIMEOUT_MS) {
			net_log("[NET] the other player stopped answering at tick %u\n", s_tick);
		}
		if (closed || now - waitStart >= NETPLAY_TIMEOUT_MS || s_state != 2) {
			if (s_state == 2) {
				Netplay_Shutdown();
			}
			end_session();
			*p1Word = 0;
			*p2Word = 0;
			return;
		}
		plat_sleep_ms(1);
	}

	DWORD mine = s_local[slot];
	DWORD theirs = s_remote[slot];
	*p1Word = (g_netMode == NET_MODE_HOST) ? mine : theirs;
	*p2Word = (g_netMode == NET_MODE_HOST) ? theirs : mine;
	s_tick++;
}

void Netplay_Shutdown(void)
{
	if (s_state != 2) {
		return;
	}
	for (int i = 0; i < 3; i++) {
		send_packet(PKT_BYE);
	}
	s_state = -1;
}
