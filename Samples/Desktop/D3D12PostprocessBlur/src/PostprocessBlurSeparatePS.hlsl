#define BLUR_RADIUS 4
static const float weight[] = {0.0002, 0.0060, 0.0606, 0.2417, 0.3829, 0.2417, 0.0606, 0.0060, 0.0002};

struct ScreenInfo
{
    uint4 size; //x = viewport width, y = viewport height.
};

ConstantBuffer<ScreenInfo> screenInfo : register(b0);

Texture2D texSceneColor : register(t0);

float4 PSPostprocessBlurX(float4 screenPos : SV_Position) : SV_Target
{
    int2 screenPosI = floor(screenPos.xy);
    
    float4 color = 0.0;
    [unroll]
    for (int i = -BLUR_RADIUS; i <= BLUR_RADIUS; i++)
    {
        int col = clamp(screenPosI.y + i, 0, screenInfo.size.y - 1);
        color += texSceneColor.Load(int3(screenPosI.x, col, 0)) * weight[i + BLUR_RADIUS];
    }

    return color;
}

float4 PSPostprocessBlurY(float4 screenPos : SV_Position) : SV_Target
{
    int2 screenPosI = floor(screenPos.xy);
    
    float4 color = 0.0;
    [unroll]
    for (int i = -BLUR_RADIUS; i <= BLUR_RADIUS; i++)
    {
        int row = clamp(screenPosI.x + i, 0, screenInfo.size.x - 1);
        color += texSceneColor.Load(int3(row, screenPosI.y, 0)) * weight[i + BLUR_RADIUS];
    }

    return color;
}