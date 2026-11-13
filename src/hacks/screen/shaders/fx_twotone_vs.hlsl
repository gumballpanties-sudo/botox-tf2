// Two tone chams vertex shader (botox hacks/visuals/two_tone_chams.cpp k_vs, 1:1). SOURCE OF RECORD for fx_twotone_vs*.h.
// Skinned position + normal + tangent. Engine constants left by the model's own shader, TF2 = CS:GO here:
// sdk2013 common_vs_fxc.h:75 cEyePosWaterZ c2 (ishadersystem_declarations.h:13 VERTEX_SHADER_CAMERA_POS),
// :83 cViewProj c8, :169 cModel[53] c58.
//
// SKINNED 0 = rigid mesh, no bone streams in the decl, cModel[0] (fx_cham_vs.hlsl has the why).
// Rebuild after ANY edit here: bash src/hacks/screen/shaders/fx_cham_build.sh
//   -> fx_twotone_vs.h k_fxTwoToneVs[DX8][SKINNED][layout], layout 0 float verts, 1 float + TANGENT stream, 2 compressed
// DX8 1 = dxlevel 80/81 vs_1_1 layout, cModel[16] at c48 (fx_cham_vs.hlsl).
float4 cEyePos : register( c2 );
float4x4 cViewProj : register( c8 );
#if DX8
float4x3 cModel[ 16 ] : register( c48 );
#else
float4x3 cModel[ 53 ] : register( c58 );
#endif

struct VS_INPUT {
	float4 vPos : POSITION;
#if SKINNED
	float4 vBoneWeights : BLENDWEIGHT;
	float4 vBoneIndices : BLENDINDICES;
#endif
	float4 vNormal : NORMAL;
	float4 vTexCoord0 : TEXCOORD0;
#if HAS_TANGENT
	float4 vUserData : TANGENT;
#endif
};

struct VS_OUTPUT {
	float4 vPos : POSITION;
	float2 vUv : TEXCOORD0;
	float3 vNormal : TEXCOORD1;
	float4 vTangent : TEXCOORD2;
	float3 vView : TEXCOORD3;
};

// common_vs_fxc.h _DecompressUByte4NormalTangent 1:1
void decompress( float4 inputNormal, out float3 outputNormal, out float4 outputTangent )
{
	float4 ztztSignBits = ( inputNormal - 128.0f ) < 0;
	float4 xyxyAbs      = abs( inputNormal - 128.0f ) - ztztSignBits;
	float4 xyxySignBits = ( xyxyAbs - 64.0f ) < 0;
	float4 normTan      = ( abs( xyxyAbs - 64.0f ) - xyxySignBits ) / 63.0f;
	outputNormal.xy     = normTan.xy;
	outputTangent.xy    = normTan.zw;

	float4 xyxySigns = 1 - 2 * xyxySignBits;
	float4 ztztSigns = 1 - 2 * ztztSignBits;

	outputNormal.z    = 1.0f - outputNormal.x - outputNormal.y;
	outputNormal.xyz  = normalize( outputNormal.xyz );
	outputNormal.xy  *= xyxySigns.xy;
	outputNormal.z   *= ztztSigns.x;

	outputTangent.z    = 1.0f - outputTangent.x - outputTangent.y;
	outputTangent.xyz  = normalize( outputTangent.xyz );
	outputTangent.xy  *= xyxySigns.zw;
	outputTangent.z   *= ztztSigns.z;
	outputTangent.w    = ztztSigns.w;
}

VS_OUTPUT main( const VS_INPUT i )
{
#if SKINNED
	float4 vWeights = i.vBoneWeights;
#if COMPRESSED_VERTS
	vWeights = ( vWeights + 1 ) / 32768;
#endif
	int3 boneIndices = D3DCOLORtoUBYTE4( i.vBoneIndices ).xyz;
	float3 weights   = vWeights.xyz;
	weights[ 2 ]     = 1 - ( weights[ 0 ] + weights[ 1 ] );
	float4x3 blendMatrix = cModel[ boneIndices[ 0 ] ] * weights[ 0 ] + cModel[ boneIndices[ 1 ] ] * weights[ 1 ] + cModel[ boneIndices[ 2 ] ] * weights[ 2 ];
#else
	float4x3 blendMatrix = cModel[ 0 ];
#endif

	float3 normal;
	float4 tangent;
#if COMPRESSED_VERTS
	decompress( i.vNormal, normal, tangent );
#else
	normal = i.vNormal.xyz;
#if HAS_TANGENT
	tangent = i.vUserData;
#else
	tangent = float4( 1, 0, 0, 1 );
#endif
#endif

	float3 world = mul( i.vPos, blendMatrix );

	VS_OUTPUT o;
	o.vPos     = mul( float4( world, 1.0f ), cViewProj );
	o.vUv      = i.vTexCoord0.xy;
	o.vNormal  = mul( normal, ( float3x3 )blendMatrix );
	o.vTangent = float4( mul( tangent.xyz, ( float3x3 )blendMatrix ), tangent.w );
	o.vView    = cEyePos.xyz - world;
	return o;
}
