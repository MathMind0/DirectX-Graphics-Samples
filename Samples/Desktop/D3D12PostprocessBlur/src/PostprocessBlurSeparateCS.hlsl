#define BLUR_RADIUS 4
static const float weight[] = {0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002};

#define GROUP_SIZE 64
#define CACHE_SIZE GROUP_SIZE + 2 * BLUR_RADIUS

Texture2D texSceneColor : register(t0);
RWTexture2D<float4> gTexOutput : register(u0);

groupshared float3 CachedColor[CACHE_SIZE];

[numthreads(GROUP_SIZE, 1, 1)]
void CSPostprocessBlurX(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    if (groupThreadID.x < BLUR_RADIUS)
    {
        CachedColor[groupThreadID.x] =
            texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS, dispatchThreadID.y)].rgb;
    }

    if (groupThreadID.x >= GROUP_SIZE - BLUR_RADIUS)
    {
        CachedColor[groupThreadID.x + 2 * BLUR_RADIUS] =
            texSceneColor[int2(dispatchThreadID.x + BLUR_RADIUS, dispatchThreadID.y)].rgb;
    }

    CachedColor[groupThreadID.x + BLUR_RADIUS] = texSceneColor[dispatchThreadID.xy].rgb;

    GroupMemoryBarrierWithGroupSync();
    
    float4 color = 0.0;
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {        
        color.rgb += CachedColor[groupThreadID.x + i] * weight[i];
    }

    gTexOutput[dispatchThreadID.xy] = color;
}

[numthreads(1, GROUP_SIZE, 1)]
void CSPostprocessBlurY(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    if (groupThreadID.y < BLUR_RADIUS)
    {
        CachedColor[groupThreadID.y] =
            texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS)].rgb;
    }

    if (groupThreadID.y >= GROUP_SIZE - BLUR_RADIUS)
    {
        CachedColor[groupThreadID.y + 2 * BLUR_RADIUS] =
            texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y + BLUR_RADIUS)].rgb;
    }

    CachedColor[groupThreadID.y + BLUR_RADIUS] = texSceneColor[dispatchThreadID.xy].rgb;

    GroupMemoryBarrierWithGroupSync();
    
    float4 color = 0.0;
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {        
        color.rgb += CachedColor[groupThreadID.y + i] * weight[i];
    }

    gTexOutput[dispatchThreadID.xy] = color;
}