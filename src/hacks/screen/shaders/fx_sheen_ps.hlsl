// "normal blend" weapon sheen pixel shader. SOURCE OF RECORD for fx_sheen_ps*.h.
// Stands in for TF2's own weapon_sheen_pass ps (sdk2013 weapon_sheen_pass_ps2x.fxc) on the sheen pass draw:
// same engine vs, same bound textures (s2 = $sheenmap cube, s3 = $sheenmapmask, effects/animatedsheen/
// animatedsheen0, the botox-assets file), same reflection + mask uv math. Stock colours envmap * tint * 10
// (blown out) times the mask, alpha = envmap brightness * mask; here the tinted envmap reflection is the
// colour at our strength and the mask alone is the alpha, under the pass's own srcalpha / invsrcalpha blend.
// Ours: c10.x opacity, c10.y envmap strength (stock = 10).
//
// Rebuild after ANY edit here (engine pass is ps20b or ps30 by HasFastVertexTextures; ours must match its vs):
//   fxc /nologo /T ps_2_b /E main /Fh src/hacks/screen/shaders/fx_sheen_ps2.h /Vn k_fxSheenPs2Bytes src/hacks/screen/shaders/fx_sheen_ps.hlsl
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_sheen_ps3.h /Vn k_fxSheenPs3Bytes src/hacks/screen/shaders/fx_sheen_ps.hlsl
sampler EnvmapSampler     : register( s2 );
sampler EnvmapMaskSampler : register( s3 );

const float4 g_vPackedConst6 : register( c6 );   // mask scale x, y, offset x, y
const float4 g_vPackedConst7 : register( c7 );   // x = sheen direction 0/1/2
const float4 g_cSheenTint    : register( c8 );
const float4 cSheenNormal    : register( c10 );  // x = opacity, y = envmap strength
const float4 cLightScale     : register( c30 );  // z = ENV_MAP_SCALE (common_ps_fxc.h)

struct PS_INPUT
{
	float3   vWorldViewVector       : TEXCOORD2;
	float3x3 mTangentSpaceTranspose : TEXCOORD3;   // + TEXCOORD4, TEXCOORD5
	float4   vModelSpacePos         : TEXCOORD7;
};

float4 main( PS_INPUT i ) : COLOR
{
	// engine reflection, 1:1: flat tangent normal, never the bump
	float3 vEyeDir          = -normalize( i.vWorldViewVector );
	float3 worldSpaceNormal = normalize( mul( i.mTangentSpaceTranspose, float3( 0, 0, 1 ) ) );
	float3 vReflect         = 2 * worldSpaceNormal * dot( worldSpaceNormal, vEyeDir ) - vEyeDir;
	float3 envMapColor      = cLightScale.z * texCUBE( EnvmapSampler, vReflect ).rgb * g_cSheenTint.rgb * cSheenNormal.y;

	float3 ppos = i.vModelSpacePos.xyz;
	float2 temp;
	if ( g_vPackedConst7.x == 0 )      temp = ppos.zy;
	else if ( g_vPackedConst7.x == 1 ) temp = ppos.zx;
	else                               temp = ppos.yx;

	temp = ( temp - g_vPackedConst6.zw ) / g_vPackedConst6.xy;
	temp.y = 1.0 - temp.y;

	float mask = tex2D( EnvmapMaskSampler, temp ).x;
	return float4( saturate( envMapColor ), saturate( mask * cSheenNormal.x ) );
}
