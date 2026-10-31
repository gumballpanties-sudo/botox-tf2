#include "esp.h"
#include "../esp/esp_math.h"
#include "../esp/esp_fx.h"
#include "../hud/avatar_pure.h"   // n_avatar::FriendIdToSteam64 -- plain arithmetic, no Steam/d3d9 types
#include <cstdio>

void DebugLog(const char* fmt, ...);   // dllmain.cpp, same forward decl as aim.cpp:18

// dih's own plain g_esp* globals (src/globals.h extern-declares the same names for config.cpp /
// unitC_menu.cpp; src/dllmain.cpp defines them). Declared here too -- primitive types only, no
// aim/dih header collision (see esp.h banner comment) -- so this TU does not need to include
// dih's globals.h/types.h (which stub out IClientEntityList etc. and would redefine the real
// SDK classes this file needs from aim/defs.h).
extern bool  g_espEnable;
extern bool  g_espTeammates;
extern bool  g_espDistanceFade;
extern float g_espFadeNear;
extern float g_espFadeFar;
extern bool  g_espName;
extern bool  g_espHealth;
extern bool  g_espDistance;
extern bool  g_espFlags;
extern bool  g_espSkeleton;
extern bool  g_espHitboxes;
extern int   g_espBoxStyle;   // 0 off, 1 2d corner, 2 2d full

extern bool  g_espWorldEnable;
extern bool  g_espBuildings;
extern bool  g_espAmmoPacks;
extern bool  g_espDroppedWeapons;
extern bool  g_espProjectiles;

extern bool  g_espOofArrows;

extern bool  g_espProjPath;
extern float g_espProjPathColor[4];
extern bool  g_espSoundPings;
extern bool  g_espDamageNumbers;
extern bool  g_espAvatar;

// user colours (menu: esp settings / world entities settings)
extern float g_espEnemyColor[4];
extern float g_espTeamColor[4];
extern float g_espSkeletonColor[4];
extern float g_espHitboxColor[4];
extern float g_espFlagsColor[4];
extern float g_espOofColor[4];
extern float g_espSoundColor[4];
extern float g_espDmgColor[4];
extern float g_espBuildingColor[4];
extern float g_espItemColor[4];
extern float g_espProjColor[4];

static Color_t ToColor(const float* c)
{
	return { (byte)(c[0] * 255.f + 0.5f), (byte)(c[1] * 255.f + 0.5f), (byte)(c[2] * 255.f + 0.5f), (byte)(c[3] * 255.f + 0.5f) };
}

// World-space line, drawn in SCREEN space. This runs off IEngineVGui_Paint (2D surface pass):
// H::Draw.RenderLine/RenderWireframeBox/RenderSphere are IRenderUtils 3D calls, which only draw
// inside the 3D view (Amalgam calls them from DoPostScreenSpaceEffects) -- here they drew nothing.
static void Line3D(const Vec3& a, const Vec3& b, Color_t c)
{
	Vec3 sa, sb;
	if (SDK::W2S(a, sa) && SDK::W2S(b, sb))
		H::Draw.Line((int)sa.x, (int)sa.y, (int)sb.x, (int)sb.y, c);
}

// mstudiobone_t (studio.h): sznameindex 0, parent 4, ... flags 0xA0; stride 0xD8. No pointers,
// so x86 == x64. defs.h has no bone struct, so read the two fields by offset.
static int BoneParent(const studiohdr_t* h, int i) { return *reinterpret_cast<const int*>(reinterpret_cast<const byte*>(h) + h->boneindex + i * 0xD8 + 0x4); }
static int BoneFlags(const studiohdr_t* h, int i)  { return *reinterpret_cast<const int*>(reinterpret_cast<const byte*>(h) + h->boneindex + i * 0xD8 + 0xA0); }

