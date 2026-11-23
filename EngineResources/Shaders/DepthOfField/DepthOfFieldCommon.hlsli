#ifndef DEPTH_OF_FIELD_COMMON_HLSLI
#define DEPTH_OF_FIELD_COMMON_HLSLI

#pragma pack_matrix(row_major)

cbuffer DepthOfFieldConstants : register(b0)
{
    float4x4 InverseProjection;
    float4 FullSizeAndInvSize;
    float4 HalfSizeAndInvSize;
    float4 LensParameters;
    float4 ApertureParameters;
    float4 DepthBlurParameters;
};

SamplerState PointClampSampler : register(s0);
SamplerState LinearClampSampler : register(s1);

struct FDepthOfFieldVertexOutput
{
    float4 Position : SV_Position;
};

FDepthOfFieldVertexOutput MakeDepthOfFieldVertex(uint VertexID)
{
    static const float2 Positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    FDepthOfFieldVertexOutput Output;
    Output.Position = float4(Positions[VertexID], 0.0f, 1.0f);
    return Output;
}

#endif
