#pragma once
#include <memory>
#include "Core/Types.h"
#include "Containers/Array.h"

namespace physx
{
	class PxScene;
	class PxRigidActor;
	class PxBatchQuery;
	class PxVehicleDrivableSurfaceToTireFrictionPairs;
}

class UWheeledVehicleMovementComponent;

// 물리 Scene 하나에 들어 있는 모든 차량을 모아 매 스텝 한 번에 갱신하는 관리자.
// 언리얼 PhysXVehicles의 FPhysXVehicleManager에 대응한다.
//
// 관계
// - FPhysicsScene_PhysX가 unique_ptr로 소유한다(합성). 바퀴 레이용 PxBatchQuery가 그 PxScene에 묶이므로 Scene과 수명이 같아야 한다.
// - 차량 컴포넌트는 가리키기만 한다(집약). 차는 레벨에서 생기고 사라지며, 컴포넌트가 스스로 Add/Remove한다.
class FPhysXVehicleManager
{
public:
	explicit FPhysXVehicleManager(physx::PxScene* InScene);
	~FPhysXVehicleManager();
	// 배치 쿼리와 마찰 표를 소유하므로 복사하면 두 관리자가 같은 객체를 release하게 된다.
	FPhysXVehicleManager(const FPhysXVehicleManager&) = delete;
	FPhysXVehicleManager& operator=(const FPhysXVehicleManager&) = delete;

	void AddVehicle(UWheeledVehicleMovementComponent* Vehicle);
	void RemoveVehicle(UWheeledVehicleMovementComponent* Vehicle);

	// simulate 직전에 부른다. 입력 반영 → 바퀴 레이캐스트 → 서스펜션·타이어·엔진 계산 순서로 돈다.
	// Scene 쓰기 잠금 안에서 불러야 한다(PxVehicleUpdates가 차체 속도를 바꾼다).
	void Update(float DeltaTime);

	// 차체 Shape에 표시를 남겨 바퀴 레이가 자기 차체를 맞히지 않게 한다.
	// 표시가 없으면 레이가 차체에 먼저 맞아 서스펜션이 늘 압축된 것으로 계산되고, 차가 공중에 뜬 채 떨린다.
	static void MarkChassisShapes(physx::PxRigidActor& ChassisActor);

private:
	// 이번 스텝에 필요한 바퀴 수만큼 결과 버퍼와 배치 쿼리를 확보한다.
	void EnsureQueryCapacity(uint32 NumWheels);

	physx::PxScene* Scene = nullptr;   // 소유하지 않는다. FPhysicsScene_PhysX가 이 관리자보다 늦게 release한다.

	TArray<UWheeledVehicleMovementComponent*> Vehicles;

	// 바퀴 레이를 한 번에 쏘는 배치 쿼리. 바퀴 수가 늘면 다시 만든다.
	physx::PxBatchQuery* BatchQuery = nullptr;
	uint32 QueryCapacity = 0;

	// 노면 재질 × 타이어 종류 마찰 표. 표에 없는 재질은 노면 타입 0으로 처리되므로 한 칸이면 모든 바닥에 쓸 수 있다.
	physx::PxVehicleDrivableSurfaceToTireFrictionPairs* FrictionPairs = nullptr;

	// PhysX 결과 버퍼(PxRaycastQueryResult 등)는 typedef라 헤더에서 전방 선언할 수 없어 .cpp에 정의한다.
	struct FBuffers;
	std::unique_ptr<FBuffers> Buffers;
};