// Replicates SDK::IsOnScreen(pEntity, vOrigin)'s own 6-point sweep (sdk.cpp:167-204) but keeps
// the screen rect instead of discarding it -- that overload only returns a bool.
static bool ComputeScreenBox(CBaseEntity* pEntity, const Vec3& vOrigin, float& flLeft, float& flRight, float& flTop, float& flBottom)
{
	Vec3 vMins = pEntity->m_vecMins(), vMaxs = pEntity->m_vecMaxs();
	const Vec3 vPoints[] = {
		Vec3(0.f, 0.f, vMins.z),
		Vec3(0.f, 0.f, vMaxs.z),
		Vec3(vMins.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMins.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMins.y, (vMins.z + vMaxs.z) * 0.5f),
		Vec3(vMaxs.x, vMaxs.y, (vMins.z + vMaxs.z) * 0.5f)
	};

	bool bInit = false;
	for (int n = 0; n < 6; n++)
	{
		Vec3 vScreen;
		if (!SDK::W2S(vOrigin + vPoints[n], vScreen))
			continue;

		flLeft   = bInit ? std::min(flLeft, vScreen.x) : vScreen.x;
		flRight  = bInit ? std::max(flRight, vScreen.x) : vScreen.x;
		flTop    = bInit ? std::max(flTop, vScreen.y) : vScreen.y;
		flBottom = bInit ? std::min(flBottom, vScreen.y) : vScreen.y;
		bInit = true;
	}
	return bInit && !(flRight < 0 || flLeft > H::Draw.m_nScreenW || flTop < 0 || flBottom > H::Draw.m_nScreenH);
}

void CEsp::DrawBoxName(CBaseEntity* pEntity, const char* sName, Color_t tColor, float flDistU, bool bHealthBar, int iHealth, int iMaxHealth)
{
	const Vec3 vOrigin = pEntity->GetAbsOrigin();
	float flLeft, flRight, flTop, flBottom;
	if (!ComputeScreenBox(pEntity, vOrigin, flLeft, flRight, flTop, flBottom))
		return;

	// flTop/flBottom carry SDK::IsOnScreen's naming (flTop = max screen y). See BoxFromBounds.
	const auto tBox = EspMath::BoxFromBounds(flLeft, flRight, flTop, flBottom);
	const int x = tBox.x, y = tBox.y, w = tBox.w, h = tBox.h;
	if (w <= 0 || h <= 0) return;
	++m_nDrawn;

	float flAlpha = 1.f;
	if (g_espDistanceFade)
	{
		flAlpha = EspMath::DistanceFadeAlpha(flDistU, g_espFadeNear, g_espFadeFar);
		if (flAlpha <= 0.01f) return;
		tColor.a = (byte)(tColor.a * flAlpha);
	}

	if (g_espBoxStyle == 1)
		H::Draw.LineCornerRectOutline(x, y, w, h, tColor);
	else if (g_espBoxStyle == 2)
		H::Draw.LineRectOutline(x, y, w, h, tColor);

	if (bHealthBar && iMaxHealth > 0)
	{
		const float t = std::clamp((float)iHealth / (float)iMaxHealth, 0.f, 1.f);
		Color_t barColor = { (byte)(255 * (1.f - t)), (byte)(255 * t), 0, tColor.a };
		// ALIGN_BOTTOM: FillRectPercent only scales for LEFT/RIGHT/TOP/BOTTOM (helpers.cpp), BOTTOMLEFT
		// fell through = full bar at any hp. Amalgam ESP.cpp uses ALIGN_BOTTOM.
		H::Draw.FillRectPercent(x - 6, y, 3, h, t, barColor, { 0, 0, 0, tColor.a }, ALIGN_BOTTOM);
	}

	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	Color_t tOutline = { 0, 0, 0, tColor.a };
	int iLine = 0;
	if (g_espName && sName)
		H::Draw.StringOutlined(fFont, x + w / 2, y - 2 - 12 * iLine++, tColor, tOutline, ALIGN_BOTTOM, sName);
	if (g_espDistance)
		H::Draw.StringOutlined(fFont, x + w / 2, y - 2 - 12 * iLine++, tColor, tOutline, ALIGN_BOTTOM, std::format("[{}m]", (int)(flDistU / 52.49f)).c_str());
	if (g_espHealth && iMaxHealth > 0)
		H::Draw.StringOutlined(fFont, x + w / 2, y + h + 12, tColor, tOutline, ALIGN_TOP, std::format("{}hp", iHealth).c_str());
}

