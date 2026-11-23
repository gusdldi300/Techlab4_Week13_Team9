#pragma once
#include "Physics/PhysicsScene.h"

namespace physx
{
	class PxScene;
}

//FPhysScene은 UWorld 하나에 대응하는 물리 세계로, PxScene을 소유하고 매 프레임 시뮬레이션과 결과 동기화를 책임지는 클래스다
class FPhysicsScene_PhysX : public FPhysicsScene
{
public:
	FPhysicsScene_PhysX() = default;
	~FPhysicsScene_PhysX();
	FPhysicsScene_PhysX(const FPhysicsScene_PhysX&) = delete;
	FPhysicsScene_PhysX& operator=(const FPhysicsScene_PhysX&) = delete;

	void CreatePhysicsScene() override;
	void StartFrame(float DeltaTime) override;
	void EndFrame() override;
	void AddActor(const FPhysicsActorHandle& Handle) override;


private:
	// 한 스텝을 시작한다. simulate 후 bSimulating이 true가 된다.
	void BeginStep();
	// 진행 중인 스텝을 끝내고 통계와 컴포넌트 위치를 갱신한다.
	void FinishStep();

	physx::PxScene* PhysXScene = nullptr;

	// 아직 시뮬레이션하지 않은 누적 시간
	float Accumulator = 0.0f;
	//// simulate 후 fetchResults 전인가
	bool  bSimulating = false;
	static constexpr float FixedStep = 1.0f / 60.0f;
	// 한 프레임에 돌 수 있는 최대 스텝 수. 이보다 밀린 시간은 버린다(Spiral of Death 방지).
	static constexpr int MaxStepsPerFrame = 4;



};