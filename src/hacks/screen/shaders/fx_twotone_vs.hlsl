// Two tone chams vertex shader (botox hacks/visuals/two_tone_chams.cpp k_vs, 1:1). SOURCE OF RECORD for fx_twotone_vs*.h.
// Skinned position + normal + tangent. Engine constants left by the model's own (phong / vertexlit) shader,
// TF2 = CS:GO here: sdk2013 common_vs_fxc.h:83 cViewProj c8, :127 cLightInfo[4] c27, :169 cModel[53] c58.
// c240 = which lights are live (ours, set by the DIP detour from i0 = g_nLightCountRegister, common_vs_fxc.h:70).
//
// Rebuild after ANY edit here (three variants, one header each):
//   fxc /nologo /T vs_3_0 /E main /D COMPRESSED_VERTS=0 /D HAS_TANGENT=0 /Fh src/hacks/screen/shaders/fx_twotone_vs0.h /Vn k_fxTwoToneVs0Bytes src/hacks/screen/shaders/fx_twotone_vs.hlsl
//   fxc /nologo /T vs_3_0 /E main /D COMPRESSED_VERTS=0 /D HAS_TANGENT=1 /Fh src/hacks/screen/shaders/fx_twotone_vs1.h /Vn k_fxTwoToneVs1Bytes src/hacks/screen/shaders/fx_twotone_vs.hlsl
//   fxc /nologo /T vs_3_0 /E main /D COMPRESSED_VERTS=1 /D HAS_TANGENT=0 /Fh src/hacks/screen/shaders/fx_twotone_vs2.h /Vn k_fxTwoToneVs2Bytes src/hacks/screen/shaders/fx_twotone_vs.hlsl
float4x4 cViewProj : register( c8 );

struct LightInfo {
	float4 color;
	float4 dir;
	float4 pos;
	float4 spotParams;
	float4 atten;
};
LightInfo cLightInfo[ 4 ] : register( c27 );

float4x3 cModel[ 53 ] : register( c58 );
float4 cLightUsed : register( c240 );

struct VS_INPUT {
	float4 vPos : POSITION;
	float4 vBoneWeights : BLENDWEIGHT;
	float4 vBoneIndices : BLENDINDICES;
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
	float3 vWorld : TEXCOORD3;
	float4 vAtten : TEXCOORD4;
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

// common_vs_fxc.h VertexAttenInternal 1:1
float light_atten( const float3 worldPos, const int lightNum )
{
	float3 lightDir        = cLightInfo[ lightNum ].pos.xyz - worldPos;
	float lightDistSquared = dot( lightDir, lightDir );
	float ooLightDist      = rsqrt( lightDistSquared );
	lightDir *= ooLightDist;

	float3 vDist          = dst( lightDistSquared, ooLightDist ).xyz;
	float flDistanceAtten = 1.0f / dot( cLightInfo[ lightNum ].atten.xyz, vDist );

	float flCosTheta  = dot( cLightInfo[ lightNum ].dir.xyz, -lightDir );
	float flSpotAtten = ( flCosTheta - cLightInfo[ lightNum ].spotParams.z ) * cLightInfo[ lightNum ].spotParams.w;
	flSpotAtten       = max( 0.0001f, flSpotAtten );
	flSpotAtten       = pow( flSpotAtten, cLightInfo[ lightNum ].spotParams.x );
	flSpotAtten       = saturate( flSpotAtten );

	float flAtten = lerp( flDistanceAtten, flDistanceAtten * flSpotAtten, cLightInfo[ lightNum ].dir.w );
	return lerp( flAtten, 1.0f, cLightInfo[ lightNum ].color.w );
}

VS_OUTPUT main( const VS_INPUT i )
{
	float4 vWeights = i.vBoneWeights;
#if COMPRESSED_VERTS
	vWeights = ( vWeights + 1 ) / 32768;
#endif
	int3 boneIndices = D3DCOLORtoUBYTE4( i.vBoneIndices ).xyz;
	float3 weights   = vWeights.xyz;
	weights[ 2 ]     = 1 - ( weights[ 0 ] + weights[ 1 ] );
	float4x3 blendMatrix = cModel[ boneIndices[ 0 ] ] * weights[ 0 ] + cModel[ boneIndices[ 1 ] ] * weights[ 1 ] + cModel[ boneIndices[ 2 ] ] * weights[ 2 ];

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
	o.vWorld   = world;

	// unused light slots hold another draw's leftovers: never evaluate them
	o.vAtten = 0;
	[branch] if ( cLightUsed.x > 0.5 ) o.vAtten.x = light_atten( world, 0 );
	[branch] if ( cLightUsed.y > 0.5 ) o.vAtten.y = light_atten( world, 1 );
	[branch] if ( cLightUsed.z > 0.5 ) o.vAtten.z = light_atten( world, 2 );
	[branch] if ( cLightUsed.w > 0.5 ) o.vAtten.w = light_atten( world, 3 );
	return o;
}
