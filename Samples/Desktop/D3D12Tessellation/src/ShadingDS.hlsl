#include "ShadingCommon.hlsl"

[domain("quad")]
PSInput DSMain(PatchOutput patch, float2 uv : SV_DomainLocation, const OutputPatch<HSOutput, 4> quad)
{
    PSInput output;

    output.uv = uv;
    
    float3 v0 = lerp(quad[0].position, quad[1].position, uv.x);
    float3 v1 = lerp(quad[2].position, quad[3].position, uv.x);
    float3 p = lerp(v0, v1, uv.y);

    //p.y = sin(p.x + p.z + sceneInfo.x);
    
    output.position = mul(float4(p, 1.0), patch.matMVP);
    output.worldpos = mul(patch.matWorld, float4(p, 1.0));

    float3 tangentX = {1.0, cos(p.x + p.z + sceneInfo.x), 0.0};
    tangentX = normalize(tangentX);
    float3 tangentZ = tangentX.zyx;
    float3 normal = cross(tangentZ, tangentX);
    
    output.tangent = tangentX;
    output.normal = normal;
    
    return output;
}
