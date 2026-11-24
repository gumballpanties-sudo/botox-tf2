#pragma once
#include "defs.h"
#include "utils.h"
#include <windows.h>
#include <unordered_map>
#include <map>
#include <typeinfo>

#define DEFAULT_BIND -1

// forward declartion of ConfigVar
template <class T>
class ConfigVar;

class BaseVar
{
public:
	size_t m_iType;
	std::string m_sName;
	int m_iFlags = 0;

	std::vector<const char*> m_vTitle;
	const char* m_sSection;
	union {
		int i = 0;
		float f;
	} m_unMin;
	union {
		int i = 0;
		float f;
	} m_unMax;
	union {
		int i = 0;
		float f;
	} m_unStep;
	std::vector<const char*> m_vValues = {};
	const char* m_sExtra = nullptr;

	// getter for ConfigVar
	template <class T>
	inline ConfigVar<T>* As()
	{
		if (typeid(T).hash_code() != m_iType)
			return nullptr;

		return reinterpret_cast<ConfigVar<T>*>(this);
	}
};

namespace G
{
	inline std::vector<BaseVar*> Vars = {};
};

template <class T>
class ConfigVar : public BaseVar
{
public:
	T Default;
	T Value;
	std::unordered_map<int, T> Map = {};
	ConfigVar(T tValue, std::string sName, const char* sSection, std::vector<const char*> vTitle, int iFlags = 0, std::vector<const char*> vValues = {}, const char* sNone = nullptr)
	{
		Default = tValue;
		Value = tValue;
		Map[DEFAULT_BIND] = tValue;

		m_iType = typeid(T).hash_code();
		m_sName = sName;
		m_iFlags = iFlags;

		m_vTitle = vTitle;
		m_sSection = sSection;
		m_vValues = vValues;
		m_sExtra = sNone;

		G::Vars.push_back(this);
	}
	ConfigVar(T tValue, std::string sName, const char* sSection, std::vector<const char*> vTitle, int iFlags, int iMin, int iMax, int iStep = 1, const char* sFormat = "%i")
	{
		Default = tValue;
		Value = tValue;
		Map[DEFAULT_BIND] = tValue;

		m_iType = typeid(T).hash_code();
		m_sName = sName;
		m_iFlags = iFlags;

		m_vTitle = vTitle;
		m_sSection = sSection;
		m_unMin.i = iMin;
		m_unMax.i = iMax;
		m_unStep.i = iStep;
		m_sExtra = sFormat;

		G::Vars.push_back(this);
	}
	ConfigVar(T tValue, std::string sName, const char* sSection, std::vector<const char*> vTitle, int iFlags, float flMin, float flMax, float flStep = 1.f, const char* sFormat = "%g")
	{
		Default = tValue;
		Value = tValue;
		Map[DEFAULT_BIND] = tValue;

		m_iType = typeid(T).hash_code();
		m_sName = sName;
		m_iFlags = iFlags;

		m_vTitle = vTitle;
		m_sSection = sSection;
		m_unMin.f = flMin;
		m_unMax.f = flMax;
		m_unStep.f = flStep;
		m_sExtra = sFormat;

		G::Vars.push_back(this);
	}

	inline T& operator[](int i)
	{
		return Map[i];
	}
	inline bool contains(int i) const
	{
		return Map.contains(i);
	}
};

