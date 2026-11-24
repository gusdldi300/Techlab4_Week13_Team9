#include "EnginePCH.h"
#include "PhysicsAssetEditModel.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/BodySetup.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <cmath>

namespace
{
    FBox FitBounds(const USkeletalMesh& Mesh, int32 BoneIndex)
    {
        const auto& Bones = Mesh.GetRefSkeleton();
        FMatrix Reference = FMatrix::Identity;
        for (int32 I = BoneIndex; I >= 0; I = Bones[I].ParentIndex)
            Reference = Reference * Bones[I].LocalBindPose.GetLocalMatrix();
        const auto& LODs = Mesh.GetRenderData().LODRenderData;
        if (!LODs.IsEmpty() && BoneIndex < LODs[0].BoneBounds.Num() && std::abs(Reference.Determinant()) > 1e-12f)
        {
            const FBox& Bounds = LODs[0].BoneBounds[BoneIndex];
            if (Bounds.Min.X <= Bounds.Max.X && Bounds.Min.Y <= Bounds.Max.Y && Bounds.Min.Z <= Bounds.Max.Z)
                return Bounds.GetWorldAABB(Reference.Inverse());
        }
        // 가중 정점이 없는 본은 자식까지의 구간을 초기 배치 범위로 사용한다.
        FVector End = FVector::ZeroVector;
        for (int32 I = 0; I < Bones.Num(); ++I)
            if (Bones[I].ParentIndex == BoneIndex && Bones[I].LocalBindPose.Location.Size() > 0.0001f)
            {
                End = Bones[I].LocalBindPose.Location;
                break;
            }
        const float Radius = std::max(End.Size() * 0.15f, 0.01f);
        return FBox{ FVector(std::min(End.X, 0.0f), std::min(End.Y, 0.0f), std::min(End.Z, 0.0f)) - FVector(Radius),
            FVector(std::max(End.X, 0.0f), std::max(End.Y, 0.0f), std::max(End.Z, 0.0f)) + FVector(Radius) };
    }
}

void FPhysicsAssetEditModel::ResetHistory()
{
    UndoStack.Empty(); RedoStack.Empty();
    CurrentText.clear(); CommittedText.clear(); SavedText.clear(); Error.clear();
    if (Mesh.GetPhysicsAsset() && Capture()) CommittedText = SavedText = CurrentText;
}

bool FPhysicsAssetEditModel::GetShapeTransform(int32 BoneIndex, ECollisionShape Type, int32 Index, FTransform& Out) const
{
    const UBodySetup* Body = GetBody(BoneIndex);
    if (!Body || Index < 0) return false;
    const auto& G = Body->AggGeom;
    if (Type == ECollisionShape::Sphere && Index < G.SphereElems.Num()) Out = G.SphereElems[Index].GetTransform();
    else if (Type == ECollisionShape::Box && Index < G.BoxElems.Num()) Out = G.BoxElems[Index].GetTransform();
    else if (Type == ECollisionShape::Sphyl && Index < G.SphylElems.Num()) Out = G.SphylElems[Index].GetTransform();
    else return false;
    return true;
}

bool FPhysicsAssetEditModel::SetShapeTransform(int32 BoneIndex, ECollisionShape Type, int32 Index, const FTransform& Pose)
{
    FTransform Previous;
    if (!GetShapeTransform(BoneIndex, Type, Index, Previous)) return false;
    const float Norm = Pose.Rotation.X*Pose.Rotation.X + Pose.Rotation.Y*Pose.Rotation.Y +
        Pose.Rotation.Z*Pose.Rotation.Z + Pose.Rotation.W*Pose.Rotation.W;
    if (!std::isfinite(Norm) || Norm < 1e-12f || Pose.Scale != FVector(1,1,1))
    {
        Error = "The collision shape requires a finite rotation and unit pose scale.";
        return false;
    }
    UBodySetup& Body = *GetBody(BoneIndex);
    auto G = Body.AggGeom;
    if (Type == ECollisionShape::Sphere) G.SphereElems[Index].Center = Pose.Location;
    else if (Type == ECollisionShape::Box)
    {
        G.BoxElems[Index].Center = Pose.Location;
        G.BoxElems[Index].Rotation = Pose.Rotation.Normalized().ToFRotator();
    }
    else
    {
        G.SphylElems[Index].Center = Pose.Location;
        G.SphylElems[Index].Rotation = Pose.Rotation.Normalized().ToFRotator();
    }
    return SetBodyProperties(BoneIndex, Body.PhysicsType, G);
}

