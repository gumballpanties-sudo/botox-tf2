// Two tone chams pixel shader (botox hacks/visuals/two_tone_chams.cpp k_ps, 1:1). SOURCE OF RECORD for fx_twotone_ps*.h.
// Engine pixel constants of the same draw, TF2 = CS:GO: sdk2013 shader_constant_register_map.h:16
// PSREG_AMBIENT_CUBE c4, :32 PSREG_LIGHT_INFO_ARRAY c20 (4th light in the w's). Ours: c30 light colour,
// c31 dark colour, c32 threshold / edge / alpha test. TF2 normal maps sit in s3 (skin_ps20b.fxc:107
// NormalMapSampler, vertexlit_and_unlit_generic_bump_ps2x.fxc:102 BumpmapSampler); s1 kept for botox parity.
//
// Rebuild after ANY edit here (three variants, one header each):
//   fxc /nologo /T ps_3_0 /E main /D BUMP_SAMPLER=0 /Fh src/hacks/screen/shaders/fx_twotone_ps0.h /Vn k_fxTwoTonePs0Bytes src/hacks/screen/shaders/fx_twotone_ps.hlsl
//   fxc /nologo /T ps_3_0 /E main /D BUMP_SAMPLER=1 /Fh src/hacks/screen/shaders/fx_twotone_ps1.h /Vn k_fxTwoTonePs1Bytes src/hacks/screen/shaders/fx_twotone_ps.hlsl
//   fxc /nologo /T ps_3_0 /E main /D BUMP_SAMPLER=3 /Fh src/hacks/screen/shaders/fx_twotone_ps2.h /Vn k_fxTwoTonePs2Bytes src/hacks/screen/shaders/fx_twotone_ps.hlsl
const float3 cAmbientCube[ 6 ] : register( c4 );
const float4 cLight[ 6 ] : register( c20 );
const float4 cLightColor : register( c30 );
const float4 cDarkColor : register( c31 );
const float4 cParams : register( c32 );

sampler BaseSampler : register( s0 );
#if BUMP_SAMPLER == 1
sampler NormalSampler : register( s1 );
#elif BUMP_SAMPLER == 3
sampler NormalSampler : register( s3 );
#endif

struct PS_INPUT {
	float2 vUv : TEXCOORD0;
	float3 vNormal : TEXCOORD1;
	float4 vTangent : TEXCOORD2;
	float3 vWorld : TEXCOORD3;
	float4 vAtten : TEXCOORD4;
};

float3 diffuse( float3 n, float3 world, float3 pos, float3 color, float atten )
{
	return color * atten * saturate( dot( n, normalize( pos - world ) ) );
}

float4 main( PS_INPUT i ) : COLOR
{
	float3 n = normalize( i.vNormal );
#if BUMP_SAMPLER
	// phong_ps20b.fxc:502-553
	float3 t  = normalize( i.vTangent.xyz );
	float3 b  = cross( n, t ) * i.vTangent.w;
	float3 tn = tex2D( NormalSampler, i.vUv ).xyz * 2.0f - 1.0f;
	n = normalize( tn.x * t + tn.y * b + tn.z * n );
#endif

	float3 sq    = n * n;
	float3 light = sq.x * ( n.x < 0 ? cAmbientCube[ 1 ] : cAmbientCube[ 0 ] ) +
	               sq.y * ( n.y < 0 ? cAmbientCube[ 3 ] : cAmbientCube[ 2 ] ) +
	               sq.z * ( n.z < 0 ? cAmbientCube[ 5 ] : cAmbientCube[ 4 ] );

	light += diffuse( n, i.vWorld, cLight[ 1 ].xyz, cLight[ 0 ].xyz, i.vAtten.x );
	light += diffuse( n, i.vWorld, cLight[ 3 ].xyz, cLight[ 2 ].xyz, i.vAtten.y );
	light += diffuse( n, i.vWorld, cLight[ 5 ].xyz, cLight[ 4 ].xyz, i.vAtten.z );
	light += diffuse( n, i.vWorld, float3( cLight[ 3 ].w, cLight[ 4 ].w, cLight[ 5 ].w ), float3( cLight[ 0 ].w, cLight[ 1 ].w, cLight[ 2 ].w ), i.vAtten.w );

	// cut in gamma space so the threshold slider feels even. fwidth floor = 1 px anti aliased line at edge 0
	float level = pow( max( dot( light, float3( 0.2126f, 0.7152f, 0.0722f ) ), 0.0f ), 1.0f / 2.2f );
	float width = max( max( cParams.y, fwidth( level ) ), 0.0001f );
	float4 col  = lerp( cDarkColor, cLightColor, smoothstep( cParams.x - width, cParams.x + width, level ) );

	col.a *= lerp( 1.0f, tex2D( BaseSampler, i.vUv ).a, cParams.z );
	return col;
}
