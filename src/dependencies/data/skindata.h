#pragma once
#include <cstdint>
// SkinCfg itself lives in types.h -- globals.h declares g_skinCfgs and must not drag these
// ~40KB of tables into every translation unit. Only skins.cpp and menu.cpp include this.

// All TF2 weapons. redir==stock means no warpaint support (warpaint UI grayed out).
// canAustralia: has official Australium version. canFestivize: can receive Festivizer.
// canPaint: supports paintkit at stock defindex (no redirect needed, e.g. Ubersaw).
// spells: bitmask of which Halloween spell types are applicable to this weapon.
static constexpr uint8_t kSpell_Pumpkin = 1; // WpnEntry::spells -- weapon supports Pumpkin Bombs (Demo explosives, Engi melee, Soldier rockets)
static constexpr uint8_t kSpell_Fire    = 2; // WpnEntry::spells -- weapon supports Halloween Fire (Pyro primaries)
// SkinCfg::spellFlags -- which spells are selected. Exorcism always available; Pumpkin/Fire gated by WpnEntry::spells.
static constexpr uint8_t kCfgSpell_Exorcism = 0x01; // halloween_death_ghosts       (attr 1009) -- dispatched manually on kill
static constexpr uint8_t kCfgSpell_Pumpkin  = 0x02; // halloween_pumpkin_explosions (attr 1007) -- client-side projectile effect
static constexpr uint8_t kCfgSpell_Fire     = 0x04; // halloween_green_flames       (attr 1008) -- client-side flame effect
struct WpnEntry { const char* name; int stock; int redir; bool canAustralia; bool canFestivize; bool canPaint = false; int festiveIdx = 0; uint8_t spells = 0; };
static const WpnEntry k_wpnList[] = {
    //                                         stock   redir  aus    fest
    // -- Scout ----------------------------------------------------------
    { "Scattergun",                              13,   200,  true,  true,  false, 669  },
    { "Force-a-Nature",                          45,    45,  true,  true,  false, 1078 },
    { "Shortstop",                              220,   220,  false, true,  true  },
    { "Soda Popper",                            448,   448,  false, true,  true  },
    { "Baby Face's Blaster",                    772,   772,  false, true  },
    { "Back Scatter",                          1103,  1103,  false, true  },
    { "Pistol",                                  23,   209,  false, true  },
    { "Winger",                                 449,   449,  false, true,  true  },
    { "Pretty Boy's Pocket Pistol",             773,   773,  false, false },
    { "Bonk! Atomic Punch",                      46,    46,  false, false, false, 1145 },
    { "Crit-a-Cola",                            163,   163,  false, false },
    { "Mad Milk",                               222,   222,  false, false },
    { "Bat",                                      0,   190,  false, true,  false, 660  },
    { "Sandman",                                 44,    44,  false, true  },
    { "Holy Mackerel",                          221,   221,  false, true,  true,  999  },
    { "Candy Cane",                             317,   317,  false, true  },
    { "Boston Basher",                          325,   325,  false, true  },
    { "Sun-on-a-Stick",                         349,   349,  false, true  },
    { "Fan O'War",                              355,   355,  false, true  },
    { "Atomizer",                               450,   450,  false, false },
    { "Three-Rune Blade",                       452,   452,  false, true  },
    { "Wrap Assassin",                          648,   648,  false, true  },
    // -- Soldier --------------------------------------------------------->"->
    { "Rocket Launcher",                         18,   205,  true,  true,  false, 658,  kSpell_Pumpkin },
    { "Direct Hit",                             127,   127,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Black Box",                              228,   228,  true,  true,  true,  1085, kSpell_Pumpkin },
    { "Rocket Jumper",                          237,   237,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Liberty Launcher",                       414,   414,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Cow Mangler 5000",                       441,   441,  false, true,  false, 0,    kSpell_Pumpkin },
    { "The Original",                           513,   513,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Beggar's Bazooka",                       730,   730,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Air Strike",                            1104,  1104,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Shotgun",                                 10,   199,  false, true,  false, 1141 },
    { "Reserve Shooter",                        415,   415,  false, true,  true  },
    { "Panic Attack",                          1153,  1153,  false, true  },
    { "Buff Banner",                            129,   129,  false, false, false, 1001 },
    { "Battalion's Backup",                     226,   226,  false, false },
    { "Concheror",                              354,   354,  false, false },
    { "Gunboats",                               133,   133,  false, false },
    { "Righteous Bison",                        442,   442,  false, true  },
    { "Mantreads",                              444,   444,  false, false },
    { "BASE Jumper",                           1101,  1101,  false, false },
    { "Shovel",                                   6,   196,  false, true  },
    { "Equalizer",                              128,   128,  false, true  },
    { "Pain Train",                             154,   154,  false, true  },
    { "Half-Zatoichi",                          357,   357,  false, false },
    { "Market Gardener",                        416,   416,  false, true  },
    { "Disciplinary Action",                    447,   447,  false, true,  true  },
    { "Escape Plan",                            775,   775,  false, true  },
    // -- Pyro ------------------------------------------------------------
    { "Flamethrower",                            21,   208,  true,  true,  false, 659,  kSpell_Fire },
    { "Backburner",                              40,    40,  false, true,  false, 1146, kSpell_Fire },
    { "Degreaser",                              215,   215,  false, true,  true,  0,    kSpell_Fire },
    { "Phlogistinator",                         594,   594,  false, true,  false, 0,    kSpell_Fire },
    { "Dragon's Fury",                         1178,  1178,  false, true,  false, 0,    kSpell_Fire },
    { "Nostromo Napalmer",                    30474, 30474,  false, true,  false, 0,    kSpell_Fire },
    { "Flare Gun",                               39,    39,  false, true,  false, 1081 },
    { "Detonator",                              351,   351,  false, true,  true  },
    { "Manmelter",                              595,   595,  false, true  },
    { "Scorch Shot",                            740,   740,  false, true,  true  },
    { "Thermal Thruster",                      1179,  1179,  false, false },
    { "Gas Passer",                            1180,  1180,  false, false },
    { "Fire Axe",                                 2,   192,  false, true  },
    { "Axtinguisher",                            38,    38,  true,  true,  false, 1000 },
    { "Postal Pummeler",                            457,    457,  false,  true },
    { "Homewrecker",                            153,   153,  false, true  },
    { "Powerjack",                              214,   214,  false, true,  true  },
    { "Back Scratcher",                         326,   326,  false, true,  true  },
    { "Sharpened Volcano Fragment",             348,   348,  false, true  },
    { "Third Degree",                           593,   593,  false, true  },
    { "Neon Annihilator",                       813,   813,  false, true  },
    { "Hot Hand",                              1181,  1181,  false, false },
    // -- Demoman --------------------------------------------------------->"->
    { "Grenade Launcher",                        19,   206,  true,  true,  false, 1007, kSpell_Pumpkin },
    { "Loch-n-Load",                            308,   308,  false, true,  true,  0,    kSpell_Pumpkin },
    { "Loose Cannon",                           996,   996,  false, true,  true,  0,    kSpell_Pumpkin },
    { "Iron Bomber",                           1151,  1151,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Stickybomb Launcher",                     20,   207,  true,  true,  false, 661,  kSpell_Pumpkin },
    { "Scottish Resistance",                    130,   130,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Chargin' Targe",                         131,   131,  false, false, false, 1144 },
    { "Sticky Jumper",                          265,   265,  false, true  },
    { "Splendid Screen",                        406,   406,  false, false },
    { "Tide Turner",                           1099,  1099,  false, false },
    { "Quickiebomb Launcher",                  1150,  1150,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Bottle",                                   1,   191,  false, true  },
    { "Eyelander",                              132,   132,  true,  true,  false, 1082 },
    { "Scotsman's Skullcutter",                 172,   172,  false, true,  true  },
    { "Ullapool Caber",                         307,   307,  false, true  },
    { "Claidheamh Mor",                         327,   327,  false, true,  true  },
    { "Persian Persuader",                      404,   404,  false, true,  true  },
    { "Nessie's Nine Iron",                     482,   482,  false, true  },
    { "Scottish Handshake",                     609,   609,  false, true  },
    // -- Heavy ------------------------------------------------------------
    { "Minigun",                                 15,   202,  true,  true,  false, 654  },
    { "Natascha",                                41,    41,  false, true  },
    { "Iron Curtain",                           298,   298,  false, true  },
    { "Brass Beast",                            312,   312,  false, true,  true  },
    { "Tomislav",                               424,   424,  true,  true,  true  },
    { "Huo-Long Heater",                        811,   811,  false, true  },
    { "Sandvich",                                42,    42,  false, false, false, 1002 },
    { "Dalokohs Bar",                           159,   159,  false, false },
    { "Buffalo Steak Sandvich",                 311,   311,  false, false },
    { "Family Business",                        425,   425,  false, true,  true  },
    { "Second Banana",                         1190,  1190,  false, false },
    { "Fists",                                    5,   195,  false, false },
    { "Killing Gloves of Boxing",                43,    43,  false, true  },
    { "Gloves of Running Urgently",             239,   239,  false, true,  false, 1084 },
    { "Warrior's Spirit",                       310,   310,  false, false },
    { "Fists of Steel",                         331,   331,  false, true  },
    { "Eviction Notice",                        426,   426,  false, false },
    { "Apoco-Fists",                            587,   587,  false, true  },
    { "Holiday Punch",                          656,   656,  false, false },
    // -- Engineer --------------------------------------------------------->"->
    { "Shotgun (Engi)",                           9,   199,  false, true  },
    { "Frontier Justice",                       141,   141,  true,  false, false, 1004 },
    { "Widowmaker",                             527,   527,  false, true  },
    { "Pomson 6000",                            588,   588,  false, false },
    { "Rescue Ranger",                          997,   997,  false, true  },
    { "Pistol (Engi)",                           22,   209,  false, true  },
    { "Wrangler",                               140,   140,  false, true,  false, 1086 },
    { "Short Circuit",                          528,   528,  false, false },
    { "Wrench",                                   7,   197,  true,  true,  false, 662,  kSpell_Pumpkin },
    { "Gunslinger",                             142,   142,  false, false, false, 0,    kSpell_Pumpkin },
    { "Southern Hospitality",                   155,   155,  false, true,  false, 0,    kSpell_Pumpkin },
    { "Jag",                                    329,   329,  false, true,  true,  0,    kSpell_Pumpkin },
    { "Eureka Effect",                          589,   589,  false, true,  false, 0,    kSpell_Pumpkin },
    // -- Medic ------------------------------------------------------------
    { "Medi Gun",                                29,   211,  true,  true,  false, 663  },
    { "Kritzkrieg",                              35,    35,  false, true  },
    { "Quick-Fix",                              411,   411,  false, true  },
    { "Vaccinator",                             998,   998,  false, true  },
    { "Syringe Gun",                             17,    17,  false, false },
    { "Blutsauger",                              36,    36,  true,  true  },
    { "Crusader's Crossbow",                    305,   305,  false, true,  true,  1079 },
    { "Overdose",                               412,   412,  false, true  },
    { "Bonesaw",                                  8,   198,  false, true,  false, 1143 },
    { "Ubersaw",                                 37,    37,  false, true,  true,  1003 },
    { "Vita-Saw",                               173,   173,  false, true  },
    { "Amputator",                              304,   304,  false, true,  true  },
    { "Solemn Vow",                             413,   413,  false, true  },
    // -- Sniper ----------------------------------------------------------->"->
    { "Sniper Rifle",                            14,   201,  true,  true,  false, 664  },
    { "Huntsman",                                56,    56,  false, true,  false, 1005 },
    { "Sydney Sleeper",                         230,   230,  false, true  },
    { "Bazaar Bargain",                         402,   402,  false, true,  true  },
    { "Machina",                                526,   526,  false, true  },
    { "Hitman's Heatmaker",                     752,   752,  false, true  },
    { "AWPer Hand",                             851,   851,  false, true  },
    { "Fortified Compound",                    1092,  1092,  false, true  },
    { "Classic",                               1098,  1098,  false, true  },
    { "SMG",                                     16,   203,  true,  true,  false, 1149 },
    { "Cleaner's Carbine",                      751,   751,  false, true  },
    { "Razorback",                               57,    57,  false, false },
    { "Jarate",                                  58,    58,  false, false, false, 1083 },
    { "Darwin's Danger Shield",                 231,   231,  false, false },
    { "Cozy Camper",                            642,   642,  false, false },
    { "Kukri",                                    3,   193,  false, true  },
    { "Bushwacka",                              232,   232,  false, true  },
    { "Tribalman's Shiv",                       171,   171,  false, true  },
    { "Shahanshah",                             401,   401,  false, true,  true  },
    // -- Spy --------------------------------------------------------------
    { "Revolver",                                24,   210,  false, true,  false, 1142 },
    { "Ambassador",                              61,    61,  true,  true,  false, 1006 },
    { "L'Etranger",                             224,   224,  false, true  },
    { "Enforcer",                               460,   460,  false, true  },
    { "Diamondback",                            525,   525,  false, true  },
    { "Knife",                                    4,   194,  true,  true,  false, 665  },
    { "Your Eternal Reward",                    225,   225,  false, true  },
    { "Conniver's Kunai",                       356,   356,  false, true  },
    { "Big Earner",                             461,   461,  false, true  },
    { "Wanga Prick",                            574,   574,  false, true  },
    { "Sharp Dresser",                          638,   638,  false, true  },
    { "Spy-cicle",                              649,   649,  false, true  },
    { "Invis Watch",                             30,   212,  false, false },  // redir=212 (Upgradeable TF_WEAPON_INVIS)
    { "Dead Ringer",                             59,    59,  false, false },
    { "Cloak and Dagger",                        60,    60,  false, false },
    { "Enthusiast's Timepiece",                 297,   297,  false, false },
    { "Quackenbirdt",                           947,   947,  false, false },
    { "Sapper",                                 735,   736,  false, false,  false, 1080 }, // redir=736 (Upgradeable), festive=1080
    { "Ap-Sap",                                 933,   933,  false, false },
    { "Snack Attack",                          1102,  1102,  false, false },
    // -- Demoman boots ----------------------------------------------------
    { "Ali Baba's Wee Booties",                 405,   405,  false, false },
    { "Bootlegger",                             608,   608,  false, false },
    // -- All-class melee / misc --------------------------------------------
    { "Conscientious Objector",                 474,   474,  false, true  },
    { "Freedom Staff",                          880,   880,  false, true  },
    { "Bat Outta Hell",                         939,   939,  false, true  },
    { "Ham Shank",                             1013,  1013,  false, true  },
    { "Necro Smasher",                         1123,  1123,  false, true  },
    { "Crossing Guard",                        1127,  1127,  false, true  },
    { "Saxxy",                                  423,   423,  false, true  },
    { "Frying Pan",                             264,   264,  false, true  },
    { "Golden Frying Pan",                     1071,  1071,  false, true  },
    { "Memory Maker",                           954,   954,  false, false },
    { "Prinny Machete",                       30758, 30758,  false, true  },
};
static constexpr int k_wpnCount = (int)(sizeof(k_wpnList)/sizeof(k_wpnList[0]));

// Festivizer lights are the def's attached_models_festive and nothing else
// (CEconEntity::UpdateAttachmentModels, econ_entity.cpp:1107): a def missing from this table
// draws no lights however attr 2053 is set -- every Festive variant (669...), most reskins.
// Generated by tools/gen_festivedefs.py -- every def with attached_models_festive. Sorted.
static const int k_festiveDefs[] = {
        1,    35,    36,    37,    38,    39,    40,    41,    43,    44,    45,    56,    61,   127,
      128,   130,   132,   140,   153,   154,   155,   171,   172,   173,   190,   191,   192,   193,
      194,   196,   197,   198,   199,   200,   201,   202,   203,   205,   206,   207,   208,   209,
      210,   211,   214,   215,   220,   221,   224,   225,   228,   230,   232,   237,   239,   264,
      265,   298,   304,   305,   307,   308,   312,   317,   325,   326,   327,   329,   331,   348,
      349,   351,   355,   356,   401,   402,   404,   411,   412,   413,   414,   415,   416,   423,
      424,   425,   441,   442,   447,   448,   449,   452,   457,   460,   461,   466,   474,   482,
      513,   525,   526,   527,   572,   574,   587,   589,   593,   594,   595,   609,   638,   648,
      649,   727,   730,   740,   751,   752,   772,   775,   811,   813,   832,   834,   851,   880,
      939,   996,   997,   998,  1013,  1071,  1092,  1098,  1103,  1104,  1123,  1127,  1150,  1151,
     1153,  1178,  1206, 15000, 15001, 15002, 15003, 15004, 15005, 15006, 15007, 15008, 15009, 15010,
    15011, 15012, 15013, 15014, 15015, 15016, 15017, 15018, 15019, 15020, 15021, 15022, 15023, 15024,
    15025, 15026, 15027, 15028, 15029, 15030, 15031, 15032, 15033, 15034, 15035, 15036, 15037, 15038,
    15039, 15040, 15041, 15042, 15043, 15044, 15045, 15046, 15047, 15048, 15049, 15050, 15051, 15052,
    15053, 15054, 15055, 15056, 15057, 15058, 15059, 15060, 15061, 15062, 15063, 15064, 15065, 15066,
    15067, 15068, 15069, 15070, 15071, 15072, 15073, 15074, 15075, 15076, 15077, 15078, 15079, 15080,
    15081, 15082, 15083, 15084, 15085, 15086, 15087, 15088, 15089, 15090, 15091, 15092, 15094, 15095,
    15096, 15097, 15098, 15099, 15100, 15101, 15102, 15103, 15104, 15105, 15106, 15107, 15108, 15109,
    15110, 15111, 15112, 15113, 15114, 15115, 15116, 15117, 15118, 15119, 15120, 15121, 15122, 15123,
    15124, 15125, 15126, 15127, 15128, 15129, 15130, 15131, 15132, 15133, 15134, 15135, 15136, 15137,
    15138, 15139, 15140, 15141, 15142, 15143, 15144, 15145, 15146, 15147, 15148, 15149, 15150, 15151,
    15152, 15153, 15154, 15155, 15156, 15157, 15158, 30474, 30758,
};
static constexpr int k_festiveDefCount = (int)(sizeof(k_festiveDefs)/sizeof(k_festiveDefs[0]));
inline bool Skin_DefHasFestive(int def)
{
    for (int i = 0; i < k_festiveDefCount; ++i) if (k_festiveDefs[i] == def) return true;
    return false;
}

// -- Reskin table -- alternate model variants per weapon (not Festive/Australium/Botkiller) --
// Keyed by weaponRedir (same key used in g_skinCfgs). Each entry lists one or more reskins.
// SkinCfg::reskinDef stores the chosen def index (0 = stock).
// All-class melee footnotes (from TF2 wiki reskin template):
//   Group A (no Engi/Spy): Frying Pan, Conscientious Objector, Freedom Staff,
//                           Bat Outta Hell, Ham Shank, Crossing Guard
//   Group B (no Spy, adds Engi): Necro Smasher -- EXCLUDED: item_class=saxxy causes entity mismatch
//   Group C (all 9 classes):     Saxxy, Golden Frying Pan, Memory Maker, Prinny Machete
struct ReskinVariant { const char* name; int defindex; int style = 0; };
struct ReskinGroup   { int weaponRedir; const ReskinVariant* variants; int count; };

// Weapons that only have class-specific reskins:
static const ReskinVariant k_rv_flamethrower[] = { {"Rainblower",              741}, {"Nostromo Napalmer", 30474} };
static const ReskinVariant k_rv_pistol[]       = { {"Lugermorph",              160}, {"C.A.P.P.E.R",  30666} };
static const ReskinVariant k_rv_revolver[]     = { {"Big Kill",                161} };
static const ReskinVariant k_rv_wrangler[]     = { {"Giger Counter",         30668} };
static const ReskinVariant k_rv_madmilk[]      = { {"Mutated Milk",           1121} };
static const ReskinVariant k_rv_sandvich[]     = { {"Robo-Sandvich",           863} };
static const ReskinVariant k_rv_dalokohs[]     = { {"Fishcake",                433} };
static const ReskinVariant k_rv_mackerel[]     = { {"Unarmed Combat",          572} };
static const ReskinVariant k_rv_homewrecker[]  = { {"Maul",                    466} };
static const ReskinVariant k_rv_jarate[]       = { {"Self-Aware Beauty Mark", 1105} };
static const ReskinVariant k_rv_machina[]      = { {"Shooting Star",         30665} };
static const ReskinVariant k_rv_invis[]        = { {"Enthusiast's Timepiece",  297}, {"Quackenbirdt",  947} };
static const ReskinVariant k_rv_sapper[]       = { {"Ap-Sap",                  933}, {"Snack Attack", 1102} };
static const ReskinVariant k_rv_boots[]        = { {"Bootlegger",              608} };
static const ReskinVariant k_rv_yer[]          = { {"Wanga Prick",             574} };
static const ReskinVariant k_rv_basher[]       = { {"Three-Rune Blade",        452} };

// Melee reskin groups include class-specific reskins + applicable all-class melees.
// Group A: no Engi/Spy.  Group B: no Spy (adds Wrench).  Group C: all classes.
// Bat (190) -- Batsaber, Holy Mackerel + Group A + C
static const ReskinVariant k_rv_bat[] = {
    {"Batsaber",                30667}, {"Holy Mackerel",            221},
    {"Frying Pan",                264}, {"Conscientious Objector",   474},
    {"Freedom Staff",             880}, {"Bat Outta Hell",           939},
    {"Ham Shank",                1013}, {"Crossing Guard",          1127},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Fire Axe (192) -- Lollichop + Group A + C
static const ReskinVariant k_rv_fireaxe[] = {
    {"Lollichop",                 739},
    {"Frying Pan",                264}, {"Conscientious Objector",   474},
    {"Freedom Staff",             880}, {"Bat Outta Hell",           939},
    {"Ham Shank",                1013}, {"Crossing Guard",          1127},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Bottle (191) -- Scottish Handshake + Group A + C
static const ReskinVariant k_rv_bottle[] = {
    {"Scottish Handshake",        609},
    {"Frying Pan",                264}, {"Conscientious Objector",   474},
    {"Freedom Staff",             880}, {"Bat Outta Hell",           939},
    {"Ham Shank",                1013}, {"Crossing Guard",          1127},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Shovel (196) -- Group A + C (no class-specific reskin)
static const ReskinVariant k_rv_shovel[] = {
    {"Frying Pan",                264}, {"Conscientious Objector",   474},
    {"Freedom Staff",             880}, {"Bat Outta Hell",           939},
    {"Ham Shank",                1013}, {"Crossing Guard",          1127},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Fists (195) -- Bread Bite + Group A + C
static const ReskinVariant k_rv_fists[] = {
    {"Bread Bite",               1100},
    {"Frying Pan",                264}, {"Conscientious Objector",   474},
    {"Freedom Staff",             880}, {"Bat Outta Hell",           939},
    {"Ham Shank",                1013}, {"Crossing Guard",          1127},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Bonesaw (198) -- Group A + C
static const ReskinVariant k_rv_bonesaw[] = {
    {"Frying Pan",                264}, {"Conscientious Objector",   474},
    {"Freedom Staff",             880}, {"Bat Outta Hell",           939},
    {"Ham Shank",                1013}, {"Crossing Guard",          1127},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Kukri (193) -- Group A + C
static const ReskinVariant k_rv_kukri[] = {
    {"Frying Pan",                264}, {"Conscientious Objector",   474},
    {"Freedom Staff",             880}, {"Bat Outta Hell",           939},
    {"Ham Shank",                1013}, {"Crossing Guard",          1127},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Wrench (197) -- Golden Wrench + C (Necro Smasher excluded: item_class=saxxy mismatch)
static const ReskinVariant k_rv_wrench[] = {
    {"Golden Wrench",             169},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Knife (194) -- Black Rose, Sharp Dresser + Group C only (Saxxy/GFP/etc = all classes)
static const ReskinVariant k_rv_knife[] = {
    {"Black Rose",                727}, {"Black Rose (Black)",        727, 1}, {"Sharp Dresser",            638},
    {"Saxxy",                     423}, {"Golden Frying Pan",       1071},
    {"Memory Maker",              954}, {"Prinny Machete",         30758},
};
// Eyelander (132) -- Nessie's Nine Iron, Horseless Headless Horsemann's Headtaker
static const ReskinVariant k_rv_eyelander[] = { {"Nessie's Nine Iron",      482}, {"Horseless Headless Horsemann's Headtaker", 266} };
// Axtinguisher (38) -- Postal Pummeler
static const ReskinVariant k_rv_axtinguisher[] = { {"Postal Pummeler",       457} };
// Minigun (202) -- Iron Curtain
static const ReskinVariant k_rv_minigun[] = { {"Iron Curtain",              298} };
// Sniper Rifle (201) -- AWPer Hand
static const ReskinVariant k_rv_sniperrifle[] = { {"AWPer Hand",             851} };
// Huntsman (56) -- Fortified Compound
static const ReskinVariant k_rv_huntsman[] = { {"Fortified Compound",       1092} };

#define RG(redir, arr) { redir, arr, (int)(sizeof(arr)/sizeof(arr[0])) }
static const ReskinGroup k_reskinGroups[] = {
    // Melee weapons (class-specific + all-class)
    RG(190,  k_rv_bat),          // Bat
    RG(191,  k_rv_bottle),       // Bottle
    RG(192,  k_rv_fireaxe),      // Fire Axe
    RG(193,  k_rv_kukri),        // Kukri
    RG(194,  k_rv_knife),        // Knife
    RG(195,  k_rv_fists),        // Fists
    RG(196,  k_rv_shovel),       // Shovel
    RG(197,  k_rv_wrench),       // Wrench
    RG(198,  k_rv_bonesaw),      // Bonesaw
    RG( 38,  k_rv_axtinguisher), // Axtinguisher
    RG(132,  k_rv_eyelander),    // Eyelander
    RG(225,  k_rv_yer),          // Your Eternal Reward
    RG(325,  k_rv_basher),       // Boston Basher
    RG(202,  k_rv_minigun),      // Minigun
    RG(201,  k_rv_sniperrifle),  // Sniper Rifle
    RG( 56,  k_rv_huntsman),     // Huntsman
    // Ranged weapons
    RG(208,  k_rv_flamethrower), // Flamethrower
    RG(209,  k_rv_pistol),       // Pistol
    RG(210,  k_rv_revolver),     // Revolver
    RG(140,  k_rv_wrangler),     // Wrangler
    RG(526,  k_rv_machina),      // Machina
    // Utility / consumables
    RG( 42,  k_rv_sandvich),     // Sandvich
    RG(159,  k_rv_dalokohs),     // Dalokohs Bar
    RG(222,  k_rv_madmilk),      // Mad Milk
    RG(221,  k_rv_mackerel),     // Holy Mackerel
    RG(153,  k_rv_homewrecker),  // Homewrecker
    RG( 58,  k_rv_jarate),       // Jarate
    // Spy gadgets
    RG(212,  k_rv_invis),        // Invis Watch (redir=212, Upgradeable TF_WEAPON_INVIS)
    RG(736,  k_rv_sapper),       // Sapper (redir=736, Upgradeable TF_WEAPON_BUILDER_SPY)
    // Demo boots
    RG(405,  k_rv_boots),        // Ali Baba's Wee Booties
};
#undef RG
static constexpr int k_reskinGroupCount = (int)(sizeof(k_reskinGroups)/sizeof(k_reskinGroups[0]));

// Returns pointer to ReskinGroup for a given weaponRedir, or nullptr if none.
inline const ReskinGroup* Skin_FindReskinGroup(int weaponRedir)
{
    for (int i = 0; i < k_reskinGroupCount; i++)
        if (k_reskinGroups[i].weaponRedir == weaponRedir) return &k_reskinGroups[i];
    return nullptr;
}

// -- Warpaint name/ID table (paintkit_proto_def_index values) ----------------->"->
struct WarpaintEntry { const char* name; int defindex; };
static const WarpaintEntry k_warpaints[] = {
    { "(none)", 0 },
    { "Airwolf", 82 },
    { "Alien Tech", 232 },
    { "Alpine", 252 },
    { "American Pastoral", 28 },
    { "Anodized Aloha", 305 },
    { "Antique Annihilator", 26 },
    { "Aqua Marine", 57 },
    { "Autumn", 60 },
    { "Autumn Mk.II", 160 },
    { "Backcountry Blaster", 29 },
    { "Backwoods Boomstick", 12 },
    { "Backwoods Boomstick Mk.II", 112 },
    { "Balloonicorn", 66 },
    { "Bamboo Brushed", 306 },
    { "Bank Rolled", 202 },
    { "Barn Burner", 17 },
    { "Black Dahlia", 46 },
    { "Blackout", 431 },
    { "Blasted Bombardier", 24 },
    { "Blitzkrieg", 81 },
    { "Bloom Buffed", 200 },
    { "Blue Mew", 69 },
    { "Bogtrotter", 33 },
    { "Bomb Carrier", 414 },
    { "Bomber Soul", 217 },
    { "Boneyard", 64 },
    { "Bonk Varnished", 207 },
    { "Bonzo Gnawed", 405 },
    { "Bovine Blazemaker", 30 },
    { "Bovine Blazemaker Mk.II", 130 },
    { "Brain Candy", 70 },
    { "Brawler's Iron", 429 },
    { "Brick House", 55 },
    { "Broken Bones", 291 },
    { "Broken Record", 432 },
    { "Business Class", 415 },
    { "Butcher Bird", 84 },
    { "Cabin Fevered", 220 },
    { "Calavera Canvas", 244 },
    { "Candy Coated", 258 },
    { "Cardboard Boxed", 211 },
    { "Carpet Bomber", 4 },
    { "Carpet Bomber Mk.II", 104 },
    { "Chilly Autumn", 410 },
    { "Citizen Pain", 40 },
    { "Civic Duty", 44 },
    { "Civic Duty Mk.II", 144 },
    { "Civil Servant", 39 },
    { "Civil Servant Mk.II", 139 },
    { "Clover Camo'd", 209 },
    { "Coffin Nail", 76 },
    { "Cookie Fortress", 283 },
    { "Corsair", 83 },
    { "Cosmic Calamity", 225 },
    { "Country Crusher", 21 },
    { "Crawlspace Critters", 261 },
    { "Cream Corned", 401 },
    { "Croc Dusted", 308 },
    { "Current Event", 53 },
    { "Damascus and Mahogany", 234 },
    { "Dead Reckoner", 51 },
    { "Dead Reckoner Mk.II", 151 },
    { "Deadly Dragon", 416 },
    { "Death Deluxe", 263 },
    { "Dovetailed", 224 },
    { "Dragon Slayer", 390 },
    { "Dream Piped", 212 },
    { "Dressed to Kill", 78 },
    { "Earth, Sky and Fire", 34 },
    { "Electroshocked", 241 },
    { "Elfin Enamel", 272 },
    { "Eyestalker", 265 },
    { "Fire Glazed", 205 },
    { "Flash Fryer", 49 },
    { "Flower Power", 72 },
    { "Forest Fire", 9 },
    { "Forest Fire Mk.II", 109 },
    { "Freedom Wrapped", 210 },
    { "Frost Ornamented", 246 },
    { "Frosty Delivery", 281 },
    { "Frozen Aurora", 279 },
    { "Geometrical Teams", 215 },
    { "Ghost Town", 242 },
    { "Ghoul Blaster", 400 },
    { "Gift Wrapped", 253 },
    { "Gifting Mann's Wrapping Paper", 276 },
    { "Gingerbread Winner", 270 },
    { "Glacial Glazed", 282 },
    { "Gobi Glazed", 423 },
    { "Gourdy Green", 267 },
    { "Graphite Gripped", 425 },
    { "Hana", 223 },
    { "Haunted Ghosts", 236 },
    { "Hazard Warning", 226 },
    { "Health and Hell", 406 },
    { "Health and Hell (Green)", 407 },
    { "Helldriver", 255 },
    { "Hickory Hole-Puncher", 35 },
    { "High Roller's", 79 },
    { "Homemade Heater", 18 },
    { "Horror Holiday", 239 },
    { "Hypergon", 408 },
    { "Igloo", 248 },
    { "Iron Wood", 20 },
    { "Iron Wood Mk.II", 120 },
    { "Jazzy", 230 },
    { "Kill Covered", 204 },
    { "Killer Bee", 85 },
    { "Kiln and Conquer", 296 },
    { "King of the Jungle", 10 },
    { "Leopard Printed", 310 },
    { "Lightning Rod", 47 },
    { "Liquid Asset", 45 },
    { "Local Hero", 41 },
    { "Low Profile", 58 },
    { "Lumber From Down Under", 19 },
    { "Macabre Web", 63 },
    { "Macabre Web Mk.II", 163 },
    { "Macaw Masked", 303 },
    { "Mannana Peeled", 302 },
    { "Masked Mender", 5 },
    { "Masked Mender Mk.II", 105 },
    { "Mayor", 42 },
    { "Mechanized Monster", 420 },
    { "Merc Stained", 203 },
    { "Metalized Soul", 404 },
    { "Miami Element", 213 },
    { "Misfortunate", 287 },
    { "Mister Cuddles", 73 },
    { "Mosaic", 228 },
    { "Mummified Mimic", 268 },
    { "Necromanced", 297 },
    { "Necropolish", 430 },
    { "Neo Tokyo", 214 },
    { "Neon-Ween", 289 },
    { "Night Owl", 14 },
    { "Night Owl Mk.II", 114 },
    { "Night Terror", 11 },
    { "Nutcracker", 61 },
    { "Nutcracker Mk.II", 161 },
    { "Old Country", 27 },
    { "Organ-ically Hellraised", 256 },
    { "Pacific Peacemaker", 419 },
    { "Park Pigmented", 301 },
    { "Party Phantoms", 294 },
    { "Peppermint Swirl", 273 },
    { "Pink Elephant", 48 },
    { "Piranha Mania", 427 },
    { "Pizza Polished", 206 },
    { "Pina Polished", 309 },
    { "Plaid Potshotter", 22 },
    { "Plaid Potshotter Mk.II", 122 },
    { "Polar Surprise", 221 },
    { "Polter-Guised", 295 },
    { "Portal Plastered", 262 },
    { "Potent Poison", 292 },
    { "Psychedelic Slugger", 3 },
    { "Pumpkin Patch", 62 },
    { "Pumpkin Pied", 259 },
    { "Pumpkin Plastered", 409 },
    { "Purple Range", 7 },
    { "Quack Canvassed", 201 },
    { "Rainbow", 67 },
    { "Raving Dead", 264 },
    { "Reclaimed Reanimator", 25 },
    { "Red Bear", 86 },
    { "Red Rock Roscoe", 0 },
    { "Rooftop Wrangler", 38 },
    { "Rustic Ruiner", 16 },
    { "Saccharine Striped", 271 },
    { "Sacred Slayer", 403 },
    { "Sand Cannon", 1 },
    { "Sandstone Special", 56 },
    { "Sarsparilla Sprayed", 284 },
    { "Sax Waxed", 304 },
    { "Searing Souls", 293 },
    { "Secretly Serviced", 412 },
    { "Seriously Snowed", 249 },
    { "Shell Shocker", 52 },
    { "Shot in the Dark", 23 },
    { "Shot to Hell", 74 },
    { "Simple Spirits", 290 },
    { "Skull Cracked", 286 },
    { "Skull Study", 235 },
    { "Sky Stallion", 413 },
    { "Sleek Greek", 424 },
    { "Sleighin' Style", 251 },
    { "Smalltown Bringdown", 43 },
    { "Smalltown Bringdown Mk.II", 143 },
    { "Smissmas Camo", 250 },
    { "Smissmas Spycrabs", 278 },
    { "Smissmas Sweater", 391 },
    { "Smissmas Village", 247 },
    { "Snow Covered", 245 },
    { "Snow Globalization", 275 },
    { "Snowflake Swirled", 277 },
    { "Spark of Life", 50 },
    { "Spectral Shimmered", 237 },
    { "Spectrum Splattered", 257 },
    { "Spider Season", 269 },
    { "Spider's Cluster", 266 },
    { "Spirit of Halloween", 238 },
    { "Spruce Deuce", 36 },
    { "Stabbed to Hell", 71 },
    { "Star Crossed", 208 },
    { "Stardust", 421 },
    { "Starlight Serenity", 280 },
    { "Stealth Specialist", 426 },
    { "Steel Brushed", 411 },
    { "Sudden Flurry", 8 },
    { "Sunriser", 402 },
    { "Swashbuckled", 285 },
    { "Sweet Dreams", 68 },
    { "Sweet Toothed", 260 },
    { "Tartan Torpedo", 15 },
    { "Team Charged", 428 },
    { "Team Detail", 422 },
    { "Team Serviced", 417 },
    { "Team Sprayer", 37 },
    { "Thunderbolt", 59 },
    { "Tiger Buffed", 307 },
    { "Top Shelf", 77 },
    { "Torqued to Hell", 75 },
    { "Totally Boned", 240 },
    { "Treadplate Tormenter", 32 },
    { "Tumor Toasted", 243 },
    { "Turbine Torcher", 54 },
    { "Uranium", 218 },
    { "War Room", 31 },
    { "Warborn", 418 },
    { "Warhawk", 80 },
    { "Wildwood", 65 },
    { "Winterland Wrapped", 254 },
    { "Woodland Warrior", 6 },
    { "Woodland Warrior Mk.II", 106 },
    { "Woodsy Widowmaker", 13 },
    { "Woodsy Widowmaker Mk.II", 113 },
    { "Wrapped Reviver", 2 },
    { "Wrapped Reviver Mk.II", 102 },
    { "Yeti Coated", 300 },
};
static constexpr int k_warpaintCount = (int)(sizeof(k_warpaints)/sizeof(k_warpaints[0]));

// Weapon-specific skin lookup: (weapon redir def, paintkit ID 0-86) -> specific 15xxx item def.
// Sourced from items_game.txt -- each entry is one decorated weapon variant from a case/collection.
struct SkinDefEntry { int16_t weaponRedir; int8_t pkId; int skinDef; };
static const SkinDefEntry k_skinDefs[] = {
    // Knife (redir 194)
    { 194, 64, 15080 }, { 194, 69, 15094 }, { 194, 70, 15095 }, { 194, 71, 15096 },
    { 194, 77, 15119 }, { 194, 78, 15118 }, { 194, 81, 15143 }, { 194, 82, 15144 },
    // Wrench (redir 197)
    { 197, 60, 15074 }, { 197, 61, 15073 }, { 197, 64, 15075 }, { 197, 75, 15114 },
    { 197, 77, 15140 }, { 197, 78, 15139 }, { 197, 82, 15156 },
    // Shotgun (redir 199)
    { 199, 12, 15003 }, { 199, 16, 15016 }, { 199, 44, 15044 }, { 199, 47, 15047 },
    { 199, 60, 15085 }, { 199, 72, 15109 }, { 199, 76, 15132 }, { 199, 78, 15133 },
    { 199, 86, 15152 },
    // Scattergun (redir 200)
    { 200, 11, 15002 }, { 200, 15, 15015 }, { 200, 21, 15021 }, { 200, 29, 15029 },
    { 200, 36, 15036 }, { 200, 53, 15053 }, { 200, 61, 15069 }, { 200, 63, 15065 },
    { 200, 69, 15106 }, { 200, 72, 15107 }, { 200, 74, 15108 }, { 200, 76, 15131 },
    { 200, 83, 15157 }, { 200, 85, 15151 },
    // Sniper Rifle (redir 201)
    { 201,  7, 15007 }, { 201, 14, 15000 }, { 201, 19, 15019 }, { 201, 23, 15023 },
    { 201, 33, 15033 }, { 201, 59, 15059 }, { 201, 62, 15070 }, { 201, 64, 15071 },
    { 201, 65, 15072 }, { 201, 66, 15111 }, { 201, 67, 15112 }, { 201, 76, 15135 },
    { 201, 78, 15136 }, { 201, 82, 15154 },
    // Minigun (redir 202)
    { 202, 10, 15004 }, { 202, 20, 15020 }, { 202, 26, 15026 }, { 202, 31, 15031 },
    { 202, 40, 15040 }, { 202, 55, 15055 }, { 202, 61, 15088 }, { 202, 62, 15087 },
    { 202, 63, 15086 }, { 202, 70, 15098 }, { 202, 73, 15099 }, { 202, 76, 15123 },
    { 202, 77, 15125 }, { 202, 78, 15124 }, { 202, 84, 15147 },
    // SMG (redir 203)
    { 203, 13, 15001 }, { 203, 22, 15022 }, { 203, 32, 15032 }, { 203, 37, 15037 },
    { 203, 58, 15058 }, { 203, 65, 15076 }, { 203, 69, 15110 }, { 203, 79, 15134 },
    { 203, 81, 15153 },
    // Rocket Launcher (redir 205)
    { 205,  1, 15014 }, { 205,  6, 15006 }, { 205, 28, 15028 }, { 205, 43, 15043 },
    { 205, 52, 15052 }, { 205, 57, 15057 }, { 205, 60, 15081 }, { 205, 69, 15104 },
    { 205, 70, 15105 }, { 205, 76, 15129 }, { 205, 79, 15130 }, { 205, 80, 15150 },
    // Grenade Launcher (redir 206)
    { 206, 60, 15077 }, { 206, 63, 15079 }, { 206, 67, 15091 }, { 206, 68, 15092 },
    { 206, 76, 15116 }, { 206, 77, 15117 }, { 206, 80, 15142 }, { 206, 84, 15158 },
    // Stickybomb Launcher (redir 207)
    { 207,  4, 15012 }, { 207,  8, 15009 }, { 207, 24, 15024 }, { 207, 38, 15038 },
    { 207, 45, 15045 }, { 207, 48, 15048 }, { 207, 60, 15082 }, { 207, 62, 15083 },
    { 207, 63, 15084 }, { 207, 68, 15113 }, { 207, 76, 15137 }, { 207, 78, 15138 },
    { 207, 81, 15155 },
    // Flamethrower (redir 208)
    { 208,  9, 15005 }, { 208, 17, 15017 }, { 208, 30, 15030 }, { 208, 34, 15034 },
    { 208, 49, 15049 }, { 208, 54, 15054 }, { 208, 60, 15066 }, { 208, 61, 15068 },
    { 208, 62, 15067 }, { 208, 66, 15089 }, { 208, 67, 15090 }, { 208, 76, 15115 },
    { 208, 80, 15141 },
    // Pistol (redir 209)
    { 209,  0, 15013 }, { 209, 18, 15018 }, { 209, 35, 15035 }, { 209, 41, 15041 },
    { 209, 46, 15046 }, { 209, 56, 15056 }, { 209, 61, 15061 }, { 209, 63, 15060 },
    { 209, 69, 15100 }, { 209, 70, 15101 }, { 209, 74, 15102 }, { 209, 78, 15126 },
    { 209, 81, 15148 },
    // Revolver (redir 210)
    { 210,  3, 15011 }, { 210, 27, 15027 }, { 210, 42, 15042 }, { 210, 51, 15051 },
    { 210, 63, 15064 }, { 210, 64, 15062 }, { 210, 65, 15063 }, { 210, 72, 15103 },
    { 210, 76, 15127 }, { 210, 77, 15128 }, { 210, 81, 15149 },
    // Medi Gun (redir 211)
    { 211,  2, 15010 }, { 211,  5, 15008 }, { 211, 25, 15025 }, { 211, 39, 15039 },
    { 211, 50, 15050 }, { 211, 65, 15078 }, { 211, 72, 15097 }, { 211, 76, 15120 },
    { 211, 78, 15121 }, { 211, 79, 15122 }, { 211, 81, 15145 }, { 211, 83, 15146 },
};
static constexpr int k_skinDefCount = (int)(sizeof(k_skinDefs)/sizeof(k_skinDefs[0]));

// ---- Reskin runtime info, generated from items_game.txt ------------------------------
// model  : "model_player" (walked through the item's prefab chain). fpsoverlay typed
//          these by hand and got 15 of them wrong -- a path the engine cannot load
//          silently leaves the stock model on screen.
// anim   : "anim_slot". A reskin whose slot differs from the weapon it replaces needs
//          its sequences remapped (ACT_MELEE_VM_* -> ACT_MELEE_ALLCLASS_VM_*, ...).
// skins  : "visuals/styles/<n>/skin_red|skin_blu" -- what makes a Gold Frying Pan gold.
// gold   : the item carries set_turn_to_gold (attr 150): Saxxy, Golden Wrench, GFP.
// logname: "item_logname", which is what the killfeed reads to pick its icon -- but only
//          when tf/scripts/mod_textures.txt has an entry for it. nullptr = it has none
//          (skullbat, prinny_machete, awper_hand, giger_counter, shooting_star), and
//          the event must be left alone: "d_<name>" with no entry draws the SKULL.
// pfx    : the activity family the item's anim_slot names, as a prefix.
// repl   : "visuals/animation_replacement" -- the item's OWN activity overrides, which
//          TF2 consults BEFORE the anim_slot family table (tf_weaponbase.cpp:4419), so
//          this, not anim_slot, is what actually decides which sequence plays. 27 of the
//          44 reskins carry one; the Snack Attack is nothing BUT one (its slot is
//          "building", same as the stock sapper -- ACT_VM_DRAW -> ACT_BREADSAPPER_VM_DRAW
//          is the entire difference).
enum AnimSlot : uint8_t { AS_DEFAULT = 0, AS_MELEE_ALLCLASS, AS_ITEM1, AS_ITEM2, AS_NONE };

// One entry of an animation_replacement block: the activity the weapon code asks for,
// and the activity this item plays instead.
struct ActPair { const char* from; const char* to; };

static const ActPair k_actRepl_r264[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r474[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r880[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r939[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1013[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1127[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r423[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1071[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r954[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r30758[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALLCLASS_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALLCLASS_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALLCLASS_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1100[] = {
    { "ACT_VM_HITLEFT",                          "ACT_BREADGLOVES_VM_HITLEFT" },
    { "ACT_VM_HITRIGHT",                         "ACT_BREADGLOVES_VM_HITRIGHT" },
    { "ACT_VM_SWINGHARD",                        "ACT_BREADGLOVES_VM_SWINGHARD" },
    { "ACT_VM_IDLE",                             "ACT_BREADGLOVES_VM_IDLE" },
    { "ACT_VM_DRAW",                             "ACT_BREADGLOVES_VM_DRAW" },
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_MELEE_ALT2_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_MELEE_ALT2_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_MELEE_ALT2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r727[] = {
    { "ACT_VM_HITCENTER",                        "ACT_MELEE_VM_HITCENTER" },
    { "ACT_VM_SWINGHARD",                        "ACT_MELEE_VM_SWINGHARD" },
};
static const ActPair k_actRepl_r638[] = {
    { "ACT_VM_DRAW",                             "ACT_ITEM1_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_ITEM1_VM_IDLE" },
    { "ACT_VM_HITCENTER",                        "ACT_ITEM1_VM_HITCENTER" },
    { "ACT_VM_HITCENTER2",                       "ACT_ITEM1_VM_HITCENTER2" },
    { "ACT_VM_SWINGHARD",                        "ACT_ITEM1_VM_SWINGHARD" },
    { "ACT_BACKSTAB_VM_UP",                      "ACT_ITEM1_BACKSTAB_VM_UP" },
    { "ACT_BACKSTAB_VM_DOWN",                    "ACT_ITEM1_BACKSTAB_VM_DOWN" },
    { "ACT_BACKSTAB_VM_IDLE",                    "ACT_ITEM1_BACKSTAB_VM_IDLE" },
    { "ACT_MELEE_VM_STUN",                       "ACT_MELEE_VM_ITEM1_STUN" },
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r482[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_SPECIAL_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_SPECIAL_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_SPECIAL_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r266[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_SPECIAL_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_SPECIAL_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_SPECIAL_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r851[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_PRIMARY_ALT1_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_PRIMARY_ALT1_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_PRIMARY_ALT1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1092[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_ITEM2_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_ITEM2_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_ITEM2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r741[] = {
    { "ACT_MP_ATTACK_CROUCH_SECONDARYFIRE",      "ACT_MP_ATTACK_CROUCH_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_STAND_SECONDARYFIRE",       "ACT_MP_ATTACK_STAND_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_SWIM_SECONDARYFIRE",        "ACT_MP_ATTACK_SWIM_PRIMARY_ALT" },
};
static const ActPair k_actRepl_r30474[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_PRIMARY_ALT1_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_PRIMARY_ALT1_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_PRIMARY_ALT1_VM_INSPECT_END" },
    { "ACT_MP_ATTACK_CROUCH_SECONDARYFIRE",      "ACT_MP_ATTACK_CROUCH_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_STAND_SECONDARYFIRE",       "ACT_MP_ATTACK_STAND_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_SWIM_SECONDARYFIRE",        "ACT_MP_ATTACK_SWIM_PRIMARY_ALT" },
};
static const ActPair k_actRepl_r30668[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r30665[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_PRIMARY_ALT1_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_PRIMARY_ALT1_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_PRIMARY_ALT1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r863[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r433[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1121[] = {
    { "ACT_MP_ATTACK_STAND_MELEE_ALLCLASS",      "ACT_MP_THROW" },
    { "ACT_VM_DRAW",                             "ACT_BREADMONSTER_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_BREADMONSTER_VM_IDLE" },
    { "ACT_VM_PRIMARYATTACK",                    "ACT_BREADMONSTER_VM_PRIMARYATTACK" },
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_BREADMONSTER_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_BREADMONSTER_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_BREADMONSTER_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1105[] = {
    { "ACT_MP_ATTACK_STAND_MELEE_ALLCLASS",      "ACT_MP_THROW" },
    { "ACT_VM_DRAW",                             "ACT_BREADMONSTER_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_BREADMONSTER_VM_IDLE" },
    { "ACT_VM_PRIMARYATTACK",                    "ACT_BREADMONSTER_VM_PRIMARYATTACK" },
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_BREADMONSTER_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_BREADMONSTER_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_BREADMONSTER_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r1102[] = {
    { "ACT_VM_DRAW",                             "ACT_BREADSAPPER_VM_DRAW" },
    { "ACT_VM_DRAW_DEPLOYED",                    "ACT_BREADSAPPER_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_BREADSAPPER_VM_IDLE" },
    { "ACT_BUILDING_VM_INSPECT_START",           "ACT_BREADMONSTER_VM_INSPECT_START" },
    { "ACT_BUILDING_VM_INSPECT_IDLE",            "ACT_BREADMONSTER_VM_INSPECT_IDLE" },
    { "ACT_BUILDING_VM_INSPECT_END",             "ACT_BREADMONSTER_VM_INSPECT_END" },
};
static const ActPair k_actRepl_r574[] = {
    { "ACT_VM_DRAW",                             "ACT_ITEM2_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_ITEM2_VM_IDLE" },
    { "ACT_VM_HITCENTER",                        "ACT_ITEM2_VM_HITCENTER" },
    { "ACT_VM_SWINGHARD",                        "ACT_ITEM2_VM_SWINGHARD" },
    { "ACT_BACKSTAB_VM_UP",                      "ACT_ITEM2_BACKSTAB_VM_UP" },
    { "ACT_BACKSTAB_VM_DOWN",                    "ACT_ITEM2_BACKSTAB_VM_DOWN" },
    { "ACT_BACKSTAB_VM_IDLE",                    "ACT_ITEM2_BACKSTAB_VM_IDLE" },
    { "ACT_MELEE_VM_STUN",                       "ACT_MELEE_VM_ITEM2_STUN" },
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_ITEM2_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_ITEM2_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_ITEM2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b194[] = {
    { "ACT_VM_HITCENTER",                        "ACT_MELEE_VM_HITCENTER" },
    { "ACT_VM_SWINGHARD",                        "ACT_MELEE_VM_SWINGHARD" },
};
static const ActPair k_actRepl_b195[] = {
    { "ACT_VM_IDLE",                             "ACT_FISTS_VM_IDLE" },
    { "ACT_VM_HITLEFT",                          "ACT_FISTS_VM_HITLEFT" },
    { "ACT_VM_HITRIGHT",                         "ACT_FISTS_VM_HITRIGHT" },
    { "ACT_VM_SWINGHARD",                        "ACT_FISTS_VM_SWINGHARD" },
    { "ACT_VM_DRAW",                             "ACT_FISTS_VM_DRAW" },
};
static const ActPair k_actRepl_b132[] = {
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_SPECIAL_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_SPECIAL_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_SPECIAL_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b225[] = {
    { "ACT_VM_DRAW",                             "ACT_ITEM2_VM_DRAW" },
    { "ACT_VM_IDLE",                             "ACT_ITEM2_VM_IDLE" },
    { "ACT_VM_HITCENTER",                        "ACT_ITEM2_VM_HITCENTER" },
    { "ACT_VM_SWINGHARD",                        "ACT_ITEM2_VM_SWINGHARD" },
    { "ACT_BACKSTAB_VM_UP",                      "ACT_ITEM2_BACKSTAB_VM_UP" },
    { "ACT_BACKSTAB_VM_DOWN",                    "ACT_ITEM2_BACKSTAB_VM_DOWN" },
    { "ACT_BACKSTAB_VM_IDLE",                    "ACT_ITEM2_BACKSTAB_VM_IDLE" },
    { "ACT_MELEE_VM_STUN",                       "ACT_MELEE_VM_ITEM2_STUN" },
    { "ACT_MELEE_VM_INSPECT_START",              "ACT_ITEM2_VM_INSPECT_START" },
    { "ACT_MELEE_VM_INSPECT_IDLE",               "ACT_ITEM2_VM_INSPECT_IDLE" },
    { "ACT_MELEE_VM_INSPECT_END",                "ACT_ITEM2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b56[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_ITEM2_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_ITEM2_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_ITEM2_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b208[] = {
    { "ACT_MP_ATTACK_CROUCH_SECONDARYFIRE",      "ACT_MP_ATTACK_CROUCH_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_STAND_SECONDARYFIRE",       "ACT_MP_ATTACK_STAND_PRIMARY_ALT" },
    { "ACT_MP_ATTACK_SWIM_SECONDARYFIRE",        "ACT_MP_ATTACK_SWIM_PRIMARY_ALT" },
};
static const ActPair k_actRepl_b140[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b526[] = {
    { "ACT_PRIMARY_VM_INSPECT_START",            "ACT_PRIMARY_ALT1_VM_INSPECT_START" },
    { "ACT_PRIMARY_VM_INSPECT_IDLE",             "ACT_PRIMARY_ALT1_VM_INSPECT_IDLE" },
    { "ACT_PRIMARY_VM_INSPECT_END",              "ACT_PRIMARY_ALT1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b42[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b159[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b222[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};
static const ActPair k_actRepl_b58[] = {
    { "ACT_SECONDARY_VM_INSPECT_START",          "ACT_ITEM1_VM_INSPECT_START" },
    { "ACT_SECONDARY_VM_INSPECT_IDLE",           "ACT_ITEM1_VM_INSPECT_IDLE" },
    { "ACT_SECONDARY_VM_INSPECT_END",            "ACT_ITEM1_VM_INSPECT_END" },
};

static constexpr int k_maxReskinStyles = 4;
struct ReskinInfo {
    int      defindex;
    const char* model;          // nullptr = the item has no c_model of its own
    const char* logname;        // "item_logname" -- the killfeed icon name
    AnimSlot anim;
    uint8_t  styleCount;        // 0 = no styles block, skins come from the team default
    int8_t   skinRed[k_maxReskinStyles];
    int8_t   skinBlu[k_maxReskinStyles];
    bool     turnToGold;        // attr 150
    bool     specialTaunt;      // attr 551 special_taunt -- the Saxxy/Rainblower taunt kill
    const char* animPfx;        // activity family prefix, null when the slot names none
    const ActPair* actRepl;     // animation_replacement, null when the item has none
    uint8_t  actReplCount;
};

static const ReskinInfo k_reskinInfo[] = {
    {  30667, "models/workshop/weapons/c_models/c_invasion_bat/c_invasion_bat.mdl",          "batsaber",              AS_DEFAULT,        1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // Batsaber
    {    221, "models/workshop/weapons/c_models/c_holymackerel/c_holymackerel.mdl",          "holymackerel",          AS_DEFAULT,        1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Holy Mackerel
    {    264, "models/weapons/c_models/c_frying_pan/c_frying_pan.mdl",                       "fryingpan",             AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r264, 3, }, // Frying Pan
    {    474, "models/workshop/weapons/c_models/c_picket/c_picket.mdl",                      "nonnonviolent_protest", AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r474, 3, }, // The Conscientious Objector
    {    880, "models/workshop_partner/weapons/c_models/c_tw_eagle/c_tw_eagle.mdl",          "freedom_staff",         AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r880, 3, }, // The Freedom Staff
    {    939, "models/workshop/weapons/c_models/c_skullbat/c_skullbat.mdl",                  nullptr,                 AS_MELEE_ALLCLASS, 4, {0,0,0,0}, {1,1,1,1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r939, 3, }, // The Bat Outta Hell
    {   1013, "models/workshop/weapons/c_models/c_ham/c_ham.mdl",                            "ham_shank",             AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1013, 3, }, // The Ham Shank
    {   1127, "models/workshop/weapons/c_models/c_crossing_guard/c_crossing_guard.mdl",      "crossing_guard",        AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1127, 3, }, // The Crossing Guard
    {    423, "models/weapons/c_models/c_saxxy/c_saxxy.mdl",                                 "saxxy",                 AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, true, true,  "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r423, 3, }, // Saxxy
    {   1071, "models/weapons/c_models/c_frying_pan/c_frying_pan.mdl",                       "golden_fryingpan",      AS_MELEE_ALLCLASS, 1, {2,-1,-1,-1}, {3,-1,-1,-1}, true, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1071, 3, }, // Gold Frying Pan
    {    954, "models/weapons/c_models/c_8mm_camera/c_8mm_camera.mdl",                       "memory_maker",          AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r954, 3, }, // Memory Maker
    {  30758, "models/workshop_partner/weapons/c_models/c_prinny_knife/c_prinny_knife.mdl",  nullptr,                 AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r30758, 3, }, // Prinny Machete
    {    739, "models/weapons/c_models/c_lollichop/c_lollichop.mdl",                         "lollichop",             AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Lollichop
    {    609, "models/workshop/weapons/c_models/c_scotland_shard/c_scotland_shard.mdl",      "scotland_shard",        AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Scottish Handshake
    {   1100, "models/weapons/c_models/c_breadmonster_gloves/c_breadmonster_gloves.mdl",     "bread_bite",            AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           k_actRepl_r1100, 8, }, // The Bread Bite
    {    169, "models/weapons/c_models/c_wrench/c_wrench.mdl",                               "wrench_golden",         AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, true, false, "ACT_MELEE_VM_",           nullptr, 0, }, // Golden Wrench
    {    727, "models/workshop/weapons/c_models/c_ava_roseknife/c_ava_roseknife.mdl",        "black_rose",            AS_DEFAULT,        2, {0,2,-1,-1}, {1,3,-1,-1}, false, true,  "ACT_MELEE_VM_",           k_actRepl_r727, 2, }, // The Black Rose
    {    638, "models/workshop/weapons/c_models/c_acr_hookblade/c_acr_hookblade.mdl",        "sharp_dresser",         AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_ITEM1_VM_",           k_actRepl_r638, 12, }, // The Sharp Dresser
    {    482, "models/workshop/weapons/c_models/c_golfclub/c_golfclub.mdl",                  "nessieclub",            AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_ITEM1_VM_",           k_actRepl_r482, 3, }, // Nessie's Nine Iron
    {    266, "models/weapons/c_models/c_headtaker/c_headtaker.mdl",                         "headtaker",             AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_ITEM1_VM_",           k_actRepl_r266, 3, }, // The Horseless Headless Horseman's Headtaker
    {    457, "models/workshop/weapons/c_models/c_mailbox/c_mailbox.mdl",                    "mailbox",               AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Postal Pummeler
    {    298, "models/workshop/weapons/c_models/c_iron_curtain/c_iron_curtain.mdl",          "iron_curtain",          AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_PRIMARY_VM_",         nullptr, 0, }, // Iron Curtain
    {    851, "models/weapons/c_models/c_csgo_awp/c_csgo_awp.mdl",                           nullptr,                 AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_PRIMARY_VM_",         k_actRepl_r851, 3, }, // The AWPer Hand
    {   1092, "models/workshop_partner/weapons/c_models/c_bow_thief/c_bow_thief.mdl",        nullptr,                 AS_ITEM2,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_ITEM2_VM_",           k_actRepl_r1092, 3, }, // The Fortified Compound
    {    741, "models/weapons/c_models/c_rainblower/c_rainblower.mdl",                       "rainblower",            AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, true,  "ACT_PRIMARY_VM_",         k_actRepl_r741, 3, }, // The Rainblower
    {  30474, "models/workshop_partner/weapons/c_models/c_ai_flamethrower/c_ai_flamethrower.mdl", "ai_flamethrower",       AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_PRIMARY_VM_",         k_actRepl_r30474, 6, }, // The Nostromo Napalmer
    {    160, "models/workshop/weapons/c_models/c_ttg_max_gun/c_ttg_max_gun.mdl",            "maxgun",                AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_SECONDARY_VM_",       nullptr, 0, }, // TTG Max Pistol
    {  30666, "models/workshop/weapons/c_models/c_invasion_pistol/c_invasion_pistol.mdl",    "the_capper",            AS_DEFAULT,        1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_SECONDARY_VM_",       nullptr, 0, }, // The C.A.P.P.E.R
    {    161, "models/workshop/weapons/c_models/c_ttg_sam_gun/c_ttg_sam_gun.mdl",            "samrevolver",           AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_SECONDARY_VM_",       nullptr, 0, }, // TTG Sam Revolver
    {  30668, "models/workshop/weapons/c_models/c_invasion_wrangler/c_invasion_wrangler.mdl", nullptr,                 AS_ITEM1,          1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_ITEM1_VM_",           k_actRepl_r30668, 3, }, // The Giger Counter
    {  30665, "models/workshop/weapons/c_models/c_invasion_sniperrifle/c_invasion_sniperrifle.mdl", nullptr,                 AS_DEFAULT,        1, {0,-1,-1,-1}, {1,-1,-1,-1}, false, false, "ACT_PRIMARY_VM_",         k_actRepl_r30665, 3, }, // Shooting Star
    {    863, "models/weapons/c_models/c_sandwich/c_robo_sandwich.mdl",                      nullptr,                 AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, true,  "ACT_ITEM1_VM_",           k_actRepl_r863, 3, }, // The Robo-Sandvich
    {    433, "models/workshop/weapons/c_models/c_fishcake/c_fishcake.mdl",                  nullptr,                 AS_ITEM1,          0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, true,  "ACT_ITEM1_VM_",           k_actRepl_r433, 3, }, // Fishcake
    {   1121, "models/weapons/c_models/c_breadmonster/c_breadmonster_milk.mdl",              nullptr,                 AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1121, 7, }, // Mutated Milk
    {    572, "models/workshop/weapons/c_models/c_unarmed_combat/c_unarmed_combat.mdl",      "unarmed_combat",        AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // Unarmed Combat
    {    466, "models/workshop/weapons/c_models/c_rfa_hammer/c_rfa_hammer.mdl",              "the_maul",              AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // The Maul
    {   1105, "models/weapons/c_models/c_breadmonster/c_breadmonster.mdl",                   nullptr,                 AS_MELEE_ALLCLASS, 0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_ALLCLASS_VM_",  k_actRepl_r1105, 7, }, // The Self-Aware Beauty Mark
    {    297, "models/weapons/v_models/v_ttg_watch_spy.mdl",                                 nullptr,                 AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   nullptr, 0, }, // TTG Watch
    {    947, "models/workshop_partner/weapons/v_models/v_hm_watch/v_hm_watch.mdl",          nullptr,                 AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   nullptr, 0, }, // The Quackenbirdt
    {    933, "models/weapons/c_models/c_p2rec/c_p2rec.mdl",                                 "psapper",               AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   nullptr, 0, }, // The Ap-Sap
    {   1102, "models/weapons/c_models/c_breadmonster_sapper/c_breadmonster_sapper.mdl",     "snack_attack",          AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   k_actRepl_r1102, 6, }, // The Snack Attack
    {    608, "models/workshop/player/items/demo/pegleg/pegleg.mdl",                         nullptr,                 AS_NONE,           0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, nullptr,                   nullptr, 0, }, // The Bootlegger
    {    574, "models/workshop/weapons/c_models/c_voodoo_pin/c_voodoo_pin.mdl",              "voodoo_pin",            AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           k_actRepl_r574, 11, }, // The Wanga Prick
    {    452, "models/workshop/weapons/c_models/c_scout_sword/c_scout_sword.mdl",            "scout_sword",           AS_DEFAULT,        0, {-1,-1,-1,-1}, {-1,-1,-1,-1}, false, false, "ACT_MELEE_VM_",           nullptr, 0, }, // Three-Rune Blade
};
static constexpr int k_reskinInfoCount = (int)(sizeof(k_reskinInfo)/sizeof(k_reskinInfo[0]));

inline const ReskinInfo* Skin_FindReskinInfo(int defindex)
{
    for (int i = 0; i < k_reskinInfoCount; ++i)
        if (k_reskinInfo[i].defindex == defindex) return &k_reskinInfo[i];
    return nullptr;
}

// The weapon a reskin can be applied TO -- the "from" half of a remap.
// model is that weapon's own "model_player" c_model: the sequence table the game picked
// its index out of before we rewrote the def index. Generated rather than read back off
// the live entity, because by the second pre-write pass every model field on that
// weapon is already OURS and reading one back returns the reskin.
struct BaseAnimEntry {
    int weaponRedir; AnimSlot anim; const char* model;
    const char* animPfx;        // the family the BASE plays in -- the "from" side
    const ActPair* actRepl;     // the base's own overrides, applied before the family
    uint8_t actReplCount;
};
static const BaseAnimEntry k_baseAnimSlots[] = {
    {  190, AS_DEFAULT,        "models/weapons/c_models/c_bat.mdl",                                     "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_BAT
    {  191, AS_DEFAULT,        "models/weapons/c_models/c_bottle/c_bottle.mdl",                         "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_BOTTLE
    {  192, AS_DEFAULT,        "models/weapons/c_models/c_fireaxe_pyro/c_fireaxe_pyro.mdl",             "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_FIREAXE
    {  193, AS_DEFAULT,        "models/weapons/c_models/c_machete/c_machete.mdl",                       "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_CLUB
    {  194, AS_DEFAULT,        "models/weapons/c_models/c_knife/c_knife.mdl",                           "ACT_MELEE_VM_",           k_actRepl_b194, 2, }, // Upgradeable TF_WEAPON_KNIFE
    {  195, AS_DEFAULT,        nullptr,                                                                 "ACT_MELEE_VM_",           k_actRepl_b195, 5, }, // Upgradeable TF_WEAPON_FISTS
    {  196, AS_DEFAULT,        "models/weapons/c_models/c_shovel/c_shovel.mdl",                         "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_SHOVEL
    {  197, AS_DEFAULT,        "models/weapons/c_models/c_wrench/c_wrench.mdl",                         "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_WRENCH
    {  198, AS_DEFAULT,        "models/weapons/c_models/c_bonesaw/c_bonesaw.mdl",                       "ACT_MELEE_VM_",           nullptr, 0, }, // Upgradeable TF_WEAPON_BONESAW
    {   38, AS_DEFAULT,        "models/weapons/c_models/c_axtinguisher/c_axtinguisher_pyro.mdl",        "ACT_MELEE_VM_",           nullptr, 0, }, // The Axtinguisher
    {  132, AS_ITEM1,          "models/weapons/c_models/c_claymore/c_claymore.mdl",                     "ACT_ITEM1_VM_",           k_actRepl_b132, 3, }, // The Eyelander
    {  225, AS_DEFAULT,        "models/workshop/weapons/c_models/c_eternal_reward/c_eternal_reward.mdl", "ACT_MELEE_VM_",           k_actRepl_b225, 11, }, // Your Eternal Reward
    {  325, AS_DEFAULT,        "models/workshop/weapons/c_models/c_boston_basher/c_boston_basher.mdl",  "ACT_MELEE_VM_",           nullptr, 0, }, // The Boston Basher
    {  202, AS_DEFAULT,        "models/weapons/c_models/c_minigun/c_minigun.mdl",                       "ACT_PRIMARY_VM_",         nullptr, 0, }, // Upgradeable TF_WEAPON_MINIGUN
    {  201, AS_DEFAULT,        "models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl",               "ACT_PRIMARY_VM_",         nullptr, 0, }, // Upgradeable TF_WEAPON_SNIPERRIFLE
    {   56, AS_ITEM2,          "models/weapons/c_models/c_bow/c_bow.mdl",                               "ACT_ITEM2_VM_",           k_actRepl_b56, 3, }, // The Huntsman
    {  208, AS_DEFAULT,        "models/weapons/c_models/c_flamethrower/c_flamethrower.mdl",             "ACT_PRIMARY_VM_",         k_actRepl_b208, 3, }, // Upgradeable TF_WEAPON_FLAMETHROWER
    {  209, AS_DEFAULT,        "models/weapons/c_models/c_pistol/c_pistol.mdl",                         "ACT_SECONDARY_VM_",       nullptr, 0, }, // Upgradeable TF_WEAPON_PISTOL
    {  210, AS_DEFAULT,        "models/weapons/c_models/c_revolver/c_revolver.mdl",                     "ACT_SECONDARY_VM_",       nullptr, 0, }, // Upgradeable TF_WEAPON_REVOLVER
    {  140, AS_ITEM1,          "models/weapons/c_models/c_wrangler.mdl",                                "ACT_ITEM1_VM_",           k_actRepl_b140, 3, }, // The Wrangler
    {  526, AS_DEFAULT,        "models/workshop_partner/weapons/c_models/c_dex_sniperrifle/c_dex_sniperrifle.mdl", "ACT_PRIMARY_VM_",         k_actRepl_b526, 3, }, // The Machina
    {   42, AS_ITEM1,          "models/weapons/c_models/c_sandwich/c_sandwich.mdl",                     "ACT_ITEM1_VM_",           k_actRepl_b42, 3, }, // The Sandvich
    {  159, AS_ITEM1,          "models/workshop/weapons/c_models/c_chocolate/c_chocolate.mdl",          "ACT_ITEM1_VM_",           k_actRepl_b159, 3, }, // The Dalokohs Bar
    {  222, AS_ITEM1,          "models/workshop/weapons/c_models/c_madmilk/c_madmilk.mdl",              "ACT_ITEM1_VM_",           k_actRepl_b222, 3, }, // Mad Milk
    {  221, AS_DEFAULT,        "models/workshop/weapons/c_models/c_holymackerel/c_holymackerel.mdl",    "ACT_MELEE_VM_",           nullptr, 0, }, // The Holy Mackerel
    {  153, AS_DEFAULT,        "models/workshop/weapons/c_models/c_sledgehammer/c_sledgehammer.mdl",    "ACT_MELEE_VM_",           nullptr, 0, }, // The Homewrecker
    {   58, AS_ITEM1,          "models/weapons/c_models/urinejar.mdl",                                  "ACT_ITEM1_VM_",           k_actRepl_b58, 3, }, // Jarate
    {  212, AS_NONE,           "models/weapons/v_models/v_watch_spy.mdl",                               nullptr,                   nullptr, 0, }, // Upgradeable TF_WEAPON_INVIS
    {  736, AS_DEFAULT,        "models/weapons/c_models/c_sapper/c_sapper.mdl",                         nullptr,                   nullptr, 0, }, // Upgradeable TF_WEAPON_BUILDER_SPY
    {  405, AS_NONE,           "models/workshop/player/items/demo/demo_booties/demo_booties.mdl",       nullptr,                   nullptr, 0, }, // Ali Baba's Wee Booties
};
static constexpr int k_baseAnimCount = (int)(sizeof(k_baseAnimSlots)/sizeof(k_baseAnimSlots[0]));

inline const BaseAnimEntry* Skin_BaseAnimEntry(int weaponRedir)
{
    for (int i = 0; i < k_baseAnimCount; ++i)
        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return &k_baseAnimSlots[i];
    return nullptr;
}

inline AnimSlot Skin_BaseAnimSlot(int weaponRedir)
{
    for (int i = 0; i < k_baseAnimCount; ++i)
        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return k_baseAnimSlots[i].anim;
    return AS_DEFAULT;
}

// The c_model the base weapon plays its viewmodel sequences out of, or null if the
// weapon is not one a reskin group covers.
inline const char* Skin_BaseModel(int weaponRedir)
{
    for (int i = 0; i < k_baseAnimCount; ++i)
        if (k_baseAnimSlots[i].weaponRedir == weaponRedir) return k_baseAnimSlots[i].model;
    return nullptr;
}

