#pragma once

// Flip World logic that touches no engine, so tests/flipworld_test.cpp can hold it to account.
// Kept byte-identical in fpsoverlay and SimpleVisual (src/hacks/visuals/flipmath.h in both;
// tools/check_vtables.py asserts it and checks every binary fact quoted here against the game).
#include <cmath>
#include <cstdint>

// Every engine number flipworld.cpp uses, in one place, each asserted against the installed game by
// tools/check_vtables.py (which also refuses a raw slot number in either flipworld.cpp).
namespace flipslot {
    inline constexpr int matGetBackBufferDimensions = 31;   // IMaterialSystem
    inline constexpr int matFindMaterial            = 71;
    inline constexpr int matCreateNamedRTEx         = 85;
    inline constexpr int matGetRenderContext        = 98;
    inline constexpr int ctxCopyRenderTargetToTex   = 17;   // IMatRenderContext (both contexts)
    inline constexpr int ctxDrawScreenSpaceRect     = 103;
    inline constexpr int texIncRef                  = 10;   // ITexture
    inline constexpr int texDecRef                  = 11;
    inline constexpr int matIncRef                  = 12;   // IMaterial
    inline constexpr int matDecRef                  = 13;
    inline constexpr int matIsErrorMaterial         = 42;
    inline constexpr int engWorldToScreenMatrix     = 36;   // IVEngineClient
    inline constexpr int engIsPlayingDemo           = 76;
    inline constexpr int engIsHLTV                  = 86;
    inline constexpr int rvPush2DView               = 39;   // IVRenderView (the 5-arg Push3DView overload sits at 37)
    inline constexpr int cmCreateMove               = 21;   // IClientMode
    inline constexpr int vrLevelInit                = 1;    // IViewRender
    inline constexpr int vrLevelShutdown            = 2;
    inline constexpr unsigned cvString              = 0x48; // ConVar parent: m_pszString
    inline constexpr unsigned cvInt                 = 0x58; //                m_nValue
    inline constexpr unsigned cmdSidemove           = 0x20; // CUserCmd
    inline constexpr unsigned cmdButtons            = 0x28;
    inline constexpr unsigned vsX                   = 0x00; // CViewSetup
    inline constexpr unsigned vsY                   = 0x08;
    inline constexpr unsigned vsWidth               = 0x10;
    inline constexpr unsigned vsHeight              = 0x18;
}

// ---- frame mirror ---------------------------------------------------------------------------

// IMatRenderContext::DrawScreenSpaceRectangle source texels for a left-right mirror of the view
// rectangle (x, y, w, h). The call's texcoords are INCLUSIVE: x0 is the texel shown at destx,
// x1 the one at destx+w-1 (materialsystem rva 0x7e940: u = (x0 + 0.5 - 0.5*tpp) / texW with
// tpp = (x1-x0)/(w-1)). Destination column c then samples texel x+w-1-c, and a point at screen
// x lands on 2x+w-x: for the full screen that is W - x, the column the HUD mirror uses.
inline void FlipMirrorSrcRect(int x, int y, int w, int h, float out[4])
{
    out[0] = float(x + w - 1);   // src x0, shown at the left edge
    out[1] = float(y);           // src y0
    out[2] = float(x);           // src x1, shown at the right edge
    out[3] = float(y + h - 1);   // src y1
}

// Whether the frame on screen went through the mirror, tracked by the frame's own events instead of
// the clock (a hitch or a slow startmovie frame must not drop the hold between two mirrored frames):
//   FrameStart  FRAME_RENDER_START, game thread, before RenderView
//   WorldPass   the 3D world was drawn this frame (ClientMode DoPostScreenSpaceEffects)
//   Mirror      the mirror drew (end of DoPostScreenSpaceEffects, right before DrawViewModels)
//   TwoD        IVRenderView::Push2DView: the 3D pass is over, the HUD paints next
// inWorldPass runs to TwoD, not to Mirror: DrawViewModels follows the mirror, and beams drawn with the
// viewmodel (unmirrored) still need the true WorldToScreenMatrix.
// A frame that drew the world and never reached the mirror (a hook gone) clears `mirrored` at the next
// FrameStart. A frame that drew no world keeps it: the freezecam shows a capture taken after the
// mirror, and a loading or minimised frame shows nothing at all.
struct FlipFrameTrack {
    bool mirrored     = false;
    bool worldPending = false;   // WorldPass seen, Mirror not yet
    bool inWorldPass  = false;   // FrameStart .. TwoD
};
inline void FlipTrackFrameStart(FlipFrameTrack& t)
{
    if (t.worldPending) { t.mirrored = false; t.worldPending = false; }
    t.inWorldPass = true;
}
inline void FlipTrackWorldPass(FlipFrameTrack& t) { t.worldPending = true; }
inline void FlipTrackMirror(FlipFrameTrack& t)    { t.mirrored = true; t.worldPending = false; }
inline void FlipTrackTwoD(FlipFrameTrack& t)      { t.inWorldPass = false; }
inline void FlipTrackReset(FlipFrameTrack& t)     { t.mirrored = false; t.worldPending = false; }

