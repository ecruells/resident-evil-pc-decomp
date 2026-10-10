// Netplay.cpp - co-op over the network (port addition, not in the original).
// See Netplay.h.
#include "Netplay.h"
#include "Coop.h"
#include "../platform/platform.h"
#include "../system/AssetPath.h"
#include "BioCard.h"
extern int is_entity_in_switch_zone(VECTOR* position, void* zoneData);   // Room.cpp 0x00462d90
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstddef>

int  g_netMode = NET_MODE_OFF;
char g_netPeer[128] = "";
int  g_netPort = 27015;
int  g_netDelay = 3;
int  g_netLocalInput = NET_INPUT_BOTH;

#define NETPLAY_PROTOCOL     3
#define NETPLAY_MAGIC        0x4E314552u   // "RE1N"
#define NETPLAY_RING         256           // ticks of input kept (power of two)
#define NETPLAY_SEND_SPAN    16            // inputs per packet (redundancy against loss)
#define NETPLAY_LOG_TICKS    300           // a full state line in the log every 10 seconds
#define NETPLAY_RESEND_MS    10
#define NETPLAY_STALL_LOG_MS 3000
#define NETPLAY_TIMEOUT_MS   10000

enum { PKT_HELLO = 1, PKT_WELCOME = 2, PKT_INPUT = 3, PKT_BYE = 4, PKT_SKIP = 5,
       PKT_SAVE = 6, PKT_SAVE_DONE = 7 };

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
	unsigned int   skipSerial;     // the last movie this machine skipped (Netplay_FmvSkip)
};

// The host's save files, sent once at the start (PKT_SAVE). Same first eight
// bytes as NetPacket.
#define NETPLAY_SAVE_CHUNK   1024
#define NETPLAY_SAVE_SLOTS   8
#define NETPLAY_SAVE_MAX     (64 * NETPLAY_SAVE_CHUNK)
struct NetSavePacket {
	unsigned int   magic;
	unsigned char  type;
	unsigned char  pad0;
	unsigned short protocol;
	unsigned int   totalSize;      // the whole blob
	unsigned short chunk;          // this chunk
	unsigned short chunks;         // how many
	unsigned char  data[NETPLAY_SAVE_CHUNK];
};
#pragma pack(pop)

static unsigned int s_fmvSerial = 0;      // movies started this session (the same count on both)
static unsigned int s_localSkip = 0;      // the last movie skipped here
static unsigned int s_remoteSkip = 0;     // ... and there
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

// ---- Save sync (state 3, between connecting and the first tick) ----
// Both games must load the same saves, so the joiner plays on the host's: the
// host sends every savedat<N>.dat and the joiner keeps them in SAVE/netplay/
// (its own saves stay where they are) and uses that folder for the session.
// Saves made during the session then land on both machines, the same.
// Blob: for each slot 1..8, a u32 size (0 = no file) and the file.
static unsigned char  s_saveBlob[NETPLAY_SAVE_MAX];
static unsigned int   s_saveSize = 0;
static unsigned short s_saveChunks = 0;
static unsigned char  s_saveHave[64];
static unsigned int   s_saveHaveCount = 0;
static DWORD          s_saveLastSend = 0;

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
	pkt.skipSerial = s_localSkip;
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

static void send_save_packet(unsigned char type, unsigned short chunk)
{
	NetSavePacket pkt;
	memset(&pkt, 0, sizeof(pkt));
	pkt.magic = NETPLAY_MAGIC;
	pkt.type = type;
	pkt.protocol = NETPLAY_PROTOCOL;
	pkt.totalSize = s_saveSize;
	pkt.chunk = chunk;
	pkt.chunks = s_saveChunks;
	if (type == PKT_SAVE) {
		unsigned int off = (unsigned int)chunk * NETPLAY_SAVE_CHUNK;
		unsigned int len = (s_saveSize - off < NETPLAY_SAVE_CHUNK) ? s_saveSize - off : NETPLAY_SAVE_CHUNK;
		memcpy(pkt.data, s_saveBlob + off, len);
	}
	plat_net_send(&pkt, sizeof(pkt));
}

