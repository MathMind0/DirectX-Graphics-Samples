#define RootSig \
    "CBV(b0)," \
    "SRV(t0)," \
    "SRV(t1)," \
    "DescriptorTable( UAV(u0) )"

#define GROUPSIZEX 16
#define GROUPSIZEY 16
#define BLOCK_SIZE 16
#define BLOCK_OFFSET 8

cbuffer cbRasterization : register(b0)
{
    float4x4 matMVP;
    uint2 szCanvas;
    uint numTriangles;
};

struct Vertex
{
    float3 position;
};

StructuredBuffer<Vertex> Vertices : register(t0);
Buffer<uint3> Indices : register(t1);
RWTexture2D<uint64_t> Canvas: register(u0);    

[RootSignature(RootSig)]
[numthreads(GROUPSIZEX, GROUPSIZEY, 1)]
void RasterMain(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    uint rand = (DTid.x * 524287 ^ DTid.y * 65537);
    
#if 1
    for (uint py = DTid.y * BLOCK_OFFSET; py < DTid.y * BLOCK_OFFSET + BLOCK_SIZE; py++)
    {
        if (py < szCanvas.y)
        {
            for (uint px = DTid.x * BLOCK_OFFSET; px < DTid.x * BLOCK_OFFSET + BLOCK_SIZE; px++)
            {
                if (px < szCanvas.x)
                {
                    uint64_t value = rand << 32 | rand;
                    InterlockedMax(Canvas[uint2(px, py)], value);
                }
            }
        }
    }
#endif
}
