#include "EnginePCH.h"
#include "Physics/PhysicsGrabHandle.h"
#include "Physics/PhysXConversions.h"
#include <algorithm>
#include <cmath>

using namespace physx;

namespace
{
    bool ValidSettings(const FPhysicsGrabSettings& Settings)
    {
        return std::isfinite(Settings.Stiffness) && Settings.Stiffness > 0 && Settings.Stiffness <= 10000 &&
            std::isfinite(Settings.Damping) && Settings.Damping >= 0 && Settings.Damping <= 500;
    }

    void ConfigureDrive(PxD6Joint& Joint, const FPhysicsGrabSettings& Settings, float Mass, float ForceLimit)
    {
        // A hand supports the connected ragdoll, not just its own mass. Force drives let us account for
        // that load explicitly; acceleration drives only see the two actors of this temporary joint.
        const PxD6JointDrive Drive(Settings.Stiffness * Mass, Settings.Damping * Mass, ForceLimit, false);
        Joint.setDrive(PxD6Drive::eX, Drive);
        Joint.setDrive(PxD6Drive::eY, Drive);
        Joint.setDrive(PxD6Drive::eZ, Drive);
    }
}

FPhysicsGrabHandle::~FPhysicsGrabHandle() { Release(); }

bool FPhysicsGrabHandle::Grab(const FPhysicsActorHandle& Body, const FVector& WorldPoint,
    const FPhysicsGrabSettings& Settings, float SupportedMass)
{
    Release();
    auto* Dynamic = Body.Actor ? Body.Actor->is<PxRigidDynamic>() : nullptr;
    if (!Dynamic || !Dynamic->getScene() || !U2PVector(WorldPoint).isFinite() || !ValidSettings(Settings)) return false;
    PxSceneWriteLock Lock(*Dynamic->getScene());
    if (Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC) return false;

    PxPhysics& Physics = Dynamic->getScene()->getPhysics();
    TargetActor = Physics.createRigidDynamic(PxTransform(U2PVector(WorldPoint)));
    if (!TargetActor) return false;
    TargetActor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
    // No shapes: the target only supplies a moving joint frame and never collides or participates in picking.
    Dynamic->getScene()->addActor(*TargetActor);
    const PxTransform LocalFrame = Dynamic->getGlobalPose().getInverse() * PxTransform(U2PVector(WorldPoint));
    Joint = PxD6JointCreate(Physics, TargetActor, PxTransform(PxIdentity), Dynamic, LocalFrame);
    if (!Joint)
    {
        TargetActor->release();
        TargetActor = nullptr;
        return false;
    }
    for (const auto Axis : {PxD6Axis::eX, PxD6Axis::eY, PxD6Axis::eZ,
        PxD6Axis::eTWIST, PxD6Axis::eSWING1, PxD6Axis::eSWING2})
        Joint->setMotion(Axis, PxD6Motion::eFREE);
    DriveMass = std::isfinite(SupportedMass) && SupportedMass > 0 ? std::max(SupportedMass, Dynamic->getMass()) : Dynamic->getMass();
    const float Gravity = Dynamic->getScene()->getGravity().magnitude();
    ForceLimit = Gravity > 0 ? DriveMass * Gravity * 10.0f : PX_MAX_F32;
    ConfigureDrive(*Joint, Settings, DriveMass, ForceLimit);
    Joint->setDrivePosition(PxTransform(PxIdentity));
    Joint->setConstraintFlag(PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES, true);
    GrabbedActor = Dynamic;
    Dynamic->getSolverIterationCounts(SavedPositionIterations, SavedVelocityIterations);
    // PhysX uses the island's maximum iteration count. The extra grab constraint benefits the whole chain.
    Dynamic->setSolverIterationCounts(std::max(SavedPositionIterations, 32u), std::max(SavedVelocityIterations, 8u));
    LocalPoint = P2UVector(LocalFrame.p);
    Target = WorldPoint;
    Dynamic->wakeUp();
    return true;
}

void FPhysicsGrabHandle::Release()
{
    const auto Clear = [&]() {
        if (GrabbedActor) GrabbedActor->setSolverIterationCounts(SavedPositionIterations, SavedVelocityIterations);
        if (Joint) Joint->release();
        if (TargetActor) TargetActor->release();
        Joint = nullptr;
        TargetActor = GrabbedActor = nullptr;
        LocalPoint = Target = FVector::ZeroVector;
    };
    if (TargetActor && TargetActor->getScene()) { PxSceneWriteLock Lock(*TargetActor->getScene()); Clear(); }
    else Clear();
}

bool FPhysicsGrabHandle::SetTarget(const FVector& WorldPoint)
{
    if (!Joint || !U2PVector(WorldPoint).isFinite()) return false;
    PxSceneWriteLock Lock(*TargetActor->getScene());
    Target = WorldPoint;
    TargetActor->setKinematicTarget(PxTransform(U2PVector(Target)));
    GrabbedActor->wakeUp();
    return true;
}

void FPhysicsGrabHandle::SetSettings(const FPhysicsGrabSettings& Settings)
{
    if (!Joint || !ValidSettings(Settings)) return;
    PxSceneWriteLock Lock(*TargetActor->getScene());
    ConfigureDrive(*Joint, Settings, DriveMass, ForceLimit);
    GrabbedActor->wakeUp();
}

FVector FPhysicsGrabHandle::GetGrabPoint() const
{
    if (!Joint) return FVector::ZeroVector;
    PxSceneReadLock Lock(*GrabbedActor->getScene());
    return P2UVector(GrabbedActor->getGlobalPose().transform(U2PVector(LocalPoint)));
}
