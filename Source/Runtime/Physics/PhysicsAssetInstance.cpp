#include "EnginePCH.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Animation/BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversions.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Physics/BodySetup.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include <algorithm>
#include <atomic>
#include <cmath>

using namespace physx;

bool FPhysicsAssetInstance::GetCenterOfMass(int32 Bone, FVector& WorldPosition, float& MassKg) const
{
    if (!bBuilt || Bone < 0 || Bone >= BodyForBone.Num()) return false;
    const int32 Index = BodyForBone[Bone];
    return Index >= 0 && Index < Bodies.Num() && Bodies[Index] && Bodies[Index]->GetCenterOfMass(WorldPosition, MassKg);
}

namespace
{
    FMatrix BodyPoseToMatrix(const PxTransform& Pose)
    {
        FTransform Transform;
        Transform.Location = P2UVector(Pose.p);
        Transform.Rotation = P2UQuat(Pose.q);
        return Transform.GetLocalMatrix();
    }

    bool MatrixToTransform(const FMatrix& Matrix, FTransform& Out)
    {
        const FVector X(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]);
        const FVector Y(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]);
        const FVector Z(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]);
        const FVector Scale(X.Size(), Y.Size(), Z.Size());
        if (!std::isfinite(Scale.X + Scale.Y + Scale.Z) || std::min({ Scale.X, Scale.Y, Scale.Z }) < 0.000001f) return false;
        const FVector NX = X / Scale.X;
        const FVector NY = Y / Scale.Y;
        const FVector NZ = Z / Scale.Z;
        if (std::abs(NX.Dot(NY)) > 0.001f || std::abs(NX.Dot(NZ)) > 0.001f || std::abs(NY.Dot(NZ)) > 0.001f || NX.Cross(NY).Dot(NZ) < 0.999f) return false;

        // 엔진은 행벡터, PhysX는 열벡터이므로 엔진의 행을 PhysX의 열로 옮긴다. 오일러 각을 거치지 않아 수직 자세에서도 회전이 유지된다.
        const PxQuat Rotation(PxMat33(U2PVector(NX), U2PVector(NY), U2PVector(NZ)));
        Out.Location = FVector(Matrix.M[3][0], Matrix.M[3][1], Matrix.M[3][2]);
        Out.Scale = Scale;
        Out.Rotation = P2UQuat(Rotation).Normalized();
        return Rotation.isFinite() && std::isfinite(Out.Location.X + Out.Location.Y + Out.Location.Z);
    }
}

FPhysicsAssetInstance::~FPhysicsAssetInstance() { Clear(); }

bool FPhysicsAssetInstance::Matches(const USkeletalMeshComponent& Mesh) const
{
    return bBuilt && Mesh.GetSkeletalMesh() == SourceMesh && SourceMesh->GetPhysicsAsset() == SourceAsset &&
        Mesh.GetBones().Num() == BodyForBone.Num();
}