#define NAMESPACE_BEGIN(name, ...)\
	namespace name {\
		inline const char* GetNamespace() { return "Vars::"#name"::"; }\
		inline const char* GetSubname() { return ""; }\
		inline const char* GetSection() { return !std::string(#__VA_ARGS__).empty() ? ""#__VA_ARGS__ : #name; }

#define SUBNAMESPACE_BEGIN(name, ...)\
	namespace name {\
		inline const char* GetSubname() { return #name"::"; }\
		inline const char* GetSection() { return !std::string(#__VA_ARGS__).empty() ? ""#__VA_ARGS__ : #name; }

#define NAMESPACE_END(name)\
	}
#define SUBNAMESPACE_END(name)\
	}

#define CVar(name, title, value, ...)\
	inline ConfigVar<decltype(value)> name = { value, std::format("{}{}{}", GetNamespace(), GetSubname(), #name), GetSection(), { title }, __VA_ARGS__ }
#define CVarValues(name, title, value, flags, none, ...)\
	inline ConfigVar<decltype(value)> name = { value, std::format("{}{}{}", GetNamespace(), GetSubname(), #name), GetSection(), { title }, flags, { __VA_ARGS__ }, none }
#define Enum(name, ...)\
	namespace name##Enum { enum name##Enum { __VA_ARGS__ }; }
#define CVarEnum(name, title, value, flags, none, values, ...)\
	CVarValues(name, title, value, flags, none, values);\
	Enum(name, __VA_ARGS__);

#define NONE 0
#define VISUAL (1 << 31)
#define NOSAVE (1 << 30)
#define NOBIND (1 << 29)
#define DEBUGVAR (1 << 28)

// flags to be automatically used in widgets. keep these as the same values as the flags in components, do not include visual flags
#define SLIDER_CLAMP (1 << 2)
#define SLIDER_MIN (1 << 3)
#define SLIDER_MAX (1 << 4)
#define SLIDER_PRECISION (1 << 5)
#define SLIDER_NOAUTOUPDATE (1 << 6)
#define DROPDOWN_MULTI (1 << 2)
#define DROPDOWN_MODIFIABLE (1 << 3)
#define DROPDOWN_CUSTOM (1 << 2)
#define DROPDOWN_AUTOUPDATE (1 << 3)

namespace Vars
{
	NAMESPACE_BEGIN(Menu)

		CVarEnum(Indicators, "Indicators", 0b00000, VISUAL | DROPDOWN_MULTI, nullptr,
			VA_LIST("Ticks", "Crit hack", "Spectators", "Ping", "Conditions", "Seed prediction"),
			Ticks = 1 << 0, CritHack = 1 << 1, Spectators = 1 << 2, Ping = 1 << 3, Conditions = 1 << 4, SeedPrediction = 1 << 5);

		CVar(TicksDisplay, "Ticks display", DragBox_t(), VISUAL | NOBIND);
		CVar(CritsDisplay, "Crits display", DragBox_t(), VISUAL | NOBIND);
		CVar(SeedPredictionDisplay, "Seed prediction display", DragBox_t(), VISUAL | NOBIND);

		CVar(Scale, "Scale", 1.f, NOBIND | SLIDER_MIN | SLIDER_PRECISION | SLIDER_NOAUTOUPDATE, 0.75f, 2.f, 0.25f);
		CVar(CheapText, "Cheap text", false);

		SUBNAMESPACE_BEGIN(Theme)
			CVar(Accent, "Accent color", Color_t(200, 25, 25, 255), VISUAL);
			CVar(Background, "Drop down bg color", Color_t(0, 0, 0, 250), VISUAL);
			CVar(Active, "Active color", Color_t(255, 255, 255, 255), VISUAL);
			CVar(Inactive, "Inactive color", Color_t(150, 150, 150, 255), VISUAL);
		SUBNAMESPACE_END(Theme);

		// One shared background style for every overlay panel (main menu, watermark, media player,
		// pixel-surf point boxes, the Minimal spectator list and the Checkpoints window): a solid
		SUBNAMESPACE_BEGIN(Style)
			// Stacked panel outlines (up to 4), each drawn just outside the previous - port of
			// the OutlineLayer stack. OutlineCount = how many layers are active (0 = off).
			CVar(OutlineCount, "Outline layers", 0, VISUAL | SLIDER_MIN, 0, 4);
			CVar(OutlineColor1, "Outline 1 color", Color_t(0, 0, 0, 255), VISUAL);
			CVar(OutlineThickness1, "Outline 1 thickness", 1.f, VISUAL | SLIDER_PRECISION, 0.5f, 8.f, 0.5f, "%.1f");
			CVar(OutlineBlur1, "Outline 1 blur", 0.f, VISUAL | SLIDER_PRECISION, 0.f, 16.f, 0.5f, "%.1f");
			CVar(OutlineColor2, "Outline 2 color", Color_t(0, 0, 0, 200), VISUAL);
			CVar(OutlineThickness2, "Outline 2 thickness", 1.f, VISUAL | SLIDER_PRECISION, 0.5f, 8.f, 0.5f, "%.1f");
			CVar(OutlineBlur2, "Outline 2 blur", 0.f, VISUAL | SLIDER_PRECISION, 0.f, 16.f, 0.5f, "%.1f");
			CVar(OutlineColor3, "Outline 3 color", Color_t(0, 0, 0, 150), VISUAL);
			CVar(OutlineThickness3, "Outline 3 thickness", 1.f, VISUAL | SLIDER_PRECISION, 0.5f, 8.f, 0.5f, "%.1f");
			CVar(OutlineBlur3, "Outline 3 blur", 0.f, VISUAL | SLIDER_PRECISION, 0.f, 16.f, 0.5f, "%.1f");
			CVar(OutlineColor4, "Outline 4 color", Color_t(0, 0, 0, 100), VISUAL);
			CVar(OutlineThickness4, "Outline 4 thickness", 1.f, VISUAL | SLIDER_PRECISION, 0.5f, 8.f, 0.5f, "%.1f");
			CVar(OutlineBlur4, "Outline 4 blur", 0.f, VISUAL | SLIDER_PRECISION, 0.f, 16.f, 0.5f, "%.1f");
			// Custom font (used when FontPreset == Custom): an installed family name ("Tahoma",
			// "Segoe UI") or a file name/stem from the Fonts folders; blank = falls back to Kodchasan.
			CVar(Font, "Custom font", std::string(""), VISUAL);
		SUBNAMESPACE_END(Style);
	NAMESPACE_END(Menu);

	NAMESPACE_BEGIN(Colors)
		CVar(Local, "Local color", Color_t(255, 255, 255, 0), VISUAL);

		CVar(IndicatorTextGood, "Indicator text good", Color_t(150, 255, 150, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextMid, "Indicator text mid", Color_t(255, 200, 0, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextBad, "Indicator text bad", Color_t(255, 150, 150, 255), NOSAVE | DEBUGVAR);
		CVar(IndicatorTextMisc, "Indicator text misc", Color_t(100, 255, 255, 255), NOSAVE | DEBUGVAR);

		CVar(Line, "Line color", Color_t(255, 255, 255, 255), VISUAL);
		CVar(LineIgnoreZ, "Line ignore Z color", Color_t(255, 255, 255, 0), VISUAL);

		CVar(PlayerPath, "Player path color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(PlayerPathIgnoreZ, "Player path ignore Z color", Color_t(255, 255, 255, 255), VISUAL);
		CVar(ProjectilePath, "Projectile path color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(ProjectilePathIgnoreZ, "Projectile path ignore Z color", Color_t(255, 255, 255, 255), VISUAL);
		CVar(SplashRadius, "Splash radius color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(RealPath, "Real path color", Color_t(255, 255, 255, 0), NOSAVE | DEBUGVAR);
		CVar(RealPathIgnoreZ, "Real path ignore Z color", Color_t(255, 255, 255, 255), NOSAVE | DEBUGVAR);

		CVar(BoundHitboxEdge, "Bound hitbox edge color", Color_t(255, 255, 255, 255), VISUAL);
		CVar(BoundHitboxEdgeIgnoreZ, "Bound hitbox edge ignore Z color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(BoundHitboxFace, "Bound hitbox face color", Color_t(255, 255, 255, 0), VISUAL);
		CVar(BoundHitboxFaceIgnoreZ, "Bound hitbox face ignore Z color", Color_t(255, 255, 255, 0), VISUAL);

	NAMESPACE_END(Colors);

	NAMESPACE_BEGIN(Aimbot)
		SUBNAMESPACE_BEGIN(General, Aimbot)
			// Master enable, so the menu's aimbot row is a feature checkbox like every other
			// dih feature. AimType is HOW to aim, not whether.
			CVar(Enabled, "Aimbot", false);
			CVarEnum(AimType, "Aim type", 0, NONE, nullptr,
				VA_LIST("Off", "Plain", "Smooth", "Silent", "Locking", "Assistive"),
				Off, Plain, Smooth, Silent, Locking, Assistive);
			// Target selection is always FOV, so the sort method is not a var --
			// CAimbotGlobal::SortTargets takes the enum directly, and the melee paths pass
			// Distance explicitly because the knife wants the nearest target, not the centred one.
			CVarEnum(TargetSelection, "Target selection", 0, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST("FOV", "Distance"),
				FOV, Distance);
			CVarEnum(Target, "Target", 0b0000001, DROPDOWN_MULTI, nullptr,
				VA_LIST("Players", "Sentries", "Dispensers", "Teleporters", "Stickies", "NPCs", "Bombs"),
				Players = 1 << 0, Sentry = 1 << 1, Dispenser = 1 << 2, Teleporter = 1 << 3, Stickies = 1 << 4, NPCs = 1 << 5, Bombs = 1 << 6,
				Building = Sentry | Dispenser | Teleporter);
			// Bits run from 0 with no gaps, and Melee::BackstabIgnore below MUST use the same
			// layout: CAimbotGlobal::ShouldIgnore reads both masks through these IgnoreEnum names.
			CVarEnum(Ignore, "Ignore", 0b0000001, DROPDOWN_MULTI, nullptr,
				VA_LIST("Invulnerable", "Invisible", "Unsimulated", "Dead ringer", "Vaccinator", "Disguised", "Taunting"),
				Invulnerable = 1 << 0, Invisible = 1 << 1, Unsimulated = 1 << 2, DeadRinger = 1 << 3, Vaccinator = 1 << 4, Disguised = 1 << 5, Taunting = 1 << 6);
			CVar(AimFOV, VA_LIST("Aim FOV", "Hitscan aim FOV"), 30.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			// THE KNIFE NEVER TRIMS THIS LIST AT ALL (CAimbotMelee::SortTargets) -- with enemies
			// stacked on each other the nearest is often the one you CANNOT stab while the man
			// behind him is wide open, and the server's own swing trace picks whoever it reaches
			// first. So auto backstab is already "everyone", whatever this reads.
			// THIS CAP IS THE GUN PATHS', AND THERE IT IS A FRAME BUDGET: hitscan runs traces per
			// hitbox per target and projectile SIMULATES a shot per target, so setting it to 999
			// asks for that work against every player on the server, every command. The top of the
			// range is 999 because the user asked for it; the default is not, because the default
			// is what runs while nobody is watching the frame time.
			CVar(MaxTargets, "Max targets", 6, SLIDER_MIN, 1, 999);
			CVar(IgnoreInvisible, "Ignore invisible", 50.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 10.f, "%g%%");
			CVar(AssistStrength, "Assist strength", 25.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 1.f, "%g%%");
			CVar(TickTolerance, "Tick tolerance", 4, SLIDER_CLAMP, 0, 21);
			CVar(AutoShoot, "Auto shoot", false);
			CVar(NoSpread, "No spread", false);

			// activation bind -- mode 0 = always on, 1 = hold key, 2 = toggle.
			// Read by CAimbot::Run through AimbotKeyOK(); nothing downstream knows about it.
			CVar(AimKey, "Aim key", 0, NOBIND);
			CVarEnum(AimKeyMode, "Aim key mode", 1, NOBIND, nullptr,
				VA_LIST("Always on", "On hotkey", "Toggle"),
				AlwaysOn, OnHotkey, Toggle);

			CVar(NoSpreadOffset, "No spread offset", 0.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1.f, 1.f, 0.1f);
			CVar(NoSpreadAverage, "No spread average", 5, NOSAVE | DEBUGVAR | SLIDER_MIN, 1, 25);
			CVar(NoSpreadInterval, "No spread interval", 0.1f, NOSAVE | DEBUGVAR | SLIDER_MIN, 0.05f, 5.f, 0.1f, "%gs");
			CVar(NoSpreadBackupInterval, "No spread backup interval", 2.f, NOSAVE | DEBUGVAR | SLIDER_MIN, 2.f, 10.f, 0.1f, "%gs");
		SUBNAMESPACE_END(Global);

		SUBNAMESPACE_BEGIN(Hitscan)
			// per weapon-type enable (the menu section's own checkbox).
			CVar(Enabled, VA_LIST("Hitscan", "Hitscan aimbot"), true);
			CVarEnum(Hitboxes, VA_LIST("Hitboxes", "Hitscan hitboxes"), 0b000111, DROPDOWN_MULTI, nullptr,
				VA_LIST("Head", "Body", "Pelvis", "Arms", "Legs", "##Divider", "Bodyaim if lethal", "Headshot only"),
				Head = 1 << 0, Body = 1 << 1, Pelvis = 1 << 2, Arms = 1 << 3, Legs = 1 << 4, BodyaimIfLethal = 1 << 5, HeadshotOnly = 1 << 6);
			CVarValues(MultipointHitboxes, "Multipoint hitboxes", 0b00000, DROPDOWN_MULTI, "All",
				VA_LIST("Head", "Body", "Pelvis", "Arms", "Legs"));
			CVarEnum(Modifiers, VA_LIST("Modifiers", "Hitscan modifiers"), 0b0100000, DROPDOWN_MULTI, nullptr,
				VA_LIST("Tapfire", "Wait for headshot", "Wait for charge", "Scoped only", "Auto scope", "Auto rev minigun", "Extinguish team"),
				Tapfire = 1 << 0, WaitForHeadshot = 1 << 1, WaitForCharge = 1 << 2, ScopedOnly = 1 << 3, AutoScope = 1 << 4, AutoRev = 1 << 5, ExtinguishTeam = 1 << 6);
			CVar(MultipointScale, "Multipoint scale", 0.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");
			CVar(TapfireDistance, "Tapfire distance", 1000.f, SLIDER_MIN | SLIDER_PRECISION, 250.f, 1000.f, 50.f);

			CVarEnum(PeekCheck, "Peek check", 1, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST("Off", "Doubletap only", "Always"),
				Off, DoubletapOnly, Always);
			CVar(PeekAmount, "Peek amount", 1, NOSAVE | DEBUGVAR, 0, 5);
			CVar(BoneSizeSubtract, "Bone size subtract", 1.f, NOSAVE | DEBUGVAR | SLIDER_MIN, 0.f, 4.f, 0.25f);
			CVar(BoneSizeMinimumScale, "Bone size minimum scale", 1.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP, 0.f, 1.f, 0.1f);
		SUBNAMESPACE_END(HITSCAN);

		SUBNAMESPACE_BEGIN(Projectile)
			// per weapon-type enable (the menu section's own checkbox).
			CVar(Enabled, VA_LIST("Projectile", "Projectile aimbot"), true);
			CVarEnum(StrafePrediction, VA_LIST("Predict", "Strafe prediction"), 0b11, DROPDOWN_MULTI, "Off",
				VA_LIST("Air strafing", "Ground strafing"),
				Air = 1 << 0, Ground = 1 << 1);
			CVarEnum(SplashPrediction, VA_LIST("Splash", "Splash prediction"), 0, NONE, nullptr,
				VA_LIST("Off", "Include", "Prefer", "Only"),
				Off, Include, Prefer, Only);
			CVarEnum(AutoDetonate, "Auto detonate", 0b00, DROPDOWN_MULTI, "Off",
				VA_LIST("Stickies", "Flares", "##Divider", "Prevent self damage", "Ignore invisible"),
				Stickies = 1 << 0, Flares = 1 << 1, PreventSelfDamage = 1 << 2, IgnoreInvisible = 1 << 3);
			CVarEnum(AutoAirblast, "Auto airblast", 0b000, DROPDOWN_MULTI, "Off", // todo: implement advanced redirect!!
				VA_LIST("Enabled", "##Divider", "Redirect", "Ignore FOV"),
				Enabled = 1 << 0, Redirect = 1 << 1, IgnoreFOV = 1 << 2);
			CVarEnum(Hitboxes, VA_LIST("Hitboxes", "Projectile hitboxes"), 0b001111, DROPDOWN_MULTI, nullptr,
				VA_LIST("Auto", "##Divider", "Head", "Body", "Feet", "##Divider", "Bodyaim if lethal", "Prioritize feet"),
				Auto = 1 << 0, Head = 1 << 1, Body = 1 << 2, Feet = 1 << 3, BodyaimIfLethal = 1 << 4, PrioritizeFeet = 1 << 5);
			CVarEnum(Modifiers, VA_LIST("Modifiers", "Projectile modifiers"), 0b1010, DROPDOWN_MULTI, nullptr,
				VA_LIST("Charge shot", "Cancel charge", "Use prime time"),
				ChargeWeapon = 1 << 0, CancelCharge = 1 << 1, UsePrimeTime = 1 << 2);
			// projectiles need a much wider cone than hitscan -- the aim point is a lead, so
			// the target is nowhere near the crosshair when the shot is worth taking. Its own FOV
			// instead of General::AimFOV; CAimbotProjectile passes it to PlayerBoneInFOV as the
			// flFOVOverride the function already accepted.
			CVar(AimFOV, VA_LIST("Aim FOV", "Projectile aim FOV"), 90.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(MaxSimulationTime, "Max simulation time", 2.f, SLIDER_MIN | SLIDER_PRECISION, 0.1f, 2.5f, 0.25f, "%gs");
			CVar(HitChance, "Hit chance", 0.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 10.f, "%g%%");
			CVar(AutodetRadius, "Autodet radius", 90.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 10.f, "%g%%");
			CVar(SplashRadius, "Splash radius", 90.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 10.f, "%g%%");
			CVar(AutoRelease, "Auto release", 0.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 5.f, "%g%%");

			CVar(GroundSamples, "Samples", 33, NOSAVE | DEBUGVAR, 3, 66);
			CVar(GroundStraightFuzzyValue, "Straight fuzzy value", 100.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 500.f, 25.f);
			CVar(GroundLowMinimumSamples, "Low min samples", 16, NOSAVE | DEBUGVAR, 3, 66);
			CVar(GroundHighMinimumSamples, "High min samples", 33, NOSAVE | DEBUGVAR, 3, 66);
			CVar(GroundLowMinimumDistance, "Low min distance", 0.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2500.f, 100.f);
			CVar(GroundHighMinimumDistance, "High min distance", 1000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2500.f, 100.f);
			CVar(GroundMaxChanges, "Max changes", 0, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 5);
			CVar(GroundMaxChangeTime, "Max change time", 0, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 66);

			CVar(AirSamples, "Samples", 33, NOSAVE | DEBUGVAR, 3, 66);
			CVar(AirStraightFuzzyValue, "Straight fuzzy value", 0.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 500.f, 25.f);
			CVar(AirLowMinimumSamples, "Low min samples", 16, NOSAVE | DEBUGVAR, 3, 66);
			CVar(AirHighMinimumSamples, "High min samples", 16, NOSAVE | DEBUGVAR, 3, 66);
			CVar(AirLowMinimumDistance, "Low min distance", 100000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2500.f, 100.f);
			CVar(AirHighMinimumDistance, "High min distance", 100000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2500.f, 100.f);
			CVar(AirMaxChanges, "Max changes", 2, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 5);
			CVar(AirMaxChangeTime, "Max change time", 16, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0, 66);

			CVar(VelocityAverageCount, "Velocity average count", 5, NOSAVE | DEBUGVAR, 1, 10);
			CVar(VerticalShift, "Vertical shift", 5.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f, 0.5f);
			CVar(DragOverride, "Drag override", 0.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 1.f, 0.01f);
			CVar(TimeOverride, "Time override", 0.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 2.f, 0.01f);
			CVar(HuntsmanLerp, "Huntsman lerp", 50.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 1.f, "%g%%");
			CVar(HuntsmanLerpLow, "Huntsman lerp low", 100.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 100.f, 1.f, "%g%%");
			CVar(HuntsmanAdd, "Huntsman add", 0.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 20.f);
			CVar(HuntsmanAddLow, "Huntsman add low", 0.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 20.f);
			CVar(HuntsmanClamp, "Huntsman clamp", 5.f, NOSAVE | DEBUGVAR | SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 10.f, 0.5f);
			CVar(HuntsmanPullPoint, "Huntsman pull point", false, NOSAVE | DEBUGVAR);

			CVar(SplashPointsDirect, "Direct splash points", 100, NOSAVE | DEBUGVAR | SLIDER_MIN, 0, 400, 5);
			CVar(SplashPointsArc, "Arc splash points", 100, NOSAVE | DEBUGVAR | SLIDER_MIN, 0, 400, 5);
			CVar(SplashCountDirect, "Direct splash count", 100, NOSAVE | DEBUGVAR | SLIDER_MIN, 1, 100);
			CVar(SplashCountArc, "Arc splash count", 5, NOSAVE | DEBUGVAR | SLIDER_MIN, 1, 100);
			CVar(SplashRotateX, "Splash Rx", -1.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, -1.f, 360.f);
			CVar(SplashRotateY, "Splash Ry", -1.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, -1.f, 360.f);
			CVar(SplashTraceInterval, "Splash trace interval", 10, NOSAVE | DEBUGVAR, 1, 10);
			CVar(SplashNormalSkip, "Splash normal skip", 1, NOSAVE | DEBUGVAR | SLIDER_MIN, 1, 10);
			CVarEnum(SplashMode, "Splash mode", 0, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST("Multi", "Single"),
				Multi, Single);
			CVarEnum(RocketSplashMode, "Rocket splash mode", 0, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST("Regular", "Special light", "Special heavy"),
				Regular, SpecialLight, SpecialHeavy);
			CVar(SplashGrates, "Splash grates", true, NOSAVE | DEBUGVAR);

			CVar(DeltaCount, "Delta count", 5, NOSAVE | DEBUGVAR, 1, 5);
			CVarEnum(DeltaMode, "Delta mode", 0, NOSAVE | DEBUGVAR, nullptr,
				VA_LIST("Average", "Max"),
				Average, Max);
			CVarEnum(MovesimFrictionFlags, "Movesim friction flags", 0b01, NOSAVE | DEBUGVAR | DROPDOWN_MULTI, nullptr,
				VA_LIST("Run reduce", "Calculate increase"),
				RunReduce = 1 << 0, CalculateIncrease = 1 << 1);
		SUBNAMESPACE_END(Projectile);

		SUBNAMESPACE_BEGIN(Melee)
			// per weapon-type enable (the menu section's own checkbox). Covers the knife
			// auto backstab too -- it lives inside CAimbotMelee.
			CVar(Enabled, VA_LIST("Melee", "Melee aimbot"), true);
			CVar(AutoBackstab, "Auto backstab", false);
			// Its own activation bind, same three modes as General::AimKey -- the master aim key
			// does not reach here (auto backstab runs with the aimbot off, by design), so without
			// this the row was on-or-off for a whole session. Read through CAimbot::BackstabOn();
			// the snapshot/track collectors stay on the plain toggle so the odds are warm the
			// instant the key goes down.
			CVar(BackstabKey, "Backstab key", 0, NOBIND);
			CVarEnum(BackstabKeyMode, "Backstab key mode", 1, NOBIND, nullptr,
				VA_LIST("Always on", "On hotkey", "Toggle"),
				AlwaysOn, OnHotkey, Toggle);
			// Backstab gets its own FOV + aim type so the knife can be tuned independently of the
			// general aimbot (values mirror General::AimType so they feed the same Aim() methods).
			CVarEnum(BackstabAimType, "Backstab aim type", 0, NONE, nullptr,
				VA_LIST("Silent", "Legit"),
				Silent, Legit);
			// SILENT-AIM LEASH ONLY -- Legit is fixed at 180 because it never moves the view.
			// It has been found parked at ~45 three sessions running (45.00 / 45.69 / 45.14, the
			// left quarter of the bar), and at 45 it throws away point-blank stabs: your eye sits
			// ~64u above your feet and his centre ~26u above his, so the angle DOWN to a man you
			// are standing on top of is 56 degrees at 24 units and grows as you close. It can no
			// longer do that quietly -- a target it refuses is counted and printed as why=fov in
			// the debug log, with a one-off line the first time it refuses somebody in reach.
			CVar(BackstabFOV, "Backstab FOV", 180.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			// dih-bs18: when the default rewind cannot stab, ask the server for an older tick (his box
			// where he was, his yaw still live). A feature switch, not a tuning knob. Only with
			// Aimbot::Backtrack::Enabled on too (global off = no rewind anywhere).
			CVar(BackstabBacktrack, "Backstab backtrack", true);
			// dih-bs13: BackstabAutoShoot DELETED. Off meant the feature never pressed anything,
			// and in Legit mode (which has no view assist at all) that is the whole feature -- the
			// cfg was found on that value too. Auto backstab IS the auto swing. Its key goes with
			// it, so a stale cfg cannot leak an old value into whatever takes the slot.
			// Backstab-specific copy of the General aim Ignore dropdown so the knife can skip
			// invulnerable / invisible / dead-ringer / vaccinator-shielded / disguised / taunting
			// The bit layout MUST match General::Ignore -- ShouldIgnore tests this mask with the
			// General IgnoreEnum names. Razorback is backstab-only and sits after them.
			CVarEnum(BackstabIgnore, "Backstab ignore", 0b10000001, DROPDOWN_MULTI, nullptr,
				VA_LIST("Invulnerable", "Invisible", "Unsimulated", "Dead ringer", "Vaccinator", "Disguised", "Taunting", "Razorback"),
				Invulnerable = 1 << 0, Invisible = 1 << 1, Unsimulated = 1 << 2, DeadRinger = 1 << 3, Vaccinator = 1 << 4, Disguised = 1 << 5, Taunting = 1 << 6, Razorback = 1 << 7);
			CVar(SwingPrediction, "Swing prediction", false);

			CVar(SwingOffset, "Swing offset", -1, NOSAVE | DEBUGVAR, -1, 1);
			CVar(SwingPredictLag, "Swing predict lag", true, NOSAVE | DEBUGVAR);
			// dih-bs11: TEN BACKSTAB KNOBS DELETED, AND THEY MUST NOT COME BACK -- account ping,
			// double test, extrapolate yaw, unstable connection + its max angle, remove interp,
			// strict range, predict movement, aggressive, multi point. Every one was a constant
			// standing in for something the client can MEASURE off the target, and the quantity
			// they all guessed at -- how wrong our copy of his yaw and his position is -- is now
			// derived per target per tick from a least-squares fit of his own turn rate plus the
			// latency the engine reports. See the header comment in AimbotMelee.cpp. Dropping them
			// also drops their keys from the config table, so a stale C:otox-tf2\default.cfg cannot
			// leak an old value into whatever takes the slot.
		SUBNAMESPACE_END(Melee);

		// botox m_backtrack_enable / m_aimbot_aim_at_backtrack / m_backtrack_time_limit (defaults
		// = botox variables.h). Extended (fake latency) not ported. See aim/backtrack.h.
		SUBNAMESPACE_BEGIN(Backtrack, Aimbot)
			CVar(Enabled, "Backtrack", false);
			CVar(AimAt, "Aim at backtrack", false);
			CVar(IgnoreMelee, "Ignore melee", false);   // RunMeleeSmack off; knife = Melee::BackstabBacktrack
			CVar(TimeLimit, "Backtrack time", 200, SLIDER_CLAMP, 0, 200, 1, "%d ms");
		SUBNAMESPACE_END(Backtrack);

		// Sniper scope FOV scale (gap.md aim extras) -- TF2's scope zoom is one fixed FOV per
		// zoom level; this scales it client-side. See CHLClient_CreateMove (hooks.cpp).
		SUBNAMESPACE_BEGIN(ScopeFOV, Aimbot)
			CVar(Enabled, "Scope FOV scale", false);
			CVar(Scale, "Scope FOV scale", 100.f, SLIDER_CLAMP | SLIDER_PRECISION, 50.f, 200.f, 5.f, "%g%%");
		SUBNAMESPACE_END(ScopeFOV);

		// Per-weapon-slot aim tuning (gap.md aim extras) -- deliberately the 3 weapons the gap
		// analysis names (sniper/minigun/scattergun), not a generic N-weapon table. FOV only --
		// see weapon_tuning.h for why smooth/hitboxes stayed out.
		SUBNAMESPACE_BEGIN(WeaponTuning, Aimbot)
			CVar(Enabled, "Per-weapon tuning", false);
			CVar(SniperFOV, "Sniper rifle FOV", 10.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(MinigunFOV, "Minigun FOV", 30.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
			CVar(ScattergunFOV, "Scattergun FOV", 30.f, SLIDER_CLAMP | SLIDER_PRECISION, 0.f, 180.f);
		SUBNAMESPACE_END(WeaponTuning);

	NAMESPACE_END(Aimbot);
	
	NAMESPACE_BEGIN(CritHack, Crit Hack)
		CVar(ForceCrits, "Force crits", false);
		CVar(AvoidRandomCrits, "Avoid random crits", false);
		CVar(AlwaysMeleeCrit, "Always melee crit", false);
	NAMESPACE_END(CritHack);

	NAMESPACE_BEGIN(Doubletap)
		CVar(Doubletap, "Doubletap", false);
		CVar(Warp, "Warp", false);
		CVar(RechargeTicks, "Recharge ticks", false);
		CVar(AntiWarp, "Anti-warp", false);
		CVar(TickLimit, "Tick limit", 22, SLIDER_CLAMP, 2, 22);
		CVar(WarpRate, "Warp rate", 22, SLIDER_CLAMP, 2, 22);
		CVar(PassiveRecharge, "Passive recharge", 0, SLIDER_CLAMP, 0, 67);
	NAMESPACE_END(DoubleTap)

	NAMESPACE_BEGIN(AutoPeek, Auto Peek)
		CVar(Enabled, VA_LIST("Enabled", "Auto peek"), false);
	NAMESPACE_END(AutoPeek);

	NAMESPACE_BEGIN(Speedhack)
		CVar(Enabled, VA_LIST("Enabled", "Speedhack enabled"), false);
		CVar(Amount, VA_LIST("Amount", "SpeedHack amount"), 1, NONE, 1, 50);
	NAMESPACE_END(Speedhack);

	NAMESPACE_BEGIN(AntiAim, Antiaim)
		CVar(Enabled, VA_LIST("Enabled", "Antiaim enabled"), false);
	NAMESPACE_END(AntiAim);

	NAMESPACE_BEGIN(Resolver)
		CVar(Enabled, VA_LIST("Enabled", "Resolver enabled"), false);
	NAMESPACE_END(Resolver);

	NAMESPACE_BEGIN(Visuals)
		SUBNAMESPACE_BEGIN(UI)
			CVar(RevealScoreboard, "Reveal scoreboard", false, VISUAL);
			CVar(CleanScreenshots, "Clean screenshots", false);
		SUBNAMESPACE_END(UI);

		SUBNAMESPACE_BEGIN(Thirdperson)
			CVar(Enabled, "Thirdperson", false, VISUAL);
			CVar(Distance, "Thirdperson distance", 150.f, VISUAL | SLIDER_PRECISION, 0.f, 400.f, 10.f);
			CVar(Up, "Thirdperson up", 0.f, VISUAL | SLIDER_PRECISION, -100.f, 100.f, 5.f);

			CVar(Scale, "Thirdperson scales", true, NOSAVE | DEBUGVAR);
		SUBNAMESPACE_END(ThirdPerson);

		SUBNAMESPACE_BEGIN(Removals)
			CVar(Interpolation, VA_LIST("Interpolation", "Remove interpolation"), false);
			CVar(Lerp, VA_LIST("Lerp", "Remove lerp"), false);
			CVar(Disguises, VA_LIST("Disguises", "Remove disguises"), false, VISUAL);
			CVar(Taunts, VA_LIST("Taunts", "Remove taunts"), false, VISUAL);
			CVar(Scope, VA_LIST("Scope", "Remove scope"), false, VISUAL);
			CVar(AngleForcing, VA_LIST("Angle forcing", "Remove angle forcing"), false, VISUAL);
		SUBNAMESPACE_END(Removals);

		SUBNAMESPACE_BEGIN(Viewmodel)
			CVar(OffsetX, VA_LIST("Offset X", "Viewmodel offset X"), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
			CVar(OffsetY, VA_LIST("Offset Y", "Viewmodel offset Y"), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
			CVar(OffsetZ, VA_LIST("Offset Z", "Viewmodel offset Z"), 0.f, VISUAL | SLIDER_PRECISION, -45.f, 45.f, 5.f);
		SUBNAMESPACE_END(Viewmodel);

		// Weather: fog changer + native precipitation (rain / snow),. See CWeather.
		SUBNAMESPACE_BEGIN(Beams) // as of now, these will stay out of the menu
			CVar(Model, "Model", std::string("sprites/physbeam.vmt"), VISUAL);
			CVar(Life, "Life", 2.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
			CVar(Speed, "Speed", 0.2f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 5.f);
		SUBNAMESPACE_END(Beams);

		SUBNAMESPACE_BEGIN(Line)
			CVar(Enabled, "Line tracers", false, VISUAL);
			CVar(DrawDuration, VA_LIST("Draw duration", "Line draw duration"), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
		SUBNAMESPACE_END(Line);

		SUBNAMESPACE_BEGIN(Hitbox)
			CVarEnum(BoundsEnabled, VA_LIST("Bounds enabled", "Hitbox bounds enabled"), 0b000, VISUAL | DROPDOWN_MULTI, "Off",
				VA_LIST("On shot", "On hit", "Aim point"),
				OnShot = 1 << 0, OnHit = 1 << 1, AimPoint = 1 << 2);
			CVar(DrawDuration, VA_LIST("Draw duration", "Hitbox draw duration"), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);
		SUBNAMESPACE_END(Hitbox);

		SUBNAMESPACE_BEGIN(Simulation)
			Enum(Style, Off, Line, Separators, Spaced, Arrows, Boxes);
			CVarValues(PlayerPath, "Player path", 0, VISUAL, nullptr,
				"Off", "Line", "Separators", "Spaced", "Arrows", "Boxes");
			CVarValues(ProjectilePath, "Projectile path", 0, VISUAL, nullptr,
				"Off", "Line", "Separators", "Spaced", "Arrows", "Boxes");
			CVarEnum(SplashRadius, "Splash radius", 0b0, VISUAL | DROPDOWN_MULTI, "Off",
				VA_LIST("Simulation", "##Divider", "Priority", "Enemy", "Team", "Local", "Friends", "Party", "##Divider", "Rockets", "Stickies", "Pipes", "Scorch shot", "##Divider", "Trace"),
				Simulation = 1 << 0, Priority = 1 << 1, Enemy = 1 << 2, Team = 1 << 3, Local = 1 << 4, Friends = 1 << 5, Party = 1 << 6, Rockets = 1 << 7, Stickies = 1 << 8, Pipes = 1 << 9, ScorchShot = 1 << 10, Trace = 1 << 11);
			CVar(Timed, VA_LIST("Timed", "Timed path"), false, VISUAL);
			CVar(SwingLines, "Swing lines", false, VISUAL);
			CVar(DrawDuration, VA_LIST("Draw duration", "Simulation draw duration"), 5.f, VISUAL | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f);

			CVarValues(RealPath, "Real path", 0, NOSAVE | DEBUGVAR, nullptr,
				"Off", "Line", "Separators", "Spaced", "Arrows", "Boxes");
		SUBNAMESPACE_END(Simulation);

		SUBNAMESPACE_BEGIN(Trajectory)
			CVar(Override, "Simulation override", false, NOSAVE | DEBUGVAR);
			CVar(OffsetX, "Offset X", 16.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(OffsetY, "Offset Y", 8.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(OffsetZ, "Offset Z", -6.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -25.f, 25.f, 0.5f);
			CVar(Pipes, "Pipes", true, NOSAVE | DEBUGVAR);
			CVar(Hull, "Hull", 5.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f, 0.5f);
			CVar(Speed, "Speed", 1200.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 5000.f, 50.f);
			CVar(Gravity, "Gravity", 1.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 1.f, 0.1f);
			CVar(LifeTime, "Life time", 2.2f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 10.f, 0.1f);
			CVar(UpVelocity, "Up velocity", 200.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 1000.f, 50.f);
			CVar(AngularVelocityX, "Angular velocity X", 600.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(AngularVelocityY, "Angular velocity Y", -1200.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(AngularVelocityZ, "Angular velocity Z", 0.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, -1000.f, 1000.f, 50.f);
			CVar(Drag, "Drag", 1.f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 2.f, 0.1f);
			CVar(DragX, "Drag X", 0.003902f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(DragY, "Drag Y", 0.009962f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(DragZ, "Drag Z", 0.009962f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(AngularDragX, "Angular drag X", 0.003618f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(AngularDragY, "Angular drag Y", 0.001514f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(AngularDragZ, "Angular drag Z", 0.001514f, NOSAVE | DEBUGVAR | SLIDER_PRECISION, 0.f, 0.1f, 0.01f, "%.15g");
			CVar(MaxVelocity, "Max velocity", 2000.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 4000.f, 50.f);
			CVar(MaxAngularVelocity, "Max angular velocity", 3600.f, NOSAVE | DEBUGVAR | SLIDER_MIN | SLIDER_PRECISION, 0.f, 7200.f, 50.f);
		SUBNAMESPACE_END(ProjectileTrajectory);

		// ESP tab "Model Preview" window: renders the heavy player model (stand_MELEE)
		SUBNAMESPACE_BEGIN(ModelPreview)
			CVar(Enabled, "Model preview", false, VISUAL);
		SUBNAMESPACE_END(ModelPreview);
	NAMESPACE_END(Visuals);

	NAMESPACE_BEGIN(Misc)
		SUBNAMESPACE_BEGIN(Movement)
			CVar(Bunnyhop, "Bunnyhop", false);
			CVar(LongJump, "Long jump", false);

			// Fake POV (Movement > FAKE POV): bake a fake camera angle into recorded POV demos via the
			// Clean POV demo path (CPrediction::GetLocalViewAngles) without touching live aim. The angle
			CVar(FakePOV, "Fake POV", false);

		SUBNAMESPACE_END(Movement);

		SUBNAMESPACE_BEGIN(Automation)
			CVar(TauntControl, "Taunt control", false);
			CVar(KartControl, "Kart control", false);
		SUBNAMESPACE_END(Automation);

		SUBNAMESPACE_BEGIN(Game)
			CVar(AntiCheatCompatibility, "Anti-cheat compatibility", false);
			// Write your real view angle into your local POV demos so silent aim never snaps the first-person camera.
			CVar(CleanPOVDemos, "Clean POV demos", false);

			CVar(AntiCheatCritHack, "Anti-cheat crit hack", false, NOSAVE | DEBUGVAR);
		SUBNAMESPACE_END(Game);

		SUBNAMESPACE_BEGIN(MannVsMachine, Mann vs. Machine)
			CVar(InstantRevive, "Instant revive", false);
		SUBNAMESPACE_END(Sound);

	NAMESPACE_END(Misc);

	NAMESPACE_BEGIN(Debug)
		CVar(Info, "Debug info", false, NOSAVE);
		CVar(Logging, "Debug logging", false, NOSAVE);
	NAMESPACE_END(Debug);

}