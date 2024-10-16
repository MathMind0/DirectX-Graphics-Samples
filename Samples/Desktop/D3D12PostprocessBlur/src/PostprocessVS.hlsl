float4 VSPostprocess(uint vertexID : SV_VertexID) : SV_POSITION
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
