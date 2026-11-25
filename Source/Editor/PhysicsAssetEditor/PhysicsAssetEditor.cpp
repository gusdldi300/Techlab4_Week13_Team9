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
#include "Physics/PhysicsScene.h"
#include "Animation/BonePose.h"
#include <cmath>
#include <algorithm>

FPhysicsAssetEditor::FPhysicsAssetEditor(UEditorEngine& InEngine, FEditorManager& InManager,
    const FString& InMeshPath, USkeletalMesh& InMesh)
    : FSkeletalPreviewEditor(InEngine, InManager, MakeDocumentKey(InMeshPath))
    , MeshAssetPath(InMeshPath)
    , EditModel(InMesh, InMeshPath)
{
    Mesh = &InMesh;
}

FPhysicsAssetEditor::~FPhysicsAssetEditor() { StopSimulation(); CancelGeneration(); }

void FPhysicsAssetEditor::SetSimulationGravity(float Value)
{
    if (!IsPreviewSimulating() && std::isfinite(Value)) SimulationGravity = FMath::Clamp(Value,-100000.0f,100000.0f);
}

FRagdollDebugStats FPhysicsAssetEditor::GetSimulationStats() const
{
    return SimulationInstance ? SimulationInstance->GetDebugStats() : FRagdollDebugStats{};
}

uint64 FPhysicsAssetEditor::GetSimulationStepCount() const
{
    return SimulationScene ? SimulationScene->GetStats().NumSteps : 0;
}

void FPhysicsAssetEditor::SetSimulationPaused(bool Paused)
{
    if (Paused && SimulationInstance) SimulationInstance->EndGrab();
    bSimulationPaused = Paused;
}

void FPhysicsAssetEditor::SetGrabSettings(const FPhysicsGrabSettings& Settings)
{
    if (!std::isfinite(Settings.Stiffness) || !std::isfinite(Settings.Damping)) return;
    GrabSettings.Stiffness = FMath::Clamp(Settings.Stiffness, 1.0f, 10000.0f);
    GrabSettings.Damping = FMath::Clamp(Settings.Damping, 0.0f, 500.0f);
    if (SimulationInstance) SimulationInstance->SetGrabSettings(GrabSettings);
}

bool FPhysicsAssetEditor::StartSimulation()
{
    if (GenerationPreview) return false;
    if (IsPreviewSimulating()) return true;
    SimulationError.clear();
    if (!MeshComponent || !GetPhysicsAsset() || GetPhysicsAsset()->BodySetups.IsEmpty())
    {
        SimulationError = "Add at least one physics body before simulating.";
        return false;
    }
    CancelViewportInteraction();
    EditModel.Commit(false);
    MeshComponent->RefreshBoneTransformsNow();
    PreSimulationPose = MeshComponent->GetBoneSpaceTransforms();
    SimulationScene = MakePhysicsScene(true);
    SimulationScene->CreatePhysicsScene();
    if (!SimulationScene->SetGravity(FVector(0,0,SimulationGravity)))
    {
        SimulationError = "Could not initialize the preview physics scene.";
        StopSimulation();
        return false;
    }
    if (bSimulationFloor)
    {
        const FBox Bounds = MeshComponent->CalcBounds();
        const FVector Size = Bounds.Max-Bounds.Min;
        const float Extent = std::max(Size.Size()*3.0f,1.0f);
        SimulationFloorHalfExtent = FVector(Extent,Extent,std::max(Extent*0.002f,0.01f));
        FBodyInstanceInitParams Params;
        Params.WorldLocation = FVector(0,0,-SimulationFloorHalfExtent.Z);
        Params.WorldRotation = FQuat::Identity();
        FKBoxElem Floor;
        Floor.X = Floor.Y = Extent*2;
        Floor.Z = SimulationFloorHalfExtent.Z*2;
        Params.FallbackGeom.BoxElems.Add(Floor);
        SimulationFloor.InitBody(nullptr,Params,nullptr,SimulationScene.get());
        if (!SimulationFloor.IsValidBodyInstance())
        {
            SimulationError = "Could not create the preview floor.";
            StopSimulation();
            return false;
        }
    }
    SimulationInstance = MakeUnique<FPhysicsAssetInstance>();
    if (!SimulationInstance->Build(*MeshComponent,SimulationScene.get()) ||
        !SimulationInstance->SetSimulating(*MeshComponent,true))
    {
        SimulationError = "Could not start simulation. Check the asset and uniform scale; at least one body must not be Kinematic.";
        StopSimulation();
        return false;
    }
    bSimulationPaused = false;
    EditGizmo.SetTarget(nullptr);
    return true;
}

