#include "aim.h"

#include "sdk.h"
#include "events.h"
#include "core.h"
#include "aimbot.h"
#include "menu.h"

#include "../../dependencies/imgui/imgui.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

void DebugLog(const char* fmt, ...);


// =============================================================================================
// load / unload
// =============================================================================================

static bool s_bActive = false;
static bool s_bTried  = false;

bool Aim_Active() { return s_bActive; }

// CBaseHudChat::ChatPrintf (vtable 19). Text goes in as the format: caller strips '%'.
void Aim_ChatPrint(const char* text)
{
	if (I::ClientModeShared && I::ClientModeShared->m_pChatElement)
		I::ClientModeShared->m_pChatElement->ChatPrintf(0, text);
}

// The hooks the aim half installs. Every other MAKE_HOOK in hooks.cpp is compiled (so it
// registers) but never initialised. Rules used to pick this list:
//   - it feeds the aimbot (entity cache, bone/anim fidelity, prediction), or
//   - it is what makes silent/psilent aim actually silent (the cmd encode + input read path), or
//   - it is the clean-POV-demo fix the user asked for (CPrediction::GetLocalViewAngles), and
//   - it is NOT a target dih already hooks (CreateMove is the exception: it moved here whole,
//     see CHLClient_CreateMove in hooks.cpp).
static const std::vector<std::string> s_vHookWhitelist =
{
	// command path -- the aimbot pipeline itself, and the tick manipulation it rides on
	"CHLClient_CreateMove",
	"CBasePlayer_ItemPostFrame",

	// silent aim: keep the engine from writing our aim angle back into the real view, and
	// keep the encoded command carrying the aim angle while the view keeps the real one.
	//
	// CInput_GetUserCmd and CInput_ValidateUserCmd are gone from this tree entirely: dih hooks
	// both itself, off the same signatures and with the same bodies (movement.cpp, h_GetUserCmd
	// / h_ValidateUserCmd -- they are what makes dih's own post-CreateMove button edits survive).
	// MinHook allows one hook per target, so installing them from both halves would just make
	// one of the two fail.
	//
	// IEngineTrace_TraceRay / IEngineTrace_SetTraceEntity are gone too. The TraceRay hook only
	// ever existed inside "#ifdef DEBUG_TRACES" (never defined), so whitelisting it installed
	// nothing, and SetTraceEntity's CONTENTS_NOSTARTSOLID restore read a mask that only the
	// never-whitelisted CM_BoxTrace hook ever wrote -- it was a pure passthrough.
	"CHLClient_EncodeUserCmdToBuffer",
	"CClientState_ProcessFixAngle",

	// clean POV demos -- the demo cmdinfo's local view angle
	"CPrediction_GetLocalViewAngles",

	// entity data the target selection and the projectile lead read. RecvProxy_SimulationTime
	// is what makes a choking player's simulation time correct, and H::Entities::Store builds
	// the per-target velocity history off that delta -- the projectile aimbot leads on it.
	"CHLClient_FrameStageNotify",
	"RecvProxy_SimulationTime",
	"CL_ProcessPacketEntities",

	// bone + animation fidelity for hitbox aiming
	"CBaseAnimating_UpdateClientSideAnimation",
	"CBaseAnimating_MaintainSequenceTransitions",
	"CBaseAnimating_Interpolate",
	"CBaseAnimating_SetSequence",
	"CBaseEntity_BaseInterpolatePart1",
	"CBaseEntity_ResetLatched",
	"CTFPlayer_BuildTransformations",

	// movement simulation (projectile lead) + engine prediction
	"CBaseEntity_SetAbsVelocity",
	"CBaseEntity_EstimateAbsVelocity",
	"CPrediction_RunSimulation",

	// weapon state the aimbot gates on
	"CTFPlayerShared_InCond",
	"CTFWeaponBase_CalcIsAttackCritical",
	"CTFWeaponBase_CanFireRandomCriticalShot",
	"CTFRocketLauncher_CheckReloadMisfire",

	// the aimbot's own on-screen overlays (tick indicator, crit / no-spread readout)
	"IEngineVGui_Paint",

	// silences the datatable spam the netvar walk provokes
	"DataTable_Warning",

	// eats the server's "playerperf" TextMsg reply no spread asks for -- without it the reply
	// is never parsed (no spread cannot sync) and every one prints to the console.
	"CHLClient_DispatchUserMessage",
};

