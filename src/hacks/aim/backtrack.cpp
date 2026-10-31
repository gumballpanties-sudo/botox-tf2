#include "backtrack.h"

void CBacktrack::Store(CTFPlayer* pPlayer)
{
	TickRecord tRecord = {};
	tRecord.m_flSimTime = pPlayer->m_flSimulationTime();
	tRecord.m_vOrigin = pPlayer->m_vecOrigin();    // true origin, not the +height one the
	                                                // velocity-fix record next to this call uses
	tRecord.m_vMins = tRecord.m_vMaxs = Vec3();
	tRecord.m_bOnShot = tRecord.m_bInvalid = false;

	auto pModel = pPlayer->GetModel();
	if (!pModel || !I::ModelInfoClient->GetStudiomodel(pModel))
		return;    // no model this tick -- nothing valid to snapshot
	// Captured NOW, at the entity's current pose -- see the note on m_mHistory in backtrack.h.
	// ANYTHING, not HITBOX: backtrack chams (aim/chams.cpp) RENDERS these bones, and a hitbox-only
	// setup leaves every other bone zeroed (mesh spikes to the world origin). Superset for aim.
	if (!pPlayer->SetupBones(tRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, tRecord.m_flSimTime))
		return;

	auto& vHistory = m_mHistory[pPlayer->entindex()];
	vHistory.emplace_front(std::move(tRecord));

	// Trimmed by age, not count: the window is a time budget (MaxLatency, clamped to
	// sv_maxunlag), so the deque should hold exactly as many ticks as that takes, no more.
	// +0.2: the server accepts a record up to 0.2s either side of its correct (extended mode)
	static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
	const float flWindow = std::min(Vars::Aimbot::Backtrack::MaxLatency.Value / 1000.f, (sv_maxunlag ? sv_maxunlag->GetFloat() : 0.2f) + 0.2f) + TICK_INTERVAL;
	const float flNewest = vHistory.front().m_flSimTime;
	while (vHistory.size() > 1 && flNewest - vHistory.back().m_flSimTime > flWindow)
		vHistory.pop_back();
}

// CNetChannel fields (Amalgam INetChannel.h CNetChannel): vptr, m_ConnectionState,
// m_nOutSequenceNr, m_nInSequenceNr @0x10, m_nOutSequenceNrAck, m_nOutReliableState,
// m_nInReliableState @0x1C. All ints after one vptr.
static int& NcInSeq(void* p) { return *reinterpret_cast<int*>(reinterpret_cast<byte*>(p) + 0x10); }
static int& NcInRel(void* p) { return *reinterpret_cast<int*>(reinterpret_cast<byte*>(p) + 0x1C); }

// Amalgam CBacktrack::UpdateDatagram + AdjustPing, merged: the sequence history is recorded on
// every send instead of on every Store (one tick of time error at most, no extra call site).
void CBacktrack::AdjustPing(void* pNetChan)
{
	m_nOldInSequenceNr = NcInSeq(pNetChan), m_nOldInReliableState = NcInRel(pNetChan);

	if (m_nOldInSequenceNr > m_iLastInSequence)
	{
		m_iLastInSequence = m_nOldInSequenceNr;
		m_dSequences.push_front({ m_nOldInReliableState, m_nOldInSequenceNr, I::GlobalVars->realtime });
	}
	else if (m_nOldInSequenceNr < m_iLastInSequence - 1000)   // new connection: sequences restart
	{
		m_dSequences.clear();
		m_iLastInSequence = m_nLastInSequenceNr = 0;
	}
	while (m_dSequences.size() > 90)   // > 1.2s of 66 tick
		m_dSequences.pop_back();

	auto Set = [&]() -> float
	{
		if (!Vars::Aimbot::Backtrack::Enabled.Value || !Vars::Aimbot::Backtrack::Extended.Value)
			return 0.f;
		auto pLocal = H::Entities.GetLocal();
		if (!pLocal || !pLocal->IsAlive())
			return 0.f;

		static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
		static auto host_timescale = H::ConVars.FindVar("host_timescale");
		const float flMaxUnlag = sv_maxunlag ? sv_maxunlag->GetFloat() : 1.f;
		const float flTimescale = host_timescale ? host_timescale->GetFloat() : 1.f;
		// wish = the part of the slider past the 0.2s the server gives for free
		const float flFake = std::clamp(Vars::Aimbot::Backtrack::MaxLatency.Value / 1000.f - 0.2f, 0.f, flMaxUnlag);
		const float flReal = 5 * TICK_INTERVAL;   // ponytail: Amalgam's smoothed choke margin, as a constant

		int nInReliableState = NcInRel(pNetChan), nInSequenceNr = NcInSeq(pNetChan); float flLatency = 0.f;
		for (auto& tSeq : m_dSequences)
		{
			nInReliableState = tSeq.m_nInReliableState;
			nInSequenceNr = tSeq.m_nSequenceNr;
			flLatency = (I::GlobalVars->realtime - tSeq.m_flTime) * flTimescale - TICK_INTERVAL;
			if (flLatency > flFake || m_nLastInSequenceNr >= tSeq.m_nSequenceNr || flLatency > flMaxUnlag - flReal)
				break;
		}
		if (flLatency > 1.f)   // Amalgam's failsafe
			return 0.f;

		NcInRel(pNetChan) = nInReliableState;
		NcInSeq(pNetChan) = nInSequenceNr;
		return flLatency;
	};

	const float flLatency = Set();
	m_nLastInSequenceNr = NcInSeq(pNetChan);

	// ramp at most one tick per send, so the server sees ping climb instead of jump
	m_flFakeLatency = std::clamp(m_flFakeLatency + (flLatency - m_flFakeLatency) * 0.1f, m_flFakeLatency - TICK_INTERVAL, m_flFakeLatency + TICK_INTERVAL);
	if (!flLatency && m_flFakeLatency < TICK_INTERVAL)
		m_flFakeLatency = 0.f;
}

