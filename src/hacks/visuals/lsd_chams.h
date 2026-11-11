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
// "normal blend" killstreak sheen (g_sheenNormal): the same DIP detour spots TF2's weapon_sheen_pass
// ps by its constant table and swaps in fx_sheen_ps (flat tint, sheen mask = alpha). No arming: the
// pass is its own draw. Render thread, once a frame: installs the hook the first frame it is on.
void SheenNormal_Tick();

// Shader chams (Atlas-TF2 model.hlsl): every material slot but original / lsd / two tone. The layer draws a
// plain white VertexLitGeneric carrier (it exists at every graphics setting and brings normals); while armed
// the DIP detour swaps in fx_cham_vs + fx_cham_ps[mode], so the look never depends on mat_phong /
// mat_specular / shader detail. mode = ChamsLayers index, colour 0..1 display, alpha pre-scaled.
void ShaderChams_Begin(int mode, const float color[4], float realTime, bool ignorez);
void ShaderChams_End();

void TwoToneChams_Begin(const float light[4], const float dark[4], float threshold, float edge, bool ignorez);
void TwoToneChams_End();