// ---- HUD mirror -----------------------------------------------------------------------------

// IVEngineClient::WorldToScreenMatrix, mirrored about the screen's vertical centre line.
// FrustumTransform (sdk view_scene.cpp:55) divides row 0 by row 3, and GetVectorInScreenSpace /
// GetVectorInHudSpace map that to 0.5 * (1 + x) * ScreenWidth (cdll_util.cpp:533,562). Negating
// row 0 negates x, which lands the point on ScreenWidth - x. GetVectorInHudSpace truncates that to
// (int)(W - a) = W - 1 - (int)a for a fractional a: the pixel column the frame mirror moved the
// point's pixel to. Row 3 (w) is untouched, so "behind the camera" does not change.
inline void FlipMirrorW2S(float m[4][4])
{
    for (int c = 0; c < 4; ++c) m[0][c] = -m[0][c];
}

// A world-to-VIEW matrix has row 3 = (0,0,0,1); a projection's row 3 carries the view forward
// axis. Refusing anything that is not a projection turns a wrong vtable slot into "HUD not
// mirrored" instead of HUD elements thrown across the screen.
inline bool FlipIsProjection(const float m[4][4])
{
    return m[3][0] != 0.f || m[3][1] != 0.f || m[3][2] != 0.f;
}

// Whether a WorldToScreenMatrix call gets the mirrored copy. The 3D pass itself calls it too:
// beams build their ribbon from screen-space directions (sdk beamdraw.cpp:1297) and sprite
// glows size their occlusion query from it (c_pixel_visibility.cpp:103). Those draw into the
// frame before the mirror, or after it with the unmirrored viewmodel, so they need the true matrix.
// On the game thread that window runs
// from FRAME_RENDER_START to the 2D push (FlipFrameTrack::inWorldPass); every other caller (HUD
// paint and think, overlays on the render thread) is looking at the mirrored picture.
inline bool FlipW2SMirrored(bool mirroring, bool onGameThread, bool inWorldPass)
{
    return mirroring && !(onGameThread && inWorldPass);
}

// HUD elements that place themselves from the view ANGLES instead of WorldToScreenMatrix.
// Each is a byte patch inside client.dll that makes the game compute the mirrored answer
// itself, toggled only on the game thread (vgui paint and think run there and nowhere else):
//   xpos  CTFMedicCallerPanel::GetCallerPosition == CHudDamageIndicator::GetDamagePosition
//         (the linker folded the two identical bodies, sdk tf_hud_mediccallers.cpp:229 /
//         tf_hud_damageindicator.cpp:151) and CTFFlagCalloutPanel::GetCalloutPosition
//         (tf_hud_flagstatus.cpp:1059): `*xpos = flRadius * -side` loses its negation, so the
//         target's side flips and atan2, the rotation and the final x all follow: 360 - rotation,
//         and x = (int)(W/2 - v), which is W - 1 - (int)(W/2 + v), the frame mirror's pixel.
//   arrow m_iDrawArrow = MainViewRight().Dot(...) > 0 ? RIGHT : LEFT (mediccallers.cpp:366,
//         flagstatus.cpp:1191): seta -> setbe.
//   side  CTFAnnotationsPanelCallout::PerformLayout pins an off-screen callout to the left edge
//         when flPerpDot <= 0 (annotationspanel.cpp:431): jbe -> ja. Its arrow image follows x.
//   arrow CTFArrowPanel::GetAngleRotation (flagstatus.cpp:167): the compass arrows around the flag
//         icons, the intel carrier's capture arrow, Robot Destruction. The angle is negated when
//         DotProduct(flag, right) < 0; jae -> jbe negates it on the other side instead.
//   arrow PASS Time's off-screen ball arrow (passtime_ball_offscreen_arrow.cpp:163): its screen
//         direction's x is -y / aspect, compiled as (-1.0 in xmm12) / aspect * y. xmm11 holds +1.0
//         in the same function (both loaded at its top, both callee-saved across the aspect call),
//         so `movaps xmm9, xmm12` -> `movaps xmm9, xmm11` negates x; the angle and edge point follow.
struct FlipHudPatch {
    const char*   what;
    const char*   sig;          // unique in client.dll; the patched bytes are wildcards, so it
                                //   still matches while some other copy holds the patch
    int           at;           // window start, from the sig match
    int           len;          // window length
    unsigned char orig[12];     // the window as the game ships it
    unsigned char flip[12];     // the window while the HUD is mirrored
};

