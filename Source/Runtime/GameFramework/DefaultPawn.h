#pragma once

#include "GameFramework/Pawn.h"
#include "DefaultPawn.generated.h"

class UCameraComponent;
class UStaticMeshComponent;

UCLASS()
class ADefaultPawn : public APawn
{
	GENERATED_BODY()
public:
	ADefaultPawn();
	// MovementComponent가 없으므로 입력은 Pawn에 누적되고, 여기서 소비해 이동한다
	void Tick(float DeltaTime) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	// 이동 기준 방향. 카메라가 폰보다 기울어 붙어 있으므로 화면이 보는 방향(카메라 회전)을 쓴다
	FRotator GetMovementInputRotation() const override;
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }
	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
private:
	UCameraComponent* CameraComponent = nullptr;
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	float MoveSpeed = 20.0f;
};
