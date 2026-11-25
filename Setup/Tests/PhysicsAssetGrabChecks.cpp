#include "EnginePCH.h"
#include "Physics/PhysicsAssetBuilder.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMeshObject.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Core/Misc/Paths.h"
#include <PxPhysicsAPI.h>
#include <cstdio>
#include <limits>

// Test-only initialization/headless skinning, following PhysicsAssetJitterProbe.
template<class Tag, typename Tag::Type Member> struct Access { friend typename Tag::Type MemberOf(Tag) { return Member; } };
struct Init { using Type = bool(*)(); friend Type MemberOf(Init); };
struct Shutdown { using Type = void(*)(); friend Type MemberOf(Shutdown); };
struct MeshField { using Type = USkeletalMesh* USkinnedMeshComponent::*; friend Type MemberOf(MeshField); };
struct ObjectField { using Type = TUniquePtr<FSkeletalMeshObject> USkinnedMeshComponent::*; friend Type MemberOf(ObjectField); };
struct BodiesField { using Type = TArray<TUniquePtr<FBodyInstance>> FPhysicsAssetInstance::*; friend Type MemberOf(BodiesField); };
template struct Access<Init, &FPhysicsCore::InitPhysX>;
template struct Access<Shutdown, &FPhysicsCore::ShutdownPhysX>;
template struct Access<MeshField, &USkinnedMeshComponent::SkeletalMesh>;
template struct Access<ObjectField, &USkinnedMeshComponent::MeshObject>;
template struct Access<BodiesField, &FPhysicsAssetInstance::Bodies>;

class FNoRender final : public FSkeletalMeshObject
{
public:
    explicit FNoRender(USkeletalMesh* Mesh) : FSkeletalMeshObject(Mesh) {}
    void Update(const TArray<FMatrix>&) override {}
    const FVertexFactory* GetVertexFactory(uint32) const override { return nullptr; }
};

static int Checks = 0;
static void Check(bool Passed, const char* Message)
{
    ++Checks;
    if (!Passed) { std::printf("FAIL: %s\n", Message); std::exit(1); }
}

static USkeletalMeshComponent* ComponentFor(USkeletalMesh& Mesh)
{
    auto* Component = NewObject<USkeletalMeshComponent>();
    Component->*MemberOf(MeshField{}) = &Mesh;
    Component->*MemberOf(ObjectField{}) = MakeUnique<FNoRender>(&Mesh);
    Component->ResetPose();
    Component->RefreshBoneTransformsNow();
    return Component;
}

static void Step(FPhysicsScene& Scene, FPhysicsAssetInstance& Instance, USkeletalMeshComponent& Component, float Dt)
{
    Instance.SyncFromAnimation(Component);
    Scene.StartFrame(Dt);
    Scene.EndFrame();
    Instance.ApplyToMesh(Component);
    Component.RefreshBoneTransformsNow();
}