void FPhysicsAssetEditor::StopSimulation()
{
    // No step remains in flight: AdvanceSimulation always pairs StartFrame with EndFrame.
    SimulationInstance.reset();
    SimulationFloor.TermBody();
    SimulationScene.reset();
    bSimulationPaused = false;
    if (MeshComponent && PreSimulationPose.Num() == MeshComponent->GetBones().Num() && !PreSimulationPose.IsEmpty())
    {
        FCompactPose Restored;
        Restored.SetNumBones(PreSimulationPose.Num());
        for (int I=0; I<PreSimulationPose.Num(); ++I) Restored[I] = PreSimulationPose[I];
        MeshComponent->SetBoneSpaceTransforms(Restored);
        MeshComponent->RefreshBoneTransformsNow();
    }
    PreSimulationPose.Empty();
    SyncEditGizmo();
}

void FPhysicsAssetEditor::AdvanceSimulation(float DeltaTime)
{
    if (!SimulationInstance || !SimulationScene || !MeshComponent) return;
    if (!SimulationInstance->IsValidFor(*MeshComponent)) { StopSimulation(); return; }
    SimulationInstance->SyncFromAnimation(*MeshComponent);
    SimulationScene->StartFrame(DeltaTime);
    SimulationScene->EndFrame();
    SimulationInstance->ApplyToMesh(*MeshComponent);
    // Overlay validation and skinning must read the same completed physics pose.
    MeshComponent->RefreshBoneTransformsNow();
}

void FPhysicsAssetEditor::StepSimulation()
{
    if (IsPreviewSimulating() && bSimulationPaused) AdvanceSimulation(1.0f/60.0f);
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
    EditGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
    if (!EditGizmoTarget) return false;
    EditGizmoTarget->DetachFromParent();
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
    if (GetPhysicsAsset()) return false;
    return PreviewGeneration() && ApplyGeneration();
}

bool FPhysicsAssetEditor::PreviewGeneration()
{
    if (!Mesh || IsPreviewSimulating()) return false;
    CancelViewportInteraction();
    EditGizmo.SetTarget(nullptr);
    ShapeSelection = {};
    CancelGeneration();
    GenerationPreview = PhysicsAssetBuilder::CreateFromSkeleton(*Mesh, GenerationSettings, FileError);
    return GenerationPreview != nullptr;
}

void FPhysicsAssetEditor::CancelGeneration()
{
    PhysicsAssetBuilder::DestroyGeneratedAsset(GenerationPreview);
    GenerationPreview = nullptr;
}

bool FPhysicsAssetEditor::ApplyGeneration()
{
    if (!GenerationPreview || IsPreviewSimulating()) return false;
    if (!EditModel.ReplaceWithGenerated(*GenerationPreview))
    { FileError = EditModel.GetError(); return false; }
    CancelGeneration();
    ShapeSelection = {};
    EditTarget = EPhysicsAssetEditTarget::Shape;
    FileError.clear();
    return true;
}

bool FPhysicsAssetEditor::Save()
{
    FileError.clear();
    if (GenerationPreview) { FileError = "Apply or cancel the generation preview before saving."; return false; }
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
    CancelGeneration();
    StopSimulation();
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
    CancelGeneration();
    StopSimulation();
    EditModel.Commit(false);
    if (!IsDirty() || bCloseConfirmed) return true;
    PendingAction = EAction::Close;
    Activate();
    return false;
}

void FPhysicsAssetEditor::RequestReload()
{
    StopSimulation();
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
    CancelGeneration();
    StopSimulation();
    EditGizmo.EndDrag();
    EditGizmo.SetTarget(nullptr);
    EditGizmoTarget = nullptr;
    FSkeletalPreviewEditor::ShutdownResources();
}

void FPhysicsAssetEditor::CancelViewportInteraction()
{
    if (SimulationInstance) SimulationInstance->EndGrab();
    if (EditGizmo.IsUsing()) EditModel.CancelInteraction();
    EditGizmo.EndDrag();
}

