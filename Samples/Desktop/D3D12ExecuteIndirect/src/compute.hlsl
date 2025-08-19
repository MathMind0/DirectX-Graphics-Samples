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

#define threadBlockSize 64

struct PrimitiveStaticData
{
    float4 color;
    float2 velocity;
    float  size;
    float  rotation;
    float  padding[256 - 4 * 8];
};

struct PrimitiveDynamicData
{
    float2 position;
    float angle;
    float padding;
};

struct IndirectCommand
{
    uint2 cbvAddress;
    PrimitiveDynamicData cbDynamicData;
    uint4 drawArguments;
};

cbuffer RootConstants : register(b0)
{
    float4 cullBox;    
    float commandCount;    // The number of commands to be processed.
    float deltaTime;
};

StructuredBuffer<PrimitiveStaticData> cbv                : register(t0);    // SRV: Wrapped constant buffers
RWStructuredBuffer<IndirectCommand> inputCommands        : register(u0);    // UAV: Indirect commands
AppendStructuredBuffer<IndirectCommand> outputCommands   : register(u1);    // UAV: Processed indirect commands

[numthreads(threadBlockSize, 1, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    // Each thread of the CS operates on one of the indirect commands.
    uint index = dispatchID.x;

    // Don't attempt to access commands that don't exist if more threads are allocated than commands.
    if (index < commandCount)
    {
        PrimitiveStaticData cb = cbv[index];
        IndirectCommand cmd = inputCommands[index];

        cmd.cbDynamicData.position += cb.velocity * deltaTime;
        cmd.cbDynamicData.angle += cb.rotation * deltaTime;

        if (cmd.cbDynamicData.position.x < -1.0f)
        {
            cmd.cbDynamicData.position.x += 2.0f;
        }
        else if (cmd.cbDynamicData.position.x > 1.0f)
        {
            cmd.cbDynamicData.position.x -= 2.0f;
        }

        if (cmd.cbDynamicData.position.y < -1.0f)
        {
            cmd.cbDynamicData.position.y += 2.0f;
        }
        else if (cmd.cbDynamicData.position.y > 1.0f)
        {
            cmd.cbDynamicData.position.y -= 2.0f;
        }

        inputCommands[index].cbDynamicData.position = cmd.cbDynamicData.position;
        inputCommands[index].cbDynamicData.angle = cmd.cbDynamicData.angle;
        
        // Only draw triangles that are within the culling box.
        if (all(cmd.cbDynamicData.position >= cullBox.xy) && all(cmd.cbDynamicData.position < cullBox.zw))
        {
            outputCommands.Append(cmd);
        }
    }
}