bool FPhysicsAssetInstance::Build(USkeletalMeshComponent& Mesh, FPhysicsScene* SceneOverride)
{
    Clear();
    USkeletalMesh* SkeletalMesh = Mesh.GetSkeletalMesh();
    UPhysicsAsset* Asset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
    UWorld* World = Mesh.GetWorld();
    FPhysicsScene* Scene = SceneOverride ? SceneOverride : (World ? World->GetPhysicsScene() : nullptr);
    PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
    if (!Asset || !Scene || !Physics || Asset->BodySetups.IsEmpty()) return false;
    // Use the same validation as editor save/load, including orthogonal frames and valid limits.
    FString Text, Error;
    if (!PhysicsAssetFile::ToText(SkeletalMesh->GetPath(),*SkeletalMesh,*Asset,Text,Error))
    {
        KRS_LOG(Warning,"[PhysicsAsset] Invalid asset: {}",Error);
        return false;
    }
    Mesh.RefreshBoneTransformsNow();
    const auto& Bones = Mesh.GetBones();
    for (int I=0; I<Bones.Num(); ++I)
        if (Bones[I].ParentIndex < -1 || Bones[I].ParentIndex >= I) return false;
    TArray<FTransform> Poses;
    BodyForBone.SetNum(Bones.Num());
    for (int& Index : BodyForBone) Index = -1;
    for (int I=0; I<Asset->BodySetups.Num(); ++I)
    {
        const int Bone = Mesh.FindBoneIndex(Asset->BodySetups[I]->BoneName.ToString());
        FTransform Pose;
        if (!MatrixToTransform(Mesh.GetBoneWorldMatrix(Bone),Pose) ||
            std::abs(Pose.Scale.X-Pose.Scale.Y) > Pose.Scale.X*0.001f ||
            std::abs(Pose.Scale.X-Pose.Scale.Z) > Pose.Scale.X*0.001f)
        {
            KRS_LOG(Warning,"[PhysicsAsset] Positive uniform scale without shear is required: {}",Bones[Bone].Name);
            Clear();
            return false;
        }
        BoneIndices.Add(Bone);
        BodyForBone[Bone] = I;
        Poses.Add(Pose);
    }
    static std::atomic<uint32> NextGroup{1};
    const uint32 Group = NextGroup.fetch_add(1);
    for (int I=0; I<Asset->BodySetups.Num(); ++I)
    {
        const UBodySetup* Setup = Asset->BodySetups[I];
        FBodyInstanceInitParams Params;
        Params.WorldLocation = Poses[I].Location;
        Params.WorldRotation = Poses[I].Rotation;
        Params.Scale3D = Poses[I].Scale;
        Params.bSimulatePhysics = true; // Default bodies must also be switchable to dynamic.
        Bodies.Add(MakeUnique<FBodyInstance>());
        // No component owner: a bone actor must never move the entire component during scene sync.
        Bodies.Last()->InitBody(Setup,Params,nullptr,Scene);
        if (!Bodies.Last()->IsValidBodyInstance()) { Clear(); return false; }
        PxRigidActor* Actor = Bodies.Last()->GetActorHandle().Actor;
        if (!Actor->getScene()) { Clear(); return false; }
        if (Actor->getNbShapes() != static_cast<PxU32>(Setup->AggGeom.GetElementCount())) { Clear(); return false; }
        BoneInBody.Add(Mesh.GetBoneWorldMatrix(BoneIndices[I]) * BodyPoseToMatrix(Actor->getGlobalPose()).Inverse());
        KinematicBodies.Add(Setup->PhysicsType == EPhysicsType::Kinematic);
        PxSceneWriteLock Lock(*Actor->getScene());
        PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>();
        Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC,true);
        Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD,true);
        Dynamic->setLinearDamping(0.05f);
        Dynamic->setAngularDamping(0.3f);
        Dynamic->setSolverIterationCounts(16,4);
        TArray<PxShape*> Shapes;
        Shapes.SetNum(Actor->getNbShapes());
        Actor->getShapes(Shapes.GetData(),Shapes.Num());
        for (PxShape* Shape : Shapes)
            Shape->setSimulationFilterData(PxFilterData(Group,static_cast<PxU32>(I),0,FBodyInstance::AssetCollisionFilterTag));
    }
    for (const uint64 Pair : Asset->CollisionDisableTable)
    {
        const int A = static_cast<int>(Pair >> 32), B = static_cast<int>(static_cast<uint32>(Pair));
        Bodies[A]->DisableCollisionWith(Bodies[B].get());
        Bodies[B]->DisableCollisionWith(Bodies[A].get());
    }
    // Build all bodies first: array order need not follow the skeleton hierarchy.
    bool Success = true;
    PxScene* NativeScene = Bodies[0]->GetActorHandle().Actor->getScene();
    {
        PxSceneWriteLock Lock(*NativeScene);
        const auto Frame = [](const FVector& Position, const FVector& Primary, const FVector& Secondary, float Scale) {
            const PxVec3 X = U2PVector(Primary).getNormalized();
            const PxVec3 Z = X.cross(U2PVector(Secondary)).getNormalized();
            const PxVec3 Y = Z.cross(X);
            return PxTransform(U2PVector(Position*Scale),PxQuat(PxMat33(X,Y,Z)).getNormalized());
        };
        const auto Motion = [](EConstraintMotion M) {
            switch(M) { case EConstraintMotion::Free: return PxD6Motion::eFREE;
                case EConstraintMotion::Limited: return PxD6Motion::eLIMITED; default: return PxD6Motion::eLOCKED; }
        };
        for (const auto* Definition : Asset->PhysicsConstraint)
        {
            const int Child = BodyForBone[Mesh.FindBoneIndex(Definition->ConstraintBone1.ToString())];
            const int Parent = BodyForBone[Mesh.FindBoneIndex(Definition->ConstraintBone2.ToString())];
            // Actor 0 is child (frame 1), actor 1 is parent (frame 2), matching serialized data.
            auto* Joint = PxD6JointCreate(*Physics,Bodies[Child]->GetActorHandle().Actor,
                Frame(Definition->Pos1,Definition->PriAxis1,Definition->SecAxis1,Poses[Child].Scale.X),
                Bodies[Parent]->GetActorHandle().Actor,
                Frame(Definition->Pos2,Definition->PriAxis2,Definition->SecAxis2,Poses[Parent].Scale.X));
            if (!Joint) { Success=false; break; }
            Joints.Add(Joint);
            Joint->setMotion(PxD6Axis::eX,Motion(Definition->XMotion));
            Joint->setMotion(PxD6Axis::eY,Motion(Definition->YMotion));
            Joint->setMotion(PxD6Axis::eZ,Motion(Definition->ZMotion));
            Joint->setMotion(PxD6Axis::eSWING1,Motion(Definition->Swing1Motion));
            Joint->setMotion(PxD6Axis::eSWING2,Motion(Definition->Swing2Motion));
            Joint->setMotion(PxD6Axis::eTWIST,Motion(Definition->TwistMotion));
            if (Definition->XMotion == EConstraintMotion::Limited || Definition->YMotion == EConstraintMotion::Limited || Definition->ZMotion == EConstraintMotion::Limited)
                Joint->setLinearLimit(PxJointLinearLimit(Physics->getTolerancesScale(),Definition->LinearLimit*Poses[Parent].Scale.X));
            const auto Radians = [](float Degrees) { return Degrees*PxPi/180.0f; };
            if (Definition->Swing1Motion == EConstraintMotion::Limited || Definition->Swing2Motion == EConstraintMotion::Limited)
                Joint->setSwingLimit(PxJointLimitCone(
                    Radians(Definition->Swing1Motion == EConstraintMotion::Limited ? Definition->Swing1LimitDegrees : 45.0f),
                    Radians(Definition->Swing2Motion == EConstraintMotion::Limited ? Definition->Swing2LimitDegrees : 45.0f)));
            if (Definition->TwistMotion == EConstraintMotion::Limited)
            {
                const float Angle = Radians(Definition->TwistLimitDegrees);
                Joint->setTwistLimit(PxJointAngularLimitPair(-Angle,Angle));
            }
            Joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED,Asset->IsCollisionEnabled(Child,Parent));
            if (Definition->XMotion == EConstraintMotion::Locked && Definition->YMotion == EConstraintMotion::Locked &&
                Definition->ZMotion == EConstraintMotion::Locked)
            {
                // Contact and joint constraints may not converge in a finite
                // iteration budget. Bound visible separation for rigid anchors.
                Joint->setProjectionLinearTolerance(std::max(0.1f * Poses[Parent].Scale.X,0.00001f));
                Joint->setConstraintFlag(PxConstraintFlag::ePROJECTION,true);
            }
        }
    }
    if (!Success) { Clear(); return false; }
    SourceMesh = SkeletalMesh;
    SourceAsset = Asset;
    const FBox Bounds = Mesh.CalcBounds();
    DebugRadius = std::max((Bounds.Max-Bounds.Min).Size()*0.008f,0.005f);
    bBuilt = true;
    KRS_LOG(Info,"[PhysicsAsset] Created {} bodies and {} D6 joints",Bodies.Num(),Joints.Num());
    return true;
}

