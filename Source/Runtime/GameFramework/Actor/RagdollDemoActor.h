#pragma once

#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "RagdollDemoActor.generated.h"

class UTextRenderComponent;

// Asset-driven animation -> ragdoll demo. K restores the original scene placement.
UCLASS()
class ARagdollDemoActor : public ASkeletalMeshActor
{
    GENERATED_BODY()

public:
    ARagdollDemoActor();
    void BeginPlay() override;
    void Tick(float DeltaTime) override;
    bool StartRagdoll();
    bool ResetDemo();
    bool IsReady() const { return bReady; }
    const FString& GetDemoError() const { return DemoError; }

    UPROPERTY(EditAnywhere)
    FString AnimationPath = "FBX/Sonic_Running/Sonic_Running.anim.bin";
    UPROPERTY(EditAnywhere)
    bool bAutoRagdoll = false;
    UPROPERTY(EditAnywhere)
    float AutoRagdollDelay = 3.0f;
    UPROPERTY(EditAnywhere)
    bool bMoveWhileAnimating = false;
    UPROPERTY(EditAnywhere)
    float MoveSpeed = 1.8f;
    UPROPERTY(EditAnywhere)
    float MaxTravelDistance = 6.0f;

private:
    bool Fail(const FString& Message);
    void UpdateLabel();
    UTextRenderComponent* StatusLabel = nullptr;
    UTextRenderComponent* ControlsLabel = nullptr;
    FTransform InitialTransform;
    FVector LastAnimationVelocity = FVector::ZeroVector;
    float Elapsed = 0.0f;
    float Travelled = 0.0f;
    bool bReady = false;
    bool bRDown = false;
    bool bKDown = false;
    bool bMDown = false;
    bool bTDown = false;
    FString DemoError;
};
