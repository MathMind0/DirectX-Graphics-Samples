#define BLUR_RADIUS 4
static const half weight[] = {0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002};

#define BLOCK_SIZE 16
#define USE_AUTO_UNROLL 1

Texture2D texSceneColor : register(t0);
RWTexture2D<half4> gTexOutput : register(u0);

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSPostprocessBlurXBlock(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    half3 color = 0.0;
#if USE_AUTO_UNROLL
    //[unroll]
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

    gTexOutput[dispatchThreadID.xy] = half4(color, 0.0);
}

[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSPostprocessBlurYBlock(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    half3 color = 0.0;
    
#if USE_AUTO_UNROLL
    //[unroll]
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
    
    gTexOutput[dispatchThreadID.xy] = half4(color, 0.0);
}