inline constexpr FlipHudPatch k_flipHudPatches[] = {
    { "damage arcs + medic callers: side",
      "48 89 5C 24 08 57 48 83 EC 60 0F 29 74 24 50 49 8B F9 0F 29 7C 24 40 48 8B DA 0F 28 FA E8 ? ? ? ? E8 ? ? ? ? 45 33 C9",
      0xF3, 11,
      { 0xF3, 0x0F, 0x59, 0xCF,  0x0F, 0x57, 0xCE,  0xF3, 0x0F, 0x59, 0xDF },     // mulss xmm1,xmm7; xorps xmm1,xmm6; mulss xmm3,xmm7
      { 0xF3, 0x0F, 0x59, 0xCF,  0x0F, 0x1F, 0x00,  0xF3, 0x0F, 0x59, 0xDF } },   //                 nop dword [rax]
    { "flag callouts: side",
      "48 89 5C 24 08 57 48 83 EC 60 0F 29 74 24 50 49 8B F9 0F 29 7C 24 40 48 8B DA 0F 28 FA E8 ? ? ? ? 45 33 C9",
      0xEE, 11,
      { 0xF3, 0x0F, 0x59, 0xCF,  0x0F, 0x57, 0xCE,  0xF3, 0x0F, 0x59, 0xDF },
      { 0xF3, 0x0F, 0x59, 0xCF,  0x0F, 0x1F, 0x00,  0xF3, 0x0F, 0x59, 0xDF } },
    { "medic callers: arrow",
      "0F 2F C8 40 0F ? C6 FF C6 89 B7 ? ? 00 00",
      0, 9,
      { 0x0F, 0x2F, 0xC8,  0x40, 0x0F, 0x97, 0xC6,  0xFF, 0xC6 },                // comiss xmm1,xmm0; seta sil; inc esi
      { 0x0F, 0x2F, 0xC8,  0x40, 0x0F, 0x96, 0xC6,  0xFF, 0xC6 } },              //                   setbe sil
    { "flag callouts: arrow",
      "0F 2F C8 41 0F ? C6 41 FF C6 44 89 B6 ? ? 00 00",
      0, 10,
      { 0x0F, 0x2F, 0xC8,  0x41, 0x0F, 0x97, 0xC6,  0x41, 0xFF, 0xC6 },          // comiss xmm1,xmm0; seta r14b; inc r14d
      { 0x0F, 0x2F, 0xC8,  0x41, 0x0F, 0x96, 0xC6,  0x41, 0xFF, 0xC6 } },        //                   setbe r14b
    { "annotations: off-screen side",
      "45 0F 2F E0 66 0F 6E C0 0F 5B C0 F3 0F 59 C7 F3 0F 2C F8 ? 27 E8",
      0x0F, 6,
      { 0xF3, 0x0F, 0x2C, 0xF8,  0x76, 0x27 },                                   // cvttss2si edi,xmm0; jbe +0x27
      { 0xF3, 0x0F, 0x2C, 0xF8,  0x77, 0x27 } },                                 //                     ja  +0x27
    { "flag compass arrows: side",
      "45 0F 2F C5 44 0F 28 84 24 ? ? ? ? ? 07 0F 57 05",
      0x0D, 2,
      { 0x73, 0x07 },                                                            // jae +7 (skips the negation)
      { 0x76, 0x07 } },                                                          // jbe +7
    { "passtime ball arrow: side",
      "FF 90 F8 02 00 00 F3 0F 10 25 ? ? ? ? F3 44 0F 58 D7 45 0F 28 ? F3 44 0F 5E C8",
      0x13, 4,
      { 0x45, 0x0F, 0x28, 0xCC },                                                // movaps xmm9, xmm12 (-1.0)
      { 0x45, 0x0F, 0x28, 0xCB } },                                              // movaps xmm9, xmm11 (+1.0)
};
inline constexpr int k_flipHudPatchCount = int(sizeof(k_flipHudPatches) / sizeof(k_flipHudPatches[0]));

