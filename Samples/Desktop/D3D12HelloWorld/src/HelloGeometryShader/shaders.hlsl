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

struct VSOutput
{
    float4 position : POSITION;
    float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VSOutput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    VSOutput result;

    result.position = position;
    result.color = color;

    return result;
}

[maxvertexcount(15)]
void GSMain(triangle VSOutput vs[3], inout TriangleStream<PSInput> triStream)
{
    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        const int j = (i + 1) % 3;
        PSInput v0 = {vs[i].position, vs[i].color};
        PSInput v1 = {vs[j].position, vs[j].color};
        float2 dir = float2(v0.position.y - v1.position.y, v1.position.x - v0.position.x);
        
        triStream.Append(v0);
        v0.position.xy += dir;
        triStream.Append(v0);   
        
        triStream.Append(v1);
        v1.position.xy += dir;
        triStream.Append(v1);
        triStream.RestartStrip();
    }
    
    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        PSInput v = {vs[i].position, vs[i].color};
        triStream.Append(v);
    }   
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
