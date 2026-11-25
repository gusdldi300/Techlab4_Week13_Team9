#ifndef DOF_APERTURE_HLSLI
#define DOF_APERTURE_HLSLI

#include "DepthOfFieldCommon.hlsli"

static const float DOFPi = 3.14159265358979323846f;

struct FDOFAperture
{
    float BladeCount;
    float Roundness;
    float Apothem;
    float UnitArea;
};

FDOFAperture MakeDOFAperture()
{
    FDOFAperture Aperture;
    Aperture.BladeCount = clamp(round(ApertureParameters.y), 3.0f, 16.0f);
    const float OpeningFraction = saturate(ApertureParameters.x / LensParameters.w);
    // 최대 개방은 원형, 조일수록 날 수에 따른 다각형으로 전이한다.
    Aperture.Roundness = OpeningFraction * OpeningFraction;
    const float HalfSector = DOFPi / Aperture.BladeCount;
    Aperture.Apothem = cos(HalfSector);
    const float PolygonWeight = 1.0f - Aperture.Roundness;
    const float RoundWeight = Aperture.Roundness;
    const float SecantIntegral = log((1.0f + sin(HalfSector)) / Aperture.Apothem);
    // 반경 함수의 제곱을 각도에 대해 적분한 실제 커널 면적이다.
    Aperture.UnitArea = Aperture.BladeCount * (PolygonWeight * PolygonWeight * Aperture.Apothem * Aperture.Apothem * tan(HalfSector) + 2.0f * PolygonWeight * RoundWeight * Aperture.Apothem * SecantIntegral + RoundWeight * RoundWeight * HalfSector);
    return Aperture;
}

float GetDOFApertureBoundary(FDOFAperture Aperture, float Angle)
{
    const float Sector = 2.0f * DOFPi / Aperture.BladeCount;
    const float HalfSector = Sector * 0.5f;
    const float FoldedAngle = Angle - Sector * floor((Angle + HalfSector) / Sector);
    const float PolygonRadius = Aperture.Apothem / cos(FoldedAngle);
    return lerp(PolygonRadius, 1.0f, Aperture.Roundness);
}

float GetDOFApertureCoverage(FDOFAperture Aperture, float2 OffsetFullPixels, float RadiusFullPixels, float2 SourceCellSizeFullPixels)
{
    const float Distance = length(OffsetFullPixels);
    float Angle = 0.0f;
    if (Distance > 0.0f)
        Angle = atan2(OffsetFullPixels.y, OffsetFullPixels.x);

    const float Boundary = GetDOFApertureBoundary(Aperture, Angle) * RadiusFullPixels;
    // 거친 피라미드 셀의 면적을 경계 필터 폭에 반영한다.
    const float FilterWidth = max(1.0f, 0.5f * length(SourceCellSizeFullPixels));
    return saturate(0.5f + (Boundary - Distance) / FilterWidth);
}

float4 PackDOFBokeh(float4 AccumulatedColor)
{
    if (!all(isfinite(AccumulatedColor)) || AccumulatedColor.a <= 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    const float Coverage = saturate(AccumulatedColor.a);
    // 커버리지 포화 시 색상도 같은 비율로 조정한다.
    const float3 PremultipliedColor = AccumulatedColor.rgb * (Coverage / AccumulatedColor.a);
    return float4(clamp(PremultipliedColor, -65504.0f, 65504.0f), Coverage);
}

#endif
