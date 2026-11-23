#ifndef DOF_DILATE_NEAR_COMMON_HLSLI
#define DOF_DILATE_NEAR_COMMON_HLSLI

#include "DepthOfFieldCommon.hlsli"

#if DOF_DILATE_HORIZONTAL
Texture2D<float2> SourceNearParamsTexture : register(t0);
#else
Texture2D<float> SourceNearRadiusTexture : register(t0);
#endif

float ReadNearRadius(int2 Pixel)
{
#if DOF_DILATE_HORIZONTAL
    const float2 Params = SourceNearParamsTexture.Load(int3(Pixel, 0));
    return IsValidDOFDepth(Params.y) && isfinite(Params.x) ? max(Params.x, 0.0f) : 0.0f;
#else
    const float Radius = SourceNearRadiusTexture.Load(int3(Pixel, 0));
    return isfinite(Radius) ? max(Radius, 0.0f) : 0.0f;
#endif
}

float ComputeNearDilation(FDepthOfFieldVertexOutput Input)
{
    const int2 HalfSize = int2(HalfSizeAndInvSize.xy);
    const int2 Pixel = int2(Input.Position.xy);
#if DOF_DILATE_HORIZONTAL
    const int Axis = 0;
    const float AxisScale = HalfSizeAndInvSize.x * FullSizeAndInvSize.z;
#else
    const int Axis = 1;
    const float AxisScale = HalfSizeAndInvSize.y * FullSizeAndInvSize.w;
#endif
    const int SearchRadius = int(ceil(DepthBlurParameters.z * AxisScale)) + 1;
    const int FirstPixel = max(Pixel[Axis] - SearchRadius, 0);
    const int LastPixel = min(Pixel[Axis] + SearchRadius, HalfSize[Axis] - 1);
    float MaximumRadius = 0.0f;

    [loop]
    for (int Coordinate = FirstPixel; Coordinate <= LastPixel; ++Coordinate)
    {
        int2 SourcePixel = Pixel;
        SourcePixel[Axis] = Coordinate;
        MaximumRadius = max(MaximumRadius, ReadNearRadius(SourcePixel));
    }

#if DOF_DILATE_HORIZONTAL
    return MaximumRadius;
#else
    // 한 픽셀 여유는 탐색에만 사용하며 실제 보케 반경을 키우지 않는다.
    return MaximumRadius > 0.0f ? MaximumRadius + 1.0f : 0.0f;
#endif
}

#endif
