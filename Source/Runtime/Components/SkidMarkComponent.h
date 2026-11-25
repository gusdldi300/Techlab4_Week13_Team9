#pragma once

#include "ActorComponent.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "SkidMarkComponent.generated.h"

class ADecalActor;
class UTexture2D;
class UWheeledVehicleMovementComponent;

// 타이어가 미끄러질 때 지면에 남는 자국(스키드 마크)을 데칼로 찍는다.
//
// 매 Tick 바퀴마다 미끄러짐(횡·종)을 보고, 문턱값을 넘은 채 접지해 있으면 직전 점에서 지금 접지점까지
// 짧은 띠(SegmentLength) 하나를 데칼로 깐다. 이어서 찍힌 띠들이 하나의 자국 선이 된다.
// 데칼은 MaxMarks개를 미리 정한 고리(ring) 풀로 돌려 쓴다. 가장 오래된 자국부터 새 자리로 옮겨 간다.
//
// 소유: 차량 액터(AWheeledVehicle)가 기본 서브오브젝트로 가진다(합성).
// 참조: 같은 액터의 이동 컴포넌트, 그리고 풀의 데칼 액터들은 빌려 쓰기만 한다. 데칼 액터는 월드가 소유한다.
UCLASS()
class USkidMarkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USkidMarkComponent();

	void BeginPlay() override;
	void EndPlay() override;
	void TickComponent(float DeltaTime) override;

	// 이 컴포넌트가 읽을 바퀴. 차량 액터가 생성자에서 넣어 준다.
	void SetVehicleMovement(UWheeledVehicleMovementComponent* InMovement) { Movement = InMovement; }

	UPROPERTY(EditAnywhere)
	bool bEnableSkidMarks = true;

	UPROPERTY(EditAnywhere)
	float LateralSlipThreshold = 0.2f;        // rad(약 11도). 넘으면 옆으로 밀리는 자국

	UPROPERTY(EditAnywhere)
	float LongitudinalSlipThreshold = 0.35f;  // 넘으면 헛돌거나 잠긴 자국(급출발·급제동)

	UPROPERTY(EditAnywhere)
	float MinSpeed = 1.5f;                    // m/s. 이보다 느리면 찍지 않는다(제자리에서 미끄러짐 값이 튀므로)

	UPROPERTY(EditAnywhere)
	float SegmentLength = 0.5f;               // m. 띠 하나의 길이. 짧을수록 곡선이 매끄럽고 데칼 수가 는다

	UPROPERTY(EditAnywhere)
	float WidthScale = 0.9f;                  // 자국 폭 = 바퀴 폭 × 이 값

	UPROPERTY(EditAnywhere)
	int32 MaxMarks = 240;                     // 풀 크기(동시에 남는 띠 수)

	UPROPERTY(EditAnywhere)
	float VisibleTime = 10.0f;                // 초. 이만큼 진하게 남았다가

	UPROPERTY(EditAnywhere)
	float FadeOutTime = 3.0f;                 // 초. 이 동안 옅어진다

	UPROPERTY(EditAnywhere)
	FString TexturePath = "Textures/SkidMark.png";

private:
	struct FWheelTrail
	{
		bool bMarking = false;   // 직전 Tick에도 미끄러지고 있었다
		FVector LastPoint = FVector::ZeroVector;
	};

	// A에서 B까지 띠 하나를 풀의 다음 데칼로 깐다.
	void PlaceSegment(const FVector& A, const FVector& B, const FVector& Normal, float Width);
	ADecalActor* AcquireDecal();

	UWheeledVehicleMovementComponent* Movement = nullptr;
	UTexture2D* Texture = nullptr;
	TArray<FWheelTrail> Trails;
	TArray<ADecalActor*> Pool;
	int32 NextIndex = 0;
};
