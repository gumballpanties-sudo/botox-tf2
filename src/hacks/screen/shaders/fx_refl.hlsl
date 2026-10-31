// Screen-space reflections, single pass. Reconstructs view-space position + normal off the
// same depth source every other screenfx pass shares (ScreenFx_DepthTexture) -- no new
// resolve, no new hook. SOURCE OF RECORD for fx_refl.h.
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_refl.h /Vn k_fxReflBytes src/hacks/screen/shaders/fx_refl.hlsl
//
// Output is (reflectedColor, alpha); the caller composites with SRCALPHA/INVSRCALPHA so a
// miss (alpha 0) leaves the backbuffer untouched -- same blend shape as botox's own SSR
// composite pass (reflections.cpp), just folded into one pass instead of five.
//
// ponytail: one pass, fixed step count, no normal-smoothing pass, no half-res render scale,
// no cubemap-miss fallback (botox's reflections.cpp has all four, ported from a reshade
// qUINT_ssr base). Upgrade path if a bare miss needs to stay lit: sample a baked env_cubemap
// the way that file does when `hit` is false below.
//
//   c0 = zNear, zFar, tan(fovX/2), tan(fovY/2)        -- the view_pos/linear_z convention
//        fx_ao_prepare.hlsl already uses, so AO and SSR agree on what a depth texel means.
//   c1 = 1/width, 1/height, intensity [0..1], viewmodel raw-depth cutoff (0.1)
//   c2 = max view-space ray distance (units), surface distance cutoff (botox "distance",
//        reshade-linear 0..1), unused, unused
//
// Weighting is botox's trace pass with angle falloff 0 (its default) and no cubemap:
//   a = intensity * border fade, out = (ssr * a, a * (1 - lin / distance)), SRCALPHA blend.
// The old schlick^5 * intensity weight was ~0.015 on a floor seen 30 degrees down: invisible.

#define REFL_STEPS 24
#define REFL_REFINE 4

sampler2D sScene : register(s0);   // StretchRect copy of the backbuffer -- see fx_ao_prepare's
sampler2D sDepth : register(s1);   // note on why a render target cannot sample itself
float4 c0 : register(c0);
float4 c1 : register(c1);
float4 c2 : register(c2);

float linear_z(float d) { return (c0.x * c0.y) / (c0.y - d * (c0.y - c0.x)); }
float3 view_pos(float2 uv, float z) { return float3((uv.x * 2.0 - 1.0) * c0.z, (1.0 - uv.y * 2.0) * c0.w, 1.0) * z; }
float raw_depth(float2 uv) { return tex2Dlod(sDepth, float4(uv, 0, 0)).r; }
float3 at(float2 uv) { return view_pos(uv, linear_z(raw_depth(uv))); }

// the screen uv a view-space point projects to, under the same tanX/tanY as view_pos above
float2 project(float3 p)
{
    float2 ndc = float2(p.x / (p.z * c0.z), p.y / (p.z * c0.w));
    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float rawD = raw_depth(uv);
    // sky (far plane) and the gun's own depth slice (raw < vmCutoff) never reflect.
    if (rawD >= 0.9999 || rawD < c1.w) return float4(0, 0, 0, 0);
    float lin = rawD / (1000.0 - rawD * 999.0);   // botox lin(): reshade linear, far plane 1000
    if (lin >= c2.y) return float4(0, 0, 0, 0);

    float z = linear_z(rawD);
    float3 p = view_pos(uv, z);

    // normal off four neighbours, same "nearer step wins" rule as fx_ao_prepare -- the
    // neighbour across a silhouette would otherwise bend the normal toward whatever is behind it.
    float3 xr = at(uv + float2(c1.x, 0)) - p;
    float3 xl = p - at(uv - float2(c1.x, 0));
    float3 yd = at(uv + float2(0, c1.y)) - p;
    float3 yu = p - at(uv - float2(0, c1.y));
    float3 dx = (abs(xr.z) < abs(xl.z)) ? xr : xl;
    float3 dy = (abs(yd.z) < abs(yu.z)) ? yd : yu;
    float3 n = normalize(cross(dy, dx));
    n *= (dot(n, p) > 0.0) ? -1.0 : 1.0;

    float3 viewDir = normalize(p);
    float3 rdir = reflect(viewDir, n);
    if (rdir.z <= 0.0) return float4(0, 0, 0, 0);   // points back at the eye, never resolves

    float maxDist = max(c2.x, 32.0);
    float stepLen = maxDist / REFL_STEPS;
    float3 pos = p;
    bool hit = false;
    float2 hitUv = uv;

    [loop]
    for (int i = 0; i < REFL_STEPS; ++i)
    {
        pos += rdir * stepLen;
        if (pos.z <= c0.x || pos.z >= maxDist) break;

        float2 suv = project(pos);
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;

        float sceneZ = linear_z(raw_depth(suv));
        float diff = pos.z - sceneZ;
        if (diff > 0.0 && diff < stepLen * 2.0)
        {
            float3 lo = pos - rdir * stepLen, hi = pos;
            [loop]
            for (int j = 0; j < REFL_REFINE; ++j)
            {
                float3 mid = (lo + hi) * 0.5;
                float mz = linear_z(raw_depth(project(mid)));
                if (mid.z - mz > 0.0) hi = mid; else lo = mid;
            }
            hitUv = project(hi);
            hit = true;
            break;
        }
    }

    if (!hit) return float4(0, 0, 0, 0);

    // botox: hits in the outer 1/8 of the screen fade out instead of cutting hard
    float2 border = min(hitUv, 1.0 - hitUv);
    float a = saturate(c1.z) * saturate(min(border.x, border.y) * 8.0);
    return float4(tex2Dlod(sScene, float4(hitUv, 0, 0)).rgb * a, a * saturate(1.0 - lin / c2.y));
}
