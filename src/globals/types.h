#pragma once
#include "common.h"
#include <cstdint>

// -- Config paths --------------------------------------------------------------
static constexpr const char* CONFIG_DIR  = "C:\\botox-tf2";
static constexpr const char* CONFIG_PATH = "C:\\botox-tf2\\default.cfg";

// -- Watermark position enum ---------------------------------------------------
enum WatermarkPos : int {
    WM_TOP_LEFT = 0, WM_TOP_CENTER, WM_TOP_RIGHT,
    WM_BOTTOM_LEFT,  WM_BOTTOM_CENTER, WM_BOTTOM_RIGHT,
};

// -- Entity list stub ------------------------------------------------------------
struct IClientEntityList {};

// -- Color correction curve ---------------------------------------------------------
// A mapping, not an offset: the identity diagonal is neutral. Lives here rather than in
// colorcorrect.h because globals.h declares the four of them and colorcorrect.h includes
// globals.h. See colorcorrect.h for the interpolation and the rules on point order.
inline constexpr int k_ccMaxPts = 12;

struct CcCurve {
    int   n;                 // 2 .. k_ccMaxPts
    float x[k_ccMaxPts];
    float y[k_ccMaxPts];
};

// -- Skinchanger: one weapon's whole look ----------------------------------------------
// Kept here rather than in data/skindata.h so globals.h can declare the map without pulling
// in that file's ~40KB of name tables. Every field is an econ attribute or a def-index
// switch -- see hacks/visuals/skins.cpp for which attribute each one becomes.
struct SkinCfg {
    bool     enabled    = false;
    int      paintKitId = 0;     // attr 834 paintkit_proto_def_index (int bits in a float)
    uint64_t pattern    = 0;     // attrs 866/867 custom_paintkit_seed_lo/_hi
    float    wear       = 0.2f;  // attr 725 set_item_texture_wear
    int      ksTier     = 0;     // attr 2025: 0 none, 1 basic, 2 specialized, 3 professional
    int      ksSheen    = 0;     // attr 2014 killstreak_idleeffect (1-7)
    int      ksEffect   = 0;     // attr 2013 killstreak_effect (1-7, professional only)
    // Not an attribute: the sheen tint AND the eye glow color both come out of one static
    // table in client.dll (g_KillStreakEffectsBase), so the row ksSheen picks is rewritten
    // with this color while the weapon is held -- see hacks/visuals/skins.cpp.
    bool     ksCustomColor = false;
    float    ksColor[3]    = { 1.f, 1.f, 1.f };   // 0..1 rgb
    // attrs 134 + 370 (set_attached_particle / _static). -1 = none. 0 IS a real schema id
    // ("Invalid Particle"), so it cannot double as the off value -- see data/particledata.h.
    int      unusual    = -1;
    bool     australium = false; // attrs 2027 is_australium_item + 542 item_style_override
    bool     festivized = false; // attr 2053 is_festivized
    bool     festive    = false; // swap the def index for the Festive variant (Scattergun 669)
    bool     statClock  = false; // attr 214 kill_eater + 292 kill_eater_score_type
    int      statKills  = 0;
    uint8_t  spellFlags = 0;     // kCfgSpell_Exorcism | kCfgSpell_Pumpkin | kCfgSpell_Fire
    int      weaponSkinDef = 0;  // 15xxx def for a weapon-specific skin (0 = generic warpaint)
    int      reskinDef     = 0;  // reskin variant def index (0 = stock, 741 = Rainblower, ...)
    int      reskinStyle   = 0;  // style index within that reskin (Black Rose 1 = the black one)
};

// -- Econ attribute list (skinchanger) -------------------------------------------------
// CAttributeList lives inside CEconEntity's m_AttributeManager; its offset comes from the
// DT_EconEntity netvar m_AttributeList. The void* pad at offset 0 is the vtable pointer --
// without it every field below is off by 8 and the writes land in the wrong place.
struct CEconItemAttribute
{
    void*    pad = nullptr;
#ifdef _WIN64
    uint32_t m_iAttributeDefinitionIndex = 0;
    uint32_t _align32 = 0;
#else
    uint16_t m_iAttributeDefinitionIndex = 0;
    uint16_t pad16 = 0;
#endif
    union { int m_iRawValue32; float m_flValue; };
    int m_nRefundableCurrency = 0;

