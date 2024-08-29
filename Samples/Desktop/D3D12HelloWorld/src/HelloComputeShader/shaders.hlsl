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

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};


struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

const float ONE_OVER_MAX_UINT = 1.0 / 0xFFFFFFFF;

#if 0
cbuffer InitBlocksConstantBuffer : register(b0)
{
    uint4 nTiles;
    float4 blockWidth;
    float4 padding[14];
};

RWStructuredBuffer<VSInput> gOutputVertexBuffer : register(u0);
RWStructuredBuffer<float2> gOutputVelocityBuffer : register(u1);
#endif

[numthreads(8, 8, 1)]
void CSInitBlocks(uint3 dispatchThreadID : SV_DispatchThreadID)
{
#if 0
    VSInput v;
    v.position.xy = blockWidth.x * dispatchThreadID.xy;
    v.position.z = 0.f;
    v.color.rgb = ONE_OVER_MAX_UINT * uint3(
        dispatchThreadID.x ^ dispatchThreadID.y,
        (dispatchThreadID.x * 37) ^ dispatchThreadID.y,
        dispatchThreadID.x ^ (dispatchThreadID.y * 37));
    v.color.a = 1.0;

    uint indexVelo = dispatchThreadID.y * 8 * nTiles.x + dispatchThreadID.x;
    uint indexVert = indexVelo * 6;
    
    gOutputVertexBuffer[indexVert] = v;
    gOutputVertexBuffer[indexVert + 3] = v;
    v.position.x += blockWidth;
    gOutputVertexBuffer[indexVert + 1] = v;
    v.position.y += blockWidth;
    gOutputVertexBuffer[indexVert + 2] = v;
    gOutputVertexBuffer[indexVert + 4] = v;
    v.position.x -= blockWidth;
    gOutputVertexBuffer[indexVert + 5] = v;

    gOutputVelocityBuffer[indexVelo].xy = v.color.rg;
#endif
}


PSInput VSMain(VSInput v)
{
    PSInput result;

    result.position = float4(v.position, 1.0);
    result.color = v.color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
