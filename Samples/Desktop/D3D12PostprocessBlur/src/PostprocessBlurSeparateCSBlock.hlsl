#define BLUR_RADIUS 4
static const half weight[] = {0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002};

#define BLOCK_SIZE 8
#define USE_CACHE 1
#define CACHE_TEX_MIXED 0

Texture2D texSceneColor : register(t0);
RWTexture2D<half4> gTexOutput : register(u0);

#if USE_CACHE
#if CACHE_TEX_MIXED
groupshared half3 CachedColor[BLOCK_SIZE][BLOCK_SIZE];
#else
#define CACHE_SIZE ((BLOCK_SIZE) + (2 * (BLUR_RADIUS)))
groupshared half3 CachedColor[CACHE_SIZE][BLOCK_SIZE];
#endif
#endif

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSPostprocessBlurXBlock(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    half3 color = 0.0;
#if USE_CACHE
#if CACHE_TEX_MIXED
    CachedColor[groupThreadID.x][groupThreadID.y] = texSceneColor[dispatchThreadID.xy].rgb;
    GroupMemoryBarrierWithGroupSync();
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
#else // !CACHE_TEX_MIXED
    if (groupThreadID.x < BLUR_RADIUS)
    {
        CachedColor[groupThreadID.x][groupThreadID.y] =
            texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS, dispatchThreadID.y)].rgb;
    }

    if (groupThreadID.x >= BLOCK_SIZE - BLUR_RADIUS)
    {
        CachedColor[groupThreadID.x + 2 * BLUR_RADIUS][groupThreadID.y] =
            texSceneColor[int2(dispatchThreadID.x + BLUR_RADIUS, dispatchThreadID.y)].rgb;
    }

    CachedColor[groupThreadID.x + BLUR_RADIUS][groupThreadID.y] = texSceneColor[dispatchThreadID.xy].rgb;

    GroupMemoryBarrierWithGroupSync();

    //[unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {        
        color += CachedColor[groupThreadID.x + i][groupThreadID.y] * weight[i];
    }
#endif
#else    
#if 0
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i, dispatchThreadID.y)].rgb * weight[i];
    }
#else    
    for (int i = 0; i <= 2 * BLUR_RADIUS; i+=3)
    {        
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i, dispatchThreadID.y)].rgb * weight[i];
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i + 1, dispatchThreadID.y)].rgb * weight[i + 1];
        color += texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS + i + 2, dispatchThreadID.y)].rgb * weight[i + 2];
    }
#endif
#endif

    gTexOutput[dispatchThreadID.xy] = half4(color, 0.0);
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSPostprocessBlurYBlock(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    half3 color = 0.0;
#if USE_CACHE
#if CACHE_TEX_MIXED
    CachedColor[groupThreadID.x][groupThreadID.y] = texSceneColor[dispatchThreadID.xy].rgb;
    GroupMemoryBarrierWithGroupSync();

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
#else // !CACHE_TEX_MIXED
    if (groupThreadID.y < BLUR_RADIUS)
    {
        CachedColor[groupThreadID.y][groupThreadID.x] =
            texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS)].rgb;
    }

    if (groupThreadID.y >= BLOCK_SIZE - BLUR_RADIUS)
    {
        CachedColor[groupThreadID.y + 2 * BLUR_RADIUS][groupThreadID.x] =
            texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y + BLUR_RADIUS)].rgb;
    }

    CachedColor[groupThreadID.y + BLUR_RADIUS][groupThreadID.x] = texSceneColor[dispatchThreadID.xy].rgb;

    GroupMemoryBarrierWithGroupSync();

    //[unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {        
        color += CachedColor[groupThreadID.y + i][groupThreadID.x] * weight[i];
    }
#endif
#else
#if 0
    [unroll]
    for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
    {
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i)].rgb * weight[i];
    }
#else
    for (int i = 0; i <= 2 * BLUR_RADIUS; i+=3)
    {        
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i)].rgb * weight[i];
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i + 1)].rgb * weight[i + 1];
        color += texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y - BLUR_RADIUS + i + 2)].rgb * weight[i + 2];
    }
#endif
#endif
    
    gTexOutput[dispatchThreadID.xy] = half4(color, 0.0);
}