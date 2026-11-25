#define main RuntimeGrabChecksMain
#include "PhysicsAssetGrabChecks.cpp"
#undef main
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Editor/KurisuEd/EditorEngine.h"
#include "Render/RenderCommand.h"

struct PreviewComponent { using Type = USkeletalMeshComponent* FSkeletalPreviewEditor::*; friend Type MemberOf(PreviewComponent); };
struct PreviewBones { using Type = TArray<FEditorBoneItem> FSkeletalPreviewEditor::*; friend Type MemberOf(PreviewBones); };
struct ViewportField { using Type = TUniquePtr<FEditorViewportClient> FAssetEditor::*; friend Type MemberOf(ViewportField); };
struct WidthField { using Type = uint32 FEditorViewportClient::*; friend Type MemberOf(WidthField); };
struct HeightField { using Type = uint32 FEditorViewportClient::*; friend Type MemberOf(HeightField); };
struct EditorTick { using Type = void(FPhysicsAssetEditor::*)(float); friend Type MemberOf(EditorTick); };
struct EditorInstance { using Type = TUniquePtr<FPhysicsAssetInstance> FPhysicsAssetEditor::*; friend Type MemberOf(EditorInstance); };
template struct Access<PreviewComponent, &FSkeletalPreviewEditor::MeshComponent>;
template struct Access<PreviewBones, &FSkeletalPreviewEditor::BoneItems>;
template struct Access<ViewportField, &FAssetEditor::ViewportClient>;
template struct Access<WidthField, &FEditorViewportClient::Width>;
template struct Access<HeightField, &FEditorViewportClient::Height>;
template struct Access<EditorTick, &FPhysicsAssetEditor::TickAfterWorld>;
template struct Access<EditorInstance, &FPhysicsAssetEditor::SimulationInstance>;

int main()
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    FString Error;
    Check(FPaths::InitEngineDir() && FPaths::SetProject(std::filesystem::current_path()/"DefaultProject/DefaultProject.kproject", &Error),"paths");
    Check(MemberOf(Init{})(),"PhysX init");
    FRenderDevice Device; RenderCommand::Init(&Device);
    auto* Mesh = NewObject<USkeletalMesh>();
    Mesh->SetPath(FPaths::ToUtf8(std::filesystem::current_path()/"DefaultProject/Content/InputGrabCheck.fbx.bin"));
    FBoneInfo Bone; Bone.Name = "Root"; Bone.LocalBindPose.Location = FVector(0,0,100);
    const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton()).Add(Bone);
    FSkeletalMeshLODRenderData LOD; LOD.BindAABB = FBox{FVector(-5,-5,95),FVector(5,5,105)};
    const_cast<FSkeletalMeshRenderData&>(Mesh->GetRenderData()).LODRenderData.Add(std::move(LOD));
    auto* Asset = NewObject<UPhysicsAsset>(Mesh); auto* Body = NewObject<UBodySetup>(Asset);
    Body->BoneName=FName("Root"); Body->MassOverrideKg=1;
    FKBoxElem Box; Box.X=Box.Y=Box.Z=10; Body->AggGeom.BoxElems.Add(Box);
    Asset->BodySetups.Add(Body); Asset->UpdateBodySetupIndexMap(); Mesh->SetPhysicsAsset(Asset);
    auto* Component = ComponentFor(*Mesh);
    auto* Engine = NewObject<UEditorEngine>();
    {
        FEditorManager Manager(*Engine);
        FPhysicsAssetEditor Editor(*Engine,Manager,Mesh->GetPath(),*Mesh);
        Editor.*MemberOf(PreviewComponent{}) = Component;
        auto& BoneItems = Editor.*MemberOf(PreviewBones{}); BoneItems.SetNum(1); BoneItems[0].BoneIndex=0; BoneItems[0].Name="Root";
        Editor.*MemberOf(ViewportField{}) = MakeUnique<FEditorViewportClient>();
        auto* View = Editor.GetViewportClient();
        View->*MemberOf(WidthField{})=800; View->*MemberOf(HeightField{})=600;
        View->SetViewLocation(FVector(-100,0,100)); View->SetViewRotation(FRotator(0,0,0));
        Editor.GetEditModel().ResetHistory();
        const auto Input = [&Editor](const FViewportPointerInput& Pointer, uint32 Width=800) {
            Editor.SetViewportState(Width,Width?600:0,{},Pointer);
            (Editor.*MemberOf(EditorTick{}))(1.0f/60);
        };
        FViewportPointerInput Pointer;
        Pointer.MouseUV=FVector2(0.5f,0.5f);Pointer.bHovered=Pointer.bFocused=Pointer.bMousePressed=Pointer.bMouseDown=true;
        Check(Editor.StartSimulation(),"editor simulation starts");
        Input(Pointer); Check(!Editor.IsSimulationGrabbing(),"ordinary LMB selects without grab");
        Editor.StopSimulation(); Check(Editor.StartSimulation(),"restart after ordinary click");
        Pointer.bCtrlDown=true;
        Input(Pointer); Check(Editor.IsSimulationGrabbing(),"Ctrl LMB starts grab and selection does not cancel it");
        Check(Editor.GetSelectedBoneIndex()==0 && Editor.IsShapeSelected(ECollisionShape::Box,0),"clicked body highlighted");
        Pointer.bCtrlDown=Pointer.bMousePressed=false; Pointer.MouseUV=FVector2(0.55f,0.4f);
        for(int I=0;I<120;++I) Input(Pointer);
        Check(Editor.IsSimulationGrabbing(),"releasing Ctrl keeps LMB grab");
        const auto& Instance=Editor.*MemberOf(EditorInstance{});
        Check(Instance->GetGrabTarget().Z>110 && (Instance->GetGrabTarget()-Instance->GetGrabPoint()).Size()<1,"editor ray plane lifts physical body");
        Pointer.bMouseDown=false;Input(Pointer);Check(!Editor.IsSimulationGrabbing(),"LMB release drops body");
        for(int Mode=0;Mode<5;++Mode)
        {
            Editor.StopSimulation();Check(Editor.StartSimulation(),"restart cancellation fixture");
            Pointer={};Pointer.MouseUV=FVector2(0.5f,0.5f);Pointer.bHovered=Pointer.bFocused=Pointer.bMouseDown=Pointer.bMousePressed=Pointer.bCtrlDown=true;
            Input(Pointer);Check(Editor.IsSimulationGrabbing(),"regrab before cancellation");
            Pointer.bMousePressed=false;
            if(Mode==0){Pointer.bCancel=true;Input(Pointer);}
            if(Mode==1){Pointer.bFocused=false;Input(Pointer);}
            if(Mode==2){Input(Pointer,0);}
            if(Mode==3){Editor.SetSimulationPaused(true);}
            if(Mode==4){Editor.StopSimulation();}
            Check(!Editor.IsSimulationGrabbing(),"Esc/focus/hidden viewport/pause/stop release grab");
        }
        Editor.StopSimulation();
        Check(!Editor.IsDirty() && !Editor.GetEditModel().CanUndo(),"grab never dirties editor or adds undo history");
        Check(std::abs(Component->GetBoneWorldMatrix(0).TransformPosition(FVector::ZeroVector).Z-100)<0.001f,"stop restores pre-simulation pose");
        Editor.*MemberOf(PreviewComponent{})=nullptr;
    }
    delete Engine;delete Component;PhysicsAssetBuilder::DestroyGeneratedAsset(Asset);delete Mesh;
    MemberOf(Shutdown{})();
    std::printf("PASS: %d editor grab input checks\n",Checks);
}
