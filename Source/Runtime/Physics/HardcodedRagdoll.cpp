#include "EnginePCH.h"
#include "Physics/HardcodedRagdoll.h"
#include "Animation/BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Math/Box.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversions.h"
#include "Render/LineBatcher.h"
#include <foundation/PxMathUtils.h>
#include <algorithm>
#include <cctype>
#include <cmath>

using namespace physx;

namespace
{
    struct FBodyDefinition
    {
        const char* Bone;
        const char* EndBone;
        int32 ParentBody;
        float WidthRatio;
        float Mass;
        float SwingLimit;
        float TwistLimit;
    };

    // 본 이름, 박스 두께, 질량, 부모 바디, 관절 각도(라디안)를 이 표에서 직접 지정한다.(임시)
    // 손가락·척추 중간 본은 바디를 만들지 않고 가장 가까운 물리 부모를 따라간다.
    constexpr FBodyDefinition Definitions[] = {
        { "Hips",         "Spine",           -1, 0.85f,  8.0f, 0.00f, 0.00f },
        { "Spine",        "Neck",             0, 0.40f, 12.0f, 0.45f, 0.35f },
        { "Head",         "HeadTop_End",      1, 0.65f,  5.0f, 0.65f, 0.50f },
        { "LeftArm",      "LeftForeArm",      1, 0.15f,  2.0f, 1.30f, 0.70f },
        { "LeftForeArm",  "LeftHand",         3, 0.15f,  1.0f, 1.10f, 0.15f },
        { "LeftHand",     "LeftHandMiddle1",  4, 0.55f,  0.5f, 0.50f, 0.25f },
        { "RightArm",     "RightForeArm",     1, 0.15f,  2.0f, 1.30f, 0.70f },
        { "RightForeArm", "RightHand",        6, 0.15f,  1.0f, 1.10f, 0.15f },
        { "RightHand",    "RightHandMiddle1", 7, 0.55f,  0.5f, 0.50f, 0.25f },
        { "LeftUpLeg",    "LeftLeg",          0, 0.20f,  5.0f, 1.00f, 0.45f },
        { "LeftLeg",      "LeftFoot",         9, 0.18f,  3.0f, 1.10f, 0.12f },
        { "LeftFoot",     "LeftToeBase",     10, 0.40f,  1.0f, 0.45f, 0.20f },
        { "RightUpLeg",   "RightLeg",         0, 0.20f,  5.0f, 1.00f, 0.45f },
        { "RightLeg",     "RightFoot",       12, 0.18f,  3.0f, 1.10f, 0.12f },
        { "RightFoot",    "RightToeBase",    13, 0.40f,  1.0f, 0.45f, 0.20f },
    };

    int32 FindRigBone(const USkeletalMeshComponent& Mesh, const char* Name)
    {
        const FString Qualified = FString("mixamorig:") + Name;
        int32 Index = Mesh.FindBoneIndex(Qualified);
        if (Index >= 0) return Index;
        Index = Mesh.FindBoneIndex(Name);
        if (Index >= 0) return Index;

        // 소닉의 LeftLeg7·LeftFoot8처럼 임포트 시 붙은 숫자 접미사도 명시적 본 이름에 연결한다.
        int32 Match = -1;
        const auto& Bones = Mesh.GetBones();
        for (int32 I = 0; I < Bones.Num(); ++I)
        {
            const FString& BoneName = Bones[I].Name;
            const size_t Colon = BoneName.find_last_of(':');
            const FString ShortName = Colon == FString::npos ? BoneName : BoneName.substr(Colon + 1);
            const size_t Length = std::char_traits<char>::length(Name);
            if (ShortName.compare(0, Length, Name) != 0 || ShortName.size() <= Length) continue;
            const bool bDigits = std::all_of(ShortName.begin() + Length, ShortName.end(), [](unsigned char C) { return std::isdigit(C) != 0; });
            if (!bDigits) continue;
            if (Match >= 0) return -1;
            Match = I;
        }
        return Match;
    }

