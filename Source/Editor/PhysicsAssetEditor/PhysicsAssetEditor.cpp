#include "EnginePCH.h"
#include "PhysicsAssetEditor.h"
#include "PhysicsAssetEditorTab.h"
#include "Render/SkeletalMesh.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/LineBatcher.h"
#include <cmath>

FPhysicsAssetEditor::FPhysicsAssetEditor(UEditorEngine& InEngine, FEditorManager& InManager,
    const FString& InMeshPath, USkeletalMesh& InMesh)
    : FSkeletalPreviewEditor(InEngine, InManager, MakeDocumentKey(InMeshPath))
    , MeshAssetPath(InMeshPath)
    , EditModel(InMesh, InMeshPath)
{
    Mesh = &InMesh;
}

FString FPhysicsAssetEditor::MakeDocumentKey(const FString& MeshPath)
{
    return MeshPath + ".physicsasset-editor";
}

UPhysicsAsset* FPhysicsAssetEditor::GetPhysicsAsset() const
{
    return Mesh ? Mesh->GetPhysicsAsset() : nullptr;
}

TUniquePtr<IEditorTab> FPhysicsAssetEditor::CreateTab()
{
    return MakeUnique<FPhysicsAssetEditorTab>(*this);
}

bool FPhysicsAssetEditor::InitializeResources()
{
    if (!InitializePreview()) return false;
    ShapeGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
    if (!ShapeGizmoTarget) return false;
    ShapeGizmoTarget->DetachFromParent();
    SetBoneDrawMode(EBoneDrawMode::SelectedOnly);
    if (!GetPhysicsAsset() && HasSavedAsset()) Reload();
    EditModel.ResetHistory();
    return true;
}

bool FPhysicsAssetEditor::HasSavedAsset() const
{
    std::error_code Error;
    return std::filesystem::is_regular_file(PhysicsAssetFile::GetDefaultPath(MeshAssetPath), Error);
}

bool FPhysicsAssetEditor::CreateAsset()
{
    FileError.clear();
    if (!Mesh || GetPhysicsAsset()) return false;
    UPhysicsAsset* Asset = NewObject<UPhysicsAsset>(Mesh);
    if (!PhysicsAssetFile::Write(PhysicsAssetFile::GetDefaultPath(MeshAssetPath), MeshAssetPath,
        *Mesh, *Asset, FileError, false))
    {
        delete Asset;
        return false;
    }
    Mesh->SetPhysicsAsset(Asset);
    EditModel.ResetHistory();
    KRS_LOG(Info, "Created Physics Asset for {}", MeshAssetPath);
    return true;
}

bool FPhysicsAssetEditor::Save()
{
    FileError.clear();
    if (!Mesh || !GetPhysicsAsset()) return false;
    EditModel.Commit(false);
    const bool Saved = PhysicsAssetFile::Write(PhysicsAssetFile::GetDefaultPath(MeshAssetPath),
        MeshAssetPath, *Mesh, *GetPhysicsAsset(), FileError);
    if (Saved)
    {
        EditModel.MarkSaved();
        KRS_LOG(Info, "Saved Physics Asset for {}", MeshAssetPath);
    }
    return Saved;
}

bool FPhysicsAssetEditor::Reload()
{
    FileError.clear();
    if (!Mesh) return false;
    UPhysicsAsset* Asset = GetPhysicsAsset();
    if (!PhysicsAssetFile::Read(PhysicsAssetFile::GetDefaultPath(MeshAssetPath), *Mesh, Asset, FileError))
        return false;
    Mesh->SetPhysicsAsset(Asset);
    EditModel.ResetHistory();
    KRS_LOG(Info, "Loaded Physics Asset for {}", MeshAssetPath);
    return true;
}

bool FPhysicsAssetEditor::ConfirmClose()
{
    EditModel.Commit(false);
    if (!IsDirty() || bCloseConfirmed) return true;
    PendingAction = EAction::Close;
    Activate();
    return false;
}

void FPhysicsAssetEditor::RequestReload()
{
    EditModel.Commit(false);
    if (IsDirty()) PendingAction = EAction::Reload;
    else Reload();
}