bool FPhysicsAssetEditModel::Capture()
{
    FString Text;
    if (!Mesh.GetPhysicsAsset() || !PhysicsAssetFile::ToText(MeshPath, Mesh, *Mesh.GetPhysicsAsset(), Text, Error)) return false;
    CurrentText = std::move(Text);
    return true;
}

UBodySetup* FPhysicsAssetEditModel::GetBody(int32 BoneIndex) const
{
    if (!Mesh.GetPhysicsAsset() || BoneIndex < 0 || BoneIndex >= Mesh.GetNumBones()) return nullptr;
    const FName Name(Mesh.GetRefSkeleton()[BoneIndex].Name);
    for (UBodySetup* Body : Mesh.GetPhysicsAsset()->BodySetups)
        if (Body && Body->BoneName == Name) return Body;
    return nullptr;
}

void FPhysicsAssetEditModel::PushHistory(TArray<FString>& Stack, const FString& Text)
{
    Stack.Add(Text);
    size_t Bytes = 0;
    for (const auto& Item : Stack) Bytes += Item.size();
    while (Stack.Num() > 1 && (Stack.Num() > 100 || Bytes > 64 * 1024 * 1024))
    {
        Bytes -= Stack[0].size();
        Stack.RemoveAt(0);
    }
}

void FPhysicsAssetEditModel::Commit(bool bInteractionActive)
{
    if (bInteractionActive || CurrentText == CommittedText) return;
    PushHistory(UndoStack, CommittedText);
    CommittedText = CurrentText;
    RedoStack.Empty();
}

bool FPhysicsAssetEditModel::Apply(const FString& Text)
{
    UPhysicsAsset* Asset = Mesh.GetPhysicsAsset();
    if (!PhysicsAssetFile::ApplyText(Text, Mesh, Asset, Error)) return false;
    Mesh.SetPhysicsAsset(Asset);
    CurrentText = CommittedText = Text;
    return true;
}

bool FPhysicsAssetEditModel::Undo()
{
    Commit(false);
    if (UndoStack.IsEmpty()) return false;
    const FString Previous = UndoStack.Last();
    const FString Current = CommittedText;
    if (!Apply(Previous)) return false;
    UndoStack.RemoveLast();
    PushHistory(RedoStack, Current);
    return true;
}

bool FPhysicsAssetEditModel::Redo()
{
    if (!CanRedo()) return false;
    const FString Next = RedoStack.Last();
    const FString Current = CommittedText;
    if (!Apply(Next)) return false;
    RedoStack.RemoveLast();
    PushHistory(UndoStack, Current);
    return true;
}

void FPhysicsAssetEditModel::MarkSaved()
{
    Commit(false);
    SavedText = CurrentText;
}

bool FPhysicsAssetEditModel::RestoreSaved()
{
    if (!Apply(SavedText)) return false;
    UndoStack.Empty(); RedoStack.Empty();
    return true;
}

bool FPhysicsAssetEditModel::SetBodyProperties(int32 BoneIndex, EPhysicsType Type, const FKAggregateGeom& Geometry)
{
    UBodySetup* Body = GetBody(BoneIndex);
    if (!Body) return false;
    const auto OldGeometry = Body->AggGeom;
    const auto OldType = Body->PhysicsType;
    Body->AggGeom = Geometry; Body->PhysicsType = Type;
    if (Capture()) return true;
    Body->AggGeom = OldGeometry; Body->PhysicsType = OldType;
    return false;
}

