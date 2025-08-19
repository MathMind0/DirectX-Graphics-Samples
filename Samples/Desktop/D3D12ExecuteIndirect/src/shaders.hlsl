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

cbuffer PrimitiveStaticData : register(b0)
{
    float4 color;
    float2 velocity;
    float  size;
    float  rotation;
};

cbuffer PremitiveDynamicData : register(b1)
{
    float2 position;
    float angle;
};

cbuffer ViewData : register(b2)
{
    float ratio;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 vpos : POSITION)
{
    PSInput result;
   
    float sinAngle, cosAngle;
    sincos(angle, sinAngle, cosAngle);
    float2x2 rot = {cosAngle, sinAngle, -sinAngle, cosAngle};

    float2 posL = mul(vpos, rot);
    posL *= float2(size, size * ratio);
    
    result.position = float4(position + posL, length(velocity), 1.0f);
    
    result.color = color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
