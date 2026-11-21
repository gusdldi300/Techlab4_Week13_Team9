#pragma once
#include <memory>
#include "Physics/PhysicsHandles.h"


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

private:



};

// 사용할 물리 엔진의 FPhysicsScene 구현을 만들어 돌려준다.
// 정의는 구현 쪽 .cpp(PhysicsScene_PhysX.cpp)에 있어서, 호출하는 UWorld는 어떤 구현인지 모른다.
// 물리 엔진을 바꿀 때는 이 함수의 정의만 바꾼다.
std::unique_ptr<FPhysicsScene> MakePhysicsScene();