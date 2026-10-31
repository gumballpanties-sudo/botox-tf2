#pragma once
#include "sdk.h"
#include "aim_record.h"

// Lag-comp backtrack (gap.md Unit F, "MISSING" -- infra already compiled/whitelisted, just
// needed a record store; see the "Re-add it WITH a record store" note in aim_record.h and
// aim_hitscan.cpp's CanHit). Read side only: the write side is one line in the existing
// CBaseEntity_SetAbsVelocity hook body (hooks.cpp) that already timestamps every lag-comp-
// eligible player tick for the velocity fix next to it. This keeps its OWN short history
// instead of reusing CEntities::m_mOrigins, which is sized for velocity averaging
// (Projectile::VelocityAverageCount, default 5), not for a latency window.
class CBacktrack
{
private:
	// TickRecord, not VelFixRecord: a backtrack record IS a bone pose, and it has to be
	// captured HERE, at store time, off the entity's pose as it stands RIGHT NOW. SetupBones
	// never rebuilds a historical pose from a time argument alone -- it poses the entity's
	// CURRENT origin/angles/pose-params/anim-layers and only uses the time for animation
	// blending. Asking for it later (old code did, in GetRecords) silently returns the
	// CURRENT pose stamped with an old time -- bones in the right shape, standing in the
	// wrong place. Amalgam's CBacktrack snapshots bones in its own Store(); same fix here.
	std::unordered_map<int, std::deque<TickRecord>> m_mHistory = {};

	// Extended backtrack = Amalgam's fake latency (Features/Backtrack AdjustPing): SendDatagram
	// acks an OLDER incoming sequence, so the server reads our ping as higher and its lag-comp
	// `correct` (player_lagcompensation.cpp) grows up to sv_maxunlag. Records then stay valid
	// out to correct + 0.2s, i.e. ~1.2s at the default sv_maxunlag 1.0.
	struct Sequence { int m_nInReliableState, m_nSequenceNr; float m_flTime; };
	std::deque<Sequence> m_dSequences = {};
	int m_iLastInSequence = 0, m_nLastInSequenceNr = 0;
	int m_nOldInSequenceNr = 0, m_nOldInReliableState = 0;
	float m_flFakeLatency = 0.f;

public:
	// CNetChannel_SendDatagram hook (hooks.cpp): rewind before the send, restore right after
	void AdjustPing(void* pNetChan);
	void RestorePing(void* pNetChan);
	float GetFakeLatency() const { return m_flFakeLatency; }

	// Called once per valid network update for pPlayer (hooks.cpp, CBaseEntity_SetAbsVelocity).
	void Store(CTFPlayer* pPlayer);

	// Fills pOut (caller-owned, capacity iMax) with historical bone poses for pTarget inside
	// the backtrack window, newest first. Returns the count filled -- 0 when off, no history,
	// or everything on record is already outside the window.
	int GetRecords(CTFPlayer* pTarget, TickRecord* pOut, int iMax);
};

ADD_FEATURE(CBacktrack, Backtrack);