// Player ESP: box/name/health/distance/flags/hitboxes/skeleton, teammates filter, distance fade,
// dormancy skip. Flags ride CTFPlayer::InCond (aim/defs.h ~10731, direct virtual call -- not the
// InCond hook target, that's a crit-hack intercept on a different purpose).
void CEsp::DrawPlayers(CTFPlayer* pLocal)
{
	// unit E5: this hook runs before this frame's ImGui::NewFrame() (render.cpp's
	// BuildImGuiFrame(), called later off the nested FocusOverlayPanel paint) -- Avatar_Get()/
	// AddImage() need that context, so the box rect + steamid is snapshotted here and drawn
	// later by RenderEspAvatars() (render.cpp). See esp_fx.h banner.
	if (g_espAvatar) EspFx_BeginAvatarFrame();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerAll))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer || pPlayer == pLocal || pPlayer->IsDormant() || !pPlayer->IsAlive())
			continue;

		const bool bTeammate = pPlayer->m_iTeamNum() == pLocal->m_iTeamNum();
		if (bTeammate && !g_espTeammates)
			continue;

		const Vec3 vOrigin = pPlayer->GetAbsOrigin();
		const float flDist = (vOrigin - pLocal->GetAbsOrigin()).Length();

		int iHealth = pPlayer->m_iHealth(), iMaxHealth = 0;
		if (auto pResource = H::Entities.GetResource())
			iMaxHealth = pResource->m_iMaxHealth(pPlayer->entindex());

		Color_t tColor = ToColor(bTeammate ? g_espTeamColor : g_espEnemyColor);

		const char* sName = nullptr;
		if (g_espName)
			if (auto pResource = H::Entities.GetResource())
				sName = pResource->GetName(pPlayer->entindex());

		if (g_espBoxStyle != 0 || g_espName || g_espDistance || g_espHealth)
			DrawBoxName(pPlayer, sName, tColor, flDist, g_espHealth, iHealth, iMaxHealth);

		if (g_espAvatar)
		{
			player_info_t tInfo;
			if (I::EngineClient->GetPlayerInfo(pPlayer->entindex(), &tInfo) && !tInfo.fakeplayer)
			{
				const uint64_t steamid64 = n_avatar::FriendIdToSteam64(tInfo.friendsID);
				float flLeft, flRight, flTop, flBottom;
				if (steamid64 && ComputeScreenBox(pPlayer, vOrigin, flLeft, flRight, flTop, flBottom))
					EspFx_PushAvatar(flLeft - 18.f, flBottom, flLeft - 2.f, flBottom + 16.f, steamid64);   // flBottom = screen top
			}
		}

		if (g_espFlags)
		{
			std::string sFlags;
			if (pPlayer->InCond(TF_COND_INVULNERABLE))  sFlags += "[uber]";
			if (pPlayer->InCond(TF_COND_BURNING))       sFlags += "[burn]";
			if (pPlayer->InCond(TF_COND_BLEEDING))      sFlags += "[bleed]";
			if (pPlayer->InCond(TF_COND_STEALTHED))     sFlags += "[cloak]";
			if (pPlayer->InCond(TF_COND_DISGUISED))     sFlags += "[spy]";
			if (!sFlags.empty())
			{
				float flLeft, flRight, flTop, flBottom;
				if (ComputeScreenBox(pPlayer, vOrigin, flLeft, flRight, flTop, flBottom))
					H::Draw.StringOutlined(H::Fonts.GetFont(FONT_INDICATORS), (int)((flLeft + flRight) / 2), (int)flTop + 24,   // flTop = screen bottom
						ToColor(g_espFlagsColor), Color_t{ 0, 0, 0, 255 }, ALIGN_TOP, sFlags.c_str());
			}
		}

		if (g_espHitboxes || g_espSkeleton)
		{
			matrix3x4 aBones[MAXSTUDIOBONES];
			if (pPlayer->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, I::GlobalVars->curtime))
			{
				if (auto pModel = pPlayer->GetModel())
					if (auto pStudioHdr = I::ModelInfoClient->GetStudiomodel(pModel))
					{
						const auto BonePos = [&](int b) { return Vec3(aBones[b][0][3], aBones[b][1][3], aBones[b][2][3]); };

						// skeleton = each hitbox bone to its parent. Only BONE_USED_BY_HITBOX bones
						// were set up above, so a parent without the flag holds garbage: skip it.
						if (g_espSkeleton)
						{
							const Color_t tSkel = ToColor(g_espSkeletonColor);
							const int nBones = std::min(pStudioHdr->numbones, MAXSTUDIOBONES);
							for (int b = 0; b < nBones; b++)
							{
								const int p = BoneParent(pStudioHdr, b);
								if (p < 0 || p >= nBones || !(BoneFlags(pStudioHdr, b) & BONE_USED_BY_HITBOX)
									|| !(BoneFlags(pStudioHdr, p) & BONE_USED_BY_HITBOX))
									continue;
								Line3D(BonePos(b), BonePos(p), tSkel);
							}
						}

						// hitboxes: the 8 corners of each bbox through its bone matrix, 12 edges
						if (g_espHitboxes)
							if (auto pSet = pStudioHdr->pHitboxSet(0))
							{
								const Color_t tHb = ToColor(g_espHitboxColor);
								for (int i = 0; i < pSet->numhitboxes; i++)
								{
									auto pBox = pSet->pHitbox(i);
									if (!pBox || pBox->bone < 0 || pBox->bone >= MAXSTUDIOBONES) continue;
									Vec3 c[8];
									for (int k = 0; k < 8; k++)
									{
										const Vec3 vLocal((k & 1) ? pBox->bbmax.x : pBox->bbmin.x,
										                  (k & 2) ? pBox->bbmax.y : pBox->bbmin.y,
										                  (k & 4) ? pBox->bbmax.z : pBox->bbmin.z);
										Math::VectorTransform(vLocal, aBones[pBox->bone], c[k]);
									}
									for (int k = 0; k < 8; k++)
										for (int bit = 1; bit < 8; bit <<= 1)
											if (!(k & bit)) Line3D(c[k], c[k | bit], tHb);
								}
							}
					}
			}
		}
	}
}