void FPhysicsAssetInstance::Clear()
{
    EndGrab();
    bBuilt = bSimulating = false;
    SourceMesh = nullptr; SourceAsset = nullptr;
    LastKinematicSyncMs = LastPhysicsPoseMs = 0;
    PxScene* Scene = Bodies.IsEmpty() || !Bodies[0]->IsValidBodyInstance() ? nullptr : Bodies[0]->GetActorHandle().Actor->getScene();
    const auto Release = [&]() {
        for (auto* Joint : Joints) Joint->release();
        Joints.Empty();
        Bodies.Empty();
    };
    if (Scene) { PxSceneWriteLock Lock(*Scene); Release(); }
    else Release();
    BoneIndices.Empty(); BodyForBone.Empty(); BoneInBody.Empty(); KinematicBodies.Empty();
}

bool FPhysicsAssetInstance::SyncFromAnimation(USkeletalMeshComponent& Mesh, bool bTeleport)
{
    LastKinematicSyncMs = 0;
    if (!Matches(Mesh)) { Clear(); return false; }
    FScopeCycleCounter Timer(RagdollStats::STAT_KinematicSync);
    TArray<PxTransform> Targets;
    Targets.SetNum(Bodies.Num());
    for (int I=0; I<Bodies.Num(); ++I)
    {
        if (bSimulating && !KinematicBodies[I]) continue;
        FTransform Pose;
        if (!MatrixToTransform(BoneInBody[I].Inverse()*Mesh.GetBoneWorldMatrix(BoneIndices[I]),Pose) ||
            std::abs(Pose.Scale.X-1)>0.001f || std::abs(Pose.Scale.Y-1)>0.001f || std::abs(Pose.Scale.Z-1)>0.001f)
            return false;
        Targets[I] = U2PTransform(Pose.Location,Pose.Rotation);
    }
    PxSceneWriteLock Lock(*Bodies[0]->GetActorHandle().Actor->getScene());
    for (int I=0; I<Bodies.Num(); ++I)
    {
        if (bSimulating && !KinematicBodies[I]) continue;
        auto* Dynamic = Bodies[I]->GetActorHandle().Actor->is<PxRigidDynamic>();
        if (bTeleport) Dynamic->setGlobalPose(Targets[I]);
        else Dynamic->setKinematicTarget(Targets[I]);
    }
    LastKinematicSyncMs = FPlatformTime::ToMilliseconds64(Timer.Finish());
    return true;
}

