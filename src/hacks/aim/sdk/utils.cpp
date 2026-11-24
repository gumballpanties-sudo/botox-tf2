#include "utils.h"

#include "core.h"
#include "sdk.h"
#include "defs.h"
#include "sigscan.h"

#include <format>

// ================ Utils/Hooks/Hooks.cpp ================
// Hook table for the aim code. Every MAKE_HOOK in the binary registers itself at static init
// and Initialize() installs all of them. None may share a target with a dih hook (MinHook
// refuses the second one): CreateMove and DispatchUserMessage each have ONE owner, see
// thread.cpp. No WndProc / D3D9 hooks here -- dih owns those (render.cpp, thread.cpp).

CHook::CHook(const std::string& sName, void* pInitFunc)
{
	m_pInitFunc = pInitFunc;
	U::Hooks.m_mHooks[sName] = this;
}

// MH_Initialize already ran (thread.cpp MainThread). Enables are queued and applied in one
// MH_ApplyQueued: one thread freeze for all of them (same as the old MH_ALL_HOOKS), and only
// OUR hooks -- dih creates some hooks disarmed on purpose (netgraph) and arms them itself.
int CHooks::Initialize()
{
	// A hook that is created but not enabled is removed again and its m_pTarget cleared, so
	// "m_pTarget set" always means "live" (Aim_OwnsCreateMove reads it) and dih can still hook
	// that target itself.
	auto Drop = [](CHook* pHook)
	{
		MH_RemoveHook(pHook->m_pTarget);
		pHook->m_pTarget = nullptr;
	};

	int nFailed = 0;
	for (auto& [sName, pHook] : m_mHooks)
	{
		reinterpret_cast<void(__cdecl*)()>(pHook->m_pInitFunc)();
		if (!pHook->m_pTarget || MH_QueueEnableHook(pHook->m_pTarget) != MH_OK)
		{
			if (pHook->m_pTarget)
				Drop(pHook);
			U::Core.AppendFailText(std::format("hook {} NOT installed (target missing or taken)", sName).c_str());
			nFailed++;
			continue;
		}
		m_vInstalled.push_back(pHook);
	}
	if (MH_ApplyQueued() != MH_OK)
	{
		// MinHook stops at the first hook it cannot enable: retry one by one, drop the bad ones
		U::Core.AppendFailText("MH_ApplyQueued failed, enabling one by one");
		std::erase_if(m_vInstalled, [&](CHook* pHook)
		{
			const MH_STATUS eStatus = MH_EnableHook(pHook->m_pTarget);
			if (eStatus == MH_OK || eStatus == MH_ERROR_ENABLED)
				return false;
			for (auto& [sName, pOther] : m_mHooks)
				if (pOther == pHook)
					U::Core.AppendFailText(std::format("hook {} NOT enabled (status {})", sName, int(eStatus)).c_str());
			Drop(pHook);
			nFailed++;
			return true;
		});
	}
	return nFailed;
}

// dih calls MH_Uninitialize itself on eject; this only turns ours off first.
void CHooks::Unload()
{
	for (auto pHook : m_vInstalled)
		MH_QueueDisableHook(pHook->m_pTarget);
	MH_ApplyQueued();
	m_vInstalled.clear();
}

// ================ Utils/Interfaces/Interfaces.cpp ================
#include <TlHelp32.h>
#include <string>
#include <sstream>
#include <format>

#pragma warning (disable: 4172)

InterfaceInit_t::InterfaceInit_t(void** pPtr, const char* sDLL, const char* sName, int8_t nType, int8_t nOffset, int8_t nDereferenceCount)
{
	m_pPtr = pPtr;
	m_sDLL = sDLL;
	m_sName = sName;
	m_nType = nType;
	m_nOffset = nOffset;
	m_nDereferenceCount = nDereferenceCount;

	U::Interfaces.AddInterface(this);
}

