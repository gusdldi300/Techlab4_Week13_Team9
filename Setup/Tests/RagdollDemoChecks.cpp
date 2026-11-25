#include "EnginePCH.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Core/Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor/RagdollDemoActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/AmbientLightActor.h"
#include "GameFramework/PlayerController.h"
#include "Input/GameKeyboardInput.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetBuilder.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene_PhysX.h"
#include "Render/RenderCommand.h"
#include "Render/SkeletalMesh.h"
#include "Serialization/JsonArchive.h"
#include <PxPhysicsAPI.h>
#include <cstdio>
#include <limits>

template<class Tag, typename Tag::Type Member> struct FTestAccess { friend typename Tag::Type MemberOf(Tag) { return Member; } };
struct FInit { using Type = bool(*)(); friend Type MemberOf(FInit); };
struct FShutdown { using Type = void(*)(); friend Type MemberOf(FShutdown); };
struct FInstance { using Type = TUniquePtr<FPhysicsAssetInstance> USkeletalMeshComponent::*; friend Type MemberOf(FInstance); };
struct FBodies { using Type = TArray<TUniquePtr<FBodyInstance>> FPhysicsAssetInstance::*; friend Type MemberOf(FBodies); };
template struct FTestAccess<FInit,&FPhysicsCore::InitPhysX>;
template struct FTestAccess<FShutdown,&FPhysicsCore::ShutdownPhysX>;
template struct FTestAccess<FInstance,&USkeletalMeshComponent::PhysicsAssetInstance>;
template struct FTestAccess<FBodies,&FPhysicsAssetInstance::Bodies>;
static int Checks = 0;
static void Check(bool Success, const char* Message)
{
    ++Checks;
    if (!Success) { std::printf("FAIL: %s\n",Message); std::exit(1); }
}

static int PrepareAsset()
{
    const auto Path = FPaths::ProjectContentDir()/"FBX/Sonic_Running/Sonic_Running.fbx.bin";
    FString Error;
    FFbxBakedMesh Data;
    const bool Loaded = FFbxMeshBake::Read(FPaths::ToUtf8(Path),Data,Error);
    if (!Loaded) std::printf("%s\n",Error.c_str());
    Check(Loaded,"Read actual Sonic Running FBX cache");
    auto* Mesh = NewObject<USkeletalMesh>();
    Mesh->SetPath(FPaths::ToUtf8(Path));
    const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton()) = Data.SkeletalMesh.Bones;
    FSkeletalMeshLODRenderData LOD;
    LOD.BindVertices = std::move(Data.SkeletalMesh.BindVertices);
    LOD.SkinWeights = std::move(Data.SkeletalMesh.SkinWeight);
    LOD.BindAABB = Data.SkeletalMesh.BindAABB;
    const_cast<FSkeletalMeshRenderData&>(Mesh->GetRenderData()).LODRenderData.Add(std::move(LOD));
    const auto AssetPath = PhysicsAssetFile::GetDefaultPath(Mesh->GetPath());
    UPhysicsAsset* Existing = nullptr;
    if (PhysicsAssetFile::Read(AssetPath,*Mesh,Existing,Error) && !Existing->BodySetups.IsEmpty())
    {
        std::printf("Keeping existing nonempty Physics Asset (%d bodies).\n",Existing->BodySetups.Num());
        return 0;
    }
    auto* Asset = PhysicsAssetBuilder::CreateFromSkeleton(*Mesh,{},Error);
    Check(Asset && Asset->BodySetups.Num() >= 12,"Generate connected humanoid bodies for Running itself");
    Check(PhysicsAssetFile::Write(AssetPath,Mesh->GetPath(),*Mesh,*Asset,Error),"Save Running Physics Asset");
    std::printf("Prepared Sonic_Running: %d bodies / %d constraints\n",Asset->BodySetups.Num(),Asset->PhysicsConstraint.Num());
    return 0;
}

