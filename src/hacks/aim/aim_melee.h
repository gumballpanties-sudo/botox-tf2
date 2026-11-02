#pragma once
#include "sdk.h"

#include "aim_global.h"
#include "bs_math.h"

// Which gate refused a backstab, in pipeline order (a later gate = got further). Printed as why=
// so a quiet session says WHY it was quiet.
enum EBsGate
{
	BSG_FIRE = 0,     // nothing refused
	BSG_COOLDOWN,     // m_flNextPrimaryAttack / smack window: no swing is possible this tick
	BSG_NOTARGET,     // nobody inside the reach cull
	BSG_NOTRACK,      // no snapshot of him yet (just out of dormancy)
	BSG_RANGE,        // the swept hull cannot touch his rewound box along any angle we may swing
	BSG_FOV,          // the Backstab FOV slider refused every angle (Silent only)
	BSG_BLOCKED,      // the server's swing stops on geometry / a teammate first
	BSG_TRACEMISS,    // the swing reaches nobody
	BSG_IGNORED,      // whoever it reaches is filtered out (invuln, cloaked, razorback...)
	BSG_REACH,        // it connects, but by less than his position is uncertain
	BSG_FACING,       // our view is not within 60 degrees of him
	BSG_ODDS,         // the chance of a backstab is under BsMath::k_pTake
	BSG_TURN,         // good odds on his predicted turn, not at the guard point (BsMath::TurnGuarded)
	BSG_HOLD,         // good odds, but the next tick is clearly better
	BSG_THIN,         // connects by less than our relative motion, solid next tick (BsMath::ThinPad)
	BSG_COUNT
};
const char* BsGateName(int iGate);

