#pragma once

#include "Physics/BodyInstance.h"
#include "Core/EngineString.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"

class USkeletalMeshComponent;
class FLineBatcher;

namespace physx
{
    class PxD6Joint;
}

class FHardcodedRagdoll
{
public:
    FHardcodedRagdoll() = default;
    ~FHardcodedRagdoll();

    FHardcodedRagdoll(const FHardcodedRagdoll&) = delete;
    FHardcodedRagdoll& operator=(const FHardcodedRagdoll&) = delete;

    bool Build(USkeletalMeshComponent& Mesh, const FString& UpperArmBone, const FString& ForeArmBone, const FString& HandBone);

    void Clear();
    void DrawDebug(FLineBatcher& Lines) const;

    void ApplyToMesh(USkeletalMeshComponent& Mesh);

private:
    // [0] 위팔, [1] 아래팔
    FBodyInstance Bodies[2];

    // 이번 예제만을 위한 별도 바닥
    FBodyInstance TestFloor;

    physx::PxD6Joint* ElbowJoint = nullptr;
    physx::PxD6Joint* ShoulderJoint = nullptr;

    // 디버그 박스를 그릴 때 사용
    FVector HalfExtents[2];
    FVector FloorHalfExtent;

    bool bBuilt = false;

    // Bodies[0], Bodies[1]에 대응하는 본 인덱스
    int32 BoneIndices[2] = { -1, -1 };

    // 각 본을 해당 바디 기준으로 표현한 변환
    FMatrix BoneInBody[2];

    // 물리 바디가 없는 본은 시작 시점의 로컬 자세를 유지한다.
    TArray<FTransform> InitialLocalPose;
};