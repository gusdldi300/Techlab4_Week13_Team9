#pragma once

#include "Physics/BodyInstance.h"
#include "Physics/RagdollStats.h"
#include "Math/Transform.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UPhysicsAsset;
class FLineBatcher;
namespace physx { class PxD6Joint; }

// One component's runtime bodies/joints. Shared Physics Asset data is read only during Build.
class FPhysicsAssetInstance
{
public:
    FPhysicsAssetInstance() = default;
    FPhysicsAssetInstance(const FPhysicsAssetInstance&) = delete;
    FPhysicsAssetInstance& operator=(const FPhysicsAssetInstance&) = delete;
    ~FPhysicsAssetInstance();
    bool Build(USkeletalMeshComponent& Mesh);
    void Clear();
    bool IsSimulating() const { return bBuilt && bSimulating; }
    bool IsValidFor(const USkeletalMeshComponent& Mesh) const { return Matches(Mesh); }
    bool SyncFromAnimation(USkeletalMeshComponent& Mesh, bool bTeleport = false);
    bool SetSimulating(USkeletalMeshComponent& Mesh, bool bEnable);
    void ApplyToMesh(USkeletalMeshComponent& Mesh);
    void DrawConstraints(FLineBatcher& Lines) const;
    FRagdollDebugStats GetDebugStats() const;

private:
    bool Matches(const USkeletalMeshComponent& Mesh) const;
    TArray<TUniquePtr<FBodyInstance>> Bodies;
    TArray<physx::PxD6Joint*> Joints;
    TArray<int32> BoneIndices;
    TArray<int32> BodyForBone;
    TArray<FMatrix> BoneInBody;
    TArray<uint8> KinematicBodies;
    const USkeletalMesh* SourceMesh = nullptr;
    const UPhysicsAsset* SourceAsset = nullptr;
    float DebugRadius = 0.02f;
    bool bBuilt = false;
    bool bSimulating = false;
    double LastKinematicSyncMs = 0;
    double LastPhysicsPoseMs = 0;
};
