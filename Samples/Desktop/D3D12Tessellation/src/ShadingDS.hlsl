#include "ShadingCommon.hlsl"

[domain("quad")]
PSInput DSMain(PatchOutput patch, float2 uv : SV_DomainLocation, const OutputPatch<HSOutput, 4> quad)
{
    PSInput output;

    output.uv = uv;
    
    float3 v0 = lerp(quad[0].position, quad[1].position, uv.x);
    float3 v1 = lerp(quad[2].position, quad[3].position, uv.x);
    float3 p = lerp(v0, v1, uv.y);
    float3 wp = mul(patch.matWorld, float4(p, 1.0));
    
    p.y = WAVE_HEIGHT * sin(0.25 * (wp.x * wp.x + wp.z * wp.z) + 0.1 * sceneInfo.x);
    float c = cos(wp.x * wp.x + wp.z * wp.z + 0.1 * sceneInfo.x);
    
    float3 tangentX = {1.0, 2.0 * wp.x * c, 0.0};
    tangentX = normalize(tangentX);
    float3 tangentZ = {0.0, 2.0 * wp.z * c, 1.0};
    tangentZ = normalize(tangentZ);
    float3 normal = cross(tangentZ, tangentX);
    
    output.tangent = mul((float3x3)patch.matWorld, tangentX).xyz; 
    output.normal = mul((float3x3)patch.matWorld, normal).xyz;
    
    output.position = mul(float4(p, 1.0), patch.matMVP);
    output.worldpos = mul(patch.matWorld, float4(p, 1.0));
    
    return output;
}
