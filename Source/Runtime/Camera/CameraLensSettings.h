#pragma once

#include "UObject/Class.h"
#include "Math/EngineMath.h"
#include <cmath>
#include "CameraLensSettings.generated.h"

USTRUCT()
struct FCameraLensSettings
{
    GENERATED_BODY()

    // false는 기존 세로 FOV, true는 렌즈 초점거리 기준이다.
    UPROPERTY(EditAnywhere)
    bool bUseFocalLength = false;

    UPROPERTY(EditAnywhere)
    float FocalLengthMm = 20.78461f;

    // 유효 센서의 가로 길이는 높이 * 실제 뷰 종횡비다.
    UPROPERTY(EditAnywhere)
    float SensorHeightMm = 24.0f;

    float ResolveVerticalFov(float ExistingFovDegrees) const
    {
        if (!bUseFocalLength)
            return ExistingFovDegrees;

        const float HalfAngle = std::atan(SensorHeightMm / (2.0f * FocalLengthMm));
        return FMath::RadiansToDegrees(2.0f * HalfAngle);
    }

    float ResolveFocalLength(float FinalFovDegrees) const
    {
        const float HalfAngle = FMath::DegreesToRadians(FinalFovDegrees) * 0.5f;
        return SensorHeightMm / (2.0f * std::tan(HalfAngle));
    }
};
