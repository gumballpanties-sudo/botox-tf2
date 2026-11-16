#include "hud_aim.h"
#include "sdk.h"
#include "aimbot.h"       // F::Aimbot.m_bToggled: the aim key's toggle latch
#include "aim_record.h"   // BacktrackLerp: m_fLerpTime as the server computes it
#include "../hud/hud_snap.h"
#include "../hud/targetid_pure.h"
#include "../menu/cfg/vars_hud.h"
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

void DebugLog(const char* fmt, ...);   // dllmain.cpp
bool Binds_Blocked();                  // render.cpp: aim key typed in chat = not held
extern bool g_chCsgoEnable;            // dllmain.cpp: csgo crosshair, styles 0/1 = panorama
extern int  g_chCsgoStyle;
extern bool g_chCsgoEnemyRed;

// ============================================================================================
// HUD extras, game thread half. botox reads these straight off g_ctx/g_interfaces inside its
// paint; here the frame may be built on the render thread, so the numbers are taken where the
// typed SDK is (IEngineVGui::Paint, hooks.cpp) and published as a POD copy (hud/hud_snap.h).
//   sniper crosshair  botox indicators.cpp sniper_crosshair gate (sniper weapon, not scoped)
//   fov circle        botox aimbot_fov_circle (projection probe, same 5 deg / 8192 u)
//   net graph         botox net_graph: CNetGraphPanel::GetFrameData's inputs
// ============================================================================================

namespace
{
	void Copy(char* dst, size_t n, const char* s) { strncpy_s(dst, n, s ? s : "", _TRUNCATE); }

	// ---- net_graphtext: engine text off while ours draws (botox stored_text) -------------------
	// Raw value write, not SetValue: net_graphtext is FCVAR_ARCHIVE, and DrawTextFields reads
	// GetBool (m_nValue). The string keeps the player's value, so a config save mid-session
	// writes their setting back, never our 0.
	ConVar* s_ngText = nullptr;
	int s_ngStored = INT_MIN;

	void NgTextPin(bool on)
	{
		if (!s_ngText) s_ngText = H::ConVars.FindVar("net_graphtext");
		if (!s_ngText || !s_ngText->m_pParent) return;
		ConVar* p = s_ngText->m_pParent;
		if (on)
		{
			if (s_ngStored == INT_MIN) s_ngStored = p->m_nValue;
			p->m_nValue = 0; p->m_fValue = 0.f;
		}
		else if (s_ngStored != INT_MIN)
		{
			p->m_nValue = s_ngStored; p->m_fValue = float(s_ngStored);
			s_ngStored = INT_MIN;
		}
	}

	bool IsSniperRifle(int id)
	{
		return id == TF_WEAPON_SNIPERRIFLE || id == TF_WEAPON_SNIPERRIFLE_DECAP || id == TF_WEAPON_SNIPERRIFLE_CLASSIC;
	}

	// ---- csgo crosshair enemy red: C_CSPlayer::UpdateIDTarget (c_cs_player.cpp:6537) ---------
	// CTraceFilterSimple(local, COLLISION_GROUP_NONE): everything but us. TF2's two invisible
	// team brushes pass, csgo has nothing like them.
	class CTraceFilterTargetId : public ITraceFilter
	{
	public:
		CBaseEntity* pSkip = nullptr;
		bool ShouldHitEntity(IHandleEntity* pServerEntity, int) override
		{
			if (!pServerEntity || pServerEntity == pSkip) return false;
			switch (reinterpret_cast<CBaseEntity*>(pServerEntity)->GetClassID())
			{
			case ETFClassID::CFuncAreaPortalWindow:
			case ETFClassID::CFuncRespawnRoomVisualizer: return false;
			default: return true;
			}
		}
		TraceType_t GetTraceType() const override { return TRACE_EVERYTHING; }
	};

	constexpr unsigned kContentsOpaque  = 0x80;     // bspflags.h CONTENTS_OPAQUE
	constexpr unsigned kIgnoreNodrawOpq = 0x2000;   // bspflags.h CONTENTS_IGNORE_NODRAW_OPAQUE
	constexpr unsigned kSurfHitbox      = 0x8000;   // bspflags.h SURF_HITBOX
	constexpr unsigned kMaskVisNpcs     = CONTENTS_SOLID | CONTENTS_MOVEABLE | kContentsOpaque
	                                    | CONTENTS_MONSTER | kIgnoreNodrawOpq;   // MASK_VISIBLE_AND_NPCS

