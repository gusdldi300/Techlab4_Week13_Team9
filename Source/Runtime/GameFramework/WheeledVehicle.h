#pragma once

#include "GameFramework/Pawn.h"
#include "WheeledVehicle.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UWheeledVehicleMovementComponent4W;
class USpringArmComponent;
class UCameraComponent;

// 레벨에 놓는 4륜 차. 언리얼 PhysXVehicles의 AWheeledVehicle에 대응한다.
//
// 구성(모두 이 액터가 소유하는 컴포넌트, 합성)
// - CollisionBox: 루트. 차체 강체(PxRigidDynamic)가 여기 붙는다. 물리 결과가 이 컴포넌트를 움직이고 자식이 따라온다.
// - ChassisMesh: 차체 모양만 보여 준다. 충돌 없음.
// - WheelMeshes[4]: 바퀴 모양만 보여 준다. 매 Tick 바퀴 회전·조향·서스펜션 위치를 넣는다. 충돌 없음.
// - VehicleMovement: PhysX 차량 객체를 만들고 입력을 넘긴다.
// - SpringArm, Camera: 차 뒤를 따라가는 카메라.
//
// 언리얼은 차체를 스켈레탈 메시로 두고 애니메이션 노드가 바퀴 본을 돌리지만,
// 여기서는 바퀴 메시 넷을 직접 옮긴다. 루트를 큐브 메시로 두지 않은 이유는 차체 크기를 스케일로 주면
// 그 스케일이 자식 바퀴까지 늘려 버리기 때문이다. 박스는 크기를 BoxExtent로 주므로 자식에 스케일이 번지지 않는다.
UCLASS()
class AWheeledVehicle : public APawn
{
	GENERATED_BODY()

public:
	AWheeledVehicle();

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	UBoxComponent* GetCollisionBox() const { return CollisionBox; }
	UWheeledVehicleMovementComponent4W* GetVehicleMovement() const { return VehicleMovement; }

private:
	// 빙의한 PlayerController의 키 상태를 차량 입력으로 바꾼다. W/S 스로틀, A/D 조향, Space 사이드 브레이크.
	void UpdateInput();
	// 물리 결과(바퀴 로컬 포즈)를 바퀴 메시에 넣는다.
	void UpdateWheelMeshes();
	// 차체·바퀴 메시 크기를 이동 컴포넌트 속성에 맞춘다.
	void ApplyDimensions();

	UBoxComponent* CollisionBox = nullptr;
	UStaticMeshComponent* ChassisMesh = nullptr;
	UStaticMeshComponent* WheelMeshes[4] = {};
	UWheeledVehicleMovementComponent4W* VehicleMovement = nullptr;
	USpringArmComponent* SpringArm = nullptr;
	UCameraComponent* Camera = nullptr;
};
