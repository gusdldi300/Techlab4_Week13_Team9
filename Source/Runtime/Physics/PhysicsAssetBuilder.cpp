#include "EnginePCH.h"
#include "Physics/PhysicsAssetBuilder.h"

#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cctype>

namespace
{
    bool Finite(const FVector& V)
    {
        return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
    }

    FQuat CapsuleRotation(const FVector& Direction)
    {
        // Capsules use local Z. Handle the antiparallel case explicitly.
        if (Direction.Z < -0.99999f) return FQuat::MakeFromAxisAngle(FVector(1,0,0), PI);
        return FQuat(-Direction.Y, Direction.X, 0, 1 + Direction.Z).Normalized();
    }

    enum class EBodyRole { None, Pelvis, Spine, Neck, Head, UpperArm, Forearm, Hand, Thigh, Calf, Foot };
    FString NormalName(const FString& Name)
    {
        FString Result;
        for (unsigned char C : Name) if (std::isalnum(C)) Result += static_cast<char>(std::tolower(C));
        return Result;
    }
    bool Digit(const FString& Name)
    {
        for (const char* Word : {"finger", "thumb", "index", "middle", "ring", "pinky", "toe"})
            if (Name.find(Word) != FString::npos) return true;
        return false;
    }
    EBodyRole Role(const FString& Name)
    {
        const auto N = NormalName(Name);
        if (N.ends_with("end") || N.find("twist") != FString::npos || Digit(N)) return EBodyRole::None;
        if (N.ends_with("pelvis") || N.ends_with("hips")) return EBodyRole::Pelvis;
        if (N.find("spine") != FString::npos || N.ends_with("chest")) return EBodyRole::Spine;
        if (N.ends_with("neck")) return EBodyRole::Neck;
        if (N.ends_with("head")) return EBodyRole::Head;
        if (N.ends_with("forearm") || N.ends_with("lowerarm")) return EBodyRole::Forearm;
        if (N.ends_with("upperarm") || N.ends_with("leftarm") || N.ends_with("rightarm")) return EBodyRole::UpperArm;
        if (N.ends_with("hand")) return EBodyRole::Hand;
        if (N.ends_with("thigh") || N.ends_with("upleg")) return EBodyRole::Thigh;
        if (N.ends_with("calf") || N.ends_with("lowerleg") || N.ends_with("leftleg") || N.ends_with("rightleg")) return EBodyRole::Calf;
        if (N.ends_with("foot")) return EBodyRole::Foot;
        return EBodyRole::None;
    }

    // Trim extreme samples instead of letting one distant, weakly connected vertex
    // enlarge an entire body. All samples are already in the body's local space.
    void Range(std::vector<float>& Values, float& Low, float& High)
    {
        std::sort(Values.begin(), Values.end());
        const size_t Trim = Values.size() >= 50 ? Values.size() / 50 : 0;
        Low = Values[Trim]; High = Values[Values.size()-1-Trim];
    }

    float MassWeight(EBodyRole Role, int SpineCount)
    {
        switch (Role)
        {
        case EBodyRole::Pelvis: return 0.16f;
        case EBodyRole::Spine: return 0.30f / std::max(SpineCount,1);
        case EBodyRole::Neck: return 0.03f;
        case EBodyRole::Head: return 0.08f;
        case EBodyRole::UpperArm: return 0.04f;
        case EBodyRole::Forearm: return 0.025f;
        case EBodyRole::Hand: return 0.015f;
        case EBodyRole::Thigh: return 0.10f;
        case EBodyRole::Calf: return 0.05f;
        case EBodyRole::Foot: return 0.025f;
        default: return 0.03f;
        }
    }
}

void PhysicsAssetBuilder::DestroyGeneratedAsset(UPhysicsAsset* Asset)
{
    if (!Asset) return;
    for (auto* Joint : Asset->PhysicsConstraint) delete Joint;
    for (auto* Body : Asset->BodySetups) delete Body;
    delete Asset;
}