static void CheckPickingAndLifetime()
{
    auto* Mesh = NewObject<USkeletalMesh>();
    Mesh->SetPath(FPaths::ToUtf8(std::filesystem::current_path()/"DefaultProject/Content/GrabCheck.fbx.bin"));
    auto& Bones = const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton());
    FBoneInfo Root; Root.Name = "Root"; Root.LocalBindPose.Location = FVector(0,0,100); Bones.Add(Root);
    FSkeletalMeshLODRenderData LOD; LOD.BindAABB = FBox{FVector(-5,-35,90),FVector(5,35,110)};
    const_cast<FSkeletalMeshRenderData&>(Mesh->GetRenderData()).LODRenderData.Add(std::move(LOD));
    auto* Asset = NewObject<UPhysicsAsset>(Mesh);
    auto* Setup = NewObject<UBodySetup>(Asset); Setup->BoneName = FName("Root");
    FKSphereElem Sphere; Sphere.Center = FVector(0,-30,0); Sphere.Radius = 5;
    FKBoxElem Box; Box.X = Box.Y = Box.Z = 10;
    FKSphylElem Capsule; Capsule.Center = FVector(0,30,0); Capsule.Radius = 5; Capsule.Length = 10;
    Setup->AggGeom.SphereElems.Add(Sphere); Setup->AggGeom.BoxElems.Add(Box); Setup->AggGeom.SphylElems.Add(Capsule);
    Asset->BodySetups.Add(Setup); Asset->UpdateBodySetupIndexMap(); Mesh->SetPhysicsAsset(Asset);
    auto* Component = ComponentFor(*Mesh);
    auto Scene = MakePhysicsScene(); Scene->CreatePhysicsScene(); Scene->SetGravity(FVector::ZeroVector);
    FPhysicsAssetInstance Instance; Check(Instance.Build(*Component,Scene.get()), "single body build");
    FPhysicsBodyHit Hit;
    Check(!Instance.BeginGrab({FVector(-100,0,100),FVector(1,0,0)},Hit), "not simulating cannot grab");
    Check(Instance.SetSimulating(*Component,true), "start single body");
    auto* Actor = (Instance.*MemberOf(BodiesField{}))[0]->GetActorHandle().Actor->is<physx::PxRigidDynamic>();
    auto* NativeScene = Actor->getScene();
    const auto ActorCount = NativeScene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC);
    const auto ConstraintCount = NativeScene->getNbConstraints();
    physx::PxU32 OriginalPositionIterations, OriginalVelocityIterations;
    Actor->getSolverIterationCounts(OriginalPositionIterations,OriginalVelocityIterations);
    const ECollisionShape Types[] = {ECollisionShape::Sphere,ECollisionShape::Box,ECollisionShape::Sphyl};
    for (int Index = 0; Index < 3; ++Index)
    {
        Check(Instance.BeginGrab({FVector(-100,(Index-1)*30.0f,100),FVector(2,0,0)},Hit), "ray picks each native shape, nonunit direction");
        Check(Hit.Bone == 0 && Hit.ShapeIndex == 0 && Hit.Type == Types[Index], "shape identity");
        Check(std::abs(Hit.Position.X+5) < 0.01f && std::abs(Hit.Distance-95) < 0.01f, "exact surface point and distance");
        Check((Instance.GetGrabPoint()-Hit.Position).Size() < 0.001f, "initial anchor has no snap");
        Check(NativeScene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC) == ActorCount+1 &&
            NativeScene->getNbConstraints() == ConstraintCount+1, "one transient actor/joint");
        Instance.EndGrab(); Instance.EndGrab();
        physx::PxU32 PositionIterations, VelocityIterations;
        Actor->getSolverIterationCounts(PositionIterations,VelocityIterations);
        Check(PositionIterations == OriginalPositionIterations && VelocityIterations == OriginalVelocityIterations,
            "release restores original solver settings");
        Check(NativeScene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC) == ActorCount &&
            NativeScene->getNbConstraints() == ConstraintCount, "release is complete and idempotent");
    }
    Check(!Instance.BeginGrab({FVector(-100,1000,100),FVector(1,0,0)},Hit) && Hit.Bone == -1, "miss clears result");
    Check(!Instance.BeginGrab({FVector::ZeroVector,FVector::ZeroVector},Hit), "zero ray rejected");
    Check(Instance.BeginGrab({FVector(-100,0,100),FVector(1,0,0)},Hit), "begin drag");
    const FVector Target = Hit.Position+FVector(100,0,100);
    Check(!Instance.UpdateGrabTarget(FVector(std::numeric_limits<float>::quiet_NaN(),0,0)), "nonfinite target rejected");
    for (int I = 0; I < 360; ++I)
    {
        Instance.UpdateGrabTarget(Hit.Position+(Target-Hit.Position)*std::min((I+1)/60.0f,1.0f));
        Step(*Scene,Instance,*Component,1.0f/60);
    }
    std::printf("Single anchor error=%.6f speed=%.4f angular=%.4f\n",(Instance.GetGrabPoint()-Target).Size(),
        Actor->getLinearVelocity().magnitude(),Actor->getAngularVelocity().magnitude());
    Check((Instance.GetGrabPoint()-Target).Size() < 0.1f, "offcenter anchor converges while rotating");
    Check(std::abs(Actor->getGlobalPose().q.x)+std::abs(Actor->getGlobalPose().q.y)+std::abs(Actor->getGlobalPose().q.z) > 0.01f,
        "grab leaves body rotation free");
    const auto BeforeRelease = Actor->getLinearVelocity();
    Instance.EndGrab();
    Check((Actor->getLinearVelocity()-BeforeRelease).magnitude() == 0, "release preserves velocity");
    Check(Instance.BeginGrab({FVector(Actor->getGlobalPose().p.x-100,Actor->getGlobalPose().p.y,Actor->getGlobalPose().p.z),FVector(1,0,0)},Hit), "regrab current moving pose");
    Check(Instance.SetSimulating(*Component,false) && !Instance.IsGrabbing(), "disable simulation releases grab");
    Check(Instance.SetSimulating(*Component,true), "restart simulation");
    const auto P = Actor->getGlobalPose().p;
    Check(Instance.BeginGrab({FVector(P.x-100,P.y,P.z),FVector(1,0,0)},Hit), "grab before clear");
    Instance.Clear();
    Check(!Instance.IsGrabbing() && NativeScene->getNbConstraints() == 0 &&
        NativeScene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC) == 0, "clear releases handle before bodies");
    delete Component;
    Setup->PhysicsType = EPhysicsType::Kinematic;
    Component = ComponentFor(*Mesh);
    Check(Instance.Build(*Component,Scene.get()), "kinematic body build");
    Check(!Instance.SetSimulating(*Component,true) && !Instance.BeginGrab({FVector(-100,0,100),FVector(1,0,0)},Hit), "kinematic body cannot be grabbed");
    Instance.Clear(); delete Component;
    PhysicsAssetBuilder::DestroyGeneratedAsset(Asset); delete Mesh;
}

