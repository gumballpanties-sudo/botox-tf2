// Player chams + player glow (10-31 rework).
//
// Structure: botox CS:GO hacks/chams/chams.h (material table, ignorez "xqz" variant per slot,
// layer lists drawn in order through the original DrawModelExecute) + Amalgam
// Features/Visuals/{Materials,Chams} (materials from KeyValues through
// IMaterialSystem::CreateMaterial, colour through IVRenderView modulation/blend + $envmaptint/
// $phongtint, override + colour state saved before and restored after every model).
//
// Lives in the aim tree because it needs the aim SDK (IMaterial, KeyValues, CTFPlayer,
// F::Backtrack); dih's DrawModelExecute hook (visuals.cpp) reaches it through chams_api.h.
//
// Material refcount (the "botox_glow/glow has bad reference count 0" console spam): the old
// code FindMaterial'd its materials without IncrementReferenceCount and then DecRef'd them on
// every level change. Here every material we hold gets exactly one IncrementReferenceCount
// when created/found and exactly one DecrementReferenceCount when released.
#include "sdk.h"
#include "backtrack.h"
#include "../visuals/chams_api.h"
#include "../visuals/chams_layers.h"
#include "../esp/esp_fx.h"   // EspFx_NowMs: one clock for the sound stamp (game thread) and the draw
#include <cmath>

void DebugLog(const char* fmt, ...);   // dllmain.cpp
bool Aim_Active();                     // aim.cpp: false = I::/S:: never initialised, chams stays off

extern void* g_matSystem;     // IMaterialSystem (dih, thread.cpp)
extern void* g_modelRender;   // IVModelRender  (dih, thread.cpp)

// ---- settings (dllmain.cpp, saved by config.cpp) ----
extern bool  g_chamsEnable;              // botox "enable enemy chams"
extern bool  g_chamsTeam;                // teammates (TF2 extra, same visible/occluded lists)
extern bool  g_chamsLocalEnable;         // botox "enable local chams"
extern bool  g_chamsRagdolls;
extern bool  g_chamsBacktrackXqz;
extern int   g_chamsBacktrackType;       // 0 oldest record, 1 all records, 2 aimbot target record
extern int   g_chamsBacktrackMaxGhosts;
extern bool  g_chamsBacktrackGradient;
extern float g_chamsSoundDuration;
extern bool  g_chamsSoundFade;
extern float g_chamsSoundMinVolume;
extern float g_chamsSoundMaxDistance;
extern char  g_chamsVisibleCfg[ChamsLayers::kCfgLen];
extern char  g_chamsOccludedCfg[ChamsLayers::kCfgLen];
extern char  g_chamsBacktrackCfg[ChamsLayers::kCfgLen];
extern char  g_chamsSoundCfg[ChamsLayers::kCfgLen];
extern char  g_chamsTeamVisibleCfg[ChamsLayers::kCfgLen];
extern char  g_chamsTeamOccludedCfg[ChamsLayers::kCfgLen];
extern char  g_chamsWeaponCfg[ChamsLayers::kCfgLen];
extern char  g_chamsArmsCfg[ChamsLayers::kCfgLen];
extern char  g_chamsSleeveCfg[ChamsLayers::kCfgLen];

extern bool  g_glowEnable;               // botox "player glow"
extern float g_glowVisColor[4];
extern float g_glowInvisColor[4];
extern bool  g_glowLegacy;
extern int   g_glowLegacyStyle;          // 0 default, 1 rim, 2 outline, 3 pulse
extern bool  g_glowNormalBlend;
extern float g_glowThickness;
extern bool  g_glowGradient;
extern float g_glowVisOuterColor[4];
extern float g_glowInvisOuterColor[4];
extern float g_glowGradientInner;
extern bool  g_glowWave;
extern float g_glowWaveColor[4];
extern float g_glowWaveSpeed;
extern float g_glowWaveSize;
extern float g_glowWaveStrength;
extern bool  g_glowPulse;
extern float g_glowPulseSpeed;
extern float g_glowPulseMin;

namespace
{
	// ---- vtable slots, each verified against Amalgam x64 TF2 (Aletherium-main/Amalgam/src) ----
	// IMaterialSystem: SDK/Definitions/Interfaces/IMaterialSystem.h:425-426, CreateMaterial is the
	// slot right before FindMaterial; FindMaterial 71 is flipmath.h matFindMaterial (live in this tree).
	constexpr int kMatSysCreateMaterial = 70;
	constexpr int kMatSysFindMaterial   = 71;
	// IVModelRender: SDK/Definitions/Interfaces/IVModelRender.h: DrawModel 0, ForcedMaterialOverride 1,
	// DrawModelExecute 19 (dih's hook slot), GetMaterialOverride 25.
	constexpr int kMdlForcedMaterialOverride = 1;
	constexpr int kMdlGetMaterialOverride    = 25;
	// ModelRenderInfo_t (public/engine/ivmodelrender.h, x64): flags +0x40 (entity_index +0x44 is what
	// visuals.cpp already reads). Depth-only passes must never get chams (botox studio_depth_pass_flags).
	constexpr int kInfoFlags = 0x40;
	constexpr int kDepthPassFlags = 0x08000000 /*STUDIO_SSAODEPTHTEXTURE*/ | 0x40000000 /*STUDIO_SHADOWDEPTHTEXTURE*/;

	// IMaterialVar, Amalgam SDK/Definitions/Main/IMaterialVar.h declaration order through the
	// SetVecValue overload group (MSVC lays an overload group out as a block, so the group is
	// declared exactly as the engine header has it). Amalgam Materials.cpp:271 calls the 3-float
	// SetVecValue on TF2 x64 through this same declaration. aim/defs.h only forward-declares it.
	class ChamsMatVar
	{
	public:
		virtual void* GetTextureValue() = 0;
		virtual const char* GetName() const = 0;
		virtual unsigned short GetNameAsSymbol() const = 0;
		virtual void SetFloatValue(float val) = 0;
		virtual void SetIntValue(int val) = 0;
		virtual void SetStringValue(char const* val) = 0;
		virtual char const* GetStringValue() const = 0;
		virtual void SetFourCCValue(unsigned long type, void* pData) = 0;
		virtual void GetFourCCValue(unsigned long* type, void** ppData) = 0;
		virtual void SetVecValue(float const* val, int numcomps) = 0;
		virtual void SetVecValue(float x, float y) = 0;
		virtual void SetVecValue(float x, float y, float z) = 0;
		virtual void SetVecValue(float x, float y, float z, float w) = 0;
	};

	template <typename R, typename... A>
	R VCallAt(void* p, int idx, A... a)
	{
		return reinterpret_cast<R(__fastcall*)(void*, A...)>((*reinterpret_cast<void***>(p))[idx])(p, a...);
	}

