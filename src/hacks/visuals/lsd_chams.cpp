// botox hacks/visuals/lsd_chams.cpp + model_shader.h (commit 95ef94a), TF2 port.
//
// The "lsd" chams layer draws a flat white UnlitGeneric carrier (aim/chams.cpp). While armed, the
// DrawIndexedPrimitive detour below swaps the carrier's shaders for the skinned-position vs +
// "70s Melt" ps (shaders/fx_lsd_*.hlsl, fxc at build time: no d3dx9 at runtime).
//
// TF2 departures from botox:
//   - shader constants: TF2's studio vs layout is CS:GO's (sdk2013 common_vs_fxc.h:83 cViewProj c8,
//     :169 cModel[53] c58; compressed normals UBYTE4, imaterial.h:258), so the vs is botox's verbatim
//   - arm/disarm ride the material system call queue (screenfx.cpp ScreenFx_QueueFunctor, the same
//     seam the screen passes use) instead of botox's n_render_queue; each arm is its own fake
//     functor from a ring, so two lsd draws in one frame never share one alpha
//   - DIP (IDirect3DDevice9 slot 82, d3d9.h: DrawPrimitive 81, DrawIndexedPrimitive 82, anchored on
//     EndScene 42 / Present 17 that thread.cpp hooks) is hooked lazily on the first lsd draw, so a
//     cfg without lsd never pays a detour on every draw call
#include "lsd_chams.h"
#include "chams_layers.h"
#include "../../globals/globals.h"
#include "../screen/screenfx.h"              // ScreenFx_QueueFunctor
#include "../screen/shaders/fx_lsd_vs0.h"    // k_fxLsdVs0Bytes (float verts)
#include "../screen/shaders/fx_lsd_vs1.h"    // k_fxLsdVs1Bytes (COMPRESSED_VERTS)
#include "../screen/shaders/fx_lsd_ps.h"     // k_fxLsdPsBytes
#include "../screen/shaders/fx_twotone_vs0.h" // k_fxTwoToneVs0Bytes (float verts)
#include "../screen/shaders/fx_twotone_vs1.h" // k_fxTwoToneVs1Bytes (float verts + tangent stream)
#include "../screen/shaders/fx_twotone_vs2.h" // k_fxTwoToneVs2Bytes (COMPRESSED_VERTS)
#include "../screen/shaders/fx_twotone_ps0.h" // k_fxTwoTonePs0Bytes (no normal map)
#include "../screen/shaders/fx_twotone_ps1.h" // k_fxTwoTonePs1Bytes (normal map s1)
#include "../screen/shaders/fx_twotone_ps2.h" // k_fxTwoTonePs2Bytes (normal map s3)
#include "../screen/shaders/fx_sheen_ps2.h"   // k_fxSheenPs2Bytes (engine sheen pass ps_2_b)
#include "../screen/shaders/fx_sheen_ps3.h"   // k_fxSheenPs3Bytes (engine sheen pass ps_3_0)
#include "../screen/shaders/fx_cham_vs.h"     // k_fxChamVs0 / 1 (float / compressed verts)
#include "../screen/shaders/fx_cham_ps.h"     // k_fxChamPs[mode]
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>

void DebugLog(const char* fmt, ...);

namespace
{
	using DIP_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	DIP_t o_DIP = nullptr;
	std::atomic<bool> s_hookTried{ false };

	std::atomic<bool> s_armed{ false };
	float s_params[2] = {};                 // render thread only: time, alpha

	IDirect3DVertexShader9* s_vs[2] = {};
	IDirect3DPixelShader9*  s_ps = nullptr;
	bool s_gpuFailed = false;

	// ---- botox model_shader.h ----
	using Defs = std::vector<std::pair<int, int>>;   // kind (0 f, 1 i, 2 b), register
	Defs s_vsDefs, s_psDefs;