// Host: gather savedat1..8 from the save folder.
static void save_build_blob(void)
{
	char path[300];
	char list[64] = "";
	s_saveSize = 0;
	for (int slot = 1; slot <= NETPLAY_SAVE_SLOTS; slot++) {
		snprintf(path, sizeof(path), "%ssavedat%d.dat", GetSaveRoot(), slot);
		size_t size = 0;
		void* data = plat_file_read_all(path, &size);
		if (data != nullptr && s_saveSize + 4 + size > NETPLAY_SAVE_MAX) {
			net_log("[NET] save slot %d does not fit, not sent\n", slot);
			free(data);
			data = nullptr;
		}
		unsigned int sz = (data != nullptr) ? (unsigned int)size : 0;
		memcpy(s_saveBlob + s_saveSize, &sz, 4);
		s_saveSize += 4;
		if (data != nullptr) {
			memcpy(s_saveBlob + s_saveSize, data, sz);
			s_saveSize += sz;
			free(data);
			snprintf(list + strlen(list), sizeof(list) - strlen(list), " %d", slot);
		}
	}
	s_saveChunks = (unsigned short)((s_saveSize + NETPLAY_SAVE_CHUNK - 1) / NETPLAY_SAVE_CHUNK);
	net_log("[NET] sending save slots%s (%u bytes) to player 2\n", list[0] ? list : " - none", s_saveSize);
}

// Joiner: all chunks are in - write SAVE/netplay/ and play from it.
static void save_write_blob(void)
{
	char dir[300];
	char path[320];
	char list[64] = "";
	snprintf(dir, sizeof(dir), "%snetplay", GetSaveRoot());
	plat_mkdir(dir);
	unsigned int off = 0;
	for (int slot = 1; slot <= NETPLAY_SAVE_SLOTS && off + 4 <= s_saveSize; slot++) {
		unsigned int sz;
		memcpy(&sz, s_saveBlob + off, 4);
		off += 4;
		snprintf(path, sizeof(path), "%s/savedat%d.dat", dir, slot);
		if (sz == 0 || off + sz > s_saveSize) {
			remove(path);
			continue;
		}
		plat_file_write(path, s_saveBlob + off, sz);
		off += sz;
		snprintf(list + strlen(list), sizeof(list) - strlen(list), " %d", slot);
	}
	SetSaveRoot(dir);
	net_log("[NET] got the host's save slots%s, playing from %s\n", list[0] ? list : " - none", GetSaveRoot());
}

static void save_receive(const NetSavePacket* pkt, unsigned char type)
{
	if (type == PKT_SAVE_DONE) {
		if (g_netMode == NET_MODE_HOST && s_state == 3) {
			s_state = 2;
			net_log("[NET] player 2 has the saves\n");
		}
		return;
	}
	if (g_netMode != NET_MODE_JOIN) {
		return;
	}
	if (s_state == 2) {
		send_save_packet(PKT_SAVE_DONE, 0);   // the host missed our last one
		return;
	}
	if (s_state != 3 || pkt->chunks > 64 || pkt->chunk >= pkt->chunks || pkt->totalSize > NETPLAY_SAVE_MAX) {
		return;
	}
	if (s_saveChunks != pkt->chunks || s_saveSize != pkt->totalSize) {
		s_saveChunks = pkt->chunks;
		s_saveSize = pkt->totalSize;
		memset(s_saveHave, 0, sizeof(s_saveHave));
		s_saveHaveCount = 0;
	}
	if (!s_saveHave[pkt->chunk]) {
		unsigned int off = (unsigned int)pkt->chunk * NETPLAY_SAVE_CHUNK;
		unsigned int len = (s_saveSize - off < NETPLAY_SAVE_CHUNK) ? s_saveSize - off : NETPLAY_SAVE_CHUNK;
		memcpy(s_saveBlob + off, pkt->data, len);
		s_saveHave[pkt->chunk] = 1;
		s_saveHaveCount++;
	}
	if (s_saveHaveCount == s_saveChunks) {
		save_write_blob();
		for (int i = 0; i < 3; i++) {
			send_save_packet(PKT_SAVE_DONE, 0);
		}
		s_state = 2;
	}
}