	void ForceMaterial(IMaterial* pMat, int iType = 0 /*OVERRIDE_NORMAL*/)
	{
		if (o_ForcedMaterialOverride) o_ForcedMaterialOverride(g_modelRender, pMat, iType);   // past our own hook
		else VCallAt<void>(g_modelRender, kMdlForcedMaterialOverride, pMat, iType);
	}

	// ---- material table (botox impl_t::m_materials[index][ignorez]) ----
	struct Mat
	{
		IMaterial*   pMat = nullptr;
		ChamsMatVar* pEnvTint = nullptr;
		ChamsMatVar* pPhongTint = nullptr;
		ChamsMatVar* pFresnel = nullptr;   // $phongfresnelranges -- glow thickness
		bool         bShared = false;      // ambient: one found material serves both variants
	};
	Mat  s_mats[ChamsLayers::Count][2];
	int  s_buildIndex = 1;                 // next material to build (Original = 0 needs none)
	int  s_lastBuildFrame = -1;
	bool s_built = false;
	bool s_loggedBuilt = false;

	// botox gold tint multipliers
	constexpr float kGoldEnv[3]   = { 0.55f, 0.3f, 0.05f };
	constexpr float kGoldPhong[3] = { 0.6f, 0.35f, 0.08f };

	ChamsMatVar* FindVar(IMaterial* p, const char* name)
	{
		bool bFound = false;
		auto pVar = p->FindVar(name, &bFound, false);
		return bFound ? reinterpret_cast<ChamsMatVar*>(pVar) : nullptr;
	}

	// Amalgam CMaterials::Create + botox from_keyvalues: KeyValues -> CreateMaterial -> one IncRef.
	// KeyValues sigs (defs.cpp) checked against the installed engine.dll 10-31: both unique, and
	// LoadFromBuffer(char*) calls the CUtlBuffer overload that references "#include"/"#base".
	IMaterial* CreateFromVMT(const char* name, const char* vmt)
	{
		auto kv = new KeyValues(name);
		if (!kv->LoadFromBuffer(name, vmt))
		{
			DebugLog("chams: KeyValues::LoadFromBuffer failed for %s", name);
			delete kv;
			return nullptr;
		}
		// CreateMaterial takes ownership of kv (the material keeps it), botox/Amalgam both rely on it.
		auto pMat = VCallAt<IMaterial*>(g_matSystem, kMatSysCreateMaterial, name, kv);
		if (!pMat || pMat->IsErrorMaterial())
		{
			DebugLog("chams: CreateMaterial failed for %s (%p)", name, (void*)pMat);
			return nullptr;
		}
		pMat->IncrementReferenceCount();
		return pMat;
	}

	void StoreVars(Mat& m)
	{
		if (!m.pMat) return;
		m.pEnvTint   = FindVar(m.pMat, "$envmaptint");
		m.pPhongTint = FindVar(m.pMat, "$phongtint");
		m.pFresnel   = FindVar(m.pMat, "$phongfresnelranges");
	}

	// botox make_variants: the depth/fog flags are appended per variant, never inside `body`
	// (FindKey returns the first match -- a duplicate would win).
	void MakeVariants(int index, const char* name, const char* shader, const char* body)
	{
		static const char* const kFlags[2] = {
			"\t\"$ignorez\" \"0\"\n\t\"$znearer\" \"0\"\n\t\"$nofog\" \"0\"\n",
			"\t\"$ignorez\" \"1\"\n\t\"$znearer\" \"1\"\n\t\"$nofog\" \"1\"\n" };
		for (int v = 0; v < 2; ++v)
		{
			char sName[64], sVMT[1024];
			sprintf_s(sName, "botox_chams_%s%s", name, v ? "_xqz" : "");
			sprintf_s(sVMT, "\"%s\"\n{\n%s\t\"$model\" \"1\"\n%s}\n", shader, body, kFlags[v]);
			s_mats[index][v].pMat = CreateFromVMT(sName, sVMT);
			StoreVars(s_mats[index][v]);
		}
	}