bool FPhysicsAssetEditModel::AddShape(int32 BoneIndex, ECollisionShape Type)
{
    if (!Mesh.GetPhysicsAsset() || BoneIndex < 0 || BoneIndex >= Mesh.GetNumBones()
        || (Type != ECollisionShape::Sphere && Type != ECollisionShape::Box && Type != ECollisionShape::Sphyl)) return false;
    Commit(false);
    const FBox Bounds = FitBounds(Mesh, BoneIndex);
    const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
    FVector Size = Bounds.Max - Bounds.Min;
    Size.X = std::max(Size.X, 0.02f); Size.Y = std::max(Size.Y, 0.02f); Size.Z = std::max(Size.Z, 0.02f);
    UBodySetup* Body = GetBody(BoneIndex);
    const bool bNewBody = Body == nullptr;
    if (bNewBody)
    {
        Body = NewObject<UBodySetup>(Mesh.GetPhysicsAsset());
        Body->BoneName = FName(Mesh.GetRefSkeleton()[BoneIndex].Name);
        Body->PhysicsType = EPhysicsType::Simulated;
        Mesh.GetPhysicsAsset()->BodySetups.Add(Body);
    }
    FKAggregateGeom Geometry = Body->AggGeom;
    if (Type == ECollisionShape::Sphere)
    {
        FKSphereElem Sphere; Sphere.Center = Center; Sphere.Radius = std::max({Size.X, Size.Y, Size.Z}) * 0.5f;
        Geometry.SphereElems.Add(Sphere);
    }
    else if (Type == ECollisionShape::Box)
    {
        FKBoxElem Box; Box.Center = Center; Box.X = Size.X; Box.Y = Size.Y; Box.Z = Size.Z;
        Geometry.BoxElems.Add(Box);
    }
    else
    {
        FKSphylElem Capsule; Capsule.Center = Center;
        float Length = Size.Z;
        Capsule.Radius = std::max(Size.X, Size.Y) * 0.5f;
        if (Size.X > Size.Y && Size.X > Size.Z)
        {
            Capsule.Rotation = FQuat::MakeFromAxisAngle(FVector(0, 1, 0), PI * 0.5f).ToFRotator();
            Capsule.Radius = std::max(Size.Y, Size.Z) * 0.5f; Length = Size.X;
        }
        else if (Size.Y > Size.Z)
        {
            Capsule.Rotation = FQuat::MakeFromAxisAngle(FVector(1, 0, 0), -PI * 0.5f).ToFRotator();
            Capsule.Radius = std::max(Size.X, Size.Z) * 0.5f; Length = Size.Y;
        }
        Capsule.Length = std::max(Length - Capsule.Radius * 2.0f, 0.0f);
        Geometry.SphylElems.Add(Capsule);
    }
    if (!SetBodyProperties(BoneIndex, Body->PhysicsType, Geometry))
    {
        if (bNewBody) { Mesh.GetPhysicsAsset()->BodySetups.RemoveLast(); delete Body; }
        return false;
    }
    Mesh.GetPhysicsAsset()->UpdateBodySetupIndexMap();
    Commit(false);
    return true;
}