static void receive_all(void)
{
	union {
		NetPacket     pkt;
		NetSavePacket save;
	} buf;
	NetPacket& pkt = buf.pkt;
	int n;
	while ((n = plat_net_recv(&buf, sizeof(buf))) >= 0) {
		if (n >= 8 && pkt.magic == NETPLAY_MAGIC && pkt.protocol == NETPLAY_PROTOCOL &&
		    (pkt.type == PKT_SAVE || pkt.type == PKT_SAVE_DONE)) {
			if (n == (int)sizeof(NetSavePacket) || pkt.type == PKT_SAVE_DONE) {
				save_receive(&buf.save, pkt.type);
			}
			continue;
		}
		if (n != (int)sizeof(pkt) || pkt.magic != NETPLAY_MAGIC || pkt.protocol != NETPLAY_PROTOCOL) {
			continue;
		}
		if ((pkt.type == PKT_INPUT || pkt.type == PKT_SKIP) && pkt.skipSerial > s_remoteSkip) {
			s_remoteSkip = pkt.skipSerial;
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
				s_state = 3;
				net_log("[NET] connected (%s), input delay %d ticks\n",
				        g_netMode == NET_MODE_HOST ? "host, player 1" : "joined, player 2", g_netDelay);
				if (g_netMode == NET_MODE_HOST) {
					save_build_blob();
				}
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
	if (s_state == 3) {
		// Host: send every chunk every 30 ms until player 2 has them all.
		if (g_netMode == NET_MODE_HOST && plat_time_ms() - s_saveLastSend >= 30) {
			for (unsigned short c = 0; c < s_saveChunks; c++) {
				send_save_packet(PKT_SAVE, c);
			}
			if (s_saveChunks == 0) {
				send_save_packet(PKT_SAVE, 0);   // no saves: one empty chunk says so
			}
			s_saveLastSend = plat_time_ms();
		}
		if (g_netMode == NET_MODE_JOIN && plat_time_ms() - s_lastHello > 250) {
			send_packet(PKT_HELLO);   // the host may not have our first one
			s_lastHello = plat_time_ms();
		}
		receive_all();
		return s_state == 2 ? 1 : (s_state == -1 ? -1 : 0);
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

// Bit 31 of a pad word (no button uses it): this machine's voice line was still
// playing when the word was made.
#define NET_WORD_VOICE_BUSY 0x80000000u
static bool         s_voiceBusy = false;       // either machine's, for the last exchanged tick
static unsigned int s_voiceArmTick = 0;        // first tick whose words were made after the line began
static bool         s_voiceFlagSeen = false;

void Netplay_VoiceStarted(void)
{
	// Sim tick s_tick - 1 is running; the word for tick X was made at the start
	// of tick X - delay, so words from tick s_tick + delay on know of this line.
	s_voiceArmTick = s_tick + (unsigned int)g_netDelay;
}

int Netplay_VoiceWaitDone(int flagSet)
{
	if (!flagSet) {
		s_voiceFlagSeen = false;
		return 1;
	}
	if (!s_voiceFlagSeen) {
		s_voiceFlagSeen = true;
		if ((int)(s_voiceArmTick - s_tick) < 0) {
			Netplay_VoiceStarted();   // raised without a line (scripted waits)
		}
	}
	if ((int)(s_tick - s_voiceArmTick) > 0 && !s_voiceBusy) {
		net_log("[NET] voice wait ended at tick %u\n", s_tick - 1);
		s_voiceFlagSeen = false;
		return 1;
	}
	return 0;
}

void Netplay_ExchangeInputs(DWORD localWord, DWORD* p1Word, DWORD* p2Word)
{
	extern int Sound_VoiceBusy(void);
	localWord &= ~NET_WORD_VOICE_BUSY;
	if (Sound_VoiceBusy()) {
		localWord |= NET_WORD_VOICE_BUSY;
	}

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

	DWORD waited = plat_time_ms() - waitStart;
	if (waited > 60) {
		net_log("[NET] tick %u waited %u ms for the other player\n", s_tick, waited);
	}
	static DWORD s_lastTickTime = 0;
	DWORD nowT = plat_time_ms();
	if (s_lastTickTime != 0 && nowT - s_lastTickTime > 100 && waited <= 60) {
		net_log("[NET] tick %u came %u ms after the last (this machine was busy)\n", s_tick,
		        nowT - s_lastTickTime);
	}
	s_lastTickTime = nowT;

	DWORD mine = s_local[slot];
	DWORD theirs = s_remote[slot];
	s_voiceBusy = ((mine | theirs) & NET_WORD_VOICE_BUSY) != 0;
	mine &= ~NET_WORD_VOICE_BUSY;
	theirs &= ~NET_WORD_VOICE_BUSY;
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

// ============================================================================
// Player 2's camera (joiner only)
// ============================================================================
static int  s_viewCam = -1;           // her camera id; -1 = not tracked yet
static int  s_viewAssetsCam = -1;     // the camera whose background/masks are loaded for the view; -1 = the shared game's
static int  s_viewSerial = -1;        // the partner spawn the tracking belongs to
static int  s_viewSimCam = -1;
static void* s_viewSimZones = nullptr;
static unsigned char s_viewSimInZone[31];   // byte 3 of each entity (30 + the player) under his camera
static bool s_viewSwapped = false;
static int  s_viewFrameFov = -1;      // her camera's fov for this frame's 3D pass; -1 = the shared game's

static CAM_SWITCH_ZONE* view_group(int cam)
{
	CAM_SWITCH_ZONE* zone = (CAM_SWITCH_ZONE*)g_RdtPointer->cam_switch_zones;
	for (int guard = 0; guard < 256 && (unsigned short)zone->camFrom != (unsigned short)cam; guard++) {
		zone++;
	}
	return ((unsigned short)zone->camFrom == (unsigned short)cam) ? zone : nullptr;
}

static bool s_viewResync = false;   // a cutscene held her camera: check it still fits her

// Can a switch zone lead to this camera? Cutscene-only shots usually have no
// way in, and are no place to leave her when the scene is over.
static bool view_reachable(int cam)
{
	for (int from = 0; from < g_RdtPointer->cameras_count; from++) {
		CAM_SWITCH_ZONE* group = view_group(from);
		if (group == nullptr) {
			continue;
		}
		for (CAM_SWITCH_ZONE* zone = group + 1; (unsigned short)zone->camFrom == (unsigned short)from; zone++) {
			if ((unsigned short)zone->camTo == (unsigned short)cam) {
				return true;
			}
		}
	}
	return false;
}

// The camera to show her at `pos`: `prefer` when its area (a group's header -
// what render_entity culls against) holds her, else a camera a switch zone
// leads to whose area does, else any camera whose area does.
static int view_pick(VECTOR* pos, int prefer)
{
	CAM_SWITCH_ZONE* header = view_group(prefer);
	if (header != nullptr && is_entity_in_switch_zone(pos, header) != 0) {
		return prefer;
	}
	int any = -1;
	for (int cam = 0; cam < g_RdtPointer->cameras_count; cam++) {
		header = view_group(cam);
		if (header == nullptr || is_entity_in_switch_zone(pos, header) == 0) {
			continue;
		}
		if (view_reachable(cam)) {
			return cam;
		}
		if (any < 0) {
			any = cam;
		}
	}
	return (any >= 0) ? any : prefer;
}

// check_camera_switch for her: one zone crossing per frame, as his.
static void view_track(void)
{
	VECTOR* pos = (VECTOR*)g_coopPartner->scaMatrixData.localMatrix.t;
	if (s_viewCam < 0 || view_group(s_viewCam) == nullptr) {
		s_viewCam = view_pick(pos, g_roomCameraId);   // a new room: his camera if it shows her
		s_viewResync = false;
		return;
	}
	if (s_viewResync) {
		// After a cutscene: her camera from before it, if it still shows her
		// (the scene may have moved her).
		s_viewResync = false;
		int cam = view_pick(pos, s_viewCam);
		if (cam != s_viewCam) {
			net_log("[NET] view: after the cutscene camera %d, not %d\n", cam, s_viewCam);
			s_viewCam = cam;
		}
		return;
	}
	CAM_SWITCH_ZONE* group = view_group(s_viewCam);
	for (CAM_SWITCH_ZONE* zone = group + 1; (unsigned short)zone->camFrom == (unsigned short)s_viewCam; zone++) {
		if (is_entity_in_switch_zone(pos, zone) != 0) {
			s_viewCam = (unsigned short)zone->camTo;
			return;
		}
	}
	// The player never leaves his camera's area without crossing a switch
	// zone; she can (placed beside him on entry, a zone missed between frames),
	// and would then be invisible on her own screen.
	if (is_entity_in_switch_zone(pos, group) == 0) {
		int cam = view_pick(pos, s_viewCam);
		if (cam != s_viewCam) {
			net_log("[NET] view: she is outside camera %d's area, now camera %d\n", s_viewCam, cam);
			s_viewCam = cam;
		}
	}
}

// Put camera `cam`'s background and masks on screen, as cut_set does, without
// its sprite-flag pass (that queue belongs to the shared game) and leaving the
// texture counters as they were.
static void view_load_assets(int cam)
{
	DWORD start = plat_time_ms();
	TextureBankCell cell = g_TextureBankCell;
	unsigned char simCam = g_roomCameraId;
	g_roomCameraId = (unsigned char)cam;
	*(unsigned short*)&g_TextureBankCell = g_SavedTextureBankID;
	Room_LoadCameraSprites();
	load_room_bg_image();
	g_roomCameraId = simCam;
	g_TextureBankCell = cell;
	net_log("[NET] view: camera %d's background/masks at tick %u (%u ms, his camera %d)\n",
	        cam, s_tick, plat_time_ms() - start, simCam);
}

void Netplay_OnSimCutSet(void)
{
	s_viewAssetsCam = -1;   // the shared game just put its own camera's on screen
}

void Netplay_ViewBegin(void)
{
	if (!Netplay_IsActive() || g_netMode != NET_MODE_JOIN || g_RdtPointer == nullptr) {
		s_viewFrameFov = -1;
		return;
	}
	extern int Coop_PartnerSpawnSerial(void);
	if (s_viewSerial != Coop_PartnerSpawnSerial()) {
		s_viewSerial = Coop_PartnerSpawnSerial();
		s_viewCam = -1;   // a new room
	}

	// Her camera when she is up and about; his during cutscenes (the camera is
	// locked to the scene) or when she is not in the room.
	int want = g_roomCameraId;
	if (g_coopPartner == nullptr || !g_coopPartnerPlaced || g_coopP2Down) {
		s_viewCam = -1;
	} else if ((g_main_state_flags & MSF_CAMERA_LOCK) != 0) {
		s_viewResync = true;   // keep hers for after the scene
	} else {
		view_track();
		want = s_viewCam;
	}

	if (want == g_roomCameraId) {
		s_viewFrameFov = -1;
		if (s_viewAssetsCam != -1) {
			view_load_assets(g_roomCameraId);   // back to his background/masks
			s_viewAssetsCam = -1;
		}
		return;
	}
	if (s_viewAssetsCam != want) {
		view_load_assets(want);
		s_viewAssetsCam = want;
	}
	s_viewSimCam = g_roomCameraId;
	g_roomCameraId = (unsigned char)want;
	Room_SetupCamera();
	// Entities and room objects are culled against the current camera's zone
	// group (render_entity, RoomObjectRender): hers while drawing.
	s_viewSimZones = g_CurrentRdtDataTypePtr;
	if (CAM_SWITCH_ZONE* group = view_group(want)) {
		g_CurrentRdtDataTypePtr = group;
	}
	// Byte 3 of every character is "inside the current camera's area", set
	// each tick against his camera (character_npc_update, the enemy updates,
	// the player's zoneFlags); render_entity skips a character whose low seven
	// bits are clear. Redo it against hers for the drawing, put his back after.
	for (int i = 0; i < 31; i++) {
		unsigned char* e = (i < 30) ? (unsigned char*)&g_EnemiesList[i] : (unsigned char*)&g_playerEntity;
		s_viewSimInZone[i] = e[3];
		if ((e[0] & 1) == 0) continue;
		if (is_entity_in_switch_zone((VECTOR*)((Entity*)e)->scaMatrixData.localMatrix.t, g_CurrentRdtDataTypePtr) != 0) {
			if ((e[3] & 0x7f) == 0) e[3] |= 1;
		} else {
			e[3] &= 0x80;
		}
	}
	s_viewSwapped = true;
}

void Netplay_ViewEnd(void)
{
	if (!s_viewSwapped) {
		return;
	}
	for (int i = 0; i < 31; i++) {
		unsigned char* e = (i < 30) ? (unsigned char*)&g_EnemiesList[i] : (unsigned char*)&g_playerEntity;
		e[3] = s_viewSimInZone[i];
	}
	s_viewSwapped = false;
	// The 3D queue is projected when the frame is presented, after this: keep
	// her fov for it (Netplay_FrameFov) while the shared game gets his back.
	s_viewFrameFov = g_sceneRenderParam;
	g_roomCameraId = (unsigned char)s_viewSimCam;
	g_CurrentRdtDataTypePtr = s_viewSimZones;
	Room_SetupCamera();
}

int Netplay_FrameFov(void)
{
	return (s_viewFrameFov >= 0) ? s_viewFrameFov : g_sceneRenderParam;
}

void Netplay_FmvStarted(void)
{
	s_fmvSerial++;
}

int Netplay_FmvSkip(int localPressed)
{
	if (!Netplay_IsActive()) {
		return localPressed;
	}
	if (localPressed && s_localSkip < s_fmvSerial) {
		s_localSkip = s_fmvSerial;
		net_log("[NET] movie %u skipped here\n", s_fmvSerial);
		for (int i = 0; i < 3; i++) {
			send_packet(PKT_SKIP);
		}
	}
	receive_all();
	if (s_remoteSkip == s_fmvSerial && !localPressed) {
		net_log("[NET] movie %u skipped by the other player\n", s_fmvSerial);
		s_localSkip = s_fmvSerial;
		return 1;
	}
	return localPressed;
}

DWORD Netplay_LocalPadWord(void)
{
	DWORD word = 0;
	if (g_pMasterInputState.frameFlag != 0) {
		word |= JoyToPSX(g_pMasterInputState.keyboardPrev, 0);
	}
	if ((1 < g_NumControllers) && (g_pMasterInputState.joysticks[0].enabled != 0)) {
		word |= JoyToPSX(g_pMasterInputState.joysticks[0].currPress, 1);
	}
	return word;
}