	bool HitNonWorld(const CGameTrace& tr) { return tr.m_pEnt && tr.m_pEnt->entindex() != 0; }

	// UTIL_ClipTraceToPlayers (util_shared.cpp:690), as shipped: smallestFraction starts from
	// the caller's trace while each player trace is a fraction of the longer ray.
	void ClipTraceToPlayers(const Vec3& vStart, const Vec3& vEnd, unsigned nMask, ITraceFilter* pFilter, CGameTrace* pTr)
	{
		Ray_t ray; ray.Init(vStart, vEnd);
		float flSmallest = pTr->fraction;
		const float a[3] = { vStart.x, vStart.y, vStart.z }, b[3] = { vEnd.x, vEnd.y, vEnd.z };
		const int nMax = I::EngineClient->GetMaxClients();
		for (int k = 1; k <= nMax; ++k)
		{
			auto pEnt = I::ClientEntityList->GetClientEntity(k);
			if (!pEnt) continue;
			auto pPlayer = pEnt->As<CBaseEntity>();
			if (!pPlayer->IsPlayer() || pPlayer->IsDormant() || !pPlayer->As<CTFPlayer>()->IsAlive()) continue;
			if (pFilter && !pFilter->ShouldHitEntity(pPlayer, nMask)) continue;
			const Vec3 c = pPlayer->GetCenter();
			const float p[3] = { c.x, c.y, c.z };
			const float flRange = tid::DistanceToRay(p, a, b);
			if (flRange < 0.f || flRange > 60.f) continue;
			CGameTrace tPlayer = {};
			I::EngineTrace->ClipRayToEntity(ray, nMask | CONTENTS_HITBOX, pPlayer, &tPlayer);
			if (tPlayer.fraction < flSmallest) { *pTr = tPlayer; flSmallest = tPlayer.fraction; }
		}
	}

	// The entity index the crosshair is on this frame, 0 = none. The 0.4 s delay / 0.2 s hold
	// that turn it into GetIDTarget() are tid::TargetId (hud/targetid_pure.h, tested).
	int TraceIdTarget(CTFPlayer* pLocal)
	{
		const Vec3 vEye = pLocal->GetAbsOrigin() + pLocal->m_vecViewOffset();   // CalcPlayerView: the rendered eye
		Vec3 vFwd; Math::AngleVectors(I::EngineClient->GetViewAngles(), &vFwd);
		Vec3 vEnd = vEye + vFwd * 2500.f;
		const Vec3 vStart = vEye + vFwd * 10.f;

		CTraceFilterTargetId filter; filter.pSkip = pLocal;
		CGameTrace tr = {};
		SDK::Trace(vStart, vEnd, kMaskVisNpcs | MASK_SOLID, &filter, &tr);
		vEnd = tr.endpos + vFwd * 32.f;
		if (!tr.startsolid && !HitNonWorld(tr))
			ClipTraceToPlayers(vStart, vEnd + vFwd * 40.f, MASK_SOLID | CONTENTS_HITBOX, &filter, &tr);   // hitboxes outside the hull

		if (tr.startsolid || !HitNonWorld(tr) || tr.m_pEnt == pLocal) return 0;
		CBaseEntity* pEntity = tr.m_pEnt;
		// only test against hit boxes for enemy players: the hull alone does not count
		if (pEntity->m_iTeamNum() != pLocal->m_iTeamNum())
		{
			CGameTrace trHb = {};
			SDK::Trace(vStart, vEnd, kMaskVisNpcs | CONTENTS_HITBOX, &filter, &trHb);
			if (!(trHb.surface.flags & kSurfHitbox)) return 0;
		}
		return pEntity->entindex();
	}