	// def literals can leak into the hw register file under the engine's state cache: save + restore each
	void CollectDefs(const DWORD* code, Defs& out)
	{
		for (size_t i = 1; code[i] != 0x0000FFFF;)
		{
			const DWORD token = code[i], op = token & 0xFFFF;
			if (op == D3DSIO_COMMENT) { i += 1 + ((token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT); continue; }
			if (op == D3DSIO_DEF || op == D3DSIO_DEFI || op == D3DSIO_DEFB)
				out.emplace_back(op == D3DSIO_DEF ? 0 : op == D3DSIO_DEFI ? 1 : 2, int(code[i + 1] & D3DSP_REGNUM_MASK));
			i += 1 + ((token & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT);
		}
	}

	// -1 = not a studio decl we can read, 0 = float normals, 1 = UBYTE4 compressed normals
	int VertexCompression(IDirect3DDevice9* dev)
	{
		static IDirect3DVertexDeclaration9* last = nullptr;
		static int result = -1;
		IDirect3DVertexDeclaration9* decl = nullptr;
		if (FAILED(dev->GetVertexDeclaration(&decl)) || !decl)
			return -1;
		if (decl != last)
		{
			D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1] = {};
			UINT n = 0;
			result = -1;
			if (SUCCEEDED(decl->GetDeclaration(nullptr, &n)) && n <= MAXD3DDECLLENGTH + 1 && SUCCEEDED(decl->GetDeclaration(el, &n)))
				for (UINT i = 0; i < n && el[i].Stream != 0xFF; i++)
					if (el[i].Usage == D3DDECLUSAGE_NORMAL && el[i].UsageIndex == 0)
						result = el[i].Type == D3DDECLTYPE_UBYTE4 ? 1 : el[i].Type == D3DDECLTYPE_FLOAT3 ? 0 : -1;
			last = decl;
		}
		decl->Release();
		return result;
	}

	// diag: one line per new vertex decl seen while lsd / two tone is armed. viewport maxz 0.1 = the
	// viewmodel pass (viewrender.cpp:1109 DepthRange hack), so local vs enemy draws read apart
	void DeclDiag(IDirect3DDevice9* dev, const char* tag, int compressed)
	{
		static IDirect3DVertexDeclaration9* seen[48] = {};
		static int nSeen = 0;
		if (nSeen >= 48) return;
		IDirect3DVertexDeclaration9* decl = nullptr;
		if (FAILED(dev->GetVertexDeclaration(&decl)) || !decl) { DebugLog("%s: diag no decl", tag); nSeen = 48; return; }
		for (int i = 0; i < nSeen; i++) if (seen[i] == decl) { decl->Release(); return; }
		seen[nSeen++] = decl;
		D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1] = {};
		UINT n = 0;
		int normal = -1, weight = -1, index = -1;
		if (SUCCEEDED(decl->GetDeclaration(nullptr, &n)) && n <= MAXD3DDECLLENGTH + 1 && SUCCEEDED(decl->GetDeclaration(el, &n)))
			for (UINT i = 0; i < n && el[i].Stream != 0xFF; i++)
			{
				if (el[i].UsageIndex) continue;
				if (el[i].Usage == D3DDECLUSAGE_NORMAL)       normal = el[i].Type;
				if (el[i].Usage == D3DDECLUSAGE_BLENDWEIGHT)  weight = el[i].Type;
				if (el[i].Usage == D3DDECLUSAGE_BLENDINDICES) index  = el[i].Type;
			}
		decl->Release();
		D3DVIEWPORT9 vp = {};
		dev->GetViewport(&vp);
		DebugLog("%s: decl %p compressed %d (normal type %d weight %d index %d, -1 absent) maxz %.2f %s",
			tag, (void*)decl, compressed, normal, weight, index, vp.MaxZ, vp.MaxZ < 0.5f ? "VIEWMODEL" : "world");
	}

	struct Saved { int kind, reg; float v[4]; };
	void SaveConsts(IDirect3DDevice9* d, bool vs, const Defs& defs, std::vector<Saved>& out)
	{
		for (const auto& [kind, reg] : defs)
		{
			Saved s{ kind, reg, {} };
			if (kind == 0)      vs ? d->GetVertexShaderConstantF(reg, s.v, 1) : d->GetPixelShaderConstantF(reg, s.v, 1);
			else if (kind == 1) vs ? d->GetVertexShaderConstantI(reg, (int*)s.v, 1) : d->GetPixelShaderConstantI(reg, (int*)s.v, 1);
			else                vs ? d->GetVertexShaderConstantB(reg, (BOOL*)s.v, 1) : d->GetPixelShaderConstantB(reg, (BOOL*)s.v, 1);
			out.push_back(s);
		}
	}
	void RestoreConsts(IDirect3DDevice9* d, bool vs, const std::vector<Saved>& in)
	{
		for (const Saved& s : in)
		{
			if (s.kind == 0)      vs ? d->SetVertexShaderConstantF(s.reg, s.v, 1) : d->SetPixelShaderConstantF(s.reg, s.v, 1);
			else if (s.kind == 1) vs ? d->SetVertexShaderConstantI(s.reg, (const int*)s.v, 1) : d->SetPixelShaderConstantI(s.reg, (const int*)s.v, 1);
			else                  vs ? d->SetVertexShaderConstantB(s.reg, (const BOOL*)s.v, 1) : d->SetPixelShaderConstantB(s.reg, (const BOOL*)s.v, 1);
		}
	}

	template <size_t N>
	std::vector<DWORD> Code(const BYTE (&bytes)[N])   // BYTE arrays carry no alignment: copy into DWORDs
	{
		std::vector<DWORD> c((N + 3) / 4);
		memcpy(c.data(), bytes, N);
		return c;
	}

	bool GpuReady(IDirect3DDevice9* dev)
	{
		if (s_ps) return true;
		if (s_gpuFailed) return false;
		const auto vs0 = Code(k_fxLsdVs0Bytes), vs1 = Code(k_fxLsdVs1Bytes), ps = Code(k_fxLsdPsBytes);
		bool ok = SUCCEEDED(dev->CreateVertexShader(vs0.data(), &s_vs[0])) && SUCCEEDED(dev->CreateVertexShader(vs1.data(), &s_vs[1]))
			&& SUCCEEDED(dev->CreatePixelShader(ps.data(), &s_ps));
		if (ok)
		{
			CollectDefs(vs0.data(), s_vsDefs);   // both vs variants share the one def (c0)
			CollectDefs(ps.data(), s_psDefs);
			DebugLog("LSD: shaders ready (%d vs defs, %d ps defs)", int(s_vsDefs.size()), int(s_psDefs.size()));
			return true;
		}
		DebugLog("LSD: gpu FAILED -- lsd layers draw as flat white");
		for (auto*& v : s_vs) if (v) { v->Release(); v = nullptr; }
		if (s_ps) { s_ps->Release(); s_ps = nullptr; }
		s_gpuFailed = true;
		return false;
	}

