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
    float3 color : COLOR;
};


struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

static const float ONE_OVER_MAX_UINT = 1.0 / 0xFFFFFFFF;
static const float ONE_OVER_255 = 1.0 / 0xFF;

cbuffer ConstantBufferBlocks : register(b0)
{
    uint4 nTiles;
    float4 blockWidth;
    float4 padding[14];
};

RWStructuredBuffer<float3> gOutputVertexBufferPos : register(u0);
RWStructuredBuffer<float3> gOutputVertexBufferColor : register(u1);
RWStructuredBuffer<float2> gOutputVelocityBuffer : register(u2);

[numthreads(8, 8, 1)]
void CSInitBlocks(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float3 pos;
    pos.xy = blockWidth.x * dispatchThreadID.xy - 1.0;
    pos.z = 0.f;

    float3 color;
    color.rgb = uint3(
        (dispatchThreadID.x ^ dispatchThreadID.y) & 0xFF,
        ((dispatchThreadID.x * 37) ^ dispatchThreadID.y) & 0xFF,
        (dispatchThreadID.x ^ (dispatchThreadID.y * 37) & 0xFF)) * ONE_OVER_255;
    
    uint indexVelocity = dispatchThreadID.y * 8 * nTiles.x + dispatchThreadID.x;
    uint indexVertex = indexVelocity * 6;
    
    gOutputVertexBufferPos[indexVertex] = pos;
    gOutputVertexBufferPos[indexVertex + 3] = pos;
    pos.x += blockWidth;
    gOutputVertexBufferPos[indexVertex + 1] = pos;
    pos.y += blockWidth;
    gOutputVertexBufferPos[indexVertex + 2] = pos;
    gOutputVertexBufferPos[indexVertex + 4] = pos;
    pos.x -= blockWidth;
    gOutputVertexBufferPos[indexVertex + 5] = pos;

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        gOutputVertexBufferColor[indexVertex + i] = color;
    }

    gOutputVelocityBuffer[indexVelocity].xy = color.rg;
}

StructuredBuffer<float3> gInputVertexBufferPos : register(t0);

[numthreads(8, 8, 1)]
void CSUpdateBlocks(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint indexVelocity = dispatchThreadID.y * 8 * nTiles.x + dispatchThreadID.x;
    uint indexVertex = indexVelocity * 6;

    float2 velocity = gOutputVelocityBuffer[indexVelocity];
    float2 newPos = gInputVertexBufferPos[indexVertex].xy + velocity;
    
    if (newPos.x < -1.0 || newPos.x > 1.0)
    {
        velocity.x = -velocity.x;
    }

    if (newPos.y < -1.0 || newPos.y > 1.0)
    {
        velocity.y = -velocity.y;
    }

    gOutputVelocityBuffer[indexVelocity] = velocity;

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        gOutputVertexBufferPos[indexVertex + i].xy += velocity;
        gOutputVertexBufferPos[indexVertex + i].z = 0.0;
    }
}

PSInput VSMain(VSInput v)
{
    PSInput result;

    result.position = float4(v.position, 1.0);
    result.color = float4(v.color, 1.0);

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
