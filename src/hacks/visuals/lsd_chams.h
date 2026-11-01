#pragma once
// botox hacks/visuals/lsd_chams.{h,cpp} (commit 95ef94a): the "lsd" chams material. The layer draws
// a flat white carrier material (aim/chams.cpp); while armed, a DrawIndexedPrimitive detour swaps
// in the shadertoy "70s Melt" vs/ps (fx_lsd_*.hlsl). Plain C API: aim/chams.cpp (aim TU) calls it.
//
// Main thread, around one chams layer draw. Queued onto the render thread through the material
// system's call queue (screenfx.cpp ScreenFx_QueueFunctor) so the arm lands right before the
// model's queued draw and the disarm right after it; mat_queue_mode 0 = set directly.
void LsdChams_Begin(float realTime, float alpha);
void LsdChams_End();

// botox hacks/visuals/two_tone_chams.{h,cpp}: the "two tone" layer draws the model's OWN materials
// (no override, so its normal maps + lighting constants stay bound); while armed the same DIP
// detour swaps in a lit-side / shadow-side cut shader (fx_twotone_*.hlsl). Colours 0..1 display,
// alpha pre-scaled. Same queue rules as lsd. No DIP hook = the own materials draw as is.
void TwoToneChams_Begin(const float light[4], const float dark[4], float threshold, float edge, bool ignorez);
void TwoToneChams_End();