// World ESP: buildings (sentry/dispenser/teleporter, health bar + level), ammo packs, dropped
// weapons, thrown projectiles. Buildings + projectiles ride CEntities' already-populated groups
// (aim/helpers.cpp Store()); ammo packs/dropped weapons are NOT grouped there (that branch is
// commented out, helpers.cpp:710-722) so this walks the entity list itself, same pattern as
// aim_melee.cpp's own I::ClientEntityList loop -- no new hook, no new entity-create listener.
void CEsp::DrawWorld(CTFPlayer* pLocal)
{
	if (g_espBuildings)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingAll))
		{
			auto pObj = pEntity->As<CBaseObject>();
			if (!pObj || pObj->IsDormant())
				continue;

			const bool bTeammate = pObj->m_iTeamNum() == pLocal->m_iTeamNum();
			const float flDist = (pObj->GetAbsOrigin() - pLocal->GetAbsOrigin()).Length();
			const char* sName = "Building";
			switch (pObj->GetClassID())
			{
			case ETFClassID::CObjectSentrygun:  sName = "Sentry"; break;
			case ETFClassID::CObjectDispenser:  sName = "Dispenser"; break;
			case ETFClassID::CObjectTeleporter: sName = "Teleporter"; break;
			default: break;
			}
			Color_t tColor = ToColor(bTeammate ? g_espTeamColor : g_espBuildingColor);
			DrawBoxName(pObj, sName, tColor, flDist, true, pObj->m_iHealth(), pObj->m_iMaxHealth());
		}
	}

	if (g_espAmmoPacks || g_espDroppedWeapons)
	{
		const int iHighest = I::ClientEntityList->GetHighestEntityIndex();
		for (int i = 1; i <= iHighest; i++)
		{
			auto pEntity = I::ClientEntityList->GetClientEntity(i);
			if (!pEntity) continue;
			auto pBase = pEntity->As<CBaseEntity>();
			if (!pBase) continue;

			const bool bAmmo = pBase->GetClassID() == ETFClassID::CTFAmmoPack;
			const bool bDropped = pBase->GetClassID() == ETFClassID::CTFDroppedWeapon;
			if ((bAmmo && !g_espAmmoPacks) || (bDropped && !g_espDroppedWeapons) || (!bAmmo && !bDropped))
				continue;
			if (pBase->IsDormant())
				continue;

			const float flDist = (pBase->GetAbsOrigin() - pLocal->GetAbsOrigin()).Length();
			DrawBoxName(pBase, bAmmo ? "Ammo" : "Weapon", ToColor(g_espItemColor), flDist);
		}
	}

	if (g_espProjectiles)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
		{
			if (pEntity->IsDormant())
				continue;
			const float flDist = (pEntity->GetAbsOrigin() - pLocal->GetAbsOrigin()).Length();
			DrawBoxName(pEntity, "Projectile", ToColor(g_espProjColor), flDist);
		}
	}
}