bool FPhysicsAssetEditModel::RemoveBody(int32 BoneIndex)
{
    UBodySetup* Body = GetBody(BoneIndex);
    if (!Body || !Capture()) return false;
    Commit(false);
    const FString BoneName = Body->BoneName.ToString();
    json Data = json::parse(CurrentText);
    auto& Bodies = Data["Bodies"];
    int32 Index = -1;
    for (int32 I = 0; I < static_cast<int32>(Bodies.size()); ++I)
        if (Bodies[I]["Bone"] == BoneName) { Index = I; break; }
    if (Index < 0) return false;
    Bodies.erase(Bodies.begin() + Index);
    auto& Constraints = Data["Constraints"];
    for (auto It = Constraints.begin(); It != Constraints.end();)
        if ((*It)["Child"] == BoneName || (*It)["Parent"] == BoneName) It = Constraints.erase(It);
        else ++It;
    auto& Pairs = Data["DisabledCollisions"];
    for (auto It = Pairs.begin(); It != Pairs.end();)
    {
        int A = (*It)[0].get<int>(), B = (*It)[1].get<int>();
        if (A == Index || B == Index) { It = Pairs.erase(It); continue; }
        (*It)[0] = A > Index ? A - 1 : A;
        (*It)[1] = B > Index ? B - 1 : B;
        ++It;
    }
    UPhysicsAsset* Asset = Mesh.GetPhysicsAsset();
    if (!PhysicsAssetFile::ApplyText(Data.dump(), Mesh, Asset, Error)) return false;
    if (!Capture()) return false;
    Commit(false);
    return true;
}

bool FPhysicsAssetEditModel::RemoveShape(int32 BoneIndex, ECollisionShape Type, int32 ShapeIndex)
{
    UBodySetup* Body = GetBody(BoneIndex);
    if (!Body || ShapeIndex < 0) return false;
    auto Geometry = Body->AggGeom;
    if (Type == ECollisionShape::Sphere && ShapeIndex < Geometry.SphereElems.Num()) Geometry.SphereElems.RemoveAt(ShapeIndex);
    else if (Type == ECollisionShape::Box && ShapeIndex < Geometry.BoxElems.Num()) Geometry.BoxElems.RemoveAt(ShapeIndex);
    else if (Type == ECollisionShape::Sphyl && ShapeIndex < Geometry.SphylElems.Num()) Geometry.SphylElems.RemoveAt(ShapeIndex);
    else return false;
    if (Geometry.GetElementCount() == 0) return RemoveBody(BoneIndex);
    Commit(false);
    if (!SetBodyProperties(BoneIndex, Body->PhysicsType, Geometry)) return false;
    Commit(false);
    return true;
}

namespace
{
    FQuat ConstraintRotation(const FVector& X, const FVector& Y)
    {
        const FVector Z = X.Cross(Y);
        const float M[3][3] = {{X.X, X.Y, X.Z}, {Y.X, Y.Y, Y.Z}, {Z.X, Z.Y, Z.Z}};
        FQuat Q;
        const float Trace = M[0][0] + M[1][1] + M[2][2];
        if (Trace > 0)
        {
            const float S = std::sqrt(Trace + 1.0f) * 2.0f;
            Q = FQuat((M[1][2] - M[2][1]) / S, (M[2][0] - M[0][2]) / S,
                (M[0][1] - M[1][0]) / S, S * 0.25f);
        }
        else
        {
            int I = 0;
            if (M[1][1] > M[I][I]) I = 1;
            if (M[2][2] > M[I][I]) I = 2;
            const int J = (I + 1) % 3, K = (J + 1) % 3;
            const float S = std::sqrt(std::max(1.0f + M[I][I] - M[J][J] - M[K][K], 0.0f)) * 2.0f;
            float V[4] = {};
            V[I] = S * 0.25f;
            V[J] = (M[I][J] + M[J][I]) / S;
            V[K] = (M[I][K] + M[K][I]) / S;
            V[3] = (M[J][K] - M[K][J]) / S;
            Q = FQuat(V[0], V[1], V[2], V[3]);
        }
        return Q.Normalized();
    }

    bool FrameIsRigid(const FTransform& Frame)
    {
        const FQuat Q = Frame.Rotation;
        const float Norm = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
        return std::isfinite(Norm) && Norm > 1e-12f &&
            std::isfinite(Frame.Location.X + Frame.Location.Y + Frame.Location.Z) &&
            Frame.Scale == FVector(1, 1, 1);
    }

