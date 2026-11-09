// botox csgo hacks/visuals/players/esp_preview.cpp, TF2 port. See visuals/esp_preview.h.
//
// TF2 departures (Source 2013 has no CMergedMDL): the model is Aletherium ModelPreview's fake entity, a zeroed
// block run through the C_BaseFlex ctor and SetModelByIndex. Two hooks keep it alive out of a map (main menu):
// CModelInfoClient::GetModel hands our model_t* to SetModelByIndex / the draw, R_LightcacheGetDynamic spoofs a
// neutral light state. Both only act on the game thread, out of a map, inside our own calls. Aletherium's
// UpdateVisibility hook (keeps the ent out of the client leaf system) is a private vtable here instead:
// slot 91 = UpdateVisibility (SetModelByIndex calls [vt+0x2d8] after OnNewModel [vt+0x2f0]).
// Its lightcache hook wrote the light state through the 3rd arg (stats); the real args are
// (origin, LightingState_t&, stats, flags, bDebug) (engine call at 0xed546), fixed here.
// The held shotgun is a second fake ent built the same way; no bone merge without a move parent, so its
// renderable SetupBones is swapped for one that returns the soldier's bones by name (MergeGunBones).
#include "sdk.h"
#include "esp.h"
#include "backtrack.h"
#include "../visuals/esp_preview.h"
#include "../visuals/chams_api.h"
#include "../esp/esp_queue.h"
#include "../esp/esp_math.h"
#include "../../dependencies/imgui/imgui.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

void DebugLog(const char* fmt, ...);   // dllmain.cpp
extern void* g_matSystem;               // IMaterialSystem (thread.cpp)
extern void* g_modelRender;             // IVModelRender
extern IDirect3DDevice9* g_d3dDev;      // globals.h (not included here: drags dih's SDK in)

bool g_espPreviewDrawing = false;

namespace
{
	// ImGui's dx9 backend blends with texture alpha x vertex alpha. In a map the engine writes depth (or
	// whatever the pass left) into the target's alpha -- the depth-to-dest-alpha switch in Render() does not
	// cover every pass -- and the body came out see-through. Take alpha from the vertex alone for the image.
	void ImageAlphaOpaque(const ImDrawList*, const ImDrawCmd*)
	{
		if (!g_d3dDev) return;
		g_d3dDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		g_d3dDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	}
	void ImageAlphaRestore(const ImDrawList*, const ImDrawCmd*)
	{
		if (!g_d3dDev) return;   // imgui_impl_dx9.cpp SetupRenderState values
		g_d3dDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		g_d3dDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	}
}

namespace
{
	// ---- TF2 x64, disasm-checked 11-09 (client / engine / materialsystem, see the port notes) ----
	constexpr const char* kModel = "models/player/soldier.mdl";
	constexpr const char* kSequence = "stand_SECONDARY";   // shotgun idle (soldier.mdl has it, checked in the vpk)
	// stock shotgun world model: bones weapon_bone / weapon_bone_1 are soldier.mdl's own (merged by name),
	// vm_weapon_bone / vm_weapon_bone_1 / c_weapon_stattrack are not (placed off their parent's local pose)
	constexpr const char* kGunModel = "models/weapons/c_models/c_shotgun/c_shotgun.mdl";
	constexpr int   kGunMenuIndex = 2;                  // kMenuIndex's twin for the gun ent
	constexpr int   kRvtSlots = 64;                     // IClientRenderable vtable (~40 slots), copy past it
	constexpr int   kSlotSetupBones = 16;               // IClientRenderable::SetupBones (aim/defs.h)
	constexpr int   kBoneStride = 0xD8, kBoneParent = 0x4, kBonePos = 0x20, kBoneQuat = 0x2C;   // mstudiobone_t (esp.cpp)
	constexpr int   kSequenceIndex = -1;                // -1 = look the label up in the local sequences
	constexpr float kHullMins[3] = { -24.f, -24.f, 0.f }, kHullMaxs[3] = { 24.f, 24.f, 82.f };   // VEC_HULL
	constexpr float kRunSpeed = 240.f;                  // soldier max speed: ghosts trail like a target running sideways
	constexpr float kDefaultTick = 1.f / 66.f;
	constexpr float kFov = 40.f;
	constexpr int   kEntSize = 0x400 * 10;              // Aletherium: C_BaseFlex fits with room
	constexpr int   kVtSlots = 256;                     // C_BaseFlex primary vtable: 220 slots (css 219), copy past it
	constexpr int   kSlotUpdateVisibility = 91;
	constexpr int   kMenuIndex = 1;                     // out of a map the ent sits on index 1, the GetModel hook answers it
	constexpr int   kStudioRender = 0x1;

