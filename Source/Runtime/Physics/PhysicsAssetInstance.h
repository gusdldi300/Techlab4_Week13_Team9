#pragma once

#include "Physics/BodyInstance.h"
#include "Physics/RagdollStats.h"
#include "Math/Transform.h"
#include "Physics/PhysicsGrabHandle.h"
#include "Collision/Ray.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UPhysicsAsset;
class FLineBatcher;
namespace physx { class PxD6Joint; }

struct FPhysicsBodyHit
{
    int32 Bone = -1;
    ECollisionShape Type = ECollisionShape::Unknown;
    int32 ShapeIndex = -1;
    FVector Position = FVector::ZeroVector;
    float Distance = 0;
};

// One component's runtime bodies/joints. Shared Physics Asset data is read only during Build.
class FPhysicsAssetInstance
{
public:
    FPhysicsAssetInstance() = default;
    FPhysicsAssetInstance(const FPhysicsAssetInstance&) = delete;
    FPhysicsAssetInstance& operator=(const FPhysicsAssetInstance&) = delete;
    ~FPhysicsAssetInstance();
    // An explicit scene lets asset editors use the same instance in an isolated preview simulation.
    bool Build(USkeletalMeshComponent& Mesh, FPhysicsScene* SceneOverride = nullptr);
    void Clear();
    bool IsSimulating() const { return bBuilt && bSimulating; }
    bool IsValidFor(const USkeletalMeshComponent& Mesh) const { return Matches(Mesh); }
    bool SyncFromAnimation(USkeletalMeshComponent& Mesh, bool bTeleport = false);
    bool SetSimulating(USkeletalMeshComponent& Mesh, bool bEnable);
    void ApplyToMesh(USkeletalMeshComponent& Mesh);
    void DrawConstraints(FLineBatcher& Lines) const;
    FRagdollDebugStats GetDebugStats() const;
    bool GetCenterOfMass(int32 Bone, FVector& WorldPosition, float& MassKg) const;
    // Uses live PhysX geometry. Only dynamic bodies of this instance are eligible.
    bool RaycastBodies(const FRay& Ray, FPhysicsBodyHit& OutHit) const;
    bool BeginGrab(const FRay& Ray, FPhysicsBodyHit& OutHit, const FPhysicsGrabSettings& Settings = {});
    bool UpdateGrabTarget(const FVector& Target) { return GrabHandle.SetTarget(Target); }
    void EndGrab() { GrabHandle.Release(); }
    bool IsGrabbing() const { return GrabHandle.IsGrabbing(); }
    FVector GetGrabPoint() const { return GrabHandle.GetGrabPoint(); }
    FVector GetGrabTarget() const { return GrabHandle.GetTarget(); }
    void SetGrabSettings(const FPhysicsGrabSettings& Settings) { GrabHandle.SetSettings(Settings); }

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
    FPhysicsGrabHandle GrabHandle;
};