    bool IsDescendant(const TArray<FBoneInfo>& Bones, int32 Child, int32 Ancestor)
    {
        for (int32 I = Child; I >= 0; I = Bones[I].ParentIndex)
            if (I == Ancestor) return true;
        return false;
    }

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

FHardcodedRagdoll::~FHardcodedRagdoll()
{
    Clear();
}

bool FHardcodedRagdoll::Build(USkeletalMeshComponent& Mesh)
{
    static_assert(sizeof(Definitions) / sizeof(Definitions[0]) == BodyCount);
    Clear();
    UWorld* World = Mesh.GetWorld();
    FPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
    PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
    if (!Scene || !Physics || !Mesh.GetSkeletalMesh()) return false;

    Mesh.RefreshBoneTransformsNow();
    const auto& Bones = Mesh.GetBones();
    InitialLocalPose = Mesh.GetBoneSpaceTransforms();
    TArray<FMatrix> InitialWorld;
    InitialWorld.SetNum(Bones.Num());
    for (int32 I = 0; I < Bones.Num(); ++I)
    {
        // 양수 균일 스케일을 전제로 한다. 실패 시 일부 바디만 남기지 않고 전체 생성을 취소한다.
        if (Bones[I].ParentIndex < -1 || Bones[I].ParentIndex >= I)
        {
            KRS_LOG(Warning, "[Ragdoll] Invalid skeleton hierarchy at {}", Bones[I].Name);
            Clear();
            return false;
        }
        InitialWorld[I] = Mesh.GetBoneWorldMatrix(I);
        FTransform Check;
        if (!MatrixToTransform(InitialWorld[I], Check) || std::abs(Check.Scale.X - Check.Scale.Y) > Check.Scale.X * 0.001f || std::abs(Check.Scale.X - Check.Scale.Z) > Check.Scale.X * 0.001f)
        {
            KRS_LOG(Warning, "[Ragdoll] Use positive uniform scale without shear: {}", Bones[I].Name);
            Clear();
            return false;
        }
    }

    FVector Starts[BodyCount];
    FVector Ends[BodyCount];
    float MinBottom = FLT_MAX;
    float MaxTop = -FLT_MAX;
    float MaxSize = 0.0f;
    for (int32 I = 0; I < BodyCount; ++I)
    {
        const FBodyDefinition& Def = Definitions[I];
        BoneIndices[I] = FindRigBone(Mesh, Def.Bone);
        const int32 EndIndex = FindRigBone(Mesh, Def.EndBone);
        const int32 BoneIndex = BoneIndices[I];
        if (BoneIndex < 0 || EndIndex < 0 || BoneIndex == EndIndex || !IsDescendant(Bones, EndIndex, BoneIndex))
        {
            KRS_LOG(Warning, "[Ragdoll] Missing or invalid bone segment: {} -> {}", Def.Bone, Def.EndBone);
            Clear();
            return false;
        }
        if (Def.ParentBody >= 0 && !IsDescendant(Bones, BoneIndex, BoneIndices[Def.ParentBody]))
        {
            KRS_LOG(Warning, "[Ragdoll] Invalid parent body for {}", Def.Bone);
            Clear();
            return false;
        }
        Starts[I] = InitialWorld[BoneIndex].TransformPosition(FVector::ZeroVector);
        Ends[I] = InitialWorld[EndIndex].TransformPosition(FVector::ZeroVector);
        const PxVec3 Delta = U2PVector(Ends[I] - Starts[I]);
        const float Length = Delta.magnitude();
        if (!std::isfinite(Length) || Length < 0.001f)
        {
            KRS_LOG(Warning, "[Ragdoll] Bone segment is too short: {}", Def.Bone);
            Clear();
            return false;
        }

        HalfExtents[I] = FVector(Length * 0.45f, Length * Def.WidthRatio, Length * Def.WidthRatio);
        FBodyInstanceInitParams Params;
        Params.WorldLocation = (Starts[I] + Ends[I]) * 0.5f;
        Params.WorldRotation = P2UQuat(PxShortestRotation(PxVec3(1, 0, 0), Delta / Length));
        FKBoxElem Box;
        Box.X = HalfExtents[I].X * 2.0f;
        Box.Y = HalfExtents[I].Y * 2.0f;
        Box.Z = HalfExtents[I].Z * 2.0f;
        Box.Center = FVector::ZeroVector;
        Params.FallbackGeom.BoxElems.Add(Box);
        Params.bSimulatePhysics = true;

        Bodies[I].InitBody(nullptr, Params, nullptr, Scene);
        if (!Bodies[I].IsValidBodyInstance())
        {
            Clear();
            return false;
        }
        // FBodyInstance가 보장하는 최소 박스 크기를 디버그와 바닥 배치에도 적용한다.
        for (int32 Axis = 0; Axis < 3; ++Axis) HalfExtents[I][Axis] = std::max(HalfExtents[I][Axis], 0.01f);
        const PxTransform Pose = Bodies[I].GetActorHandle().Actor->getGlobalPose();
        BoneInBody[I] = InitialWorld[BoneIndex] * BodyPoseToMatrix(Pose).Inverse();
        const PxMat33 Rotation(Pose.q);
        const float VerticalExtent = std::abs(Rotation.column0.z) * HalfExtents[I].X + std::abs(Rotation.column1.z) * HalfExtents[I].Y + std::abs(Rotation.column2.z) * HalfExtents[I].Z;
        MinBottom = std::min(MinBottom, Pose.p.z - VerticalExtent);
        MaxTop = std::max(MaxTop, Pose.p.z + VerticalExtent);
        MaxSize = std::max(MaxSize, Length);
    }

    const float Height = std::max(MaxTop - MinBottom, 0.1f);
    DebugRadius = std::max(Height * 0.008f, 0.005f);
    FloorHalfExtent = FVector(Height * 2.0f + MaxSize, Height * 2.0f + MaxSize, std::max(Height * 0.025f, 0.05f));
    FBodyInstanceInitParams FloorParams;
    FloorParams.WorldLocation = FVector(Starts[0].X, Starts[0].Y, MinBottom - Height * 0.3f - FloorHalfExtent.Z);
    FloorParams.WorldRotation = FQuat::Identity();
    FloorParams.bSimulatePhysics = false;


    FKBoxElem FloorBox;
    FloorBox.X = FloorHalfExtent.X * 2.0f;
    FloorBox.Y = FloorHalfExtent.Y * 2.0f;
    FloorBox.Z = FloorHalfExtent.Z * 2.0f;
    FloorBox.Center = FVector::ZeroVector;
    FloorParams.FallbackGeom.BoxElems.Add(FloorBox);

    TestFloor.InitBody(nullptr, FloorParams, nullptr, Scene);
    if (!TestFloor.IsValidBodyInstance())
    {
        Clear();
        return false;
    }

    PxScene* NativeScene = Bodies[0].GetActorHandle().Actor->getScene();
    bool bSucceeded = NativeScene != nullptr;
    if (NativeScene)
    {
        PxSceneWriteLock Lock(*NativeScene);
        // 첫 단계는 자체 충돌을 끈 Aggregate를 사용해 겹치는 초기 박스가 서로 밀어내는 현상을 피한다. 바닥·다른 물체와는 충돌한다.
        Aggregate = Physics->createAggregate(BodyCount, false);
        bSucceeded = Aggregate != nullptr;
        for (int32 I = 0; bSucceeded && I < BodyCount; ++I)
        {
            PxRigidActor* Actor = Bodies[I].GetActorHandle().Actor;
            NativeScene->removeActor(*Actor);
            bSucceeded = Aggregate->addActor(*Actor);
            PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>();
            if (Dynamic)
            {
                PxRigidBodyExt::setMassAndUpdateInertia(*Dynamic, Definitions[I].Mass);
                Dynamic->setLinearDamping(0.05f);
                Dynamic->setAngularDamping(0.3f);
                Dynamic->setSolverIterationCounts(16, 4);
                Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_SPECULATIVE_CCD, true);
            }
        }
        if (bSucceeded) NativeScene->addAggregate(*Aggregate);

        for (int32 I = 1; bSucceeded && I < BodyCount; ++I)
        {
            PxRigidActor* Parent = Bodies[Definitions[I].ParentBody].GetActorHandle().Actor;
            PxRigidActor* Child = Bodies[I].GetActorHandle().Actor;
            const PxTransform JointWorld(U2PVector(Starts[I]), Child->getGlobalPose().q);
            PxD6Joint* Joint = PxD6JointCreate(*Physics, Parent, Parent->getGlobalPose().getInverse() * JointWorld, Child, Child->getGlobalPose().getInverse() * JointWorld);
            Joints[I - 1] = Joint;
            bSucceeded = Joint != nullptr;
            if (!Joint) break;
            Joint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
            Joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
            Joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);
            Joint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
            Joint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLIMITED);
            Joint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLIMITED);
            Joint->setTwistLimit(PxJointAngularLimitPair(-Definitions[I].TwistLimit, Definitions[I].TwistLimit));
            Joint->setSwingLimit(PxJointLimitCone(Definitions[I].SwingLimit, Definitions[I].SwingLimit));
            Joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);
        }
    }
    if (!bSucceeded)
    {
        KRS_LOG(Warning, "[Ragdoll] Failed to create full-body actors or joints");
        Clear();
        return false;
    }

    {
        PxSceneWriteLock Lock(*NativeScene);

        for (FBodyInstance& Body : Bodies)
        {
            PxRigidDynamic* Dynamic = Body.GetActorHandle().Actor->is<PxRigidDynamic>();
            Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
        }
    }

    bSimulating = false;

    SourceMesh = Mesh.GetSkeletalMesh();
    bBuilt = true;
    KRS_LOG(Info, "[Ragdoll] Created {} bodies and {} D6 joints; self collision disabled", BodyCount, BodyCount - 1);
    return true;
}

