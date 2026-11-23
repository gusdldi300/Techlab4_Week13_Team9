#define DOF_NEAR_LAYER 0
#include "BokehCommon.hlsli"

FDepthOfFieldVertexOutput mainVS(uint VertexID : SV_VertexID)
{
    return MakeDepthOfFieldVertex(VertexID);
}

float4 mainPS(FDepthOfFieldVertexOutput Input) : SV_Target0
{
    return ComputeDOFBokeh(Input);
}
