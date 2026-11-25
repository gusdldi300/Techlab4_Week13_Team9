#include "EnginePCH.h"
#include "SkidMarkComponent.h"

#include "Asset/AssetManager.h"
#include "Components/DecalComponent.h"
#include "Components/VehicleWheel.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"

#include <cfloat>

#include <cmath>

USkidMarkComponent::USkidMarkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USkidMarkComponent::BeginPlay()
{
	Super::BeginPlay();
	Texture = UAssetManager::GetAssetByPath<UTexture2D>(TexturePath);
	if (!Texture)
	{
		KRS_LOG(Warning, "[SkidMark] texture '{}' not found. Skid marks are disabled", TexturePath);
	}
	Pool.Empty();
	NextIndex = 0;
	Trails.Empty();
}

void USkidMarkComponent::EndPlay()
{
	// 풀 데칼은 월드에 놓인 액터다. 차가 사라지면 같이 치운다.
	for (ADecalActor* Decal : Pool)
	{
		if (Decal) Decal->Destroy();
	}
	Pool.Empty();
	Super::EndPlay();
}

void USkidMarkComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!bEnableSkidMarks || !Movement || !Texture || !GetWorld()) return;

	const TArray<UVehicleWheel*>& Wheels = Movement->GetWheels();
	if (Trails.Num() != Wheels.Num()) Trails.SetNum(Wheels.Num());

	// 차체 앞 방향 속도의 크기. 후진 중에 미끄러져도 자국이 남는다.
	const float Speed = std::abs(Movement->GetForwardSpeed());

	for (int32 i = 0; i < Wheels.Num(); ++i)
	{
		const UVehicleWheel* Wheel = Wheels[i];
		FWheelTrail& Trail = Trails[i];
		const bool bSkidding = Wheel && !Wheel->IsInAir() && Speed >= MinSpeed &&
			(std::abs(Wheel->GetLateralSlip()) > LateralSlipThreshold ||
			 std::abs(Wheel->GetLongitudinalSlip()) > LongitudinalSlipThreshold);
		if (!bSkidding)
		{
			Trail.bMarking = false;   // 자국이 끊긴다. 다음에 미끄러지면 새 선으로 시작한다
			continue;
		}

		const FVector Point = Wheel->GetContactPoint();
		if (!Trail.bMarking)
		{
			Trail.bMarking = true;
			Trail.LastPoint = Point;
			continue;
		}

		const float Distance = (Point - Trail.LastPoint).Size();
		if (Distance < SegmentLength) continue;
		// 한 Tick에 너무 멀리 갔으면(순간이동, 큰 프레임 지연) 선을 잇지 않고 새로 시작한다
		if (Distance > SegmentLength * 8.0f)
		{
			Trail.LastPoint = Point;
			continue;
		}

		const float Width = Movement->GetWheelWidth(i) * WidthScale;
		PlaceSegment(Trail.LastPoint, Point, Wheel->GetContactNormal(), Width);
		Trail.LastPoint = Point;
	}
}

ADecalActor* USkidMarkComponent::AcquireDecal()
{
	const int32 Capacity = std::max(MaxMarks, 1);
	if (Pool.Num() < Capacity)
	{
		ADecalActor* Decal = GetWorld()->SpawnActor<ADecalActor>();
		if (!Decal) return nullptr;
		Pool.Add(Decal);
		return Decal;
	}
	// 풀이 찼으면 가장 오래된 것을 다시 쓴다
	ADecalActor* Oldest = Pool[NextIndex];
	NextIndex = (NextIndex + 1) % Capacity;
	return Oldest;
}

void USkidMarkComponent::PlaceSegment(const FVector& A, const FVector& B, const FVector& InNormal, float Width)
{
	ADecalActor* Decal = AcquireDecal();
	UDecalComponent* Component = Decal ? Decal->GetDecalComponent() : nullptr;
	if (!Component) return;

	// 데칼은 로컬 X 방향으로 투사하고, 텍스처 U는 로컬 Y(폭), V는 로컬 Z(길이)를 따른다.
	// 그래서 X = 아래(지면 쪽), Z = 진행 방향인 회전, 즉 "진행 방향으로 Yaw → 앞이 땅을 향하게 Pitch 90도"가 필요하다.
	// 행렬로 넣으면 MatrixToRotator가 X가 정확히 아래일 때(짐벌 락) Yaw·Roll을 모두 0으로 만들어 방향이 사라지므로
	// FRotator를 직접 만든다. Pitch·Yaw 부호 규칙은 쿼터니언 축으로 확인해 맞는 조합을 고른다.
	// 맵 바닥은 평평하다고 보고 법선은 쓰지 않는다(경사에서는 0.3m 투사 깊이 안에서 그대로 찍힌다).
	(void)InNormal;
	FVector Along = B - A;
	const float Length = Along.Size();
	Along.Z = 0.0f;
	if (Along.Size() < 1e-4f) return;
	Along = Along.Normalized();

	const float Heading = std::atan2(Along.Y, Along.X) * 57.2957795f;
	FRotator Rotation(-90.0f, Heading, 0.0f);
	float BestScore = -FLT_MAX;
	for (float PitchSign : { -1.0f, 1.0f })
	{
		for (float YawSign : { 1.0f, -1.0f })
		{
			const FRotator Candidate(90.0f * PitchSign, Heading * YawSign, 0.0f);
			const FQuat Q(Candidate);
			// 앞(X)이 아래를, 위(Z)가 진행 방향을 향할수록 점수가 높다
			const float Score = -Q.GetForwardVector().Z + Q.GetUpVector().Dot(Along);
			if (Score > BestScore) { BestScore = Score; Rotation = Candidate; }
		}
	}
	Component->SetWorldLocationAndRotation((A + B) * 0.5f, Rotation, false);
	if (Pool.Num() == 1 && NextIndex == 0)
	{
		KRS_LOG(Info, "[SkidMark] first skid segment at ({:.1f}, {:.1f}, {:.2f}), heading {:.0f} deg", A.X, A.Y, A.Z, Heading);
	}

	// X: 지면 위아래로 0.3m 안의 면에만 찍힌다(벽·차체에는 안 묻게). Z: 이웃 띠와 조금 겹쳐 틈이 안 보이게 한다.
	Component->SetExtent(FVector(0.3f, Width * 0.5f, Length * 0.5f + 0.04f));
	Component->SetDecalTexture(Texture);
	Component->SetFadeTimes(0.0f, 0.05f, VisibleTime, FadeOutTime);
	Component->RestartFade();
}