void FPhysicsAssetEditor::SetBodiesVisible(bool bVisible)
{
    CancelViewportInteraction();
    bShowBodies = bVisible;
    SyncEditGizmo();
}

void FPhysicsAssetEditor::SetConstraintsVisible(bool bVisible)
{
    CancelViewportInteraction();
    bShowConstraints = bVisible;
    SyncEditGizmo();
}

void FPhysicsAssetEditor::SetGizmoMode(EGizmoMode Mode)
{
    if (Mode != EGizmoMode::Location && Mode != EGizmoMode::Rotation && Mode != EGizmoMode::Scale) return;
    if (Mode == EGizmoMode::Scale && EditTarget != EPhysicsAssetEditTarget::Shape) return;
    if (Mode == EditGizmo.GetMode()) return;
    CancelViewportInteraction();
    EditGizmo.SetMode(Mode);
    SyncEditGizmo();
}

void FPhysicsAssetEditor::SetGizmoSpace(EGizmoSpace Space)
{
    if (Space == EditGizmo.GetSpace()) return;
    CancelViewportInteraction();
    EditGizmo.SetSpace(Space);
    SyncEditGizmo();
}

bool FPhysicsAssetEditor::IsShapeSelected(ECollisionShape Type, int32 Index) const
{
    return EditTarget == EPhysicsAssetEditTarget::Shape &&
        ShapeSelection == FPhysicsShapeSelection{GetSelectedBoneIndex(), Type, Index};
}

void FPhysicsAssetEditor::SelectShape(ECollisionShape Type, int32 Index)
{
    CancelViewportInteraction();
    FTransform Pose;
    if (!EditModel.GetShapeTransform(GetSelectedBoneIndex(),Type,Index,Pose)) return;
    EditModel.Commit(false);
    ShapeSelection = {GetSelectedBoneIndex(),Type,Index};
    EditTarget = EPhysicsAssetEditTarget::Shape;
    SyncEditGizmo();
}

bool FPhysicsAssetEditor::IsConstraintFrameSelected(int32 ChildBone, bool bParentFrame) const
{
    return GetSelectedBoneIndex() == ChildBone && EditTarget == (bParentFrame
        ? EPhysicsAssetEditTarget::ParentFrame : EPhysicsAssetEditTarget::ChildFrame);
}

bool FPhysicsAssetEditor::SelectConstraintFrame(int32 ChildBone, bool bParentFrame)
{
    FPhysicsConstraintSettings Settings;
    if (!EditModel.GetConstraintSettings(ChildBone,Settings)) return false;
    SelectBone(ChildBone); // The selected tree bone always identifies the constraint's child.
    EditModel.Commit(false);
    EditTarget = bParentFrame ? EPhysicsAssetEditTarget::ParentFrame : EPhysicsAssetEditTarget::ChildFrame;
    if (EditGizmo.GetMode() == EGizmoMode::Scale) EditGizmo.SetMode(EGizmoMode::Location);
    SyncEditGizmo();
    return true;
}

bool FPhysicsAssetEditor::SelectEditTarget(EPhysicsAssetEditTarget Target)
{
    if (Target != EPhysicsAssetEditTarget::Shape)
        return SelectConstraintFrame(GetSelectedBoneIndex(),Target == EPhysicsAssetEditTarget::ParentFrame);
    FTransform Local;
    if (!EditModel.GetShapeTransform(ShapeSelection.Bone,ShapeSelection.Type,ShapeSelection.Index,Local)) return false;
    SelectShape(ShapeSelection.Type,ShapeSelection.Index);
    return true;
}

bool FPhysicsAssetEditor::GetEditFrame(FTransform& Local, int32& Bone) const
{
    Bone = GetSelectedBoneIndex();
    if (EditTarget == EPhysicsAssetEditTarget::Shape)
        return bShowBodies && ShapeSelection.Bone == Bone &&
            EditModel.GetShapeTransform(Bone,ShapeSelection.Type,ShapeSelection.Index,Local);
    if (!bShowConstraints) return false;
    FPhysicsConstraintSettings Settings;
    if (!EditModel.GetConstraintSettings(Bone,Settings)) return false;
    if (EditTarget == EPhysicsAssetEditTarget::ParentFrame)
    {
        Bone = EditModel.GetConstraintParent(Bone);
        Local = Settings.ParentFrame;
    }
    else Local = Settings.ChildFrame;
    return Bone >= 0;
}