bool CInterfaces::Initialize()
{
	for (auto& Interface : m_vInterfaces)
	{
		const char* sModule = nullptr;
		std::vector<std::string> vModules;
		// split on comma OR space, dropping empties.
		{
			const std::string sDLLs = Interface->m_sDLL ? Interface->m_sDLL : "";
			std::string sPart;
			for (char c : sDLLs)
			{
				if (c == ',' || c == ' ')
				{
					if (!sPart.empty()) vModules.push_back(sPart);
					sPart.clear();
				}
				else
					sPart += c;
			}
			if (!sPart.empty()) vModules.push_back(sPart);
		}
		if (vModules.size() == 1)
			sModule = vModules.front().c_str();
		else
		{
			for (auto& sName : vModules)
			{
				if (GetModuleHandle(sName.c_str()))
				{
					sModule = sName.c_str();
					break;
				}
			}
			if (!sModule)
			{
				std::stringstream ssModuleStream;
				for (auto& sName : vModules)
					ssModuleStream << std::format("{}{}", !ssModuleStream.str().empty() ? ", " : "", sName);

				U::Core.AppendFailText(std::format("CInterfaces::Initialize() failed to find module:\n  {}\n  {}", ssModuleStream.str(), Interface->m_sName).c_str());
				m_bFailed = true;
				continue;
			}
		}

		switch (Interface->m_nType)
		{
		case 0:
		{
			*Interface->m_pPtr = U::Memory.FindInterface(sModule, Interface->m_sName);
			break;
		}
		case 1:
		{
			*Interface->m_pPtr = U::Memory.GetModuleExport<void*>(sModule, Interface->m_sName);
			break;
		}
		case 2:
		{
			auto dwDest = U::Memory.FindSignature(sModule, Interface->m_sName);
			if (!dwDest)
			{
				U::Core.AppendFailText(std::format("CInterfaces::Initialize() failed to find signature").c_str());
				break;
			}

			*Interface->m_pPtr = reinterpret_cast<void*>(U::Memory.RelToAbs(dwDest) + Interface->m_nOffset);
			break;
		}
		}

		for (int n = 0; n < Interface->m_nDereferenceCount; n++)
		{
			if (*Interface->m_pPtr)   // the slot's VALUE: a missed sig leaves it null
				*Interface->m_pPtr = *reinterpret_cast<void**>(*Interface->m_pPtr);
		}

		if (!*Interface->m_pPtr)
		{
			U::Core.AppendFailText(std::format("CInterfaces::Initialize() failed to initialize:\n  {}\n  {}", sModule, Interface->m_sName).c_str());
			m_bFailed = true;
		}
	}

	// The by-hand ones (KeyValuesSystem). A miss is logged and not fatal: only KeyValues
	// creation needs it, and that checks KeyValues::Available().
	H::Interfaces.Initialize();

	return !m_bFailed;
}

// ================ Utils/KeyHandler/KeyHandler.cpp ================
#include <Windows.h>

// ================ Utils/Memory/Memory.cpp ================
#include <format>
#include <Psapi.h>

std::vector<int> CMemory::PatternToInt(const char* szPattern)
{
	std::vector<int> vPattern = {};

	const auto pStart = const_cast<char*>(szPattern);
	const auto pEnd = const_cast<char*>(szPattern) + strlen(szPattern);
	for (char* pCurrent = pStart; pCurrent < pEnd; ++pCurrent)
	{
		if (*pCurrent == '?') // Is current byte a wildcard? Simply ignore that that byte later
		{
			++pCurrent;
			if (*pCurrent == '?') // Check if following byte is also a wildcard
				++pCurrent;

			vPattern.push_back(-1);
		}
		else
			vPattern.push_back(std::strtoul(pCurrent, &pCurrent, 16));
	}

	return vPattern;
}

uintptr_t CMemory::FindSignature(const char* szModule, const char* szPattern)
{
	if (const auto hModule = GetModuleHandle(szModule))
	{
		// Get module information to search in the given module
		MODULEINFO lpModuleInfo;
		if (!GetModuleInformation(GetCurrentProcess(), hModule, &lpModuleInfo, sizeof(MODULEINFO)))
			return 0x0;

		// The region where we will search for the byte sequence
		const auto dwImageSize = lpModuleInfo.SizeOfImage;

		// Check if the image is faulty
		if (!dwImageSize)
			return 0x0;

		// IDA-style signature -> bytes, then the memchr-anchored scan (sigscan.h, gate-tested)
		const auto pImageBytes = reinterpret_cast<const unsigned char*>(hModule);
		const auto iOffset = sigscan::Find(pImageBytes, dwImageSize, PatternToInt(szPattern));
		return iOffset < 0 ? 0x0 : uintptr_t(pImageBytes + iOffset);
	}

	return 0x0;
}

