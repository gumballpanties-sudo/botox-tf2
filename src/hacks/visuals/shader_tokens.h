#pragma once
// d3d9 shader bytecode walks shared by lsd_chams.cpp (engine shaders at draw time) and tests/chamvs_test.cpp
// (our own tables). Linear token walk, no instruction length field needed: vs_1_x (dxlevel 80/81) has none,
// so parameter tokens are told by bit 31 and the payloads that could fake one (comment blocks, def literals:
// a negative float has bit 31 too) are skipped by opcode.
#include <d3d9.h>
#include <cstddef>

namespace ShaderTokens
{
	inline unsigned RegType(DWORD t) { return ((t & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT) | ((t & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2); }

	// Calls fn(DWORD token) for every parameter token outside comment / def payloads
	template <class Fn>
	inline void ForEachParam(const DWORD* code, size_t n, Fn fn)
	{
		for (size_t i = 1; i < n && code[i] != 0x0000FFFF; i++)
		{
			const DWORD t = code[i];
			if (t & 0x80000000) { fn(t); continue; }
			const DWORD op = t & 0xFFFF;
			if (op == D3DSIO_COMMENT)                       i += (t & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT;
			else if (op == D3DSIO_DEF || op == D3DSIO_DEFI) i += 5;   // dst + 4 literals
			else if (op == D3DSIO_DEFB)                     i += 2;
		}
	}

	// Bone matrix base register of a studio vs: the engine's SkinPosition reads cModel[boneIndices[0]] as
	// c58[a0.x] (dx9 vs20+, sdk2013 common_vs_fxc.h:169) or c48[a0.x] (dxlevel 80/81 vs_1_1, :148). Decided by
	// a relative-addressed constant source at exactly 48 or 58; nothing found (rigid vs, no skinning) = 58.
	// vs_1_1 (version token 0xFFFE0101) = 48 outright: the dx8 studio shaders are hand asm (macros.vsh:658)
	// that fold the base in from c3.w (= 48, the only base 16 bones fit under vs_1_1's 96 regs) and then read
	// c[a0.x], c[a0.x+1], c[a0.x+2] -- relative regs 0..2, so the 48/58 scan never sees it.
	inline int BoneBase(const DWORD* code, size_t n)
	{
		if (n && ((code[0] >> 8) & 0xFF) == 1) return 48;
		bool seen48 = false, seen58 = false;
		ForEachParam(code, n, [&](DWORD t)
		{
			if (RegType(t) != D3DSPR_CONST || !(t & D3DSHADER_ADDRMODE_RELATIVE)) return;
			const unsigned reg = t & D3DSP_REGNUM_MASK;
			seen48 |= reg == 48;
			seen58 |= reg == 58;
		});
		return seen48 && !seen58 ? 48 : 58;
	}

	// Highest constant register a shader references absolutely (-1 none)
	inline int MaxAbsoluteConst(const DWORD* code, size_t n)
	{
		int best = -1;
		ForEachParam(code, n, [&](DWORD t)
		{
			if (RegType(t) == D3DSPR_CONST && !(t & D3DSHADER_ADDRMODE_RELATIVE))
				best = best > int(t & D3DSP_REGNUM_MASK) ? best : int(t & D3DSP_REGNUM_MASK);
		});
		return best;
	}

	// Bit mask of D3DDECLUSAGE_* a vs declares as INPUT registers (dcl_usage vN). Needs the length field (vs_2_0+).
	inline unsigned InputUsages(const DWORD* code, size_t n)
	{
		unsigned mask = 0;
		for (size_t i = 1; i < n && code[i] != 0x0000FFFF;)
		{
			const DWORD token = code[i], op = token & 0xFFFF;
			if (op == D3DSIO_COMMENT) { i += 1 + ((token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT); continue; }
			const size_t len = (token & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT;
			if (op == D3DSIO_DCL && len >= 2 && RegType(code[i + 2]) == D3DSPR_INPUT)
				mask |= 1u << (code[i + 1] & D3DSP_DCL_USAGE_MASK);
			i += 1 + len;
		}
		return mask;
	}
}
