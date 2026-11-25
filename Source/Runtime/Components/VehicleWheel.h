#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "VehicleWheel.generated.h"

// 바퀴 하나의 설정과 실행 중 상태. 언리얼 PhysXVehicles의 UVehicleWheel에 대응한다.
// 바퀴는 강체가 아니다. 차체에서 아래로 쏜 레이로 지면까지 거리를 재고, 그 거리로 서스펜션 힘을 낸다.
//
// 관계: UWheeledVehicleMovementComponent가 차를 만들 때 바퀴마다 하나씩 만들고, 차를 지울 때 함께 지운다(합성).
// 차 한 대에만 쓰이고 차보다 오래 살 이유가 없기 때문이다.
UCLASS()
class UVehicleWheel : public UObject
{
	GENERATED_BODY()

public:
	// ---- 모양·회전 ----
	UPROPERTY(EditAnywhere)
	float ShapeRadius = 0.4f;            // m. 렌더 메시와 같아야 바퀴가 뜨거나 묻히지 않는다

	UPROPERTY(EditAnywhere)
	float ShapeWidth = 0.3f;             // m

	UPROPERTY(EditAnywhere)
	float Mass = 20.0f;                  // kg. 관성은 원통 근사 0.5 × m × r²로 구한다

	UPROPERTY(EditAnywhere)
	float DampingRate = 0.25f;           // 바퀴 회전 감쇠. 0이면 불안정하다

	UPROPERTY(EditAnywhere)
	float SteerAngle = 0.0f;             // 최대 조향각(도). 앞바퀴만 0보다 크다

	UPROPERTY(EditAnywhere)
	float MaxBrakeTorque = 1500.0f;      // N·m

	UPROPERTY(EditAnywhere)
	float MaxHandBrakeTorque = 0.0f;     // N·m. 뒷바퀴만 0보다 크다

	// ---- 서스펜션 ----
	UPROPERTY(EditAnywhere)
	float SuspensionMaxRaise = 0.3f;     // 정지 위치에서 위로 압축될 수 있는 거리(m)

	UPROPERTY(EditAnywhere)
	float SuspensionMaxDrop = 0.1f;      // 정지 위치에서 아래로 늘어날 수 있는 거리(m)

	UPROPERTY(EditAnywhere)
	float SuspensionNaturalFrequency = 7.0f;   // 강성 = 고유진동수² × 스프렁 질량

	UPROPERTY(EditAnywhere)
	float SuspensionDampingRatio = 1.0f;       // 1이면 임계 감쇠. 감쇠 = 감쇠비 × 2√(강성 × 스프렁 질량)

	// ---- 타이어 (PhysX 기본값과 같다) ----
	UPROPERTY(EditAnywhere)
	float LatStiffMaxLoad = 2.0f;        // 횡방향 강성이 최대가 되는 정규화 하중

	UPROPERTY(EditAnywhere)
	float LatStiffValue = 17.0f;         // 횡방향 강성 최대치

	UPROPERTY(EditAnywhere)
	float LongStiffValue = 1000.0f;      // 종방향 강성(중력 1당)

	// ---- 실행 중 읽기 (차량 갱신 직후 값) ----
	float GetSteerAngle() const { return CurrentSteerDegrees; }
	float GetRotationAngle() const { return CurrentRotationDegrees; }
	// 정지 위치 기준 서스펜션 압축량(m). 양수면 위로 눌렸다.
	float GetSuspensionOffset() const { return CurrentSuspensionOffset; }
	bool IsInAir() const { return bCurrentInAir; }

	// 차체(루트 컴포넌트) 기준 바퀴 위치와 회전. 바퀴 메시를 옮길 때 쓴다.
	const FVector& GetLocalLocation() const { return LocalLocation; }
	const FQuat& GetLocalRotation() const { return LocalRotation; }

private:
	friend class UWheeledVehicleMovementComponent;

	float CurrentSteerDegrees = 0.0f;
	float CurrentRotationDegrees = 0.0f;
	float CurrentSuspensionOffset = 0.0f;
	bool  bCurrentInAir = true;
	FVector LocalLocation = FVector::ZeroVector;
	FQuat   LocalRotation = FQuat::Identity();
};