	// botox's CS:GO material list mapped to TF2 content. Texture paths are the ones Amalgam's
	// own TF2 materials use (Materials.cpp LoadMaterials: "white", models/player/shared/
	// shared_normal, skybox/sky_dustbowl_01, cubemaps/cubemap_sheen002.hdr, models/player/shared/
	// ice_player) plus engine-wide env_cubemap / debug/debugambientcube. CS:GO-only bits
	// ($pearlescent, damascus paint, vertigo cubes) have no TF2 file and are replaced.
	void BuildOne(int index)
	{
		using namespace ChamsLayers;
		switch (index)
		{
		case Flat:
			MakeVariants(Flat, "flat", "UnlitGeneric", "\t\"$basetexture\" \"white\"\n");
			break;
		case Shaded:
			MakeVariants(Shaded, "shaded", "VertexLitGeneric", "\t\"$basetexture\" \"white\"\n\t\"$halflambert\" \"1\"\n");
			break;
		case Wireframe:
			MakeVariants(Wireframe, "wireframe", "UnlitGeneric", "\t\"$basetexture\" \"white\"\n\t\"$wireframe\" \"1\"\n");
			break;
		case Glow:   // Amalgam "Fresnel": additive fresnel rim, the TF2 analog of botox's envmap-fresnel glow
			MakeVariants(Glow, "glow", "VertexLitGeneric",
				"\t\"$basetexture\" \"white\"\n\t\"$bumpmap\" \"models/player/shared/shared_normal\"\n"
				"\t\"$color2\" \"[0 0 0]\"\n\t\"$additive\" \"1\"\n\t\"$phong\" \"1\"\n"
				"\t\"$phongfresnelranges\" \"[0 0.5 1]\"\n\t\"$envmap\" \"skybox/sky_dustbowl_01\"\n"
				"\t\"$envmapfresnel\" \"1\"\n\t\"$envmaptint\" \"[1 1 1]\"\n");
			break;
		case Metallic:   // Amalgam "Shine" + botox $envmapcontrast
			MakeVariants(Metallic, "metallic", "VertexLitGeneric",
				"\t\"$additive\" \"1\"\n\t\"$envmap\" \"cubemaps/cubemap_sheen002.hdr\"\n"
				"\t\"$envmaptint\" \"[1 1 1]\"\n\t\"$envmapcontrast\" \"1\"\n");
			break;
		case Pearlescent:   // TF2 VertexLitGeneric has no $pearlescent: botox's phong set, bumped (TF2 phong needs a bumpmap)
			MakeVariants(Pearlescent, "pearlescent", "VertexLitGeneric",
				"\t\"$basetexture\" \"white\"\n\t\"$bumpmap\" \"models/player/shared/shared_normal\"\n"
				"\t\"$phong\" \"1\"\n\t\"$phongboost\" \"2\"\n\t\"$phongfresnelranges\" \"[0.5 0.5 0.5]\"\n"
				"\t\"$phongexponent\" \"10\"\n\t\"$phongtint\" \"[1 1 1]\"\n");
			break;
		case Ambient:   // botox from_name: one engine material, both variants, flags toggled per draw
		{
			auto pMat = VCallAt<IMaterial*>(g_matSystem, kMatSysFindMaterial, "debug/debugambientcube", "Model textures", false, (const char*)nullptr);
			if (pMat && !pMat->IsErrorMaterial())
			{
				pMat->IncrementReferenceCount();
				for (int v = 0; v < 2; ++v) { s_mats[Ambient][v].pMat = pMat; s_mats[Ambient][v].bShared = true; StoreVars(s_mats[Ambient][v]); }
			}
			else
				DebugLog("chams: debug/debugambientcube not found");
			break;
		}
		case Gold:
			MakeVariants(Gold, "gold", "VertexLitGeneric",
				"\t\"$basetexture\" \"white\"\n\t\"$bumpmap\" \"models/player/shared/shared_normal\"\n"
				"\t\"$color2\" \"[.2 .11 .02]\"\n\t\"$envmap\" \"env_cubemap\"\n\t\"$envmaptint\" \"[.55 .3 .05]\"\n"
				"\t\"$envmapfresnel\" \".6\"\n\t\"$phong\" \"1\"\n\t\"$phongfresnelranges\" \"[.7 .8 1]\"\n"
				"\t\"$phongtint\" \"[.6 .35 .08]\"\n\t\"$phongboost\" \"6\"\n\t\"$phongexponent\" \"128\"\n"
				"\t\"$phongdisablehalflambert\" \"1\"\n");
			break;
		case Damascus:   // CS:GO paint texture -> TF2's own frozen-player texture, botox's phong set
			MakeVariants(Damascus, "damascus", "VertexLitGeneric",
				"\t\"$basetexture\" \"models/player/shared/ice_player\"\n\t\"$bumpmap\" \"models/player/shared/shared_normal\"\n"
				"\t\"$phong\" \"1\"\n\t\"$phongboost\" \"4\"\n\t\"$phongexponent\" \"16\"\n"
				"\t\"$phongfresnelranges\" \"[.5 .75 1]\"\n\t\"$phongtint\" \"[1 1 1]\"\n");
			break;
		case Crystal:
			MakeVariants(Crystal, "crystal", "VertexLitGeneric",
				"\t\"$basetexture\" \"white\"\n\t\"$color2\" \"[0 0 0]\"\n\t\"$bumpmap\" \"models/player/shared/shared_normal\"\n"
				"\t\"$translucent\" \"1\"\n\t\"$envmap\" \"cubemaps/cubemap_sheen002.hdr\"\n\t\"$envmaptint\" \"[1 1 1]\"\n"
				"\t\"$envmapsaturation\" \"0.1\"\n\t\"$phong\" \"1\"\n\t\"$phongexponent\" \"16\"\n"
				"\t\"$phongboost\" \"2\"\n\t\"$phongtint\" \"[1 1 1]\"\n");
			break;
		case Wet:
			MakeVariants(Wet, "wet", "VertexLitGeneric",
				"\t\"$basetexture\" \"white\"\n\t\"$bumpmap\" \"models/player/shared/shared_normal\"\n"
				"\t\"$additive\" \"1\"\n\t\"$color2\" \"[0 0 0]\"\n\t\"$envmap\" \"env_cubemap\"\n"
				"\t\"$envmaptint\" \"[1 1 1]\"\n\t\"$envmapfresnel\" \"1\"\n\t\"$phong\" \"1\"\n"
				"\t\"$phongexponent\" \"200\"\n\t\"$phongboost\" \"10\"\n\t\"$phongfresnelranges\" \"[.2 .5 1]\"\n"
				"\t\"$phongtint\" \"[1 1 1]\"\n\t\"$phongdisablehalflambert\" \"1\"\n");
			break;
		default:
			break;
		}
	}

	// botox create(): one material per frame on the render path (never off-thread), so the first
	// enable costs no visible hitch.
	void BuildStep()
	{
		if (s_built || !g_matSystem) return;
		const int frame = I::GlobalVars ? I::GlobalVars->framecount : s_lastBuildFrame + 1;
		if (frame == s_lastBuildFrame) return;
		s_lastBuildFrame = frame;

		BuildOne(s_buildIndex);
		if (++s_buildIndex >= ChamsLayers::Count)
		{
			s_built = true;
			if (!s_loggedBuilt)
			{
				int n = 0;
				for (auto& v : s_mats) for (auto& m : v) n += m.pMat != nullptr;
				DebugLog("chams: materials built (%d/%d variants)", n, 2 * (ChamsLayers::Count - 1));
				s_loggedBuilt = true;
			}
		}
	}

	IMaterial* GetMat(int index, bool ignorez)
	{
		if (index <= ChamsLayers::Original || index >= ChamsLayers::Count) return nullptr;
		BuildStep();
		return s_mats[index][ignorez ? 1 : 0].pMat;
	}

	// ---- colour (Amalgam CMaterials::SetColor + botox apply_color) ----
	void SetModulation(const float c[4], float alphaScale)
	{
		I::RenderView->SetColorModulation(c);
		I::RenderView->SetBlend(c[3] * alphaScale);
	}

	void ApplyColor(int index, Mat& m, const float c[4], float alphaScale)
	{
		SetModulation(c, alphaScale);
		const bool bGold = index == ChamsLayers::Gold;
		if (m.pEnvTint)
			bGold ? m.pEnvTint->SetVecValue(c[0] * kGoldEnv[0], c[1] * kGoldEnv[1], c[2] * kGoldEnv[2]) : m.pEnvTint->SetVecValue(c[0], c[1], c[2]);
		if (m.pPhongTint)
			bGold ? m.pPhongTint->SetVecValue(c[0] * kGoldPhong[0], c[1] * kGoldPhong[1], c[2] * kGoldPhong[2]) : m.pPhongTint->SetVecValue(c[0], c[1], c[2]);
	}

	// one model draw through the original DrawModelExecute, optionally on other bones (backtrack)
	struct DrawCtx
	{
		ChamsDrawFn fn; void* rcx; const void* pState; const void* pInfo; const void* pBones;
		void Draw(const void* pOtherBones = nullptr) const { fn(rcx, pState, pInfo, pOtherBones ? pOtherBones : pBones); }
	};

	// botox draw_layers: each slot is one full model draw under its material + colour.
	// Returns true when at least one draw happened.
	bool DrawLayers(const ChamsLayers::List& l, bool ignorez, float alphaScale, const DrawCtx& ctx, const void* pBones = nullptr)
	{
		bool bDrew = false;
		for (int i = 0; i < l.used; ++i)
		{
			const int index = l.slots[i];
			const float* c = l.colors[i];
			if (index == ChamsLayers::Original)
			{
				SetModulation(c, alphaScale);
				ForceMaterial(nullptr);
				if (c[3] * alphaScale > 0.f) { ctx.Draw(pBones); bDrew = true; }
				continue;
			}
			IMaterial* pMat = GetMat(index, ignorez);
			if (!pMat) continue;
			Mat& m = s_mats[index][ignorez ? 1 : 0];
			ApplyColor(index, m, c, alphaScale);
			if (m.bShared)
			{
				pMat->SetMaterialVarFlag(MATERIAL_VAR_IGNOREZ, ignorez);
				pMat->SetMaterialVarFlag(MATERIAL_VAR_ZNEARER, ignorez);
				pMat->SetMaterialVarFlag(MATERIAL_VAR_NOFOG, ignorez);
			}
			ForceMaterial(pMat);
			ctx.Draw(pBones);
			bDrew = true;
		}
		ForceMaterial(nullptr);
		return bDrew;
	}

