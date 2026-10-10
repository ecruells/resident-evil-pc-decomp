// net.cpp - UDP for co-op netplay (port addition): the plat_net_* calls of
// platform.h on BSD sockets.
#include "../platform.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int                s_sock = -1;
static struct sockaddr_in s_peer;
static BOOL               s_hasPeer = FALSE;

BOOL plat_net_open(unsigned short port)
{
    plat_net_close();
    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock < 0) {
        return FALSE;
    }
    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(s_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(s_sock);
        s_sock = -1;
        return FALSE;
    }
    fcntl(s_sock, F_SETFL, fcntl(s_sock, F_GETFL, 0) | O_NONBLOCK);
    s_hasPeer = FALSE;
    return TRUE;
}

BOOL plat_net_set_peer(const char* host, unsigned short port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = NULL;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL) {
        return FALSE;
    }
    memcpy(&s_peer, res->ai_addr, sizeof(s_peer));
    s_peer.sin_port = htons(port);
    freeaddrinfo(res);
    s_hasPeer = TRUE;
    return TRUE;
}

BOOL plat_net_send(const void* data, int size)
{
    if (s_sock < 0 || !s_hasPeer) {
        return FALSE;
    }
    return sendto(s_sock, data, (size_t)size, 0, (struct sockaddr*)&s_peer, sizeof(s_peer)) == size;
}

int plat_net_recv(void* buffer, int capacity)
{
    if (s_sock < 0) {
        return -1;
    }
    for (;;) {
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        ssize_t n = recvfrom(s_sock, buffer, (size_t)capacity, 0, (struct sockaddr*)&from, &len);
        if (n < 0) {
            return -1;
        }
        if (!s_hasPeer) {
            s_peer = from;
            s_hasPeer = TRUE;
        } else if (from.sin_addr.s_addr != s_peer.sin_addr.s_addr || from.sin_port != s_peer.sin_port) {
            continue;   // not our peer
        }
        return (int)n;
    }
}

void plat_net_close(void)
{
    if (s_sock >= 0) {
        close(s_sock);
    }
    s_sock = -1;
    s_hasPeer = FALSE;
}

void plat_sleep_ms(DWORD ms)
{
    usleep((useconds_t)ms * 1000);
}
