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

// 양수는 실제 거리(m), 0은 배경, -1은 잘못된 깊이다.
static const float DOFBackgroundDepth = 0.0f;
static const float DOFInvalidDepth = -1.0f;

bool IsValidDOFDepth(float LinearDepth)
{
    return isfinite(LinearDepth) && LinearDepth >= DOFBackgroundDepth;
}

bool IsBackgroundDOFDepth(float LinearDepth)
{
    return LinearDepth == DOFBackgroundDepth;
}

// 반경은 절반 해상도의 세로 픽셀 단위로 보관한다.
float ConvertDOFRadiusFullToHalf(float FullRadius)
{
    return FullRadius * HalfSizeAndInvSize.y * FullSizeAndInvSize.w;
}

float ConvertDOFRadiusHalfToFull(float HalfRadius)
{
    return HalfRadius * FullSizeAndInvSize.y * HalfSizeAndInvSize.w;
}

// 원본 화면에서 원형인 반경을 같은 UV 영역으로 환산한다.
float2 ConvertDOFRadiusHalfToUV(float HalfRadius)
{
    return ConvertDOFRadiusHalfToFull(HalfRadius) * FullSizeAndInvSize.zw;
}

float ComputeDOFBlurAmount(float RadiusFullPixels)
{
    // 원본 화면의 픽셀 반경을 기준으로 전환한다.
    const float Transition = saturate((RadiusFullPixels - 0.5f) / 1.5f);
    return Transition * Transition * (3.0f - 2.0f * Transition);
}

#endif