bool FPhysicsAssetEditor::CompletePendingAction(bool bSave)
{
    const EAction Action = PendingAction;
    if (Action == EAction::None) return false;
    if (bSave && !Save()) return false;
    if (Action == EAction::Reload)
    {
        if (!Reload()) return false;
        PendingAction = EAction::None;
        return true;
    }
    if (!bSave && !EditModel.RestoreSaved()) return false;
    PendingAction = EAction::None;
    bCloseConfirmed = true;
    Close();
    return true;
}

namespace
{
    void DrawConstraintArc(FLineBatcher& Lines, const FTransform& Frame, const FVector& Axis,
        const FVector& StartDirection, EConstraintMotion Motion, float LimitDegrees, float Radius, const FVector4& Color)
    {
        if (Motion == EConstraintMotion::Locked) return;
        const float Angle = Motion == EConstraintMotion::Free ? PI : LimitDegrees * PI / 180.0f;
        const auto Point = [&](float T) {
            return Frame.Location + Frame.Rotation.RotateVector(
                FQuat::MakeFromAxisAngle(Axis, T).RotateVector(StartDirection)) * Radius;
        };
        FVector Previous = Point(-Angle);
        if (Motion == EConstraintMotion::Limited) Lines.AddLine(Frame.Location, Previous, Color);
        for (int I = 1; I <= 48; ++I)
        {
            const FVector Next = Point(-Angle + 2.0f * Angle * I / 48.0f);
            Lines.AddLine(Previous, Next, Color);
            Previous = Next;
        }
        if (Motion == EConstraintMotion::Limited) Lines.AddLine(Frame.Location, Previous, Color);
    }
}

void FPhysicsAssetEditor::ShutdownResources()
{
    ShapeGizmo.EndDrag();
    ShapeGizmo.SetTarget(nullptr);
    ShapeGizmoTarget = nullptr;
    FSkeletalPreviewEditor::ShutdownResources();
}

void FPhysicsAssetEditor::CancelViewportInteraction()
{
    if (ShapeGizmo.IsUsing()) EditModel.CancelInteraction();
    ShapeGizmo.EndDrag();
}

void FPhysicsAssetEditor::SetBodiesVisible(bool bVisible)
{
    CancelViewportInteraction();
    bShowBodies = bVisible;
    SyncShapeGizmo();
}

void FPhysicsAssetEditor::SetGizmoMode(EGizmoMode Mode)
{
    if (Mode != EGizmoMode::Location && Mode != EGizmoMode::Rotation) return;
    if (Mode == ShapeGizmo.GetMode()) return;
    CancelViewportInteraction();
    ShapeGizmo.SetMode(Mode);
    SyncShapeGizmo();
}

void FPhysicsAssetEditor::SetGizmoSpace(EGizmoSpace Space)
{
    if (Space == ShapeGizmo.GetSpace()) return;
    CancelViewportInteraction();
    ShapeGizmo.SetSpace(Space);
    SyncShapeGizmo();
}

bool FPhysicsAssetEditor::IsShapeSelected(ECollisionShape Type, int32 Index) const
{
    return ShapeSelection == FPhysicsShapeSelection{GetSelectedBoneIndex(), Type, Index};
}

void FPhysicsAssetEditor::SelectShape(ECollisionShape Type, int32 Index)
{
    CancelViewportInteraction();
    FTransform Pose;
    if (!EditModel.GetShapeTransform(GetSelectedBoneIndex(),Type,Index,Pose)) return;
    EditModel.Commit(false);
    ShapeSelection = {GetSelectedBoneIndex(),Type,Index};
    SyncShapeGizmo();
}

void FPhysicsAssetEditor::OnBoneSelected()
{
    CancelViewportInteraction();
    ShapeSelection = {};
    if (const auto* Body = EditModel.GetBody(GetSelectedBoneIndex()))
    {
        const auto& G = Body->AggGeom;
        if (!G.SphereElems.IsEmpty()) ShapeSelection = {GetSelectedBoneIndex(),ECollisionShape::Sphere,0};
        else if (!G.BoxElems.IsEmpty()) ShapeSelection = {GetSelectedBoneIndex(),ECollisionShape::Box,0};
        else if (!G.SphylElems.IsEmpty()) ShapeSelection = {GetSelectedBoneIndex(),ECollisionShape::Sphyl,0};
    }
    SyncShapeGizmo();
}