bool Aim_Init()
{
	if (s_bTried)
		return s_bActive;
	s_bTried = true;

	if (!U::Signatures.Initialize())
	{
		DebugLog("aim: signature init failed");
		return false;
	}
	if (!U::Interfaces.Initialize())
	{
		DebugLog("aim: interface init failed");
		return false;
	}
	if (!H::Interfaces.Initialize())
		DebugLog("aim: null-interface init incomplete (steam side), continuing");

	if (!U::Hooks.Initialize(s_vHookWhitelist))
	{
		DebugLog("aim: hook init failed");
		return false;
	}
	if (!H::Events.Initialize())
		DebugLog("aim: event listener init incomplete, continuing");

	H::ConVars.Unlock();
	H::Fonts.Reload(Vars::Menu::Scale[DEFAULT_BIND]);
	// No config load here: dih's own LoadConfig already ran (MainThread, before PollThread) and
	// it carries our vars now -- see the config section below.

	s_bActive = true;
	DebugLog("aim: aimbot subsystem loaded (%d hooks)", int(s_vHookWhitelist.size()));
	return true;
}

void Aim_Unload()
{
	if (!s_bActive)
		return;

	G::Unload = true;
	U::Hooks.Unload();
	H::Events.Unload();
	H::ConVars.Restore();
	s_bActive = false;
}


// =============================================================================================
// stubs: fail-text sink + menu console line
// =============================================================================================

// Both sinks end in dih's debug.log. DebugLog is declared at the top of this file rather than
// by including globals.h -- that header drags the whole dih feature surface in with it.
void CCore::AppendFailText(const char* sMessage)
{
	DebugLog("aim: %s", sMessage ? sMessage : "(null)");
}

// The menu console line. dih has no such pane, so the line goes to debug.log --
// same information, same call sites, no ImGui dependency.
void CMenu::AddOutput(const char* sFunction, const char* sLog, Color_t tColor)
{
	std::lock_guard tLock(m_tMutex);

	m_vOutput.emplace_front(sFunction ? sFunction : "", sLog ? sLog : "", m_vOutput.size(), tColor);
	if (m_vOutput.size() > m_iMaxOutputSize)
		m_vOutput.pop_back();

	DebugLog("aim %s %s", sFunction ? sFunction : "", sLog ? sLog : "");
}


// =============================================================================================
// config: aim vars inside dih's own config file
// =============================================================================================

// Persistence for the aim vars, written INTO dih's own config file.
//
// The aim tree has no config file of its own: dih's SaveConfig / LoadConfig
// (src/hacks/menu/config.cpp) call the two functions here, so the aim settings live in
// C:otox-tf2\default.cfg next to everything else and the menu's existing save/load buttons cover
// them with no extra button.
//
// Keys are the var's own registered name ("Vars::Aimbot::General::AimFOV"), which is unique and
// contains no '=' -- dih's parser splits on the first one. Only the four scalar types the aim
// tree uses are handled; no aim var is a string, gradient or box.

// Every var whose name starts with one of these is ours. Vars.h registers the whole var tree
// (menu theme, ESP colours, ...) including entries no compiled feature reads, and writing all
// of it would fill dih's config with dead keys.
static bool IsAimVar(const std::string& sName)
{
	static const char* kPrefixes[] =
	{
		"Vars::Aimbot::",
		"Vars::CritHack::",
		"Vars::Misc::Game::CleanPOVDemos",
		"Vars::Misc::Game::AntiCheatCompatibility",
	};
	for (auto sPrefix : kPrefixes)
	{
		if (sName.rfind(sPrefix, 0) == 0)
			return true;
	}
	return false;
}

