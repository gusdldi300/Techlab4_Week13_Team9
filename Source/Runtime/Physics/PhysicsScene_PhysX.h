#pragma once
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXVehicleManager.h"

namespace physx
{
	class PxScene;
}

namespace nv { namespace cloth { class Solver; } }

//FPhysScene은 UWorld 하나에 대응하는 물리 세계로, PxScene을 소유하고 매 프레임 시뮬레이션과 결과 동기화를 책임지는 클래스다
class FPhysicsScene_PhysX : public FPhysicsScene
{
public:
	FPhysicsScene_PhysX() = default;
	~FPhysicsScene_PhysX();
	FPhysicsScene_PhysX(const FPhysicsScene_PhysX&) = delete;
	FPhysicsScene_PhysX& operator=(const FPhysicsScene_PhysX&) = delete;

	void CreatePhysicsScene() override;
	bool SetGravity(const FVector& Gravity) override;
	void StartFrame(float DeltaTime) override;
	void EndFrame() override;
	void AddActor(const FPhysicsActorHandle& Handle) override;
	FPhysXVehicleManager* GetVehicleManager() override { return VehicleManager.get(); }
	void DrawDebugBodies(FLineBatcher* Lines) const override;

	void AddCloth(const FClothHandle& Handle) override;
	void RemoveCloth(const FClothHandle& Handle) override;


private:
	// 한 스텝을 시작한다. simulate 후 bSimulating이 true가 된다.
	void BeginStep();
	// 이 Scene의 천을 FixedStep만큼 진행한다. PhysX가 워커 스레드에서 강체를 계산하는 동안 게임 스레드에서 돈다.
	void StepCloth();
	// 진행 중인 스텝을 끝내고 통계와 컴포넌트 위치를 갱신한다.
	void FinishStep();

	physx::PxScene* PhysXScene = nullptr;

	// 이 Scene의 차량 관리자. 배치 쿼리가 PhysXScene에 묶이므로 PhysXScene보다 먼저 해제한다.
	std::unique_ptr<FPhysXVehicleManager> VehicleManager;

	// 아직 시뮬레이션하지 않은 누적 시간
	float Accumulator = 0.0f;
	//// simulate 후 fetchResults 전인가
	bool  bSimulating = false;
	static constexpr float FixedStep = 1.0f / 60.0f;
	// 한 프레임에 돌 수 있는 최대 스텝 수. 이보다 밀린 시간은 버린다(Spiral of Death 방지).
	static constexpr int MaxStepsPerFrame = 4;

	// 이 Scene의 천을 한 번에 시뮬레이션하는 NvCloth 실행기. Scene이 소유하고 함께 해제한다.
	nv::cloth::Solver* ClothSolver = nullptr;


};
