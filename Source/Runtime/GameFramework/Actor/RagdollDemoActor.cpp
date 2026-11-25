#include "EnginePCH.h"
#include "GameFramework/Actor/RagdollDemoActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Asset/AssetManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/TextRenderComponent.h"
#include "Core/Misc/Paths.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/KeyCode.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <cmath>

ARagdollDemoActor::ARagdollDemoActor()
{
    PrimaryActorTick.bCanEverTick = true;
    // Mesh evaluation runs in PrePhysics. Sample input and switch only after that pose is complete.
    PrimaryActorTick.TickGroup = ETickingGroup::PostUpdateWork;
    auto* Mesh = GetSkeletalMeshComponent();
    Mesh->PrimaryComponentTick.TickGroup = ETickingGroup::PrePhysics;
    Mesh->SetRelativeScale3D(FVector(0.01f));
    Mesh->bSimulatePhysics = false;
    if (FPaths::HasProject())
        if (auto* Asset = UAssetManager::GetAssetByPath<USkeletalMesh>("FBX/Sonic_Running/Sonic_Running.fbx.bin")) Mesh->SetSkeletalMesh(Asset);

    StatusLabel = CreateDefaultSubobject<UTextRenderComponent>("RagdollStatusLabel");
    StatusLabel->SetupAttachment(GetRootComponent());
    StatusLabel->SetRelativeLocation(FVector(2.5f,0,2.6f));
    StatusLabel->SetTextSize(0.2f);
    StatusLabel->SetText("ANIMATION");
    ControlsLabel = CreateDefaultSubobject<UTextRenderComponent>("RagdollControlsLabel");
    ControlsLabel->SetupAttachment(GetRootComponent());
    ControlsLabel->SetRelativeLocation(FVector(2.5f,0,2.35f));
    ControlsLabel->SetTextSize(0.16f);
    ControlsLabel->SetText("R: Ragdoll | K: Reset | M: Motion | T: Auto");
}

void ARagdollDemoActor::BeginPlay()
{
    InitialTransform = GetActorTransform();
    Super::BeginPlay();
    auto* Mesh = GetSkeletalMeshComponent();
    if (!Mesh->GetSkeletalMesh()) { Fail("Missing skeletal mesh"); return; }
    if (!std::isfinite(MoveSpeed) || MoveSpeed < 0 || !std::isfinite(MaxTravelDistance) || MaxTravelDistance < 0 ||
        !std::isfinite(AutoRagdollDelay) || AutoRagdollDelay < 0) { Fail("Demo timing and speed must be finite and nonnegative"); return; }
    auto* Animation = UAssetManager::LoadAnimSequence(AnimationPath);
    if (!Animation) { Fail("Cannot load animation: " + AnimationPath); return; }
    Mesh->PlayAnimation(Animation,true);
    Mesh->GetAnimInstance()->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
    ResetDemo();
}

bool ARagdollDemoActor::Fail(const FString& Message)
{
    if (DemoError != Message) KRS_LOG(Warning,"[RagdollDemo] {}",Message);
    DemoError = Message;
    bReady = false;
    UpdateLabel();
    return false;
}

bool ARagdollDemoActor::ResetDemo()
{
    auto* Mesh = GetSkeletalMeshComponent();
    auto* Animation = Mesh->GetSingleNodeInstance();
    if (!Animation || !Animation->GetCurrentAsset()) return Fail("Missing animation");
    if (Mesh->IsRagdollSimulating() && !Mesh->SetRagdollSimulating(false)) return Fail("Cannot stop ragdoll");
    GetRootComponent()->SetTransform(InitialTransform);
    Animation->SetPosition(0);
    Animation->SetLooping(true);
    Animation->SetPlaying(true);
    Mesh->RefreshAnimationPose();
    // Reuse the instance on repeated resets. Rebuild only when it has not been created yet.
    if (!Mesh->SyncPhysicsAssetFromAnimation(true) && (!Mesh->RecreatePhysicsAsset() || !Mesh->SyncPhysicsAssetFromAnimation(true)))
        return Fail("Missing or invalid Physics Asset: create bodies and constraints for this mesh");
    Mesh->ConsumeRootMotion();
    LastAnimationVelocity = FVector::ZeroVector;
    Elapsed = Travelled = 0;
    DemoError.clear();
    bReady = true;
    UpdateLabel();
    return true;
}