bool FPhysicsAssetEditor::ApplyEditFrame(const FTransform& Local)
{
    if (EditTarget == EPhysicsAssetEditTarget::Shape)
        return EditModel.SetShapeTransform(ShapeSelection.Bone,ShapeSelection.Type,ShapeSelection.Index,Local);
    FPhysicsConstraintSettings Settings;
    if (!EditModel.GetConstraintSettings(GetSelectedBoneIndex(),Settings)) return false;
    if (EditTarget == EPhysicsAssetEditTarget::ParentFrame) Settings.ParentFrame = Local;
    else Settings.ChildFrame = Local;
    return EditModel.SetConstraintSettings(GetSelectedBoneIndex(),Settings);
}

TArray<FPhysicsConstraintScreenHandle> FPhysicsAssetEditor::GetConstraintHandles()
{
    TArray<FPhysicsConstraintScreenHandle> Handles;
    if (GenerationPreview || !bShowConstraints || !GetPhysicsAsset() || !ViewportClient || !ViewportWidth || !ViewportHeight ||
        ViewportClient->GetWidth() != ViewportWidth || ViewportClient->GetHeight() != ViewportHeight) return Handles;
    const FMatrix VP = ViewportClient->GetViewProjectionMatrix();
    for (const auto* Joint : GetPhysicsAsset()->PhysicsConstraint)
    {
        if (!Joint) continue;
        const int Child = Mesh->FindBoneIndex(Joint->ConstraintBone1.ToString());
        FPhysicsConstraintSettings Settings;
        if (!EditModel.GetConstraintSettings(Child,Settings)) continue;
        for (bool Parent : {false,true})
        {
            FTransform Bone;
            const int Index = Parent ? EditModel.GetConstraintParent(Child) : Child;
            if (!GetBodyWorldTransform(Index,Bone)) continue;
            const auto& Frame = Parent ? Settings.ParentFrame : Settings.ChildFrame;
            const FVector World = Bone.GetLocalMatrix().TransformPosition(Frame.Location);
            const FVector4 Clip = FVector4(World,1)*VP;
            if (Clip.W <= 0.0001f || Clip.Z < 0 || Clip.Z > Clip.W) continue;
            const FVector2 Anchor((Clip.X/Clip.W*0.5f+0.5f)*ViewportWidth,(0.5f-Clip.Y/Clip.W*0.5f)*ViewportHeight);
            // Offset screen handles keep coincident child/parent anchors individually selectable.
            Handles.Add({Child,Parent,Anchor,Anchor+FVector2(Parent ? 13.0f : -13.0f,-14.0f),Clip.Z/Clip.W});
        }
    }
    std::stable_sort(Handles.begin(),Handles.end(),[](const auto& A,const auto& B){ return A.Depth > B.Depth; });
    return Handles;
}

void FPhysicsAssetEditor::OnBoneSelected()
{
    CancelViewportInteraction();
    ShapeSelection = {};
    EditTarget = EPhysicsAssetEditTarget::Shape;
    if (const auto* Body = EditModel.GetBody(GetSelectedBoneIndex()))
    {
        const auto& G = Body->AggGeom;
        if (!G.SphereElems.IsEmpty()) ShapeSelection = {GetSelectedBoneIndex(),ECollisionShape::Sphere,0};
        else if (!G.BoxElems.IsEmpty()) ShapeSelection = {GetSelectedBoneIndex(),ECollisionShape::Box,0};
        else if (!G.SphylElems.IsEmpty()) ShapeSelection = {GetSelectedBoneIndex(),ECollisionShape::Sphyl,0};
    }
    SyncEditGizmo();
}

