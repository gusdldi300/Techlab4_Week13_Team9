#include "EnginePCH.h"
#include "Components/WheeledVehicleMovementComponent4W.h"
#include "Components/VehicleWheel.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysXConversions.h"
#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleSDK.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleUtilSetup.h>
#include <vehicle/PxVehicleUtilControl.h>
#include <algorithm>
#include <cmath>

using namespace physx;

namespace
{
	constexpr float DegToRad = 0.0174532925f;

	// 입력이 0→1로 오르는 속도와 1→0으로 내리는 속도(초당). 키보드 입력이 한 번에 튀지 않게 한다. PhysX 샘플 값이다.
	const PxVehiclePadSmoothingData GPadSmoothing =
	{
		{ 6.0f, 6.0f, 6.0f, 2.5f, 2.5f },    // rise: 가속, 브레이크, 사이드, 왼쪽, 오른쪽
		{ 10.0f, 10.0f, 10.0f, 5.0f, 5.0f }  // fall
	};

	// 속도(m/s)가 빠를수록 조향을 덜 꺾는다. 고속에서 핸들을 끝까지 꺾으면 바로 전복되기 때문이다. PhysX 샘플 값이다.
	PxF32 GSteerVsForwardSpeedData[2 * 8] =
	{
		0.0f,   0.75f,
		5.0f,   0.75f,
		30.0f,  0.125f,
		120.0f, 0.1f,
		PX_MAX_F32, PX_MAX_F32,
		PX_MAX_F32, PX_MAX_F32,
		PX_MAX_F32, PX_MAX_F32,
		PX_MAX_F32, PX_MAX_F32
	};
	const PxFixedSizeLookupTable<8> GSteerVsForwardSpeedTable(GSteerVsForwardSpeedData, 4);

	PxVehicleDifferential4WData::Enum ToPhysXDiff(int32 Type)
	{
		switch (Type)
		{
		case 1:  return PxVehicleDifferential4WData::eDIFF_TYPE_LS_FRONTWD;
		case 2:  return PxVehicleDifferential4WData::eDIFF_TYPE_LS_REARWD;
		case 3:  return PxVehicleDifferential4WData::eDIFF_TYPE_OPEN_4WD;
		case 4:  return PxVehicleDifferential4WData::eDIFF_TYPE_OPEN_FRONTWD;
		case 5:  return PxVehicleDifferential4WData::eDIFF_TYPE_OPEN_REARWD;
		default: return PxVehicleDifferential4WData::eDIFF_TYPE_LS_4WD;
		}
	}
}

