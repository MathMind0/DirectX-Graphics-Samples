#include "ShadingCommon.hlsl"

VSOutput VSMain(VSInput input)
{
    VSOutput result = (VSOutput)0;

    result.position = input.position;
    result.matWorld[0] = input.matWorld0;
    result.matWorld[1] = input.matWorld1;
    result.matWorld[2] = input.matWorld2;
    
    return result;
}
