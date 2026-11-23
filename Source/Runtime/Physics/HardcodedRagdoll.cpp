#include "EnginePCH.h"
#include "Physics/HardcodedRagdoll.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversions.h"

#include "Math/Box.h"
#include "Math/Transform.h"
#include "Render/LineBatcher.h"

#include <foundation/PxMathUtils.h>
#include <algorithm>

#include "Animation/BonePose.h"
#include "Math/Rotator.h"

using namespace physx;

namespace
{
    FVector GetBonePosition(USkeletalMeshComponent& Mesh, int32 BoneIndex)
    {
        return Mesh.GetBoneWorldMatrix(BoneIndex).TransformPosition(FVector(0.0f, 0.0f, 0.0f));
    }

    bool MakeSegment(FBodyInstance& Body, FPhysicsScene* Scene, const FVector& Start, const FVector& End, FVector& OutHalfExtent)
    {
        const PxVec3 A = U2PVector(Start);
        const PxVec3 B = U2PVector(End);

        const PxVec3 Delta = B - A;
        const float Length = Delta.magnitude();

        if (Length < 0.001f)
        {
            return false;
        }

        const PxVec3 Direction = Delta / Length;

        // 박스의 긴 방향인 로컬 X축을
        // Start → End 방향에 맞춘다.
        const PxQuat Rotation = PxShortestRotation(PxVec3(1.0f, 0.0f, 0.0f), Direction);

        // 길이의 약 90%를 채운다.
        // 관절 주변에서 박스가 심하게 겹치지 않도록 약간 줄인다.
        const float Thickness = Length * 0.15f;

        OutHalfExtent = FVector(Length * 0.45f, Thickness, Thickness);

        FBodyInstanceInitParams Params;
        Params.WorldLocation = P2UVector((A + B) * 0.5f);
        Params.WorldRotation = P2UQuat(Rotation);
        Params.BoxHalfExtent = OutHalfExtent;
        Params.BoxCenterOffset = FVector(0.0f, 0.0f, 0.0f);
        Params.bSimulatePhysics = true;

        // 본별 바디가 메시 컴포넌트 전체를 움직이면 안 된다.
        Body.InitBody(Params, nullptr, Scene);

        return Body.IsValidBodyInstance();
    }

    FMatrix BodyPoseToMatrix(const PxTransform& Pose)
    {
        FTransform Transform;
        Transform.Location = P2UVector(Pose.p);
        Transform.Rotation = P2UQuat(Pose.q);
        Transform.Scale = FVector(1.0f, 1.0f, 1.0f);

        return Transform.GetLocalMatrix();
    }

    bool MatrixToTransform(const FMatrix& Matrix, FTransform& OutTransform)
    {
        // 행벡터 방식:
        // 각 행은 스케일이 포함된 로컬 축이다.
        const FVector AxisX(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]);

        const FVector AxisY(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]);

        const FVector AxisZ(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]);

        const FVector Scale(AxisX.Size(), AxisY.Size(), AxisZ.Size());

        // 크기가 0인 축으로는 회전을 복원할 수 없다.
        constexpr float MinScale = 0.000001f;

        if (Scale.X < MinScale || Scale.Y < MinScale || Scale.Z < MinScale)
        {
            return false;
        }

        // 각 축의 길이를 나눠 스케일을 제거한다.
        FMatrix RotationMatrix = FMatrix::Identity;

        for (int32 Column = 0; Column < 3; ++Column)
        {
            RotationMatrix.M[0][Column] = Matrix.M[0][Column] / Scale.X;

            RotationMatrix.M[1][Column] = Matrix.M[1][Column] / Scale.Y;

            RotationMatrix.M[2][Column] = Matrix.M[2][Column] / Scale.Z;
        }

        OutTransform.Location = FVector(Matrix.M[3][0], Matrix.M[3][1], Matrix.M[3][2]);

        OutTransform.Scale = Scale;

        OutTransform.Rotation = MatrixToRotator(RotationMatrix).Quaternion().Normalized();

        return true;
    }

}

FHardcodedRagdoll::~FHardcodedRagdoll()
{
    Clear();
}

