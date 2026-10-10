// net.cpp - co-op netplay networking (port addition). Netplay is Linux-only
// for now: these keep the Windows build linking, and plat_net_open's FALSE
// keeps netplay off.
#include "../platform.h"
#include <windows.h>

BOOL plat_net_open(unsigned short port) { (void)port; return FALSE; }
BOOL plat_net_set_peer(const char* host, unsigned short port) { (void)host; (void)port; return FALSE; }
BOOL plat_net_send(const void* data, int size) { (void)data; (void)size; return FALSE; }
int  plat_net_recv(void* buffer, int capacity) { (void)buffer; (void)capacity; return -1; }
void plat_net_close(void) {}
void plat_sleep_ms(DWORD ms) { Sleep(ms); }
BOOL plat_pump_events(void) { return TRUE; }
