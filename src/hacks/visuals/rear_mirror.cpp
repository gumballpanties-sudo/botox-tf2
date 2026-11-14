#include "../../globals/globals.h"
#include "../movement/prediction.h"   // g_globalVars
#include "../screen/screenfx.h"       // ScreenFx_FlushRenderContext / ScreenFx_QueueFunctor
#include "../../render/dpi.h"         // Dpi_PanelScale
#include "rear_mirror.h"

using namespace rmslot;

namespace
{
	constexpr const char* k_textureName = "botox_rear_mirror";
	constexpr DWORD k_drawnWindowMs = 250;
	constexpr int k_clearFlags = 0x1 | 0x2 | 0x20;   // VIEW_CLEAR_COLOR | DEPTH | STENCIL
	constexpr int k_whatToDraw = 0x4;                // RENDERVIEW_SUPPRESSMONITORRENDERING: no hud, no viewmodel, no monitors
	constexpr const char* k_viewSig = "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B 03 48 8D 54 24";

	using RenderView_t = void(__fastcall*)(void*, const void*, int, int);
	RenderView_t o_RenderView = nullptr;

	std::atomic<bool> s_drawing{ false };
	bool  s_failed  = false;
	int   s_frame   = -1;
	void* s_texture = nullptr;   // ITexture*
	void* s_context = nullptr;   // IMatRenderContext*

	std::atomic<IDirect3DTexture9*> s_shown{ nullptr };
	std::atomic<float> s_u{ 1.f }, s_v{ 1.f };
	std::atomic<int> s_width{ 0 }, s_height{ 0 };
	std::atomic<DWORD> s_drawnTick{ 0 };

