#define DOF_DILATE_HORIZONTAL 0
#include "DilateNearCommon.hlsli"

FDepthOfFieldVertexOutput mainVS(uint VertexID : SV_VertexID)
{
    return MakeDepthOfFieldVertex(VertexID);
}

float mainPS(FDepthOfFieldVertexOutput Input) : SV_Target0
{
    return ComputeNearDilation(Input);
}