	// IMatRenderContext / IMaterialSystem / IStudioRender / IVModelRender slots (disasm, tf2 == css)
	// SetIntRenderingParameter 115: counted off Amalgam IMaterialSystem.h, the count lands on all 7 slots above it here
	constexpr int kRcRelease = 1, kRcBindCubemap = 5, kRcClearBuffers = 12, kRcAmbientCube = 16, kRcClearColor4ub = 73,
	              kRcSetIntRenderingParameter = 115, kRcSetToneMapScale = 153, kRcGetToneMapScale = 154, kRcLightingOrigin = 157;
	constexpr int kMatFindTexture = 79, kMatGetRenderContext = 98;
	constexpr int kStudioAmbientColors = 17, kStudioLocalLights = 18;   // CStudioRenderContext: 17 = Vector4D* (16 = Vector*, w forced 1)
	constexpr int kMdlForcedMaterialOverride = 1, kMdlSuppressEngineLighting = 22;
	constexpr int kParmWriteDepthToDestAlpha = 9;   // public/renderparm.h INT_RENDERPARM_WRITE_DEPTH_TO_DESTALPHA

	template <typename R, typename... A>
	R VCall(void* p, int idx, A... a) { return reinterpret_cast<R(__fastcall*)(void*, A...)>((*reinterpret_cast<void***>(p))[idx])(p, a...); }

	using Ctor_t = void(__fastcall*)(void*);
	using SetModelByIndex_t = void(__fastcall*)(void*, int);
	using SetAbsAngles_t = void(__fastcall*)(void*, const Vec3&);
	Ctor_t s_ctor = nullptr;
	SetModelByIndex_t s_setModelByIndex = nullptr;
	SetAbsAngles_t s_setAbsAngles = nullptr;

	void* s_studioRender = nullptr;
	CBaseEntity* s_ent = nullptr;
	void* s_vt[kVtSlots + 1] = {};
	const model_t* s_model = nullptr;
	void* s_texture = nullptr;   // ITexture*, aim/chams.cpp Glow_LevelInit
	void* s_cubemap = nullptr;
	int  s_sequence = 0;
	int  s_stage = 0;
	bool s_ready = false, s_failed = false;
	bool s_waitMap = false;   // injected mid-map: targets come with the next map load (Init stage 2)
	int  s_builtConnected = -1, s_builtIndex = -2;

	// our own calls on the game thread: the GetModel / lightcache hooks act only inside them
	std::atomic<DWORD> s_gameThread{ 0 };
	bool s_inCall = false;
	struct CallScope { CallScope() { s_inCall = true; s_gameThread = GetCurrentThreadId(); } ~CallScope() { s_inCall = false; } };
	bool InOurCall() { return s_inCall && GetCurrentThreadId() == s_gameThread.load(); }

	alignas(16) matrix3x4 s_bones[MAXSTUDIOBONES];
	int s_nBones = 0;

	// the held shotgun: a second fake ent, same build; its renderable SetupBones hands back s_gunBones
	// (soldier bones by name), which is how the engine's draw and the chams ghosts get its pose
	CBaseEntity* s_gun = nullptr;
	void* s_gunVt[kVtSlots + 1] = {};
	void* s_gunRvt[kRvtSlots + 1] = {};
	const model_t* s_gunModel = nullptr;
	int  s_gunBuiltConnected = -1, s_gunBuiltIndex = -2;
	alignas(16) matrix3x4 s_gunBones[MAXSTUDIOBONES];
	int  s_nGunBones = 0;
	int  s_gunMerge[MAXSTUDIOBONES];   // gun bone -> soldier bone, -1 = none
	const studiohdr_t* s_gunMergeHdr = nullptr;

	bool __fastcall GunSetupBones(void*, matrix3x4* pOut, int nMax, int, float)
	{
		if (pOut && s_nGunBones > 0)
			std::memcpy(pOut, s_gunBones, sizeof(matrix3x4) * std::min(s_nGunBones, nMax));
		return s_nGunBones > 0;
	}

	std::atomic<int>   s_frame{ -1 };
	int                s_seenFrame = -1;
	double             s_lastRender = -1.0;
	std::atomic<float> s_yaw{ 180.f };
	std::atomic<unsigned> s_clear{ 0xFF191919u };

	std::mutex s_mutex;
	EspFrame   s_espBuild, s_espShown;

