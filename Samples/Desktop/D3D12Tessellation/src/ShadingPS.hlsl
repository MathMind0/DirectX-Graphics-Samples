//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
#include "ShadingCommon.hlsl"

//SamplerState sampleWrap : register(s0);
//SamplerState sampleClamp : register(s1);

//--------------------------------------------------------------------------------------
// Diffuse lighting calculation, with angle and distance falloff.
//--------------------------------------------------------------------------------------
float4 CalcLightingColor(float3 vLightPos, float3 vLightDir, float4 vLightColor, float4 vFalloffs,
    float3 vPosWorld, float3 vPerPixelNormal)
{
    float3 vLightToPixelUnNormalized = vPosWorld - vLightPos;

    // Dist falloff = 0 at vFalloffs.x, 1 at vFalloffs.x - vFalloffs.y
    float fDist = length(vLightToPixelUnNormalized);

    //float fDistFalloff = saturate((vFalloffs.x - fDist) / vFalloffs.y);

    // Normalize from here on.
    float3 vLightToPixelNormalized = vLightToPixelUnNormalized / fDist;

    // Angle falloff = 0 at vFalloffs.z, 1 at vFalloffs.z - vFalloffs.w
    //float fCosAngle = dot(vLightToPixelNormalized, vLightDir / length(vLightDir));
    //float fAngleFalloff = saturate((fCosAngle - vFalloffs.z) / vFalloffs.w);

    // Diffuse contribution.
    float fNDotL = saturate(-dot(vLightToPixelNormalized, vPerPixelNormal));
    
    //return vLightColor * fNDotL * fDistFalloff * fAngleFalloff;
    return vLightColor * fNDotL;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 totalLight = ambientColor;

    for (int i = 0; i < NUM_LIGHTS; i++)
    {
        float4 lightPass = CalcLightingColor(
            lights[i].position, lights[i].direction, lights[i].color, lights[i].falloff,
            input.worldpos.xyz, input.normal);
        totalLight += lightPass;
    }

    float4 diffuseColor;
    float height = saturate(input.worldpos.y / sceneInfo.y);

    diffuseColor.r = saturate(height - 0.666667);
    height -= diffuseColor.r;
    diffuseColor.r *= 3.0;

    diffuseColor.g = saturate(height - 0.333333);
    height -= diffuseColor.g;
    diffuseColor.g *= 3.0;
    
    diffuseColor.b = height * 3.0;
    diffuseColor.a = 1.0;
    
    return float4(1.0, 1.0, 1.0, 1.0);
    //return diffuseColor * saturate(totalLight);
}