void FPhysicsAssetEditor::SyncShapeGizmo()
{
    if (ShapeGizmo.IsUsing()) return;
    FTransform Local, Bone;
    if (!ShapeGizmoTarget || !bShowBodies || ShapeSelection.Bone != GetSelectedBoneIndex() ||
        !EditModel.GetShapeTransform(ShapeSelection.Bone,ShapeSelection.Type,ShapeSelection.Index,Local) ||
        !GetBodyWorldTransform(ShapeSelection.Bone,Bone) || std::abs(Bone.GetLocalMatrix().Determinant()) < 1e-12f ||
        (ShapeSelection.Type == ECollisionShape::Sphere && ShapeGizmo.GetMode() == EGizmoMode::Rotation))
    {
        ShapeGizmo.SetTarget(nullptr);
        return;
    }
    ShapeGizmoTarget->SetTransform(FTransform((Bone.Rotation * Local.Rotation).Normalized(),
        Bone.GetLocalMatrix().TransformPosition(Local.Location)));
    ShapeGizmo.SetTarget(ShapeGizmoTarget);
}

bool FPhysicsAssetEditor::UpdateShapeGizmo()
{
    const bool WasUsing = ShapeGizmo.IsUsing();
    if (PointerInput.bCancel || !CanProcessViewportInput())
    {
        CancelViewportInteraction();
        return WasUsing;
    }
    if (!ShapeGizmo.GetTarget() || (!PointerInput.bHovered && !WasUsing) || CameraInput.bCaptured) return false;
    const FVector2 Mouse(PointerInput.MouseUV.X*ViewportWidth,PointerInput.MouseUV.Y*ViewportHeight);
    const FRay Ray = ViewportClient->DeprojectScreenToWorld(Mouse);
    // Start only on a fresh press; preserve the final release position.
    ShapeGizmo.Update(Ray,Mouse,ViewportClient->GetViewProjectionMatrix(),ViewportWidth,ViewportHeight,
        WasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown),ViewportClient->GetViewLocation(),
        !ViewportClient->IsPerspective(),PointerInput.bMousePressed);
    if (!WasUsing && ShapeGizmo.IsUsing())
    {
        FTransform Bone;
        if (!GetBodyWorldTransform(ShapeSelection.Bone,Bone) || std::abs(Bone.GetLocalMatrix().Determinant()) < 1e-12f ||
            !EditModel.GetShapeTransform(ShapeSelection.Bone,ShapeSelection.Type,ShapeSelection.Index,DragStartPose))
        {
            ShapeGizmo.EndDrag();
            return true;
        }
        EditModel.Commit(false);
        DragBoneInverse = Bone.GetLocalMatrix().Inverse();
        DragBoneRotation = Bone.Rotation;
    }
    if (ShapeGizmo.IsUsing())
    {
        FTransform Local = DragStartPose;
        if (ShapeGizmo.GetMode() == EGizmoMode::Location)
            Local.Location = DragBoneInverse.TransformPosition(ShapeGizmoTarget->GetWorldLocation());
        else Local.Rotation = (DragBoneRotation.Inverse() * ShapeGizmoTarget->GetRelativeRotation().Quaternion()).Normalized();
        EditModel.SetShapeTransform(ShapeSelection.Bone,ShapeSelection.Type,ShapeSelection.Index,Local);
        if (!PointerInput.bMouseDown)
        {
            ShapeGizmo.EndDrag();
            EditModel.Commit(false);
        }
    }
    return WasUsing || ShapeGizmo.IsUsing() || ShapeGizmo.GetHoveredAxis() >= 0;
}

