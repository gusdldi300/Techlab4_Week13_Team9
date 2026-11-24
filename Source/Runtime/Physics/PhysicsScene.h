#pragma once
#include <memory>
#include "Core/Types.h"
#include "Physics/PhysicsHandles.h"

// stat physics가 표시하는 물리 세계의 개수 통계. 엔진 타입만 써서 에디터가 PhysX를 몰라도 읽을 수 있다.
// 시뮬레이션 도중에 읽으면 값이 섞이므로, 구현이 한 스텝을 끝낸 직후(fetchResults 직후)에 채운다.
struct FPhysicsSceneStats
{
	uint32 NumStaticBodies = 0;          // Scene에 있는 Static 바디
	uint32 NumDynamicBodies = 0;         // Scene에 있는 Dynamic 바디 (Kinematic 포함)
	uint32 NumKinematicBodies = 0;       // 그중 Kinematic
	uint32 NumActiveDynamicBodies = 0;   // 깨어 있어 실제로 계산된 Dynamic 바디
	uint32 NumSyncedComponents = 0;      // 이번 스텝에 위치를 갱신한 컴포넌트
	uint32 NumContactPairs = 0;          // 실제로 닿아 있는 Shape 쌍
	uint32 NumNewTouches = 0;            // 이번 스텝에 새로 닿은 쌍
	uint32 NumLostTouches = 0;           // 이번 스텝에 떨어진 쌍
	uint32 NumActiveConstraints = 0;     // 관절 등 활성 구속
	uint64 NumSteps = 0;                 // 지금까지 진행한 시뮬레이션 스텝 수
	float  FixedStep = 0.0f;             // 한 스텝의 시간(초)
	uint32 NumStepsLastFrame = 0;        // 직전 프레임에 돈 스텝 수 (0이면 누적 시간이 모자라 쉬었다)
};


class FLineBatcher;

//서로다른 물리엔진에서 시뮬레이션 시키는 공간을 추상화한 클래스 physX는 PxScene, havok hknpWorld라는 이름으로 불리므로
//물리 엔진을 교체하더라도 유연하게 대응하기 위해 이 클래스를 만들었다.
class FPhysicsScene
{
public:
	FPhysicsScene() = default;
	virtual ~FPhysicsScene() = default;
	FPhysicsScene(const FPhysicsScene&) = delete;
	FPhysicsScene& operator=(const FPhysicsScene&) = delete;

	virtual void CreatePhysicsScene()  = 0;

	//이번 프레임의 물리 계산을 시작시킨다
	virtual void StartFrame(float DeltaTime) = 0;
	//계산이 끝나기를 기다리고 결과를 엔진에 반영한다
	virtual void EndFrame() = 0;

	//바디를 이 물리 세계에 넣는다. 넣은 뒤부터 매 시뮬레이션의 계산 대상이 된다
	//해제는 바디 쪽에서 액터를 release하면 Scene에서도 자동으로 빠진다
	virtual void AddActor(const FPhysicsActorHandle& Handle) = 0;

	// 이 물리 세계의 차량 관리자. 차량을 지원하지 않는 구현은 nullptr을 돌려준다.
	// 차량 컴포넌트가 자기를 등록할 때 쓴다. 소유권은 Scene에 있다.
	virtual class FPhysXVehicleManager* GetVehicleManager() { return nullptr; }

	// Debug rendering reads the completed simulation, including aggregate actors.
	virtual void DrawDebugBodies(FLineBatcher* Lines) const = 0;

	//stat physics용 통계. 마지막으로 끝난 스텝 기준의 값이다
	const FPhysicsSceneStats& GetStats() const { return Stats; }

protected:
	//구현이 EndFrame에서 채운다
	FPhysicsSceneStats Stats;

private:



};

// 사용할 물리 엔진의 FPhysicsScene 구현을 만들어 돌려준다.
// 정의는 구현 쪽 .cpp(PhysicsScene_PhysX.cpp)에 있어서, 호출하는 UWorld는 어떤 구현인지 모른다.
// 물리 엔진을 바꿀 때는 이 함수의 정의만 바꾼다.
std::unique_ptr<FPhysicsScene> MakePhysicsScene();