static void RunRagdoll(USkeletalMesh& Mesh, const char* Name, const FPhysicsGrabSettings& Settings, float Dt, bool Validate)
{
    auto* Component = ComponentFor(Mesh);
    auto Scene = MakePhysicsScene(true); Scene->CreatePhysicsScene(); Scene->SetGravity(FVector(0,0,-980));
    FBodyInstance Floor;
    FBodyInstanceInitParams Params; Params.WorldLocation = FVector(0,0,-5);
    FKBoxElem Box; Box.X = Box.Y = 5000; Box.Z = 10; Params.FallbackGeom.BoxElems.Add(Box);
    Floor.InitBody(nullptr,Params,nullptr,Scene.get());
    FPhysicsAssetInstance Instance; Check(Instance.Build(*Component,Scene.get()) && Instance.SetSimulating(*Component,true), "ragdoll build/simulate");
    auto& Bodies = Instance.*MemberOf(BodiesField{});
    // Grab the left hand when available: this tests lifting the full ragdoll through a light extremity.
    int Pick = 0;
    for (int I = 0; I < Mesh.GetPhysicsAsset()->BodySetups.Num(); ++I)
    {
        const FString Bone = Mesh.GetPhysicsAsset()->BodySetups[I]->BoneName.ToString();
        if (Bone.find("LeftHand") != FString::npos || Bone.find("hand_l") != FString::npos ||
            Bone.find("L-Hand") != FString::npos) { Pick = I; break; }
    }
    auto* PickActor = Bodies[Pick]->GetActorHandle().Actor;
    physx::PxShape* PickShape = nullptr; PickActor->getShapes(&PickShape,1);
    const auto ShapePose = PickActor->getGlobalPose()*PickShape->getLocalPose();
    const FVector Center(ShapePose.p.x,ShapePose.p.y,ShapePose.p.z);
    FRay PickRay{Center-FVector(1000,0,0),FVector(1,0,0)};
    FPhysicsBodyHit Hit;
    const int PickBone = Component->FindBoneIndex(Mesh.GetPhysicsAsset()->BodySetups[Pick]->BoneName.ToString());
    for (const auto& Direction : {FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)})
    {
        const FRay Candidate{Center-Direction*1000,Direction};
        if (Instance.RaycastBodies(Candidate,Hit) && Hit.Bone == PickBone) { PickRay = Candidate; break; }
    }
    Check(Instance.BeginGrab(PickRay,Hit,Settings) && Hit.Bone == PickBone, "ragdoll native ray grabs the hand");
    const FVector Start = Hit.Position;
    const FBox MeshBounds = Component->CalcBounds();
    std::printf("height=%.2f ",MeshBounds.Max.Z-MeshBounds.Min.Z);
    float MinStartZ = FLT_MAX;
    for (const auto& Body : Bodies) MinStartZ = std::min(MinStartZ,Body->GetActorHandle().Actor->getGlobalPose().p.z);
    double TailError = 0, TailSpeed = 0; int TailSamples = 0; float MaxGap = 0, MaxError = 0;
    FVector PreviousPoint = Start;
    const int Frames = static_cast<int>(12.0f/Dt);
    for (int Frame = 0; Frame < Frames; ++Frame)
    {
        const float Time = (Frame+1)*Dt;
        const float Lift = std::min(Time/1.5f,1.0f);
        const float Side = Time < 3 ? 60*std::sin(Time*2.0943951f) : 0;
        const FVector Target = Start+FVector(Side,0,250*Lift);
        Instance.UpdateGrabTarget(Target);
        Step(*Scene,Instance,*Component,Dt);
        const FVector CurrentPoint = Instance.GetGrabPoint();
        const float PointSpeed = (CurrentPoint-PreviousPoint).Size()/Dt;
        PreviousPoint = CurrentPoint;
        const float Error = (CurrentPoint-Target).Size();
        MaxError = std::max(MaxError,Error);
        MaxGap = std::max(MaxGap,Instance.GetDebugStats().MaxJointSeparation);
        for (const auto& Body : Bodies)
        {
            const auto* Actor = Body->GetActorHandle().Actor->is<physx::PxRigidDynamic>();
            Check(Actor->getGlobalPose().isFinite() && Actor->getLinearVelocity().isFinite(), "finite ragdoll motion");
        }
        if (Time >= 11)
        {
            TailError += Error; ++TailSamples;
            TailSpeed += PointSpeed;
        }
    }
    float MinEndZ = FLT_MAX;
    for (const auto& Body : Bodies) MinEndZ = std::min(MinEndZ,Body->GetActorHandle().Actor->getGlobalPose().p.z);
    std::printf("%s bodies=%d picked=%s dt=%.5f K=%.0f D=%.1f: steady_error=%.4f speed=%.4f max_error=%.3f joint_gap=%.4f lowest_lift=%.3f\n",
        Name,Bodies.Num(),Mesh.GetRefSkeleton()[Hit.Bone].Name.c_str(),Dt,Settings.Stiffness,Settings.Damping,
        TailError/TailSamples,TailSpeed/TailSamples,MaxError,MaxGap,MinEndZ-MinStartZ);
    if (Validate)
    {
        Check(TailError/TailSamples < 1, "ragdoll hold error below one preview unit");
        // A freely rotating ragdoll can keep swinging. Bound movement of the held point per 60 Hz step.
        Check(TailSpeed/TailSamples/60 < 1, "held point vibration below one unit per physics step");
        Check(MinEndZ-MinStartZ > 75, "whole ragdoll lifted clear of floor");
        Check(MaxGap < 3, "original ragdoll joint anchors stay connected");
    }
    const float HeldZ = Instance.GetGrabPoint().Z;
    Instance.EndGrab();
    for (int Frame = 0; Frame < static_cast<int>(0.4f/Dt); ++Frame) Step(*Scene,Instance,*Component,Dt);
    const int Grabbed = Mesh.GetPhysicsAsset()->FindBodyIndex(FName(Mesh.GetRefSkeleton()[Hit.Bone].Name));
    if (Validate) Check(Bodies[Grabbed]->GetActorHandle().Actor->getGlobalPose().p.z < HeldZ-20, "released ragdoll falls");
    Instance.Clear(); Floor.TermBody(); delete Component;
}