class CAimbotMelee
{
private:
	std::vector<Target_t> GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);
	bool AimFriendlyBuilding(CBaseObject* pBuilding);
	std::vector<Target_t> SortTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	// Knife backstab overrides the general aim type / FOV so it can run on its own settings (and
	// even when the general aimbot type is Off). Non-knife melee keeps using General.
	//
	// THERE IS NO AUTO-SHOOT KNOB AND IT MUST NOT COME BACK: auto backstab IS the auto swing.
	// The FOV slider STAYS (user's call, dih-bs13): a Silent-mode leash that refuses out loud
	// (why=fov).
	bool IsKnifeBackstab(CTFWeaponBase* pWeapon);
	int GetBackstabAimType(); // KNIFE dropdown index -> General aim type
	int GetAimType(CTFWeaponBase* pWeapon);
	float GetFOV(CTFWeaponBase* pWeapon);

	int GetSwingTime(CTFWeaponBase* pWeapon, bool bVar = true);
	void UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, std::vector<Target_t> vTargets);
	int CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon);

	// ---- the knife path (dih-bs15, math in bs_math.h) ------------------------------------------
	struct BsJudge
	{
		int   m_iGate = BSG_NOTARGET;
		bool  m_bFire = false;
		int   m_iIndex = 0;          // who the server's swing would return (NOT always the target)
		Vec3  m_vAim = {};
		float m_flP = 0.f;           // odds of a backstab on the server (the turn-guarded min)
		float m_flPNext = 0.f;       // the same one tick later
		float m_flPBest = 0.f;       // odds on the best estimator's yaw alone (bs15's p)
		float m_flPGuard = 0.f;      // odds at the turn guard point (seen + k_turnTrust of the turn)
		float m_flGuardMargin = 0.f; // degrees inside his yaw window at the guard point (< 0 = outside)
		float m_flSigCore = 0.f;     // the sigma the guard point is judged on (no spread term)
		float m_flMargin = 0.f;      // degrees inside his yaw window at the estimate
		float m_flFacing = 0.f;      // degrees inside the facing window
		float m_flSigma = 0.f;       // his yaw sigma, degrees -- measured
		float m_flSigBear = 0.f;     // bearing sigma, degrees
		float m_flRate = 0.f;        // his turn rate, deg/s
		float m_flHorizon = 0.f;     // how far past his newest record the server reads his yaw, s
		float m_flExtrap = 0.f;      // how far past his newest record his position was guessed, s
		float m_flSlack = -1.f, m_flPad = 0.f;
		float m_flDist = 0.f, m_flSpeed = 0.f;
		float m_flRel = 0.f;         // 2D speed between us and him, u/s (ThinPad)
		int   m_iK = 1;             // yaw estimator: 0 hold, 1 half rate, 2 full rate
		int   m_iHeld = 0;
		// front-stab forensics (bs17b, log only): sm = margin at his last SEEN yaw (sm < margin = the
		// bet turns him deeper in), rb = seen yaw - bearing (0 = dead behind), brate = how fast the
		// bearing swings as we pass (deg/s), the term sigBear does not price
		float m_flSeenMargin = 0.f, m_flRelYaw = 0.f, m_flBearRate = 0.f;
		bool  m_bReachNext = true;
	};
	void RunBackstab(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	bool JudgeKnife(int iBox, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, BsJudge& tOut);   // iBox into m_aBoxes

	bool Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod = Vars::Aimbot::General::AimType.Value);
	void Aim(CUserCmd* pCmd, Vec3& vAngle, int iMethod = Vars::Aimbot::General::AimType.Value);

	bool FindNearestBuildPoint(CBaseObject* pBuilding, CTFPlayer* pLocal, Vec3& vPoint);
	bool RunSapper(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);

	Vec3 m_vEyePos = {};
	// The eye as UpdateInfo left it; each CanHit restores from this.
	Vec3 m_vEyePosBase = {};
	float m_flRange = 0.f;
	bool m_bShouldSwing = false;
	bool m_bBackstabLegit = false; // Legit backstab does no view rotation, only auto-swings

	// ---- one knife command's context, shared by every target -------------------------------------
	Vec3  m_vKnifeEye = {};        // post-move eye the server swings from
	Vec3  m_vKnifeView = {};       // the command's own angle (Legit's only candidate)
	float m_flTDue = 0.f;          // server time this command runs = when his live yaw is read
	float m_flTRewind = 0.f;       // server time lag compensation rewinds everyone to
	float m_flKnifeRange = 48.f, m_flKnifeHull = 18.f;
	static constexpr int k_boxMax = 64;
	BsMath::Box  m_aBoxes[k_boxMax] = {};   // every nearby player's rewound box
	CBaseEntity* m_aBoxEnt[k_boxMax] = {};
	int          m_aBoxTeam[k_boxMax] = {};
	float        m_aBoxSpeed[k_boxMax] = {};
	float        m_aBoxExtrap[k_boxMax] = {};
	int          m_iBoxes = 0;
	// Razorback / cannot_be_backstabbed: one attribute walk per target per command
	int  m_aRazorTick[65] = {};
	bool m_aRazorBlocked[65] = {};
	// consecutive ticks a good-odds stab has been held for a better one (BsMath::k_patience)
	struct BsHold { int m_iTick = -1000; int m_iCount = 0; };
	BsHold m_aHold[65] = {};

	// Live (real-time) target snapshot -- the non-knife melee path's single record.
	TickRecord m_tLiveRecord = {};

	int m_iDoubletapTicks = 0;

	std::unordered_map<int, std::deque<TickRecord>> m_mRecordMap;
	std::unordered_map<int, std::vector<Vec3>> m_mPaths;

	// ---- diagnostics: the promise a swing was bet on, and what came back --------------------------
	struct BackstabFire
	{
		float m_flTime = -1000.f;   // GlobalVars->realtime
		int   m_iIndex = 0;
		float m_flP = 0.f, m_flSigma = 0.f, m_flMargin = 0.f;
		float m_flSlack = 0.f, m_flPad = 0.f, m_flHorizon = 0.f, m_flExtrap = 0.f;
		float m_flDist = 0.f, m_flSpeed = 0.f;
		int   m_iK = 1;
		float m_flPBest = 0.f, m_flPGuard = 0.f, m_flRate = 0.f, m_flGuardMargin = 0.f, m_flSigCore = 0.f;
		float m_flSeenMargin = 0.f, m_flRelYaw = 0.f, m_flBearRate = 0.f, m_flRel = 0.f;
		float m_flLat = 0.f;        // AimLatency at the fire: outcome dt well past it = ping spiked mid-swing
		int   m_iBack = 0;          // bs18: ticks the rewind was moved back (0 = default tick)
	};
	int m_iBtFires = 0;            // swings that only existed on an older tick
	// sum of (1 - p) over judged swings: the fronts the odds PROMISED, next to the fronts that came
	double m_flExpFront = 0.0;
	BackstabFire m_tLastFire = {};
	int m_iQuietTick = 0;      // throttle for the "nothing fired" line
	// realtime clocks: inside engine prediction tickcount is the tickbase, events are not
	float m_flPendTime = -1.f;  // a swing was pressed, for the press-took-effect read
	float m_flPendNext = 0.f;   // m_flNextPrimaryAttack as it was when that press went in
	int m_iPressLost = 0;
	float m_flWhiffTime = -1.f; // a press was seen to swing; player_hurt clears it
	int m_aGateHits[BSG_COUNT] = {};
	int m_iFires = 0, m_iStabs = 0, m_iFrontStabs = 0, m_iWhiffs = 0;

	// microseconds per knife command, measured (us= on the miss line)
	double m_flCostSum = 0.0, m_flCostMax = 0.0;
	int    m_iCostN = 0;

public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	// FRAME_NET_UPDATE_END: raw per-player records for the backstab (hooks.cpp)
	void StoreBackstabSnapshots();
	// every command: one yaw prediction per player at the server's read time (CAimbot::Run)
	void UpdateBackstabTracks();
	// player_hurt: prints the RESULT beside the promise the swing was bet on.
	void Event(IGameEvent* pEvent, uint32_t uHash, CTFPlayer* pLocal);
};

ADD_FEATURE(CAimbotMelee, AimbotMelee);
