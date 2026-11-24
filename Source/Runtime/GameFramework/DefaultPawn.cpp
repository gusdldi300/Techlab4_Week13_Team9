#include "EnginePCH.h"
#include "GameFramework/DefaultPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Asset/AssetManager.h"

ADefaultPawn::ADefaultPawn()
{
	SetActorEnableCollision(false);
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
	SetRootComponent(StaticMeshComponent);
	StaticMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
	// UE의 DefaultPawn처럼 게임 화면(PIE 카메라)에서는 구를 숨긴다.
	// 에디터 뷰는 게임 뷰가 아니므로 F8로 빠져나오면(SIE) 구가 보인다.
	StaticMeshComponent->SetHiddenInGame(true);
	CameraComponent->SetupAttachment(StaticMeshComponent);
	// 카메라는 폰 중심에 회전 없이 붙인다(UE DefaultPawn과 같음).
	// 예전처럼 뒤쪽 위(-5, 0, 8)에 붙이면 마우스로 폰을 돌릴 때 카메라가 제자리에서 돌지 않고
	// 숨겨진 구를 축으로 반지름 약 9.4의 원을 그리며 돈다. 폰은 MainCamera 위치에 생기므로 시작 화면도 MainCamera와 같아진다.
	CameraComponent->SetRelativeLocation({0.0f, 0.0f, 0.0f});
	CameraComponent->SetRelativeRotation({0.0f, 0.0f, 0.0f});
	CameraComponent->SetExternalInputManaged(true);
}

void ADefaultPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const FVector Input = ConsumeMovementInputVector();
	if (GetRootComponent() && Input.SizeSquared() > 0.0f)
		GetRootComponent()->SetRelativeLocation(GetActorLocation() + Input * (MoveSpeed * DeltaTime));
}

void ADefaultPawn::AddControllerYawInput(float Value)
{
	if (!GetRootComponent()) return;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += Value;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

void ADefaultPawn::AddControllerPitchInput(float Value)
{
	if (!GetRootComponent()) return;
	FRotator Rotation = GetActorRotation();
	// 화면 Pitch = 폰 Pitch + 카메라 상대 Pitch(45°)다. 폰만 ±89°로 막으면 화면은 134°까지 넘어가 뒤집힌다.
	// 그래서 카메라까지 더한 화면 Pitch가 ±89° 안에 있도록 폰 Pitch 범위를 옮긴다.
	const float CameraPitch = CameraComponent ? CameraComponent->GetRelativeRotation().Pitch : 0.0f;
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f - CameraPitch, 89.0f - CameraPitch);
	GetRootComponent()->SetRelativeRotation(Rotation);
}

FRotator ADefaultPawn::GetMovementInputRotation() const
{
	// 폰 회전을 쓰면 W가 카메라 상대 회전(Pitch 45°)만큼 어긋난 방향으로 가서, 화면 기준 대각선으로 움직인다.
	// 화면에 보이는 방향으로 가도록 카메라의 월드 회전을 쓴다(UE 관전 카메라처럼 보는 쪽으로 날아간다).
	return CameraComponent ? CameraComponent->GetWorldRotation() : GetActorRotation();
}