int main(int Argc, char** Argv)
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    FString Error;
    const auto Root = std::filesystem::current_path();
    Check(FPaths::InitEngineDir() && FPaths::SetProject(Root/"DefaultProject/DefaultProject.kproject",&Error), "paths");
    Check(MemberOf(Init{})(), "PhysX initialization");
    CheckPickingAndLifetime();
    const bool Tune = Argc > 1 && FString(Argv[1]) == "--tune";
    for (const char* Name : {"Sonic_HeadHit","Kurisu_Running"})
    {
        const auto Path = Root/"DefaultProject/Content/FBX"/Name/(FString(Name)+".fbx.bin");
        FFbxBakedMesh Data; Check(FFbxMeshBake::Read(FPaths::ToUtf8(Path),Data,Error), "read real skeletal fixture");
        auto* Mesh = NewObject<USkeletalMesh>(); Mesh->SetPath(FPaths::ToUtf8(Path));
        const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton()) = std::move(Data.SkeletalMesh.Bones);
        FSkeletalMeshLODRenderData LOD; LOD.BindVertices = std::move(Data.SkeletalMesh.BindVertices);
        LOD.SkinWeights = std::move(Data.SkeletalMesh.SkinWeight); LOD.BindAABB = Data.SkeletalMesh.BindAABB;
        const_cast<FSkeletalMeshRenderData&>(Mesh->GetRenderData()).LODRenderData.Add(std::move(LOD));
        PhysicsAssetBuilder::FAutoGenerateSettings Settings;
        auto* Asset = PhysicsAssetBuilder::CreateFromSkeleton(*Mesh,Settings,Error);
        Check(Asset != nullptr,"generate ragdoll"); Mesh->SetPhysicsAsset(Asset);
        FString Before, After;
        Check(PhysicsAssetFile::ToText(Mesh->GetPath(),*Mesh,*Asset,Before,Error), "asset snapshot before grab");
        if (Tune)
            for (float K : {6400.0f,8000.0f,10000.0f})
                for (float D : {40.0f,60.0f,80.0f,100.0f,120.0f}) RunRagdoll(*Mesh,Name,{K,D},1.0f/60,false);
        else
            for (float Dt : {1.0f/30,1.0f/60,1.0f/120}) RunRagdoll(*Mesh,Name,{},Dt,true);
        Check(PhysicsAssetFile::ToText(Mesh->GetPath(),*Mesh,*Asset,After,Error) && Before == After, "grab never modifies asset");
        PhysicsAssetBuilder::DestroyGeneratedAsset(Asset); delete Mesh;
    }
    MemberOf(Shutdown{})();
    std::printf("PASS: %d grab checks\n",Checks);
}