	// ---- config lists: cfg string (config.cpp STR) -> parsed list, re-parsed only on change ----
	struct CfgList
	{
		const char* pCfg;
		char seen[ChamsLayers::kCfgLen] = { '\x01' };   // never equal to any real cfg on first sync
		ChamsLayers::List list = {};
		const ChamsLayers::List& Get()
		{
			if (strcmp(seen, pCfg) != 0)
			{
				ChamsLayers::Parse(pCfg, list);
				strcpy_s(seen, pCfg);
			}
			return list;
		}
	};
	CfgList s_visible{ g_chamsVisibleCfg }, s_occluded{ g_chamsOccludedCfg }, s_backtrack{ g_chamsBacktrackCfg },
	        s_sound{ g_chamsSoundCfg }, s_teamVisible{ g_chamsTeamVisibleCfg }, s_teamOccluded{ g_chamsTeamOccludedCfg },
	        s_weapon{ g_chamsWeaponCfg }, s_arms{ g_chamsArmsCfg }, s_sleeve{ g_chamsSleeveCfg };

	// ---- sound chams stamps (botox sound_esp m_last_sound / m_origin) ----
	float s_soundMs[65] = {};
	float s_soundOrigin[65][3] = {};

	float SoundAlphaFor(int entIdx, const Vec3& vLocal)
	{
		if (entIdx <= 0 || entIdx > 64 || s_soundMs[entIdx] <= 0.f) return 0.f;
		if (g_chamsSoundMaxDistance > 0.f)
		{
			const float* o = s_soundOrigin[entIdx];
			const Vec3 vSound(o[0], o[1], o[2]);
			if ((vSound - vLocal).Length() > g_chamsSoundMaxDistance) return 0.f;
		}
		return ChamsLayers::SoundAlpha((EspFx_NowMs() - s_soundMs[entIdx]) / 1000.f, g_chamsSoundDuration, g_chamsSoundFade);
	}

	// ---- backtrack ghosts (botox draw_model_execute.cpp backtrack switch) ----
	TickRecord s_records[24];

	void DrawBacktrack(CTFPlayer* pPlayer, int entIdx, const ChamsLayers::List& l, const DrawCtx& ctx)
	{
		const int n = F::Backtrack.GetRecords(pPlayer, s_records, int(sizeof(s_records) / sizeof(s_records[0])));   // newest first
		if (n <= 0) return;

		const Vec3 vLive = pPlayer->m_vecOrigin();
		// Amalgam RenderBacktrack: a ghost standing on the live model fades out (1u..24u -> 0..1)
		auto Fade = [&](const TickRecord& r) { return std::clamp(((r.m_vOrigin - vLive).Length() - 1.f) / 23.f, 0.f, 1.f); };
		auto DrawRecord = [&](TickRecord& r, float flAlpha) { if (flAlpha > 0.f) DrawLayers(l, g_chamsBacktrackXqz, flAlpha, ctx, r.m_aBones); };

		switch (g_chamsBacktrackType)
		{
		case 0:
			DrawRecord(s_records[n - 1], Fade(s_records[n - 1]));
			break;
		case 1:
		{
			// botox: ghost cap thins, never shortens -- newest + oldest always, rest evenly spaced,
			// drawn oldest first so newer ghosts sit on top
			const int iMax = std::clamp(g_chamsBacktrackMaxGhosts, 2, 64);
			const int iDrawn = n < iMax ? n : iMax;
			for (int pick = iDrawn - 1; pick >= 0; --pick)
			{
				const int i = iDrawn > 1 ? int(float(pick) * float(n - 1) / float(iDrawn - 1) + 0.5f) : 0;
				const float flTick = g_chamsBacktrackGradient && n > 1 ? 1.f - float(i) / float(n - 1) : 1.f;
				DrawRecord(s_records[i], Fade(s_records[i]) * flTick);
			}
			break;
		}
		case 2:
			// botox draws the record the aimbot picked; this aimbot does not keep the picked record,
			// so it is the oldest in-window one, shown only while this player is the live aim target
			if (G::AimTarget.m_iEntIndex == entIdx && I::GlobalVars->tickcount - G::AimTarget.m_iTickCount <= G::AimTarget.m_iDuration)
				DrawRecord(s_records[n - 1], Fade(s_records[n - 1]));
			break;
		}
	}

	// ---- player glow colour helpers (botox "player glow" popup) ----
	//   accent wave = colour pulled toward the wave colour by a travelling sine (size = spatial scale)
	//   edge pulse  = alpha breathing between pulse min % and 100 %
	// The glow itself is the Amalgam stencil + render-target pass at the end of this file.
	void GlowColor(const float* base, float out[4], float flPhase)
	{
		for (int k = 0; k < 4; ++k) out[k] = base[k];
		if (!g_glowWave) return;
		const float t = EspFx_NowMs() / 1000.f;
		const float w = (0.5f + 0.5f * sinf(6.2831853f * (t * g_glowWaveSpeed + flPhase))) * std::clamp(g_glowWaveStrength, 0.f, 1.f);
		for (int k = 0; k < 3; ++k) out[k] += (g_glowWaveColor[k] - out[k]) * w;
	}

	float GlowPulse()
	{
		const bool bForced = g_glowLegacy && g_glowLegacyStyle == 3;
		if (!g_glowPulse && !bForced) return 1.f;
		const float flSpeed = g_glowPulse ? g_glowPulseSpeed : 1.f;
		const float flMin = g_glowPulse ? std::clamp(g_glowPulseMin / 100.f, 0.f, 1.f) : 0.f;
		const float s = 0.5f + 0.5f * sinf(6.2831853f * EspFx_NowMs() / 1000.f * flSpeed);
		return flMin + (1.f - flMin) * s;
	}

	// ---- entity classification ----
	bool IsWearableClass(ETFClassID id)
	{
		switch (id)
		{
		case ETFClassID::CTFWearable: case ETFClassID::CTFWearableItem: case ETFClassID::CTFWearableDemoShield:
		case ETFClassID::CTFWearableRazorback: case ETFClassID::CTFWearableRobotArm: case ETFClassID::CTFWearableLevelableItem:
		case ETFClassID::CTFWearableCampaignItem:
			return true;
		default:
			return false;
		}
	}