using CreateInterfaceFn = void*(*)(const char* pName, int* pReturnCode);

PVOID CMemory::FindInterface(const char* szModule, const char* szObject)
{
	const auto CreateInterface = GetModuleExport<CreateInterfaceFn>(szModule, "CreateInterface");
	return CreateInterface(szObject, nullptr);
}

std::string CMemory::GetModuleOffset(uintptr_t uAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(uAddress), &hModule))
		return std::format("{:#x}", uAddress);

	uintptr_t uBase = uintptr_t(hModule);
	if (char buffer[MAX_PATH]; GetModuleBaseName(GetCurrentProcess(), hModule, buffer, sizeof(buffer) / sizeof(char)))
		return std::format("{}+{:#x}", buffer, uAddress - uBase);

	return std::format("{:#x}+{:#x}", uBase, uAddress - uBase);
}

uintptr_t CMemory::GetOffsetFromBase(uintptr_t uAddress)
{
	HMODULE hModule;
	if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(uAddress), &hModule))
		return -1;

	uintptr_t uBase = uintptr_t(hModule);
	return uAddress - uBase;
}

// ================ Utils/NetVars/NetVars.cpp ================
#ifdef GetProp
	#undef GetProp
#endif

int CNetVars::GetOffset(RecvTable* pTable, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szNetVar);
	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		RecvProp* pProp = pTable->GetProp(i);
		if (uHash == FNV1A::Hash32(pProp->m_pVarName))
			return pProp->GetOffset();

		if (auto pDataTable = pProp->GetDataTable())
		{
			if (auto nOffset = GetOffset(pDataTable, szNetVar))
				return nOffset + pProp->GetOffset();
		}
	}

	return 0;
}

int CNetVars::GetNetVar(const char* szClass, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szClass);
	for (auto pCurrNode = I::Client->GetAllClasses(); pCurrNode; pCurrNode = pCurrNode->m_pNext)
	{
		if (uHash == FNV1A::Hash32(pCurrNode->m_pNetworkName))
			return GetOffset(pCurrNode->m_pRecvTable, szNetVar);
	}

	return 0;
}

RecvProp* CNetVars::GetProp(RecvTable* pTable, const char* szNetVar)
{
	auto uHash = FNV1A::Hash32(szNetVar);
	for (int i = 0; i < pTable->GetNumProps(); i++)
	{
		RecvProp* pProp = pTable->GetProp(i);
		if (uHash == FNV1A::Hash32(pProp->m_pVarName))
			return pProp;

		if (auto pDataTable = pProp->GetDataTable())
		{
			if (pProp = GetProp(pDataTable, szNetVar))
				return pProp;
		}
	}

	return nullptr;
}

// ================ Utils/Signatures/Signatures.cpp ================
#include <string>
#include <format>

CSignature::CSignature(const char* sDLLName, const char* sSignature, int8_t nOffset, const char* sName)
{
	m_dwVal = 0x0;
	m_sDLLName = sDLLName;
	m_sSignature = sSignature;
	m_nOffset = nOffset;
	m_sName = sName;

	U::Signatures.AddSignature(this);
}

bool CSignature::Initialize()
{
	m_dwVal = U::Memory.FindSignature(m_sDLLName, m_sSignature);
	if (!m_dwVal)
	{
		U::Core.AppendFailText(std::format("CSignature::Initialize() failed to initialize:\n  {}\n  {}\n  {}", m_sName, m_sDLLName, m_sSignature).c_str());
		return false;
	}

	m_dwVal += m_nOffset;
	return true;
}

int CSignatures::Initialize()
{
	int nMissed = 0;
	for (auto pSignature : m_vSignatures)
	{
		if (pSignature && !pSignature->Initialize())
			nMissed++;
	}
	return nMissed;
}

// ================ Utils/Timer/Timer.cpp ================
Timer::Timer()
{
	m_flLast = SDK::PlatFloatTime();
}

bool Timer::Check(float flS) const
{
	float flCurrentTime = SDK::PlatFloatTime();
	return flCurrentTime - m_flLast >= flS;
}

bool Timer::Run(float flS)
{
	if (Check(flS))
	{
		Update();
		return true;
	}
	return false;
}

void Timer::Update()
{
	m_flLast = SDK::PlatFloatTime();
}
