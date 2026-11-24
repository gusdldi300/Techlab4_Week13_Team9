#include "EnginePCH.h"
#include "Physics/PhysicsAssetBuilder.h"

#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Render/SkeletalMesh.h"

UPhysicsAsset* PhysicsAssetBuilder::CreateTwoBodyTestAsset(USkeletalMesh& Mesh, int32 ParentBoneIndex, int32 ChildBoneIndex)
{
    const auto& Bones = Mesh.GetRefSkeleton();

    if (ParentBoneIndex < 0 || ParentBoneIndex >= Bones.Num() || ChildBoneIndex < 0 || ChildBoneIndex >= Bones.Num())
    {
        return nullptr;
    }

    if (Bones[ChildBoneIndex].ParentIndex != ParentBoneIndex)
    {
        return nullptr;
    }

    const FTransform& ChildBind = Bones[ChildBoneIndex].LocalBindPose;

    const float BoneDistance = ChildBind.Location.Size();

    if (BoneDistance <= 0.0001f)
    {
        return nullptr;
    }

    // 아래에서 계속 작성
    UPhysicsAsset* Asset = NewObject<UPhysicsAsset>(&Mesh);

    UBodySetup* ParentBody = NewObject<UBodySetup>(Asset);
    UBodySetup* ChildBody = NewObject<UBodySetup>(Asset);

    ParentBody->BoneName = FName(Bones[ParentBoneIndex].Name);

    ChildBody->BoneName = FName(Bones[ChildBoneIndex].Name);

    ParentBody->PhysicsType = EPhysicsType::Kinematic;
    ChildBody->PhysicsType = EPhysicsType::Simulated;

    const float Radius = BoneDistance * 0.15f;

    FKSphereElem Sphere;
    Sphere.Center = FVector::ZeroVector;
    Sphere.Radius = Radius;

    ParentBody->AggGeom.SphereElems.Add(Sphere);
    ChildBody->AggGeom.SphereElems.Add(Sphere);

    Asset->BodySetups.Add(ParentBody);
    Asset->BodySetups.Add(ChildBody);

    Asset->UpdateBodySetupIndexMap();

    UPhysicsConstraintTemplate* Joint = NewObject<UPhysicsConstraintTemplate>(Asset);

    // 기존 프로젝트 규칙: 1은 자식, 2는 부모
    Joint->ConstraintBone1 = ChildBody->BoneName;
    Joint->ConstraintBone2 = ParentBody->BoneName;

    // 관절을 자식 본의 시작점에 배치한다.
    Joint->Pos1 = FVector::ZeroVector;
    Joint->Pos2 = ChildBind.Location;

    // 자식 몸체 기준 관절 방향
    Joint->PriAxis1 = FVector(1.0f, 0.0f, 0.0f);
    Joint->SecAxis1 = FVector(0.0f, 1.0f, 0.0f);

    // 같은 방향을 부모 몸체 기준으로 표현
    Joint->PriAxis2 = ChildBind.Rotation.RotateVector(Joint->PriAxis1);

    Joint->SecAxis2 = ChildBind.Rotation.RotateVector(Joint->SecAxis1);

    Asset->PhysicsConstraint.Add(Joint);

    Asset->DisableCollision(0, 1);

    return Asset;
}