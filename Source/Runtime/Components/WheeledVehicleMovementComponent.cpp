#include "EnginePCH.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include "Components/VehicleWheel.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXVehicleManager.h"
#include "Physics/PhysXConversions.h"
#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleSDK.h>
#include <vehicle/PxVehicleWheels.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleDriveNW.h>
#include <vehicle/PxVehicleDriveTank.h>
#include <vehicle/PxVehicleNoDrive.h>
#include <algorithm>
#include <cmath>

using namespace physx;

UWheeledVehicleMovementComponent::UWheeledVehicleMovementComponent()
{
	// 바퀴 상태를 컴포넌트 쪽에서 매 프레임 읽을 수 있게 Tick은 켜 둔다. 실제 계산은 물리 스텝에서 한다.
	PrimaryComponentTick.bCanEverTick = true;
}

void UWheeledVehicleMovementComponent::SetThrottleInput(float Throttle) { RawThrottle = std::clamp(Throttle, -1.0f, 1.0f); }
void UWheeledVehicleMovementComponent::SetBrakeInput(float Brake) { RawBrake = std::clamp(Brake, 0.0f, 1.0f); }
void UWheeledVehicleMovementComponent::SetSteeringInput(float Steering) { RawSteering = std::clamp(Steering, -1.0f, 1.0f); }
void UWheeledVehicleMovementComponent::SetHandbrakeInput(bool bNewHandbrake) { bRawHandbrake = bNewHandbrake; }

float UWheeledVehicleMovementComponent::GetForwardSpeed() const
{
	return PVehicle ? PVehicle->computeForwardSpeed() : 0.0f;
}

void UWheeledVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	// 차체 바디는 루트 컴포넌트가 등록될 때(OnRegister) 이미 만들어져 있다.
	CreateVehicle();
}

void UWheeledVehicleMovementComponent::EndPlay()
{
	DestroyVehicle();
	Super::EndPlay();
}

void UWheeledVehicleMovementComponent::OnUnregister()
{
	// 월드를 내릴 때 EndPlay 없이 등록 해제만 올 수 있으므로 여기서도 정리한다. 두 번 불러도 된다.
	DestroyVehicle();
	Super::OnUnregister();
}

void UWheeledVehicleMovementComponent::ComputeWheelOffsets(TArray<FVector>& OutOffsets) const
{
	// PhysX 4륜 순서: 0 앞왼, 1 앞오, 2 뒤왼, 3 뒤오. 엔진은 X가 앞, Y가 오른쪽, Z가 위다.
	// 위치는 모델에서 잰 값(FrontAxleX 등)을 쓴다. 모델 바퀴 메시와 물리 바퀴가 같은 자리에 있어야 바퀴가 뜨거나 묻히지 않는다.
	const float FrontX = FrontAxleX;
	const float RearX = RearAxleX;
	const float FrontY = WheelHalfTrack;
	const float FrontZ = WheelCenterZ;
	// 앞뒤 바퀴 크기가 다른 모델(큰 뒷바퀴)은 뒷축의 좌우 간격과 높이가 앞축과 다르다.
	const float RearY = bSeparateRearWheels ? RearWheelHalfTrack : WheelHalfTrack;
	const float RearZ = bSeparateRearWheels ? RearWheelCenterZ : WheelCenterZ;

	OutOffsets.SetNum(4);
	OutOffsets[PxVehicleDrive4WWheelOrder::eFRONT_LEFT]  = FVector(FrontX, -FrontY, FrontZ);
	OutOffsets[PxVehicleDrive4WWheelOrder::eFRONT_RIGHT] = FVector(FrontX,  FrontY, FrontZ);
	OutOffsets[PxVehicleDrive4WWheelOrder::eREAR_LEFT]   = FVector(RearX,  -RearY, RearZ);
	OutOffsets[PxVehicleDrive4WWheelOrder::eREAR_RIGHT]  = FVector(RearX,   RearY, RearZ);
}

float UWheeledVehicleMovementComponent::GetWheelRadius(int32 WheelIndex) const
{
	const bool bRear = WheelIndex == PxVehicleDrive4WWheelOrder::eREAR_LEFT || WheelIndex == PxVehicleDrive4WWheelOrder::eREAR_RIGHT;
	return (bRear && bSeparateRearWheels) ? RearWheelRadius : WheelRadius;
}

