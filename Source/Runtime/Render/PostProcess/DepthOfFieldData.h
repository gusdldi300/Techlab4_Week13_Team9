#pragma once

#include "Math/Matrix.h"
#include "Math/Vector4.h"
#include <cstddef>

struct alignas(16) FDepthOfFieldData
{
    // 깊이 버퍼의 위치를 카메라 공간으로 복원한다.
    FMatrix InverseProjection;

    // 너비, 높이, 너비의 역수, 높이의 역수.
    FVector4 FullSizeAndInvSize;
    FVector4 HalfSizeAndInvSize;

    // 렌즈 초점거리(m), 센서 높이(m), 초점 거리(m), F-Stop.
    FVector4 LensParameters;

    // 최소 F-Stop, 조리개 날 수, Near 샘플 수, Far 샘플 수.
    FVector4 ApertureParameters;

    // 50% 도달 거리(m), 추가 블러 반경(원본 px),
    // 처리 반경 한도(원본 px), 월드 거리의 m 환산 계수.
    FVector4 DepthBlurParameters;
};

static_assert(sizeof(FMatrix) == 64);
static_assert(sizeof(FVector4) == 16);
static_assert(offsetof(FDepthOfFieldData, FullSizeAndInvSize) == 64);
static_assert(offsetof(FDepthOfFieldData, HalfSizeAndInvSize) == 80);
static_assert(offsetof(FDepthOfFieldData, LensParameters) == 96);
static_assert(offsetof(FDepthOfFieldData, ApertureParameters) == 112);
static_assert(offsetof(FDepthOfFieldData, DepthBlurParameters) == 128);
static_assert(sizeof(FDepthOfFieldData) == 144);