bool FHardcodedRagdoll::Build(USkeletalMeshComponent& Mesh,  const FString& UpperArmBone,  const FString& ForeArmBone,  const FString& HandBone)
{
    Clear();

    UWorld* World = Mesh.GetWorld();
    FPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;

    PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();

    if (!Scene || !Physics)
    {
        KRS_LOG(Warning, "[Ragdoll] Physics scene is not ready");
        return false;
    }

    const int32 UpperIndex = Mesh.FindBoneIndex(UpperArmBone);
    const int32 ForeIndex = Mesh.FindBoneIndex(ForeArmBone);
    const int32 HandIndex = Mesh.FindBoneIndex(HandBone);

    if (UpperIndex < 0 || ForeIndex < 0 || HandIndex < 0)
    {
        KRS_LOG(Warning, "[Ragdoll] Bone not found: {}, {}, {}", UpperArmBone, ForeArmBone, HandBone);

        return false;
    }

    Mesh.RefreshBoneTransformsNow();

    const FVector Shoulder = GetBonePosition(Mesh, UpperIndex);
    const FVector Elbow = GetBonePosition(Mesh, ForeIndex);
    const FVector Wrist = GetBonePosition(Mesh, HandIndex);

    const bool bUpperCreated = MakeSegment(Bodies[0], Scene, Shoulder, Elbow, HalfExtents[0]);

    const bool bForeCreated = MakeSegment(Bodies[1], Scene, Elbow, Wrist, HalfExtents[1]);

    if (!bUpperCreated || !bForeCreated)
    {
        Clear();
        return false;
    }

    // 테스트 바닥: 시작 자세의 팔보다 아래에 둔다.
    const float MinZ = std::min({ Shoulder.Z, Elbow.Z, Wrist.Z });

    const float Span = (U2PVector(Wrist) - U2PVector(Shoulder)).magnitude();

    const float FloorWidth = std::max(2.0f, Span * 3.0f);

    FloorHalfExtent = FVector(FloorWidth, FloorWidth, 0.05f);

    // HalfExtents.X는 박스 길이의 절반이다.
    const float UpperLength = HalfExtents[0].X * 2.0f;
    const float ForeLength = HalfExtents[1].X * 2.0f;

    // 모델 크기에 비례한 낙하 공간을 확보한다.
    const float DropDistance = std::max(0.8f, UpperLength + ForeLength);

    FBodyInstanceInitParams FloorParams;
    FloorParams.WorldLocation = FVector(Shoulder.X, Shoulder.Y, MinZ - DropDistance);

    FloorParams.WorldRotation = FQuat::Identity();
    FloorParams.BoxHalfExtent = FloorHalfExtent;
    FloorParams.BoxCenterOffset = FVector(0.0f, 0.0f, 0.0f);
    FloorParams.bSimulatePhysics = false; // 정적 바닥

    TestFloor.InitBody(FloorParams, nullptr, Scene);

    if (!TestFloor.IsValidBodyInstance())
    {
        Clear();
        return false;
    }

    PxRigidActor* Upper = Bodies[0].GetActorHandle().Actor;
    PxRigidActor* Fore = Bodies[1].GetActorHandle().Actor;

    PxScene* NativeScene = Upper->getScene();

    if (!NativeScene)
    {
        Clear();
        return false;
    }

    {
        PxSceneWriteLock Lock(*NativeScene);

        // 첫 실험은 두 바디의 질량을 동일하게 둔다.
        for (FBodyInstance& Body : Bodies)
        {
            PxRigidDynamic* Dynamic = Body.GetActorHandle().Actor->is<PxRigidDynamic>();

            if (Dynamic)
            {
                PxRigidBodyExt::setMassAndUpdateInertia(*Dynamic, 1.0f);

                Dynamic->setLinearDamping(0.05f);
                Dynamic->setAngularDamping(0.2f);
                Dynamic->setSolverIterationCounts(8, 2);
            }
        }

        // 두 바디가 공유해야 하는 관절의 월드 프레임.
        // 이번에는 위치만 팔꿈치에 두고 월드 축을 사용한다.
        const PxTransform JointWorld(U2PVector(Elbow), PxQuat(PxIdentity));

        // 같은 월드 프레임을 각 바디의 로컬 공간으로 변환한다.
        const PxTransform Local0 = Upper->getGlobalPose().getInverse() * JointWorld;

        const PxTransform Local1 = Fore->getGlobalPose().getInverse() * JointWorld;

        ElbowJoint = PxD6JointCreate(*Physics, Upper, Local0, Fore, Local1);

        if (ElbowJoint)
        {
            // 관절 위치가 벌어지지 않도록 이동 3축 잠금.
            ElbowJoint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
            ElbowJoint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
            ElbowJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);

            // 첫 실험은 회전 3축을 제한된 범위에서 허용한다.
            ElbowJoint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
            ElbowJoint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eLIMITED);
            ElbowJoint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eLIMITED);

            ElbowJoint->setTwistLimit(PxJointAngularLimitPair(-0.5f, 0.5f));

            ElbowJoint->setSwingLimit(PxJointLimitCone(0.8f, 0.8f));

            // 서로 연결된 두 바디끼리의 충돌은 끈다.
            ElbowJoint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);
        }


        // 어깨 관절의 월드 프레임.
        // 위치는 어깨 본에 맞추고,
        // 방향은 시작 시점 위팔 바디의 회전에 맞춘다.
        const PxTransform ShoulderWorld(U2PVector(Shoulder), Upper->getGlobalPose().q);
        // 위팔 바디 기준으로 어깨 연결점을 표현한다.
        const PxTransform ShoulderLocal = Upper->getGlobalPose().getInverse() * ShoulderWorld;

        ShoulderJoint = PxD6JointCreate(*Physics, nullptr, ShoulderWorld, Upper, ShoulderLocal);

        if (ShoulderJoint)
        {
            // 어깨 연결점이 이동하지 않도록 한다.
            ShoulderJoint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);

            ShoulderJoint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);

            ShoulderJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);

            // 첫 실험에서는 어깨 회전을 모두 허용한다.
            ShoulderJoint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);

            ShoulderJoint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);

            ShoulderJoint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
        }

    }

    // 쓰기 잠금이 끝난 뒤 실패 처리를 한다.
    if (!ElbowJoint || !ShoulderJoint)
    {
        KRS_LOG(Warning, "[Ragdoll] Failed to create elbow or shoulder joint");

        Clear();
        return false;
    }

    BoneIndices[0] = UpperIndex;
    BoneIndices[1] = ForeIndex;

    InitialLocalPose = Mesh.GetBoneSpaceTransforms();

    {
        PxSceneReadLock Lock(*NativeScene);

        for (int32 I = 0; I < 2; ++I)
        {
            const FMatrix BoneWorld = Mesh.GetBoneWorldMatrix(BoneIndices[I]);

            const PxTransform BodyPose = Bodies[I].GetActorHandle().Actor->getGlobalPose();

            const FMatrix BodyWorld = BodyPoseToMatrix(BodyPose);

            BoneInBody[I] = BoneWorld * BodyWorld.Inverse();
        }
    }


    bBuilt = true;

    KRS_LOG(Info, "[Ragdoll] Created two bodies and two D6 joint");

    return true;
}

