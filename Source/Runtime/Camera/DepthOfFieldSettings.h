#pragma once

#include "UObject/Class.h"
#include "DepthOfFieldSettings.generated.h"

USTRUCT()
struct FDepthOfFieldSettings
{
    GENERATED_BODY()

    // 카메라의 사용 의도로, 뷰포트 Show Flag와 별개다.
    UPROPERTY(EditAnywhere)
    bool bEnabled = false;

    UPROPERTY(EditAnywhere)
    float FStop = 2.8f;

    // Maximum Aperture: 최대 개방 시의 최소 F-number.
    UPROPERTY(EditAnywhere)
    float MinFStop = 1.4f;

    UPROPERTY(EditAnywhere)
    int32 ApertureBladeCount = 7;

    // 카메라 전방 축을 따라 측정하는 초점 거리(m).
    UPROPERTY(EditAnywhere)
    float FocalDistanceMeters = 10.0f;

    // 추가 거리 블러가 최대 반경의 50%에 도달하는 거리(km).
    // 0이면 추가 거리 블러를 사용하지 않는다.
    UPROPERTY(EditAnywhere)
    float DepthBlurKmFor50Percent = 0.0f;

    // 추가 거리 블러의 최대 반경을 원본 화면 높이 비율로 저장한다.
    UPROPERTY(EditAnywhere)
    float DepthBlurRadiusFractionOfHeight = 0.0f;
};
