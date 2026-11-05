#include "sdk.h"
#include "../hud/radar_snap.h"
#include "../menu/cfg/vars_hud.h"
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

void DebugLog(const char* fmt, ...);   // dllmain.cpp
extern void* g_matSystem;              // IMaterialSystem (dih, thread.cpp)

// ============================================================================================
// csgo radar, game thread half (hud/radar.cpp draws). Runs from hooks.cpp IEngineVGui::Paint
// next to HudAim_Collect, where traces and entity reads are safe.
//
//   bounds   the bot nav mesh, maps/<map>.nav through the engine filesystem (Valve's own in
//            tf2_misc_dir.vpk, or one nav_generate wrote to tf/maps): the walkable extent, so
//            skybox and out-of-map brushes never shrink the map. No .nav ->
//            CWorld m_WorldMins/m_WorldMaxs, and the file is re-checked every 10 s (a nav_generate
//            mid-session reloads the map with the new mesh).
//   texture  the world, traced straight down once per map over a grid inside those bounds and
//            shaded into a CS:GO style overview (rdr::Shade). Nav mode traces only the texels a
//            nav area covers, from just above that area's floor, so roofs never hide the floor.
//            Spread over frames by a 2 ms budget per paint.
//   players  positions / angles / teams + "spotted" (TF2 has no m_bSpotted: an enemy is spotted
//            while the local player or any teammate has line of sight to his eyes, 20 Hz, the
//            wm_aim.cpp trace). Cloaked spies stay unspotted.
// ============================================================================================

namespace
{
	class CTraceFilterWorld : public ITraceFilter
	{
	public:
		bool ShouldHitEntity(IHandleEntity*, int) override { return false; }
		TraceType_t GetTraceType() const override { return TRACE_WORLD_ONLY; }
	};

	// what a player stands on: brushes, glass, grates. Not playerclip (invisible on any overview).
	constexpr unsigned kMaskFloor = CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW | CONTENTS_GRATE;
	constexpr int    kNavN = 512;       // nav bakes trace only walkable texels
	constexpr int    kWorldN = 256;     // world fallback traces every texel top to bottom
	constexpr double kBudgetMs = 2.0;

	struct Bake
	{
		std::string level;
		bool  active = false, fromNav = false, done = false;
		bool  navSeen = false;          // a .nav existed at start (parsed or not): never re-check it
		float retryAt = 0.f;            // world entity not networked yet: next StartBake
		int   n = 0;
		rdr::Overview ov;
		rdr::Bounds b;
		std::vector<float> top, height, nz;
		std::vector<uint32_t> albedo;   // floor material colour per texel (rdr::PackAlbedo), 0 = unknown
		size_t next = 0;
		uint32_t id = 0;
		float navCheckAt = 0.f;
	} s_bake;

	double NowMs()
	{
		static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
		LARGE_INTEGER c; QueryPerformanceCounter(&c);
		return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
	}

	// "maps/ctf_2fort.bsp" -> "maps/ctf_2fort.nav"
	std::string NavPath(const std::string& level)
	{
		std::string p = level;
		if (p.size() > 4 && !_stricmp(p.c_str() + p.size() - 4, ".bsp")) p.resize(p.size() - 4);
		return p.empty() ? p : p + ".nav";
	}

	// IBaseFileSystem, VBaseFileSystem011 in TF2 x64 filesystem_stdio.dll: Read 0, Open 2, Close 3,
	// Seek 4, Tell 5 (reflections.cpp ReadGameFile, the same calls). The "GAME" path reaches
	// tf/maps, download, custom AND the vpks: Valve ships 46 .nav files inside tf2_misc_dir.vpk
	// (pl_upward, tr_dustbowl, the mvm maps...), which a loose file read never sees.
	void* FileSystem()
	{
		static void* fs = [] () -> void* {
			const HMODULE mod = GetModuleHandleA("filesystem_stdio.dll");
			using CreateInterfaceFn = void* (*)(const char*, int*);
			const auto fn = mod ? reinterpret_cast<CreateInterfaceFn>(GetProcAddress(mod, "CreateInterface")) : nullptr;
			return fn ? fn("VBaseFileSystem011", nullptr) : nullptr;
		}();
		return fs;
	}
	template <class R, class... A> R FsCall(void* fs, int slot, A... a)
	{
		return reinterpret_cast<R(__fastcall*)(void*, A...)>((*reinterpret_cast<void***>(fs))[slot])(fs, a...);
	}