bool FPhysicsAssetInstance::SetSimulating(USkeletalMeshComponent& Mesh, bool bEnable, const FVector& InitialVelocity)
{
    if (bEnable && !U2PVector(InitialVelocity).isFinite()) return false;
    if (!bEnable) EndGrab();
    if (!Matches(Mesh)) { Clear(); return false; }
    if (bSimulating == bEnable) return true;
    if (bEnable)
    {
        if (std::none_of(KinematicBodies.begin(),KinematicBodies.end(),[](bool Kinematic){ return !Kinematic; })) return false;
        Mesh.RefreshBoneTransformsNow();
        if (!SyncFromAnimation(Mesh,true)) return false;
    }
    PxSceneWriteLock Lock(*Bodies[0]->GetActorHandle().Actor->getScene());
    for (int I=0; I<Bodies.Num(); ++I)
    {
        auto* Dynamic = Bodies[I]->GetActorHandle().Actor->is<PxRigidDynamic>();
        const bool Simulate = bEnable && !KinematicBodies[I];
        Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC,!Simulate);
        if (Simulate)
        {
            Dynamic->setLinearVelocity(U2PVector(InitialVelocity)); Dynamic->setAngularVelocity(PxVec3(0)); Dynamic->wakeUp();
        }
    }
    bSimulating = bEnable;
    return true;
}