void FPhysicsAssetEditor::TickAfterWorld(float DeltaTime)
{
    SyncShapeGizmo();
    if (!UpdateShapeGizmo() && CanProcessViewportInput() && PointerInput.bHovered && PointerInput.bMousePressed &&
        !PointerInput.bCancel && !CameraInput.bCaptured)
    {
        const FVector2 Mouse(PointerInput.MouseUV.X*ViewportWidth,PointerInput.MouseUV.Y*ViewportHeight);
        const auto Hit = bShowBodies ? FPhysicsAssetPreview::Pick(GatherPreviewShapes(),ViewportClient->DeprojectScreenToWorld(Mouse))
            : FPhysicsShapeSelection{};
        if (Hit.Bone >= 0)
        {
            SelectBone(Hit.Bone);
            SelectShape(Hit.Type,Hit.Index);
        }
        else if (IsBoneVisualizerVisible()) UpdateBonePicking();
        else SelectBone(-1);
    }
    SyncShapeGizmo();
}

TArray<FPhysicsPreviewShape> FPhysicsAssetEditor::GatherPreviewShapes()
{
    TArray<FPhysicsPreviewShape> Shapes;
    if (!MeshComponent || !GetPhysicsAsset()) return Shapes;
    for (const UBodySetup* Body : GetPhysicsAsset()->BodySetups)
    {
        if (!Body) continue;
        const int Bone = Mesh->FindBoneIndex(Body->BoneName.ToString());
        FTransform Transform;
        if (GetBodyWorldTransform(Bone,Transform)) FPhysicsAssetPreview::AppendShapes(Shapes,Bone,Body->AggGeom,Transform);
    }
    return Shapes;
}

void FPhysicsAssetEditor::OnRenderOverlay(const FSceneView& View)
{
    PreviewWarning.clear();
    if (bShowBodies) BodyPreview.Render(GatherPreviewShapes(),ShapeSelection,View,BodyOpacity);
}

bool FPhysicsAssetEditor::GetBodyWorldTransform(int32 Bone, FTransform& Transform)
{
    if (!MeshComponent || !Mesh || Bone < 0 || Bone >= Mesh->GetNumBones()) return false;
    const FMatrix World = MeshComponent->GetBoneWorldMatrix(Bone);
    Transform.Location = World.TransformPosition(FVector::ZeroVector);
    Transform.Scale = MeshComponent->GetWorldScale3D();
    FQuat Rotation = FQuat::Identity();
    for (int32 I=Bone; I>=0; I=Mesh->GetRefSkeleton()[I].ParentIndex)
    {
        const FTransform Local = MeshComponent->GetBoneLocalTransform(I);
        Rotation = Local.Rotation * Rotation;
        Transform.Scale.X *= Local.Scale.X; Transform.Scale.Y *= Local.Scale.Y; Transform.Scale.Z *= Local.Scale.Z;
    }
    Transform.Rotation = (MeshComponent->GetWorldRotation().Quaternion()*Rotation).Normalized();
    const FMatrix Reconstructed = Transform.GetLocalMatrix();
    for (int R=0; R<3; ++R) for (int C=0; C<3; ++C)
        if (!std::isfinite(World.M[R][C]) || std::abs(World.M[R][C]-Reconstructed.M[R][C]) >
            0.001f*std::max(1.0f,std::abs(World.M[R][C])))
        {
            PreviewWarning = "A bone has a sheared transform; its physics preview is unavailable.";
            return false;
        }
    return true;
}