void Aim_SaveVars(FILE* f)
{
	if (!f)
		return;

	for (auto pBase : G::Vars)
	{
		if (pBase->m_iFlags & NOSAVE || !IsAimVar(pBase->m_sName))
			continue;

		if (auto pVar = pBase->As<bool>())
			fprintf(f, "%s=%d\n", pVar->m_sName.c_str(), pVar->Value ? 1 : 0);
		else if (auto pVar2 = pBase->As<int>())
			fprintf(f, "%s=%d\n", pVar2->m_sName.c_str(), pVar2->Value);
		else if (auto pVar3 = pBase->As<float>())
			fprintf(f, "%s=%.4f\n", pVar3->m_sName.c_str(), pVar3->Value);
		else if (auto pVar4 = pBase->As<Color_t>())
			fprintf(f, "%s=%d %d %d %d\n", pVar4->m_sName.c_str(),
				pVar4->Value.r, pVar4->Value.g, pVar4->Value.b, pVar4->Value.a);
	}
}

bool Aim_LoadVar(const char* szKey, const char* szValue)
{
	if (!szKey || !szValue)
		return false;
	// Cheap reject before the linear walk: every key of ours starts with the namespace.
	if (strncmp(szKey, "Vars::", 6) != 0)
		return false;

	for (auto pBase : G::Vars)
	{
		if (pBase->m_sName != szKey)
			continue;

		// Map[DEFAULT_BIND] is the value the config layer round-trips; Value is what the
		// features read. Both are set so a load takes effect without a menu interaction.
		if (auto pVar = pBase->As<bool>())
			pVar->Value = pVar->Map[DEFAULT_BIND] = atoi(szValue) != 0;
		else if (auto pVar2 = pBase->As<int>())
			pVar2->Value = pVar2->Map[DEFAULT_BIND] = atoi(szValue);
		else if (auto pVar3 = pBase->As<float>())
			pVar3->Value = pVar3->Map[DEFAULT_BIND] = float(atof(szValue));
		else if (auto pVar4 = pBase->As<Color_t>())
		{
			int r = 0, g = 0, b = 0, a = 0;
			if (sscanf(szValue, "%d %d %d %d", &r, &g, &b, &a) == 4)
				pVar4->Value = pVar4->Map[DEFAULT_BIND] = Color_t(byte(r), byte(g), byte(b), byte(a));
		}
		else
			return false;

		return true;
	}

	return false;
}


// =============================================================================================
// menu: the aimbot tab body
// =============================================================================================

// The aimbot tab in dih's own menu (src/hacks/menu/menu.cpp calls Aim_RenderMenuTab from its
// tab switch). It lives on this side because it reads the aim tree's ConfigVar objects, and
// those drag in Vars.h/SDK.h -- which dih's menu.cpp must not include.
//
// Nothing here hardcodes a label, a range or a dropdown entry. Every widget reads the metadata
// the CVar macro already recorded on the var (m_vTitle, m_vValues, m_unMin/Max/Step, m_sExtra,
// the SLIDER_*/DROPDOWN_* flags), so the options, their names and their limits come from the
// var declarations by construction and stay correct if one changes.
//
// Layout is botox tab_aimbot 1:1: menu_columns_begin/next/end + menu_group_begin/end boxes,
// a feature row is a Checkbox, its secondary tuning sits behind that row's cog (OptionPopup).
#include "../menu/menu_chrome.h"

// Can't include src/hacks/menu/menu.h here: it pulls in dih's own SDK (src/globals.h), which
// redefines type names (ConVar, RecvProp, ...) that aim/defs.h already defines differently --
// same wall as the MenuSection decls above used to work around. Signatures copied from there.
namespace ImGui
{
	void OptionPopup(const char* str_id, const std::function<void()>& function, const ImVec2& window_size);
	bool Keybind(const char* label, int* key, int* mode, bool has_style = true);
}
void UnitF_Menu_aimbot_config();   // unitF_menu.cpp
void UnitF_Menu_aimbot_lob();      // unitF_menu.cpp

