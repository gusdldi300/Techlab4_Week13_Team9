#pragma once

#include "PawnMovementComponent.h"
#include "Containers/Array.h"
#include "WheeledVehicleMovementComponent.generated.h"

namespace physx
{
	class PxVehicleWheels;
	class PxRigidDynamic;
	struct PxVehicleWheelQueryResult;
}

class UVehicleWheel;
class FPhysXVehicleManager;

// 바퀴 달린 차량의 공통 기반. 언리얼 PhysXVehicles의 UWheeledVehicleMovementComponent에 대응한다.
// 입력, 차체 질량, 바퀴 데이터, 관리자 등록처럼 구동 방식과 상관없는 부분을 맡는다.
// 엔진·기어가 있는 4륜(PxVehicleDrive4W), 바퀴를 직접 돌리는 NoDrive 같은 구동 방식은
// 자식이 SetupVehicle과 UpdateSimulation을 재정의해서 정한다.
//
// 관계
// - 차체 강체: 루트 프리미티브의 FBodyInstance가 소유한다. 여기서는 가리키기만 한다(집약).
// - PVehicle(PhysX 차량 객체), Wheels: 이 컴포넌트가 만들고 지운다(합성). 차 한 대에만 쓰인다.
// - FPhysXVehicleManager: Scene이 소유한다. 여기서는 등록만 한다(집약).
UCLASS()
class UWheeledVehicleMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UWheeledVehicleMovementComponent();

	// ---- 차체 ----
	UPROPERTY(EditAnywhere)
	float Mass = 1500.0f;                    // kg

	UPROPERTY(EditAnywhere)
	float ChassisLength = 4.0f;              // 앞뒤 길이(m). 관성과 바퀴 위치 계산에 쓴다

	UPROPERTY(EditAnywhere)
	float ChassisWidth = 2.0f;               // 좌우 폭(m)

	UPROPERTY(EditAnywhere)
	float ChassisHeight = 1.0f;              // 높이(m)

	UPROPERTY(EditAnywhere)
	FVector COMOffset = FVector(0.25f, 0.0f, -0.3f);   // 무게 중심. 낮을수록 잘 안 뒤집힌다

	UPROPERTY(EditAnywhere)
	FVector InertiaTensorScale = FVector(1.0f, 1.0f, 0.8f);   // 상자 관성에 곱한다. Z(위)를 줄이면 잘 돈다

	// ---- 바퀴 (차를 만들 때 바퀴마다 UVehicleWheel로 복사한다) ----
	UPROPERTY(EditAnywhere)
	float WheelRadius = 0.4f;

	UPROPERTY(EditAnywhere)
	float WheelWidth = 0.3f;

	UPROPERTY(EditAnywhere)
	float WheelMass = 20.0f;

	UPROPERTY(EditAnywhere)
	float MaxSteerAngle = 45.0f;             // 앞바퀴 최대 조향각(도)

	UPROPERTY(EditAnywhere)
	float MaxHandBrakeTorque = 4000.0f;      // 뒷바퀴 사이드 브레이크

	UPROPERTY(EditAnywhere)
	float SuspensionMaxRaise = 0.3f;

	UPROPERTY(EditAnywhere)
	float SuspensionMaxDrop = 0.1f;

	UPROPERTY(EditAnywhere)
	float SuspensionNaturalFrequency = 7.0f;

	UPROPERTY(EditAnywhere)
	float SuspensionDampingRatio = 1.0f;

	// ---- 입력 (게임 Tick에서 저장하고, 다음 물리 스텝 직전에 PhysX로 넘긴다) ----
	void SetThrottleInput(float Throttle);   // -1 ~ 1. 음수는 감속 후 후진
	void SetBrakeInput(float Brake);         // 0 ~ 1
	void SetSteeringInput(float Steering);   // -1(왼쪽) ~ 1(오른쪽)
	void SetHandbrakeInput(bool bNewHandbrake);

	// ---- 상태 읽기 ----
	float GetForwardSpeed() const;           // m/s. 뒤로 가면 음수
	virtual float GetEngineRotationSpeed() const { return 0.0f; }   // RPM
	virtual int32 GetCurrentGear() const { return 0; }              // -1 후진, 0 중립, 1~ 전진
	bool IsInAir() const { return bIsInAir; }
	const TArray<UVehicleWheel*>& GetWheels() const { return Wheels; }

	// ---- FPhysXVehicleManager가 부른다 ----
	physx::PxVehicleWheels* GetPhysXVehicle() const { return PVehicle; }
	// simulate 직전. 저장해 둔 입력을 PhysX 입력으로 바꿔 넣는다.
	void PreSimulate(float DeltaTime);
	// PxVehicleUpdates 직후. 바퀴 상태를 엔진 타입으로 옮겨 둔다.
	void PostSimulate(const physx::PxVehicleWheelQueryResult& Result);
	// 관리자가 먼저 사라질 때 가리키던 포인터를 끊는다. 여기서 관리자 목록을 건드리지 않는다.
	void OnVehicleManagerDestroyed() { VehicleManager = nullptr; }

	void BeginPlay() override;
	void EndPlay() override;

protected:
	void OnUnregister() override;

	// 차를 만든다: 차체 Shape 표시 → 질량 → SetupVehicle(자식) → 관리자 등록
	void CreateVehicle();
	void DestroyVehicle();

	void SetupVehicleShapes(physx::PxRigidDynamic& Chassis);
	void SetupVehicleMass(physx::PxRigidDynamic& Chassis);
	// 구동 방식별 PhysX 차량 객체를 만든다. Wheels와 WheelOffsets는 이미 채워져 있다.
	virtual physx::PxVehicleWheels* SetupVehicle(physx::PxRigidDynamic& Chassis);
	// 구동 방식별로 저장된 입력을 PhysX에 넣는다.
	virtual void UpdateSimulation(float DeltaTime) {}

	// 바퀴 수와 순서는 구동 방식이 정한다. 기본은 4륜 순서(앞왼, 앞오, 뒤왼, 뒤오)다.
	virtual int32 GetNumWheels() const { return 4; }
	// 차체 기준 바퀴 중심(정지 위치). 4륜 순서대로 채운다.
	virtual void ComputeWheelOffsets(TArray<FVector>& OutOffsets) const;

	physx::PxVehicleWheels* PVehicle = nullptr;
	TArray<UVehicleWheel*> Wheels;
	TArray<FVector> WheelOffsets;
	FPhysXVehicleManager* VehicleManager = nullptr;

	float RawThrottle = 0.0f;
	float RawBrake = 0.0f;
	float RawSteering = 0.0f;
	bool  bRawHandbrake = false;
	bool  bIsInAir = true;
};