void CBacktrack::RestorePing(void* pNetChan)
{
	NcInSeq(pNetChan) = m_nOldInSequenceNr, NcInRel(pNetChan) = m_nOldInReliableState;
}

int CBacktrack::GetRecords(CTFPlayer* pTarget, TickRecord* pOut, int iMax)
{
	if (!Vars::Aimbot::Backtrack::Enabled.Value || iMax <= 0 || !pTarget)
		return 0;

	auto it = m_mHistory.find(pTarget->entindex());
	if (it == m_mHistory.end() || it->second.empty())
		return 0;

	auto pModel = pTarget->GetModel();
	if (!pModel || !I::ModelInfoClient->GetStudiomodel(pModel))
		return 0;

	// BacktrackCorrect(), not a flat sv_maxunlag clamp: a record outside the server's own
	// correct (player_lagcompensation.cpp:379-398, FLOW_OUTGOING latency + lerp) gets its
	// requested tick_count overridden by the server's 0.2s sanity fallback (lagcompensation.cpp
	// :409-412) anyway, so offering it here just aims at a position the server will never use.
	float flMinAge = 0.f;
	float flWindow = std::min(Vars::Aimbot::Backtrack::MaxLatency.Value / 1000.f, BacktrackCorrect());

	// Extended: the server's correct = its RTT to us (real + fake) + lerp, clamped to sv_maxunlag,
	// and it keeps a tick_count whose delta is within 0.2s of that. Our record age + real RTT is
	// that delta, so the valid ages are [correct - rtt - 0.2, correct - rtt + 0.2]. One tick of
	// margin each side. The newest records fall OUT of the window here -- that is the trade.
	if (Vars::Aimbot::Backtrack::Extended.Value && m_flFakeLatency > 0.f)
	{
		static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
		const float flRtt = AimLatency(FLOW_OUTGOING);
		const float flCorrect = std::clamp(flRtt + m_flFakeLatency + BacktrackLerp(), 0.f, sv_maxunlag ? sv_maxunlag->GetFloat() : 1.f);
		flMinAge = flCorrect - flRtt - 0.2f + TICK_INTERVAL;
		flWindow = std::min(Vars::Aimbot::Backtrack::MaxLatency.Value / 1000.f, flCorrect - flRtt + 0.2f - TICK_INTERVAL);
	}
	const float flNow = pTarget->m_flSimulationTime();

	int iCount = 0;
	for (auto& tRecord : it->second)
	{
		if (iCount >= iMax)
			break;

		const float flAge = flNow - tRecord.m_flSimTime;
		if (flAge <= 0.f || flAge < flMinAge)
			continue;    // not actually older than the live record / too new for the window
		if (flAge > flWindow)
			break;       // deque is newest-first; nothing past here is in window either

		// Bones/origin copied as captured in Store() at the time -- no SetupBones here. Doing
		// it again with today's pose and yesterday's timestamp is exactly bug #1 this fixes.
		pOut[iCount++] = tRecord;
	}
	return iCount;
}