void FPhysicsAssetEditor::SyncEditGizmo()
{
    if (GenerationPreview) { EditGizmo.SetTarget(nullptr); return; }
    if (IsPreviewSimulating()) { EditGizmo.SetTarget(nullptr); return; }
    if (EditGizmo.IsUsing()) return;
    FTransform Local, Bone;
    int32 BoneIndex;
    if (!EditGizmoTarget || !GetEditFrame(Local,BoneIndex) ||
        !GetBodyWorldTransform(BoneIndex,Bone) || std::abs(Bone.GetLocalMatrix().Determinant()) < 1e-12f ||
        (EditTarget == EPhysicsAssetEditTarget::Shape && ShapeSelection.Type == ECollisionShape::Sphere && EditGizmo.GetMode() == EGizmoMode::Rotation))
    {
        EditGizmo.SetTarget(nullptr);
        return;
    }
    EditGizmoTarget->SetTransform(FTransform((Bone.Rotation * Local.Rotation).Normalized(),
        Bone.GetLocalMatrix().TransformPosition(Local.Location)));
    EditGizmo.SetTarget(EditGizmoTarget);
}

bool FPhysicsAssetEditor::UpdateEditGizmo()
{
    if (IsPreviewSimulating()) return false;
    const bool WasUsing = EditGizmo.IsUsing();
    if (PointerInput.bCancel || !CanProcessViewportInput())
    {
        CancelViewportInteraction();
        return WasUsing;
    }
    if (!EditGizmo.GetTarget() || (!PointerInput.bHovered && !WasUsing) || CameraInput.bCaptured) return false;
    const FVector2 Mouse(PointerInput.MouseUV.X*ViewportWidth,PointerInput.MouseUV.Y*ViewportHeight);
    const FRay Ray = ViewportClient->DeprojectScreenToWorld(Mouse);
    // Start only on a fresh press; preserve the final release position.
    EditGizmo.Update(Ray,Mouse,ViewportClient->GetViewProjectionMatrix(),ViewportWidth,ViewportHeight,
        WasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown),ViewportClient->GetViewLocation(),
        !ViewportClient->IsPerspective(),PointerInput.bMousePressed);
    if (!WasUsing && EditGizmo.IsUsing())
    {
        FTransform Bone;
        int32 BoneIndex;
        if (!GetEditFrame(DragStartPose,BoneIndex) || !GetBodyWorldTransform(BoneIndex,Bone) ||
            std::abs(Bone.GetLocalMatrix().Determinant()) < 1e-12f)
        {
            EditGizmo.EndDrag();
            return true;
        }
        EditModel.Commit(false);
        DragBoneInverse = Bone.GetLocalMatrix().Inverse();
        DragBoneRotation = Bone.Rotation;
        if (EditGizmo.GetMode() == EGizmoMode::Scale &&
            !EditModel.GetShapeSize(ShapeSelection.Bone,ShapeSelection.Type,ShapeSelection.Index,DragStartSize))
        { EditGizmo.EndDrag(); return true; }
    }
    if (EditGizmo.IsUsing())
    {
        FTransform Local = DragStartPose;
        if (EditGizmo.GetMode() == EGizmoMode::Location)
            Local.Location = DragBoneInverse.TransformPosition(EditGizmoTarget->GetWorldLocation());
        else if (EditGizmo.GetMode() == EGizmoMode::Rotation)
            Local.Rotation = (DragBoneRotation.Inverse() * EditGizmoTarget->GetRelativeRotation().Quaternion()).Normalized();
        if (EditGizmo.GetMode() == EGizmoMode::Scale)
            EditModel.ScaleShape(ShapeSelection.Bone,ShapeSelection.Type,ShapeSelection.Index,DragStartSize,
                EditGizmoTarget->GetRelativeScale3D());
        else ApplyEditFrame(Local);
        if (!PointerInput.bMouseDown)
        {
            EditGizmo.EndDrag();
            EditModel.Commit(false);
        }
    }
    return WasUsing || EditGizmo.IsUsing() || EditGizmo.GetHoveredAxis() >= 0;
}