    void WriteConstraintSettings(UPhysicsConstraintTemplate& Joint, const FPhysicsConstraintSettings& S)
    {
        Joint.Pos1 = S.ChildFrame.Location;
        Joint.Pos2 = S.ParentFrame.Location;
        Joint.PriAxis1 = S.ChildFrame.Rotation.Normalized().RotateVector(FVector(1, 0, 0));
        Joint.SecAxis1 = S.ChildFrame.Rotation.Normalized().RotateVector(FVector(0, 1, 0));
        Joint.PriAxis2 = S.ParentFrame.Rotation.Normalized().RotateVector(FVector(1, 0, 0));
        Joint.SecAxis2 = S.ParentFrame.Rotation.Normalized().RotateVector(FVector(0, 1, 0));
#define COPY_FIELD(Name) Joint.Name = S.Name;
        COPY_FIELD(XMotion) COPY_FIELD(YMotion) COPY_FIELD(ZMotion) COPY_FIELD(LinearLimit)
        COPY_FIELD(Swing1Motion) COPY_FIELD(Swing2Motion) COPY_FIELD(TwistMotion)
        COPY_FIELD(Swing1LimitDegrees) COPY_FIELD(Swing2LimitDegrees) COPY_FIELD(TwistLimitDegrees)
#undef COPY_FIELD
    }

    uint64 ConstraintPairKey(int A, int B)
    {
        if (A > B) std::swap(A, B);
        return (static_cast<uint64>(A) << 32) | static_cast<uint32>(B);
    }

    void SetPairCollision(UPhysicsAsset& Asset, int ChildBone, int ParentBone, const USkeletalMesh& Mesh, bool bDisabled)
    {
        const int A = Asset.FindBodyIndex(FName(Mesh.GetRefSkeleton()[ChildBone].Name));
        const int B = Asset.FindBodyIndex(FName(Mesh.GetRefSkeleton()[ParentBone].Name));
        if (bDisabled) Asset.DisableCollision(A, B);
        else Asset.CollisionDisableTable.Remove(ConstraintPairKey(A, B));
    }

    FTransform ParentFrameFromChild(const USkeletalMesh& Mesh, int Child, int Parent, const FTransform& ChildFrame)
    {
        FMatrix Relative = FMatrix::Identity;
        FQuat Rotation = FQuat::Identity();
        const auto& Bones = Mesh.GetRefSkeleton();
        for (int I = Child; I != Parent; I = Bones[I].ParentIndex)
        {
            Relative = Relative * Bones[I].LocalBindPose.GetLocalMatrix();
            Rotation = Bones[I].LocalBindPose.Rotation * Rotation;
        }
        return FTransform((Rotation * ChildFrame.Rotation).Normalized(), Relative.TransformPosition(ChildFrame.Location));
    }
}

bool FPhysicsAssetEditModel::CanConnect(int32 ChildBone, int32 ParentBone) const
{
    if (ChildBone == ParentBone || !GetBody(ChildBone) || !GetBody(ParentBone)) return false;
    for (int I = Mesh.GetRefSkeleton()[ChildBone].ParentIndex; I >= 0; I = Mesh.GetRefSkeleton()[I].ParentIndex)
        if (I == ParentBone) return true;
    return false;
}

int32 FPhysicsAssetEditModel::FindConstraint(int32 ChildBone) const
{
    if (!Mesh.GetPhysicsAsset() || ChildBone < 0 || ChildBone >= Mesh.GetNumBones()) return -1;
    const FName Name(Mesh.GetRefSkeleton()[ChildBone].Name);
    for (int I = 0; I < Mesh.GetPhysicsAsset()->PhysicsConstraint.Num(); ++I)
    {
        const auto* Joint = Mesh.GetPhysicsAsset()->PhysicsConstraint[I];
        if (Joint && Joint->ConstraintBone1 == Name) return I;
    }
    return -1;
}

