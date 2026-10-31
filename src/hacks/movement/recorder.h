#pragma once
#include "../../globals/globals.h"
#include "movement.h"
#include "recorder_ser.h"
#include <string>
#include <vector>

// Movement recorder: record a run's input (view + move + buttons) and play it back.
// Port of botox hacks/movement/movement_recorder.cpp, trimmed to what a TAS-style route
// replay needs: no jetpack replay (TF2 has no CS:GO jetpack). "Force same weapons" replays
// the recorded m_hMyWeapons slot (confirmed via Amalgam SDK CBaseCombatCharacter.h +
// this codebase's own EHANDLE decode convention, see recorder.cpp ActiveWeaponSlot/
// ForceWeaponSlot).
// Clips live under kRecorderDir, one file per clip, named "<map>__<name>.rec".
static constexpr const char* kRecorderDir = "C:\\botox-tf2\\recorder";

inline bool  g_recEnable        = false;   // master: recorder bind polling runs at all
inline int   g_recRecordKey     = 0, g_recRecordMode = 2;   // toggle by default
inline int   g_recPlayKey       = 0, g_recPlayMode   = 2;
inline bool  g_recLockVa        = false;   // playback drives viewangles too, not just move
inline bool  g_recLockGoingToStart = false; // walk to the recorded start before replaying
inline bool  g_recStopOnMove    = true;    // real WASD/jump during playback cancels it
inline bool  g_recForceWeapon   = false;   // reselect the recorded m_hMyWeapons slot each frame
inline float g_recApproachDist  = 24.f;    // hull radius -- "close enough" to start replaying

// game-thread entry point, one call from Movement_RunImpl (after the pLocal/alive check).
// Returns true if the recorder owns this cmd (playback approach/replay in progress) --
// caller still runs the rest of the pipeline on the (now recorder-driven) cmd; it does NOT
// block other features the way a dead/noclip return does.
void Recorder_OnCreateMove(void* pLocal, UserCmd_t* pCmd);

// ---- menu-facing controls (unitB_menu.cpp) -- render thread, all internally locked ----
void Recorder_StartRecord();
void Recorder_StopRecord();     // keeps the buffer so Save() can still write it
void Recorder_StartPlay();
void Recorder_StopPlay();
void Recorder_Clear();          // discard the live recording buffer
bool Recorder_IsRecording();
bool Recorder_IsPlaying();
int  Recorder_FrameCount();     // live recording buffer size (counter in the menu)

void Recorder_Refresh();                 // rescan kRecorderDir for the current map
int  Recorder_ClipCount();
const char* Recorder_ClipName(int index); // display name, "" if index is stale
void Recorder_Save(const char* name);     // writes the trimmed recording buffer as a new clip
void Recorder_Delete(int index);
void Recorder_SetSelected(int index);
int  Recorder_GetSelected();
bool Recorder_Load(int index);            // stages a clip for StartPlay()

// clipper: trim range applied at Save() time, [start, end) ticks of the LIVE buffer
void Recorder_SetTrim(int start, int end);
void Recorder_GetTrim(int& start, int& end, int& maxTicks);

// small "RECORDING" / "PLAYING" status line, top centre. Render thread, reads the same
// mutex-guarded flags the menu does.
void Recorder_RenderStatus();