inline const unsigned char* FlipHudPatchBytes(const FlipHudPatch& p, bool mirrored)
{
    return mirrored ? p.flip : p.orig;
}

// ---- input ------------------------------------------------------------------------------------

// A/D under the mirror: the command is made to describe what the player sees. sidemove is
// negated and IN_MOVELEFT / IN_MOVERIGHT (sdk in_buttons.h: 1<<9, 1<<10) trade places, so
// anything reading the buttons (movement assists, the game's own code) agrees with sidemove.
static constexpr int k_flipInMoveLeft  = 1 << 9;
static constexpr int k_flipInMoveRight = 1 << 10;
inline void FlipMirrorCmd(float& sidemove, int& buttons)
{
    sidemove = -sidemove;
    const int lr = buttons & (k_flipInMoveLeft | k_flipInMoveRight);
    if (lr == k_flipInMoveLeft || lr == k_flipInMoveRight)
        buttons ^= k_flipInMoveLeft | k_flipInMoveRight;
}

// Mouse yaw: m_yaw's sign, owned. held = the value we wrote, 0 = nothing held (in/out).
// Returns true with *out = the value to write. Only a value this code wrote is ever put back,
// so a player who plays with a negative m_yaw keeps it whenever flip world is off.
//   - activating: hold the negation of whatever m_yaw is (player's own, either sign)
//   - held, but m_yaw no longer reads our value: the player (or a cfg) set a new one, which is
//     their own now -- hold its negation
//   - releasing: put the player's own back, unless m_yaw was changed since we wrote it
inline bool FlipYawStep(bool active, float cur, float& held, float* out)
{
    if (active) {
        if (held != 0.f && cur == held) return false;
        if (cur == 0.f) { held = 0.f; return false; }
        held = -cur;
        *out = held;
        return true;
    }
    if (held == 0.f) return false;
    const bool ours = cur == held;
    held = 0.f;
    if (!ours) return false;
    *out = -cur;
    return true;
}

// ---- hand ---------------------------------------------------------------------------------------

// Which cl_flipviewmodels to hold. Returns false for "hold nothing".
//   mirroring   the frame on screen went through the mirror (FlipFrameTrack)
//   cfgGuard    the config writer hook is in, so config.cfg can never save the held hand
//   hltv        SourceTV (IVEngineClient 86): the viewmodel follows the spectated player's own
//               networked hand, the cvar does nothing for it
//   demo        a demo (76 IsPlayingDemo): its projectiles were recorded, so hold the recorded
//               hand (DT_TFPlayer m_bFlipViewModels, recordedHand 0/1, -1 unknown = nothing)
//   live        target -1: the inverse of the player's own
inline bool FlipHandTarget(bool mirroring, bool cfgGuard, bool hltv, bool demo,
                           int recordedHand, int& target)
{
    target = -1;
    if (!mirroring || !cfgGuard || hltv) return false;
    if (demo) {
        if (recordedHand != 0 && recordedHand != 1) return false;
        target = recordedHand;
    }
    return true;
}

// cl_flipviewmodels hold -- the STRING, the one the server hears (NET_SetConVar) and config.cfg reads.
// The world is mirrored and the viewmodel is not (DrawViewModels runs after the mirror), so the
// projectiles have to leave from the side opposite the gun, and the mirror carries them back to it:
//   live     the server spawns rockets/stickies from whatever hand we send, so hold the INVERSE
//            of the player's own.
//   POV demo projectiles were recorded from the hand in the player's networked m_bFlipViewModels,
//            so hold exactly that hand; they appear on the other side, and so is the gun (FlipClientHand).
//   active  hold wanted (FlipHandTarget)
//   cur     cl_flipviewmodels right now, 0 or 1
//   target  -1 = the inverse of the player's own (live), 0/1 = that hand (demo)
//   pref    the player's own value while held, -1 = nothing held (in/out)
// Returns the value to write, or -1 to leave the cvar alone. Nothing is held while the cvar
// already reads the wanted hand.
//
// While held, a cur that is not the wanted hand means something put a hand back (a class cfg run
// at spawn, the console while dead): write it again. A player typing a hand while held cannot be
// told apart from our own write, so release restores the value they had when the hold began.
inline int FlipVmStep(bool active, int cur, int target, int& pref)
{
    if (active) {
        const int own  = pref < 0 ? cur : pref;
        const int want = target < 0 ? (own ? 0 : 1) : target;
        if (cur == want) return -1;
        pref = own;
        return want;
    }
    if (pref < 0) return -1;
    const int back = pref;
    pref = -1;
    return back;
}