UPhysicsAsset* PhysicsAssetBuilder::CreateFromSkeleton(USkeletalMesh& Mesh,
    const FAutoGenerateSettings& Settings, FString& Error)
{
    Error.clear();
    const auto& Bones = Mesh.GetRefSkeleton();
    if (Bones.IsEmpty() || !std::isfinite(Settings.MinBoneLengthRatio) ||
        Settings.MinBoneLengthRatio < 0 || Settings.MinBoneLengthRatio > 1 ||
        !std::isfinite(Settings.RadiusRatio) || Settings.RadiusRatio <= 0 || Settings.RadiusRatio > 0.5f ||
        !std::isfinite(Settings.MinVertexWeight) || Settings.MinVertexWeight <= 0 || Settings.MinVertexWeight > 1 ||
        !std::isfinite(Settings.TotalMassKg) || Settings.TotalMassKg <= 0)
    {
        Error = "Choose a valid skeleton, minimum bone length (0-100%) and capsule radius (0-50%).";
        return nullptr;
    }

    float Longest = 0;
    TArray<FMatrix> Reference;
    Reference.SetNum(Bones.Num());
    for (int I = 0; I < Bones.Num(); ++I)
    {
        const auto& Bone = Bones[I];
        if (Bone.ParentIndex < -1 || Bone.ParentIndex >= I || !Finite(Bone.LocalBindPose.Location))
        {
            Error = "The skeleton contains an invalid parent or bone position.";
            return nullptr;
        }
        Reference[I] = Bone.LocalBindPose.GetLocalMatrix();
        if (Bone.ParentIndex >= 0)
        {
            Reference[I] = Reference[I] * Reference[Bone.ParentIndex];
            Longest = std::max(Longest, Bone.LocalBindPose.Location.Size());
        }
        if (!std::isfinite(Reference[I].Determinant()) || std::abs(Reference[I].Determinant()) < 1e-12f)
        {
            Error = "The skeleton contains an invalid bind transform.";
            return nullptr;
        }
    }

    const float Minimum = std::max(Longest * Settings.MinBoneLengthRatio, 0.0001f);
    const auto& LODs = Mesh.GetRenderData().LODRenderData;
    auto* Asset = NewObject<UPhysicsAsset>(&Mesh);
    TArray<int32> BodyIndices;
    BodyIndices.SetNum(Bones.Num());
    for (int I = 0; I < Bones.Num(); ++I) BodyIndices[I] = -1;

    std::vector<EBodyRole> Roles(Bones.Num(), EBodyRole::None);
    std::vector<std::vector<FVector>> Points(Bones.Num());
    if (Settings.bHumanoid)
    {
        for (int I = 0; I < Bones.Num(); ++I) Roles[I] = Role(Bones[I].Name);
        if (!LODs.IsEmpty())
        {
            const auto& LOD = LODs[0];
            for (int V = 0; V < LOD.BindVertices.Num() && V < LOD.SkinWeights.Num(); ++V)
            {
                const auto& Weights = LOD.SkinWeights[V];
                int Best = 0;
                for (int W = 1; W < MaxBoneInfluences; ++W)
                    if (Weights.Weight[W] > Weights.Weight[Best]) Best = W;
                int Bone = Weights.BoneIndex[Best];
                if (Bone >= Bones.Num() || !std::isfinite(Weights.Weight[Best]) || Weights.Weight[Best] < Settings.MinVertexWeight) continue;
                // Fingers/toes belong to the hand/foot. Hair and clothing do not
                // inflate head/chest bodies by being merged into their ancestors.
                if (Roles[Bone] == EBodyRole::None && Digit(NormalName(Bones[Bone].Name)))
                    while (Bone >= 0 && Roles[Bone] == EBodyRole::None) Bone = Bones[Bone].ParentIndex;
                if (Bone < 0 || Roles[Bone] == EBodyRole::None || !Finite(LOD.BindVertices[V].Position)) continue;
                Points[Bone].push_back(Reference[Bone].Inverse().TransformPosition(LOD.BindVertices[V].Position));
            }
        }
        // A helper spine without supported vertices must not become a tiny, light
        // link between heavy torso bodies. Keep the bone, but skip its rigid body.
        if (!LODs.IsEmpty() && !LODs[0].BindVertices.IsEmpty())
            for (int I=0; I<Bones.Num(); ++I)
                if (Roles[I] == EBodyRole::Spine && Points[I].size() < 4) Roles[I] = EBodyRole::None;
    }

    for (int I = 0; I < Bones.Num(); ++I)
    {
        if (Settings.bHumanoid && Roles[I] == EBodyRole::None) continue;
        FVector End = FVector::ZeroVector;
        bool HasChild = false;
        for (int Child = I + 1; Child < Bones.Num(); ++Child)
            if (!Settings.bHumanoid && Bones[Child].ParentIndex == I)
            {
                HasChild = true;
                if (Bones[Child].LocalBindPose.Location.SizeSquared() > End.SizeSquared())
                    End = Bones[Child].LocalBindPose.Location;
            }

        if (Settings.bHumanoid)
        {
            int Chosen = -1;
            for (int Child = I + 1; Child < Bones.Num(); ++Child)
            {
                if (Roles[Child] == EBodyRole::None) continue;
                int Parent = Bones[Child].ParentIndex;
                while (Parent >= 0 && Roles[Parent] == EBodyRole::None) Parent = Bones[Parent].ParentIndex;
                if (Parent != I) continue;
                if (Chosen < 0) Chosen = Child;
                // Continue up the torso instead of following a shoulder or leg.
                if (Roles[Child] == EBodyRole::Spine || Roles[Child] == EBodyRole::Neck || Roles[Child] == EBodyRole::Head)
                { Chosen = Child; break; }
            }
            if (Chosen >= 0)
            {
                End = Reference[I].Inverse().TransformPosition(Reference[Chosen].TransformPosition(FVector::ZeroVector));
                HasChild = true;
            }
            else if (Bones[I].ParentIndex >= 0)
            {
                // Terminal hands, feet and head still need a useful fallback size.
                End = FVector(0,0,Bones[I].LocalBindPose.Location.Size() * 0.6f);
            }
        }

        FVector Center = End * 0.5f;
        // Leaf bones have no outgoing segment. Fit their weighted vertex bounds instead;
        // unweighted end markers do not receive tiny, arbitrary bodies.
        if (!Settings.bHumanoid && !HasChild && !LODs.IsEmpty() && I < LODs[0].BoneBounds.Num())
        {
            const auto& Bounds = LODs[0].BoneBounds[I];
            if (Finite(Bounds.Min) && Finite(Bounds.Max) &&
                Bounds.Min.X <= Bounds.Max.X && Bounds.Min.Y <= Bounds.Max.Y && Bounds.Min.Z <= Bounds.Max.Z)
            {
                const auto Local = Bounds.GetWorldAABB(Reference[I].Inverse());
                const FVector Size = Local.Max - Local.Min;
                int Axis = 0;
                if (Size.Y > Size[Axis]) Axis = 1;
                if (Size.Z > Size[Axis]) Axis = 2;
                End = FVector::ZeroVector;
                End[Axis] = Size[Axis];
                Center = (Local.Min + Local.Max) * 0.5f;
            }
        }
        const float Length = End.Size();
        if (!std::isfinite(Length) || !Finite(Center) || (!Settings.bHumanoid && Length < Minimum)) continue;

        const float FallbackLength = std::max(Length, std::max(Longest * 0.05f, 0.01f));
        FVector Axis = Length > 0.0001f ? End / Length : FVector(0,0,1);
        const bool Box = Settings.bHumanoid && (Roles[I] == EBodyRole::Pelvis || Roles[I] == EBodyRole::Spine ||
            Roles[I] == EBodyRole::Hand || Roles[I] == EBodyRole::Foot);
        if (Box) Axis = FVector(0,0,1);
        const FQuat Orientation = CapsuleRotation(Axis);
        const FVector U = Orientation.RotateVector(FVector(1,0,0));
        const FVector V = Orientation.RotateVector(FVector(0,1,0));
        FVector Size(FallbackLength * 0.4f, FallbackLength * 0.4f, FallbackLength);
        if (Settings.bHumanoid && Points[I].size() >= 4)
        {
            std::vector<float> Coordinates[3];
            for (const auto& P : Points[I])
            {
                Coordinates[0].push_back(P.Dot(U));
                Coordinates[1].push_back(P.Dot(V));
                Coordinates[2].push_back(P.Dot(Axis));
            }
            FVector Low, High;
            for (int A=0; A<3; ++A) Range(Coordinates[A], Low[A], High[A]);
            const FVector Middle = (Low+High)*0.5f;
            Center = U*Middle.X + V*Middle.Y + Axis*Middle.Z;
            Size = (High-Low)*0.9f;
            for (int A=0; A<3; ++A) Size[A] = std::max(Size[A], FallbackLength*0.04f);
        }

        auto* Body = NewObject<UBodySetup>(Asset);
        Body->BoneName = FName(Bones[I].Name);
        Body->PhysicsType = EPhysicsType::Simulated;
        FKSphylElem Capsule;
        Capsule.Center = Center;
        Capsule.Radius = Settings.bHumanoid ? std::max(Size.X,Size.Y)*0.5f : Length * Settings.RadiusRatio;
        Capsule.Length = std::max((Settings.bHumanoid ? Size.Z : Length) - 2 * Capsule.Radius, 0.0001f);
        Capsule.Rotation = Orientation.ToFRotator();
        if (Box)
        {
            FKBoxElem Shape; Shape.Center = Center; Shape.X = Size.X; Shape.Y = Size.Y; Shape.Z = Size.Z;
            Body->AggGeom.BoxElems.Add(Shape);
        }
        else if (Settings.bHumanoid && Roles[I] == EBodyRole::Head)
        {
            // Fit the head core: one wide spike/ear axis should not inflate all
            // three sphere axes. The median dimension tolerates that protrusion.
            float Dimensions[] = {Size.X,Size.Y,Size.Z};
            std::sort(std::begin(Dimensions),std::end(Dimensions));
            FKSphereElem Shape; Shape.Center = Center; Shape.Radius = Dimensions[1]*0.5f;
            Body->AggGeom.SphereElems.Add(Shape);
        }
        else Body->AggGeom.SphylElems.Add(Capsule);
        BodyIndices[I] = Asset->BodySetups.Num();
        Asset->BodySetups.Add(Body);
    }

    if (Asset->BodySetups.IsEmpty())
    {
        delete Asset;
        Error = Settings.bHumanoid ? "No recognized humanoid bones. Choose Skeleton mode or check the bone names." :
            "No usable bone segments were found. Lower the minimum bone length or check the skeleton.";
        return nullptr;
    }
    Asset->UpdateBodySetupIndexMap();
    if (Settings.bHumanoid)
    {
        int SpineCount = 0;
        for (int I=0; I<Bones.Num(); ++I) if (BodyIndices[I]>=0 && Roles[I]==EBodyRole::Spine) ++SpineCount;
        float TotalWeight = 0;
        for (int I=0; I<Bones.Num(); ++I) if (BodyIndices[I]>=0) TotalWeight += MassWeight(Roles[I],SpineCount);
        for (int I=0; I<Bones.Num(); ++I)
            if (BodyIndices[I]>=0) Asset->BodySetups[BodyIndices[I]]->MassOverrideKg = Settings.TotalMassKg * MassWeight(Roles[I],SpineCount) / TotalWeight;
    }
    for (int I = 0; I < Bones.Num(); ++I)
    {
        if (BodyIndices[I] < 0) continue;
        int Parent = Bones[I].ParentIndex;
        while (Parent >= 0 && BodyIndices[Parent] < 0) Parent = Bones[Parent].ParentIndex;
        if (Parent < 0) continue;

        auto* Joint = NewObject<UPhysicsConstraintTemplate>(Asset);
        Joint->ConstraintBone1 = FName(Bones[I].Name);
        Joint->ConstraintBone2 = FName(Bones[Parent].Name);
        FMatrix Relative = FMatrix::Identity;
        FQuat Rotation = FQuat::Identity();
        for (int Bone = I; Bone != Parent; Bone = Bones[Bone].ParentIndex)
        {
            Relative = Relative * Bones[Bone].LocalBindPose.GetLocalMatrix();
            Rotation = Bones[Bone].LocalBindPose.Rotation * Rotation;
        }
        // Both frames coincide in bind pose, including any skipped helper bones.
        Joint->Pos2 = Relative.TransformPosition(FVector::ZeroVector);
        Joint->PriAxis2 = Rotation.Normalized().RotateVector(Joint->PriAxis1);
        Joint->SecAxis2 = Rotation.Normalized().RotateVector(Joint->SecAxis1);
        Asset->PhysicsConstraint.Add(Joint);
        Asset->DisableCollision(BodyIndices[I], BodyIndices[Parent]);
    }
    if (Settings.bHumanoid)
    {
        // Grandparent and sibling bodies meet around the same joint region.
        // Suppress these local overlaps while retaining distant self collision.
        for (const auto* A : Asset->PhysicsConstraint)
            for (const auto* B : Asset->PhysicsConstraint)
            {
                if (A == B) continue;
                if (A->ConstraintBone2 == B->ConstraintBone1)
                    Asset->DisableCollision(Asset->FindBodyIndex(A->ConstraintBone1), Asset->FindBodyIndex(B->ConstraintBone2));
                if (A->ConstraintBone2 == B->ConstraintBone2)
                    Asset->DisableCollision(Asset->FindBodyIndex(A->ConstraintBone1), Asset->FindBodyIndex(B->ConstraintBone1));
            }
    }
    return Asset;
}

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