bool FPhysicsAssetEditor::UpdateSimulationGrab()
{
    if (!SimulationInstance) return false;
    const bool WasGrabbing = IsSimulationGrabbing();
    // Process release even when input is no longer eligible (closed/detached viewport, resize, focus loss).
    if (bSimulationPaused || !CanProcessViewportInput() || PointerInput.bCancel || !PointerInput.bMouseDown)
    {
        SimulationInstance->EndGrab();
        return WasGrabbing;
    }
    const FVector2 Mouse(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);
    const FRay Ray = ViewportClient->DeprojectScreenToWorld(Mouse);
    const bool WantsGrab = PointerInput.bCtrlDown && PointerInput.bHovered && PointerInput.bMousePressed && !CameraInput.bCaptured;
    if (WantsGrab)
    {
        FPhysicsBodyHit Hit;
        if (SimulationInstance->RaycastBodies(Ray, Hit))
        {
            // Selection cancels existing interactions; do it before creating the grab joint.
            SelectBone(Hit.Bone);
            SelectShape(Hit.Type, Hit.ShapeIndex);
            if (SimulationInstance->BeginGrab(Ray, Hit, GrabSettings))
            {
                GrabPlanePoint = Hit.Position;
                GrabPlaneNormal = ViewportClient->GetViewForward().GetSafeNormal();
            }
        }
    }
    if (IsSimulationGrabbing())
    {
        float Distance = 0;
        if (RayIntersectsPlane(Ray, GrabPlanePoint, GrabPlaneNormal, Distance) && Distance >= 0 &&
            std::isfinite(Distance) && Distance <= ViewportClient->GetFarClip())
            SimulationInstance->UpdateGrabTarget(Ray.PointAt(Distance));
    }
    // A Ctrl-click on empty space must not fall through to edit/bone picking.
    return WantsGrab || WasGrabbing || IsSimulationGrabbing();
}

void FPhysicsAssetEditor::TickAfterWorld(float DeltaTime)
{
    if (GenerationPreview) { EditGizmo.SetTarget(nullptr); return; }
    const bool GrabConsumedInput = IsPreviewSimulating() && UpdateSimulationGrab();
    if (IsPreviewSimulating() && !bSimulationPaused) AdvanceSimulation(DeltaTime);
    SyncEditGizmo();
    if (GrabConsumedInput) return;
    if (!EditGizmo.IsUsing() && CanProcessViewportInput() && PointerInput.bHovered && PointerInput.bMousePressed &&
        !PointerInput.bCancel && !CameraInput.bCaptured)
    {
        const FVector2 Mouse(PointerInput.MouseUV.X*ViewportWidth,PointerInput.MouseUV.Y*ViewportHeight);
        FPhysicsConstraintScreenHandle Hit;
        float Depth = FLT_MAX;
        for (const auto& Handle : GetConstraintHandles())
        {
            const FVector2 Delta = Mouse-Handle.Position;
            if (Delta.X*Delta.X+Delta.Y*Delta.Y <= 81 && Handle.Depth < Depth) { Hit = Handle; Depth = Handle.Depth; }
        }
        if (Hit.ChildBone >= 0) { SelectConstraintFrame(Hit.ChildBone,Hit.bParentFrame); return; }
    }
    if (!UpdateEditGizmo() && CanProcessViewportInput() && PointerInput.bHovered && PointerInput.bMousePressed &&
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
    SyncEditGizmo();
}

TArray<FPhysicsPreviewShape> FPhysicsAssetEditor::GatherPreviewShapes()
{
    TArray<FPhysicsPreviewShape> Shapes;
    const auto* Asset = GenerationPreview ? GenerationPreview : GetPhysicsAsset();
    if (!MeshComponent || !Asset) return Shapes;
    for (const UBodySetup* Body : Asset->BodySetups)
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
    if (IsSimulationGrabbing())
    {
        const FVector Point = SimulationInstance->GetGrabPoint();
        const FVector Target = SimulationInstance->GetGrabTarget();
        const float Radius = std::max((MeshComponent->CalcBounds().Max-MeshComponent->CalcBounds().Min).Size()*0.006f, 0.01f);
        Lines.AddLine(Point, Target, FVector4(1, 0.7f, 0.1f, 1));
        Lines.AddSphere(Point, Radius, FVector4(0.1f, 0.8f, 1, 1));
        Lines.AddSphere(Target, Radius, FVector4(1, 0.7f, 0.1f, 1));
    }
    if (IsPreviewSimulating() && bSimulationFloor)
    {
        const FVector Center(0,0,-SimulationFloorHalfExtent.Z);
        Lines.AddBox(FBox{Center-SimulationFloorHalfExtent,Center+SimulationFloorHalfExtent},FVector4(0.5f,0.5f,0.55f,1));
    }
    if (!MeshComponent || !GetPhysicsAsset() || GenerationPreview) return;
    if (bShowBodies && EditTarget == EPhysicsAssetEditTarget::Shape)
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
