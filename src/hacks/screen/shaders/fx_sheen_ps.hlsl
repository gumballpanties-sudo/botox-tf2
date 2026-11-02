// "normal blend" weapon sheen pixel shader. SOURCE OF RECORD for fx_sheen_ps*.h.
// Stands in for TF2's own weapon_sheen_pass ps (sdk2013 weapon_sheen_pass_ps2x.fxc) on the sheen pass draw:
// same engine vs, same bound mask (s3 = $sheenmapmask, effects/animatedsheen/animatedsheen0, the botox-assets
// file), same mask uv math (c6 scale/offset, c7.x direction). Instead of envmap * mask added on top, the
// sheen tint (c8, 0..1 display) is painted flat with the mask as its alpha, under the pass's own
// srcalpha / invsrcalpha blend. Ours: c10.x opacity.
//
// Rebuild after ANY edit here (engine pass is ps20b or ps30 by HasFastVertexTextures; ours must match its vs):
//   fxc /nologo /T ps_2_b /E main /Fh src/hacks/screen/shaders/fx_sheen_ps2.h /Vn k_fxSheenPs2Bytes src/hacks/screen/shaders/fx_sheen_ps.hlsl
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_sheen_ps3.h /Vn k_fxSheenPs3Bytes src/hacks/screen/shaders/fx_sheen_ps.hlsl
sampler EnvmapMaskSampler : register( s3 );

const float4 g_vPackedConst6 : register( c6 );   // mask scale x, y, offset x, y
const float4 g_vPackedConst7 : register( c7 );   // x = sheen direction 0/1/2
const float4 g_cSheenTint    : register( c8 );
const float4 cSheenNormal    : register( c10 );  // x = opacity

float4 main( float4 vModelSpacePos : TEXCOORD7 ) : COLOR
{
	float3 ppos = vModelSpacePos.xyz;
	float2 temp;
	if ( g_vPackedConst7.x == 0 )      temp = ppos.zy;
	else if ( g_vPackedConst7.x == 1 ) temp = ppos.zx;
	else                               temp = ppos.yx;

	temp = ( temp - g_vPackedConst6.zw ) / g_vPackedConst6.xy;
	temp.y = 1.0 - temp.y;

	float mask = tex2D( EnvmapMaskSampler, temp ).x;
	return float4( saturate( g_cSheenTint.rgb ), saturate( mask * cSheenNormal.x ) );
}
