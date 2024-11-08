#include "ShadingCommon.hlsl"

PatchOutput ConstantHS(InputPatch<VSOutput, 4> patch)
{
    PatchOutput output = (PatchOutput)0;

    output.matWorld = patch[0].matWorld;
    
    float4x4 model = {patch[0].matWorld, 0.0, 0.0, 0.0, 0.1};
         
    float4x4 matMVP = mul(transpose(model), mul(view, projection));
    output.matMVP = matMVP;
    
#if 1    
    float4 screenPos[4];
    int i = 0;
    for (i = 0; i < 4; ++i)
    {
        screenPos[i] = mul(float4(patch[i].position, 1.0), matMVP);
        screenPos[i].xyz /= screenPos[i].w;
    }

    float2 screenDelta = abs(screenPos[2].xy - screenPos[0].xy) * screenSize.xy;
    float numPixel = length(screenDelta);
    output.tessEdge[0] = max(0.0, min(numPixel / MINIMUM_PIXEL_PER_SEGMENT, MAXINUM_TESS_SEGMENT));

    screenDelta = abs(screenPos[0].xy - screenPos[1].xy) * screenSize.xy;
    numPixel = length(screenDelta);
    output.tessEdge[1] = max(0.0, min(numPixel / MINIMUM_PIXEL_PER_SEGMENT, MAXINUM_TESS_SEGMENT));
    
    screenDelta = abs(screenPos[1].xy - screenPos[3].xy) * screenSize.xy;
    numPixel = length(screenDelta);
    output.tessEdge[2] = max(0.0, min(numPixel / MINIMUM_PIXEL_PER_SEGMENT, MAXINUM_TESS_SEGMENT));

    screenDelta = abs(screenPos[3].xy - screenPos[2].xy) * screenSize.xy;
    numPixel = length(screenDelta);
    output.tessEdge[3] = max(0.0, min(numPixel / MINIMUM_PIXEL_PER_SEGMENT, MAXINUM_TESS_SEGMENT));

    output.tessInside[0] = 0.5 * (output.tessEdge[1] + output.tessEdge[3]);
    output.tessInside[1] = 0.5 * (output.tessEdge[0] + output.tessEdge[2]);
#endif
    
    //output.tessEdge[0] = output.tessEdge[1] = output.tessEdge[2] = output.tessEdge[3] = 0.0;
    //output.tessInside[0] = output.tessInside[1] = 0.0;
#if 0
    output.tessEdge[0] = 1;
    output.tessEdge[1] = 2;
    output.tessEdge[2] = 3;
    output.tessEdge[3] = 4;
    output.tessInside[0] = 1;
    output.tessInside[1] = 2;
#endif

    return output;
}

[domain("quad")]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(MAXINUM_TESS_SEGMENT)]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
HSOutput HSMain(InputPatch<VSOutput, 4> patch, uint i : SV_OutputControlPointID)
{
    HSOutput output = (HSOutput)0;

    output.position = patch[i].position;

    return output;
}


