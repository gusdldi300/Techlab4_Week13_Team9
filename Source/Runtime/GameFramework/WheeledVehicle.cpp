#include "EnginePCH.h"
#include "GameFramework/WheeledVehicle.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VehicleWheel.h"
#include "Components/WheeledVehicleMovementComponent4W.h"
#include "Asset/AssetManager.h"
#include "Input/KeyCode.h"
#include <string>

namespace
{
	// 원통 메시는 Z축이 높이 방향이다. X축으로 90도 눕혀 바퀴 축(Y)과 맞춘다.
	FQuat WheelMeshBaseRotation()
	{
		return FQuat::MakeFromAxisAngle(FVector(1.0f, 0.0f, 0.0f), 1.57079633f);
	}
}

AWheeledVehicle::AWheeledVehicle()
{
	CollisionBox = CreateDefaultSubobject<UBoxComponent>("CollisionBox");
	SetRootComponent(CollisionBox);
	// 루트에 Dynamic 바디가 있어야 차량 컴포넌트가 그 강체를 차체로 쓴다.
	CollisionBox->bSimulatePhysics = true;

	ChassisMesh = CreateDefaultSubobject<UStaticMeshComponent>("ChassisMesh");
	ChassisMesh->SetupAttachment(CollisionBox);
	ChassisMesh->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Cube"));

	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Wheel = CreateDefaultSubobject<UStaticMeshComponent>(FName(("WheelMesh" + std::to_string(i)).c_str()));
		Wheel->SetupAttachment(CollisionBox);
		Wheel->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Cylinder"));
		WheelMeshes[i] = Wheel;
	}

	VehicleMovement = CreateDefaultSubobject<UWheeledVehicleMovementComponent4W>("VehicleMovement");

	// 차 뒤 위에서 따라가는 카메라. 차가 기울어도 화면이 같이 구르지 않게 Yaw만 따른다.
	SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
	SpringArm->SetupAttachment(CollisionBox);
	SpringArm->TargetArmLength = 9.0f;
	SpringArm->TargetOffset = FVector(0.0f, 0.0f, 2.5f);
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bInheritYaw = true;
	// 팔 충돌 검사는 끈다. 켜 두면 자기 차체에 막혀 카메라가 차 안으로 들어온다.
	SpringArm->bDoCollisionTest = false;

	Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
	Camera->SetupAttachment(SpringArm);
	Camera->SetExternalInputManaged(true);
	Camera->SetRelativeRotation(FRotator(-10.0f, 0.0f, 0.0f));

	ApplyDimensions();
}

void AWheeledVehicle::ApplyDimensions()
{
	if (!VehicleMovement) return;

	const float L = VehicleMovement->ChassisLength;
	const float W = VehicleMovement->ChassisWidth;
	const float H = VehicleMovement->ChassisHeight;
	CollisionBox->BoxExtent = FVector(L * 0.5f, W * 0.5f, H * 0.5f);
	ChassisMesh->SetRelativeScale3D(FVector(L, W, H));   // 큐브는 한 변 1m

	// 차가 만들어지기 전(에디터)에도 바퀴가 제자리에 보이도록 정지 위치에 둔다.
	const float R = VehicleMovement->WheelRadius;
	const float WW = VehicleMovement->WheelWidth;
	const float FrontX = L * 0.3f, RearX = -L * 0.3f;
	const float SideY = (W - WW) * 0.5f;
	const float Z = -(H * 0.5f + R);
	const FVector Rest[4] = { FVector(FrontX, -SideY, Z), FVector(FrontX, SideY, Z), FVector(RearX, -SideY, Z), FVector(RearX, SideY, Z) };
	const FRotator BaseRot = WheelMeshBaseRotation().ToFRotator();
	for (int32 i = 0; i < 4; ++i)
	{
		WheelMeshes[i]->SetRelativeScale3D(FVector(R, R, WW));   // 원통은 반지름 1, 높이 1
		WheelMeshes[i]->SetRelativeLocationAndRotation(Rest[i], BaseRot);
	}
}

void AWheeledVehicle::BeginPlay()
{
	// 에디터에서 바꾼 차체·바퀴 크기를 바디가 만들어지기 전에 반영할 수는 없지만(바디는 등록 때 만들어진다),
	// 표시용 메시는 여기서 다시 맞춘다.
	ApplyDimensions();
	Super::BeginPlay();
}

void AWheeledVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateInput();
	UpdateWheelMeshes();
}

void AWheeledVehicle::UpdateInput()
{
	// PlayerController는 WASD를 AddMovementInput으로도 보낸다. 차는 그 값을 쓰지 않으므로 쌓이지 않게 비운다.
	ConsumeMovementInputVector();

	APlayerController* PC = GetController();
	if (!PC || !VehicleMovement) return;

	auto Down = [PC](EKeyCode Key) { return PC->IsKeyDown(static_cast<int32>(Key)) ? 1.0f : 0.0f; };
	VehicleMovement->SetThrottleInput(Down(EKeyCode::W) - Down(EKeyCode::S));
	VehicleMovement->SetSteeringInput(Down(EKeyCode::D) - Down(EKeyCode::A));
	VehicleMovement->SetHandbrakeInput(Down(EKeyCode::Space) > 0.0f);
}

void AWheeledVehicle::UpdateWheelMeshes()
{
	if (!VehicleMovement) return;
	const TArray<UVehicleWheel*>& Wheels = VehicleMovement->GetWheels();
	if (Wheels.Num() != 4) return;

	// 바퀴 로컬 포즈는 차체 강체(루트) 기준이고 조향과 굴림 회전이 들어 있다. 원통을 눕히는 회전을 먼저 적용한다.
	const FQuat Base = WheelMeshBaseRotation();
	for (int32 i = 0; i < 4; ++i)
	{
		const UVehicleWheel* Wheel = Wheels[i];
		const FQuat Rotation = Wheel->GetLocalRotation() * Base;
		WheelMeshes[i]->SetRelativeLocationAndRotation(Wheel->GetLocalLocation(), Rotation.ToFRotator());
	}
}
