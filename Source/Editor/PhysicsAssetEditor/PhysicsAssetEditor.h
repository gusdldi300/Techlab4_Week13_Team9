#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "PhysicsAssetEditModel.h"
#include "PhysicsAssetPreview.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Physics/PhysicsAssetBuilder.h"

class UPhysicsAsset;

enum class EPhysicsAssetEditTarget { Shape, ChildFrame, ParentFrame };
enum class EPhysicsAssetConstraintView { Selected, All, None };
enum class EPhysicsAssetCOMView { None, Selected, All };

struct FPhysicsCOMScreenMarker
{
    int32 Bone = -1;
    FVector2 Position;
    float MassKg = 0;
    float Depth = 0;
};

struct FPhysicsConstraintScreenHandle
{
    int32 ChildBone = -1;
    bool bParentFrame = false;
    FVector2 Anchor;
    FVector2 Position;
    float Depth = 0;
};

// Physics Asset의 기본 메시를 보며 본과 물리 설정을 선택하는 전용 에디터.
class FPhysicsAssetEditor final : public FSkeletalPreviewEditor
{
public:
    FPhysicsAssetEditor(UEditorEngine& InEngine, FEditorManager& InManager,
        const FString& InMeshPath, USkeletalMesh& InMesh);
    ~FPhysicsAssetEditor() override;

