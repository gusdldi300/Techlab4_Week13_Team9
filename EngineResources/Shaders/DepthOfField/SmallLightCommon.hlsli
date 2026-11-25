#ifndef DOF_SMALL_LIGHT_COMMON_HLSLI
#define DOF_SMALL_LIGHT_COMMON_HLSLI

#include "DepthOfFieldCommon.hlsli"

cbuffer SmallLightConstants : register(b1)
{
    // 최대 후보 수, 최대 지름(px), 최소 주변 대비, 최소 휘도 차이.
    float4 SmallLightParameters;
};

static const uint DOFSmallLightTileSize = 16;
static const uint DOFSmallLightWindowRadius = 32;
static const uint DOFSmallLightWindowSize = DOFSmallLightWindowRadius * 2 + 1;
static const uint DOFSmallLightWindowPixelCount =
    DOFSmallLightWindowSize * DOFSmallLightWindowSize;
static const uint DOFSmallLightSampleGridSize = 16;
static const uint DOFSmallLightPixelsPerRegion = 256;

struct FDOFSmallLightRegion
{
    uint4 Bounds; // 시작 X/Y, 너비/높이. 최대 32x32 원본 픽셀.
    float4 Parameters; // 추출할 픽셀의 최소 휘도, 나머지는 예약.
};

float GetSmallLightLuminance(float3 Color)
{
    return max(dot(Color, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
}

float GetSmallLightRadius(float2 CoCData)
{
    return min(max(abs(CoCData.x), max(CoCData.y, 0.0f)), DepthBlurParameters.z);
}

#endif