// Rest of dih's menu is all lowercase, so every label the var metadata hands us gets folded
// here rather than in the declarations -- the vars keep their own capitalisation.
static std::string LowerLabel(const std::string& sText)
{
	std::string sOut = sText;
	for (char& c : sOut)
		c = char(std::tolower((unsigned char)c));
	return sOut;
}

// Var titles are sometimes "Label##uniqueid" and sometimes a { short, long } pair.
// Take the short one and cut at "##" -- ImGui would treat the tail as an id anyway, but the
// var name is a better id than a shared suffix.
static std::string VarLabel(BaseVar* pVar)
{
	std::string sTitle = pVar->m_vTitle.empty() ? pVar->m_sName : pVar->m_vTitle.front();
	if (auto i = sTitle.find("##"); i != std::string::npos)
		sTitle = sTitle.substr(0, i);
	return LowerLabel(sTitle) + "##" + pVar->m_sName;
}

static void VarToggle(ConfigVar<bool>& tVar)
{
	bool bValue = tVar.Value;
	if (ImGui::Checkbox(VarLabel(&tVar).c_str(), &bValue))
		tVar.Value = tVar.Map[DEFAULT_BIND] = bValue;
}

static void VarSlider(ConfigVar<float>& tVar)
{
	float flValue = tVar.Value;
	const char* szFormat = tVar.m_sExtra ? tVar.m_sExtra : "%g";
	// A value already outside the declared range keeps the bar draggable instead of
	// pinning the thumb at an end -- same rule as dih's own sliders.
	float flMin = tVar.m_unMin.f, flMax = tVar.m_unMax.f;
	if (flValue < flMin) flMin = flValue;
	if (flValue > flMax) flMax = flValue;
	if (ImGui::SliderFloat(VarLabel(&tVar).c_str(), &flValue, flMin, flMax, szFormat))
		tVar.Value = tVar.Map[DEFAULT_BIND] = flValue;
}

static void VarSlider(ConfigVar<int>& tVar)
{
	int iValue = tVar.Value;
	const char* szFormat = tVar.m_sExtra ? tVar.m_sExtra : "%d";
	int iMin = tVar.m_unMin.i, iMax = tVar.m_unMax.i;
	if (iValue < iMin) iMin = iValue;
	if (iValue > iMax) iMax = iValue;
	if (ImGui::SliderInt(VarLabel(&tVar).c_str(), &iValue, iMin, iMax, szFormat))
		tVar.Value = tVar.Map[DEFAULT_BIND] = iValue;
}

// Single-select enum: m_vValues in declaration order, value == index.
static void VarCombo(ConfigVar<int>& tVar)
{
	if (tVar.m_vValues.empty())
		return;

	// Names must outlive the Combo call, so keep the lowercased strings alive next to the
	// pointer array ImGui reads.
	std::vector<std::string> vNames;
	for (auto szItem : tVar.m_vValues)
	{
		if (!szItem || std::string(szItem) == "##Divider")
			continue;
		vNames.push_back(LowerLabel(szItem));
	}

	std::vector<const char*> vItems;
	for (auto& sName : vNames)
		vItems.push_back(sName.c_str());

	int iValue = std::clamp(tVar.Value, 0, int(vItems.size()) - 1);
	if (ImGui::Combo(VarLabel(&tVar).c_str(), &iValue, vItems.data(), int(vItems.size())))
		tVar.Value = tVar.Map[DEFAULT_BIND] = iValue;
}

