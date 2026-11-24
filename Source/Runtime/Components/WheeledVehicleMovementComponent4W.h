#pragma once

#include "WheeledVehicleMovementComponent.h"
#include "WheeledVehicleMovementComponent4W.generated.h"

// 엔진, 클러치, 기어박스, 차동 장치를 갖춘 4륜 차. PhysX의 PxVehicleDrive4W를 감싼다.
// 언리얼 PhysXVehicles의 UWheeledVehicleMovementComponent4W에 대응한다.
// 엔진 회전수, 자동 변속, 타이어 슬립 같은 실제 계산은 PxVehicleUpdates 안에서 PhysX가 한다.
// 여기서는 속성을 PhysX 구조체로 옮겨 담고, 입력을 PhysX 입력으로 바꿔 넘긴다.
UCLASS()
class UWheeledVehicleMovementComponent4W : public UWheeledVehicleMovementComponent
{
	GENERATED_BODY()

public:
	// ---- 엔진 (FVehicleEngineData) ----
	UPROPERTY(EditAnywhere)
	float PeakTorque = 500.0f;           // N·m. 토크 곡선 최대치

	UPROPERTY(EditAnywhere)
	float MaxRPM = 6000.0f;

	UPROPERTY(EditAnywhere)
	float EngineMOI = 1.0f;              // 엔진 관성. 클수록 회전수가 천천히 오른다

	// ---- 변속기 (FVehicleTransmissionData) ----
	UPROPERTY(EditAnywhere)
	bool bUseAutomaticGears = true;

	UPROPERTY(EditAnywhere)
	float GearSwitchTime = 0.5f;         // 초

	UPROPERTY(EditAnywhere)
	float FinalRatio = 4.0f;             // 최종 감속비. 기어비에 곱한다

	UPROPERTY(EditAnywhere)
	float ClutchStrength = 10.0f;

	// ---- 차동 장치 (FVehicleDifferential4WData) ----
	// 0 LSD 4륜, 1 LSD 앞바퀴굴림, 2 LSD 뒷바퀴굴림, 3 오픈 4륜, 4 오픈 앞바퀴굴림, 5 오픈 뒷바퀴굴림
	UPROPERTY(EditAnywhere)
	int32 DifferentialType = 0;

	UPROPERTY(EditAnywhere)
	float FrontRearSplit = 0.45f;        // 4륜일 때 앞바퀴로 가는 토크 비율

	// ---- 조향 ----
	UPROPERTY(EditAnywhere)
	float AckermannAccuracy = 1.0f;      // 0 끄기, 1 안쪽 바퀴를 정확히 더 꺾는다

	float GetEngineRotationSpeed() const override;
	int32 GetCurrentGear() const override;

protected:
	physx::PxVehicleWheels* SetupVehicle(physx::PxRigidDynamic& Chassis) override;
	void UpdateSimulation(float DeltaTime) override;
};
