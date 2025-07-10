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
    uint2 numTriangles;
};

struct Vertex
{
    float3 position;
};

StructuredBuffer<Vertex> Vertices : register(t0);
StructuredBuffer<uint3> Indices : register(t1);
RWTexture2D<uint64_t> Canvas: register(u0);    

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void RasterInit(uint2 DTid : SV_DispatchThreadID)
{
    if (all(DTid.xy < szCanvas.xy))
    {
        Canvas[DTid] = 0;
    }
}

[RootSignature(RootSig)]
//[numthreads(GROUPSIZEX, GROUPSIZEY, 1)]
[numthreads(64, 1, 1)]
void RasterMain(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    if (DTid.x > numTriangles.x)
        return;

    uint3 index = Indices[DTid.x];

    Vertex vertices[3];
    float2 screenPos[3];
    
    for (int i = 0; i < 3; i++)
    {
        vertices[i] = Vertices.Load(index[i]);
        float4 posH = mul(matMVP, float4(vertices[i].position, 1.0));
        float invz = 1.0 / posH.w; 
        vertices[i].position = posH.xyz * invz;
        screenPos[i] = (vertices[i].position.xy * float2(0.5, -0.5) + 0.5) * szCanvas;
    }

    float2 screenPosMin = min(screenPos[0], min(screenPos[1], screenPos[2]));
    float2 screenPosMax = max(screenPos[0], max(screenPos[1], screenPos[2]));

    int2 topLeft = max(int2(0, 0), ceil(screenPosMin));
    int2 bottomRight = min(szCanvas - 1, ceil(screenPosMax));

    float2 edges[3];
    edges[0] = screenPos[1] - screenPos[0];
    edges[1] = screenPos[2] - screenPos[1];
    edges[2] = screenPos[0] - screenPos[2];
    
    for (int y = topLeft.y; y < bottomRight.y; y++)
    {
        for (int x = topLeft.x; x < bottomRight.x; x++)
        {
            int i = 0;
            for (; i < 3; i++)
            {
                float2 v = float2(x + 0.5, y + 0.5) - screenPos[i];
                if (edges[i].x * v.y - edges[i].y * v.x < 0.0)
                    break;
            }

            if (i >= 3)
            {
                Canvas[uint2(x, y)] = 0xFF;
            }
        }
    }
    
#if 0 //Test 64bit atomic op.
    uint rand = ((DTid.x + 1) * 53) ^ ((DTid.y + 1) * 59);

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