    CEconItemAttribute(uint16_t idx, float val)
#ifdef _WIN64
        : pad(nullptr), m_iAttributeDefinitionIndex(idx), _align32(0),
          m_flValue(val), m_nRefundableCurrency(0) {}
#else
        : pad(nullptr), m_iAttributeDefinitionIndex(idx), pad16(0),
          m_flValue(val), m_nRefundableCurrency(0) {}
#endif
};

struct CUtlMemory_Attr { CEconItemAttribute* m_pMemory; int m_nAllocationCount; int m_nGrowSize; };

struct CUtlVector_Attr
{
    CUtlMemory_Attr     m_Memory;
    int                 m_Size;
    CEconItemAttribute* m_pElements;
    int  Count()   const { return m_Size; }
    bool IsEmpty() const { return m_Size == 0; }
};

struct CAttributeList
{
    void*           pad;
    CUtlVector_Attr m_Attributes;
    void*           m_pManager;

    int  Count()   const { return m_Attributes.Count(); }
    bool IsEmpty() const { return m_Attributes.IsEmpty(); }

    // Only ever grows into space the engine ALREADY allocated. AddToTail in Source uses
    // malloc+memcpy, never realloc, so growing an engine-owned buffer here would hand a
    // heap block from our CRT back to theirs -- refuse instead.
    void AddToTail(uint16_t index, float value)
    {
        if (m_Attributes.m_Size < 0 || m_Attributes.m_Size >= 48) return;
        if (m_Attributes.m_Size >= m_Attributes.m_Memory.m_nAllocationCount) return;
        if (!m_Attributes.m_Memory.m_pMemory) return;
        if (reinterpret_cast<uintptr_t>(m_Attributes.m_Memory.m_pMemory) < 0x10000) return;
        m_Attributes.m_Memory.m_pMemory[m_Attributes.m_Size++] = CEconItemAttribute(index, value);
    }

    void SetOrAdd(uint16_t index, float value)
    {
        if (m_Attributes.m_Memory.m_pMemory &&
            reinterpret_cast<uintptr_t>(m_Attributes.m_Memory.m_pMemory) >= 0x10000)
        {
            for (int i = 0; i < m_Attributes.m_Size; ++i)
            {
                if (m_Attributes.m_Memory.m_pMemory[i].m_iAttributeDefinitionIndex == index)
                {
                    m_Attributes.m_Memory.m_pMemory[i].m_flValue = value;
                    return;
                }
            }
        }
        AddToTail(index, value);
    }
};

// The engine's own schema path. Writing an attribute through these is what makes the
// killstreak / paintkit proxies notice it; SetOrAdd alone only edits the list.
using GetItemSchema_t            = void*(__fastcall*)();
using GetAttributeDefinition_t   = void*(__fastcall*)(void*, int);
using SetRuntimeAttributeValue_t = void (__fastcall*)(void*, void*, float);
using SetSequence_t              = void (__fastcall*)(void*, int);
using TF_IsHolidayActive_t       = bool (__fastcall*)(int);

// -- Client-side particle dispatch ------------------------------------------------------
// The Exorcism spell's ghosts are a SERVER effect, so an attribute write alone shows
// nothing off a Halloween server -- the particle has to be dispatched by hand.
struct CEffectData_Weather {
    float  m_vOrigin[3];
    float  m_vStart[3];
    float  m_vNormal[3];
    float  m_vAngles[3];
    int    m_fFlags;
    int    m_nEntIndex;
    float  m_flScale;
    float  m_flMagnitude;
    float  m_flRadius;
    int    m_nAttachmentIndex;
    short  m_nSurfaceProp;
    short  _pad4A;
    int    m_nMaterial;
    int    m_nDamageType;
    int    m_nHitBox;          // the particle system index goes here
    unsigned char m_nColor;
    bool   m_bCustomColors;
    char   _pad5A[2];
    float  m_CustomColors[6];
    bool   m_bControlPoint1;
    char   _pad75[3];
    int    m_eParticleAttachment;
    float  m_vecOffset[3];
};

using CTE_DispatchEffect_t      = int(__fastcall*)(const char*, const CEffectData_Weather&);
using Get_ParticleSystemIndex_t = int(__fastcall*)(const char*);
