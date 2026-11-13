// LSD chams vertex shader (botox hacks/visuals/lsd_chams.cpp k_vs, 1:1). SOURCE OF RECORD for fx_lsd_vs.h.
// Skinned studio position only. TF2 engine constants are CS:GO's (sdk2013 common_vs_fxc.h:83 cViewProj c8,
// :169 cModel[53] c58, :609 DecompressBoneWeights (w + 1) / 32768).
//
// SKINNED 0 = rigid mesh, no bone streams in the decl, cModel[0] (fx_cham_vs.hlsl has the why).
// DX8 1 = dxlevel 80/81 vs_1_1 layout, cModel[16] at c48 (fx_cham_vs.hlsl).
// Rebuild after ANY edit here: bash src/hacks/screen/shaders/fx_cham_build.sh -> fx_lsd_vs.h k_fxLsdVs[DX8][SKINNED][COMPRESSED_VERTS]
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
};

float4 main( const VS_INPUT i ) : POSITION
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

	return mul( float4( mul( i.vPos, blendMatrix ), 1.0f ), cViewProj );
}