	HRESULT DrawLsd(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// no shader / unreadable decl = the carrier draws as plain flat chams
		const int compressed = GpuReady(dev) ? VertexCompression(dev) : -1;
		DeclDiag(dev, "LSD", compressed);
		if (compressed < 0)
			return o_DIP(dev, type, base, minV, nV, start, prims);

		D3DVIEWPORT9 vp = {};
		dev->GetViewport(&vp);
		const float k[2][4] = { { s_params[0], s_params[1], 0.f, 0.f },
			{ float(vp.X), float(vp.Y), float(vp.Width ? vp.Width : 1), float(vp.Height ? vp.Height : 1) } };

		DWORD oldSrgb = 0;
		IDirect3DVertexShader9* oldVs = nullptr;
		IDirect3DPixelShader9* oldPs = nullptr;
		float oldPsK[2][4] = {};
		static std::vector<Saved> savedVs, savedPs;   // render thread only
		savedVs.clear(); savedPs.clear();

		dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &oldSrgb);
		dev->GetVertexShader(&oldVs);
		dev->GetPixelShader(&oldPs);
		dev->GetPixelShaderConstantF(20, oldPsK[0], 2);
		SaveConsts(dev, true, s_vsDefs, savedVs);
		SaveConsts(dev, false, s_psDefs, savedPs);

		// shadertoy colours are display values: no linear -> srgb on write
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		dev->SetVertexShader(s_vs[compressed]);
		dev->SetPixelShader(s_ps);
		dev->SetPixelShaderConstantF(20, k[0], 2);

		const HRESULT hr = o_DIP(dev, type, base, minV, nV, start, prims);