	void __fastcall NoUpdateVisibility(void*) {}

	double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	template <typename F> bool Seh(F&& f) { __try { f(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
}

// ---- hooks (aim.cpp whitelist) ----------------------------------------------------------------

MAKE_HOOK(CModelInfoClient_GetModel,
	U::Memory.FindSignature("engine.dll", "83 FA ? 0F 8C ? ? ? ? 48 8D 0D ? ? ? ? E9 ? ? ? ? CC CC CC CC CC CC CC CC CC CC CC 48 89 5C 24"),
	const model_t*, void* rcx, int iIndex)
{
	if ((iIndex == kMenuIndex || iIndex == kGunMenuIndex) && InOurCall() && !I::EngineClient->IsConnected())
	{
		const model_t* p = iIndex == kMenuIndex ? s_model : s_gunModel;
		if (p) return p;
	}
	return CALL_ORIGINAL(rcx, iIndex);
}

namespace
{
	struct PreviewLightingState { Vec3 boxcolor[6]; int numlights; void* locallight[4]; };   // LightingState_t x64
}

MAKE_HOOK(R_LightcacheGetDynamic,
	U::Memory.FindSignature("engine.dll", "48 89 5C 24 ? 44 89 4C 24 ? 4C 89 44 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 48 8B 05"),
	void*, const Vec3* pOrigin, PreviewLightingState* pState, void* pStats, unsigned int uFlags, bool bDebug)
{
	if (!InOurCall() || I::EngineClient->IsInGame())
		return CALL_ORIGINAL(pOrigin, pState, pStats, uFlags, bDebug);
	if (pState)
	{
		for (auto& c : pState->boxcolor) c = Vec3(0.5f, 0.5f, 0.5f);
		pState->numlights = 0;
		for (auto& l : pState->locallight) l = nullptr;
	}
	return nullptr;
}

// ---- record stream ----------------------------------------------------------------------------

EspPreview::Stream EspPreview::GhostStream()
{
	const float interval = I::GlobalVars ? I::GlobalVars->interval_per_tick : 0.f;
	const float tick = I::EngineClient && I::EngineClient->IsConnected() && interval > 0.f ? interval : kDefaultTick;
	const float window = F::Backtrack.TimeLimit();
	return { std::clamp(int(window / tick + 0.5f), 2, CBacktrack::kMaxRecords), kRunSpeed * tick };
}

bool EspPreview::Drawing() { return g_espPreviewDrawing; }

namespace
{
	int FindSequence(const studiohdr_t* pHdr)
	{
		if (kSequenceIndex >= 0) return kSequenceIndex;
		for (int i = 0; pHdr && i < pHdr->numlocalseq; i++)
			if (auto pSeq = pHdr->pLocalSeqdesc(i); pSeq && !_stricmp(reinterpret_cast<const char*>(pSeq) + pSeq->szlabelindex, kSequence))
				return i;
		return 0;
	}

	bool Fail(const char* text)
	{
		DebugLog("[preview] %s", text);
		s_failed = true;
		return false;
	}

	// model_t* + the ent's model index for the current state: in game the precache index, out of a map our model
	// on menuIndex (the GetModel hook answers it). 0 = not usable now, 1 = as built, 2 = just (re)built, -1 = faulted
	int BuildModel(CBaseEntity* ent, const char* path, int menuIndex, const model_t*& model, int& builtConnected, int& builtIndex)
	{
		const bool connected = I::EngineClient->IsConnected();
		const int index = connected ? I::ModelInfoClient->GetModelIndex(path) : -1;
		if (builtConnected >= 0 && connected == (builtConnected == 1) && (!connected || index == builtIndex))   // -1 = never built
			return !connected || index >= 0;
		if (connected && index < 0)
			return 0;   // mid level load (or, for the gun, not precached on this server): keep the last image
		CallScope _c;
		model = I::ModelInfoClient->FindOrLoadModel(path);
		if (!model) return 0;
		if (!Seh([&]() { s_setModelByIndex(ent, index >= 0 ? index : menuIndex); }))
			return -1;
		builtConnected = connected ? 1 : 0;
		builtIndex = index;
		return 2;
	}

	bool SetModel()
	{
		const int r = BuildModel(s_ent, kModel, kMenuIndex, s_model, s_builtConnected, s_builtIndex);
		if (r < 0)
			return Fail("SetModelByIndex faulted, preview off");
		if (r == 2)
			s_sequence = FindSequence(I::ModelInfoClient->GetStudiomodel(s_model));
		return r > 0;
	}

	// the gun is extra: any trouble drops it, never the preview
	bool SetGunModel()
	{
		if (!s_gun) return false;
		const int r = BuildModel(s_gun, kGunModel, kGunMenuIndex, s_gunModel, s_gunBuiltConnected, s_gunBuiltIndex);
		if (r < 0)
		{
			DebugLog("[preview] gun SetModelByIndex faulted, gun off");
			s_gun = nullptr;   // leaked on purpose, same as s_ent
		}
		return r > 0;
	}

	// fake C_BaseFlex: zeroed block through the ctor, private primary vtable (UpdateVisibility would put the
	// ent in the client leaf system, drawn in the world)
	CBaseEntity* MakeEnt(void** vtOut)
	{
		auto ent = static_cast<CBaseEntity*>(std::calloc(1, kEntSize));
		if (!ent) return nullptr;
		CallScope _c;
		if (!Seh([&]() { s_ctor(ent); }))
			return nullptr;   // leaked on purpose: its state is unknown
		void** vt = *reinterpret_cast<void***>(ent);
		std::memcpy(vtOut, vt - 1, sizeof(void*) * (kVtSlots + 1));
		vtOut[1 + kSlotUpdateVisibility] = reinterpret_cast<void*>(&NoUpdateVisibility);
		*reinterpret_cast<void***>(ent) = vtOut + 1;
		return ent;
	}

	// gun bones = soldier bones by name; the rest off their parent's local pose (QuaternionMatrix + pos)
	void MergeGunBones(const studiohdr_t* pBody, float yaw)
	{
		s_nGunBones = 0;
		const studiohdr_t* pGun = s_gunModel ? I::ModelInfoClient->GetStudiomodel(s_gunModel) : nullptr;
		if (!pGun || !pBody || s_nBones <= 0) return;
		const int n = std::min(pGun->numbones, int(MAXSTUDIOBONES));
		auto Bone = [](const studiohdr_t* h, int i) { return reinterpret_cast<const uint8_t*>(h) + h->boneindex + i * kBoneStride; };
		auto Name = [&](const studiohdr_t* h, int i) { const uint8_t* b = Bone(h, i); return reinterpret_cast<const char*>(b + *reinterpret_cast<const int*>(b)); };
		if (s_gunMergeHdr != pGun)
		{
			for (int i = 0; i < n; i++)
			{
				s_gunMerge[i] = -1;
				for (int j = 0; j < s_nBones; j++)
					if (!_stricmp(Name(pGun, i), Name(pBody, j))) { s_gunMerge[i] = j; break; }
			}
			s_gunMergeHdr = pGun;
		}
		matrix3x4 root;
		Math::AngleMatrix(Vec3(0.f, yaw, 0.f), root);
		for (int i = 0; i < n; i++)
		{
			if (s_gunMerge[i] >= 0)
			{
				std::memcpy(s_gunBones[i], s_bones[s_gunMerge[i]], sizeof(matrix3x4));
				continue;
			}
			const uint8_t* b = Bone(pGun, i);
			const int parent = *reinterpret_cast<const int*>(b + kBoneParent);
			const float* p = reinterpret_cast<const float*>(b + kBonePos);
			const float* q = reinterpret_cast<const float*>(b + kBoneQuat);   // x y z w
			matrix3x4 local = {
				{ 1.f - 2.f * (q[1] * q[1] + q[2] * q[2]), 2.f * (q[0] * q[1] - q[3] * q[2]), 2.f * (q[0] * q[2] + q[3] * q[1]), p[0] },
				{ 2.f * (q[0] * q[1] + q[3] * q[2]), 1.f - 2.f * (q[0] * q[0] + q[2] * q[2]), 2.f * (q[1] * q[2] - q[3] * q[0]), p[1] },
				{ 2.f * (q[0] * q[2] - q[3] * q[1]), 2.f * (q[1] * q[2] + q[3] * q[0]), 1.f - 2.f * (q[0] * q[0] + q[1] * q[1]), p[2] } };
			Math::ConcatTransforms(parent >= 0 && parent < i ? s_gunBones[parent] : root, local, s_gunBones[i]);
		}
		s_nGunBones = n;
	}

	// one step per paint: sigs, interfaces, targets, model load, ent, done
	bool Init()
	{
		if (s_ready) return true;
		if (s_failed) return false;
		switch (s_stage)
		{
		case 0:
			s_ctor = reinterpret_cast<Ctor_t>(U::Memory.FindSignature("client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 33 ED"));
			s_setModelByIndex = reinterpret_cast<SetModelByIndex_t>(U::Memory.FindSignature("client.dll", "48 89 5C 24 ? 57 48 83 EC ? 66 89 91"));
			break;
		case 1:
			s_setAbsAngles = reinterpret_cast<SetAbsAngles_t>(U::Memory.FindSignature("client.dll", "48 89 5C 24 ? 57 48 81 EC ? ? ? ? 48 8B FA 48 8B D9 E8 ? ? ? ? F3 0F 10 83"));
			if (auto fn = reinterpret_cast<void* (*)(const char*, int*)>(GetProcAddress(GetModuleHandleA("studiorender.dll"), "CreateInterface")))
				s_studioRender = fn("VStudioRender025", nullptr);
			if (!s_ctor || !s_setModelByIndex || !s_setAbsAngles || !s_studioRender || !g_matSystem || !g_modelRender)
				return Fail("signatures / interfaces missing, preview off");
			break;
		case 2:
			if (!Glow_PreviewTarget())
			{
				// injected, no map load made them yet. Out of a map only: the main menu renders on this
				// thread; in a map the queued render thread is live, so wait for the next map's LevelInit
				// (flipworld.cpp, same rule as its own RT)
				s_waitMap = I::EngineClient->IsConnected();
				if (s_waitMap)
					return false;
				Glow_PreviewInit();
			}
			s_waitMap = false;
			s_texture = Glow_PreviewTarget();
			if (!s_texture)
				return Fail("render target missing, preview off");
			s_cubemap = VCall<void*>(g_matSystem, kMatFindTexture, "editor/cubemap.hdr", "CubeMap textures", false, 0);
			break;
		case 3:
		{
			CallScope _c;   // synchronous vpk read
			s_model = I::ModelInfoClient->FindOrLoadModel(kModel);
			if (!s_model)
				return Fail("soldier.mdl missing, preview off");
			s_gunModel = I::ModelInfoClient->FindOrLoadModel(kGunModel);
			if (!s_gunModel)
				DebugLog("[preview] c_shotgun.mdl missing, no gun");
			break;
		}
		case 4:
		{
			s_ent = MakeEnt(s_vt);
			if (!s_ent)
				return Fail("C_BaseFlex alloc / ctor faulted, preview off");
			if (s_gunModel && (s_gun = MakeEnt(s_gunVt)))
			{
				// renderable vtable (+8) too: SetupBones hands back the merged pose, the engine's draw reads it
				IClientRenderable* pRend = s_gun;
				void** rvt = *reinterpret_cast<void***>(pRend);
				std::memcpy(s_gunRvt, rvt - 1, sizeof(s_gunRvt));
				s_gunRvt[1 + kSlotSetupBones] = reinterpret_cast<void*>(&GunSetupBones);
				*reinterpret_cast<void***>(pRend) = s_gunRvt + 1;
			}
			else if (s_gunModel)
				DebugLog("[preview] gun ent faulted, no gun");
			break;
		}
		default:
			if (!SetModel())
				return s_failed ? false : (s_ready = false);
			s_ready = true;
			DebugLog("[preview] ready (sequence %d, gun %d)", s_sequence, s_gun ? 1 : 0);
			return true;
		}
		s_stage++;
		return false;
	}

	void SetStencilOff(void* rc) { VCall<void>(rc, 117, false); }

	void RenderEsp(const VMatrix& worldToProjection, const studiohdr_t* pHdr);

	void Render()
	{
		if (!SetModel())
			return;
		const bool gunModel = SetGunModel();   // own CallScope: never inside the one below (its dtor ends ours)

		const float yaw = s_yaw.load();
		auto pAnim = s_ent->As<CBaseAnimating>();
		static const int nSkin = U::NetVars.GetNetVar("CBaseAnimating", "m_nSkin");
		static const int nAnimTime = U::NetVars.GetNetVar("CBaseEntity", "m_flAnimTime");
		static double s_cycleClock = Now();
		const double now = Now();
		const float dt = std::clamp(float(now - s_cycleClock), 0.f, 0.1f);
		s_cycleClock = now;

		const studiohdr_t* pHdr = I::ModelInfoClient->GetStudiomodel(s_model);
		if (!pHdr) return;

		// camera: fitted off the engine's projection scale, model 68 % of the height, centred, never moves
		const float height = kHullMaxs[2] - kHullMins[2];
		CViewSetup view{};
		const int w = int(EspPreview::kWidth * EspPreview::kRtScale), h = int(EspPreview::kHeight * EspPreview::kRtScale);
		view.width = view.m_nUnscaledWidth = w;
		view.height = view.m_nUnscaledHeight = h;
		view.fov = kFov;
		view.zNear = 7.f;
		view.zFar = 1000.f;
		view.m_flAspectRatio = float(w) / float(h);
		VMatrix worldToView, viewToProjection, worldToProjection, worldToPixels;
		I::RenderView->GetMatricesForView(view, &worldToView, &viewToProjection, &worldToProjection, &worldToPixels);
		const float distance = height * 0.5f / 0.68f * fabsf(viewToProjection.m[1][1]);
		view.origin = Vec3(-distance, 0.f, height * 0.528f);   // csgo 38 of 72
		I::RenderView->GetMatricesForView(view, &worldToView, &viewToProjection, &worldToProjection, &worldToPixels);

		void* rc = VCall<void*>(g_matSystem, kMatGetRenderContext);
		if (!rc) return;

		bool drawn = false;
		{
			CallScope _c;
			drawn = Seh([&]() {
				*reinterpret_cast<int*>(uintptr_t(s_ent) + nSkin) = 0;   // red
				pAnim->m_nSequence() = s_sequence;
				pAnim->m_flCycle() = fmodf(pAnim->m_flCycle() + dt * 0.5f, 1.f);
				*reinterpret_cast<float*>(uintptr_t(s_ent) + nAnimTime) = I::GlobalVars->curtime;
				s_setAbsAngles(s_ent, Vec3(0.f, yaw, 0.f));
				s_ent->InvalidateBoneCache();   // the bone counter only ticks in a map
				s_nBones = std::min(pHdr->numbones, int(MAXSTUDIOBONES));
				if (!s_ent->SetupBones(s_bones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
					s_nBones = 0;
			});
			const bool gun = drawn && gunModel && Seh([&]() {
				*reinterpret_cast<int*>(uintptr_t(s_gun) + nSkin) = 0;   // red
				s_setAbsAngles(s_gun, Vec3(0.f, yaw, 0.f));
				MergeGunBones(pHdr, yaw);
			}) && s_nGunBones > 0;

			Frustum frustum;
			if (drawn)
			{
				if (s_cubemap) VCall<void>(rc, kRcBindCubemap, s_cubemap);
				const Vec3 lightOrigin(0.f, 0.f, 40.f);
				VCall<void>(rc, kRcLightingOrigin, &lightOrigin);
				I::RenderView->Push3DView(view, 0x1 | 0x2 | 0x20 /* clear color, depth, stencil */, reinterpret_cast<ITexture*>(s_texture), frustum);
				// in a map the main view leaves depth-to-dest-alpha on (viewrender.cpp:890): the model's alpha
				// came out as its depth and ImGui blended it see-through. Valve's own model panels turn it off the
				// same way (modelimagepanel.cpp:140) and leave it: every view sets it again
				VCall<void>(rc, kRcSetIntRenderingParameter, kParmWriteDepthToDestAlpha, 0);

				// in a map the auto exposure scales every shader output (white chams read grey): neutral here
				Vec3 toneMap(1.f, 1.f, 1.f);
				VCall<void>(rc, kRcGetToneMapScale, &toneMap);
				const Vec3 neutral(1.f, 1.f, 1.f);
				VCall<void>(rc, kRcSetToneMapScale, &neutral);

				const unsigned clear = s_clear.load();
				VCall<void>(rc, kRcClearColor4ub, (unsigned char)(clear & 0xFF), (unsigned char)((clear >> 8) & 0xFF), (unsigned char)((clear >> 16) & 0xFF), (unsigned char)255);
				VCall<void>(rc, kRcClearBuffers, true, true, true);

				Vector4D cube[6]; for (auto& c : cube) c = { 0.4f, 0.4f, 0.4f, 1.f };
				VCall<void>(rc, kRcAmbientCube, cube);
				VCall<void>(s_studioRender, kStudioAmbientColors, (const Vector4D*)cube);
				VCall<void>(s_studioRender, kStudioLocalLights, 0, (const void*)nullptr);
				VCall<void>(g_modelRender, kMdlSuppressEngineLighting, true);
				I::RenderView->SetColorModulation(1.f, 1.f, 1.f);
				I::RenderView->SetBlend(1.f);

				// live passes + ghosts: aim/chams.cpp preview branch (DrawModel -> h_DrawModelExecute)
				g_espPreviewDrawing = true;
				drawn = Seh([&]() { s_ent->DrawModel(kStudioRender); });
				if (drawn && gun && !Seh([&]() { s_gun->DrawModel(kStudioRender); }))
				{
					DebugLog("[preview] gun draw faulted, gun off");
					s_gun = nullptr;   // leaked on purpose, same as s_ent
				}
				// outline / glow / inner glow: the live pass on this target
				if (drawn)
					drawn = Seh([&]() { Glow_RenderPreview(s_ent, gun ? s_gun : nullptr, worldToProjection.m, s_bones, s_nBones, pHdr); });
				if (drawn)
					EspPreview::QueueTextureCapture();   // the target is bound: its d3d texture for ImGui
				g_espPreviewDrawing = false;

				SetStencilOff(rc);
				VCall<void>(g_modelRender, kMdlSuppressEngineLighting, false);
				VCall<void>(g_modelRender, kMdlForcedMaterialOverride, (void*)nullptr, 0);
				I::RenderView->SetColorModulation(1.f, 1.f, 1.f);
				I::RenderView->SetBlend(1.f);
				VCall<void>(rc, kRcSetToneMapScale, &toneMap);
				I::RenderView->PopView(frustum);
				if (s_cubemap) VCall<void>(rc, kRcBindCubemap, (void*)nullptr);
			}
		}
		VCall<int>(rc, kRcRelease);

		if (!drawn)
		{
			Fail("model draw faulted, preview off");
			s_ready = false;
			s_ent = nullptr;   // leaked on purpose: its state is unknown
			EspPreview::DropTexture();
			return;
		}

		RenderEsp(worldToProjection, pHdr);
	}
}

void EspPreview::Paint()
{
	const int frame = s_frame.load();
	const bool wanted = frame != s_seenFrame;
	s_seenFrame = frame;
	if (!wanted || G::Unload || !g_espPreview || !I::EngineClient || !I::RenderView || !I::ModelInfoClient)
		return;

	// the target keeps the last frame: 60 renders a second whatever the game fps
	const double now = Now();
	if (now >= s_lastRender && now - s_lastRender < 1.0 / 60.0)
		return;
	s_lastRender = now;

	if (Init())
		Render();
}

void EspPreview::OnEject()
{
	// the ent is leaked, never freed: nothing may hold it, but a free under a late engine read is a crash
	EspPreview::DropTexture();
	s_ready = false;
	s_failed = true;
}

// ---- 2d esp -----------------------------------------------------------------------------------

namespace
{
	uint64_t LocalSteamId()
	{
		HMODULE h = GetModuleHandleA("steam_api64.dll");
		auto user = h ? reinterpret_cast<void* (*)()>(GetProcAddress(h, "SteamAPI_SteamUser_v023")) : nullptr;
		auto id = h ? reinterpret_cast<uint64_t(*)(void*)>(GetProcAddress(h, "SteamAPI_ISteamUser_GetSteamID")) : nullptr;
		void* p = user ? user() : nullptr;
		return p && id ? id(p) : 0;
	}

	// same DrawPlayer as the live loop, projected through the preview camera onto the kWidth x kHeight body
	void RenderEsp(const VMatrix& worldToProjection, const studiohdr_t* pHdr)
	{
		s_espBuild.Clear();
		[&]() {
			if (!g_espEnable || !s_nBones)
				return;
			// live gates: distance cut + fade on a target 600 u out
			constexpr float kDistance = 600.f;
			const float a = EspMath::DistanceAlpha(kDistance, g_espMaxDistance, g_espDistanceFade);
			if (a <= 0.f)
				return;

			EspPlayer p;
			p.m_alpha = std::min(a, 1.f);
			p.m_mins = Vec3(kHullMins[0], kHullMins[1], kHullMins[2]);
			p.m_maxs = Vec3(kHullMaxs[0], kHullMaxs[1], kHullMaxs[2]);
			p.m_viewYaw = 0.f;   // camera looks down +x
			p.m_name = "botox";
			p.m_health = 76;
			p.m_healthFactor = 0.76f;
			p.m_visible = true;
			p.m_distance = kDistance;
			p.m_steamId = g_espAvatar ? LocalSteamId() : 0;   // TF2 bots have no portrait: your own avatar
			p.m_weaponId = TF_WEAPON_SHOTGUN_SOLDIER;   // the held gun
			p.m_hasAmmo = g_espAmmoBar;
			p.m_ammoFactor = 4.f / 6.f;   // shotgun clip
			if (g_espFlags)
				for (bool& f : p.m_flags) f = true;   // the flag list picks which ones show
			p.m_studio = pHdr;
			p.m_bones = s_bones;
			auto pSet = pHdr->pHitboxSet(0);
			auto pHead = pSet && pSet->numhitboxes > 0 ? pSet->pHitbox(0) : nullptr;   // TF2 hitbox 0 = head
			if (p.m_steamId && g_espAvatarPosition == 3 && pHead && pHead->bone >= 0 && pHead->bone < s_nBones)
			{
				const Vec3 e = pHead->bbmax - pHead->bbmin;
				p.m_headRadius = std::max(std::max(e.x, e.y), e.z) * 0.5f;
				Math::VectorTransform((pHead->bbmin + pHead->bbmax) * 0.5f, s_bones[pHead->bone], p.m_head);
				p.m_viewRight = Vec3(0.f, -1.f, 0.f);
				p.m_hasHead = true;
			}

			// W2S + the esp queue point at the preview for this one call (game thread, same as the live esp)
			const VMatrix savedMatrix = H::Draw.m_mWorldToProjection;
			const int savedW = H::Draw.m_nScreenW, savedH = H::Draw.m_nScreenH;
			H::Draw.m_mWorldToProjection = worldToProjection;
			H::Draw.m_nScreenW = int(EspPreview::kWidth);
			H::Draw.m_nScreenH = int(EspPreview::kHeight);
			std::swap(g_espQBuild.cmds, s_espBuild.cmds);
			std::swap(g_espQBuild.pts, s_espBuild.pts);
			std::swap(g_espQBuild.text, s_espBuild.text);
			F::Esp.DrawPlayer(p);
			std::swap(g_espQBuild.cmds, s_espBuild.cmds);
			std::swap(g_espQBuild.pts, s_espBuild.pts);
			std::swap(g_espQBuild.text, s_espBuild.text);
			H::Draw.m_mWorldToProjection = savedMatrix;
			H::Draw.m_nScreenW = savedW;
			H::Draw.m_nScreenH = savedH;
		}();

		std::lock_guard<std::mutex> lock(s_mutex);
		std::swap(s_espShown, s_espBuild);
	}
}

void EspPreview::DrawBody()
{
	s_frame.store(ImGui::GetFrameCount());

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 end(pos.x + kWidth, pos.y + kHeight);

	ImGui::InvisibleButton("##esp preview body", ImVec2(kWidth, kHeight));
	if (ImGui::IsItemActive())
		s_yaw.store(s_yaw.load() + ImGui::GetIO().MouseDelta.x * 0.5f);

	const ImU32 background = ImGui::GetColorU32(ImGuiCol_ChildBg) | IM_COL32_A_MASK;
	s_clear.store(background);

	// group box look (RenderChildBorder): child bg + menu outline, window rounding - 2
	const float rounding = std::max(ImGui::GetStyle().WindowRounding - 2.f, 0.f);
	dl->AddRectFilled(pos, end, background, rounding);

	if (void* tex = EspPreview::Texture())
	{
		dl->AddCallback(ImageAlphaOpaque, nullptr);
		dl->AddImageRounded((ImTextureID)tex, pos, end, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32_WHITE, rounding);
		dl->AddCallback(ImageAlphaRestore, nullptr);
	}
	else
	{
		const char* text = s_failed ? "preview unavailable" : s_waitMap ? "reload the map once" : "loading";
		const ImVec2 size = ImGui::CalcTextSize(text);
		dl->AddText(ImVec2(pos.x + (kWidth - size.x) * 0.5f, pos.y + (kHeight - size.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
	}

	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (!s_espShown.cmds.empty())
		{
			// esp is laid out on 0..kWidth: emit unclipped (text culls on the clip rect), then move it onto the body and clip there
			dl->PushClipRect(ImVec2(-100000.f, -100000.f), ImVec2(100000.f, 100000.f), false);
			const int firstVertex = dl->VtxBuffer.Size, firstCommand = dl->CmdBuffer.Size - 1;
			EspQ_ReplayFrame(dl, s_espShown);
			for (int i = firstVertex; i < dl->VtxBuffer.Size; i++)
				dl->VtxBuffer[i].pos.x += pos.x, dl->VtxBuffer[i].pos.y += pos.y;
			for (int i = firstCommand; i < dl->CmdBuffer.Size; i++)
				dl->CmdBuffer[i].ClipRect = ImVec4(pos.x, pos.y, end.x, end.y);
			dl->PopClipRect();
		}
	}

	// last: esp never paints over the edge
	dl->AddRect(pos, end, ImGui::GetColorU32(ImGuiCol_MenuOutline), rounding);
}