int32 FPhysicsAssetEditModel::GetConstraintParent(int32 ChildBone) const
{
    const int Index = FindConstraint(ChildBone);
    return Index < 0 ? -1 : Mesh.FindBoneIndex(Mesh.GetPhysicsAsset()->PhysicsConstraint[Index]->ConstraintBone2.ToString());
}

bool FPhysicsAssetEditModel::GetConstraintSettings(int32 ChildBone, FPhysicsConstraintSettings& Out) const
{
    const int Index = FindConstraint(ChildBone);
    if (Index < 0) return false;
    const auto& Joint = *Mesh.GetPhysicsAsset()->PhysicsConstraint[Index];
    Out.ChildFrame = FTransform(ConstraintRotation(Joint.PriAxis1, Joint.SecAxis1), Joint.Pos1);
    Out.ParentFrame = FTransform(ConstraintRotation(Joint.PriAxis2, Joint.SecAxis2), Joint.Pos2);
#define COPY_FIELD(Name) Out.Name = Joint.Name;
    COPY_FIELD(XMotion) COPY_FIELD(YMotion) COPY_FIELD(ZMotion) COPY_FIELD(LinearLimit)
    COPY_FIELD(Swing1Motion) COPY_FIELD(Swing2Motion) COPY_FIELD(TwistMotion)
    COPY_FIELD(Swing1LimitDegrees) COPY_FIELD(Swing2LimitDegrees) COPY_FIELD(TwistLimitDegrees)
#undef COPY_FIELD
    const int A = Mesh.GetPhysicsAsset()->FindBodyIndex(Joint.ConstraintBone1);
    const int B = Mesh.GetPhysicsAsset()->FindBodyIndex(Joint.ConstraintBone2);
    Out.bDisableCollision = !Mesh.GetPhysicsAsset()->IsCollisionEnabled(A, B);
    return true;
}

bool FPhysicsAssetEditModel::AddConstraint(int32 ChildBone, int32 ParentBone)
{
    Error.clear();
    if (!CanConnect(ChildBone, ParentBone)) { Error = "Choose an ancestor bone with a physics body as the parent."; return false; }
    if (FindConstraint(ChildBone) >= 0) { Error = "This body already has a parent constraint."; return false; }
    Commit(false);
    UPhysicsAsset& Asset = *Mesh.GetPhysicsAsset();
    FPhysicsConstraintSettings S;
    S.ParentFrame = ParentFrameFromChild(Mesh, ChildBone, ParentBone, S.ChildFrame);
    auto* Joint = NewObject<UPhysicsConstraintTemplate>(&Asset);
    Joint->ConstraintBone1 = GetBody(ChildBone)->BoneName;
    Joint->ConstraintBone2 = GetBody(ParentBone)->BoneName;
    WriteConstraintSettings(*Joint, S);
    Asset.PhysicsConstraint.Add(Joint);
    const auto PreviousPairs = Asset.CollisionDisableTable;
    SetPairCollision(Asset, ChildBone, ParentBone, Mesh, true);
    if (!Capture())
    {
        Asset.PhysicsConstraint.RemoveLast(); delete Joint;
        Asset.CollisionDisableTable = PreviousPairs;
        return false;
    }
    Commit(false);
    return true;
}

bool FPhysicsAssetEditModel::RemoveConstraint(int32 ChildBone)
{
    const int Index = FindConstraint(ChildBone);
    if (Index < 0) return false;
    Commit(false);
    auto& Joints = Mesh.GetPhysicsAsset()->PhysicsConstraint;
    auto* Joint = Joints[Index];
    Joints.RemoveAt(Index);
    delete Joint;
    // Pair collision policy is separate from the lifetime of the joint.
    if (!Capture()) return false;
    Commit(false);
    return true;
}