// The client's own copy: m_nValue/m_fValue (+0x58/+0x54), read by TeamFortress_ShouldFlipClientViewModel
// (c_baseviewmodel.cpp:108 -> ShouldFlipViewModel bones + cull, jiggle bones, client IsViewModelFlipped).
// While a hand is held it is poked to the OTHER hand (for live play: the player's own), so the gun draws
// unmirrored on the side the mirrored projectiles appear; otherwise it matches the string again.
//   holding   hold wanted this tick (FlipHandTarget)
//   str       the string's hand after this tick's write, 0/1
//   clientInt m_nValue right now
// Returns the value to poke, or -1 when it already reads right. Only the bool matters to the game.
inline int FlipClientHand(bool holding, int str, int clientInt)
{
    const int want = holding ? (str ? 0 : 1) : (str ? 1 : 0);
    return (clientInt != 0 ? 1 : 0) == want ? -1 : want;
}

// ---- weapon sway ----------------------------------------------------------------------------------

// CTFViewModel::CalcViewModelLag (tf_viewmodel.cpp:85, client C_TFViewModel vtable 212) adds
// forward*x + right*-y + up*z to the viewmodel origin, the axes from AngleVectors(angles)
// (mathlib_base.cpp:948 for right). Under the mirror the mouse yaw is inverted, so the unmirrored gun
// sways against the turn the player sees. Reflecting the lag across the view's right axis flips the
// right term and keeps forward/up: before = origin on entry, origin = after the original ran.
inline void FlipMirrorSway(const float before[3], float origin[3], const float angles[3])
{
    const double d2r = 3.14159265358979323846 / 180.0;
    const double sp = std::sin(angles[0] * d2r), cp = std::cos(angles[0] * d2r);
    const double sy = std::sin(angles[1] * d2r), cy = std::cos(angles[1] * d2r);
    const double sr = std::sin(angles[2] * d2r), cr = std::cos(angles[2] * d2r);
    const double r[3] = { -sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp };
    const double d[3] = { double(origin[0]) - before[0], double(origin[1]) - before[1], double(origin[2]) - before[2] };
    const double dot = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
    for (int i = 0; i < 3; ++i) origin[i] = float(before[i] + d[i] - 2.0 * dot * r[i]);
}

// config.cfg while a hand is held. The hold is a real SetValue, so the int, the float and the
// string all read the held hand -- which is what the engine's userinfo resend at signon, the
// server's cvar queries and every reader in the client must see. Only config.cfg must not:
// CCvarUtilities::WriteVariables (engine rva 0x1777b0) prints m_pszString ([parent+0x48]) of
// every FCVAR_ARCHIVE cvar whose string differs from its default. Around that one call the
// string's digit is swapped for the player's own and put straight back. No flag is touched, so
// nothing else (the aim tree's flag snapshot included) can restore a stale one.
// Returns the digit to put in s[0] for the write, or 0 to leave the string alone.
inline char FlipCfgDigit(int pref, const char* s)
{
    if (pref != 0 && pref != 1) return 0;
    if (!s || (s[0] != '0' && s[0] != '1') || s[1] != '\0') return 0;
    const char own = pref ? '1' : '0';
    return s[0] == own ? 0 : own;
}

// The swap itself, around write(). s is the cvar's live string buffer.
template <class Write>
inline void FlipCfgWrite(char* s, int pref, Write&& write)
{
    const char own = FlipCfgDigit(pref, s);
    if (!own) { write(); return; }
    const char held = s[0];
    s[0] = own;
    write();
    if (s[0] == own) s[0] = held;
}

// ---- the tick ---------------------------------------------------------------------------------------
// Everything flipworld.cpp decides once per tick (CreateMove, FRAME_RENDER_START, level shutdown,
// eject), written against an engine interface so the gate test can drive it:
//   bool  TakeOwner();  void DropOwner();          one flip world per process (both DLLs carry it)
//   bool  HasYaw();  float Yaw();  void SetYaw(float);
//   void  SetHudPatches(bool mirrored);
//   bool  HasHand(); int Hand();  bool SetHand(int value, int cur, int prefAfter, int target);
//                                  Hand() = the STRING's hand, never m_nValue (that one is poked)
//   int   ClientHand();  void SetClientHand(int value);   m_nValue/m_fValue, raw (ConVar::ForceValue)
//   bool  IsHLTV();  bool IsDemo();  int RecordedHand();
//   void  SaveOwnHandForConfig(int own);           eject fallback: config.cfg's digit only
struct FlipCore {
    FlipFrameTrack track;
    bool  owner      = false;
    float yawHeld    = 0.f;   // FlipYawStep
    int   vmPref     = -1;    // FlipVmStep: the player's own hand while one is held
    bool  hudPatched = false;
    bool  cfgSaved   = false; // SaveOwnHandForConfig already ran for this hold
    bool  clientPoked = false; // m_nValue poked to the other hand (FlipClientHand)
};