float UWheeledVehicleMovementComponent::GetWheelWidth(int32 WheelIndex) const
{
	const bool bRear = WheelIndex == PxVehicleDrive4WWheelOrder::eREAR_LEFT || WheelIndex == PxVehicleDrive4WWheelOrder::eREAR_RIGHT;
	return (bRear && bSeparateRearWheels) ? RearWheelWidth : WheelWidth;
}

void UWheeledVehicleMovementComponent::CreateVehicle()
{
	if (PVehicle) return;
	if (!FPhysicsCore::Get().IsVehicleSDKInitialized()) return;

	UWorld* World = GetWorld();
	FPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	FPhysXVehicleManager* Manager = Scene ? Scene->GetVehicleManager() : nullptr;
	if (!Manager) return;   // 에디터 월드에는 물리 Scene이 없다

	UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(UpdatedComponent);
	PxRigidActor* RootActor = Root ? Root->GetBodyInstance().GetActorHandle().Actor : nullptr;
	PxRigidDynamic* Chassis = RootActor ? RootActor->is<PxRigidDynamic>() : nullptr;
	if (!Chassis)
	{
		KRS_LOG(Warning, "[Vehicle] Root component has no dynamic body. Set bSimulatePhysics on the root.");
		return;
	}

	// 바퀴 데이터: 앞바퀴는 조향, 뒷바퀴는 사이드 브레이크를 받는다.
	ComputeWheelOffsets(WheelOffsets);
	const int32 NumWheels = GetNumWheels();
	for (int32 i = 0; i < NumWheels; ++i)
	{
		UVehicleWheel* Wheel = NewObject<UVehicleWheel>(this);
		const bool bFront = (i == PxVehicleDrive4WWheelOrder::eFRONT_LEFT || i == PxVehicleDrive4WWheelOrder::eFRONT_RIGHT);
		Wheel->ShapeRadius = GetWheelRadius(i);
		Wheel->ShapeWidth = GetWheelWidth(i);
		Wheel->Mass = WheelMass;
		Wheel->LongStiffValue = TireLongStiffness;
		Wheel->SteerAngle = bFront ? MaxSteerAngle : 0.0f;
		Wheel->MaxHandBrakeTorque = bFront ? 0.0f : MaxHandBrakeTorque;
		Wheel->SuspensionMaxRaise = SuspensionMaxRaise;
		Wheel->SuspensionMaxDrop = SuspensionMaxDrop;
		Wheel->SuspensionNaturalFrequency = SuspensionNaturalFrequency;
		Wheel->SuspensionDampingRatio = SuspensionDampingRatio;
		Wheel->LocalLocation = WheelOffsets[i];
		Wheels.Add(Wheel);
	}

	SetupVehicleShapes(*Chassis);
	SetupVehicleMass(*Chassis);
	PVehicle = SetupVehicle(*Chassis);
	if (!PVehicle)
	{
		KRS_LOG(Error, "[Vehicle] SetupVehicle failed");
		DestroyVehicle();
		return;
	}

	VehicleManager = Manager;
	VehicleManager->AddVehicle(this);
	KRS_LOG(Info, "[Vehicle] Created vehicle with {} wheels", NumWheels);
}

void UWheeledVehicleMovementComponent::DestroyVehicle()
{
	// 관리자에서 먼저 빼야 다음 스텝이 해제된 차량을 갱신하지 않는다.
	if (VehicleManager)
	{
		VehicleManager->RemoveVehicle(this);
		VehicleManager = nullptr;
	}
	if (PVehicle)
	{
		// free는 차량 메모리만 해제한다. 차체 강체는 루트의 FBodyInstance가 release한다.
		// 기반 타입의 free는 protected라서 구체 타입으로 바꿔 부른다.
		switch (PVehicle->getVehicleType())
		{
		case PxVehicleTypes::eDRIVE4W:   static_cast<PxVehicleDrive4W*>(PVehicle)->free(); break;
		case PxVehicleTypes::eDRIVENW:   static_cast<PxVehicleDriveNW*>(PVehicle)->free(); break;
		case PxVehicleTypes::eDRIVETANK: static_cast<PxVehicleDriveTank*>(PVehicle)->free(); break;
		case PxVehicleTypes::eNODRIVE:   static_cast<PxVehicleNoDrive*>(PVehicle)->free(); break;
		default: KRS_LOG(Error, "[Vehicle] Unknown vehicle type. Leaking vehicle memory."); break;
		}
		PVehicle = nullptr;
	}
	for (UVehicleWheel* Wheel : Wheels)
	{
		delete Wheel;
	}
	Wheels.Empty();
	WheelOffsets.Empty();
	bIsInAir = true;
}