void FHardcodedRagdoll::Clear()
{
    LastKinematicSyncMs = 0.0;
    LastPhysicsPoseMs = 0.0;

    bBuilt = false;
    bSimulating = false;
    bPoseErrorLogged = false;
    SourceMesh = nullptr;
    InitialLocalPose.Reset();
    for (int32& Index : BoneIndices) Index = -1;
    PxScene* Scene = TestFloor.IsValidBodyInstance() ? TestFloor.GetActorHandle().Actor->getScene() : nullptr;
    for (FBodyInstance& Body : Bodies)
        if (!Scene && Body.IsValidBodyInstance()) Scene = Body.GetActorHandle().Actor->getScene();

    const auto Release = [this]()
    {
        for (PxD6Joint*& Joint : Joints)
        {
            if (Joint) Joint->release();
            Joint = nullptr;
        }
        for (FBodyInstance& Body : Bodies) Body.TermBody();
        if (Aggregate) Aggregate->release();
        Aggregate = nullptr;
        TestFloor.TermBody();
    };
    if (Scene)
    {
        PxSceneWriteLock Lock(*Scene);
        Release();
    }
    else Release();
}

void FHardcodedRagdoll::ApplyToMesh(USkeletalMeshComponent& Mesh)
{
    LastPhysicsPoseMs = 0.0;
    if (!bBuilt || !bSimulating) return;
    // 실행 도중 메시가 교체되면 이전 스켈레톤의 본 인덱스를 사용하지 않는다.
    if (Mesh.GetSkeletalMesh() != SourceMesh)
    {
        Clear();
        return;
    }
    const auto& Bones = Mesh.GetBones();
    if (InitialLocalPose.Num() != Bones.Num()) return;
    PxScene* Scene = Bodies[0].GetActorHandle().Actor->getScene();
    if (!Scene) return;

    FScopeCycleCounter Timer(RagdollStats::STAT_PhysicsPose);
    FMatrix TargetComponent[BodyCount];

    const FMatrix WorldToComponent = Mesh.GetWorldMatrix().Inverse();
    {
        PxSceneReadLock Lock(*Scene);
        for (int32 I = 0; I < BodyCount; ++I)
            TargetComponent[I] = BoneInBody[I] * BodyPoseToMatrix(Bodies[I].GetActorHandle().Actor->getGlobalPose()) * WorldToComponent;
    }

    FCompactPose Result;
    Result.SetNumBones(Bones.Num());
    TArray<FMatrix> NewComponent;
    NewComponent.SetNum(Bones.Num());
    for (int32 Bone = 0; Bone < Bones.Num(); ++Bone)
    {
        const int32 Parent = Bones[Bone].ParentIndex;
        const FMatrix ParentComponent = Parent >= 0 ? NewComponent[Parent] : FMatrix::Identity;
        FTransform Local = InitialLocalPose[Bone];
        for (int32 I = 0; I < BodyCount; ++I)
        {
            if (BoneIndices[I] != Bone) continue;
            if (!MatrixToTransform(TargetComponent[I] * ParentComponent.Inverse(), Local))
            {
                if (!bPoseErrorLogged) KRS_LOG(Warning, "[Ragdoll] Cannot convert physics pose for {}; check uniform scale", Bones[Bone].Name);
                bPoseErrorLogged = true;
                return;
            }
            break;
        }
        Result[Bone] = Local;
        NewComponent[Bone] = Local.GetLocalMatrix() * ParentComponent;
    }
    Mesh.SetBoneSpaceTransforms(Result);
    LastPhysicsPoseMs = FPlatformTime::ToMilliseconds64(Timer.Finish());
}

