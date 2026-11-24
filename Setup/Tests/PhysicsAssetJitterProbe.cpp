#include "EnginePCH.h"
#include "Physics/PhysicsAssetBuilder.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMeshObject.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Core/Misc/Paths.h"
#include <PxPhysicsAPI.h>
#include <cstdio>

template<class Tag, typename Tag::Type Member> struct Access { friend typename Tag::Type MemberOf(Tag) { return Member; } };
struct Init { using Type=bool(*)(); friend Type MemberOf(Init); };
struct Shutdown { using Type=void(*)(); friend Type MemberOf(Shutdown); };
struct MeshField { using Type=USkeletalMesh* USkinnedMeshComponent::*; friend Type MemberOf(MeshField); };
struct ObjectField { using Type=TUniquePtr<FSkeletalMeshObject> USkinnedMeshComponent::*; friend Type MemberOf(ObjectField); };
struct BodiesField { using Type=TArray<TUniquePtr<FBodyInstance>> FPhysicsAssetInstance::*; friend Type MemberOf(BodiesField); };
struct JointsField { using Type=TArray<physx::PxD6Joint*> FPhysicsAssetInstance::*; friend Type MemberOf(JointsField); };
template struct Access<Init,&FPhysicsCore::InitPhysX>;
template struct Access<Shutdown,&FPhysicsCore::ShutdownPhysX>;
template struct Access<MeshField,&USkinnedMeshComponent::SkeletalMesh>;
template struct Access<ObjectField,&USkinnedMeshComponent::MeshObject>;
template struct Access<BodiesField,&FPhysicsAssetInstance::Bodies>;
template struct Access<JointsField,&FPhysicsAssetInstance::Joints>;
class FNoRender : public FSkeletalMeshObject
{
public:
    explicit FNoRender(USkeletalMesh* M):FSkeletalMeshObject(M){}
    void Update(const TArray<FMatrix>&) override {}
    const FVertexFactory* GetVertexFactory(uint32) const override { return nullptr; }
};
static void Require(bool B,const char* What) { if(!B){ std::printf("FAIL %s\n",What);std::exit(1); } }

