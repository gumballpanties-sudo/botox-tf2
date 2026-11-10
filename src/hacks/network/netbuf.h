#pragma once
// ==== receive-buffer bump =================================================================
// Main-thread stalls (any cause: ours, the engine's model loads, shader compiles) turned into
// net_graph loss because the UDP socket's 64 KB receive buffer overflowed while nobody read it.
// Grow it to 4 MB on the first recvfrom of every UDP socket, and raise steamclient's global
// receive-queue limits for the SDR path Valve servers use. Mechanism + numbers: netbuf_pure.h.
//
// Install: MainThread, after MH_Initialize and before MH_EnableHook(MH_ALL_HOOKS). Nothing to
// undo at eject -- a bigger buffer is harmless, MH_DisableHook(MH_ALL_HOOKS) drops the detour.
void NetBuf_Install();