		// constants before shaders: the engine shader's own defs must win their slots
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, oldSrgb);
		dev->SetPixelShaderConstantF(20, oldPsK[0], 2);
		RestoreConsts(dev, true, savedVs);
		RestoreConsts(dev, false, savedPs);
		dev->SetVertexShader(oldVs);
		dev->SetPixelShader(oldPs);
		if (oldVs) oldVs->Release();
		if (oldPs) oldPs->Release();
		return hr;
	}

	// ---- botox hacks/visuals/two_tone_chams.cpp ----
	std::atomic<bool> s_ttArmed{ false };
	float s_ttLight[4] = {}, s_ttDark[4] = {}, s_ttParams[2] = {};   // render thread only
	bool  s_ttIgnorez = false, s_ttTranslucent = false;

	// vs: plain, plain + tangent stream, compressed. ps: no normal map, normal map in s1, s3
	IDirect3DVertexShader9* s_ttVs[3] = {};
	IDirect3DPixelShader9*  s_ttPs[3] = {};
	Defs s_ttVsDefs, s_ttPsDefs;
	bool s_ttGpuFailed = false;
	// ponytail: keyed by shader pointer, never cleared (botox clears on device lost); a material
	// reload that recreates an engine shader at a freed address could reuse a stale sampler index
	std::unordered_map<IDirect3DPixelShader9*, int> s_bumpSamplers;
	int s_ttLogged = 0;

	bool TwoToneGpuReady(IDirect3DDevice9* dev)
	{
		if (s_ttPs[0]) return true;
		if (s_ttGpuFailed) return false;
		const std::vector<DWORD> vs[3] = { Code(k_fxTwoToneVs0Bytes), Code(k_fxTwoToneVs1Bytes), Code(k_fxTwoToneVs2Bytes) };
		const std::vector<DWORD> ps[3] = { Code(k_fxTwoTonePs0Bytes), Code(k_fxTwoTonePs1Bytes), Code(k_fxTwoTonePs2Bytes) };
		bool ok = true;
		for (int i = 0; i < 3 && ok; i++)
		{
			ok = SUCCEEDED(dev->CreateVertexShader(vs[i].data(), &s_ttVs[i])) && SUCCEEDED(dev->CreatePixelShader(ps[i].data(), &s_ttPs[i]));
			if (ok) { CollectDefs(vs[i].data(), s_ttVsDefs); CollectDefs(ps[i].data(), s_ttPsDefs); }   // defs differ per variant: union
		}
		if (ok)
		{
			DebugLog("TWO: shaders ready (%d vs defs, %d ps defs)", int(s_ttVsDefs.size()), int(s_ttPsDefs.size()));
			return true;
		}
		DebugLog("TWO: gpu FAILED -- two tone layers draw as the model's own materials");
		for (auto*& v : s_ttVs) if (v) { v->Release(); v = nullptr; }
		for (auto*& p : s_ttPs) if (p) { p->Release(); p = nullptr; }
		s_ttVsDefs.clear(); s_ttPsDefs.clear();
		s_ttGpuFailed = true;
		return false;
	}

	bool HasTangentStream(IDirect3DDevice9* dev)
	{
		IDirect3DVertexDeclaration9* decl = nullptr;
		if (FAILED(dev->GetVertexDeclaration(&decl)) || !decl)
			return false;
		D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1] = {};
		UINT n = 0;
		bool result = false;
		if (SUCCEEDED(decl->GetDeclaration(nullptr, &n)) && n <= MAXD3DDECLLENGTH + 1 && SUCCEEDED(decl->GetDeclaration(el, &n)))
			for (UINT i = 0; i < n && el[i].Stream != 0xFF; i++)
				result |= el[i].Usage == D3DDECLUSAGE_TANGENT && el[i].UsageIndex == 0;
		decl->Release();
		return result;
	}

	// botox reads the sampler with D3DXGetShaderConstantTable; no d3dx9 here, so the CTAB comment block
	// is walked by hand (d3dx9shader.h D3DXSHADER_CONSTANTTABLE / D3DXSHADER_CONSTANTINFO layout).
	// Returns the sampler register of `name`, -1 not found, -2 no constant table.
	int CtabSampler(const DWORD* code, size_t dwords, const char* const* names, int nNames)
	{
		for (size_t i = 1; i < dwords && code[i] != 0x0000FFFF;)
		{
			const DWORD token = code[i], op = token & 0xFFFF;
			const size_t len = op == D3DSIO_COMMENT ? (token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT
			                                        : (token & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT;
			if (op == D3DSIO_COMMENT && len >= 8 && i + 1 + len <= dwords && code[i + 1] == MAKEFOURCC('C', 'T', 'A', 'B'))
			{
				const BYTE* base = reinterpret_cast<const BYTE*>(&code[i + 2]);
				const size_t size = (len - 1) * 4;
				const DWORD* hdr = reinterpret_cast<const DWORD*>(base);   // Size, Creator, Version, Constants, ConstantInfo, Flags, Target
				const DWORD count = hdr[3], info = hdr[4];
				if (info > size || count > (size - info) / 20) return -2;
				for (DWORD c = 0; c < count; c++)
				{
					const BYTE* ci = base + info + c * 20;   // DWORD Name, WORD RegisterSet, WORD RegisterIndex, WORD RegisterCount, WORD Reserved, DWORD TypeInfo, DWORD DefaultValue
					const DWORD nameOff = *reinterpret_cast<const DWORD*>(ci);
					const WORD regSet = *reinterpret_cast<const WORD*>(ci + 4), regIdx = *reinterpret_cast<const WORD*>(ci + 6);
					if (nameOff >= size || regSet != 3 /* D3DXRS_SAMPLER */) continue;
					const char* nm = reinterpret_cast<const char*>(base + nameOff);
					const size_t maxLen = size - nameOff;
					for (int k = 0; k < nNames; k++)
						if (strnlen(nm, maxLen) < maxLen && strcmp(nm, names[k]) == 0) return int(regIdx);
				}
				return -1;
			}
			i += 1 + len;
		}
		return -2;
	}

	// normal map register of the engine pixel shader, by name from its constant table (TF2 skin/phong
	// "NormalMapSampler", bumped vertexlit "BumpmapSampler"). -1 none, -2 no table. cached per shader
	int BumpSampler(IDirect3DPixelShader9* shader)
	{
		if (!shader) return -1;
		if (const auto found = s_bumpSamplers.find(shader); found != s_bumpSamplers.end())
			return found->second;
		int result = -2;
		UINT size = 0;
		if (SUCCEEDED(shader->GetFunction(nullptr, &size)) && size)
		{
			std::vector<DWORD> code((size + 3) / 4);
			static const char* const kNames[2] = { "NormalMapSampler", "BumpmapSampler" };
			if (SUCCEEDED(shader->GetFunction(code.data(), &size)))
				result = CtabSampler(code.data(), code.size(), kNames, 2);
		}
		if (s_ttLogged < 32)
		{
			s_ttLogged++;
			DebugLog("TWO: ps %p bump %d (-1 none, -2 no ctab)", (void*)shader, result);
		}
		s_bumpSamplers.emplace(shader, result);
		return result;
	}

	HRESULT DrawTwoTone(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// no shader / not a studio mesh = the model's own material draws as is
		const int compressed = TwoToneGpuReady(dev) ? VertexCompression(dev) : -1;
		DeclDiag(dev, "TWO", compressed);
		if (compressed < 0)
			return o_DIP(dev, type, base, minV, nV, start, prims);

		IDirect3DVertexShader9* oldVs = nullptr;
		IDirect3DPixelShader9* oldPs = nullptr;
		dev->GetVertexShader(&oldVs);
		dev->GetPixelShader(&oldPs);

		const int bump = BumpSampler(oldPs);
		const int vsIndex = compressed ? 2 : HasTangentStream(dev) ? 1 : 0;
		const int psIndex = bump == 1 ? 1 : bump == 3 ? 2 : 0;

		DWORD oldSrgb = 0, oldZFunc = 0, oldZWrite = 0, oldAlphaTest = 0, oldAlphaRef = 0, oldBlend = 0, oldSrc = 0, oldDest = 0, oldColorWrite = 0;
		dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &oldSrgb);
		dev->GetRenderState(D3DRS_ZFUNC, &oldZFunc);
		dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
		dev->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
		dev->GetRenderState(D3DRS_ALPHAREF, &oldAlphaRef);
		dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldBlend);
		dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
		dev->GetRenderState(D3DRS_DESTBLEND, &oldDest);
		dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWrite);

		const float psK[3][4] = { { s_ttLight[0], s_ttLight[1], s_ttLight[2], s_ttLight[3] },
			{ s_ttDark[0], s_ttDark[1], s_ttDark[2], s_ttDark[3] },
			{ s_ttParams[0], s_ttParams[1], oldAlphaTest ? 1.f : 0.f, float(oldAlphaRef & 0xFF) / 255.f } };

		float oldPsK[3][4] = {};
		static std::vector<Saved> savedVs, savedPs;   // render thread only
		savedVs.clear(); savedPs.clear();

		dev->GetPixelShaderConstantF(30, oldPsK[0], 3);
		SaveConsts(dev, true, s_ttVsDefs, savedVs);
		SaveConsts(dev, false, s_ttPsDefs, savedPs);

		// picked colours are display values: no linear -> srgb on write. alpha test = clip in our ps
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

		// prepass lays depth, colour pass then only hits each pixel's nearest surface (equal depth must pass)
		if (s_ttIgnorez)
			dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		else if (s_ttTranslucent && (oldZFunc == D3DCMP_LESS || oldZFunc == D3DCMP_GREATER))
			dev->SetRenderState(D3DRS_ZFUNC, oldZFunc == D3DCMP_LESS ? D3DCMP_LESSEQUAL : D3DCMP_GREATEREQUAL);

		dev->SetVertexShader(s_ttVs[vsIndex]);
		dev->SetPixelShader(s_ttPs[psIndex]);
		dev->SetPixelShaderConstantF(30, psK[0], 3);

		// translucent: own blend whatever the material asked (additive, opaque snapshot). not ignorez = depth only
		// prepass first so inner parts (far arm, legs, weapon) never show through. ignorez can't: depth there is the world
		if (s_ttTranslucent)
		{
			if (!s_ttIgnorez)
			{
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
				dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				o_DIP(dev, type, base, minV, nV, start, prims);
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, oldColorWrite);
			}
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		}
		if (s_ttIgnorez || s_ttTranslucent)
			dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

		const HRESULT hr = o_DIP(dev, type, base, minV, nV, start, prims);

		// constants before shaders: the engine shader's own defs must win their slots
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, oldSrgb);
		dev->SetRenderState(D3DRS_ZFUNC, oldZFunc);
		dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldBlend);
		dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
		dev->SetRenderState(D3DRS_DESTBLEND, oldDest);
		dev->SetPixelShaderConstantF(30, oldPsK[0], 3);
		RestoreConsts(dev, true, savedVs);
		RestoreConsts(dev, false, savedPs);
		dev->SetVertexShader(oldVs);
		dev->SetPixelShader(oldPs);
		if (oldVs) oldVs->Release();
		if (oldPs) oldPs->Release();
		return hr;
	}

	// ---- shader chams (fx_cham_*.hlsl, Atlas-TF2 model.hlsl): every material slot but original / lsd / two tone ----
	std::atomic<bool> s_chArmed{ false };
	float s_chColor[4] = {}, s_chTime = 0.f;   // render thread only
	int   s_chMode = 0;
	bool  s_chIgnorez = false;

	IDirect3DVertexShader9* s_chVs[2] = {};
	IDirect3DPixelShader9*  s_chPs[ChamsLayers::Count] = {};
	static_assert(sizeof(k_fxChamPs) / sizeof(k_fxChamPs[0]) == ChamsLayers::Count, "fx_cham_build.sh COUNT != ChamsLayers::Count");
	Defs s_chVsDefs, s_chPsDefs;
	bool s_chReady = false, s_chGpuFailed = false;

	std::vector<DWORD> Code(const BYTE* bytes, unsigned size)
	{
		std::vector<DWORD> c((size + 3) / 4);
		memcpy(c.data(), bytes, size);
		return c;
	}

	bool ChamGpuReady(IDirect3DDevice9* dev)
	{
		if (s_chReady) return true;
		if (s_chGpuFailed) return false;
		const std::vector<DWORD> vs[2] = { Code(k_fxChamVs0), Code(k_fxChamVs1) };
		bool ok = true;
		for (int i = 0; i < 2 && ok; i++)
			if ((ok = SUCCEEDED(dev->CreateVertexShader(vs[i].data(), &s_chVs[i]))))
				CollectDefs(vs[i].data(), s_chVsDefs);
		for (int m = 0; m < ChamsLayers::Count && ok; m++)
		{
			if (!k_fxChamPs[m].bytes) continue;
			const auto ps = Code(k_fxChamPs[m].bytes, k_fxChamPs[m].size);
			if ((ok = SUCCEEDED(dev->CreatePixelShader(ps.data(), &s_chPs[m]))))
				CollectDefs(ps.data(), s_chPsDefs);   // union of every mode's defs
		}
		if (ok)
		{
			DebugLog("CHAM: shaders ready (%d vs defs, %d ps defs)", int(s_chVsDefs.size()), int(s_chPsDefs.size()));
			return s_chReady = true;
		}
		DebugLog("CHAM: gpu FAILED -- shader chams draw as the plain white carrier");
		for (auto*& v : s_chVs) if (v) { v->Release(); v = nullptr; }
		for (auto*& p : s_chPs) if (p) { p->Release(); p = nullptr; }
		s_chVsDefs.clear(); s_chPsDefs.clear();
		s_chGpuFailed = true;
		return false;
	}

	// Atlas model_render.hpp surface_t::additive / blends
	bool ChamAdditive(int m)
	{
		using namespace ChamsLayers;
		return m == Glow || m == Metallic || m == Wet || m == Fresnel || m == Neon || m == Soap || m == Aurora || m == Aquarium || m == Sky;
	}

	HRESULT DrawCham(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// no shader / not a studio mesh = the carrier (plain white vertexlit) draws as is
		const int compressed = ChamGpuReady(dev) ? VertexCompression(dev) : -1;
		DeclDiag(dev, "CHAM", compressed);
		IDirect3DPixelShader9* ps = s_chMode > 0 && s_chMode < ChamsLayers::Count ? s_chPs[s_chMode] : nullptr;
		if (compressed < 0 || !ps)
			return o_DIP(dev, type, base, minV, nV, start, prims);

		const bool additive = ChamAdditive(s_chMode);
		const bool blends = additive || s_chColor[3] < 1.f || s_chMode == ChamsLayers::Crystal || s_chMode == ChamsLayers::Network;

		IDirect3DVertexShader9* oldVs = nullptr;
		IDirect3DPixelShader9* oldPs = nullptr;
		dev->GetVertexShader(&oldVs);
		dev->GetPixelShader(&oldPs);

		DWORD oldSrgb = 0, oldZFunc = 0, oldZWrite = 0, oldAlphaTest = 0, oldBlend = 0, oldSrc = 0, oldDest = 0, oldColorWrite = 0, oldTexSrgb = 0;
		dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &oldSrgb);
		dev->GetRenderState(D3DRS_ZFUNC, &oldZFunc);
		dev->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
		dev->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
		dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldBlend);
		dev->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
		dev->GetRenderState(D3DRS_DESTBLEND, &oldDest);
		dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWrite);
		dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &oldTexSrgb);

		const float psK[2][4] = { { s_chColor[0], s_chColor[1], s_chColor[2], s_chColor[3] }, { s_chTime, 0.f, 0.f, 0.f } };
		float oldPsK[2][4] = {};
		static std::vector<Saved> savedVs, savedPs;   // render thread only
		savedVs.clear(); savedPs.clear();
		dev->GetPixelShaderConstantF(40, oldPsK[0], 2);
		SaveConsts(dev, true, s_chVsDefs, savedVs);
		SaveConsts(dev, false, s_chPsDefs, savedPs);

		// colours are display values (Atlas: srgb write off), basetexture read raw to match
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
		if (s_chIgnorez)
			dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		else if (blends && (oldZFunc == D3DCMP_LESS || oldZFunc == D3DCMP_GREATER))
			dev->SetRenderState(D3DRS_ZFUNC, oldZFunc == D3DCMP_LESS ? D3DCMP_LESSEQUAL : D3DCMP_GREATEREQUAL);

		dev->SetVertexShader(s_chVs[compressed]);
		dev->SetPixelShader(ps);
		dev->SetPixelShaderConstantF(40, psK[0], 2);

		// Atlas draw_depth_first: blended = depth only prepass, then colour on the nearest surface only
		if (blends)
		{
			if (!s_chIgnorez)
			{
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
				dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
				o_DIP(dev, type, base, minV, nV, start, prims);
				dev->SetRenderState(D3DRS_COLORWRITEENABLE, oldColorWrite);
			}
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			dev->SetRenderState(D3DRS_DESTBLEND, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
		}
		else
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		if (s_chIgnorez || blends)
			dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

		const HRESULT hr = o_DIP(dev, type, base, minV, nV, start, prims);

		// constants before shaders: the engine shader's own defs must win their slots
		dev->SetRenderState(D3DRS_SRGBWRITEENABLE, oldSrgb);
		dev->SetRenderState(D3DRS_ZFUNC, oldZFunc);
		dev->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
		dev->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
		dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldBlend);
		dev->SetRenderState(D3DRS_SRCBLEND, oldSrc);
		dev->SetRenderState(D3DRS_DESTBLEND, oldDest);
		dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, oldTexSrgb);
		dev->SetPixelShaderConstantF(40, oldPsK[0], 2);
		RestoreConsts(dev, true, savedVs);
		RestoreConsts(dev, false, savedPs);
		dev->SetVertexShader(oldVs);
		dev->SetPixelShader(oldPs);
		if (oldVs) oldVs->Release();
		if (oldPs) oldPs->Release();
		return hr;
	}

	// ---- normal blend weapon sheen (fx_sheen_ps.hlsl) ----
	IDirect3DPixelShader9* s_shPs[2] = {};   // ps_2_b, ps_3_0: the engine pass is either, ours must pair with its vs
	Defs s_shPsDefs;
	bool s_shGpuFailed = false;
	// ponytail: same never-cleared shader pointer cache as s_bumpSamplers
	std::unordered_map<IDirect3DPixelShader9*, int> s_sheenShaders;   // 0 not the sheen pass, 2 ps_2_x, 3 ps_3_0

	bool SheenGpuReady(IDirect3DDevice9* dev)
	{
		if (s_shPs[0]) return true;
		if (s_shGpuFailed) return false;
		const std::vector<DWORD> ps[2] = { Code(k_fxSheenPs2Bytes), Code(k_fxSheenPs3Bytes) };
		bool ok = true;
		for (int i = 0; i < 2 && ok; i++)
			if ((ok = SUCCEEDED(dev->CreatePixelShader(ps[i].data(), &s_shPs[i]))))
				CollectDefs(ps[i].data(), s_shPsDefs);   // union of both variants' defs
		if (ok)
		{
			DebugLog("SHEEN: normal blend shaders ready (%d ps defs)", int(s_shPsDefs.size()));
			return true;
		}
		DebugLog("SHEEN: normal blend gpu FAILED -- the stock additive sheen draws");
		for (auto*& p : s_shPs) if (p) { p->Release(); p = nullptr; }
		s_shPsDefs.clear();
		s_shGpuFailed = true;
		return false;
	}

	// TF2 weapon_sheen_pass_ps2x.fxc: EnvmapSampler s2 + EnvmapMaskSampler s3. Matched on those two
	// because they are SAMPLED: fxc drops unused samplers from the constant table, and the pass's
	// g_tRefractionSampler s0 / g_tBumpSampler s1 are declared but never read. Every other ps2x with
	// the mask has it at s4/s5; lightmappedgeneric_ps11 has it at s3 but is ps_1_1 (major < 2)
	int SheenPassModel(IDirect3DPixelShader9* shader)
	{
		if (!shader) return 0;
		if (const auto found = s_sheenShaders.find(shader); found != s_sheenShaders.end())
			return found->second;
		int result = 0;
		UINT size = 0;
		if (SUCCEEDED(shader->GetFunction(nullptr, &size)) && size)
		{
			std::vector<DWORD> code((size + 3) / 4);
			static const char* const kMask[1] = { "EnvmapMaskSampler" };
			static const char* const kEnv[1] = { "EnvmapSampler" };
			if (SUCCEEDED(shader->GetFunction(code.data(), &size)))
			{
				const int major = int((code[0] >> 8) & 0xFF);   // version token: 0xFFFF0300 = ps_3_0
				const int mask = CtabSampler(code.data(), code.size(), kMask, 1);
				const int env = mask >= 0 ? CtabSampler(code.data(), code.size(), kEnv, 1) : -1;
				if (mask == 3 && env == 2 && major >= 2)
				{
					result = major >= 3 ? 3 : 2;
					DebugLog("SHEEN: weapon sheen pass ps %p (ps_%d)", (void*)shader, result);
				}
				static int s_maskLogged = 0;
				if (mask >= 0 && !result && s_maskLogged < 16)
				{
					s_maskLogged++;
					DebugLog("SHEEN: ps %p ps_%d mask s%d env s%d -- not the sheen pass", (void*)shader, major, mask, env);
				}
				static bool s_noCtabLogged = false;
				if (mask == -2 && !s_noCtabLogged)
				{
					s_noCtabLogged = true;
					DebugLog("SHEEN: engine ps %p has no constant table -- if every one is like this, normal blend sheen never finds the pass", (void*)shader);
				}
			}
		}
		s_sheenShaders.emplace(shader, result);
		return result;
	}

	HRESULT DrawSheenNormal(IDirect3DDevice9* dev, IDirect3DPixelShader9* enginePs, int model,
	                        D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// no shader = the stock additive sheen
		if (!SheenGpuReady(dev))
			return o_DIP(dev, type, base, minV, nV, start, prims);

		float oldK[4] = {};
		static std::vector<Saved> savedPs;   // render thread only
		savedPs.clear();
		dev->GetPixelShaderConstantF(10, oldK, 1);
		SaveConsts(dev, false, s_shPsDefs, savedPs);

		// the pass's own state stays: srcalpha / invsrcalpha, no alpha writes, srgb write (the envmap
		// colour is linear, as stock), engine vs, cube in s2 + mask in s3, c6..c8 + c30
		const float k[4] = { std::clamp(g_sheenNormalAlpha, 0.f, 1.f), std::clamp(g_sheenNormalEnv, 0.f, 10.f), 0.f, 0.f };
		dev->SetPixelShader(s_shPs[model == 3 ? 1 : 0]);
		dev->SetPixelShaderConstantF(10, k, 1);

		const HRESULT hr = o_DIP(dev, type, base, minV, nV, start, prims);

		// constants before shaders: the engine shader's own defs must win their slots
		dev->SetPixelShaderConstantF(10, oldK, 1);
		RestoreConsts(dev, false, savedPs);
		dev->SetPixelShader(enginePs);
		return hr;
	}

	HRESULT __stdcall h_DIP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, INT base, UINT minV, UINT nV, UINT start, UINT prims)
	{
		// every draw in the game lands here: one guard (eject drains it before freeing trampolines,
		// globals.h) and two relaxed loads unless armed (plus one GetPixelShader with normal sheen on)
		MiscHookGuard _g;
		if (s_ttArmed.load(std::memory_order_relaxed))
			return DrawTwoTone(dev, type, base, minV, nV, start, prims);
		if (s_armed.load(std::memory_order_relaxed))
			return DrawLsd(dev, type, base, minV, nV, start, prims);
		if (s_chArmed.load(std::memory_order_relaxed))
			return DrawCham(dev, type, base, minV, nV, start, prims);
		if (!g_sheenNormal)
			return o_DIP(dev, type, base, minV, nV, start, prims);

		IDirect3DPixelShader9* ps = nullptr;
		dev->GetPixelShader(&ps);
		const int model = SheenPassModel(ps);
		const HRESULT hr = model ? DrawSheenNormal(dev, ps, model, type, base, minV, nV, start, prims)
		                         : o_DIP(dev, type, base, minV, nV, start, prims);
		if (ps) ps->Release();
		return hr;
	}

	void EnsureHook()
	{
		// main thread (lsd / two tone begin) and render thread (SheenNormal_Tick) both get here:
		// one winner, or the loser's MH_CreateHook fails and nulls o_DIP under a live hook
		if (!g_d3dDev || s_hookTried.exchange(true)) return;
		void** vt = *reinterpret_cast<void***>(g_d3dDev);
		if (MH_CreateHook(vt[82], (LPVOID)&h_DIP, (LPVOID*)&o_DIP) == MH_OK && MH_EnableHook(vt[82]) == MH_OK)
			DebugLog("LSD: DrawIndexedPrimitive hook installed (slot 82)");
		else
		{
			o_DIP = nullptr;
			DebugLog("LSD: DrawIndexedPrimitive hook FAILED -- lsd layers draw as flat white");
		}
	}

	// ---- fake CFunctors (layout + why: screenfx.cpp "the call queue") ----
	struct LsdFunctor { void** vfptr; float time, alpha; };
	int   __fastcall Fn_AddRef(void*)              { return 1; }
	int   __fastcall Fn_Release(void*)             { return 1; }
	void* __fastcall Fn_Dtor(void* p, unsigned)    { return p; }
	void  __fastcall Fn_Arm(void* p)
	{
		auto* f = static_cast<LsdFunctor*>(p);
		s_params[0] = f->time;
		s_params[1] = f->alpha;
		s_armed.store(true, std::memory_order_relaxed);
	}
	void  __fastcall Fn_Disarm(void*) { s_armed.store(false, std::memory_order_relaxed); }

	void* s_armVt[4]    = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Arm };
	void* s_disarmVt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_Disarm };
	// ponytail: ring of 1024 arms, the render thread trails by about one frame; a frame with more
	// lsd layer draws than that would recycle an arm still waiting in the queue (wrong time/alpha)
	LsdFunctor s_arms[1024];
	unsigned   s_armNext = 0;
	LsdFunctor s_disarm = { s_disarmVt, 0.f, 0.f };

	// two tone: same ring scheme, one arm per layer draw
	struct TtFunctor { void** vfptr; float light[4], dark[4], params[2]; bool ignorez; };
	void __fastcall Fn_TtArm(void* p)
	{
		auto* f = static_cast<TtFunctor*>(p);
		memcpy(s_ttLight, f->light, sizeof(s_ttLight));
		memcpy(s_ttDark, f->dark, sizeof(s_ttDark));
		memcpy(s_ttParams, f->params, sizeof(s_ttParams));
		s_ttIgnorez = f->ignorez;
		s_ttTranslucent = f->light[3] < 1.f || f->dark[3] < 1.f;
		s_ttArmed.store(true, std::memory_order_relaxed);
	}
	void __fastcall Fn_TtDisarm(void*) { s_ttArmed.store(false, std::memory_order_relaxed); }

	void* s_ttArmVt[4]    = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_TtArm };
	void* s_ttDisarmVt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_TtDisarm };
	TtFunctor s_ttArms[1024];
	unsigned  s_ttArmNext = 0;
	TtFunctor s_ttDisarm = { s_ttDisarmVt };

	// shader chams: same ring scheme
	struct ChFunctor { void** vfptr; float color[4]; float time; int mode; bool ignorez; };
	void __fastcall Fn_ChArm(void* p)
	{
		auto* f = static_cast<ChFunctor*>(p);
		memcpy(s_chColor, f->color, sizeof(s_chColor));
		s_chTime = f->time;
		s_chMode = f->mode;
		s_chIgnorez = f->ignorez;
		s_chArmed.store(true, std::memory_order_relaxed);
	}
	void __fastcall Fn_ChDisarm(void*) { s_chArmed.store(false, std::memory_order_relaxed); }

	void* s_chArmVt[4]    = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_ChArm };
	void* s_chDisarmVt[4] = { (void*)&Fn_AddRef, (void*)&Fn_Release, (void*)&Fn_Dtor, (void*)&Fn_ChDisarm };
	ChFunctor s_chArms[1024];
	unsigned  s_chArmNext = 0;
	ChFunctor s_chDisarm = { s_chDisarmVt };
}