    // 실제 파일 경로와 구별되는, 메시별 편집 문서의 고유 ID.
    static FString MakeDocumentKey(const FString& MeshPath);
    const char* GetEditorTypeName() const override { return "PhysicsAsset"; }
    bool UsesPreviewSkybox() const override { return true; }
    UPhysicsAsset* GetPhysicsAsset() const;
    const FString& GetMeshAssetPath() const { return MeshAssetPath; }
    bool CreateAsset();
    bool PreviewGeneration();
    bool ApplyGeneration();
    void CancelGeneration();
    UPhysicsAsset* GetGenerationPreview() const { return GenerationPreview; }
    PhysicsAssetBuilder::FAutoGenerateSettings& GetGenerationSettings() { return GenerationSettings; }
    bool Save();
    bool Reload();
    bool HasSavedAsset() const;
    const FString& GetFileError() const { return FileError; }
    FPhysicsAssetEditModel& GetEditModel() { return EditModel; }
    bool IsDirty() const override { return EditModel.IsDirty(); }
    bool ConfirmClose() override;
    void RequestReload();
    bool HasPendingAction() const { return PendingAction != EAction::None; }
    bool CompletePendingAction(bool bSave);
    void CancelPendingAction() { PendingAction = EAction::None; }
    void OnRenderLine(FLineBatcher& Lines) override;
    void OnRenderOverlay(const FSceneView& View) override;
    const FGizmo* GetPreviewGizmo() const override { return EditGizmo.GetTarget() ? &EditGizmo : nullptr; }
    void SelectShape(ECollisionShape Type, int32 Index);
    bool IsShapeSelected(ECollisionShape Type, int32 Index) const;
    const FPhysicsShapeSelection& GetShapeSelection() const { return ShapeSelection; }
    EPhysicsAssetEditTarget GetEditTarget() const { return EditTarget; }
    bool SelectEditTarget(EPhysicsAssetEditTarget Target);
    bool SelectConstraintFrame(int32 ChildBone, bool bParentFrame);
    bool IsConstraintFrameSelected(int32 ChildBone, bool bParentFrame) const;
    int32 GetSelectedConstraintBone() const;
    bool ShouldDrawConstraintLimits(int32 ChildBone) const;
    EPhysicsAssetConstraintView GetConstraintView() const { return ConstraintView; }
    void SetConstraintView(EPhysicsAssetConstraintView View);
    TArray<FPhysicsConstraintScreenHandle> GetConstraintHandles();
    FPhysicsConstraintScreenHandle PickConstraintHandle(const FVector2& Mouse);
    TArray<FPhysicsCOMScreenMarker> GetCenterOfMassMarkers();
    EPhysicsAssetCOMView GetCOMView() const { return COMView; }
    void SetCOMView(EPhysicsAssetCOMView Mode) { COMView = Mode; }
    float GetCOMMarkerSize() const { return COMMarkerSize; }
    void SetCOMMarkerSize(float Size) { COMMarkerSize = FMath::Clamp(Size, 3.0f, 16.0f); }
    bool IsCOMMassVisible() const { return bShowCOMMass; }
    void SetCOMMassVisible(bool Visible) { bShowCOMMass = Visible; }
    void SetGizmoMode(EGizmoMode Mode);
    void SetGizmoSpace(EGizmoSpace Space);
    EGizmoMode GetGizmoMode() const { return EditGizmo.GetMode(); }
    EGizmoSpace GetGizmoSpace() const { return EditGizmo.GetSpace(); }
    bool IsGizmoDragging() const { return EditGizmo.IsUsing(); }
    float GetBodyOpacity() const { return BodyOpacity; }
    void SetBodyOpacity(float Value) { BodyOpacity = FMath::Clamp(Value, 0.02f, 0.45f); }
    bool AreBodiesVisible() const { return bShowBodies; }
    void SetBodiesVisible(bool bVisible);
    bool AreConstraintsVisible() const { return ConstraintView != EPhysicsAssetConstraintView::None; }
    void SetConstraintsVisible(bool bVisible);
    const FString& GetPreviewWarning() const { return PreviewWarning; }
    bool StartSimulation();
    void StopSimulation();
    void SetSimulationPaused(bool Paused);
    bool IsSimulationGrabbing() const { return SimulationInstance && SimulationInstance->IsGrabbing(); }
    const FPhysicsGrabSettings& GetGrabSettings() const { return GrabSettings; }
    void SetGrabSettings(const FPhysicsGrabSettings& Settings);
    void StepSimulation();
    bool IsPreviewSimulating() const { return SimulationInstance != nullptr; }
    bool IsSimulationPaused() const { return bSimulationPaused; }
    bool IsSimulationFloorEnabled() const { return bSimulationFloor; }
    void SetSimulationFloorEnabled(bool Enabled) { if (!IsPreviewSimulating()) bSimulationFloor = Enabled; }
    float GetSimulationGravity() const { return SimulationGravity; }
    void SetSimulationGravity(float Value);
    const FString& GetSimulationError() const { return SimulationError; }
    FRagdollDebugStats GetSimulationStats() const;
    uint64 GetSimulationStepCount() const;

protected:
    bool InitializeResources() override;
    void ShutdownResources() override;
    void TickAfterWorld(float DeltaTime) override;
    bool IsViewportInteractionBusy() const override { return EditGizmo.IsUsing() || IsSimulationGrabbing(); }
    void OnBoneSelected() override;
    void CancelViewportInteraction() override;
    TUniquePtr<IEditorTab> CreateTab() override;

private:
    enum class EAction { None, Close, Reload };
    FString MeshAssetPath;
    FString FileError;
    PhysicsAssetBuilder::FAutoGenerateSettings GenerationSettings;
    UPhysicsAsset* GenerationPreview = nullptr;
    FPhysicsAssetEditModel EditModel;
    EAction PendingAction = EAction::None;
    bool bCloseConfirmed = false;
    bool bShowBodies = true;
    EPhysicsAssetConstraintView ConstraintView = EPhysicsAssetConstraintView::Selected;
    EPhysicsAssetCOMView COMView = EPhysicsAssetCOMView::Selected;
    float COMMarkerSize = 6.0f; // Screen-space radius; remains visible when zooming out.
    bool bShowCOMMass = true;
    FString PreviewWarning;
    bool GetBodyWorldTransform(int32 Bone, FTransform& Out);
    TArray<FPhysicsPreviewShape> GatherPreviewShapes();
    void SyncEditGizmo();
    bool UpdateEditGizmo();
    bool GetEditFrame(FTransform& Local, int32& Bone) const;
    bool ApplyEditFrame(const FTransform& Local);
    FPhysicsAssetPreview BodyPreview;
    FPhysicsShapeSelection ShapeSelection;
    EPhysicsAssetEditTarget EditTarget = EPhysicsAssetEditTarget::Shape;
    // Logical joint identity is its child bone, independent of the tree's body selection.
    int32 SelectedConstraintBone = -1;
    FGizmo EditGizmo;
    USceneComponent* EditGizmoTarget = nullptr;
    FTransform DragStartPose;
    FVector DragStartSize;
    FMatrix DragBoneInverse;
    FQuat DragBoneRotation;
    float BodyOpacity = 0.16f;
    void AdvanceSimulation(float DeltaTime);
    bool UpdateSimulationGrab();
    FPhysicsGrabSettings GrabSettings;
    FVector GrabPlanePoint = FVector::ZeroVector;
    FVector GrabPlaneNormal = FVector::ZeroVector;
    // Bodies/joints must be released before their owning scene (also in reverse member destruction).
    TUniquePtr<FPhysicsScene> SimulationScene;
    FBodyInstance SimulationFloor;
    TUniquePtr<FPhysicsAssetInstance> SimulationInstance;
    TArray<FTransform> PreSimulationPose;
    FVector SimulationFloorHalfExtent;
    FString SimulationError;
    float SimulationGravity = -980.0f; // Raw skeletal preview coordinates are normally centimeters.
    bool bSimulationPaused = false;
    bool bSimulationFloor = true;
};