bool FPhysicsAssetEditModel::SetConstraintSettings(int32 ChildBone, const FPhysicsConstraintSettings& Settings)
{
    Error.clear();
    const int Index = FindConstraint(ChildBone);
    if (Index < 0) return false;
    if (!FrameIsRigid(Settings.ChildFrame) || !FrameIsRigid(Settings.ParentFrame))
    {
        Error = "Constraint frames require finite positions, valid rotations and unit scale.";
        return false;
    }
    UPhysicsAsset& Asset = *Mesh.GetPhysicsAsset();
    auto& Joint = *Asset.PhysicsConstraint[Index];
    FPhysicsConstraintSettings Previous;
    GetConstraintSettings(ChildBone, Previous);
    const FVector PreviousPrimary1 = Joint.PriAxis1, PreviousSecondary1 = Joint.SecAxis1;
    const FVector PreviousPrimary2 = Joint.PriAxis2, PreviousSecondary2 = Joint.SecAxis2;
    const auto PreviousPairs = Asset.CollisionDisableTable;
    WriteConstraintSettings(Joint, Settings);
    SetPairCollision(Asset, ChildBone, GetConstraintParent(ChildBone), Mesh, Settings.bDisableCollision);
    if (Capture()) return true;
    WriteConstraintSettings(Joint, Previous);
    // Invalid input must leave the original axes bit-for-bit unchanged.
    Joint.PriAxis1 = PreviousPrimary1; Joint.SecAxis1 = PreviousSecondary1;
    Joint.PriAxis2 = PreviousPrimary2; Joint.SecAxis2 = PreviousSecondary2;
    Asset.CollisionDisableTable = PreviousPairs;
    return false;
}

bool FPhysicsAssetEditModel::AlignParentFrame(int32 ChildBone)
{
    FPhysicsConstraintSettings S;
    if (!GetConstraintSettings(ChildBone, S)) return false;
    const int Parent = GetConstraintParent(ChildBone);
    if (!CanConnect(ChildBone, Parent)) return false;
    Commit(false);
    S.ParentFrame = ParentFrameFromChild(Mesh, ChildBone, Parent, S.ChildFrame);
    if (!SetConstraintSettings(ChildBone, S)) return false;
    Commit(false);
    return true;
}

bool FPhysicsAssetEditModel::ResetConstraintFrames(int32 ChildBone)
{
    FPhysicsConstraintSettings S;
    if (!GetConstraintSettings(ChildBone, S)) return false;
    const int Parent = GetConstraintParent(ChildBone);
    if (!CanConnect(ChildBone, Parent)) return false;
    Commit(false);
    S.ChildFrame = FTransform::Identity;
    S.ParentFrame = ParentFrameFromChild(Mesh, ChildBone, Parent, S.ChildFrame);
    if (!SetConstraintSettings(ChildBone, S)) return false;
    Commit(false);
    return true;
}

bool FPhysicsAssetEditModel::SetConstraintParent(int32 ChildBone, int32 ParentBone)
{
    Error.clear();
    const int Index = FindConstraint(ChildBone);
    if (Index < 0 || !CanConnect(ChildBone, ParentBone)) { Error = "Choose an ancestor body as the parent."; return false; }
    if (GetConstraintParent(ChildBone) == ParentBone) return true;
    Commit(false);
    auto& Joint = *Mesh.GetPhysicsAsset()->PhysicsConstraint[Index];
    const FName PreviousParent = Joint.ConstraintBone2;
    FPhysicsConstraintSettings S, Previous;
    GetConstraintSettings(ChildBone, Previous);
    S = Previous;
    S.ChildFrame = FTransform::Identity;
    S.ParentFrame = ParentFrameFromChild(Mesh, ChildBone, ParentBone, S.ChildFrame);
    Joint.ConstraintBone2 = GetBody(ParentBone)->BoneName;
    if (!SetConstraintSettings(ChildBone, S))
    {
        Joint.ConstraintBone2 = PreviousParent;
        return false;
    }
    Commit(false);
    return true;
}
