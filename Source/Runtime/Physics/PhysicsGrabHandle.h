#pragma once

#include "Physics/PhysicsHandles.h"
#include "Math/Vector.h"
#include "Core/Types.h"

namespace physx { class PxD6Joint; class PxRigidDynamic; }

struct FPhysicsGrabSettings
{
    // Mass-normalized spring: stiffness is in s^-2, damping in s^-1, independent of preview length units.
    float Stiffness = 8000.0f;
    float Damping = 40.0f;
};

// The caller owns the grabbed body/scene and must Release before either is destroyed.
// PhysicsAssetInstance owns this handle so Clear and SetSimulating(false) enforce that order.
class FPhysicsGrabHandle
{
public:
    FPhysicsGrabHandle() = default;
    FPhysicsGrabHandle(const FPhysicsGrabHandle&) = delete;
    FPhysicsGrabHandle& operator=(const FPhysicsGrabHandle&) = delete;
    ~FPhysicsGrabHandle();

    bool Grab(const FPhysicsActorHandle& Body, const FVector& WorldPoint, const FPhysicsGrabSettings& Settings,
        float SupportedMass = 0);
    void Release();
    bool IsGrabbing() const { return Joint != nullptr; }
    bool SetTarget(const FVector& WorldPoint);
    void SetSettings(const FPhysicsGrabSettings& Settings);
    FVector GetGrabPoint() const;
    const FVector& GetTarget() const { return Target; }

private:
    physx::PxD6Joint* Joint = nullptr;
    physx::PxRigidDynamic* TargetActor = nullptr;
    physx::PxRigidDynamic* GrabbedActor = nullptr;
    FVector LocalPoint = FVector::ZeroVector;
    FVector Target = FVector::ZeroVector;
    float DriveMass = 1;
    float ForceLimit = 0;
    uint32 SavedPositionIterations = 0;
    uint32 SavedVelocityIterations = 0;
};