void UWheeledVehicleMovementComponent::SetupVehicleShapes(PxRigidDynamic& Chassis)
{
	// 바퀴는 Shape 없이 레이로만 계산하므로 차체 Shape에만 "레이 무시" 표시를 남긴다.
	FPhysXVehicleManager::MarkChassisShapes(Chassis);
}

void UWheeledVehicleMovementComponent::SetupVehicleMass(PxRigidDynamic& Chassis)
{
	// 상자 근사 관성: 축마다 나머지 두 변의 제곱 합 × 질량 / 12. X 앞, Y 옆, Z 위.
	const float L = ChassisLength, W = ChassisWidth, H = ChassisHeight;
	const PxVec3 Inertia(
		(W * W + H * H) * Mass / 12.0f * InertiaTensorScale.X,
		(L * L + H * H) * Mass / 12.0f * InertiaTensorScale.Y,
		(L * L + W * W) * Mass / 12.0f * InertiaTensorScale.Z);

	Chassis.setMass(Mass);
	Chassis.setMassSpaceInertiaTensor(Inertia);
	Chassis.setCMassLocalPose(PxTransform(U2PVector(COMOffset)));
}

PxVehicleWheels* UWheeledVehicleMovementComponent::SetupVehicle(PxRigidDynamic& /*Chassis*/)
{
	// 기반 클래스는 구동 방식을 모른다. 4W 같은 자식이 재정의한다.
	KRS_LOG(Warning, "[Vehicle] UWheeledVehicleMovementComponent has no drive model. Use UWheeledVehicleMovementComponent4W.");
	return nullptr;
}

void UWheeledVehicleMovementComponent::PreSimulate(float DeltaTime)
{
	if (!PVehicle) return;

	// 잠든 차체는 PxVehicleUpdates가 건너뛴다. 입력이 들어오면 먼저 깨워야 가속·조향이 먹는다.
	PxRigidDynamic* Chassis = PVehicle->getRigidDynamicActor();
	if (Chassis->isSleeping() && (RawThrottle != 0.0f || RawSteering != 0.0f))
	{
		Chassis->wakeUp();
	}
	UpdateSimulation(DeltaTime);
}

void UWheeledVehicleMovementComponent::PostSimulate(const PxVehicleWheelQueryResult& Result)
{
	if (!PVehicle) return;

	// 차가 멈춰 차체가 잠들면 PxVehicleUpdates가 이 차를 건너뛰고, 결과는 기본값(localPose = 단위 변환)으로 남는다.
	// 그대로 옮겨 적으면 바퀴가 차체 원점(몸체 안쪽)으로 순간 이동해 사라져 보인다. 잠든 동안은 마지막 상태를 유지한다.
	if (PVehicle->getRigidDynamicActor()->isSleeping()) return;

	bIsInAir = PxVehicleIsInAir(Result);

	const int32 Count = std::min<int32>(static_cast<int32>(Result.nbWheelQueryResults), Wheels.Num());
	constexpr float RadToDeg = 57.2957795f;
	for (int32 i = 0; i < Count; ++i)
	{
		const PxWheelQueryResult& R = Result.wheelQueryResults[i];
		UVehicleWheel* Wheel = Wheels[i];
		Wheel->CurrentSteerDegrees = R.steerAngle * RadToDeg;
		Wheel->CurrentRotationDegrees = PVehicle->mWheelsDynData.getWheelRotationAngle(i) * RadToDeg;
		Wheel->CurrentSuspensionOffset = R.suspJounce;
		Wheel->bCurrentInAir = R.isInAir;
		Wheel->CurrentLongSlip = R.longitudinalSlip;
		Wheel->CurrentLatSlip = R.lateralSlip;
		Wheel->CurrentContactPoint = P2UVector(R.tireContactPoint);
		Wheel->CurrentContactNormal = P2UVector(R.tireContactNormal);
		// localPose는 차체 액터 기준 바퀴 포즈다. 조향과 굴림 회전이 이미 들어 있다.
		Wheel->LocalLocation = P2UVector(R.localPose.p);
		Wheel->LocalRotation = P2UQuat(R.localPose.q);
	}
}
