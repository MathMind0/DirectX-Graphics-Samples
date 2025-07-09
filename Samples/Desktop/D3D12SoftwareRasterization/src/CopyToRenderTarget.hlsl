#define RootSig \
    "RootConstants(num32BitConstants=2, b0)," \
    "DescriptorTable(SRV(t0))," \
    "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT)"

cbuffer cbCanvas : register(b0)
{
    uint2 szCanvas;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[RootSignature(RootSig)]
VSOutput VSMain(uint vid : SV_VertexID)
{
    VSOutput o;
    switch (vid)
    {
    case 0:
        o.pos = float4(-1.0, -1.0, 0.5, 1.0);
        o.uv = float2(0.0, 1.0);
        break;

    case 1:
        o.pos = float4(-1.0, 3.0, 0.5, 1.0);
        o.uv = float2(0.0, -1.0);
        break;
        
    case 2:
        o.pos = float4(3.0, -1.0, 0.5, 1.0);
        o.uv = float2(2.0, 1.0);
        break;

    default:
        o.pos = float4(0.0, 0.0, 0.5, 1.0);
        o.uv = float2(0.0, 0.0);
        break;
    }

    return o;
}

Texture2D<uint64_t> Canvas : register(t0);
SamplerState PointClamp : register(s0);

[RootSignature(RootSig)]
void PSMain(VSOutput input, out float4 color : SV_Target)
{
#if 1
    int2 location = (int2)(input.uv * szCanvas);
    uint64_t value = Canvas.Load(int3(location, 0));
    uint iColor = uint(value);
    float r = (iColor & 0xFF) / 255.0;
    float g = ((iColor >> 8) & 0xFF) / 255.0;
    float b = ((iColor >> 16) & 0xFF) / 255.0;
    color = float4(r, g, b, 1.0);
#else
    color = float4(1.0, 0.0, 0.0, 1.0);
#endif
}