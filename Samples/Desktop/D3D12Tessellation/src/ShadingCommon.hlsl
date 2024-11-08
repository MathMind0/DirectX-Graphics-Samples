#define NUM_LIGHTS 3
#define SHADOW_DEPTH_BIAS 0.00005f
#define MINIMUM_PIXEL_PER_SEGMENT 512.0
#define MAXINUM_TESS_SEGMENT 8.0

struct LightState
{
    float3 position;
    float3 direction;
    float4 color;
    float4 falloff;
    float4x4 view;
    float4x4 projection;
};

cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 view;
    float4x4 projection;
    float4 screenSize; //xy - screen size, zw - reciprocal of xy.
    float4 sceneInfo; //x - Time, y - Maximum Height
    float4 ambientColor;
    bool sampleShadowMap;
    LightState lights[NUM_LIGHTS];
};

struct VSInput
{
    float3 position : POSITION;
    float4 matWorld0 : MATWORLD0;
    float4 matWorld1 : MATWORLD1;
    float4 matWorld2 : MATWORLD2;
};

struct VSOutput
{
    float3 position : POSITION;
    float3x4 matWorld : MATWORLD;
};

struct HSOutput
{
    float3 position : POSITION;
};

struct PatchOutput
{
    float tessEdge[4] : SV_TessFactor;
    float tessInside[2] : SV_InsideTessFactor;
    float4x4 matMVP : MATMVP;
    float3x4 matWorld : MATWORLD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldpos : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};
