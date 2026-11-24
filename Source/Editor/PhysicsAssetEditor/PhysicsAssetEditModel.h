#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Physics/PhysicsConstraintTemplate.h"

class USkeletalMesh;
class UBodySetup;
struct FKAggregateGeom;
enum class ECollisionShape : uint8;
enum class EPhysicsType : uint8;

struct FPhysicsConstraintSettings
{
    FTransform ChildFrame;
    FTransform ParentFrame;
    EConstraintMotion XMotion = EConstraintMotion::Locked;
    EConstraintMotion YMotion = EConstraintMotion::Locked;
    EConstraintMotion ZMotion = EConstraintMotion::Locked;
    float LinearLimit = 0;
    EConstraintMotion Swing1Motion = EConstraintMotion::Limited;
    EConstraintMotion Swing2Motion = EConstraintMotion::Limited;
    EConstraintMotion TwistMotion = EConstraintMotion::Limited;
    float Swing1LimitDegrees = 45;
    float Swing2LimitDegrees = 45;
    float TwistLimitDegrees = 15;
    bool bDisableCollision = true;
};

// 에디터의 데이터 변경과 트랜잭션. 렌더러 및 ImGui와 독립적이다.
class FPhysicsAssetEditModel
{
public:
    FPhysicsAssetEditModel(USkeletalMesh& InMesh, const FString& InMeshPath)
        : Mesh(InMesh), MeshPath(InMeshPath) {}

    void ResetHistory();
    UBodySetup* GetBody(int32 BoneIndex) const;
    bool AddShape(int32 BoneIndex, ECollisionShape Type);
    bool RemoveBody(int32 BoneIndex);
    bool RemoveShape(int32 BoneIndex, ECollisionShape Type, int32 ShapeIndex);
    bool SetBodyProperties(int32 BoneIndex, EPhysicsType Type, const FKAggregateGeom& Geometry);
    bool GetShapeTransform(int32 BoneIndex, ECollisionShape Type, int32 Index, FTransform& Out) const;
    bool SetShapeTransform(int32 BoneIndex, ECollisionShape Type, int32 Index, const FTransform& Pose);
    bool GetShapeSize(int32 BoneIndex, ECollisionShape Type, int32 Index, FVector& Out) const;
    bool ScaleShape(int32 BoneIndex, ECollisionShape Type, int32 Index, const FVector& StartSize, const FVector& Factor);
    bool CancelInteraction() { return Apply(CommittedText); }
    bool CanConnect(int32 ChildBone, int32 ParentBone) const;
    int32 FindConstraint(int32 ChildBone) const;
    int32 GetConstraintParent(int32 ChildBone) const;
    bool GetConstraintSettings(int32 ChildBone, FPhysicsConstraintSettings& Out) const;
    bool AddConstraint(int32 ChildBone, int32 ParentBone);
    bool RemoveConstraint(int32 ChildBone);
    bool SetConstraintParent(int32 ChildBone, int32 ParentBone);
    bool SetConstraintSettings(int32 ChildBone, const FPhysicsConstraintSettings& Settings);
    bool ResetConstraintFrames(int32 ChildBone);
    bool AlignParentFrame(int32 ChildBone);
    void Commit(bool bInteractionActive);
    bool Undo();
    bool Redo();
    bool RestoreSaved();
    void MarkSaved();
    bool IsDirty() const { return CurrentText != SavedText; }
    bool CanUndo() const { return CurrentText != CommittedText || !UndoStack.IsEmpty(); }
    bool CanRedo() const { return CurrentText == CommittedText && !RedoStack.IsEmpty(); }
    const FString& GetError() const { return Error; }

private:
    bool Capture();
    bool Apply(const FString& Text);
    static void PushHistory(TArray<FString>& Stack, const FString& Text);
    USkeletalMesh& Mesh;
    FString MeshPath;
    FString CurrentText, CommittedText, SavedText, Error;
    TArray<FString> UndoStack, RedoStack;
};
