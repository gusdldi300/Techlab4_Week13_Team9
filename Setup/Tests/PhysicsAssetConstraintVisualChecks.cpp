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

struct PreviewField { using Type = USkeletalMeshComponent* FSkeletalPreviewEditor::*; friend Type MemberOf(PreviewField); };
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
    auto* Mesh = NewObject<USkeletalMesh>();
    Mesh->SetPath("ConstraintVisualFixture.fbx.bin");
    FBoneInfo Root; Root.Name="Root";
    FBoneInfo Child; Child.Name="Child"; Child.ParentIndex=0;
    auto& Bones=const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton()); Bones.Add(Root); Bones.Add(Child);
    FSkeletalMeshLODRenderData LOD; LOD.BindAABB=FBox{FVector(-10,-10,-10),FVector(10,10,10)};
    const_cast<FSkeletalMeshRenderData&>(Mesh->GetRenderData()).LODRenderData.Add(std::move(LOD));
    auto* Asset=NewObject<UPhysicsAsset>(Mesh);
    for (const char* Name : {"Root","Child"})
    {
        auto* Body=NewObject<UBodySetup>(Asset); Body->BoneName=FName(Name);
        Asset->BodySetups.Add(Body);
    }
    auto* Joint=NewObject<UPhysicsConstraintTemplate>(Asset);
    Joint->ConstraintBone1=FName("Child"); Joint->ConstraintBone2=FName("Root");
    Asset->PhysicsConstraint.Add(Joint); Asset->UpdateBodySetupIndexMap(); Mesh->SetPhysicsAsset(Asset);
    auto* Component=ComponentFor(*Mesh);
    auto* Engine=NewObject<UEditorEngine>();
    {
        FEditorManager Manager(*Engine);
        FPhysicsAssetEditor Editor(*Engine,Manager,Mesh->GetPath(),*Mesh);
        Editor.*MemberOf(PreviewField{})=Component;
        Editor.*MemberOf(SelectionField{})=1;
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
        Editor.SetConstraintsVisible(false); Check(Draw()==std::array<int,2>{0,0},"hidden constraints have no faces");
        Editor.*MemberOf(PreviewField{})=nullptr;
    }
    delete Engine; delete Component; PhysicsAssetBuilder::DestroyGeneratedAsset(Asset); delete Mesh;
    std::printf("PASS: %d constraint visual checks\n",Checks);
}
