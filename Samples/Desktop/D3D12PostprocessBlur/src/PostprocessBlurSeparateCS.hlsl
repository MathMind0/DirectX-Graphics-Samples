#define BLUR_RADIUS 4
static const half weight[] = {0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002};

#define GROUP_SIZE 64
#define CACHE_SIZE ((GROUP_SIZE) + (2 * (BLUR_RADIUS)))
#define USE_DIRECT_INDEX 0

Texture2D texSceneColor : register(t0);
RWTexture2D<half4> gTexOutput : register(u0);

groupshared half3 CachedColor[CACHE_SIZE];

#if USE_DIRECT_INDEX
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
    
    half3 color = 0.0;
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {        
        color += CachedColor[groupThreadID.x + i] * weight[i];
    }

    gTexOutput[dispatchThreadID.xy] = half4(color, 0.0);
}
#else
[numthreads(1, GROUP_SIZE, 1)]
void CSPostprocessBlurX(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    if (groupThreadID.y < BLUR_RADIUS)
    {
        CachedColor[groupThreadID.y] =
            texSceneColor[int2(dispatchThreadID.y - BLUR_RADIUS, dispatchThreadID.x)].rgb;
    }

    if (groupThreadID.y >= GROUP_SIZE - BLUR_RADIUS)
    {
        CachedColor[groupThreadID.y + 2 * BLUR_RADIUS] =
            texSceneColor[int2(dispatchThreadID.y + BLUR_RADIUS, dispatchThreadID.x)].rgb;
    }

    CachedColor[groupThreadID.y + BLUR_RADIUS] = texSceneColor[dispatchThreadID.yx].rgb;

    GroupMemoryBarrierWithGroupSync();
    
    half3 color = 0.0;
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {        
        color += CachedColor[groupThreadID.y + i] * weight[i];
    }

    gTexOutput[dispatchThreadID.yx] = half4(color, 0.0);
}
#endif

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
    
    half3 color = 0.0;
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {        
        color += CachedColor[groupThreadID.y + i] * weight[i];
    }

    gTexOutput[dispatchThreadID.xy] = half4(color, 0.0);
}