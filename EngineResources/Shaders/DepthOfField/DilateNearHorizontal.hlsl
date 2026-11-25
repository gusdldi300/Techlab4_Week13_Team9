#define DOF_DILATE_HORIZONTAL 1
#include "DilateNearCommon.hlsli"

FDepthOfFieldVertexOutput mainVS(uint VertexID : SV_VertexID)
{
    return MakeDepthOfFieldVertex(VertexID);
}

float mainPS(FDepthOfFieldVertexOutput Input) : SV_Target0
{
    return ComputeNearDilation(Input);
}