	// hats and the gun a player holds draw as their own entities; they take the owner's chams.
	// Owner must also be the move parent (attached): a rocket is owned by its shooter, and its
	// model sits under models/weapons/ too, but it flies free.
	CTFPlayer* OwnerPlayer(CBaseEntity* pEnt, const char* modelName)
	{
		const bool bWeaponModel = modelName && (strstr(modelName, "/weapons/") || strstr(modelName, "/c_models/"));
		if (!IsWearableClass(pEnt->GetClassID()) && !bWeaponModel)
			return nullptr;
		// c_baseentity.cpp: RecvPropInt(RECVINFO_NAME(m_hNetworkMoveParent, moveparent)) -- same handle encoding as m_hOwnerEntity
		static const int nMoveParentOff = U::NetVars.GetNetVar("CBaseEntity", "moveparent");
		if (nMoveParentOff <= 0)
			return nullptr;
		const unsigned uMoveParent = *reinterpret_cast<unsigned*>(reinterpret_cast<uintptr_t>(pEnt) + nMoveParentOff);
		const unsigned uOwner = *reinterpret_cast<unsigned*>(&pEnt->m_hOwnerEntity());
		if (uMoveParent != uOwner)
			return nullptr;
		auto pOwner = I::ClientEntityList->GetClientEntityFromHandle(pEnt->m_hOwnerEntity());
		if (!pOwner) return nullptr;
		auto pOwnerEnt = pOwner->As<CBaseEntity>();
		return pOwnerEnt->IsPlayer() ? pOwnerEnt->As<CTFPlayer>() : nullptr;
	}

	bool s_loggedDraw = false;
}

void Chams_ReleaseMaterials()
{
	// Amalgam CMaterials::Remove: one DecRef per IncRef, then let the engine drop it so the next
	// map's CreateMaterial with the same name is not a duplicate.
	for (int i = 0; i < ChamsLayers::Count; ++i)
	{
		for (int v = 0; v < 2; ++v)
		{
			Mat& m = s_mats[i][v];
			if (m.pMat && !(m.bShared && v == 1))
			{
				m.pMat->DecrementReferenceCount();
				if (!m.bShared)
					m.pMat->DeleteIfUnreferenced();
			}
			m = Mat{};
		}
	}
	s_built = false;
	s_buildIndex = 1;
	s_lastBuildFrame = -1;
	for (auto& t : s_soundMs) t = 0.f;
}

// esp.cpp's sound gate + sound arrows (botox g_sound_esp m_last_sound / m_origin): false = never heard
bool Chams_SoundStamp(int entindex, float& ms, float* origin)
{
	if (entindex <= 0 || entindex > 64 || s_soundMs[entindex] <= 0.f)
		return false;
	ms = s_soundMs[entindex];
	origin[0] = s_soundOrigin[entindex][0]; origin[1] = s_soundOrigin[entindex][1]; origin[2] = s_soundOrigin[entindex][2];
	return true;
}

void Chams_OnSound(int entindex, float volume, const float* origin)
{
	if (entindex <= 0 || entindex > 64 || !origin || volume < g_chamsSoundMinVolume)
		return;
	s_soundOrigin[entindex][0] = origin[0];
	s_soundOrigin[entindex][1] = origin[1];
	s_soundOrigin[entindex][2] = origin[2];
	s_soundMs[entindex] = EspFx_NowMs();
}

bool Chams_AnyActive()
{
	if (g_glowEnable) return true;
	if (g_chamsEnable && (s_visible.Get().used || s_occluded.Get().used || s_backtrack.Get().used || s_sound.Get().used)) return true;
	if (g_chamsTeam && (s_teamVisible.Get().used || s_teamOccluded.Get().used)) return true;
	if (g_chamsLocalEnable && (s_weapon.Get().used || s_arms.Get().used || s_sleeve.Get().used)) return true;
	return false;
}

bool Chams_DrawModel(ChamsDrawFn fnOriginal, void* rcx, const void* pState, const void* pInfo, const void* pBones,
                     int entIdx, const char* modelName, int vmKind)
{
	if (!Aim_Active() || !g_modelRender || !g_matSystem || !pInfo || !pBones || !I::RenderView)
		return false;
	if (*reinterpret_cast<const int*>(reinterpret_cast<const uint8_t*>(pInfo) + kInfoFlags) & kDepthPassFlags)
		return false;

	// someone else's override is live (TF2's own glow outline pass, a depth pass): leave it alone
	IMaterial* pOrigMat = nullptr; int iOrigType = 0;
	VCallAt<void>(g_modelRender, kMdlGetMaterialOverride, &pOrigMat, &iOrigType);
	if (pOrigMat)
		return false;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return false;

	const DrawCtx ctx{ fnOriginal, rcx, pState, pInfo, pBones };

	float aOrigColor[3] = { 1.f, 1.f, 1.f };
	I::RenderView->GetColorModulation(aOrigColor);
	const float flOrigBlend = I::RenderView->GetBlend();
	auto Restore = [&]()
	{
		ForceMaterial(pOrigMat, iOrigType);
		I::RenderView->SetColorModulation(aOrigColor);
		I::RenderView->SetBlend(flOrigBlend);
	};

	// ---- local: viewmodel weapon / gloves (arms) / sleeve (classic v_ viewmodels) ----
	if (vmKind)
	{
		if (!g_chamsLocalEnable)
			return false;
		const ChamsLayers::List& l = vmKind == 2 ? s_weapon.Get()
			: (modelName && strstr(modelName, "_arms.mdl")) ? s_arms.Get() : s_sleeve.Get();
		if (!l.used)
			return false;
		DrawLayers(l, false, flOrigBlend, ctx);   // keeps the transparent-viewmodel blend
		Restore();
		return true;
	}

	if (entIdx <= 0)
		return false;
	auto pClient = I::ClientEntityList->GetClientEntity(entIdx);
	if (!pClient)
		return false;
	auto pEnt = pClient->As<CBaseEntity>();

	CTFPlayer* pPlayer = nullptr;
	bool bAttachment = false, bRagdoll = false;
	int iTeam = 0;
	if (pEnt->IsPlayer())
	{
		pPlayer = pEnt->As<CTFPlayer>();
		iTeam = pPlayer->m_iTeamNum();
	}
	else if (pEnt->GetClassID() == ETFClassID::CTFRagdoll)
	{
		if (!g_chamsRagdolls)
			return false;
		static const int nTeamOff = U::NetVars.GetNetVar("CTFRagdoll", "m_iTeam");   // c_tf_player.cpp RecvPropInt(m_iTeam)
		if (nTeamOff <= 0)
			return false;
		iTeam = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pEnt) + nTeamOff);
		bRagdoll = true;
	}
	else if ((pPlayer = OwnerPlayer(pEnt, modelName)))
	{
		bAttachment = true;
		iTeam = pPlayer->m_iTeamNum();
	}
	else
		return false;

	if (pPlayer == pLocal)
		return false;
	const bool bEnemy = iTeam != pLocal->m_iTeamNum();
	if (bRagdoll && !bEnemy)
		return false;   // botox: enemy ragdolls only

	const bool bChams = bEnemy ? g_chamsEnable : g_chamsTeam;
	const ChamsLayers::List& lVisible  = bEnemy ? s_visible.Get()  : s_teamVisible.Get();
	const ChamsLayers::List& lOccluded = bEnemy ? s_occluded.Get() : s_teamOccluded.Get();
	const bool bLivePlayer = pPlayer && !bAttachment;
	const bool bBacktrack = bChams && bEnemy && bLivePlayer && s_backtrack.Get().used;
	const float flSound = bChams && bEnemy && bLivePlayer && s_sound.Get().used ? SoundAlphaFor(entIdx, pLocal->m_vecOrigin()) : 0.f;
	const bool bDrawVisible = bChams && lVisible.used;
	const bool bDrawOccluded = bChams && lOccluded.used;

	if (!bDrawVisible && !bDrawOccluded && !bBacktrack && flSound <= 0.f)
		return false;

	// botox order: sound, backtrack, occluded (all ignorez first), then the model itself
	if (flSound > 0.f)  DrawLayers(s_sound.Get(), true, flSound, ctx);
	if (bBacktrack)     DrawBacktrack(pPlayer, entIdx, s_backtrack.Get(), ctx);
	if (bDrawOccluded)  DrawLayers(lOccluded, true, 1.f, ctx);

	bool bDrewModel = false;
	if (bDrawVisible)
		bDrewModel = DrawLayers(lVisible, false, 1.f, ctx);
	if (!bDrewModel)
	{
		Restore();
		ctx.Draw();
	}

	Restore();

	if (!s_loggedDraw)
	{
		s_loggedDraw = true;
		DebugLog("chams: first chams draw (ent %d, %s, vis %d occ %d)", entIdx, bEnemy ? "enemy" : "team",
			lVisible.used, lOccluded.used);
	}
	return true;
}