void FHardcodedRagdoll::DrawConstraints(FLineBatcher& Lines) const
{
    if (!bBuilt) return;
    PxScene* Scene = Bodies[0].GetActorHandle().Actor->getScene();
    if (!Scene) return;
    PxSceneReadLock Lock(*Scene);
    for (int32 I = 1; I < BodyCount; ++I)
    {
        const PxTransform ParentAnchor = Bodies[Definitions[I].ParentBody].GetActorHandle().Actor->getGlobalPose() * Joints[I - 1]->getLocalPose(PxJointActorIndex::eACTOR0);
        const PxTransform ChildAnchor = Bodies[I].GetActorHandle().Actor->getGlobalPose() * Joints[I - 1]->getLocalPose(PxJointActorIndex::eACTOR1);
        const FVector4 Color(1.0f, 1.0f, 0.0f, 1.0f);
        Lines.AddSphere(P2UVector(ParentAnchor.p), DebugRadius, Color);
        Lines.AddLine(P2UVector(ParentAnchor.p), P2UVector(ChildAnchor.p), Color);
    }
}

bool FHardcodedRagdoll::SyncFromAnimation(USkeletalMeshComponent& Mesh, bool bTeleport)
{
    LastKinematicSyncMs = 0.0;

    if (!bBuilt || bSimulating || Mesh.GetSkeletalMesh() != SourceMesh) return false;
    if (Mesh.GetBones().Num() != InitialLocalPose.Num()) return false;

    PxScene* Scene = Bodies[0].GetActorHandle().Actor->getScene();
    if (!Scene) return false;

    FScopeCycleCounter Timer(RagdollStats::STAT_KinematicSync);
    PxTransform Targets[BodyCount];

    // 먼저 모든 변환을 계산한다. 중간 실패로 일부 바디만 갱신되는 것을 피한다.
    for (int32 I = 0; I < BodyCount; ++I)
    {
        const FMatrix BoneWorld = Mesh.GetBoneWorldMatrix(BoneIndices[I]);
        const FMatrix BodyWorld = BoneInBody[I].Inverse() * BoneWorld;

        FTransform Pose;
        if (!MatrixToTransform(BodyWorld, Pose)) return false;

        // 실행 중 캐릭터 크기를 바꾸는 기능은 지금은 다루지 않는다.
        if (std::abs(Pose.Scale.X - 1.0f) > 0.001f || std::abs(Pose.Scale.Y - 1.0f) > 0.001f ||  std::abs(Pose.Scale.Z - 1.0f) > 0.001f)
        {
            return false;
        }

        Targets[I] = U2PTransform(Pose.Location, Pose.Rotation);
    }

    PxSceneWriteLock Lock(*Scene);

    for (int32 I = 0; I < BodyCount; ++I)
    {
        PxRigidDynamic* Dynamic = Bodies[I].GetActorHandle().Actor->is<PxRigidDynamic>();

        if (bTeleport)
        {
            // Dynamic 전환 직전에는 마지막 목표가 아니라 현재 자세에 즉시 맞춘다.
            Dynamic->setGlobalPose(Targets[I]);
        }
        else
        {
            // 평상시에는 다음 물리 프레임에서 도달할 목표 자세를 전달한다.
            Dynamic->setKinematicTarget(Targets[I]);
        }
    }
    LastKinematicSyncMs = FPlatformTime::ToMilliseconds64(Timer.Finish());
    return true;
}

