// winsock2 FIRST: globals.h pulls <windows.h>, which drags the old winsock.h in unless
// winsock2.h has already claimed _WINSOCKAPI_. Reversed = redefinition errors.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "netbuf.h"
#include "netbuf_pure.h"
#include "../../globals/globals.h"
#include "../../hooks/crashlog.h"
#include "../../dependencies/minhook/MinHook.h"

// ---- ws2_32!recvfrom --------------------------------------------------------------------
// Hooked in ws2_32 itself, not an IAT: the engine reaches recvfrom through tier0's
// VCRHook_recvfrom, steamclient through its own import, and one detour covers both. Every
// UDP socket that is ever read gets the bump exactly once; TCP sockets are left alone.

using recvfrom_t = int (WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
static recvfrom_t o_recvfrom = nullptr;

static netbuf::Seen     s_seen;
static CRITICAL_SECTION s_cs;
static bool             s_csInit = false;

static void BumpOnce(SOCKET sock)
{
    // Miss path. Serialised: two threads can see the same new socket on the same tick.
    EnterCriticalSection(&s_cs);
    const bool fresh = s_seen.Add((uintptr_t)sock);
    LeaveCriticalSection(&s_cs);
    if (!fresh) return;

    int type = 0, len = sizeof(type);
    if (getsockopt(sock, SOL_SOCKET, SO_TYPE, (char*)&type, &len) != 0 || type != SOCK_DGRAM) return;

    int cur = -1; len = sizeof(cur);
    if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&cur, &len) != 0) cur = -1;

    const int want = netbuf::WantRcvBuf(cur);
    if (!want) { DebugLog("netbuf: socket %llu SO_RCVBUF %d, left alone", (unsigned long long)sock, cur); return; }

    const int rc = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&want, sizeof(want));
    int now = -1; len = sizeof(now);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&now, &len);
    DebugLog("netbuf: socket %llu SO_RCVBUF %d -> %d (set rc %d, err %d)",
             (unsigned long long)sock, cur, now, rc, rc ? WSAGetLastError() : 0);
    CrashLog_Note("netbuf: udp socket receive buffer %d -> %d", cur, now);
}

static int WSAAPI h_recvfrom(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen)
{
    MiscHookGuard _g;
    // Hit path first and lock-free: s_seen only ever grows, and a stale read here costs one
    // extra trip through BumpOnce, which re-checks under the lock.
    if (!g_eject && !s_seen.Contains((uintptr_t)s)) BumpOnce(s);
    return o_recvfrom(s, buf, len, flags, from, fromlen);
}

// ---- steamclient receive queue (SDR path) --------------------------------------------------
static void BumpSteamNetworking()
{
    HMODULE h = GetModuleHandleA("steam_api64.dll");
    if (!h) { DebugLog("netbuf: steam_api64.dll not loaded, SDR limits untouched"); return; }
    using SNU_Get_t    = void* (*)();
    using SNU_SetInt_t = bool (*)(void*, int, int);
    auto getU = (SNU_Get_t)   GetProcAddress(h, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    auto setI = (SNU_SetInt_t)GetProcAddress(h, "SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32");
    void* utils = getU ? getU() : nullptr;
    if (!utils || !setI) { DebugLog("netbuf: ISteamNetworkingUtils unavailable (%p %p)", utils, setI); return; }
    const bool a = setI(utils, netbuf::kSnsRecvBufferSize,     netbuf::kWantSnsBytes);
    const bool b = setI(utils, netbuf::kSnsRecvBufferMessages, netbuf::kWantSnsMessages);
    DebugLog("netbuf: SDR recv limits -> %d bytes (%d), %d msgs (%d); applies to the next connect",
             netbuf::kWantSnsBytes, (int)a, netbuf::kWantSnsMessages, (int)b);
}

void NetBuf_Install()
{
    if (!s_csInit) { InitializeCriticalSection(&s_cs); s_csInit = true; }

    HMODULE hWs2 = GetModuleHandleA("ws2_32.dll");
    void* p = hWs2 ? (void*)GetProcAddress(hWs2, "recvfrom") : nullptr;
    if (!p) { DebugLog("netbuf: ws2_32!recvfrom not found, udp buffer untouched"); }
    else {
        const MH_STATUS st = MH_CreateHook(p, (void*)h_recvfrom, (void**)&o_recvfrom);
        DebugLog("netbuf: recvfrom hook %s (%p)", st == MH_OK ? "created" : "FAILED", p);
    }
    BumpSteamNetworking();
}
