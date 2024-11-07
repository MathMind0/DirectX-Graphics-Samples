#include "ShadingCommon.hlsl"

PatchOutput ConstantHS(InputPatch<VSOutput, 4> patch)
{
    PatchOutput output;

    output.matWorld = patch[0].matWorld;
    
    float4x4 model = {patch[0].matWorld, 0.0, 0.0, 0.0, 0.1};
         
    float4x4 matMVP = mul(transpose(model), mul(view, projection));
    output.matMVP = matMVP;
    
    float4 screenPos[4];
    for (int i = 0; i < 4; ++i)
    {
        screenPos[i] = mul(patch[i].position, matMVP);
        screenPos[i].xyz /= screenPos[i].w;
    }

    for (int i = 0; i < 4; ++i)
    {
        float2 screenDelta = abs(screenPos[i].xy - screenPos[(i + 1) & 0x03].xy) * screenSize.xy;
        float numPixel = length(screenDelta);
        output.tessEdge[i] = min(numPixel / MINIMUM_PIXEL_PER_SEGMENT, MAXINUM_TESS_SEGMENT);
    }

    output.tessInside[0] = 0.5 * (output.tessEdge[1] + output.tessEdge[3]);
    output.tessInside[1] = 0.5 * (output.tessEdge[0] + output.tessEdge[2]);

    return output;
}

[domain("quad")]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(MAXINUM_TESS_SEGMENT)]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
HSOutput HSMain(InputPatch<VSOutput, 4> patch, uint i : SV_OutputControlPointID)
{
    HSOutput output;

    output.position = patch[i].position;

    return output;
}