bool FPhysicsAssetInstance::RaycastBodies(const FRay& Ray, FPhysicsBodyHit& OutHit) const
{
    OutHit = {};
    if (!IsSimulating() || !U2PVector(Ray.Origin).isFinite() || !U2PVector(Ray.Direction).isFinite() ||
        Ray.Direction.SizeSquared() < 1e-12f) return false;
    const PxVec3 Origin = U2PVector(Ray.Origin);
    const PxVec3 Direction = U2PVector(Ray.Direction).getNormalized();
    float Nearest = PX_MAX_F32;
    FPhysicsActorHandle Selected;
    {
        PxSceneReadLock Lock(*Bodies[0]->GetActorHandle().Actor->getScene());
        for (int I = 0; I < Bodies.Num(); ++I)
        {
            auto* Actor = Bodies[I]->GetActorHandle().Actor->is<PxRigidDynamic>();
            if (!Actor || (Actor->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)) continue;
            TArray<PxShape*> Shapes;
            Shapes.SetNum(Actor->getNbShapes());
            Actor->getShapes(Shapes.GetData(), Shapes.Num());
            int Sphere = 0, Box = 0, Capsule = 0;
            for (const auto* Shape : Shapes)
            {
                ECollisionShape Type = ECollisionShape::Unknown;
                int Index = -1;
                switch (Shape->getGeometryType())
                {
                case PxGeometryType::eSPHERE: Type = ECollisionShape::Sphere; Index = Sphere++; break;
                case PxGeometryType::eBOX: Type = ECollisionShape::Box; Index = Box++; break;
                case PxGeometryType::eCAPSULE: Type = ECollisionShape::Sphyl; Index = Capsule++; break;
                default: continue;
                }
                PxRaycastHit Hit;
                const PxGeometryHolder Geometry = Shape->getGeometry();
                if (PxGeometryQuery::raycast(Origin, Direction, Geometry.any(), Actor->getGlobalPose() * Shape->getLocalPose(),
                    Nearest, PxHitFlag::ePOSITION, 1, &Hit) && Hit.distance < Nearest)
                {
                    Nearest = Hit.distance;
                    Selected = Bodies[I]->GetActorHandle();
                    OutHit = {BoneIndices[I], Type, Index, P2UVector(Hit.position), Hit.distance};
                }
            }
        }
    }
    return Selected.IsValid();
}

bool FPhysicsAssetInstance::BeginGrab(const FRay& Ray, FPhysicsBodyHit& OutHit, const FPhysicsGrabSettings& Settings)
{
    EndGrab();
    if (RaycastBodies(Ray, OutHit))
    {
        const int Picked = BodyForBone[OutHit.Bone];
        float SupportedMass = 0;
        {
            PxSceneReadLock Lock(*Bodies[Picked]->GetActorHandle().Actor->getScene());
            TArray<uint8> Connected;
            Connected.SetNum(Bodies.Num());
            for (auto& Flag : Connected) Flag = 0;
            Connected[Picked] = 1;
            bool Changed = true;
            while (Changed)
            {
                Changed = false;
                for (const auto* Joint : Joints)
                {
                    PxRigidActor *A, *B;
                    Joint->getActors(A, B);
                    int IA = -1, IB = -1;
                    for (int I = 0; I < Bodies.Num(); ++I)
                    {
                        if (Bodies[I]->GetActorHandle().Actor == A) IA = I;
                        if (Bodies[I]->GetActorHandle().Actor == B) IB = I;
                    }
                    if (IA < 0 || IB < 0 || KinematicBodies[IA] || KinematicBodies[IB]) continue;
                    if (Connected[IA] != Connected[IB]) { Connected[IA] = Connected[IB] = 1; Changed = true; }
                }
            }
            for (int I = 0; I < Bodies.Num(); ++I)
                if (Connected[I]) SupportedMass += Bodies[I]->GetActorHandle().Actor->is<PxRigidDynamic>()->getMass();
        }
        if (GrabHandle.Grab(Bodies[Picked]->GetActorHandle(), OutHit.Position, Settings, SupportedMass)) return true;
    }
    OutHit = {};
    return false;
}

