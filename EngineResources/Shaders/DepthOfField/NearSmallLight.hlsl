#define DOF_SMALL_LIGHT_NEAR 1
#include "ScatterSmallLightCommon.hlsli"

FSmallLightVertexOutput mainVS(uint VertexID : SV_VertexID, uint InstanceID : SV_InstanceID)
{
    return MakeSmallLightVertex(VertexID, InstanceID);
}

float4 mainPS(FSmallLightVertexOutput Input) : SV_Target0
{
    return ComputeSmallLightPixel(Input);
}
