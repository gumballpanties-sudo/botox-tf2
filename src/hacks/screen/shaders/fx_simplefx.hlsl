// Unit D port: sharpen / film grain / vignette(+invert) / invert filter / posterize / deband /
// channel shift -- one pass over a copy of the finished frame.
// SOURCE OF RECORD for fx_simplefx.h (the compiled bytecode).
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_2_0 /E main /Fh src/hacks/screen/shaders/fx_simplefx.h /Vn k_fxSimpleFxBytes src/hacks/screen/shaders/fx_simplefx.hlsl
//
// ps_2_0, not 3_0: the quad is drawn through fixed-function vertex processing (XYZRHW),
// same constraint as every other screenfx pass (fx_probe.hlsl, fx_bloom_bright.hlsl).
// ps_2_0's 64-arithmetic-slot cap is why the noise below avoids sin/cos (each expands to a
// dozen+ instructions under this profile) and why the vignette distance term
// is one dot product.
//
// sScene = a StretchRect copy of the backbuffer (sampled, not written -- same reason
// bloom.cpp keeps its own scene copy: a render target cannot read itself).
//
// c0 = (1/width, 1/height, sharpenAmount, filmGrainAmount)
// c1 = (vignetteAmount, vignetteInvert 0/1, posterizeLevels, debandAmount)
// c2 = (channelShiftAmount, unused, unused, invertFilter 0/1)
// c3 = (noisePhase, width, height, unused)

sampler2D sScene : register(s0);
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);
float4 c3 : register(c3);

// trig-free hash (Dave Hoskins "hash without sine", hash12) -- frac(sin(...)) blew the ps_2_0
// arithmetic budget on its own. Takes PIXEL coords. The old one was frac(dot(uv, k) + phase):
// a linear ramp in uv, so frac of it is diagonal stripes, not noise (the "broken film grain").
float Hash(float2 p)
{
    float3 p3 = frac(p.xyx * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 texel = c0.xy;
    float3 col = tex2D(sScene, uv).rgb;
    // one noise value per pixel for deband AND grain (two hashes overflow ps_2_0's 64 slots).
    // Pixel coords = uv * screen size (c3.yz), jittered per frame by the cpu phase (c3.x) so it moves
    float noise = Hash(uv * c3.yz + c3.x * float2(1.0, 1.618)) - 0.5;

    // ---- channel shift: R and B sampled with a horizontal offset in opposite directions
    float ox = c2.x * texel.x;
    col.r = tex2D(sScene, uv + float2( ox, 0.0)).r;
    col.b = tex2D(sScene, uv + float2(-ox, 0.0)).b;

    // ---- sharpen: unsharp mask, 4 neighbour taps against the (possibly already shifted) centre
    float3 n = tex2D(sScene, uv + float2(0.0, -texel.y)).rgb
             + tex2D(sScene, uv + float2(0.0,  texel.y)).rgb
             + tex2D(sScene, uv + float2(-texel.x, 0.0)).rgb
             + tex2D(sScene, uv + float2( texel.x, 0.0)).rgb;
    col += (col * 4.0 - n) * c0.z;

    // ---- posterize: quantize to N levels per channel. The cpu side sends a huge level
    // count (1024) while the feature is off -- a quantization step that fine is identity
    // to the eye, and costs no branch here (ps_2_0 has none).
    col = floor(col * c1.z + 0.5) / c1.z;

    // ---- deband: dither noise, hides banding the game's own 8-bit backbuffer already has
    col += noise * (c1.w / 255.0) * 4.0;

    // ---- radial term for vignette
    float2 fromCentre = uv - 0.5;
    float d = dot(fromCentre, fromCentre) * 4.0;   // 0 centre .. ~2 corner, squared already

    // emphasize is NOT here: it is depth based (botox depth_view.cpp, reshade Emphasize.fx) and
    // lives in fx_depthview.hlsl, drawn from the game's post slot where the depth is live.

    // ---- vignette: radial darken, or (inverted) a lifted-edge spotlight look. Identity at
    // amount 0 (vig == 1) needs no extra guard.
    float vig = saturate(1.0 - d * c1.x);
    vig = lerp(vig, 1.0 - vig + (1.0 - c1.x), c1.y);
    col *= vig;

    // ---- invert filter: whole-frame negative
    col = lerp(col, 1.0 - col, c2.w);

    // ---- film grain: animated per-pixel noise, applied last so it rides on top of everything
    col += noise * c0.w;

    return float4(saturate(col), 1.0);
}
