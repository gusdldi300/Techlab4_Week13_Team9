#include "EnginePCH.h"
#include "GameFramework/VintageRacingCar.h"
#include "Components/WheeledVehicleMovementComponent4W.h"

AVintageRacingCar::AVintageRacingCar()
{
	// Blender에서 차체(엔진 포함)와 바퀴 4개를 나눠 내보낸 FBX를 Static으로 Import한 결과.
	// 바퀴는 앞뒤 크기가 달라 4개 모두 따로 쓴다(0 앞왼, 1 앞오, 2 뒤왼, 3 뒤오).
	BodyMeshPath = "FBX/VintageCar_Body/VintageCar_Body_Static.fbx.bin";
	WheelMeshPaths[0] = "FBX/VintageCar_WheelFL/VintageCar_WheelFL_Static.fbx.bin";
	WheelMeshPaths[1] = "FBX/VintageCar_WheelFR/VintageCar_WheelFR_Static.fbx.bin";
	WheelMeshPaths[2] = "FBX/VintageCar_WheelRL/VintageCar_WheelRL_Static.fbx.bin";
	WheelMeshPaths[3] = "FBX/VintageCar_WheelRR/VintageCar_WheelRR_Static.fbx.bin";

	UWheeledVehicleMovementComponent4W* Movement = GetVehicleMovement();

	// ---- 차체: 가벼운 1인승 경주차. 충돌 박스는 차체 아래쪽 몸통만 감싼다(지면에서 0.25 ~ 0.95m) ----
	Movement->Mass = 750.0f;
	Movement->ChassisLength = 3.3f;
	Movement->ChassisWidth = 1.3f;
	Movement->ChassisHeight = 0.7f;
	// 엔진이 앞에 있어 무게 중심을 조금 앞, 낮게 둔다
	Movement->COMOffset = FVector(0.15f, 0.0f, -0.25f);

	// ---- 바퀴 (Blender 측정값) ----
	Movement->FrontAxleX = 1.2f;
	Movement->RearAxleX = -1.2f;
	Movement->WheelHalfTrack = 0.725f;
	Movement->WheelCenterZ = -0.207f;
	Movement->WheelRadius = 0.395f;
	Movement->WheelWidth = 0.142f;

	Movement->bSeparateRearWheels = true;
	Movement->RearWheelHalfTrack = 0.77f;
	Movement->RearWheelCenterZ = -0.165f;
	Movement->RearWheelRadius = 0.44f;
	Movement->RearWheelWidth = 0.259f;

	Movement->MaxSteerAngle = 35.0f;

	// 짧고 가벼워 위축 관성이 Cutlass의 약 1/3이다. 그대로면 뒤가 조금만 밀려도 바로 돈다(스핀).
	Movement->InertiaTensorScale = FVector(1.0f, 1.0f, 1.1f);

	// ---- 구동: LSD 4륜, 뒤에 더 싣는다(앞 0.35 : 뒤 0.65) ----
	// 뒷바퀴굴림만 쓰면 코너에서 가속할 때 뒷바퀴가 마찰 한계를 구동력에 다 써서 옆으로 밀린다(파워 오버스티어).
	// 앞바퀴도 구동력을 나눠 받으면 뒷바퀴에 옆으로 잡을 여유가 남는다. 뒤 비율이 커서 뒷바퀴굴림 느낌은 남는다.
	Movement->DifferentialType = 0;
	Movement->FrontRearSplit = 0.35f;
	Movement->PeakTorque = 350.0f;
	Movement->MaxRPM = 7000.0f;

	// 부모 생성자는 Cutlass 경로·치수로 이미 한 번 맞췄으므로 바꾼 값으로 다시 맞춘다
	SetupMeshes();
	ApplyDimensions();
}