	// csgo_hudreticle.cpp GetPlayerTargetIDRenderData: m_bWantEnemyCrosshair = an enemy PLAYER
	bool CsgoEnemyUnderCrosshair(CTFPlayer* pLocal)
	{
		static tid::TargetId s_tid;
		const float now = I::GlobalVars->curtime;
		s_tid.Update(now, TraceIdTarget(pLocal));
		const int idx = s_tid.Get(now);
		if (idx <= 0) return false;
		auto pEnt = I::ClientEntityList->GetClientEntity(idx);
		if (!pEnt) return false;
		auto pBase = pEnt->As<CBaseEntity>();
		return pBase->IsPlayer() && pBase->m_iTeamNum() != pLocal->m_iTeamNum();
	}

	// botox aimbot_fov_circle: project a point 5 deg above the view and measure it, so the
	// radius follows fov / aspect / viewmodel-fov changes without knowing any of them
	float FovRadius(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
	{
		if (!Vars::Aimbot::General::Enabled.Value || !pLocal->IsAlive())
			return 0.f;

		if (g_hudFovCircleKeyOnly)
		{
			const int iKey = Vars::Aimbot::General::AimKey.Value;
			const int iMode = Vars::Aimbot::General::AimKeyMode.Value;
			if (iKey && iMode == Vars::Aimbot::General::AimKeyModeEnum::OnHotkey && (Binds_Blocked() || !(GetAsyncKeyState(iKey) & 0x8000)))
				return 0.f;
			if (iKey && iMode == Vars::Aimbot::General::AimKeyModeEnum::Toggle && !F::Aimbot.m_bToggled)
				return 0.f;
		}

		// TF2 has two aim fovs: the projectile path takes its own (vars.h Projectile::AimFOV)
		const bool bProj = pWeapon && SDK::GetWeaponType(pWeapon) == EWeaponType::PROJECTILE;
		const float flFov = std::min(bProj ? Vars::Aimbot::Projectile::AimFOV.Value : Vars::Aimbot::General::AimFOV.Value, 88.f);
		if (flFov <= 0.f)
			return 0.f;

		constexpr float kProbeDeg = 5.f, kProbeRange = 8192.f;
		const Vec3 vAng = I::EngineClient->GetViewAngles();
		Vec3 vProbeAng = vAng;
		vProbeAng.x -= kProbeDeg;

		Vec3 vFwd, vProbe;
		Math::AngleVectors(vAng, &vFwd);
		Math::AngleVectors(vProbeAng, &vProbe);
		const Vec3 vEye = pLocal->GetEyePosition();

		Vec3 s0, s1;
		if (!SDK::W2S(vEye + vFwd * kProbeRange, s0) || !SDK::W2S(vEye + vProbe * kProbeRange, s1))
			return 0.f;

		const float flDist = std::sqrt((s0.x - s1.x) * (s0.x - s1.x) + (s0.y - s1.y) * (s0.y - s1.y));
		const float flRad = flDist / std::tan(DEG2RAD(kProbeDeg)) * std::tan(DEG2RAD(flFov));
		return flRad >= 1.f ? flRad : 0.f;
	}

	void NetGraph(HudSnap& s)
	{
		static ConVar *net_graph = nullptr, *net_graphpos = nullptr, *net_graphheight = nullptr, *cl_updaterate = nullptr, *cl_cmdrate = nullptr;
		if (!net_graph)
		{
			net_graph       = H::ConVars.FindVar("net_graph");
			net_graphpos    = H::ConVars.FindVar("net_graphpos");
			net_graphheight = H::ConVars.FindVar("net_graphheight");
			cl_updaterate   = H::ConVars.FindVar("cl_updaterate");
			cl_cmdrate      = H::ConVars.FindVar("cl_cmdrate");
		}

		// CNetGraphPanel::DrawTextFields m_Framerate, plus a running std dev for botox's "var"
		// (CS:GO reads absoluteframestarttimestddev, TF2's globals have no such field)
		static float s_frame = 0.f, s_var = 0.f;
		const float ft = I::GlobalVars->absoluteframetime;
		s_frame = 0.9f * s_frame + 0.1f * ft;
		if (s_frame <= 0.f) s_frame = 1.f;
		s_var = 0.9f * s_var + 0.1f * (ft - s_frame) * (ft - s_frame);
		s.fps = 1.f / s_frame;
		s.frameVarMs = std::sqrt(s_var) * 1000.f;

		s.netGraph       = net_graph ? net_graph->GetInt() : 0;
		s.netGraphPos    = net_graphpos ? net_graphpos->GetInt() : 1;
		s.netGraphHeight = net_graphheight ? net_graphheight->GetInt() : 64;
		s.updateRate     = cl_updaterate ? cl_updaterate->GetFloat() : 66.f;
		s.cmdRate        = cl_cmdrate ? cl_cmdrate->GetFloat() : 66.f;
		s.tickInterval   = I::GlobalVars->interval_per_tick;
		s.interp         = BacktrackLerp();

		constexpr int kTotal = 13;   // INetChannelInfo::TOTAL (public/inetchannelinfo.h)
		static int s_outBytes = 0;
		auto pNci = I::EngineClient->IsConnected() ? I::EngineClient->GetNetChannelInfo() : nullptr;
		if (pNci)
		{
			s.loss       = pNci->GetAvgLoss(FLOW_INCOMING);
			s.choke      = pNci->GetAvgChoke(FLOW_INCOMING);
			s.latency    = pNci->GetAvgLatency(FLOW_OUTGOING);
			s.inData     = pNci->GetAvgData(FLOW_INCOMING) / 1024.f;
			s.outData    = pNci->GetAvgData(FLOW_OUTGOING) / 1024.f;
			s.inPackets  = pNci->GetAvgPackets(FLOW_INCOMING);
			s.outPackets = pNci->GetAvgPackets(FLOW_OUTGOING);
			s.inBytes    = pNci->GetPacketBytes(FLOW_INCOMING, pNci->GetSequenceNr(FLOW_INCOMING), kTotal);
			if (const int out = pNci->GetPacketBytes(FLOW_OUTGOING, pNci->GetSequenceNr(FLOW_OUTGOING) - 1, kTotal))
				s_outBytes = out;
			pNci->GetRemoteFramerate(&s.svTime, &s.svDev);
			if (s.updateRate > 0.001f)
				s.latency -= 0.5f / s.updateRate;
			s.latency = std::max(s.latency, 0.f);
		}
		s.outBytes = s_outBytes;
		if (I::EngineClient->IsPlayingDemo())
			s.latency = 0.f;

		// CNetGraphPanel::DrawServerType, TF2 shape (no valve-ds proxy flag, no p2p)
		const char* type = "offline";
		if (I::EngineClient->IsPlayingDemo())             type = "demo";
		else if (pNci && pNci->IsLoopback())              type = "local";
		else if (I::EngineClient->IsInGame())             type = I::EngineClient->IsHLTV() ? "SourceTV" : "online";
		else if (I::EngineClient->IsConnected())          type = "loading";
		Copy(s.serverType, sizeof(s.serverType), type);
	}
}

void HudAim_Collect(CTFPlayer* pLocal)
{
	NgTextPin(g_hudNg);

	HudSnap s;
	s.srcW = float(H::Draw.m_nScreenW);
	s.srcH = float(H::Draw.m_nScreenH);
	s.alive = pLocal && pLocal->IsAlive();
	if (pLocal)
	{
		CTFWeaponBase* pWeapon = H::Entities.GetWeapon();
		if (g_hudSniperCh && s.alive && pWeapon)
			s.sniperUnscoped = IsSniperRifle(pWeapon->GetWeaponID()) && !pLocal->InCond(TF_COND_ZOOMED);
		if (g_chCsgoEnable && g_chCsgoStyle <= 1 && g_chCsgoEnemyRed && s.alive)
			s.csgoEnemy = CsgoEnemyUnderCrosshair(pLocal);
		if (g_hudFovCircle)
			s.fovRadius = FovRadius(pLocal, pWeapon);
		if (g_hudNg)
			NetGraph(s);
	}
	s.stampMs = GetTickCount64();

	static bool s_logged = false;
	if (!s_logged && (g_hudNg || g_hudFovCircle || g_hudSniperCh))
	{
		s_logged = true;
		DebugLog("hud: collect live (screen %dx%d, ng=%d fov=%d sniper=%d)", H::Draw.m_nScreenW, H::Draw.m_nScreenH,
			int(g_hudNg), int(g_hudFovCircle), int(g_hudSniperCh));
	}

	HudSnapGuard g;
	g_hudSnap = s;
}

void HudAim_Restore()
{
	NgTextPin(false);
}