void FHardcodedRagdoll::Clear()
{
    bBuilt = false;

    BoneIndices[0] = -1;
    BoneIndices[1] = -1;

    InitialLocalPose.Reset();

    // 모든 바디는 같은 씬에 생성한다.
    PxScene* Scene = nullptr;

    for (FBodyInstance& Body : Bodies)
    {
        if (Body.IsValidBodyInstance())
        {
            Scene = Body.GetActorHandle().Actor->getScene();
            if (Scene)
            {
                break;
            }
        }
    }

    if (!Scene && TestFloor.IsValidBodyInstance())
    {
        Scene = TestFloor.GetActorHandle().Actor->getScene();
    }

    const auto ReleaseObjects = [this]()
        {
            if (ShoulderJoint)
            {
                ShoulderJoint->release();
                ShoulderJoint = nullptr;
            }

            // 관절이 바디를 참조하므로 관절부터 해제한다.
            if (ElbowJoint)
            {
                ElbowJoint->release();
                ElbowJoint = nullptr;
            }

            for (FBodyInstance& Body : Bodies)
            {
                Body.TermBody();
            }

            TestFloor.TermBody();
        };

    if (Scene)
    {
        PxSceneWriteLock Lock(*Scene);
        ReleaseObjects();
    }
    else
    {
        ReleaseObjects();
    }
}

