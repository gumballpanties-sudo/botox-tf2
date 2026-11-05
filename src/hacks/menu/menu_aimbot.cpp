#include "menu_aimbot.h"

#include "../aim/vars.h"
#include "../../dependencies/imgui/imgui.h"
#include "menu_chrome.h"   // menu_columns_begin/next/end, menu_group_begin/end -- engine-free, no SDK clash

#include <functional>

// ImGui::OptionPopup/Keybind/ColorRow bodies live in menu.cpp; can't include src/hacks/menu/menu.h
// here (it pulls in dih's own SDK via globals.h, which redefines type names aim/vars.h's SDK
// already defines differently -- same wall aim.cpp works around with local decls).
namespace ImGui
{
	void OptionPopup(const char* str_id, const std::function<void()>& function, const ImVec2& window_size);
	bool Keybind(const char* label, int* key, int* mode, bool has_style = true);
	bool ColorRow(const char* label, float col[4], ImGuiColorEditFlags flags = 0, int swatch_slot = 0);
}

static void VarCheckbox(ConfigVar<bool>& v, const char* label)
{
	bool b = v.Value;
	if (ImGui::Checkbox(label, &b))
		v.Value = v.Map[DEFAULT_BIND] = b;
}

static void VarSliderFloat(ConfigVar<float>& v, const char* label)
{
	float f = v.Value;
	const char* szFormat = v.m_sExtra ? v.m_sExtra : "%g";
	if (ImGui::SliderFloat(label, &f, v.m_unMin.f, v.m_unMax.f, szFormat))
		v.Value = v.Map[DEFAULT_BIND] = f;
}

// Color_t is bytes (0-255); ColorRow/ColorEdit4 want floats (0-1) -- convert both ways.
static void VarColor(ConfigVar<Color_t>& v, const char* label)
{
	float col[4] = { v.Value.r / 255.f, v.Value.g / 255.f, v.Value.b / 255.f, v.Value.a / 255.f };
	if (ImGui::ColorRow(label, col, ImGuiColorEditFlags_AlphaBar))
	{
		Color_t c{ (byte)(col[0] * 255.f + 0.5f), (byte)(col[1] * 255.f + 0.5f),
			(byte)(col[2] * 255.f + 0.5f), (byte)(col[3] * 255.f + 0.5f) };
		v.Value = v.Map[DEFAULT_BIND] = c;
	}
}

// botox tab_aimbot "configuration" group (scope / per-weapon, each behind its cog).
// Groups only: Aim_RenderMenuTab owns the one column pair (a second pair lands off-screen).
void Menu_Aimbot_Config()
{
	using namespace Vars::Aimbot;

	if (menu_group_begin("configuration"))
	{
		VarCheckbox(ScopeFOV::Enabled, "scope fov scale");
		if (ScopeFOV::Enabled.Value)
			ImGui::OptionPopup("scope fov settings", [&]()
			{
				VarSliderFloat(ScopeFOV::Scale, "scale##scopefov");
			}, ImVec2(200.f, -1));

		VarCheckbox(WeaponTuning::Enabled, "per-weapon tuning");
		if (WeaponTuning::Enabled.Value)
			ImGui::OptionPopup("weapon tuning settings", [&]()
			{
				VarSliderFloat(WeaponTuning::SniperFOV, "sniper rifle fov");
				VarSliderFloat(WeaponTuning::MinigunFOV, "minigun fov");
				VarSliderFloat(WeaponTuning::ScattergunFOV, "scattergun fov");
			}, ImVec2(220.f, -1));
	}
	menu_group_end();
}
