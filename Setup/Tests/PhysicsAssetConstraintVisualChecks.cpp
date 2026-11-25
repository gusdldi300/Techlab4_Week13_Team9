// Run with RunPhysicsAssetGrabChecks.ps1 -ConstraintVisuals.
#define main RuntimeGrabChecksMain
#include "PhysicsAssetGrabChecks.cpp"
#undef main
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Editor/KurisuEd/EditorEngine.h"
#include "Render/LineBatcher.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include <array>
#include <cmath>
#include <fstream>

struct PreviewField { using Type = USkeletalMeshComponent* FSkeletalPreviewEditor::*; friend Type MemberOf(PreviewField); };
struct BoneItemsField { using Type = TArray<FEditorBoneItem> FSkeletalPreviewEditor::*; friend Type MemberOf(BoneItemsField); };
template struct Access<BoneItemsField, &FSkeletalPreviewEditor::BoneItems>;
struct ApplyFrameField { using Type = bool(FPhysicsAssetEditor::*)(const FTransform&); friend Type MemberOf(ApplyFrameField); };
template struct Access<ApplyFrameField, &FPhysicsAssetEditor::ApplyEditFrame>;
struct SyncField { using Type = void(FPhysicsAssetEditor::*)(); friend Type MemberOf(SyncField); };
template struct Access<SyncField, &FPhysicsAssetEditor::SyncEditGizmo>;
struct ViewportField { using Type = TUniquePtr<FEditorViewportClient> FAssetEditor::*; friend Type MemberOf(ViewportField); };
struct WidthField { using Type = uint32 FEditorViewportClient::*; friend Type MemberOf(WidthField); };
struct HeightField { using Type = uint32 FEditorViewportClient::*; friend Type MemberOf(HeightField); };
struct TickField { using Type = void(FPhysicsAssetEditor::*)(float); friend Type MemberOf(TickField); };
struct EditFrameField { using Type = bool(FPhysicsAssetEditor::*)(FTransform&, int32&) const; friend Type MemberOf(EditFrameField); };
template struct Access<ViewportField, &FAssetEditor::ViewportClient>;
template struct Access<WidthField, &FEditorViewportClient::Width>;
template struct Access<HeightField, &FEditorViewportClient::Height>;
template struct Access<TickField, &FPhysicsAssetEditor::TickAfterWorld>;
template struct Access<EditFrameField, &FPhysicsAssetEditor::GetEditFrame>;
struct SelectionField { using Type = int32 FSkeletalPreviewEditor::*; friend Type MemberOf(SelectionField); };
template struct Access<PreviewField, &FSkeletalPreviewEditor::MeshComponent>;
template struct Access<SelectionField, &FSkeletalPreviewEditor::SelectedBoneIndex>;
struct FCapturedTriangle { FLineBatchVertex Vertices[3]; };
struct TriangleField { using Type = TArray<FCapturedTriangle>(*)(const FLineBatcher&); friend Type MemberOf(TriangleField); };
template<auto Member> struct TriangleAccess
{
    friend TriangleField::Type MemberOf(TriangleField)
    {
        return [](const FLineBatcher& Lines) {
            TArray<FCapturedTriangle> Result;
            for(const auto& Triangle : Lines.*Member)
                Result.Add({Triangle.Vertices[0],Triangle.Vertices[1],Triangle.Vertices[2]});
            return Result;
        };
    }
};
template struct TriangleAccess<&FLineBatcher::Triangles>;

