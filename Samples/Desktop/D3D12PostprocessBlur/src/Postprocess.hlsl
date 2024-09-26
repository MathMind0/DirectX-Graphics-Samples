#define BLUR_RADIUS 5
static const float weight[BLUR_RADIUS] = {0.3829, 0.2417, 0.0606, 0.0060, 0.0002};

struct ScreenInfo
{
    uint4 size; //x = viewport width, y = viewport height.
};

Texture2D texSceneColor : register(t0);
ConstantBuffer<ScreenInfo> screenInfo : register(b0);

float4 VSPostprocess(uint vertexID : SV_VertexID)
{
    switch (vertexID)
    {
    case 0:
        return float4(-1.0, -1.0, 1.0, 1.0);
    case 1:
        return float4(-1.0, 3.0, 1.0, 1.0);
    case 2:
        return float4(3.0, -1.0, 1.0, 1.0);
    default:
        return 0.0;
    }  
}

float4 PSPostprocessBlurNaive(float4 screenPos : SV_Position) : SV_Target
{
    int2 screenPosI = floor(screenPos.xy);
    
    float4 color = 0.0;
    [unroll]
    for (int i = -BLUR_RADIUS + 1; i < BLUR_RADIUS; i++)
    {
        int row = clamp(screenPosI.y + i, 0, screenInfo.size.y - 1);
        
        for (int j = -BLUR_RADIUS + 1; j < BLUR_RADIUS; j++)
        {
            int col = clamp(screenPosI.x + j, 0, screenInfo.size.x - 1);
            color += texSceneColor.Load(int2(col, row)) * weight[i] * weight[j];
        }
    }

    return color;
}