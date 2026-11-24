#pragma once

#include "Physics/BodyInstance.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "Physics/RagdollStats.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class FLineBatcher;
namespace physx { class PxD6Joint; class PxAggregate; }

// Mixamo 계열 캐릭터의 전신 바디와 관절을 직접 지정하는 첫 래그돌 실험이다.
class FHardcodedRagdoll
{
public:
    FHardcodedRagdoll() = default;
    ~FHardcodedRagdoll();
    FHardcodedRagdoll(const FHardcodedRagdoll&) = delete;
    FHardcodedRagdoll& operator=(const FHardcodedRagdoll&) = delete;

    bool Build(USkeletalMeshComponent& Mesh);
    void Clear();
    void DrawConstraints(FLineBatcher& Lines) const;
    void ApplyToMesh(USkeletalMeshComponent& Mesh);

    bool IsSimulating() const { return bBuilt && bSimulating; }
    bool SyncFromAnimation(USkeletalMeshComponent& Mesh, bool bTeleport = false);
    bool SetSimulating(USkeletalMeshComponent& Mesh, bool bEnable);

    FRagdollDebugStats GetDebugStats() const;

private:
    static constexpr int32 BodyCount = 15;
    FBodyInstance Bodies[BodyCount];
    FBodyInstance TestFloor;
    physx::PxD6Joint* Joints[BodyCount - 1] = {};
    physx::PxAggregate* Aggregate = nullptr;
    FVector HalfExtents[BodyCount];
    FVector FloorHalfExtent;
    int32 BoneIndices[BodyCount] = {};
    FMatrix BoneInBody[BodyCount];
    TArray<FTransform> InitialLocalPose;
    const USkeletalMesh* SourceMesh = nullptr;
    float DebugRadius = 0.02f;
    bool bBuilt = false;
    bool bPoseErrorLogged = false;

    bool bSimulating = false;

    double LastKinematicSyncMs = 0.0;
    double LastPhysicsPoseMs = 0.0;
};