// Bitmask dropdown (DROPDOWN_MULTI): one bit per NON-divider entry, in order. Dividers are
// layout only and do NOT consume a bit -- e.g. Hitscan::Hitboxes goes
// Head..Legs = bits 0-4, "##Divider", then BodyaimIfLethal = bit 5.
static void VarMulti(ConfigVar<int>& tVar)
{
	if (tVar.m_vValues.empty())
		return;

	// Preview: the selected names, or the var's own "none" label when nothing is set.
	std::string sPreview;
	{
		int iBit = 0;
		for (auto szItem : tVar.m_vValues)
		{
			if (!szItem || std::string(szItem) == "##Divider")
				continue;
			if (tVar.Value & (1 << iBit))
				sPreview += (sPreview.empty() ? "" : ", ") + LowerLabel(szItem);
			iBit++;
		}
		if (sPreview.empty())
			sPreview = LowerLabel(tVar.m_sExtra ? tVar.m_sExtra : "none");
	}

	if (ImGui::BeginCombo(VarLabel(&tVar).c_str(), sPreview.c_str()))
	{
		int iBit = 0;
		for (auto szItem : tVar.m_vValues)
		{
			if (!szItem || std::string(szItem) == "##Divider")
			{
				ImGui::Separator();
				continue;
			}

			const int iMask = 1 << iBit++;
			const bool bOn = (tVar.Value & iMask) != 0;
			// botox MultiCombo rows: Selectable, toggles without closing
			if (ImGui::Selectable((LowerLabel(szItem) + "##" + tVar.m_sName).c_str(), bOn, ImGuiSelectableFlags_DontClosePopups))
				tVar.Value = tVar.Map[DEFAULT_BIND] = tVar.Value ^ iMask;
		}
		ImGui::EndCombo();
	}
}


