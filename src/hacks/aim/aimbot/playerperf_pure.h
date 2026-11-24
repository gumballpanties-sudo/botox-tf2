#pragma once
// ==== the "playerperf" request no spread sends ===========================================
// Engine-free so tests/playerperf_test.cpp can check it.
//
// WHY. A bare "playerperf" makes the server dump EVERY record of the player's perf history
// (sdk player.cpp:6730 nRecords = -1 = all; sv_playerperfhistorycount default 60), ~3 KB as
// ~15 reliable TextMsg chunks (DumpPerfToRecipient, 200-char buffers). No spread asks every
// 0.1 s (NoSpreadInterval) once the last reply parsed, so that was ~30 KB/s of reliable
// INCOMING data on top of the snapshots -- 11-10 NET log: incoming B/s 13k -> 40k+, incoming
// choke 8-19 %, incoming loss up to 17 %, all gone with the aim half off.
//
// The parse only ever needs the NEWEST record: the hook reads one line per TextMsg
// (ReadString bLine) and the dump walks newest -> oldest, so line one of chunk one is the server
// time it syncs to; every older line was parsed and thrown away. "playerperf <idx> <n>" caps the
// dump at n records (player.cpp:6726-6733), so ask for exactly one, about the size of one line.

#include <cstdio>
#include <string>

inline std::string PlayerPerfCmd(int localIndex)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "playerperf %d 1", localIndex > 0 ? localIndex : 1);
    return buf;
}