void SheenNormal_Tick()
{
	static bool s_logged = false;
	if (!g_sheenNormal) return;
	EnsureHook();
	if (!s_logged)
	{
		s_logged = true;
		DebugLog("SHEEN: normal blend on, DIP hook %s", o_DIP ? "live" : "MISSING");
	}
}

void TwoToneChams_Begin(const float light[4], const float dark[4], float threshold, float edge, bool ignorez)
{
	EnsureHook();
	if (!o_DIP) return;
	TtFunctor& f = s_ttArms[s_ttArmNext++ & 1023];
	f.vfptr = s_ttArmVt;
	memcpy(f.light, light, sizeof(f.light));
	memcpy(f.dark, dark, sizeof(f.dark));
	f.params[0] = threshold;
	f.params[1] = edge;
	f.ignorez = ignorez;
	if (!ScreenFx_QueueFunctor(&f))
		Fn_TtArm(&f);   // mat_queue_mode 0: this thread is the render thread
}

void TwoToneChams_End()
{
	if (!o_DIP) return;
	if (!ScreenFx_QueueFunctor(&s_ttDisarm))
		Fn_TtDisarm(&s_ttDisarm);
}

void LsdChams_Begin(float realTime, float alpha)
{
	EnsureHook();
	if (!o_DIP) return;
	LsdFunctor& f = s_arms[s_armNext++ & 1023];
	f = { s_armVt, realTime, alpha };
	if (!ScreenFx_QueueFunctor(&f))
		Fn_Arm(&f);   // mat_queue_mode 0: this thread is the render thread
}

void LsdChams_End()
{
	if (!o_DIP) return;
	if (!ScreenFx_QueueFunctor(&s_disarm))
		Fn_Disarm(&s_disarm);
}

void ShaderChams_Begin(int mode, const float color[4], float realTime, bool ignorez)
{
	EnsureHook();
	if (!o_DIP) return;
	ChFunctor& f = s_chArms[s_chArmNext++ & 1023];
	f.vfptr = s_chArmVt;
	memcpy(f.color, color, sizeof(f.color));
	f.time = std::fmod(realTime, 1024.f);   // Atlas TIME_WRAP: float precision stays fine
	f.mode = mode;
	f.ignorez = ignorez;
	if (!ScreenFx_QueueFunctor(&f))
		Fn_ChArm(&f);   // mat_queue_mode 0: this thread is the render thread
}

void ShaderChams_End()
{
	if (!o_DIP) return;
	if (!ScreenFx_QueueFunctor(&s_chDisarm))
		Fn_ChDisarm(&s_chDisarm);
}
