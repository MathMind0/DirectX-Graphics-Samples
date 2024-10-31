#define BLUR_RADIUS 4
static const half weight[] = {0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002};

#define BLOCK_SIZE 8
#define USE_CACHE 1

Texture2D texSceneColor : register(t0);
RWTexture2D<half4> gTexOutput : register(u0);

#if USE_CACHE
groupshared half3 CachedColor[BLOCK_SIZE][BLOCK_SIZE];
#endif

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSPostprocessBlurXBlock(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
#if USE_CACHE
    CachedColor[groupThreadID.x][groupThreadID.y] = texSceneColor[dispatchThreadID.xy].rgb;
    GroupMemoryBarrierWithGroupSync();

    half3 color = 0.0;
    int pos = groupThreadID.x - BLUR_RADIUS;
    int i = 0;
    
    while (pos < 0)
    {
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i, dispatchThreadID.y)].rgb * weight[i];
        pos++;
        i++;
    }

    while (pos < BLOCK_SIZE && i <= 2 * BLUR_RADIUS)
    {
        color += CachedColor[pos][groupThreadID.y] * weight[i];
        pos++;
        i++;
    }

    while (i <= 2 * BLUR_RADIUS)
    {
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i, dispatchThreadID.y)].rgb * weight[i];
        i++;
    }
    
#if 0
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i, dispatchThreadID.y)].rgb * weight[i];
    }
#endif
#else
    half3 color = 0.0;
    
    for (int i = 0; i <= 2 * BLUR_RADIUS; i+=3)
    {        
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i, dispatchThreadID.y)].rgb * weight[i];
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i + 1, dispatchThreadID.y)].rgb * weight[i + 1];
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i + 2, dispatchThreadID.y)].rgb * weight[i + 2];
    }
#endif

    gTexOutput[dispatchThreadID.xy] = half4(color, 0.0);
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSPostprocessBlurYBlock(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
#if USE_CACHE
    CachedColor[groupThreadID.x][groupThreadID.y] = texSceneColor[dispatchThreadID.xy].rgb;
    GroupMemoryBarrierWithGroupSync();

    half3 color = 0.0;
    int pos = groupThreadID.y - BLUR_RADIUS;
    int i = 0;
    
    while (pos < 0)
    {
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i)].rgb * weight[i];
        pos++;
        i++;
    }

    while (pos < BLOCK_SIZE && i <= 2 * BLUR_RADIUS)
    {
        color += CachedColor[groupThreadID.x][pos] * weight[i];
        pos++;
        i++;
    }

    while (i <= 2 * BLUR_RADIUS)
    {
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i)].rgb * weight[i];
        i++;
    }
    
#if 0
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i)].rgb * weight[i];
    }
#endif
#else
    for (int i = 0; i <= 2 * BLUR_RADIUS; i+=3)
    {        
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i)].rgb * weight[i];
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i + 1)].rgb * weight[i + 1];
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i + 2)].rgb * weight[i + 2];
    }
#endif
    
    gTexOutput[dispatchThreadID.xy] = half4(color, 0.0);
}