void FPhysicsAssetInstance::ApplyToMesh(USkeletalMeshComponent& Mesh)
{
    LastPhysicsPoseMs = 0;
    if (!Matches(Mesh)) { Clear(); return; }
    if (!bSimulating) return;
    FScopeCycleCounter Timer(RagdollStats::STAT_PhysicsPose);
    const FMatrix Component = Mesh.GetWorldMatrix();
    if (std::abs(Component.Determinant()) < 1e-12f) return;
    TArray<FMatrix> Targets;
    Targets.SetNum(Bodies.Num());
    {
        PxSceneReadLock Lock(*Bodies[0]->GetActorHandle().Actor->getScene());
        for (int I=0; I<Bodies.Num(); ++I)
            Targets[I] = BoneInBody[I]*BodyPoseToMatrix(Bodies[I]->GetActorHandle().Actor->getGlobalPose())*Component.Inverse();
    }
    FCompactPose Result;
    const auto& Bones = Mesh.GetBones();
    Result.SetNumBones(Bones.Num());
    TArray<FMatrix> NewComponent;
    NewComponent.SetNum(Bones.Num());
    for (int Bone=0; Bone<Bones.Num(); ++Bone)
    {
        const int Parent = Bones[Bone].ParentIndex;
        const FMatrix ParentComponent = Parent >= 0 ? NewComponent[Parent] : FMatrix::Identity;
        FTransform Local = Mesh.GetBoneLocalTransform(Bone);
        const int Body = BodyForBone[Bone];
        if (Body >= 0 && (std::abs(ParentComponent.Determinant()) < 1e-12f ||
            !MatrixToTransform(Targets[Body]*ParentComponent.Inverse(),Local))) return;
        Result[Bone] = Local;
        NewComponent[Bone] = Local.GetLocalMatrix()*ParentComponent;
    }
    Mesh.SetBoneSpaceTransforms(Result);
    LastPhysicsPoseMs = FPlatformTime::ToMilliseconds64(Timer.Finish());
}

void FPhysicsAssetInstance::DrawConstraints(FLineBatcher& Lines) const
{
    if (!bBuilt) return;
    PxSceneReadLock Lock(*Bodies[0]->GetActorHandle().Actor->getScene());
    for (auto* Joint : Joints)
    {
        PxRigidActor* Child; PxRigidActor* Parent;
        Joint->getActors(Child,Parent);
        const PxTransform A = Child->getGlobalPose()*Joint->getLocalPose(PxJointActorIndex::eACTOR0);
        const PxTransform B = Parent->getGlobalPose()*Joint->getLocalPose(PxJointActorIndex::eACTOR1);
        Lines.AddSphere(P2UVector(A.p),DebugRadius,FVector4(1,0.9f,0.1f,1));
        Lines.AddSphere(P2UVector(B.p),DebugRadius,FVector4(0.1f,1,1,1));
        Lines.AddLine(P2UVector(A.p),P2UVector(B.p),FVector4(1,1,0,1));
    }
}

FRagdollDebugStats FPhysicsAssetInstance::GetDebugStats() const
{
    FRagdollDebugStats Stats;
    if (!bBuilt) return Stats;
    Stats.Instances = 1; Stats.SimulatingInstances = bSimulating ? 1 : 0;
    Stats.Bodies = Bodies.Num(); Stats.Joints = Joints.Num();
    Stats.KinematicSyncMs = LastKinematicSyncMs;
    Stats.PhysicsPoseMs = LastPhysicsPoseMs;
    PxSceneReadLock Lock(*Bodies[0]->GetActorHandle().Actor->getScene());
    for (const auto* Joint : Joints)
    {
        PxRigidActor* Child; PxRigidActor* Parent;
        Joint->getActors(Child,Parent);
        const PxTransform A = Child->getGlobalPose()*Joint->getLocalPose(PxJointActorIndex::eACTOR0);
        const PxTransform B = Parent->getGlobalPose()*Joint->getLocalPose(PxJointActorIndex::eACTOR1);
        Stats.MaxJointSeparation = std::max(Stats.MaxJointSeparation,(A.p-B.p).magnitude());
        const auto* C = Child->is<PxRigidDynamic>();
        const auto* P = Parent->is<PxRigidDynamic>();
        if (C && P && std::min(C->getMass(),P->getMass()) > 0)
            Stats.MaxJointMassRatio = std::max(Stats.MaxJointMassRatio,std::max(C->getMass(),P->getMass())/std::min(C->getMass(),P->getMass()));
    }
    return Stats;
}