// ==== player glow ====================================================================
// Amalgam Features/Visuals/Glow (CGlow::SetupBegin/Mid/End + RenderMain) on the slot recipe
// fpsoverlay vmglow.cpp proved in this TF2 build:
//   pass 1  every glowing model drawn invisible (blend 0, dev/glow_color) -> stencil 1 on its pixels
//   pass 2  same models drawn in the glow colour into rb1 (ignorez material: through walls)
//   blur    rb1 -> rb2 (BlurFilterX) -> rb1 (BlurFilterY, $bloomamount = blur)
//   halo    rb1 added onto the screen where stencil == 0: pixel-shifted copies (stencil width),
//           plus one straight copy when blurred
// IMatRenderContext slots (session_facts, screenfx.cpp:555): SetRenderTarget 6, GetRenderTarget 7,
// ClearBuffers 12, ClearColor4ub 73, DrawScreenSpaceRectangle 103, SetStencilEnable 117,
// Fail 118, ZFail 119, Pass 120, CompareFunction 121, ReferenceValue 122, TestMask 123, WriteMask 124.
// PushRenderTargetAndViewport (105) CRASHES this build (MSVC overload group) -> Get/SetRenderTarget;
// never Get/SetViewport (stale queued state). IMaterialSystem: GetBackBufferDimensions 31,
// Begin/EndRenderTargetAllocation 82/83, CreateNamedRenderTargetTextureEx 85. ITexture IncRef 10, DecRef 11.
// Menu mapping (botox popup): stencil 1 + blur 1 by default; "normal blending" makes thickness the
// blur amount (2..30 px -> 0.2..3); legacy "outline" = stencil 2, no blur; wave/pulse as before;
// gradient has no equivalent here and is ignored.
namespace
{
	constexpr int kRcSetRT = 6, kRcGetRT = 7, kRcClearBuffers = 12, kRcClearColor4ub = 73, kRcDrawSSRect = 103;
	constexpr int kRcStencilEnable = 117, kRcStencilFail = 118, kRcStencilZFail = 119, kRcStencilPass = 120,
	              kRcStencilCmp = 121, kRcStencilRef = 122, kRcStencilTestMask = 123, kRcStencilWriteMask = 124;
	constexpr int kMatSysBackBufferDims = 31, kMatSysBeginRTAlloc = 82, kMatSysEndRTAlloc = 83,
	              kMatSysCreateNamedRTEx = 85, kMatSysGetRenderContext = 98;
	constexpr int kTexIncRef = 10, kTexDecRef = 11;
	// public/materialsystem/imaterialsystem.h StencilComparisonFunction_t / StencilOperation_t
	constexpr int kStencilEqual = 3, kStencilAlways = 8, kOpKeep = 1, kOpReplace = 3;
	constexpr int kStudioRender = 0x1, kStudioNoShadows = 0x80;   // public/model_types.h

	void*         s_glowRB1 = nullptr, *s_glowRB2 = nullptr;      // ITexture*
	IMaterial*    s_glowMatColor = nullptr, *s_glowMatHalo = nullptr, *s_glowMatBlurX = nullptr, *s_glowMatBlurY = nullptr;
	ChamsMatVar*  s_glowBloom = nullptr;
	int           s_glowW = 0, s_glowH = 0;
	bool          s_glowReady = false;
	volatile bool s_glowRendering = false;
	bool          s_glowLoggedDraw = false;

	struct GlowItem { CBaseEntity* pEnt; float c[4]; };
	std::vector<GlowItem> s_glowItems;

	void GlowBackBuffer(int& w, int& h)
	{
		w = h = 0;
		if (g_matSystem) VCallAt<void>(g_matSystem, kMatSysBackBufferDims, &w, &h);
	}

	void GlowStencil(void* rc, bool bOn, int iCmp, int iRef, unsigned uTest, unsigned uWrite, int iFail, int iZFail, int iPass)
	{
		VCallAt<void>(rc, kRcStencilEnable, bOn);
		VCallAt<void>(rc, kRcStencilCmp, iCmp);
		VCallAt<void>(rc, kRcStencilRef, iRef);
		VCallAt<void>(rc, kRcStencilTestMask, uTest);
		VCallAt<void>(rc, kRcStencilWriteMask, uWrite);
		VCallAt<void>(rc, kRcStencilFail, iFail);
		VCallAt<void>(rc, kRcStencilZFail, iZFail);
		VCallAt<void>(rc, kRcStencilPass, iPass);
	}

	void GlowSSRect(void* rc, IMaterial* pMat, int dx, int dy, int w, int h)
	{
		using F = void(__fastcall*)(void*, void*, int, int, int, int, float, float, float, float, int, int, void*, int, int);
		((F)(*reinterpret_cast<void***>(rc))[kRcDrawSSRect])(rc, pMat, dx, dy, w, h, 0.f, 0.f, float(w - 1), float(h - 1), w, h, nullptr, 1, 1);
	}

