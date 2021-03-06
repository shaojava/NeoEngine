#include "Common.h"
#include "DeferredShadingCommon.h"


//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float4 Pos : POSITION;
	float2 uv  : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4	Pos		: SV_POSITION;
	float2	uv		: TEXCOORD0;
};

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
VS_OUTPUT VS( VS_INPUT IN )
{
    VS_OUTPUT OUT = (VS_OUTPUT)0;

    OUT.Pos = sign(IN.Pos);
	OUT.uv = IN.uv;
    
    return OUT;
}

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
Texture2D tex5 : register(t5);
Texture2D tex6 : register(t6);
Texture2D tex7 : register(t7);
TextureCube texCube0 : register(t10);
TextureCube texCube1 : register(t11);
SamplerState samPoint : register(s0);
SamplerState samLinear : register(s1);
SamplerState samPointBorder : register(s2);


//--------------------------------------------------------------------------------------
/// ComposePS
//--------------------------------------------------------------------------------------

float4 ComposePS( VS_OUTPUT IN ) : SV_Target
{
	float3 vNormal = tex1.Sample(samPoint, IN.uv).xyz;
	vNormal = normalize(Expand(vNormal));

	float3 vWorldPos = ReconstructWorldPos(tex3, samPoint, IN.uv);
	float3 vView = normalize(camPos - vWorldPos);

	// Sun light
	float fNdotL = saturate(dot(vNormal, lightDirection));
	float4 cDiffuse = fNdotL * lightColor;

	float4 specGloss = tex2.Sample(samPoint, IN.uv);
	float3 cSpecular = PhysicalBRDF(vNormal, vView, lightDirection, specGloss.w, specGloss.xyz);

	float4 albedo = tex0.Sample(samPoint, IN.uv);
	albedo.xyz = albedo.xyz * albedo.xyz;

	float4 oColor = albedo * cDiffuse;
	oColor.xyz += cSpecular * lightColor.xyz * fNdotL;

	// Shadow
	float4 vShadow = ComputeShadow(vWorldPos, ShadowTransform, ShadowTransform2, ShadowTransform3, shadowMapTexelSize, samPointBorder, tex4, tex5, tex6);
	oColor *= vShadow;

#ifdef AMBIENT_CUBE
	// Ambient
	float4 vAmbientDiff, vAmbientSpec;
	float fAmbDiffStrength = 0.3f, fAmbSpecStrength = 0.3f;
	ComputeAmbientCube(vAmbientDiff, vAmbientSpec, texCube0, texCube1, tex7, samLinear, vView, vNormal, specGloss.xyz, specGloss.w);

	oColor += vAmbientSpec * fAmbSpecStrength + vAmbientDiff * albedo * fAmbDiffStrength;
#endif

	return oColor;
}