bool FHardcodedRagdoll::SetSimulating(USkeletalMeshComponent& Mesh, bool bEnable)
{
    if (!bBuilt || Mesh.GetSkeletalMesh() != SourceMesh) return false;
    if (bEnable == bSimulating) return true;

    if (bEnable)
    {
        // 현재 애니메이션 자세를 확정하고 바디를 즉시 맞춘다.
        Mesh.RefreshBoneTransformsNow();

        if (!SyncFromAnimation(Mesh, true))
        {
            KRS_LOG(Warning, "[Ragdoll] Cannot synchronize the current animation pose");
            return false;
        }

        // 손가락 등 물리 바디가 없는 본은 전환 순간의 로컬 자세를 유지한다.
        InitialLocalPose = Mesh.GetBoneSpaceTransforms();
    }

    PxScene* Scene = Bodies[0].GetActorHandle().Actor->getScene();
    if (!Scene) return false;

    {
        PxSceneWriteLock Lock(*Scene);

        for (FBodyInstance& Body : Bodies)
        {
            PxRigidDynamic* Dynamic = Body.GetActorHandle().Actor->is<PxRigidDynamic>();

            Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, !bEnable);

            if (bEnable)
            {
                // 첫 실험에서는 애니메이션의 운동 속도를 전달하지 않고 정지 속도로 시작한다.
                Dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                Dynamic->setAngularVelocity(PxVec3(0, 0, 0));
                Dynamic->wakeUp();
            }
        }
    }

    bSimulating = bEnable;
    KRS_LOG(Info, "[Ragdoll] {}", bEnable ? "Dynamic enabled" : "Kinematic enabled");
    return true;
}

FRagdollDebugStats FHardcodedRagdoll::GetDebugStats() const
{
    FRagdollDebugStats Stats;

    if (!bBuilt) return Stats;

    Stats.Instances = 1;
    Stats.SimulatingInstances = bSimulating ? 1 : 0;
    Stats.Bodies = BodyCount;
    Stats.Joints = BodyCount - 1;

    // 현재 상태와 관계없는 이전 측정값은 표시하지 않는다.
    Stats.KinematicSyncMs = bSimulating ? 0.0 : LastKinematicSyncMs;
    Stats.PhysicsPoseMs = bSimulating ? LastPhysicsPoseMs : 0.0;

    return Stats;
}