struct FlipCoreIn {
    bool wanted;          // flip world on, render target ready, in game, not ejecting
    bool cfgGuard;       // the config.cfg writer hook is in
    bool onGameThread;
    bool gameThreadGone;  // off the game thread, and it has not ticked for a second (eject fallback)
    // botox "gun other side" (m_flip_world_mirror_hand): the string is left at the player's own hand
    // (the server spawns from it, the mirror carries the projectiles across) and only the client int
    // is poked to the other hand, so the unmirrored gun draws on that same far side.
    bool gunOtherSide = false;
};

inline bool FlipHolding(const FlipCore& c)
{
    return c.vmPref >= 0 || c.yawHeld != 0.f || c.hudPatched || c.clientPoked;
}

// Returns whether the frame counts as mirrored (input, W2S and the HUD patches follow it).
// Off the game thread nothing is ever TAKEN. Once the game thread is gone, the float and the HUD
// bytes may be put back from there (nothing else is executing them), and the hand -- whose release
// is a net message -- only gets config.cfg's digit, so a quit after that eject saves the player's own.
template <class Env>
inline bool FlipCoreTick(FlipCore& c, Env& env, const FlipCoreIn& in)
{
    if (in.onGameThread && in.wanted && !c.owner) c.owner = env.TakeOwner();
    const bool active = in.wanted && c.owner;
    if (!active) FlipTrackReset(c.track);
    const bool mirroring = active && c.track.mirrored;
    const bool mayWrite  = in.onGameThread || (in.gameThreadGone && !mirroring);

    if (mayWrite && env.HasYaw()) {
        float out = 0.f;
        if (FlipYawStep(mirroring, env.Yaw(), c.yawHeld, &out)) env.SetYaw(out);
    }
    if (mayWrite && c.hudPatched != mirroring) {
        env.SetHudPatches(mirroring);
        c.hudPatched = mirroring;
    }

    if (env.HasHand()) {
        if (in.onGameThread) {
            bool hltv = false, demo = false;
            int  recorded = -1;
            if (mirroring) {
                hltv = env.IsHLTV();
                demo = !hltv && env.IsDemo();
                if (demo) recorded = env.RecordedHand();
            }
            int target = -1;
            const bool want = FlipHandTarget(mirroring, in.cfgGuard, hltv, demo, recorded, target) && !in.gunOtherSide;
            const bool pokeOther = want || (mirroring && in.gunOtherSide);   // int to the other hand of the string
            int pref = c.vmPref;
            const int cur = env.Hand();
            const int value = FlipVmStep(want, cur, target, pref);
            int str = cur;
            if (value >= 0) {
                // Un-poke first: SetValue(int) is InternalSetIntValue, which returns without touching the
                // string when the int already equals the value (convar.cpp:957) -- and a poked int reads
                // exactly the hand a release writes.
                if ((env.ClientHand() != 0 ? 1 : 0) != cur) env.SetClientHand(cur);
                if (env.SetHand(value, cur, pref, target)) {
                    c.vmPref   = pref;           // a failed write is retried next tick from the same state
                    c.cfgSaved = false;
                    str = value;
                }
            }
            // Every tick, not once: a class cfg's `cl_flipviewmodels x` rewrites the int too. Only an int
            // this copy poked is ever put back, so the other DLL's tick never undoes the owner's poke.
            if (pokeOther || c.clientPoked) {
                const int poke = FlipClientHand(pokeOther, str, env.ClientHand());
                if (poke >= 0) env.SetClientHand(poke);
                c.clientPoked = pokeOther;
            }
        } else if (c.vmPref >= 0 && !c.cfgSaved) {
            env.SaveOwnHandForConfig(c.vmPref);
            c.cfgSaved = true;
        }
    }

    if (c.owner && !in.wanted && (in.onGameThread || in.gameThreadGone) && !FlipHolding(c)) {
        env.DropOwner();
        c.owner = false;
    }
    return mirroring;
}
