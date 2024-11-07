#include "ShadingCommon.hlsl"

[domain("quad")]
PSInput DSMain(PatchOutput patch, float2 uv : SV_DomainLocation, const OutputPatch<HSOutput, 4> quad)
{
    PSInput output;

    output.uv = uv;
    
    float3 v0 = lerp(qu)
    output.position = 

    return output;
}