void FHardcodedRagdoll::DrawDebug(FLineBatcher& Lines) const
{
    if (!bBuilt)
    {
        return;
    }

    PxScene* Scene = Bodies[0].GetActorHandle().Actor->getScene();

    if (!Scene)
    {
        return;
    }

    PxSceneReadLock Lock(*Scene);

    const auto DrawBody = [&Lines](const FBodyInstance& Body, const FVector& HalfExtent, const FVector4& Color)
        {
            const PxTransform Pose = Body.GetActorHandle().Actor->getGlobalPose();

            FTransform Transform;
            Transform.Location = P2UVector(Pose.p);
            Transform.Rotation = P2UQuat(Pose.q);

            const FBox LocalBox{ HalfExtent * -1.0f, HalfExtent};

            Lines.AddOrientedBox(LocalBox, Transform.GetLocalMatrix(), Color);
        };

    DrawBody(Bodies[0], HalfExtents[0], FVector4(1.0f, 0.4f, 0.1f, 1.0f));

    DrawBody(Bodies[1], HalfExtents[1], FVector4(0.1f, 0.8f, 1.0f, 1.0f));

    DrawBody(TestFloor, FloorHalfExtent, FVector4(0.4f, 1.0f, 0.4f, 1.0f));

    if (ShoulderJoint)
    {
        // actor0은 nullptr이므로 이 프레임은 월드 기준이다.
        const PxTransform FixedAnchor = ShoulderJoint->getLocalPose(PxJointActorIndex::eACTOR0);

        // actor1은 위팔 바디이므로 월드로 변환한다.
        const PxTransform UpperAnchor = Bodies[0].GetActorHandle().Actor->getGlobalPose() * ShoulderJoint->getLocalPose(PxJointActorIndex::eACTOR1);

        const FVector FixedPosition = P2UVector(FixedAnchor.p);
        const FVector UpperPosition = P2UVector(UpperAnchor.p);

        const float Radius = std::max(0.02f, HalfExtents[0].X * 0.1f);

        // 고정된 어깨 위치: 자홍색
        Lines.AddSphere(FixedPosition, Radius, FVector4(1.0f, 0.0f, 1.0f, 1.0f));

        // 실제 위팔의 어깨 연결점: 흰색
        Lines.AddSphere(UpperPosition, Radius * 0.7f, FVector4(1.0f, 1.0f, 1.0f, 1.0f));

        Lines.AddLine(FixedPosition, UpperPosition, FVector4(1.0f, 0.0f, 1.0f, 1.0f));
    }

    if (ElbowJoint)
    {
        const PxTransform Anchor0 = Bodies[0].GetActorHandle().Actor->getGlobalPose() * ElbowJoint->getLocalPose(PxJointActorIndex::eACTOR0);

        const PxTransform Anchor1 = Bodies[1].GetActorHandle().Actor->getGlobalPose() * ElbowJoint->getLocalPose(PxJointActorIndex::eACTOR1);

        const FVector4 Color(1.0f, 1.0f, 0.0f, 1.0f);

        Lines.AddSphere(P2UVector(Anchor0.p), 0.02f, Color);
        Lines.AddSphere(P2UVector(Anchor1.p), 0.02f, Color);
        Lines.AddLine(P2UVector(Anchor0.p), P2UVector(Anchor1.p), Color);
    }
}

void FHardcodedRagdoll::ApplyToMesh(USkeletalMeshComponent& Mesh)
{
    if (!bBuilt)
    {
        return;
    }

    const auto& Bones = Mesh.GetBones();
    const int32 NumBones = Bones.Num();

    if (NumBones == 0 || InitialLocalPose.Num() != NumBones)
    {
        return;
    }

    // 테스트 도중 스켈레톤이 바뀌는 경우를 방어한다.
    for (int32 I = 0; I < 2; ++I)
    {
        if (BoneIndices[I] < 0 || BoneIndices[I] >= NumBones)
        {
            return;
        }
    }

    PxScene* Scene = Bodies[0].GetActorHandle().Actor->getScene();

    if (!Scene)
    {
        return;
    }

    // PhysX에서는 현재 두 바디의 자세만 읽는다.
    FMatrix TargetBoneWorld[2];

    {
        PxSceneReadLock Lock(*Scene);

        for (int32 I = 0; I < 2; ++I)
        {
            const PxTransform BodyPose = Bodies[I].GetActorHandle().Actor->getGlobalPose();

            const FMatrix BodyWorld = BodyPoseToMatrix(BodyPose);

            TargetBoneWorld[I] = BoneInBody[I] * BodyWorld;
        }
    }

    // 월드 → 메시 컴포넌트 공간
    const FMatrix WorldToComponent = Mesh.GetWorldMatrix().Inverse();

    FMatrix TargetBoneComponent[2];

    for (int32 I = 0; I < 2; ++I)
    {
        TargetBoneComponent[I] = TargetBoneWorld[I] * WorldToComponent;
    }

    FCompactPose Result;
    Result.SetNumBones(NumBones);

    TArray<FMatrix> NewComponentMatrices;
    NewComponentMatrices.SetNum(NumBones);

    // 현재 스켈레톤은 부모 본이 자식보다 앞에 있다.
    // 따라서 부모부터 최종 자세를 계산할 수 있다.
    for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
    {
        const int32 ParentIndex = Bones[BoneIndex].ParentIndex;

        const FMatrix ParentComponent = ParentIndex >= 0 ? NewComponentMatrices[ParentIndex] : FMatrix::Identity;

        FTransform Local = InitialLocalPose[BoneIndex];

        int32 BodyIndex = -1;

        if (BoneIndex == BoneIndices[0])
        {
            BodyIndex = 0;
        }
        else if (BoneIndex == BoneIndices[1])
        {
            BodyIndex = 1;
        }

        if (BodyIndex >= 0)
        {
            const FMatrix LocalMatrix = TargetBoneComponent[BodyIndex] * ParentComponent.Inverse();

            if (!MatrixToTransform(LocalMatrix, Local))
            {
                return;
            }
        }

        Result[BoneIndex] = Local;

        NewComponentMatrices[BoneIndex] =  Local.GetLocalMatrix() * ParentComponent;
    }

    Mesh.SetBoneSpaceTransforms(Result);
}