// Grenade/projectile path draw: reuses F::ProjSim (aim/projsim.h), the SAME physics the
// projectile aimbot already simulates target hit-points with -- no re-derivation, just read the
// live projectile's info + RunTick the path out and draw the line, instead of aiming with it.
void CEsp::DrawProjectilePaths(CTFPlayer* pLocal)
{
	if (!g_espProjPath) return;

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldProjectile))
	{
		if (pEntity->IsDormant())
			continue;

		ProjectileInfo tInfo;
		F::ProjSim.GetInfo(pEntity, tInfo);
		if (!F::ProjSim.Initialize(tInfo))
			continue;

		// Cap, not tInfo.m_flLifetime (60s default -- thousands of ticks): a path visibly worth
		// drawing never needs more than ~3s/128 ticks before it leaves view or detonates.
		for (int n = 1; n <= 128; n++)
			F::ProjSim.RunTick(tInfo);
		if (tInfo.m_vPath.size() < 2)
			continue;

		Color_t tColor = ToColor(g_espProjPathColor);
		for (size_t i = 1; i < tInfo.m_vPath.size(); i++)
			Line3D(tInfo.m_vPath[i - 1], tInfo.m_vPath[i], tColor);
	}
}

// Out-of-fov arrows: Amalgam Features/Visuals/OffscreenArrows DrawArrowTo with its defaults
// (offset 100, max distance 1000, glyph Scale(25)), math in esp_math.h. Deviation: enemies that
// pass SDK::IsOnScreen are skipped -- they have a box, Amalgam draws both.
void CEsp::DrawOofArrows(CTFPlayer* pLocal)
{
	if (!g_espOofArrows) return;

	constexpr int   kOffset  = 100;
	constexpr float kMaxDist = 1000.f;
	const float flW = (float)H::Draw.m_nScreenW, flH = (float)H::Draw.m_nScreenH;
	const Vec3 vFrom = pLocal->GetEyePosition();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer || pPlayer->IsDormant() || !pPlayer->IsAlive())
			continue;
		if (SDK::IsOnScreen(pPlayer, pPlayer->GetAbsOrigin()))
			continue;

		const Vec3 vTo = pPlayer->GetCenter();
		Color_t tColor = ToColor(g_espOofColor);
		tColor.a = byte(tColor.a * EspMath::OofDistFade(vFrom.DistTo(vTo), kMaxDist));
		if (!tColor.a)
			continue;

		Vec3 vScreen;
		if (SDK::W2S(vTo, vScreen, true))   // in front of the camera: Amalgam fades by screen distance too
		{
			tColor.a = byte(tColor.a * EspMath::OofScreenFade(flW, flH, vScreen.x, vScreen.y));
			if (!tColor.a)
				continue;
		}

		const auto t = EspMath::OofArrowFromScreen(flW, flH, vScreen.x, vScreen.y, (float)kOffset, H::Draw.Scale(25));
		H::Draw.FillPolygon({
			{ Vector2D{ t.a[0],   t.a[1]   }, { 0, 0 } },
			{ Vector2D{ t.b[0],   t.b[1]   }, { 0, 0 } },
			{ Vector2D{ t.tip[0], t.tip[1] }, { 0, 0 } }
		}, tColor);
	}
}