	void GlowDrawAll(bool bColour)
	{
		s_glowRendering = true;
		for (auto& it : s_glowItems)
		{
			if (bColour)
			{
				I::RenderView->SetColorModulation(it.c);
				I::RenderView->SetBlend(it.c[3]);
			}
			if (it.pEnt->IsPlayer())
			{
				// Amalgam CGlow::DrawModel: a cloaked spy still gets a full silhouette
				auto pPlayer = it.pEnt->As<CTFPlayer>();
				const float flInvis = pPlayer->m_flInvisibility();
				pPlayer->m_flInvisibility() = 0.f;
				it.pEnt->DrawModel(kStudioRender | kStudioNoShadows);
				pPlayer->m_flInvisibility() = flInvis;
			}
			else
				it.pEnt->DrawModel(kStudioRender | kStudioNoShadows);
		}
		s_glowRendering = false;
	}

	// no C++ unwinding inside: __try (C2712)
	__declspec(noinline) void GlowPasses_SEH(void* rc, int w, int h, int iStencil, float flBlur)
	{
		__try
		{
			float aColor[3] = { 1.f, 1.f, 1.f };
			I::RenderView->GetColorModulation(aColor);
			const float flBlend = I::RenderView->GetBlend();
			IMaterial* pOrigMat = nullptr; int iOrigType = 0;
			VCallAt<void>(g_modelRender, kMdlGetMaterialOverride, &pOrigMat, &iOrigType);
			const float aWhite[3] = { 1.f, 1.f, 1.f };

			// SetupBegin: silhouette -> stencil 1, nothing visible (blend 0)
			ForceMaterial(s_glowMatColor);
			I::RenderView->SetBlend(0.f);
			I::RenderView->SetColorModulation(aWhite);
			GlowStencil(rc, true, kStencilAlways, 1, 0x0, 0xFF, kOpKeep, kOpReplace, kOpReplace);
			GlowDrawAll(false);

			// SetupMid: coloured models into rb1
			VCallAt<void>(rc, kRcStencilEnable, false);
			void* pSavedRT = VCallAt<void*>(rc, kRcGetRT);
			VCallAt<void>(rc, kRcSetRT, s_glowRB1);
			VCallAt<void>(rc, kRcClearColor4ub, (unsigned char)0, (unsigned char)0, (unsigned char)0, (unsigned char)0);
			VCallAt<void>(rc, kRcClearBuffers, true, false, false);
			GlowDrawAll(true);
			VCallAt<void>(rc, kRcSetRT, pSavedRT);

			// SetupEnd
			if (flBlur > 0.f && s_glowBloom)
			{
				s_glowBloom->SetFloatValue(flBlur);
				VCallAt<void>(rc, kRcSetRT, s_glowRB2);
				GlowSSRect(rc, s_glowMatBlurX, 0, 0, w, h);
				VCallAt<void>(rc, kRcSetRT, s_glowRB1);
				GlowSSRect(rc, s_glowMatBlurY, 0, 0, w, h);
				VCallAt<void>(rc, kRcSetRT, pSavedRT);
			}

			GlowStencil(rc, true, kStencilEqual, 0, 0xFF, 0x0, kOpKeep, kOpKeep, kOpKeep);
			I::RenderView->SetBlend(1.f);
			I::RenderView->SetColorModulation(aWhite);
			if (iStencil > 0)
			{
				const int iSide = (iStencil + 1) / 2, iCorner = iStencil / 2;
				GlowSSRect(rc, s_glowMatHalo, -iSide, 0, w, h);
				GlowSSRect(rc, s_glowMatHalo, 0, -iSide, w, h);
				GlowSSRect(rc, s_glowMatHalo, iSide, 0, w, h);
				GlowSSRect(rc, s_glowMatHalo, 0, iSide, w, h);
				if (iCorner)
				{
					GlowSSRect(rc, s_glowMatHalo, -iCorner, -iCorner, w, h);
					GlowSSRect(rc, s_glowMatHalo, iCorner, iCorner, w, h);
					GlowSSRect(rc, s_glowMatHalo, iCorner, -iCorner, w, h);
					GlowSSRect(rc, s_glowMatHalo, -iCorner, iCorner, w, h);
				}
			}
			if (flBlur > 0.f)
				GlowSSRect(rc, s_glowMatHalo, 0, 0, w, h);
			VCallAt<void>(rc, kRcStencilEnable, false);

			I::RenderView->SetColorModulation(aColor);
			I::RenderView->SetBlend(flBlend);
			ForceMaterial(pOrigMat, iOrigType);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			s_glowRendering = false;
			DebugLog("glow: exception in the glow pass, glow off until the next map");
			s_glowReady = false;
		}
	}

	void GlowRelease()
	{
		s_glowReady = false;
		auto Drop = [](IMaterial*& p) { if (p) { p->DecrementReferenceCount(); p->DeleteIfUnreferenced(); p = nullptr; } };
		Drop(s_glowMatHalo); Drop(s_glowMatBlurX); Drop(s_glowMatBlurY);
		if (s_glowMatColor) { s_glowMatColor->DecrementReferenceCount(); s_glowMatColor = nullptr; }   // engine's, never deleted
		s_glowBloom = nullptr;
		if (s_glowRB1) { VCallAt<void>(s_glowRB1, kTexDecRef); s_glowRB1 = nullptr; }
		if (s_glowRB2) { VCallAt<void>(s_glowRB2, kTexDecRef); s_glowRB2 = nullptr; }
	}
}

ForcedMaterialOverride_t o_ForcedMaterialOverride = nullptr;

void __fastcall h_ForcedMaterialOverride(void* rcx, void* pMat, int iType)
{
	if (s_glowRendering) return;   // Amalgam IVModelRender_ForcedMaterialOverride hook
	o_ForcedMaterialOverride(rcx, pMat, iType);
}

bool Glow_Rendering() { return s_glowRendering; }

void Glow_LevelUnload() {}