static ARagdollDemoActor* FindDemo(UWorld& World)
{
    for (auto* Actor : World.GetPersistentLevel()->GetActors())
        if (auto* Demo = Cast<ARagdollDemoActor>(Actor)) return Demo;
    return nullptr;
}

static void Key(UWorld& World, EKeyCode Code, bool Down)
{
    if (Down) FInputSystem::OnKeyDown(static_cast<int32>(Code));
    else FInputSystem::OnKeyUp(static_cast<int32>(Code));
    DispatchGameKeyboardInput([&](int32 KeyCode, bool Pressed) { World.GetPlayerController()->InputKey(KeyCode,Pressed); });
    World.Tick(1.0f/60);
    FInputSystem::UpdateInputStates();
}

static void CheckDemo(float Dt, const TArray<uint8>& Scene)
{
    auto* World = UWorld::CreateWorld(EWorldType::PIE,false);
    Check(World && FJsonArchive::LoadWorldFromBytes(World,Scene),"Reload demo scene in a real PIE world");
    Check(World->GetActorNum()==20,"Scene retains camera, lights, physical floor, ramp and demo");
    auto* Demo = FindDemo(*World);
    Check(Demo != nullptr,"Scene spawns registered demo actor");
    Demo->bAutoRagdoll = false;
    Demo->bMoveWhileAnimating = false;
    World->BeginPlay();
    if (!Demo->IsReady()) std::printf("Demo error: %s\n",Demo->GetDemoError().c_str());
    Check(Demo->IsReady(),"BeginPlay loads animation and mesh-specific Physics Asset");
    auto* Mesh = Demo->GetSkeletalMeshComponent();
    auto& Instance = Mesh->*MemberOf(FInstance{});
    const auto& Bodies = (*Instance).*MemberOf(FBodies{});
    const int Hips = Mesh->FindBoneIndex("mixamorig:Hips");
    Check(Hips >= 0 && Bodies.Num() >= 12,"Real Running skeleton and humanoid bodies");
    auto* Native = static_cast<FPhysicsScene_PhysX*>(World->GetPhysicsScene())->GetNativeScene();
    Check(Native->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC)>=3,"Saved floor, ramp and step receive physical bodies");
    const auto Actors = Native->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC);
    const auto Joints = Native->getNbConstraints();
    for (float Time : {0.13f,0.41f,0.83f})
    {
        Check(Demo->ResetDemo(),"Reset before a different animation phase");
        Mesh->GetSingleNodeInstance()->SetPosition(Time);
        Mesh->RefreshAnimationPose();
        const auto FrozenLocalPose = Mesh->GetBoneSpaceTransforms();
        const float FrozenAnimationTime = Mesh->GetSingleNodeInstance()->GetCurrentTime();
        const int Finger = Mesh->FindBoneIndex("mixamorig:LeftHandIndex1");
        const int Toe = Mesh->FindBoneIndex("mixamorig:RightToeBase");
        Check(Finger >= 0 && Toe >= 0,"Fixture has unbodied finger and toe bones");
        TArray<FVector> Before;
        for (int I=0; I<Mesh->GetBones().Num(); ++I) Before.Add(Mesh->GetBoneWorldMatrix(I).TransformPosition(FVector::ZeroVector));
        Check(Demo->StartRagdoll(),"Transition from an arbitrary running pose");
        Mesh->TickComponent(0);
        Mesh->RefreshBoneTransformsNow();
        for (int I=0; I<Before.Num(); ++I)
            Check((Before[I]-Mesh->GetBoneWorldMatrix(I).TransformPosition(FVector::ZeroVector)).Size()<0.0001f,"No bind-pose jump when enabling physics");
        Check(!Mesh->SetRagdollSimulating(true,FVector(std::numeric_limits<float>::quiet_NaN(),0,0)),"Reject nonfinite initial velocity");
        for (int Frame=0; Frame<static_cast<int>(3.0f/Dt); ++Frame)
        {
            World->Tick(Dt);
            Check(Mesh->GetSingleNodeInstance()->GetCurrentTime()==FrozenAnimationTime,"Animation time stays frozen during full ragdoll");
            for (int Bone : {Finger,Toe})
            {
                const auto Local = Mesh->GetBoneLocalTransform(Bone);
                const auto FrozenLocal = FrozenLocalPose[Bone];
                Check((Local.Location-FrozenLocal.Location).Size()<0.00001f &&
                    (Local.Rotation.RotateVector(FVector(1,0,0))-FrozenLocal.Rotation.RotateVector(FVector(1,0,0))).Size()<0.00001f &&
                    (Local.Rotation.RotateVector(FVector(0,1,0))-FrozenLocal.Rotation.RotateVector(FVector(0,1,0))).Size()<0.00001f,
                    "Unbodied finger and toe retain their entry pose while parent bodies simulate");
            }
            const auto Stats = Mesh->GetRagdollDebugStats();
            Check(std::isfinite(Stats.MaxJointSeparation) && Stats.MaxJointSeparation<0.12f,"Joints stay connected during floor contact");
        }
        const auto HipPosition = Mesh->GetBoneWorldMatrix(Hips).TransformPosition(FVector::ZeroVector);
        const int Head = Mesh->FindBoneIndex("mixamorig:Head");
        const auto HeadPosition = Mesh->GetBoneWorldMatrix(Head).TransformPosition(FVector::ZeroVector);
        std::printf("%.0f FPS phase %.2f: hips %.4f -> %.4f, head %.4f -> %.4f, bodies %u\n",1.0f/Dt,Time,Before[Hips].Z,HipPosition.Z,Before[Head].Z,HeadPosition.Z,Actors);
        const auto* Body0 = Bodies[0]->GetActorHandle().Actor->is<physx::PxRigidDynamic>();
        std::printf("Native z %.4f, vz %.4f, kinematic %d, steps %d, dilation %.3f, sim %d\n",Body0->getGlobalPose().p.z,Body0->getLinearVelocity().z,int(Body0->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC)),World->GetPhysicsScene()->GetStats().NumStepsLastFrame,World->GetEffectiveTimeDilation(),int(Mesh->IsRagdollSimulating()));
        Check(HipPosition.Z>0 && HeadPosition.Z<Before[Head].Z-0.15f,"Ragdoll falls and remains above the physical floor");
        Check(Native->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC)==Actors && Native->getNbConstraints()==Joints,"Repeated transition does not duplicate physics objects");
    }
    Check(Demo->ResetDemo(),"Reset after floor contact");
    const auto Start = Demo->GetActorLocation();
    Demo->bMoveWhileAnimating = true;
    for (int I=0; I<30; ++I) World->Tick(Dt);
    Check(Demo->GetActorLocation().X>Start.X,"Animated actor moves along the runway");
    Check(Demo->StartRagdoll(),"Moving actor enters ragdoll");
    for (const auto& Body : Bodies)
    {
        const auto* Dynamic = Body->GetActorHandle().Actor->is<physx::PxRigidDynamic>();
        Check(!Dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC),"Full-body asset has no pinned body");
        Check((Dynamic->getLinearVelocity()-physx::PxVec3(Demo->MoveSpeed,0,0)).magnitude()<0.0001f,"World movement velocity reaches every ragdoll body");
    }
    World->Tick(Dt);
    const auto Frozen = Demo->GetActorLocation();
    World->Tick(Dt);
    Check((Demo->GetActorLocation()-Frozen).Size()<0.00001f,"Actor motion stops while bodies simulate");
    Key(*World,EKeyCode::K,true);
    Check(!Mesh->IsRagdollSimulating() && (Demo->GetActorLocation()-Start).Size()<0.00001f,"K restores initial placement and animation");
    Key(*World,EKeyCode::K,false);
    Check(Mesh->GetSingleNodeInstance()->GetCurrentTime()>0,"Animation resumes after K reset");
    Key(*World,EKeyCode::R,true);
    Check(Mesh->IsRagdollSimulating(),"R input enables ragdoll after animation evaluation");
    Key(*World,EKeyCode::R,false);
    Key(*World,EKeyCode::M,true);
    Check(!Demo->bMoveWhileAnimating,"M reaches the demo through the real game keyboard dispatcher");
    Key(*World,EKeyCode::M,false);
    Key(*World,EKeyCode::T,true);
    Check(Demo->bAutoRagdoll,"T reaches the demo through the real game keyboard dispatcher");
    Key(*World,EKeyCode::T,false);
    Check(Demo->ResetDemo(),"Reset before automatic transition");
    Demo->bAutoRagdoll = true;
    Demo->AutoRagdollDelay = 0.2f;
    for (int I=0; I<static_cast<int>(0.3f/Dt)+1; ++I) World->Tick(Dt);
    Check(Mesh->IsRagdollSimulating(),"Timer starts ragdoll automatically");
    Check(Native->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC)==Actors && Native->getNbConstraints()==Joints,"Object counts remain stable after key and timer transitions");
    World->EndPlay();
    World->ClearWorld();
    Check(Native->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC)==0 && Native->getNbConstraints()==0,"PIE teardown releases ragdoll bodies and joints");
    delete World;
    std::printf("Demo verified at %.0f FPS\n",1.0f/Dt);
}