// Sound pings: pushed from visuals.cpp's CSoundEmitterSystem_EmitSound hook (game thread),
// drawn here off the render-thread Paint hook -- EspFx_CopySoundPings is the cross-thread
// handoff (esp/esp_fx.h), a snapshot copy, never the shared array itself.
void CEsp::DrawSoundPings()
{
	if (!g_espSoundPings) return;

	EspFxSoundPing pings[kEspFxMaxSoundPings];
	const float flNow = EspFx_NowMs();
	constexpr float flLifeMs = 600.f;
	const int n = EspFx_CopySoundPings(pings, kEspFxMaxSoundPings, flNow, flLifeMs);

	for (int i = 0; i < n; i++)
	{
		const float flAge = (flNow - pings[i].bornMs) / flLifeMs;   // 0..1
		// expanding ground ring, 16 segments through W2S (see Line3D)
		const Vec3 vCenter(pings[i].x, pings[i].y, pings[i].z);
		const float flRadius = 4.f + flAge * 20.f;
		Color_t tColor = ToColor(g_espSoundColor);
		tColor.a = (byte)(tColor.a * (1.f - flAge));
		constexpr int kSeg = 16;
		for (int s = 0; s < kSeg; s++)
		{
			const float a0 = s * (6.2831853f / kSeg), a1 = (s + 1) * (6.2831853f / kSeg);
			Line3D(vCenter + Vec3(cosf(a0) * flRadius, sinf(a0) * flRadius, 0.f),
			       vCenter + Vec3(cosf(a1) * flRadius, sinf(a1) * flRadius, 0.f), tColor);
		}
	}
}

// Damage numbers: same handoff, for player_hurt (pushed from aim/events.cpp -- same thread
// as this draw call, but the queue is shared with the sound pings above so one lock covers both).
void CEsp::DrawDamageNumbers()
{
	if (!g_espDamageNumbers) return;

	EspFxDamageNum nums[kEspFxMaxDamageNums];
	const float flNow = EspFx_NowMs();
	constexpr float flLifeMs = 1000.f;
	const int n = EspFx_CopyDamageNums(nums, kEspFxMaxDamageNums, flNow, flLifeMs);

	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	for (int i = 0; i < n; i++)
	{
		const float flAge = (flNow - nums[i].bornMs) / flLifeMs;   // 0..1
		const Vec3 vWorld(nums[i].x, nums[i].y, nums[i].z + flAge * 40.f);   // rises
		Vec3 vScreen;
		if (!SDK::W2S(vWorld, vScreen))
			continue;

		const byte a = (byte)(255.f * (1.f - flAge));
		Color_t tColor = nums[i].crit ? Color_t{ 255, 60, 40, a } : ToColor(g_espDmgColor);
		if (!nums[i].crit) tColor.a = (byte)(tColor.a * (1.f - flAge));
		H::Draw.StringOutlined(fFont, (int)vScreen.x, (int)vScreen.y, tColor, { 0, 0, 0, a },
			ALIGN_CENTER, std::format("{}", nums[i].amount).c_str());
	}
}

void CEsp::Draw(CTFPlayer* pLocal)
{
	if (!pLocal) return;

	// once-per-session proof lines: first call proves the IEngineVGui_Paint path reaches here,
	// the second proves boxes actually got drawn (w/h > 0 after W2S).
	static bool s_bLoggedCall = false, s_bLoggedDrawn = false;
	if (!s_bLoggedCall && g_espEnable)
	{
		s_bLoggedCall = true;
		DebugLog("esp: Draw reached (players group=%d, screen %dx%d, box style %d)",
			int(H::Entities.GetGroup(EntityEnum::PlayerAll).size()), H::Draw.m_nScreenW, H::Draw.m_nScreenH, g_espBoxStyle);
	}

	if (g_espEnable)
	{
		m_nDrawn = 0;
		DrawPlayers(pLocal);
		if (!s_bLoggedDrawn && m_nDrawn > 0)
		{
			s_bLoggedDrawn = true;
			DebugLog("esp: drew %d players this frame", m_nDrawn);
		}
	}
	if (g_espWorldEnable)
		DrawWorld(pLocal);
	DrawProjectilePaths(pLocal);
	DrawOofArrows(pLocal);
	DrawSoundPings();
	DrawDamageNumbers();
}