	// esp_preview_tex.cpp path: no CTexture offsets, the bound target's container off the device
	struct CaptureFunctor { void** vfptr; UINT w, h; };
	int   __fastcall Fn_AddRef(void*)           { return 1; }
	int   __fastcall Fn_Release(void*)          { return 1; }
	void* __fastcall Fn_Dtor(void* p, unsigned) { return p; }
	void  __fastcall Fn_Call(void* p)
	{
		MiscHookGuard _g;
		const auto* f = static_cast<const CaptureFunctor*>(p);
		IDirect3DDevice9* dev = g_d3dDev;
		IDirect3DSurface9* surf = nullptr;
		if (!dev || FAILED(dev->GetRenderTarget(0, &surf)) || !surf)
			return;
		D3DSURFACE_DESC d{};
		IDirect3DTexture9* tex = nullptr;
		if (SUCCEEDED(surf->GetDesc(&d)) && d.Width == f->w && d.Height == f->h
			&& SUCCEEDED(surf->GetContainer(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&tex))) && tex)
		{
			s_shown = tex;
			tex->Release();
		}
		surf->Release();
	}
	void* s_vt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Call };
	CaptureFunctor s_capture = { s_vt, 0, 0 };

	// TF2: a freeze cam grab taken in our pass would freeze the mirror image on screen
	__declspec(noinline) bool FreezeFrameBusy(const uint8_t* view, const uint8_t* setup)
	{
		__try {
			const int eye = *reinterpret_cast<const int*>(setup + vsStereoEye);
			return (eye >= 0 && eye < 3 && view[vrTakeFreezeFrame + eye]) ||
			       g_globalVars->curtime < *reinterpret_cast<const float*>(view + vrFreezeFrameUntil);
		} __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
	}

	__declspec(noinline) bool CopySetup(uint8_t* out, const void* setup)
	{
		__try { memcpy(out, setup, viewSize); return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	bool Begin(void* view, const void* setup, uint8_t* out)
	{
		if (s_drawing.load(std::memory_order_relaxed) || s_failed || g_eject || !g_wdRearMirror || !g_globalVars || !g_matSystem)
			return false;

		const int frame = g_globalVars->framecount;
		if (frame == s_frame)
			return false;
		s_frame = frame;

		if (GetTickCount() - s_drawnTick.load(std::memory_order_relaxed) > k_drawnWindowMs)
			return false;

		if (!CopySetup(out, setup) || FreezeFrameBusy(static_cast<const uint8_t*>(view), out))
			return false;

		if (!s_texture) {
			// reinject: the target outlives the module
			s_texture = VCall<void*>(g_matSystem, matFindTexture, k_textureName, "RenderTargets", false, 0);
			if (!s_texture || VCall<bool>(s_texture, texIsError))
				s_texture = VCall<void*>(g_matSystem, matCreateNamedRTEx, k_textureName, 1, 1, 4 /* RT_SIZE_FULL_FRAME_BUFFER */,
				                         VCall<int>(g_matSystem, matBackBufferFormat), 1 /* MATERIAL_RT_DEPTH_SEPARATE */,
				                         0x4u | 0x8u /* clamp s t */, 1u /* CREATERENDERTARGETFLAGS_HDR */);
			if (!s_texture || VCall<bool>(s_texture, texIsError)) {
				s_texture = nullptr;
				s_failed  = true;
				DebugLog("mirror: render target failed, rear view mirror off");
				return false;
			}
		}

		const int targetW = VCall<int>(s_texture, texActualWidth);
		const int targetH = VCall<int>(s_texture, texActualHeight);
		if (targetW < 16 || targetH < 16)
			return false;

		const int w = std::clamp(s_width.load(std::memory_order_relaxed), 16, targetW);
		const int h = std::clamp(s_height.load(std::memory_order_relaxed), 16, targetH);

		auto I = [out](unsigned o) -> int& { return *reinterpret_cast<int*>(out + o); };
		auto F = [out](unsigned o) -> float& { return *reinterpret_cast<float*>(out + o); };
		I(vsX) = I(vsUnscaledX) = I(vsY) = I(vsUnscaledY) = 0;
		I(vsWidth) = I(vsUnscaledWidth) = w;
		I(vsHeight) = I(vsUnscaledHeight) = h;
		F(vsAspect) = float(w) / float(h);
		F(vsFov) = std::clamp(g_wdRearMirrorFov, 10.f, 170.f);
		const float yaw = std::remainder(F(vsAngles + 4) + 180.f, 360.f);
		F(vsAngles) = 0.f;
		F(vsAngles + 4) = yaw;
		F(vsAngles + 8) = 0.f;
		// no off centre / subrect, no bloom + auto exposure + motion blur (a second luminance sample would drag the main view's)
		out[vsOffCenter] = out[vsSubrect] = out[vsBloom] = 0;

		void* const context = VCall<void*>(g_matSystem, matGetRenderContext);
		if (!context)
			return false;

		VCall<void>(context, ctxPushRtAndViewport);
		VCall<void>(context, ctxSetRenderTarget, s_texture);

		s_capture.w = UINT(targetW);
		s_capture.h = UINT(targetH);
		s_context = context;
		s_u.store(float(w) / float(targetW), std::memory_order_relaxed);
		s_v.store(float(h) / float(targetH), std::memory_order_relaxed);
		s_drawing.store(true, std::memory_order_relaxed);
		return true;
	}

	void End()
	{
		s_drawing.store(false, std::memory_order_relaxed);
		if (s_context) {
			// our target is still bound and drawn: its d3d texture for ImGui
			ScreenFx_FlushRenderContext();
			if (!ScreenFx_QueueFunctor(&s_capture))
				Fn_Call(&s_capture);   // mat_queue_mode 0: this thread issues the d3d
			VCall<void>(s_context, ctxPopRtAndViewport);
			VCall<int>(s_context, ctxRelease);
			s_context = nullptr;
		}
	}

	// before the main view: w2s matrix, current view, glow + post state the main pass leaves stay its own
	void __fastcall h_RenderView(void* rcx, const void* setup, int clearFlags, int whatToDraw)
	{
		MiscHookGuard _g;
		if (g_eject || !setup)
			return o_RenderView(rcx, setup, clearFlags, whatToDraw);

		alignas(16) uint8_t mirror[viewSize];
		if (Begin(rcx, setup, mirror)) {
			o_RenderView(rcx, mirror, k_clearFlags, k_whatToDraw);
			End();
		}
		o_RenderView(rcx, setup, clearFlags, whatToDraw);
	}
}

void RearMirror_Install()
{
	HMODULE hClient = GetModuleHandleA("client.dll");
	void* view = nullptr;
	if (uint8_t* at = hClient ? PatternScan(hClient, k_viewSig) : nullptr) {
		__try { view = *reinterpret_cast<void**>(at + 7 + *reinterpret_cast<const int32_t*>(at + 3)); }
		__except (EXCEPTION_EXECUTE_HANDLER) { view = nullptr; }
	}
	if (reinterpret_cast<uintptr_t>(view) < 0x10000) {
		DebugLog("mirror: IViewRender not found, rear view mirror off");
		return;
	}
	InstallVTableHook(view, vrRenderView, "mirror: CViewRender::RenderView", (void*)h_RenderView, (void**)&o_RenderView);
}

bool RearMirror_Drawing() { return s_drawing.load(std::memory_order_relaxed); }

void RearMirror_DropTexture() { s_shown = nullptr; }

void RearMirror_Draw()
{
	if (!g_wdRearMirror || !IsInGame())
		return;

	const ImVec2 size(float(std::max(g_wdRearMirrorWidth, 16)), float(std::max(g_wdRearMirrorHeight, 16)));
	const float rounding = std::clamp(g_wdRearMirrorRounding, 0.f, std::min(size.x, size.y) * 0.5f);

	ImGui::SetNextWindowPos(ImVec2((ImGui::GetMainViewport()->Size.x - size.x) * 0.5f, 16.f), ImGuiCond_FirstUseEver);   // logical px (render/dpi.cpp)
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(16.f, 16.f));

	int flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
	            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
	if (!g_showMenu)
		flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

	if (ImGui::Begin("botox-rear-mirror", nullptr, flags)) {
		ImDrawList* const list = ImGui::GetWindowDrawList();
		const ImVec2 pos = ImGui::GetWindowPos();
		const ImVec2 end(pos.x + size.x, pos.y + size.y);

		const float scale = Dpi_PanelScale();
		s_width.store(int(size.x * scale + 0.5f), std::memory_order_relaxed);
		s_height.store(int(size.y * scale + 0.5f), std::memory_order_relaxed);
		s_drawnTick.store(GetTickCount(), std::memory_order_relaxed);

		if (IDirect3DTexture9* const texture = s_shown.load()) {
			const float u = s_u.load(std::memory_order_relaxed), v = s_v.load(std::memory_order_relaxed);
			const bool flip = g_wdRearMirrorFlip;

			// scene alpha = whatever the shaders wrote (depth to alpha): opaque colour, vertex alpha only
			list->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
				if (g_d3dDev) g_d3dDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
			}, nullptr);
			list->AddImageRounded((ImTextureID)texture, pos, end, ImVec2(flip ? u : 0.f, 0.f), ImVec2(flip ? 0.f : u, v), IM_COL32_WHITE, rounding);
			list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		} else
			list->AddRectFilled(pos, end, IM_COL32(0, 0, 0, 160), rounding);

		list->AddRect(pos, end, IM_COL32(0, 0, 0, 255), rounding);
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}