// Same rejection rule used by FPhysicsAssetEditor::GetBodyWorldTransform.
static int MissingShapes(USkeletalMeshComponent& C)
{
    int Missing=0;
    for(const auto* Body:C.GetSkeletalMesh()->GetPhysicsAsset()->BodySetups)
    {
        const int Bone=C.FindBoneIndex(Body->BoneName.ToString());
        const FMatrix World=C.GetBoneWorldMatrix(Bone);
        FTransform T; T.Location=World.TransformPosition(FVector::ZeroVector);T.Scale=C.GetWorldScale3D();
        FQuat Q=FQuat::Identity();
        for(int I=Bone;I>=0;I=C.GetBones()[I].ParentIndex)
        { const auto L=C.GetBoneLocalTransform(I);Q=L.Rotation*Q;T.Scale=T.Scale*L.Scale; }
        T.Rotation=(C.GetWorldRotation().Quaternion()*Q).Normalized();
        const FMatrix Reconstructed=T.GetLocalMatrix();
        bool Reject=false;
        for(int R=0;R<3;++R)for(int K=0;K<3;++K)
            Reject|=!std::isfinite(World.M[R][K]) || std::abs(World.M[R][K]-Reconstructed.M[R][K])>0.001f*std::max(1.0f,std::abs(World.M[R][K]));
        Missing+=Reject;
    }
    return Missing;
}
static void Run(USkeletalMesh& Mesh,const char* Name,bool Projection,bool SelfCollision,bool FloorEnabled)
{
    auto* C=NewObject<USkeletalMeshComponent>();
    C->*MemberOf(MeshField{})=&Mesh;C->*MemberOf(ObjectField{})=MakeUnique<FNoRender>(&Mesh);
    C->ResetPose();C->RefreshBoneTransformsNow();
    auto Scene=MakePhysicsScene();Scene->CreatePhysicsScene();Require(Scene->SetGravity(FVector(0,0,-980)),"gravity");
    FBodyInstance Floor;
    if(FloorEnabled)
    { FBodyInstanceInitParams P;P.WorldLocation=FVector(0,0,-5);FKBoxElem B;B.X=B.Y=2000;B.Z=10;P.FallbackGeom.BoxElems.Add(B);Floor.InitBody(nullptr,P,nullptr,Scene.get()); }
    FPhysicsAssetInstance Instance;Require(Instance.Build(*C,Scene.get()),"build");
    auto& Bodies=Instance.*MemberOf(BodiesField{});auto& Joints=Instance.*MemberOf(JointsField{});
    for(auto* J:Joints)J->setConstraintFlag(physx::PxConstraintFlag::ePROJECTION,Projection);
    if(!SelfCollision)for(auto& A:Bodies)for(auto& B:Bodies)if(A.get()!=B.get())A->DisableCollisionWith(B.get());
    Require(Instance.SetSimulating(*C,true),"simulate");
    int MissingBefore=0,MissingAfter=0,FramesMissing=0,MinAwake=999,MaxAwake=0;
    double Speed=0,AngularSpeed=0,Displacement=0,Contacts=0;float MaxGap=0;int Samples=0;
    TArray<FVector> Previous;Previous.SetNum(Bodies.Num());
    for(int Frame=0;Frame<2400;++Frame)
    {
        C->RefreshBoneTransformsNow();
        Scene->StartFrame(1.0f/120);Scene->EndFrame();Instance.ApplyToMesh(*C);
        const int Before=MissingShapes(*C);MissingBefore+=Before;FramesMissing+=Before>0;
        C->RefreshBoneTransformsNow();MissingAfter+=MissingShapes(*C);
        const auto S=Instance.GetDebugStats();MaxGap=std::max(MaxGap,S.MaxJointSeparation);
        int Awake=0;double V=0,W=0,D=0;
        for(int I=0;I<Bodies.Num();++I)
        {
            const auto* Actor=Bodies[I]->GetActorHandle().Actor->is<physx::PxRigidDynamic>();
            Awake+=!Actor->isSleeping();V+=Actor->getLinearVelocity().magnitude();W+=Actor->getAngularVelocity().magnitude();
            const auto P=Actor->getGlobalPose().p;const FVector Position(P.x,P.y,P.z);
            if(Frame>0)D+=(Position-Previous[I]).Size();Previous[I]=Position;
        }
        if(Frame>=1800)
        {MinAwake=std::min(MinAwake,Awake);MaxAwake=std::max(MaxAwake,Awake);Speed+=V/Bodies.Num();AngularSpeed+=W/Bodies.Num();Displacement+=D/Bodies.Num();Contacts+=Scene->GetStats().NumContactPairs;++Samples;}
    }
    std::printf("%s projection=%d self=%d floor=%d: missing frames %d/2400, body rejects before=%d after=%d; last5s awake=%d..%d/%d mean_speed=%.4f mean_angular=%.4f displacement/frame=%.5f contacts=%.2f max_gap=%.4f\n",
        Name,Projection,SelfCollision,FloorEnabled,FramesMissing,MissingBefore,MissingAfter,MinAwake,MaxAwake,Bodies.Num(),Speed/Samples,AngularSpeed/Samples,Displacement/Samples,Contacts/Samples,MaxGap);
    Instance.Clear();Floor.TermBody();delete C;
}
int main()
{
    std::setvbuf(stdout,nullptr,_IONBF,0);FString Error;const auto Root=std::filesystem::current_path();
    Require(FPaths::InitEngineDir()&&FPaths::SetProject(Root/"DefaultProject/DefaultProject.kproject",&Error),"paths");
    Require(MemberOf(Init{})(),"PhysX");
    for(const char* Name:{"Sonic_HeadHit","Kurisu_Running"})
    {
        const auto Path=Root/"DefaultProject/Content/FBX"/Name/(FString(Name)+".fbx.bin");FFbxBakedMesh Data;
        Require(FFbxMeshBake::Read(FPaths::ToUtf8(Path),Data,Error),"fixture");
        auto* Mesh=NewObject<USkeletalMesh>();Mesh->SetPath(FPaths::ToUtf8(Path));
        const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton())=std::move(Data.SkeletalMesh.Bones);
        FSkeletalMeshLODRenderData LOD;LOD.BindVertices=std::move(Data.SkeletalMesh.BindVertices);LOD.SkinWeights=std::move(Data.SkeletalMesh.SkinWeight);LOD.BindAABB=Data.SkeletalMesh.BindAABB;
        const_cast<FSkeletalMeshRenderData&>(Mesh->GetRenderData()).LODRenderData.Add(std::move(LOD));
        PhysicsAssetBuilder::FAutoGenerateSettings Settings;auto* Asset=PhysicsAssetBuilder::CreateFromSkeleton(*Mesh,Settings,Error);Require(Asset!=nullptr,"generate");Mesh->SetPhysicsAsset(Asset);
        Run(*Mesh,Name,true,true,true);
        Run(*Mesh,Name,false,true,true);
        Run(*Mesh,Name,true,false,true);
        Run(*Mesh,Name,false,false,true);
        Run(*Mesh,Name,true,false,false);
        PhysicsAssetBuilder::DestroyGeneratedAsset(Asset);
    }
    MemberOf(Shutdown{})();return 0;
}