// CViewRender::LevelInit (flipworld.cpp hook): the one safe time to allocate named render targets.
// The RTs and materials live across maps (fpsoverlay vmglow); only a new backbuffer size remakes them.
void Glow_LevelInit()
{
	if (!g_matSystem) return;
	int w, h; GlowBackBuffer(w, h);
	if (w <= 0 || h <= 0) return;
	if (s_glowReady && w == s_glowW && h == s_glowH) return;
	GlowRelease();
	s_glowW = w; s_glowH = h;

	// flipworld.cpp's proven arguments: RT_SIZE_LITERAL 8, IMAGE_FORMAT_RGB888 2, shared depth 0,
	// CLAMPS|CLAMPT|EIGHTBITALPHA 0x200C, CREATERENDERTARGETFLAGS_HDR 1. One Begin/End block for both:
	// 85 outside a block calls End itself, and End is a full device release/reacquire.
	using CreateRT_t = void*(__fastcall*)(void*, const char*, int, int, int, int, int, unsigned, unsigned);
	auto fnCreate = (CreateRT_t)(*reinterpret_cast<void***>(g_matSystem))[kMatSysCreateNamedRTEx];
	VCallAt<void>(g_matSystem, kMatSysBeginRTAlloc);
	s_glowRB1 = fnCreate(g_matSystem, "botox_glow_rb1", w, h, 8, 2, 0, 0x200Cu, 1u);
	s_glowRB2 = fnCreate(g_matSystem, "botox_glow_rb2", w, h, 8, 2, 0, 0x200Cu, 1u);
	VCallAt<void>(g_matSystem, kMatSysEndRTAlloc);
	if (!s_glowRB1 || !s_glowRB2)
	{
		DebugLog("glow: render target creation FAILED (%p %p)", s_glowRB1, s_glowRB2);
		GlowRelease();
		return;
	}
	VCallAt<void>(s_glowRB1, kTexIncRef);
	VCallAt<void>(s_glowRB2, kTexIncRef);

	// Amalgam CGlow::Initialize materials, through CreateFromVMT (KeyValues -> CreateMaterial, one IncRef)
	s_glowMatColor = VCallAt<IMaterial*>(g_matSystem, kMatSysFindMaterial, "dev/glow_color", "Other textures", false, (const char*)nullptr);
	if (s_glowMatColor && !s_glowMatColor->IsErrorMaterial()) s_glowMatColor->IncrementReferenceCount();
	else s_glowMatColor = nullptr;
	s_glowMatHalo  = CreateFromVMT("botox_glow_halo",  "\"UnlitGeneric\"\n{\n\t\"$basetexture\" \"botox_glow_rb1\"\n\t\"$additive\" \"1\"\n}\n");
	s_glowMatBlurX = CreateFromVMT("botox_glow_blurx", "\"BlurFilterX\"\n{\n\t\"$basetexture\" \"botox_glow_rb1\"\n}\n");
	s_glowMatBlurY = CreateFromVMT("botox_glow_blury", "\"BlurFilterY\"\n{\n\t\"$basetexture\" \"botox_glow_rb2\"\n}\n");
	s_glowBloom = s_glowMatBlurY ? FindVar(s_glowMatBlurY, "$bloomamount") : nullptr;

	s_glowReady = s_glowMatColor && s_glowMatHalo && s_glowMatBlurX && s_glowMatBlurY && s_glowBloom;
	DebugLog("glow: level init %dx%d ok=%d rb1=%p rb2=%p color=%p halo=%p blurx=%p blury=%p bloom=%p",
		w, h, (int)s_glowReady, s_glowRB1, s_glowRB2, (void*)s_glowMatColor, (void*)s_glowMatHalo,
		(void*)s_glowMatBlurX, (void*)s_glowMatBlurY, (void*)s_glowBloom);
	if (!s_glowReady) GlowRelease();
}

// DoPostScreenSpaceEffects, before the original (Amalgam CClientModeShared_DoPostScreenSpaceEffects):
// the 3D scene is done, viewmodel and HUD are not drawn yet.
void Glow_Render()
{
	if (!g_glowEnable)
		return;
	if (!s_glowReady)
	{
		// injected mid-map: the RTs come with the next CViewRender::LevelInit (map change), like flipworld's
		static bool s_logged = false;
		if (!s_logged) { s_logged = true; DebugLog("glow: enabled but no render targets yet -- they are made on the next map load"); }
		return;
	}
	if (!Aim_Active() || !g_matSystem || !g_modelRender
		|| !I::RenderView || !I::ClientEntityList || !I::EngineClient || !I::ModelInfoClient)
		return;
	if (I::EngineClient->GetDXSupportLevel() < 90)   // Amalgam: BlurFilter shaders crash dx8
		return;
	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;
	int w, h; GlowBackBuffer(w, h);
	if (w <= 0 || h <= 0 || w != s_glowW || h != s_glowH)   // resized: Glow_LevelInit remakes next map
		return;
	void* rc = VCallAt<void*>(g_matSystem, kMatSysGetRenderContext);
	if (!rc)
		return;

	// Amalgam CGlow::Store: enemy players on screen, colour by line of sight (visible / invisible)
	s_glowItems.clear();
	int aItemOfPlayer[128];
	for (auto& i : aItemOfPlayer) i = -1;
	const float flPulse = GlowPulse();
	const Vec3 vEye = pLocal->GetEyePosition();
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer || pPlayer == pLocal || pPlayer->IsDormant() || !pPlayer->IsAlive() || !pPlayer->ShouldDraw())
			continue;
		if (!SDK::IsOnScreen(pPlayer, false))
			continue;
		const bool bVisible = SDK::VisPos(pLocal, pPlayer, vEye, pPlayer->GetCenter());
		const Vec3 vOrigin = pPlayer->GetAbsOrigin();
		GlowItem it{ pPlayer };
		GlowColor(bVisible ? g_glowVisColor : g_glowInvisColor, it.c, g_glowWaveSize * (vOrigin.x + vOrigin.y) / 512.f);
		it.c[3] *= flPulse;
		const int idx = pPlayer->entindex();
		if (idx > 0 && idx < 128) aItemOfPlayer[idx] = (int)s_glowItems.size();
		s_glowItems.push_back(it);
	}
	if (s_glowItems.empty())
		return;

	// hats + the held weapon draw as their own entities: same colour as their owner (OwnerPlayer)
	const int nHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int i = 1; i <= nHighest && i < 2048; ++i)
	{
		auto pClient = I::ClientEntityList->GetClientEntity(i);
		if (!pClient) continue;
		auto pEnt = pClient->As<CBaseEntity>();
		if (pEnt->IsDormant() || pEnt->IsPlayer()) continue;
		const char* sModel = nullptr;
		if (auto pModel = pEnt->GetModel()) sModel = I::ModelInfoClient->GetModelName(pModel);
		auto pOwner = OwnerPlayer(pEnt, sModel);
		if (!pOwner) continue;
		const int o = pOwner->entindex();
		if (o <= 0 || o >= 128 || aItemOfPlayer[o] < 0 || !pEnt->ShouldDraw()) continue;
		GlowItem it = s_glowItems[aItemOfPlayer[o]];
		it.pEnt = pEnt;
		s_glowItems.push_back(it);
	}

	int iStencil = 1; float flBlur = 1.f;
	if (g_glowLegacy) { if (g_glowLegacyStyle == 2) { iStencil = 2; flBlur = 0.f; } }
	else if (g_glowNormalBlend) flBlur = std::clamp(g_glowThickness / 10.f, 0.2f, 3.f);

	GlowPasses_SEH(rc, w, h, iStencil, flBlur);

	if (!s_glowLoggedDraw)
	{
		s_glowLoggedDraw = true;
		DebugLog("glow: first glow pass (%d models, stencil %d, blur %.2f)", (int)s_glowItems.size(), iStencil, flBlur);
	}
}