static void CheckAlphaFaces(FRenderDevice& Device, FLineBatcher& Lines)
{
    D3D11_TEXTURE2D_DESC Desc{};
    Desc.Width = Desc.Height = 64; Desc.MipLevels = Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; Desc.SampleDesc.Count = 1;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> Target, Readback;
    ComPtr<ID3D11RenderTargetView> RTV;
    Check(SUCCEEDED(Device.GetDevice()->CreateTexture2D(&Desc,nullptr,Target.GetAddressOf())),"offscreen target");
    Check(SUCCEEDED(Device.GetDevice()->CreateRenderTargetView(Target.Get(),nullptr,RTV.GetAddressOf())),"offscreen RTV");
    Desc.BindFlags = 0; Desc.Usage = D3D11_USAGE_STAGING; Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(SUCCEEDED(Device.GetDevice()->CreateTexture2D(&Desc,nullptr,Readback.GetAddressOf())),"offscreen readback");
    const D3D11_VIEWPORT Viewport{0,0,64,64,0,1};
    Device.GetContext()->RSSetViewports(1,&Viewport);
    const float Background[] = {0,0,1,1};
    for (int Reverse = 0; Reverse < 2; ++Reverse)
    {
        Device.GetContext()->OMSetRenderTargets(1,RTV.GetAddressOf(),nullptr);
        Device.GetContext()->ClearRenderTargetView(RTV.Get(),Background);
        Lines.BeginFrame();
        const FVector A(-0.8f,-0.8f,0.2f), B(0,0.8f,0.2f), C(0.8f,-0.8f,0.2f);
        Lines.AddTriangle(A,Reverse ? C : B,Reverse ? B : C,FVector4(1,0,0,0.25f));
        Lines.OnRender(FMatrix::Identity);
        Device.GetContext()->OMSetRenderTargets(0,nullptr,nullptr);
        Device.GetContext()->CopyResource(Readback.Get(),Target.Get());
        D3D11_MAPPED_SUBRESOURCE Mapped{};
        Check(SUCCEEDED(Device.GetContext()->Map(Readback.Get(),0,D3D11_MAP_READ,0,&Mapped)),"map pixels");
        const auto* Pixel = static_cast<const unsigned char*>(Mapped.pData) + 32 * Mapped.RowPitch + 32 * 4;
        Check(std::abs(int(Pixel[0])-64)<=2 && Pixel[1]==0 && std::abs(int(Pixel[2])-191)<=2,
            Reverse ? "back face is filled and alpha blended" : "front face is filled and alpha blended");
        Device.GetContext()->Unmap(Readback.Get(),0);
        ComPtr<ID3D11DepthStencilState> Depth;
        Device.GetContext()->OMGetDepthStencilState(Depth.GetAddressOf(),nullptr);
        D3D11_DEPTH_STENCIL_DESC State{}; Depth->GetDesc(&State);
        Check(State.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ZERO,"faces do not write depth");
    }
    Lines.BeginFrame();
    Check(MemberOf(TriangleField{})(Lines).IsEmpty(),"frame reset clears faces");
}