PxVehicleWheels* UWheeledVehicleMovementComponent4W::SetupVehicle(PxRigidDynamic& Chassis)
{
	PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	const PxU32 NumWheels = static_cast<PxU32>(Wheels.Num());
	if (!Physics || NumWheels != 4) return nullptr;

	const PxVec3 COM = U2PVector(COMOffset);

	// 1. 바퀴마다 받치는 차체 질량(스프렁 질량). 합이 차체 질량, 무게 중심이 COM이 되도록 나눈다. 2 = Z가 중력 축.
	PxVec3 Offsets[4];
	for (PxU32 i = 0; i < 4; ++i) Offsets[i] = U2PVector(WheelOffsets[i]);
	PxF32 SprungMasses[4];
	PxVehicleComputeSprungMasses(NumWheels, Offsets, COM, Mass, 2, SprungMasses);

	// 2. 바퀴·타이어·서스펜션 데이터
	PxVehicleWheelsSimData* WheelsSim = PxVehicleWheelsSimData::allocate(NumWheels);
	for (PxU32 i = 0; i < NumWheels; ++i)
	{
		const UVehicleWheel* W = Wheels[i];

		PxVehicleWheelData WheelData;
		WheelData.mRadius = W->ShapeRadius;
		WheelData.mWidth = W->ShapeWidth;
		WheelData.mMass = W->Mass;
		WheelData.mMOI = 0.5f * W->Mass * W->ShapeRadius * W->ShapeRadius;   // 원통 관성
		WheelData.mDampingRate = W->DampingRate;
		WheelData.mMaxBrakeTorque = W->MaxBrakeTorque;
		WheelData.mMaxHandBrakeTorque = W->MaxHandBrakeTorque;
		WheelData.mMaxSteer = W->SteerAngle * DegToRad;

		PxVehicleTireData TireData;
		TireData.mType = 0;
		TireData.mLatStiffX = W->LatStiffMaxLoad;
		TireData.mLatStiffY = W->LatStiffValue;
		TireData.mLongitudinalStiffnessPerUnitGravity = W->LongStiffValue;

		// 강성 = ω² × m, 감쇠 = ζ × 2√(k × m). 고유진동수와 감쇠비로 정하면 차 무게가 바뀌어도 느낌이 유지된다.
		const float Sprung = SprungMasses[i];
		const float Omega = W->SuspensionNaturalFrequency;
		PxVehicleSuspensionData Susp;
		Susp.mMaxCompression = W->SuspensionMaxRaise;
		Susp.mMaxDroop = W->SuspensionMaxDrop;
		Susp.mSprungMass = Sprung;
		Susp.mSpringStrength = Omega * Omega * Sprung;
		Susp.mSpringDamperRate = W->SuspensionDampingRatio * 2.0f * std::sqrt(Susp.mSpringStrength * Sprung);

		// 위치는 무게 중심 기준이다. 서스펜션·타이어 힘은 COM보다 0.3m 아래에 준다(너무 낮으면 커브에서 안쪽으로 기운다).
		const PxVec3 CentreFromCOM = Offsets[i] - COM;
		const PxVec3 ForcePoint(CentreFromCOM.x, CentreFromCOM.y, -0.3f);

		WheelsSim->setWheelData(i, WheelData);
		WheelsSim->setTireData(i, TireData);
		WheelsSim->setSuspensionData(i, Susp);
		WheelsSim->setSuspTravelDirection(i, PxVec3(0.0f, 0.0f, -1.0f));
		WheelsSim->setWheelCentreOffset(i, CentreFromCOM);
		WheelsSim->setSuspForceAppPointOffset(i, ForcePoint);
		WheelsSim->setTireForceAppPointOffset(i, ForcePoint);
		WheelsSim->setSceneQueryFilterData(i, PxFilterData());
		// 바퀴 충돌 Shape는 만들지 않는다. 바퀴는 레이로만 지면을 찾는다.
		WheelsSim->setWheelShapeMapping(i, -1);
	}

	// 앞·뒤 안티롤 바: 좌우 서스펜션 차이만큼 버텨서 커브에서 차체가 덜 기운다.
	PxVehicleAntiRollBarData FrontBar;
	FrontBar.mWheel0 = PxVehicleDrive4WWheelOrder::eFRONT_LEFT;
	FrontBar.mWheel1 = PxVehicleDrive4WWheelOrder::eFRONT_RIGHT;
	FrontBar.mStiffness = 10000.0f;
	WheelsSim->addAntiRollBarData(FrontBar);
	PxVehicleAntiRollBarData RearBar;
	RearBar.mWheel0 = PxVehicleDrive4WWheelOrder::eREAR_LEFT;
	RearBar.mWheel1 = PxVehicleDrive4WWheelOrder::eREAR_RIGHT;
	RearBar.mStiffness = 10000.0f;
	WheelsSim->addAntiRollBarData(RearBar);

	// 스텝(1/60초)을 속도에 따라 잘게 나눈다: 5 m/s 미만은 3번, 이상은 1번. 저속 떨림을 줄인다.
	WheelsSim->setSubStepCount(5.0f, 3, 1);

	// 3. 엔진·기어·클러치·차동 장치·애커먼
	PxVehicleDriveSimData4W DriveSim;

	PxVehicleDifferential4WData Diff;
	Diff.mType = ToPhysXDiff(DifferentialType);
	Diff.mFrontRearSplit = std::clamp(FrontRearSplit, 0.0f, 1.0f);
	DriveSim.setDiffData(Diff);

	PxVehicleEngineData Engine;
	Engine.mPeakTorque = PeakTorque;
	Engine.mMaxOmega = MaxRPM * 2.0f * PxPi / 60.0f;   // RPM → rad/s
	Engine.mMOI = EngineMOI;
	DriveSim.setEngineData(Engine);

	PxVehicleGearsData Gears;
	Gears.mSwitchTime = GearSwitchTime;
	Gears.mFinalRatio = FinalRatio;
	DriveSim.setGearsData(Gears);

	PxVehicleClutchData Clutch;
	Clutch.mStrength = ClutchStrength;
	DriveSim.setClutchData(Clutch);

	const PxVec3 FL = WheelsSim->getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eFRONT_LEFT);
	const PxVec3 FR = WheelsSim->getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eFRONT_RIGHT);
	const PxVec3 RL = WheelsSim->getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eREAR_LEFT);
	const PxVec3 RR = WheelsSim->getWheelCentreOffset(PxVehicleDrive4WWheelOrder::eREAR_RIGHT);
	PxVehicleAckermannGeometryData Ackermann;
	Ackermann.mAccuracy = std::clamp(AckermannAccuracy, 0.0f, 1.0f);
	Ackermann.mAxleSeparation = FL.x - RL.x;   // 앞뒤 축 거리(X가 앞)
	Ackermann.mFrontWidth = FR.y - FL.y;       // 좌우 폭(Y가 오른쪽)
	Ackermann.mRearWidth = RR.y - RL.y;
	DriveSim.setAckermannGeometryData(Ackermann);

	// 4. 차량 객체 생성. 차체 강체는 이미 Scene에 들어 있는 루트 바디를 그대로 쓴다.
	PxVehicleDrive4W* Vehicle = PxVehicleDrive4W::allocate(NumWheels);
	Vehicle->setup(Physics, &Chassis, *WheelsSim, DriveSim, NumWheels - 4);
	WheelsSim->free();

	Vehicle->setToRestState();
	Vehicle->mDriveDynData.setUseAutoGears(bUseAutomaticGears);
	Vehicle->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
	return Vehicle;
}

