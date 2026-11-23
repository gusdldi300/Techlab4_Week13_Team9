#pragma once

#include "Camera/CameraLensSettings.h"
#include "Camera/DepthOfFieldSettings.h"
#include <algorithm>
#include <cmath>

inline void SanitizeFloat(float& Value, float Minimum, float Maximum, float Fallback)
{
    if (!std::isfinite(Value))
        Value = Fallback;

    Value = std::clamp(Value, Minimum, Maximum);
}

inline void SanitizeDepthOfFieldSettings(FDepthOfFieldSettings& Settings)
{
    SanitizeFloat(Settings.MinFStop, 0.7f, 64.0f, 1.4f);
    // 현재 조리개는 렌즈의 최대 개방보다 더 열릴 수 없다.
    SanitizeFloat(Settings.FStop, Settings.MinFStop, 64.0f, 2.8f);
    Settings.ApertureBladeCount = std::clamp(Settings.ApertureBladeCount, 3, 16);
    SanitizeFloat(Settings.FocalDistanceMeters, 0.001f, 1000000.0f, 10.0f);
    SanitizeFloat(Settings.DepthBlurKmFor50Percent, 0.0f, 1000.0f, 0.0f);
    SanitizeFloat(Settings.DepthBlurRadiusFractionOfHeight, 0.0f, 0.25f, 0.0f);
}

// 전환·FOV 효과 적용 전에 각 카메라의 기준 화각을 결정한다.
inline void ResolveCameraLens(FCameraLensSettings& Lens, float& VerticalFovDegrees)
{
    SanitizeFloat(Lens.SensorHeightMm, 1.0f, 100.0f, 24.0f);

    if (Lens.bUseFocalLength)
    {
        const float MinimumFocalLengthMm = Lens.ResolveFocalLength(179.0f);
        const float MaximumFocalLengthMm = Lens.ResolveFocalLength(1.0f);
        const float DefaultFocalLengthMm = Lens.ResolveFocalLength(60.0f);
        SanitizeFloat(Lens.FocalLengthMm, MinimumFocalLengthMm, MaximumFocalLengthMm, DefaultFocalLengthMm);
        VerticalFovDegrees = Lens.ResolveVerticalFov(VerticalFovDegrees);
    }

    SanitizeFloat(VerticalFovDegrees, 1.0f, 179.0f, 60.0f);
    Lens.FocalLengthMm = Lens.ResolveFocalLength(VerticalFovDegrees);
}

// 최종 화각을 유지하면서 렌더용 렌즈와 초점 거리를 확정한다.
inline void FinalizeCameraOptics(FCameraLensSettings& Lens, FDepthOfFieldSettings& DepthOfField, float& FinalFovDegrees)
{
    SanitizeFloat(Lens.SensorHeightMm, 1.0f, 100.0f, 24.0f);
    SanitizeFloat(FinalFovDegrees, 1.0f, 179.0f, 60.0f);
    Lens.FocalLengthMm = Lens.ResolveFocalLength(FinalFovDegrees);

    const float FocalLengthMeters = Lens.FocalLengthMm * 0.001f;
    const float MinimumFocusDistanceMeters = FocalLengthMeters + 0.001f;
    SanitizeFloat(DepthOfField.FocalDistanceMeters, MinimumFocusDistanceMeters, 1000000.0f, 10.0f);
}

inline void BlendDepthOfFieldSettings(FDepthOfFieldSettings& Current, const FDepthOfFieldSettings& Target, float Alpha)
{
    SanitizeFloat(Alpha, 0.0f, 1.0f, 0.0f);

    // 전환용 사본만 보간하며 카메라 컴포넌트 원본은 유지한다.
    Current.FStop = FMath::Lerp(Current.FStop, Target.FStop, Alpha);
    Current.MinFStop = FMath::Lerp(Current.MinFStop, Target.MinFStop, Alpha);
    Current.FocalDistanceMeters = FMath::Lerp(Current.FocalDistanceMeters, Target.FocalDistanceMeters, Alpha);
    Current.DepthBlurRadiusFractionOfHeight = FMath::Lerp(Current.DepthBlurRadiusFractionOfHeight, Target.DepthBlurRadiusFractionOfHeight, Alpha);

    // 0km는 비활성화이므로 양쪽 모두 활성일 때만 거리를 보간한다.
    if (Current.DepthBlurKmFor50Percent > 0.0f && Target.DepthBlurKmFor50Percent > 0.0f)
    {
        Current.DepthBlurKmFor50Percent = FMath::Lerp(Current.DepthBlurKmFor50Percent, Target.DepthBlurKmFor50Percent, Alpha);
    }
    else if (Alpha >= 0.5f)
    {
        Current.DepthBlurKmFor50Percent = Target.DepthBlurKmFor50Percent;
    }

    // 불연속 설정은 전환 중간에 대상 값으로 교체한다.
    if (Alpha >= 0.5f)
    {
        Current.bEnabled = Target.bEnabled;
        Current.ApertureBladeCount = Target.ApertureBladeCount;
    }

    SanitizeDepthOfFieldSettings(Current);
}