int main()
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    FString Error;
    Check(FPaths::InitEngineDir() && FPaths::SetProject(std::filesystem::current_path()/"DefaultProject/DefaultProject.kproject",&Error),"paths");
    FRenderDevice Device; RenderCommand::Init(&Device);
    FRenderResourceManager::Init();
    FLineBatcher Lines; Check(Lines.Init(nullptr,nullptr),"batch init");
    CheckAlphaFaces(Device,Lines);
    const auto Scratch=std::filesystem::current_path()/"Build/ConstraintSelectionChecks";
    std::filesystem::create_directories(Scratch);
    const auto MeshFile=Scratch/"Fixture.fbx.bin";
    const bool CreatedFixture=!std::filesystem::exists(MeshFile);
    if(CreatedFixture) { std::ofstream File(MeshFile); File << "fixture"; }
    auto* Mesh = NewObject<USkeletalMesh>();
    Mesh->SetPath(FPaths::ToUtf8(MeshFile));
    FBoneInfo Root; Root.Name="Root";
    FBoneInfo Child; Child.Name="Child"; Child.ParentIndex=0;
    auto& Bones=const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton()); Bones.Add(Root); Bones.Add(Child);
    FBoneInfo Hand; Hand.Name="Hand"; Hand.ParentIndex=1; Hand.LocalBindPose.Location=FVector(0,5,0); Bones.Add(Hand);
    FSkeletalMeshLODRenderData LOD; LOD.BindAABB=FBox{FVector(-10,-10,-10),FVector(10,10,10)};
    const_cast<FSkeletalMeshRenderData&>(Mesh->GetRenderData()).LODRenderData.Add(std::move(LOD));
    auto* Asset=NewObject<UPhysicsAsset>(Mesh);
    for (const char* Name : {"Root","Child","Hand"})
    {
        auto* Body=NewObject<UBodySetup>(Asset); Body->BoneName=FName(Name);
        FKBoxElem Box; Box.X=Box.Y=Box.Z=1; Body->AggGeom.BoxElems.Add(Box);
        Asset->BodySetups.Add(Body);
    }
    auto* Joint=NewObject<UPhysicsConstraintTemplate>(Asset);
    Joint->ConstraintBone1=FName("Child"); Joint->ConstraintBone2=FName("Root");
    Asset->PhysicsConstraint.Add(Joint); Asset->UpdateBodySetupIndexMap(); Mesh->SetPhysicsAsset(Asset);
    auto* Wrist=NewObject<UPhysicsConstraintTemplate>(Asset);
    Wrist->ConstraintBone1=FName("Hand"); Wrist->ConstraintBone2=FName("Child");
    Asset->PhysicsConstraint.Add(Wrist);
    auto* Component=ComponentFor(*Mesh);
    auto* Engine=NewObject<UEditorEngine>();
    {
        FEditorManager Manager(*Engine);
        FPhysicsAssetEditor Editor(*Engine,Manager,Mesh->GetPath(),*Mesh);
        Editor.*MemberOf(PreviewField{})=Component;
        auto& Items=Editor.*MemberOf(BoneItemsField{}); Items.SetNum(3);
        for(int I=0; I<3; ++I) { Items[I].BoneIndex=I; Items[I].Name=Bones[I].Name; }
        Editor.SelectBone(1);
        Editor.*MemberOf(ViewportField{})=MakeUnique<FEditorViewportClient>();
        auto* Viewport=Editor.GetViewportClient();
        Viewport->*MemberOf(WidthField{})=800; Viewport->*MemberOf(HeightField{})=600;
        Viewport->SetViewLocation(FVector(-100,0,0)); Viewport->SetViewRotation(FRotator(0,0,0));
        Editor.SetViewportState(800,600,{});
        Editor.SetBodiesVisible(false);
        const auto Draw = [&] {
            Lines.BeginFrame(); Editor.OnRenderLine(Lines);
            std::array<int,2> Counts{};
            for (const auto& Triangle : MemberOf(TriangleField{})(Lines))
            {
                const auto& Color=Triangle.Vertices[0].Color;
                Check(Color.W>0 && Color.W<1,"guide faces are translucent");
                ++Counts[Color.X>Color.Y ? 0 : 1];
                for(const auto& Vertex : Triangle.Vertices)
                    Check(std::isfinite(Vertex.Position.X+Vertex.Position.Y+Vertex.Position.Z),"finite guide vertex");
            }
            return Counts;
        };
        Check(Editor.GetConstraintView()==EPhysicsAssetConstraintView::Selected,"selected limits is the default");
        Check(Draw()==std::array<int,2>{96,48},"forearm body shows only its upstream elbow limits");
        Check(Editor.ShouldDrawConstraintLimits(1) && !Editor.ShouldDrawConstraintLimits(2),"forearm does not display downstream wrist");
        Check(Editor.GetSelectedConstraintBone()<0 && Editor.IsShapeSelected(ECollisionShape::Box,0),
            "displaying parent limits preserves shape selection");
        FTransform BodyPose; int32 BodyBone=-1;
        Check((Editor.*MemberOf(EditFrameField{}))(BodyPose,BodyBone)==false,
            "hidden bodies have no shape gizmo even when joint limits are visible");
        Editor.SetBodiesVisible(true);
        Check((Editor.*MemberOf(EditFrameField{}))(BodyPose,BodyBone) && BodyBone==1,
            "visible body gizmo still edits the selected shape");
        Editor.GetEditModel().ResetHistory();
        FPhysicsConstraintSettings BeforeShapeEdit, AfterShapeEdit;
        Editor.GetEditModel().GetConstraintSettings(1,BeforeShapeEdit);
        const auto OriginalBodyPose=BodyPose; BodyPose.Location=FVector(2,3,4);
        Check((Editor.*MemberOf(ApplyFrameField{}))(BodyPose),"shape edit works while parent limits are shown");
        FTransform UpdatedShape;
        Check(Editor.GetEditModel().GetShapeTransform(1,ECollisionShape::Box,0,UpdatedShape) &&
            (UpdatedShape.Location-BodyPose.Location).Size()<0.001f,"shape rather than joint frame was moved");
        Editor.GetEditModel().GetConstraintSettings(1,AfterShapeEdit);
        Check((BeforeShapeEdit.ChildFrame.Location-AfterShapeEdit.ChildFrame.Location).Size()<0.001f &&
            (BeforeShapeEdit.ParentFrame.Location-AfterShapeEdit.ParentFrame.Location).Size()<0.001f,
            "shape gizmo does not move either joint anchor");
        Check((Editor.*MemberOf(ApplyFrameField{}))(OriginalBodyPose),"restore shape pose");
        Editor.SetBodiesVisible(false);
        Editor.SelectBone(0);
        Check(Draw()==std::array<int,2>{0,0},"root body without parent joint shows no limits");
        Editor.SelectBone(-1);
        Check(Draw()==std::array<int,2>{0,0},"empty selection shows no limits");
        Editor.SelectBone(2);
        Check(Draw()==std::array<int,2>{96,48} && !Editor.ShouldDrawConstraintLimits(1),"hand body shows only wrist limits");
        Editor.SelectBone(1);
        Check(Editor.SelectConstraintFrame(1,false),"select elbow joint");
        Check(Editor.GetShapeSelection().Bone<0,"joint selection clears body shape highlight");
        Check(Draw()==std::array<int,2>{96,48},"two swings form one red cone, twist is green fan");
        for(const auto Limits : {FVector2(30,30),FVector2(30,60),FVector2(0.1f,179.9f)})
        {
            Joint->Swing1LimitDegrees=Limits.X; Joint->Swing2LimitDegrees=Limits.Y;
            Check(Draw()[0]==96,"symmetric, asymmetric and extreme cones");
            for(const auto& Triangle : MemberOf(TriangleField{})(Lines))
            {
                if(Triangle.Vertices[0].Color.X<Triangle.Vertices[0].Color.Y) continue;
                const FVector D=Triangle.Vertices[1].Position.Normalized();
                // Avoid cancellation in 1+X for directions close to 180 degrees.
                const float HalfCos=sqrtf(D.X<0 ? (D.Y*D.Y+D.Z*D.Z)/(2*(1-D.X)) : (1+D.X)*0.5f);
                const float QY=-D.Z/(2*HalfCos), QZ=D.Y/(2*HalfCos);
                const float Y=4*atan2f(QY,1+HalfCos)*180/PI, Z=4*atan2f(QZ,1+HalfCos)*180/PI;
                const float Ellipse=Y*Y/(Limits.X*Limits.X)+Z*Z/(Limits.Y*Limits.Y);
                if(fabsf(Ellipse-1)>=0.003f) std::printf("Limits %f %f direction %f %f %f angles %f %f ellipse %f\n",Limits.X,Limits.Y,D.X,D.Y,D.Z,Y,Z,Ellipse);
                Check(fabsf(Ellipse-1)<0.003f,"cone boundary satisfies PhysX swing ellipse");
            }
        }
        Joint->Swing1LimitDegrees=30; Joint->Swing2LimitDegrees=60;
        Draw();
        const auto Original=MemberOf(TriangleField{})(Lines);
        Joint->PriAxis2=FVector(0,1,0); Joint->SecAxis2=FVector(-1,0,0);
        Draw();
        const auto Rotated=MemberOf(TriangleField{})(Lines);
        const FQuat Rotation=FQuat::MakeFromAxisAngle(FVector(0,0,1),PI/2);
        Check(Original.Num()==Rotated.Num(),"frame rotation preserves topology");
        for(int I=0; I<Original.Num(); ++I)
            for(int V=0; V<3; ++V)
                Check((Rotation.RotateVector(Original[I].Vertices[V].Position)-Rotated[I].Vertices[V].Position).Size()<0.001f,
                    "cone and twist follow joint frame rotation");
        Joint->PriAxis2=FVector(1,0,0); Joint->SecAxis2=FVector(0,1,0);
        for(auto A : {EConstraintMotion::Locked,EConstraintMotion::Limited,EConstraintMotion::Free})
            for(auto B : {EConstraintMotion::Locked,EConstraintMotion::Limited,EConstraintMotion::Free})
            {
                Joint->Swing1Motion=A; Joint->Swing2Motion=B;
                const auto Counts=Draw();
                if(A==EConstraintMotion::Locked && B==EConstraintMotion::Locked) Check(Counts[0]==0,"locked swings have no face");
                if(A==EConstraintMotion::Free && B==EConstraintMotion::Free) Check(Counts[0]==0,"free swings have no false cone");
                Check(Counts[1]==48,"swing modes preserve twist fan");
            }
        Joint->TwistMotion=EConstraintMotion::Locked; Check(Draw()[1]==0,"locked twist hidden");
        Joint->TwistMotion=EConstraintMotion::Free; Check(Draw()[1]==48,"free twist is full filled disk");
        Joint->Swing1Motion=Joint->Swing2Motion=EConstraintMotion::Limited;
        Joint->TwistMotion=EConstraintMotion::Limited;
        Editor.SetConstraintView(EPhysicsAssetConstraintView::All);
        Check(Draw()==std::array<int,2>{192,96},"all limits displays both adjacent joints");
        Editor.SetConstraintView(EPhysicsAssetConstraintView::Selected);
        const auto Handles=Editor.GetConstraintHandles();
        Check(Handles.Num()==4,"selected limits retains markers for both joints");
        FViewportPointerInput Pointer;
        for(const auto& Handle : Handles)
            if(Handle.ChildBone==2 && !Handle.bParentFrame)
                Pointer.MouseUV=FVector2(Handle.Position.X/800,Handle.Position.Y/600);
        Pointer.bHovered=Pointer.bFocused=Pointer.bMousePressed=Pointer.bMouseDown=true;
        Editor.SetViewportState(800,600,{},Pointer);
        (Editor.*MemberOf(TickField{}))(0);
        if(Editor.GetSelectedConstraintBone()!=2)
        {
            for(const auto& H : Handles) std::printf("Handle %d parent %d xy %.2f %.2f depth %.5f\n",H.ChildBone,H.bParentFrame,H.Position.X,H.Position.Y,H.Depth);
            std::printf("Click %.2f %.2f selected %d tree %d\n",Pointer.MouseUV.X*800,Pointer.MouseUV.Y*600,Editor.GetSelectedConstraintBone(),Editor.GetSelectedBoneIndex());
        }
        Check(Editor.GetSelectedConstraintBone()==2,"viewport marker click selects wrist joint");
        Editor.SetViewportState(800,600,{});
        Check(Editor.GetSelectedConstraintBone()==2 && !Editor.ShouldDrawConstraintLimits(1) && Editor.ShouldDrawConstraintLimits(2),
            "wrist selection excludes elbow");
        Check(Draw()==std::array<int,2>{96,48},"only wrist faces displayed");
        Check(Editor.SelectEditTarget(EPhysicsAssetEditTarget::ParentFrame) && Editor.IsConstraintFrameSelected(2,true),
            "frame switching preserves wrist identity");
        Editor.GetEditModel().ResetHistory();
        Check(Editor.GetEditModel().GetError().empty(),"selection fixture serializes for undo");
        FTransform EditPose; int32 EditBone=-1;
        Check((Editor.*MemberOf(EditFrameField{}))(EditPose,EditBone) && EditBone==1,
            "parent frame gizmo uses wrist parent body");
        FPhysicsConstraintSettings ElbowBefore, WristBefore, After;
        Editor.GetEditModel().GetConstraintSettings(1,ElbowBefore);
        Editor.GetEditModel().GetConstraintSettings(2,WristBefore);
        auto Pose=WristBefore.ParentFrame; Pose.Location=FVector(1,2,3);
        // Deliberately move tree focus without triggering a body selection callback.
        Editor.*MemberOf(SelectionField{})=1;
        Check((Editor.*MemberOf(ApplyFrameField{}))(Pose),"frame edits use selected joint rather than tree focus");
        Editor.GetEditModel().GetConstraintSettings(2,After);
        Check((After.ParentFrame.Location-Pose.Location).Size()<0.001f,"wrist parent frame changed");
        Editor.GetEditModel().GetConstraintSettings(1,After);
        Check((After.ParentFrame.Location-ElbowBefore.ParentFrame.Location).Size()<0.001f,"elbow frame unchanged");
        Editor.SelectBone(1);
        Check(Editor.GetSelectedConstraintBone()<0 && Draw()==std::array<int,2>{96,48} && !Editor.ShouldDrawConstraintLimits(2),"body reselection clears joint selection and displays upstream elbow");
        Check(!Editor.SelectEditTarget(EPhysicsAssetEditTarget::ParentFrame),"body cannot implicitly select an unrelated joint frame");
        Check(Editor.SelectConstraintFrame(2,false),"reselect wrist before deletion");
        Editor.GetEditModel().Commit(false);
        Check(Editor.GetEditModel().RemoveConstraint(2),"delete selected wrist");
        Check(Editor.GetSelectedConstraintBone()<0 && !Editor.ShouldDrawConstraintLimits(2),"deleted joint has no selection or limits");
        (Editor.*MemberOf(SyncField{}))();
        Check(Editor.GetEditTarget()==EPhysicsAssetEditTarget::Shape,"invalid joint falls back to body selection");
        Check(Draw()==std::array<int,2>{0,0},"deleted parent joint leaves body with no limits");
        Editor.GetEditModel().Commit(false);
        const bool Undone=Editor.GetEditModel().Undo();
        if(!Undone) std::printf("Undo error: %s\n",Editor.GetEditModel().GetError().c_str());
        Check(Undone,"undo selected joint deletion");
        Check(Editor.GetSelectedConstraintBone()<0,"undo does not resurrect stale selection");
        Check(Draw()==std::array<int,2>{96,48} && Editor.GetEditTarget()==EPhysicsAssetEditTarget::Shape,
            "undo restores body's upstream limits without switching to joint editing");
        Check(Editor.SelectConstraintFrame(2,true),"restored joint can be selected");
        Check(Editor.GetEditModel().Redo(),"redo joint deletion");
        Check(Editor.GetSelectedConstraintBone()<0,"redo invalidates restored joint selection");
        Editor.SetConstraintsVisible(false); Check(Draw()==std::array<int,2>{0,0},"hidden constraints have no faces");
        Check(Editor.GetConstraintHandles().IsEmpty(),"hidden constraints have no pick handles");
        Editor.*MemberOf(PreviewField{})=nullptr;
    }
    delete Engine; delete Component; PhysicsAssetBuilder::DestroyGeneratedAsset(Asset); delete Mesh;
    if(CreatedFixture) std::filesystem::remove(MeshFile);
    std::printf("PASS: %d constraint visual checks\n",Checks);
}