void FPhysicsAssetEditor::OnRenderLine(FLineBatcher& Lines)
{
    FSkeletalPreviewEditor::OnRenderLine(Lines);
    if (!MeshComponent || !GetPhysicsAsset()) return;
    if (bShowBodies)
    {
        FTransform Transform;
        FTransform Local;
        const auto& Selected = ShapeSelection;
        const auto* Body = EditModel.GetBody(Selected.Bone);
        if (Body && EditModel.GetShapeTransform(Selected.Bone,Selected.Type,Selected.Index,Local) &&
            GetBodyWorldTransform(Selected.Bone,Transform))
        {
            FKAggregateGeom Geometry;
            if (Selected.Type == ECollisionShape::Sphere) Geometry.SphereElems.Add(Body->AggGeom.SphereElems[Selected.Index]);
            else if (Selected.Type == ECollisionShape::Box) Geometry.BoxElems.Add(Body->AggGeom.BoxElems[Selected.Index]);
            else if (Selected.Type == ECollisionShape::Sphyl) Geometry.SphylElems.Add(Body->AggGeom.SphylElems[Selected.Index]);
            FPhysicsDebugDraw::DrawAggregateGeom(&Lines,Geometry,Transform,FVector4(0.1f,0.8f,1,1));
        }
    }
    if (!bShowConstraints) return;
    const FBox Bounds = MeshComponent->CalcBounds();
    const float Radius = std::max((Bounds.Max - Bounds.Min).Size() * 0.04f, 0.01f);
    const FVector4 XColor(1, 0.2f, 0.2f, 1), YColor(0.2f, 1, 0.2f, 1), ZColor(0.2f, 0.5f, 1, 1);
    const FVector X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
    for (const auto* Joint : GetPhysicsAsset()->PhysicsConstraint)
    {
        if (!Joint) continue;
        const int Child = Mesh->FindBoneIndex(Joint->ConstraintBone1.ToString());
        const int Parent = Mesh->FindBoneIndex(Joint->ConstraintBone2.ToString());
        FTransform ChildBody, ParentBody;
        if (!GetBodyWorldTransform(Child, ChildBody) || !GetBodyWorldTransform(Parent, ParentBody)) continue;
        FPhysicsConstraintSettings S;
        if (!EditModel.GetConstraintSettings(Child, S)) continue;
        const FVector ChildAnchor = ChildBody.GetLocalMatrix().TransformPosition(S.ChildFrame.Location);
        const FVector ParentAnchor = ParentBody.GetLocalMatrix().TransformPosition(S.ParentFrame.Location);
        const FVector4 AnchorColor(1, 0.9f, 0.1f, 1);
        Lines.AddSphere(ChildAnchor, Radius * 0.05f, AnchorColor);
        Lines.AddSphere(ParentAnchor, Radius * 0.05f, FVector4(0.1f, 1, 1, 1));
        Lines.AddLine(ChildAnchor, ParentAnchor, AnchorColor);
        if (Child != GetSelectedBoneIndex() && Parent != GetSelectedBoneIndex()) continue;
        const FTransform ParentFrame((ParentBody.Rotation * S.ParentFrame.Rotation).Normalized(), ParentAnchor);
        const FQuat ChildRotation = (ChildBody.Rotation * S.ChildFrame.Rotation).Normalized();
        Lines.AddLine(ChildAnchor, ChildAnchor + ChildRotation.RotateVector(X) * Radius, AnchorColor);
        Lines.AddLine(ParentAnchor, ParentAnchor + ParentFrame.Rotation.RotateVector(X) * Radius, XColor);
        Lines.AddLine(ParentAnchor, ParentAnchor + ParentFrame.Rotation.RotateVector(Y) * Radius, YColor);
        Lines.AddLine(ParentAnchor, ParentAnchor + ParentFrame.Rotation.RotateVector(Z) * Radius, ZColor);
        // Independent angular guides; not an approximation of the solver's coupled swing cone.
        DrawConstraintArc(Lines, ParentFrame, Y, X, S.Swing1Motion, S.Swing1LimitDegrees, Radius, YColor);
        DrawConstraintArc(Lines, ParentFrame, Z, X, S.Swing2Motion, S.Swing2LimitDegrees, Radius, ZColor);
        DrawConstraintArc(Lines, ParentFrame, X, Y, S.TwistMotion, S.TwistLimitDegrees, Radius * 0.8f, XColor);
        const EConstraintMotion Modes[] = { S.XMotion, S.YMotion, S.ZMotion };
        const FVector Axes[] = { X, Y, Z };
        const FVector4 Colors[] = { XColor, YColor, ZColor };
        for (int I = 0; I < 3; ++I)
        {
            if (Modes[I] != EConstraintMotion::Limited) continue;
            const FVector Offset = ParentBody.GetLocalMatrix().TransformVector(S.ParentFrame.Rotation.RotateVector(Axes[I]) * S.LinearLimit);
            Lines.AddLine(ParentAnchor - Offset, ParentAnchor + Offset, Colors[I]);
            Lines.AddSphere(ParentAnchor - Offset, Radius * 0.025f, Colors[I]);
            Lines.AddSphere(ParentAnchor + Offset, Radius * 0.025f, Colors[I]);
        }
    }
}
