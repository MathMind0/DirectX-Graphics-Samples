#define BLUR_RADIUS 4
static const half weight[] = {0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002};

#define GROUP_SIZE 64
#define CACHE_SIZE ((GROUP_SIZE) + (2 * (BLUR_RADIUS)))
#define LINES 4

Texture2D texSceneColor : register(t0);
RWTexture2D<half4> gTexOutput : register(u0);

groupshared half3 CachedColor[CACHE_SIZE * LINES];

[numthreads(GROUP_SIZE, 1, 1)]
void CSPostprocessBlurXCombined(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    int row = 0;
    
    if (groupThreadID.x < BLUR_RADIUS)
    {
        //[unroll]
        for (row = 0; row < LINES; ++row)
        {
            CachedColor[groupThreadID.x + row * CACHE_SIZE] =
                texSceneColor[int2(dispatchThreadID.x - BLUR_RADIUS, dispatchThreadID.y * LINES + row)].rgb;
        }
    }

    if (groupThreadID.x >= GROUP_SIZE - BLUR_RADIUS)
    {
        //[unroll]
        for (row = 0; row < LINES; ++row)
        {
            CachedColor[groupThreadID.x + 2 * BLUR_RADIUS + row * CACHE_SIZE] =
                texSceneColor[int2(dispatchThreadID.x + BLUR_RADIUS, dispatchThreadID.y * LINES + row)].rgb;
        }
    }

    //[unroll]
    for (row = 0; row < LINES; ++row)
    {
        CachedColor[groupThreadID.x + BLUR_RADIUS + row * CACHE_SIZE] =
            texSceneColor[int2(dispatchThreadID.x, dispatchThreadID.y * LINES + row)].rgb;
    }

    GroupMemoryBarrierWithGroupSync();

    //[unroll]
    for (row = 0; row < LINES; ++row)
    {
        half3 color = 0.0;
        //[unroll]
        for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
        {        
            color += CachedColor[groupThreadID.x + i + row * CACHE_SIZE] * weight[i];
        }

        gTexOutput[int2(dispatchThreadID.x, dispatchThreadID.y * LINES + row)] = half4(color, 0.0);
    }
}

[numthreads(1, GROUP_SIZE, 1)]
void CSPostprocessBlurYCombined(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    int col = 0;
    if (groupThreadID.y < BLUR_RADIUS)
    {
        //[unroll]
        for (col = 0; col < LINES; ++col)
        {
            CachedColor[groupThreadID.y + col * CACHE_SIZE] =
                texSceneColor[int2(dispatchThreadID.x * LINES + col, dispatchThreadID.y - BLUR_RADIUS)].rgb;
        }
    }

    if (groupThreadID.y >= GROUP_SIZE - BLUR_RADIUS)
    {
        //[unroll]
        for (col = 0; col < LINES; ++col)
        {
            CachedColor[groupThreadID.y + 2 * BLUR_RADIUS + col * CACHE_SIZE] =
                texSceneColor[int2(dispatchThreadID.x * LINES + col, dispatchThreadID.y + BLUR_RADIUS)].rgb;
        }
    }

    //[unroll]
    for (col = 0; col < LINES; ++col)
    {
        CachedColor[groupThreadID.y + BLUR_RADIUS + col * CACHE_SIZE] =
            texSceneColor[int2(dispatchThreadID.x * LINES + col, dispatchThreadID.y)].rgb;
    }

    GroupMemoryBarrierWithGroupSync();

    //[unroll]
    for (col = 0; col < LINES; ++col)
    {
        half3 color = 0.0;
        //[unroll]
        for (int i = 0; i <= 2 * BLUR_RADIUS; i++)
        {        
            color += CachedColor[groupThreadID.y + i + col * CACHE_SIZE] * weight[i];
        }

        gTexOutput[int2(dispatchThreadID.x * LINES + col, dispatchThreadID.y)] = half4(color, 0.0);
    }    
}