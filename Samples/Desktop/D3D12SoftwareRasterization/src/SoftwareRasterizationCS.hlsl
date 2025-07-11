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
    float3  position;
    uint    color;
};

float EdgeFunc(float2 edge0, float2 edge1)
{
    return edge0.x * edge1.y - edge0.y * edge1.x;
}

bool IsLeftTopEdge(float2 edge)
{
    return edge.y < 0.0 || (edge.y == 0.0 && edge.x > 0.0);
}

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

#if 0 //RASTER_TEST_64BIT
[RootSignature(RootSig)]
[numthreads(GROUPSIZEX, GROUPSIZEY, 1)]
void RasterMain(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    //Test 64bit atomic op.
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
}
#endif

#if 0 //RASTER_EDGE_EQUATIONS
[RootSignature(RootSig)]
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
                if (EdgeFunc(edges[i], v) < 0.0)
                    break;
            }

            if (i >= 3)
            {
                Canvas[uint2(x, y)] = 0xFF;
            }
        }
    }
}
#endif

#if 1 //RASTER_BARYCENTRIC
[RootSignature(RootSig)]
[numthreads(64, 1, 1)]
void RasterMain(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    if (DTid.x > numTriangles.x)
        return;

    uint3 index = Indices[DTid.x];

    Vertex vertices[3];
    float2 screenPos[3];
    uint4 colors[3];
    
    for (int i = 0; i < 3; i++)
    {
        vertices[i] = Vertices.Load(index[i]);

        float4 posH = mul(matMVP, float4(vertices[i].position, 1.0));
        float invz = 1.0 / posH.w; 

        vertices[i].position = posH.xyz * invz;

        screenPos[i] = (vertices[i].position.xy * float2(0.5, -0.5) + 0.5) * szCanvas;
        
        colors[i].r = vertices[i].color & 0xFF;
        colors[i].g = (vertices[i].color >> 8) & 0xFF;
        colors[i].b = (vertices[i].color >> 16) & 0xFF;
        colors[i].a = (vertices[i].color >> 24) & 0xFF;
    }

    float2 screenPosMin = min(screenPos[0], min(screenPos[1], screenPos[2]));
    float2 screenPosMax = max(screenPos[0], max(screenPos[1], screenPos[2]));

    int2 topLeft = max(int2(0, 0), floor(screenPosMin));
    int2 bottomRight = min(szCanvas - 1, floor(screenPosMax));

    float2 edges[3];
    edges[0] = screenPos[1] - screenPos[0];
    edges[1] = screenPos[2] - screenPos[1];
    edges[2] = screenPos[0] - screenPos[2];

    float area = EdgeFunc(edges[0], edges[2]);
    if (area <= 0.0001)
        return;

    float invArea = 1.0 / area;
    
    for (int y = topLeft.y; y < bottomRight.y; y++)
    {
        for (int x = topLeft.x; x < bottomRight.x; x++)
        {
            float2 p = float2(x + 0.5, y + 0.5);
            
#if 1 //LEFTTOP_RULE
            float area0 = EdgeFunc(p - screenPos[0], edges[0]);
            if (area0 < 0.0 || area0 > area || (area0 == 0.0 && !IsLeftTopEdge(edges[0])))
                continue;

            float area1 = EdgeFunc(p - screenPos[1], edges[1]);
            if (area1 < 0.0 || area1 > area || (area1 == 0.0 && !IsLeftTopEdge(edges[1])))
                continue;

            float area2 = area - area0 - area1;
            if (area2 < 0.0 || (area2 == 0.0 && !IsLeftTopEdge(edges[2])))
                continue;
#else
            float area0 = EdgeFunc(edges[0], p - screenPos[0]);
            if (area0 < 0.0 || area0 > area)
                continue;

            float area1 = EdgeFunc(edges[1], p - screenPos[1]);
            if (area1 < 0.0 || area1 > area)
                continue;

            float area2 = area - area0 - area1;
            if (area2 < 0.0)
                continue;
#endif
            float w0 = area1 * invArea;
            float w1 = area2 * invArea;
            float w2 = area0 * invArea;

            uint4 color = w0 * colors[0] + w1 * colors[1] + w2 * colors[2];
            uint64_t value = color.r;
            value |= color.g << 8;
            value |= color.b << 16;
            value |= color.a << 24;

            value |= DTid.x << 32;
            InterlockedMax(Canvas[uint2(x, y)], value);
            //Canvas[uint2(x, y)] = value;
        }
    }
}
#endif