bool ARagdollDemoActor::StartRagdoll()
{
    if (!bReady) return false;
    auto* Mesh = GetSkeletalMeshComponent();
    if (Mesh->IsRagdollSimulating()) return true;
    if (!Mesh->SetRagdollSimulating(true,LastAnimationVelocity)) return Fail("Cannot enable ragdoll for this Physics Asset");
    LastAnimationVelocity = FVector::ZeroVector;
    UpdateLabel();
    return true;
}

void ARagdollDemoActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    const auto* Controller = GetWorld() ? GetWorld()->GetPlayerController() : nullptr;
    const auto Down = [Controller](EKeyCode Key) { return Controller && Controller->IsKeyDown(static_cast<int32>(Key)); };
    const bool R = Down(EKeyCode::R), K = Down(EKeyCode::K), M = Down(EKeyCode::M), T = Down(EKeyCode::T);
    const bool Reset = K && !bKDown, Drop = R && !bRDown;
    if (M && !bMDown) bMoveWhileAnimating = !bMoveWhileAnimating;
    if (T && !bTDown) bAutoRagdoll = !bAutoRagdoll;
    bRDown = R; bKDown = K; bMDown = M; bTDown = T;
    if (Reset) { ResetDemo(); return; }
    auto* Mesh = GetSkeletalMeshComponent();
    if (bReady && !Mesh->IsRagdollSimulating() && DeltaTime > 0)
    {
        Elapsed += DeltaTime;
        LastAnimationVelocity = FVector::ZeroVector;
        if (bMoveWhileAnimating)
        {
            const float Distance = std::min(std::max(MaxTravelDistance-Travelled,0.0f),MoveSpeed*DeltaTime);
            const FVector Delta = GetActorQuat().GetForwardVector()*Distance;
            GetRootComponent()->SetWorldLocation(GetActorLocation()+Delta,false);
            LastAnimationVelocity = Delta/DeltaTime;
            Travelled += Distance;
            Mesh->SyncPhysicsAssetFromAnimation();
        }
        if (Drop || (bAutoRagdoll && Elapsed >= AutoRagdollDelay)) StartRagdoll();
    }
    UpdateLabel();
}

void ARagdollDemoActor::UpdateLabel()
{
    if (!StatusLabel) return;
    const auto* Mesh = GetSkeletalMeshComponent();
    const FString State = !DemoError.empty() ? "ERROR: " + DemoError : Mesh->IsRagdollSimulating() ? "RAGDOLL" : "ANIMATION";
    StatusLabel->SetText(std::format("{} | Motion {} | Auto {}",State,
        bMoveWhileAnimating ? "ON" : "OFF",bAutoRagdoll ? "ON" : "OFF"));
    // Keep the label at the original stage position, independent of the falling skeleton.
    StatusLabel->SetWorldLocation(InitialTransform.Location+FVector(2.5f,0,2.6f),false);
    if (ControlsLabel) ControlsLabel->SetWorldLocation(InitialTransform.Location+FVector(2.5f,0,2.35f),false);
    const auto* Controller = GetWorld() ? GetWorld()->GetPlayerController() : nullptr;
    const auto* Camera = Controller ? Controller->GetCameraManager() : nullptr;
    if (Camera)
    {
        const FVector ToCamera = Camera->GetCameraCachePOV().Location-StatusLabel->GetWorldLocation();
        const float Yaw = std::atan2(ToCamera.Y,ToCamera.X)*180.0f/PI+180.0f;
        StatusLabel->SetWorldRotation(FRotator(0,Yaw,0),false);
        if (ControlsLabel) ControlsLabel->SetWorldRotation(FRotator(0,Yaw,0),false);
    }
}