int main(int Argc, char** Argv)
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    FString Error;
    Check(FPaths::InitEngineDir() && FPaths::SetProject(std::filesystem::current_path()/"DefaultProject/DefaultProject.kproject",&Error),"Project paths");
    if (Argc==2 && FString(Argv[1])=="--prepare-asset") return PrepareAsset();
    Check(MemberOf(FInit{})(),"PhysX initialization");
    FRenderDevice Device;
    RenderCommand::Init(&Device);
    USkinnedMeshComponent::bUseGPUSkinning = false;
    UAssetManager::Init();
    AStaticMeshActor::StaticClass(); ADirectionalLightActor::StaticClass(); AAmbientLightActor::StaticClass();
    auto* EditorWorld = UWorld::CreateWorld(EWorldType::Editor,false);
    Check(FJsonArchive::LoadWorld(EditorWorld,FPaths::ToUtf8(FPaths::ProjectScenesDir()/"RagdollDemo.scene")),"Load saved demo scene");
    Check(FindDemo(*EditorWorld)!=nullptr,"Demo actor survives scene deserialization");
    TArray<uint8> Scene;
    Check(FJsonArchive::SaveWorldToBytes(EditorWorld,Scene),"Scene save round trip");
    auto* Duplicated = UWorld::GetDuplicatedWorldForPIE(EditorWorld);
    Check(Duplicated && FindDemo(*Duplicated),"Editor PIE duplication retains the demo actor");
    Duplicated->BeginPlay();
    Check(FindDemo(*Duplicated)->IsReady(),"Duplicated actor initializes independent animation and physics");
    Duplicated->ClearWorld();
    delete Duplicated;
    EditorWorld->ClearWorld();
    delete EditorWorld;
    for (float Dt : {1.0f/30,1.0f/60,1.0f/120}) CheckDemo(Dt,Scene);
    auto* MissingWorld = UWorld::CreateWorld(EWorldType::PIE,false);
    auto* MissingDemo = MissingWorld->SpawnActor<ARagdollDemoActor>();
    MissingDemo->GetSkeletalMeshComponent()->bEnablePhysicsAsset = false;
    MissingWorld->BeginPlay();
    Check(!MissingDemo->IsReady() && !MissingDemo->GetDemoError().empty() && !MissingDemo->StartRagdoll(),"Missing Physics Asset reports an error without enabling simulation");
    Check(MissingDemo->GetSkeletalMeshComponent()->IsPlaying(),"Animation keeps playing when physics initialization fails");
    MissingWorld->Tick(1.0f/60);
    MissingWorld->ClearWorld();
    delete MissingWorld;
    MemberOf(FShutdown{})();
    std::printf("PASS: %d ragdoll demo checks\n",Checks);
    return 0;
}