	// ---- floor colour: the traced surface's material reflectivity (its texture's average
	// colour, what vrad bounces light with). IMaterialSystem::FindMaterial = slot 71 (chams.cpp
	// kMatSysFindMaterial, flipmath.h). Cached per surface name pointer: the trace hands back
	// the bsp's texdata string, one pointer per material, rebuilt per map (cache cleared per bake).
	IMaterial* FindWorldMaterial(const char* name)
	{
		__try { return FsCall<IMaterial*>(g_matSystem, 71, name, "World textures", false, (const char*)nullptr); }
		__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
	}
	bool MaterialReflectivity(IMaterial* pMat, Vec3* out)
	{
		__try
		{
			if (!pMat || pMat->IsErrorMaterial()) return false;
			pMat->GetReflectivity(*out);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	std::unordered_map<const char*, uint32_t> s_surfColor;

	uint32_t SurfaceColor(const char* name)
	{
		// "**studio**" = static prop, "**displacement**" etc: no material behind the name
		if (!g_matSystem || !name || !*name || name[0] == '*') return 0;
		if (auto it = s_surfColor.find(name); it != s_surfColor.end()) return it->second;
		Vec3 r{};
		const uint32_t c = MaterialReflectivity(FindWorldMaterial(name), &r) ? rdr::PackAlbedo(r.x, r.y, r.z) : 0u;
		s_surfColor.emplace(name, c);
		return c;
	}
	void* FsOpen(void* fs, const char* path) { __try { return FsCall<void*>(fs, 2, path, "rb", "GAME"); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
	void FsClose(void* fs, void* h) { __try { FsCall<void>(fs, 3, h); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	void FsSeek(void* fs, void* h, int pos, int whence) { __try { FsCall<void>(fs, 4, h, pos, whence); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
	unsigned FsTell(void* fs, void* h) { __try { return FsCall<unsigned>(fs, 5, h); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
	int FsRead(void* fs, void* h, void* out, int n) { __try { return FsCall<int>(fs, 0, out, n, h); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }

	bool NavExists(const std::string& path)
	{
		void* fs = FileSystem();
		void* h = fs && !path.empty() ? FsOpen(fs, path.c_str()) : nullptr;
		if (h) FsClose(fs, h);
		return h != nullptr;
	}

	bool LoadNav(const std::string& path, std::vector<rdr::NavArea>& areas)
	{
		void* fs = FileSystem();
		void* h = fs && !path.empty() ? FsOpen(fs, path.c_str()) : nullptr;
		if (!h) return false;
		FsSeek(fs, h, 0, 2);   // FILESYSTEM_SEEK_TAIL
		const unsigned size = FsTell(fs, h);
		std::vector<uint8_t> bytes;
		bool ok = size > 16 && size < (64u << 20);
		if (ok)
		{
			FsSeek(fs, h, 0, 0);   // FILESYSTEM_SEEK_HEAD
			bytes.resize(size);
			ok = FsRead(fs, h, bytes.data(), int(size)) == int(size);
		}
		FsClose(fs, h);
		return ok && rdr::NavParse(bytes.data(), bytes.size(), areas);
	}

	bool WorldBounds(rdr::Bounds& b)
	{
		static const int nMins = U::NetVars.GetNetVar("CWorld", "m_WorldMins");   // c_world.cpp DT_World
		static const int nMaxs = U::NetVars.GetNetVar("CWorld", "m_WorldMaxs");
		auto pWorld = I::ClientEntityList->GetClientEntity(0);
		if (!pWorld || nMins <= 0 || nMaxs <= 0) return false;
		const Vec3 mn = *reinterpret_cast<Vec3*>(uintptr_t(pWorld) + nMins);
		const Vec3 mx = *reinterpret_cast<Vec3*>(uintptr_t(pWorld) + nMaxs);
		if (mx.x - mn.x < 64.f || mx.y - mn.y < 64.f) return false;
		b = { mn.x, mn.y, mn.z, mx.x, mx.y, mx.z };
		return true;
	}

	void StartBake(const std::string& level)
	{
		Bake& k = s_bake;
		const uint32_t id = k.id + 1;
		k = Bake{};
		k.id = id;
		k.level = level;
		k.navCheckAt = I::GlobalVars->realtime + 10.f;

		std::vector<rdr::NavArea> areas;
		const std::string nav = NavPath(level);
		const bool navThere = NavExists(nav);
		k.navSeen = navThere;
		if (navThere && LoadNav(nav, areas))
		{
			k.fromNav = true;
			k.b = rdr::NavBounds(areas);
			k.n = kNavN;
			k.ov = rdr::FitOverview(k.b);
			rdr::NavRaster(areas, k.ov, k.n, k.top);
			g_radarSource = 1;
			g_radarAreas = int(areas.size());
			DebugLog("radar: %s nav mesh, %d areas, bounds %.0f %.0f .. %.0f %.0f", level.c_str(), int(areas.size()),
				k.b.x0, k.b.y0, k.b.x1, k.b.y1);
		}
		else if (WorldBounds(k.b))
		{
			k.n = kWorldN;
			k.ov = rdr::FitOverview(k.b);
			g_radarSource = 2;
			g_radarAreas = 0;
			DebugLog("radar: %s no nav mesh%s, world bounds %.0f %.0f .. %.0f %.0f", level.c_str(),
				navThere ? " (unreadable)" : "", k.b.x0, k.b.y0, k.b.x1, k.b.y1);
		}
		else
		{
			g_radarSource = 0;
			k.retryAt = I::GlobalVars->realtime + 0.5f;   // world entity not networked yet: try again shortly
			return;
		}
		k.height.assign(size_t(k.n) * k.n, NAN);
		k.nz.assign(size_t(k.n) * k.n, 1.f);
		k.albedo.assign(size_t(k.n) * k.n, 0u);
		s_surfColor.clear();
		k.active = true;
		g_radarBakePct = 0;
	}

	// one texel: floor z + normal z, or false (void)
	bool TraceTexel(const Bake& k, size_t i, float& z, float& nz, uint32_t& col)
	{
		col = 0;
		float x, y;
		rdr::TexelToWorld(k.ov, k.n, float(i % k.n) + 0.5f, float(i / k.n) + 0.5f, x, y);
		CTraceFilterWorld filter;
		CGameTrace tr = {};

		if (k.fromNav)
		{
			const float top = k.top[i];
			if (!std::isfinite(top)) return false;
			SDK::Trace({ x, y, top + 16.f }, { x, y, top - 512.f }, kMaskFloor, &filter, &tr);
			if (tr.startsolid || tr.fraction >= 1.f) { z = top; nz = 1.f; }
			else { z = tr.endpos.z; nz = tr.plane.normal.z; col = SurfaceColor(tr.surface.name); }
			return true;
		}

		// world: top of the world down, stepping through sky brushes and out of start-solid
		float from = k.b.z1 - 1.f;
		const float to = k.b.z0 - 1.f;
		for (int step = 0; step < 8 && from > to; ++step)
		{
			tr = {};
			SDK::Trace({ x, y, from }, { x, y, to }, kMaskFloor, &filter, &tr);
			if (tr.allsolid) return false;
			if (tr.startsolid) { from = from + (to - from) * tr.fractionleftsolid - 1.f; continue; }
			if (tr.fraction >= 1.f) return false;
			if (tr.surface.flags & SURF_SKY) { from = tr.endpos.z - 1.f; continue; }
			z = tr.endpos.z; nz = tr.plane.normal.z; col = SurfaceColor(tr.surface.name);
			return true;
		}
		return false;
	}

	void FinishBake()
	{
		Bake& k = s_bake;
		float zLo = k.b.z0, zHi = k.b.z1;
		if (!k.fromNav)   // world bounds include the skybox: shade over what was actually hit
		{
			zLo = 1e30f; zHi = -1e30f;
			for (float z : k.height) if (!std::isnan(z)) { zLo = std::min(zLo, z); zHi = std::max(zHi, z); }
			if (zLo > zHi) zLo = zHi = 0.f;
		}
		RadarImage img;
		img.id = k.id;
		img.n = k.n;
		rdr::Shade(k.height, k.nz, k.n, zLo, zHi, img.px, &k.albedo);
		{
			std::lock_guard<std::mutex> lock(g_radarImageLock);
			g_radarImage = std::move(img);
		}
		g_radarImageId = k.id;
		g_radarBakePct = 100;
		k.done = true;
		k.top.clear(); k.top.shrink_to_fit();
		k.height.clear(); k.height.shrink_to_fit();
		k.nz.clear(); k.nz.shrink_to_fit();
		k.albedo.clear(); k.albedo.shrink_to_fit();
		s_surfColor.clear();
		DebugLog("radar: overview baked (%dx%d, %s)", k.n, k.n, k.fromNav ? "nav" : "world");
	}

	void StepBake()
	{
		Bake& k = s_bake;
		if (!k.active || k.done) return;
		const double t0 = NowMs();
		const size_t total = size_t(k.n) * k.n;
		while (k.next < total)
		{
			const int chunk = k.fromNav ? 64 : 8;   // a world texel can be 8 traces: keep the budget check close
			for (int j = 0; j < chunk && k.next < total; ++j, ++k.next)
			{
				float z, nz;
				uint32_t col;
				if (TraceTexel(k, k.next, z, nz, col)) { k.height[k.next] = z; k.nz[k.next] = nz; k.albedo[k.next] = col; }
			}
			if (NowMs() - t0 > kBudgetMs) break;
		}
		g_radarBakePct = int(k.next * 100 / total);
		if (k.next >= total) FinishBake();
	}

	// ---- players ---------------------------------------------------------------------------
	bool s_spotted[kRadarSlots] = {};

	CTFPlayer* PlayerAt(int i)
	{
		auto pEnt = I::ClientEntityList->GetClientEntity(i);
		if (!pEnt) return nullptr;
		auto pBase = pEnt->As<CBaseEntity>();
		return pBase->IsPlayer() ? pBase->As<CTFPlayer>() : nullptr;
	}

	// 20 Hz, like wm_aim.cpp: eye -> eye from the local player and every live teammate
	void UpdateSpotted(CTFPlayer* pLocal, int nMax)
	{
		static float flNext = 0.f;
		const float flNow = I::GlobalVars->realtime;
		if (flNow >= flNext - 1.f && flNow < flNext)   // realtime restarts on map change: resync then
			return;
		flNext = flNow + 0.05f;

		const int iLocalTeam = pLocal->m_iTeamNum();
		std::vector<CTFPlayer*> watchers;
		for (int i = 1; i <= nMax; ++i)
			if (auto p = PlayerAt(i); p && !p->IsDormant() && p->IsAlive() && p->m_iTeamNum() == iLocalTeam)
				watchers.push_back(p);

		for (int i = 1; i <= nMax; ++i)
		{
			s_spotted[i] = false;
			auto pPlayer = PlayerAt(i);
			if (!pPlayer || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->m_iTeamNum() == iLocalTeam)
				continue;
			if (pPlayer->InCond(TF_COND_STEALTHED))
				continue;
			const Vec3 vTo = pPlayer->GetAbsOrigin() + pPlayer->m_vecViewOffset();
			for (auto pWatcher : watchers)
				if (SDK::VisPos(pWatcher, pPlayer, pWatcher->GetEyePosition(), vTo)) { s_spotted[i] = true; break; }
		}
	}

	constexpr int kObsInEye = 4, kObsChase = 5;   // shared.h OBS_MODE_IN_EYE / OBS_MODE_CHASE

	CTFPlayer* ObserverTarget(CTFPlayer* pLocal, int& iMode)
	{
		static const int nMode = U::NetVars.GetNetVar("CBasePlayer", "m_iObserverMode");
		static const int nTarget = U::NetVars.GetNetVar("CBasePlayer", "m_hObserverTarget");
		iMode = 0;
		if (nMode <= 0 || nTarget <= 0) return nullptr;
		iMode = *reinterpret_cast<int*>(uintptr_t(pLocal) + nMode);
		if (iMode != kObsInEye && iMode != kObsChase) return nullptr;
		auto pEnt = reinterpret_cast<EHANDLE*>(uintptr_t(pLocal) + nTarget)->Get();
		return pEnt && pEnt->IsPlayer() ? pEnt->As<CTFPlayer>() : nullptr;
	}

	void Collect(CTFPlayer* pLocal)
	{
		RadarSnap snap;
		snap.haveMap = s_bake.active;
		snap.bakeId = s_bake.id;
		snap.ov = s_bake.ov;
		snap.viewYaw = I::EngineClient->GetViewAngles().y;
		snap.localTeam = pLocal->m_iTeamNum();
		snap.spec = snap.localTeam < 2 || I::EngineClient->IsHLTV();

		int iObsMode = 0;
		CTFPlayer* pTarget = pLocal->IsAlive() ? nullptr : ObserverTarget(pLocal, iObsMode);
		const Vec3 vCam = (pTarget ? pTarget : pLocal)->GetAbsOrigin();
		snap.camX = vCam.x; snap.camY = vCam.y; snap.camZ = vCam.z;
		// first person spectating: CalcInEyeCamView shows the target's eye angles and never sets
		// the engine view angles (those stay the spectator's mouse) -- CS:GO's MainViewAngles
		if (pTarget && iObsMode == kObsInEye) snap.viewYaw = pTarget->GetEyeAngles().y;

		const int nMax = std::min(I::EngineClient->GetMaxClients(), kRadarSlots - 1);
		UpdateSpotted(pLocal, nMax);
		auto pResource = H::Entities.GetResource();
		for (int i = 1; i <= nMax; ++i)
		{
			auto pPlayer = PlayerAt(i);
			if (!pPlayer) continue;
			RadarPlayer& r = snap.players[i];
			r.valid = true;
			r.local = pPlayer == pLocal;
			r.team = pPlayer->m_iTeamNum();
			r.active = !pPlayer->IsDormant();
			r.alive = r.active ? pPlayer->IsAlive() : (pResource && pResource->m_bAlive(i));
			r.selected = pPlayer == pTarget;
			// CS:GO counts friends always spotted because its server sends their positions; TF2
			// culls teammates by PVS like anyone (CTFPlayer::ShouldTransmit), so a dormant one
			// ghost-fades at his last spot instead of freezing there at full alpha
			r.spotted = r.active && (r.team == snap.localTeam || snap.spec || g_hudRadarShowAll || s_spotted[i] || r.selected);
			const Vec3 v = pPlayer->GetAbsOrigin();
			r.x = v.x; r.y = v.y; r.z = v.z;
			r.yaw = r.local ? snap.viewYaw : pPlayer->GetEyeAngles().y;
		}
		snap.stampMs = GetTickCount64();

		std::lock_guard<std::mutex> lock(g_radarSnapLock);
		g_radarSnap = snap;
	}
}

void RadarAim_Tick(CTFPlayer* pLocal)
{
	if (!g_hudRadar || !pLocal || !I::EngineClient->IsInGame())
		return;

	const char* szLevel = I::EngineClient->GetLevelName();
	const std::string level = szLevel ? szLevel : "";
	if (level.empty())
		return;
	const float now = I::GlobalVars->realtime;
	if (now + 1.f < s_bake.navCheckAt - 11.f || now + 1.f < s_bake.retryAt - 1.5f)   // realtime restarted (map change)
		s_bake.navCheckAt = s_bake.retryAt = now;
	if (level != s_bake.level)
		StartBake(level);
	else if (!s_bake.active)
	{
		if (now >= s_bake.retryAt) StartBake(level);
	}
	else if (!s_bake.fromNav && !s_bake.navSeen && now >= s_bake.navCheckAt)
	{
		// no nav at map start: one may appear (nav_generate). A nav that was there but would not
		// parse is never retried -- it would restart the world bake every 10 s forever.
		s_bake.navCheckAt = now + 10.f;
		if (NavExists(NavPath(level))) StartBake(level);
	}

	StepBake();
	Collect(pLocal);
}