void Aim_RenderMenuTab()
{
	using namespace Vars;

	if (!Aim_Active())
	{
		ImGui::TextDisabled("aimbot subsystem not loaded -- see C:\\botox-tf2\\debug.log");
		return;
	}

	menu_columns_begin();

	// ---- main -------------------------------------------------------------------------
	if (menu_group_begin("main"))
	{
		VarToggle(Aimbot::General::Enabled);
		ImGui::Keybind("aim key", &Aimbot::General::AimKey.Value, &Aimbot::General::AimKeyMode.Value);
		Aimbot::General::AimKey.Map[DEFAULT_BIND] = Aimbot::General::AimKey.Value;
		Aimbot::General::AimKeyMode.Map[DEFAULT_BIND] = Aimbot::General::AimKeyMode.Value;

		VarCombo(Aimbot::General::AimType);
		VarSlider(Aimbot::General::AimFOV);
		VarSlider(Aimbot::General::MaxTargets);
		if (Aimbot::General::AimType.Value == Aimbot::General::AimTypeEnum::Smooth
			|| Aimbot::General::AimType.Value == Aimbot::General::AimTypeEnum::Assistive)
			VarSlider(Aimbot::General::AssistStrength);
		VarToggle(Aimbot::General::AutoShoot);
		VarToggle(Aimbot::General::NoSpread);

		VarMulti(Aimbot::General::Target);
		VarMulti(Aimbot::General::Ignore);
		if (Aimbot::General::Ignore.Value & Aimbot::General::IgnoreEnum::Invisible)
			VarSlider(Aimbot::General::IgnoreInvisible);
		if (Aimbot::General::Ignore.Value & Aimbot::General::IgnoreEnum::Unsimulated)
			VarSlider(Aimbot::General::TickTolerance);

		VarToggle(CritHack::ForceCrits);
		VarToggle(CritHack::AvoidRandomCrits);
		VarToggle(CritHack::AlwaysMeleeCrit);
	}
	menu_group_end();

	UnitF_Menu_aimbot_config();

	menu_columns_next();

	// ---- hitscan / projectile / melee / backstab / demos, each a feature row + its cog ----
	if (menu_group_begin("hitscan"))
	{
		VarToggle(Aimbot::Hitscan::Enabled);
		if (Aimbot::Hitscan::Enabled.Value)
			ImGui::OptionPopup("hitscan settings", [&]()
			{
				VarMulti(Aimbot::Hitscan::Hitboxes);
				VarMulti(Aimbot::Hitscan::MultipointHitboxes);
				VarSlider(Aimbot::Hitscan::MultipointScale);
				VarMulti(Aimbot::Hitscan::Modifiers);
				if (Aimbot::Hitscan::Modifiers.Value & Aimbot::Hitscan::ModifiersEnum::Tapfire)
					VarSlider(Aimbot::Hitscan::TapfireDistance);
			}, ImVec2(260.f, -1));
	}
	menu_group_end();

	if (menu_group_begin("projectile"))
	{
		VarToggle(Aimbot::Projectile::Enabled);
		if (Aimbot::Projectile::Enabled.Value)
			ImGui::OptionPopup("projectile settings", [&]()
			{
				// Its own FOV: a projectile aims at a LEAD, so the aim point sits well off the
				// crosshair and the hitscan cone would refuse every shot worth taking.
				VarSlider(Aimbot::Projectile::AimFOV);
				VarMulti(Aimbot::Projectile::Hitboxes);
				VarSlider(Aimbot::Projectile::MaxSimulationTime);

				VarMulti(Aimbot::Projectile::StrafePrediction);
				if (Aimbot::Projectile::StrafePrediction.Value)
					VarSlider(Aimbot::Projectile::HitChance);
				VarCombo(Aimbot::Projectile::SplashPrediction);
				VarSlider(Aimbot::Projectile::SplashRadius);

				VarMulti(Aimbot::Projectile::Modifiers);
				VarMulti(Aimbot::Projectile::AutoDetonate);
				VarSlider(Aimbot::Projectile::AutodetRadius);
				VarMulti(Aimbot::Projectile::AutoAirblast);
				VarSlider(Aimbot::Projectile::AutoRelease);
			}, ImVec2(260.f, -1));
	}
	menu_group_end();

	if (menu_group_begin("melee"))
	{
		VarToggle(Aimbot::Melee::Enabled);
		if (Aimbot::Melee::Enabled.Value)
			ImGui::OptionPopup("melee settings", [&]()
			{
				VarToggle(Aimbot::Melee::SwingPrediction);
			}, ImVec2(200.f, -1));
	}
	menu_group_end();

	// Own group, not nested under melee: auto backstab runs with the master aimbot, the aim
	// key and melee aim all off (see CAimbot::RunMain / CAimbotMelee::Run).
	if (menu_group_begin("auto backstab"))
	{
		VarToggle(Aimbot::Melee::AutoBackstab);
		// Own bind on its own row -- the master aim key never gated this feature.
		ImGui::Keybind("backstab key", &Aimbot::Melee::BackstabKey.Value, &Aimbot::Melee::BackstabKeyMode.Value);
		Aimbot::Melee::BackstabKey.Map[DEFAULT_BIND] = Aimbot::Melee::BackstabKey.Value;
		Aimbot::Melee::BackstabKeyMode.Map[DEFAULT_BIND] = Aimbot::Melee::BackstabKeyMode.Value;
		if (Aimbot::Melee::AutoBackstab.Value)
			ImGui::OptionPopup("auto backstab settings", [&]()
			{
				VarCombo(Aimbot::Melee::BackstabAimType);
				// Silent only: Legit never moves the view, so a cone there could only refuse.
				if (Aimbot::Melee::BackstabAimType.Value == Aimbot::Melee::BackstabAimTypeEnum::Silent)
					VarSlider(Aimbot::Melee::BackstabFOV);
				VarMulti(Aimbot::Melee::BackstabIgnore);
				if (Aimbot::Melee::BackstabIgnore.Value & Aimbot::Melee::BackstabIgnoreEnum::Invisible)
					VarSlider(Aimbot::General::IgnoreInvisible);
				// dih-bs11/bs13: the ten tuning checkboxes + the auto shoot tick are gone and must
				// not come back -- see Vars::Aimbot::Melee declaration comments for why.
			}, ImVec2(220.f, -1));
	}
	menu_group_end();

	// CPrediction::GetLocalViewAngles hook: hands the engine's demo recorder the REAL view
	// angle instead of the silent-aim one, so a recorded POV demo does not snap to targets.
	if (menu_group_begin("demos"))
	{
		VarToggle(Misc::Game::CleanPOVDemos);
		if (Misc::Game::CleanPOVDemos.Value)
			ImGui::OptionPopup("demos settings", [&]()
			{
				VarToggle(Misc::Game::AntiCheatCompatibility);
			}, ImVec2(220.f, -1));
	}
	menu_group_end();

	UnitF_Menu_aimbot_lob();

	menu_columns_end();
}