void UWheeledVehicleMovementComponent4W::UpdateSimulation(float DeltaTime)
{
	PxVehicleDrive4W* Vehicle = static_cast<PxVehicleDrive4W*>(PVehicle);
	const float Speed = Vehicle->computeForwardSpeed();
	const PxU32 Gear = Vehicle->mDriveDynData.getCurrentGear();
	const bool bInReverse = (Gear == PxVehicleGearsData::eREVERSE);

	// 스로틀 하나로 전진·후진을 다룬다(언리얼 4W와 같은 방식).
	// - 앞으로 가는 중에 뒤로 누르면 먼저 브레이크, 거의 멈추면 후진 기어로 바꿔 가속한다.
	// - 후진 중에 앞으로 누르면 반대로 한다.
	float Accel = 0.0f;
	float Brake = RawBrake;
	constexpr float StopSpeed = 1.0f;   // m/s
	if (RawThrottle > 0.0f)
	{
		if (bInReverse && Speed < -StopSpeed)
		{
			Brake = std::max(Brake, RawThrottle);
		}
		else
		{
			if (bInReverse) Vehicle->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
			Accel = RawThrottle;
		}
	}
	else if (RawThrottle < 0.0f)
	{
		if (!bInReverse && Speed > StopSpeed)
		{
			Brake = std::max(Brake, -RawThrottle);
		}
		else
		{
			if (!bInReverse) Vehicle->mDriveDynData.forceGearChange(PxVehicleGearsData::eREVERSE);
			Accel = -RawThrottle;
		}
	}

	PxVehicleDrive4WRawInputData Input;
	Input.setAnalogAccel(Accel);
	Input.setAnalogBrake(Brake);
	Input.setAnalogHandbrake(bRawHandbrake ? 1.0f : 0.0f);
	Input.setAnalogSteer(RawSteering);   // 양수가 오른쪽
	PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs(GPadSmoothing, GSteerVsForwardSpeedTable, Input, DeltaTime, bIsInAir, *Vehicle);
}

float UWheeledVehicleMovementComponent4W::GetEngineRotationSpeed() const
{
	if (!PVehicle) return 0.0f;
	const PxVehicleDrive4W* Vehicle = static_cast<const PxVehicleDrive4W*>(PVehicle);
	return Vehicle->mDriveDynData.getEngineRotationSpeed() * 60.0f / (2.0f * PxPi);
}

int32 UWheeledVehicleMovementComponent4W::GetCurrentGear() const
{
	if (!PVehicle) return 0;
	const PxVehicleDrive4W* Vehicle = static_cast<const PxVehicleDrive4W*>(PVehicle);
	// PhysX: 0 후진, 1 중립, 2~ 전진 → 엔진: -1, 0, 1~
	return static_cast<int32>(Vehicle->mDriveDynData.getCurrentGear()) - 1;
}
