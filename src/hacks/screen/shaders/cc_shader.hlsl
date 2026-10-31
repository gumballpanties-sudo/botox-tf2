// Color correction post-process. SOURCE OF RECORD for cc_shader.h (the compiled bytecode).
//
// Rebuild after ANY edit here:
//   fxc /T ps_2_0 /E main /Fh src/hacks/screen/shaders/cc_shader.h /Vn k_ccShaderBytes src/hacks/screen/shaders/cc_shader.hlsl
//
// ps_2_0, not ps_3_0: the pass draws a D3DFVF_XYZRHW quad, i.e. fixed-function vertex
// processing, and D3D9 does not allow a 3_0 pixel shader to pair with that. It fits 2_0
// with room to spare (2_0 gives 64 arithmetic + 32 texture slots and 4 levels of
// dependent reads; this uses ~30 and 2).
//
// Pipeline order is fpsoverlay's, unchanged. Three things moved to the CPU because they
// are constant over the frame, and one moved into a lookup table:
//   * exposure (2^EV) is folded into the rgb gain          -- exact
//   * saturation and hue are both LINEAR ops with no clamp between them, so their two
//     3x3 matrices multiply into the ONE matrix in c3/c4/c5 -- exact
//   * the levels-in divide is a precomputed reciprocal      -- exact
//   * curves + levels-out are baked into a 256x1 LUT, so an arbitrary number of control
//     points and a real spline cost the same 3 taps that 5 tent knots used to cost in
//     ~35 ALU. The LUT is linearly filtered; error is under half an 8-bit code.

sampler2D sScene : register(s0);   // copy of the backbuffer
sampler2D sLut   : register(s1);   // 256x1 A8R8G8B8: curves (master o R/G/B) then levels out

float4 c0 : register(c0);   // rgb gain * 2^EV        (xyz), contrast     (w)
float4 c1 : register(c1);   // temperature offset rgb (xyz), 1/gamma      (w)
float4 c2 : register(c2);   // levels in black (x), 1/(inWhite-inBlack) (y), highlights (z), shadows (w)
float4 c3 : register(c3);   // saturation x hue, row 0
float4 c4 : register(c4);   // row 1
float4 c5 : register(c5);   // row 2

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 c = tex2D(sScene, uv).rgb * c0.rgb;   // gain + exposure
    c = saturate(c + c1.rgb);                    // temperature
    c = saturate((c - c2.x) * c2.y);             // levels in
    c = pow(c, c1.w);                            // gamma

    float3 m;                                    // saturation and hue, one matrix
    m.r = dot(c, c3.xyz);
    m.g = dot(c, c4.xyz);
    m.b = dot(c, c5.xyz);
    c = saturate(m);

    // Curves + levels out. Sampled at texel centres: u = (x*255 + 0.5) / 256.
    float3 u = c * (255.0 / 256.0) + (0.5 / 256.0);
    c = float3(tex2D(sLut, float2(u.r, 0.5)).r,
               tex2D(sLut, float2(u.g, 0.5)).g,
               tex2D(sLut, float2(u.b, 0.5)).b);

    float luma = dot(c, kLuma);                  // highlights / shadows
    c = saturate(c + c2.z * smoothstep(0.5, 1.0, luma)
                   + c2.w * (1.0 - smoothstep(0.0, 0.5, luma)));

    c = saturate((c - 0.5) * c0.w + 0.5);        // contrast
    return float4(c, 1.0);
}
