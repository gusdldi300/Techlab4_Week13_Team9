#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "PhysicsAssetEditModel.h"
#include "PhysicsAssetPreview.h"
#include "Editor/Gizmo/Gizmo.h"

class UPhysicsAsset;

// Physics Asset의 기본 메시를 보며 본과 물리 설정을 선택하는 전용 에디터.
class FPhysicsAssetEditor final : public FSkeletalPreviewEditor
{
public:
    FPhysicsAssetEditor(UEditorEngine& InEngine, FEditorManager& InManager,
        const FString& InMeshPath, USkeletalMesh& InMesh);

    // 실제 파일 경로와 구별되는, 메시별 편집 문서의 고유 ID.
    static FString MakeDocumentKey(const FString& MeshPath);
    const char* GetEditorTypeName() const override { return "PhysicsAsset"; }
    bool UsesPreviewSkybox() const override { return true; }
    UPhysicsAsset* GetPhysicsAsset() const;
    const FString& GetMeshAssetPath() const { return MeshAssetPath; }
    bool CreateAsset();
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
    const FGizmo* GetPreviewGizmo() const override { return ShapeGizmo.GetTarget() ? &ShapeGizmo : nullptr; }
    void SelectShape(ECollisionShape Type, int32 Index);
    bool IsShapeSelected(ECollisionShape Type, int32 Index) const;
    const FPhysicsShapeSelection& GetShapeSelection() const { return ShapeSelection; }
    void SetGizmoMode(EGizmoMode Mode);
    void SetGizmoSpace(EGizmoSpace Space);
    EGizmoMode GetGizmoMode() const { return ShapeGizmo.GetMode(); }
    EGizmoSpace GetGizmoSpace() const { return ShapeGizmo.GetSpace(); }
    bool IsGizmoDragging() const { return ShapeGizmo.IsUsing(); }
    float GetBodyOpacity() const { return BodyOpacity; }
    void SetBodyOpacity(float Value) { BodyOpacity = FMath::Clamp(Value, 0.02f, 0.45f); }
    bool AreBodiesVisible() const { return bShowBodies; }
    void SetBodiesVisible(bool bVisible);
    bool AreConstraintsVisible() const { return bShowConstraints; }
    void SetConstraintsVisible(bool bVisible) { bShowConstraints = bVisible; }
    const FString& GetPreviewWarning() const { return PreviewWarning; }

protected:
    bool InitializeResources() override;
    void ShutdownResources() override;
    void TickAfterWorld(float DeltaTime) override;
    bool IsViewportInteractionBusy() const override { return ShapeGizmo.IsUsing(); }
    void OnBoneSelected() override;
    void CancelViewportInteraction() override;
    TUniquePtr<IEditorTab> CreateTab() override;

private:
    enum class EAction { None, Close, Reload };
    FString MeshAssetPath;
    FString FileError;
    FPhysicsAssetEditModel EditModel;
    EAction PendingAction = EAction::None;
    bool bCloseConfirmed = false;
    bool bShowBodies = true;
    bool bShowConstraints = true;
    FString PreviewWarning;
    bool GetBodyWorldTransform(int32 Bone, FTransform& Out);
    TArray<FPhysicsPreviewShape> GatherPreviewShapes();
    void SyncShapeGizmo();
    bool UpdateShapeGizmo();
    FPhysicsAssetPreview BodyPreview;
    FPhysicsShapeSelection ShapeSelection;
    FGizmo ShapeGizmo;
    USceneComponent* ShapeGizmoTarget = nullptr;
    FTransform DragStartPose;
    FMatrix DragBoneInverse;
    FQuat DragBoneRotation;
    float BodyOpacity = 0.16f;
};
