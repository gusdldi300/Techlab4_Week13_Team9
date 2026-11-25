#include "EnginePCH.h"
#include "GameFramework/RedBuggy.h"
#include "Components/WheeledVehicleMovementComponent4W.h"

ARedBuggy::ARedBuggy()
{
	// Blender에서 차체(본체 + 해치 + 서스펜션 조각)와 바퀴 4개를 나눠 내보낸 FBX를 Static으로 Import한 결과.
	// 바퀴는 앞뒤 크기가 달라 4개 모두 따로 쓴다(0 앞왼, 1 앞오, 2 뒤왼, 3 뒤오).
	BodyMeshPath = "FBX/RedBuggy_Body/RedBuggy_Body_Static.fbx.bin";
	WheelMeshPaths[0] = "FBX/RedBuggy_WheelFL/RedBuggy_WheelFL_Static.fbx.bin";
	WheelMeshPaths[1] = "FBX/RedBuggy_WheelFR/RedBuggy_WheelFR_Static.fbx.bin";
	WheelMeshPaths[2] = "FBX/RedBuggy_WheelRL/RedBuggy_WheelRL_Static.fbx.bin";
	WheelMeshPaths[3] = "FBX/RedBuggy_WheelRR/RedBuggy_WheelRR_Static.fbx.bin";

	UWheeledVehicleMovementComponent4W* Movement = GetVehicleMovement();

	// ---- 차체: 길이 4.46m, 폭 2.94m의 큰 버기. 충돌 박스는 바퀴 안쪽 몸통만 감싼다(지면에서 0.35 ~ 1.15m) ----
	// 박스 폭을 차체 폭(2.94m)으로 잡으면 바퀴와 겹친다. 앞바퀴 안쪽 끝(중심 1.10 - 폭 0.34 = 0.76m)보다 좁게 둔다.
	Movement->Mass = 1200.0f;
	Movement->ChassisLength = 4.2f;
	Movement->ChassisWidth = 1.5f;
	Movement->ChassisHeight = 0.8f;
	// 차가 높아 무게 중심을 낮게 둔다(지면에서 약 0.45m). 높으면 코너에서 바깥으로 넘어간다.
	Movement->COMOffset = FVector(0.0f, 0.0f, -0.3f);

	// ---- 바퀴 (Blender 측정값) ----
	Movement->FrontAxleX = 1.711f;
	Movement->RearAxleX = -1.711f;
	Movement->WheelHalfTrack = 1.101f;
	Movement->WheelCenterZ = -0.093f;
	Movement->WheelRadius = 0.559f;
	Movement->WheelWidth = 0.678f;
	Movement->WheelMass = 35.0f;

	Movement->bSeparateRearWheels = true;
	Movement->RearWheelHalfTrack = 1.184f;
	Movement->RearWheelCenterZ = -0.093f;
	Movement->RearWheelRadius = 0.657f;
	Movement->RearWheelWidth = 0.798f;

	// 기본 강성(1000)이면 가속 중 종방향 슬립이 1단 0.25~0.35, 4단에서도 0.12~0.17로 남아 바퀴가 헛도는 게 보인다.
	// 강성을 올리면 같은 구동력을 더 적은 미끄러짐으로 전달한다.
	Movement->TireLongStiffness = 3000.0f;

	Movement->MaxSteerAngle = 35.0f;
	Movement->InertiaTensorScale = FVector(1.0f, 1.0f, 1.0f);

	// ---- 구동: LSD 4륜, 뒤에 조금 더 싣는다(앞 0.4 : 뒤 0.6) ----
	// 바퀴 반지름이 커서 같은 토크면 바퀴가 땅을 미는 힘(토크 / 반지름)이 작다. 그래서 토크를 올린다.
	Movement->DifferentialType = 0;
	Movement->FrontRearSplit = 0.4f;
	Movement->PeakTorque = 700.0f;
	Movement->MaxRPM = 6500.0f;

	// 부모 생성자는 Cutlass 경로·치수로 이미 한 번 맞췄으므로 바꾼 값으로 다시 맞춘다
	SetupMeshes();
	ApplyDimensions();
}
