#include "EnginePCH.h"
#include "PhysXVehicleManager.h"
#include "PhysicsCore.h"
#include "Components/WheeledVehicleMovementComponent.h"
#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleSDK.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleTireFriction.h>
#include <vector>

using namespace physx;

namespace
{
	// 차체 Shape의 쿼리 필터 word3에 남기는 표시. 엔진의 다른 쿼리는 필터 데이터를 쓰지 않으므로 영향이 없다.
	constexpr PxU32 ChassisShapeFlag = 0x0000ffffu;

	// 바퀴 레이가 맞힐 후보를 고른다. filterData1이 맞을 수 있는 Shape의 쿼리 필터 데이터다.
	PxQueryHitType::Enum WheelRaycastPreFilter(
		PxFilterData /*QueryData*/, PxFilterData ShapeData,
		const void* /*ConstantBlock*/, PxU32 /*ConstantBlockSize*/, PxHitFlags& /*QueryFlags*/)
	{
		return (ShapeData.word3 & ChassisShapeFlag) ? PxQueryHitType::eNONE : PxQueryHitType::eBLOCK;
	}
}

struct FPhysXVehicleManager::FBuffers
{
	std::vector<PxRaycastQueryResult> RaycastResults;
	std::vector<PxRaycastHit>         RaycastHits;
	std::vector<PxWheelQueryResult>   WheelResults;
	std::vector<PxVehicleWheelQueryResult> VehicleResults;
	std::vector<PxVehicleWheels*>     PxVehicles;
	std::vector<UWheeledVehicleMovementComponent*> Active;
};

FPhysXVehicleManager::FPhysXVehicleManager(PxScene* InScene)
	: Scene(InScene)
	, Buffers(std::make_unique<FBuffers>())
{
	// 타이어 종류 1, 노면 종류 1. 기본 재질을 노면 0으로 등록하고 마찰 배율 1.0을 준다.
	FrictionPairs = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1);
	if (FrictionPairs)
	{
		const PxMaterial* Materials[1] = { FPhysicsCore::Get().GetDefaultMaterial() };
		PxVehicleDrivableSurfaceType SurfaceTypes[1];
		SurfaceTypes[0].mType = 0;
		FrictionPairs->setup(1, 1, Materials, SurfaceTypes);
		FrictionPairs->setTypePairFriction(0, 0, 1.0f);
	}
}

FPhysXVehicleManager::~FPhysXVehicleManager()
{
	// 남은 컴포넌트가 이 관리자를 가리키지 않게 끊는다. 정상 경로에서는 컴포넌트가 먼저 빠져 비어 있다.
	for (UWheeledVehicleMovementComponent* Vehicle : Vehicles)
	{
		if (Vehicle) Vehicle->OnVehicleManagerDestroyed();
	}
	Vehicles.Empty();

	if (BatchQuery)
	{
		BatchQuery->release();
		BatchQuery = nullptr;
	}
	if (FrictionPairs)
	{
		FrictionPairs->release();
		FrictionPairs = nullptr;
	}
}

void FPhysXVehicleManager::AddVehicle(UWheeledVehicleMovementComponent* Vehicle)
{
	if (Vehicle) Vehicles.AddUnique(Vehicle);
}

void FPhysXVehicleManager::RemoveVehicle(UWheeledVehicleMovementComponent* Vehicle)
{
	Vehicles.Remove(Vehicle);
}

void FPhysXVehicleManager::MarkChassisShapes(PxRigidActor& ChassisActor)
{
	const PxU32 NumShapes = ChassisActor.getNbShapes();
	std::vector<PxShape*> Shapes(NumShapes);
	ChassisActor.getShapes(Shapes.data(), NumShapes);
	for (PxShape* Shape : Shapes)
	{
		PxFilterData Data = Shape->getQueryFilterData();
		Data.word3 |= ChassisShapeFlag;
		Shape->setQueryFilterData(Data);
	}
}

void FPhysXVehicleManager::EnsureQueryCapacity(uint32 NumWheels)
{
	if (NumWheels <= QueryCapacity && BatchQuery) return;

	if (BatchQuery)
	{
		BatchQuery->release();
		BatchQuery = nullptr;
	}

	// 차가 하나씩 늘 때마다 다시 만들지 않게 두 배씩 늘린다.
	QueryCapacity = std::max<uint32>(NumWheels, std::max<uint32>(QueryCapacity * 2, 8));
	Buffers->RaycastResults.assign(QueryCapacity, PxRaycastQueryResult());
	Buffers->RaycastHits.assign(QueryCapacity, PxRaycastHit());

	// 바퀴 하나당 레이 하나. 가장 가까운 한 점(block)만 쓰므로 터치 버퍼는 바퀴 수만큼이면 된다.
	PxBatchQueryDesc Desc(QueryCapacity, 0, 0);
	Desc.queryMemory.userRaycastResultBuffer = Buffers->RaycastResults.data();
	Desc.queryMemory.userRaycastTouchBuffer = Buffers->RaycastHits.data();
	Desc.queryMemory.raycastTouchBufferSize = QueryCapacity;
	Desc.preFilterShader = WheelRaycastPreFilter;
	BatchQuery = Scene->createBatchQuery(Desc);
	if (!BatchQuery)
	{
		KRS_LOG(Error, "[Vehicle] createBatchQuery failed");
		QueryCapacity = 0;
	}
}

void FPhysXVehicleManager::Update(float DeltaTime)
{
	if (Vehicles.IsEmpty() || !Scene || !FrictionPairs) return;

	FBuffers& B = *Buffers;
	B.PxVehicles.clear();
	B.Active.clear();

	// 1. 입력 반영. 입력은 게임 Tick에서 저장해 둔 값을 여기서 PhysX에 넣는다.
	uint32 NumWheels = 0;
	for (UWheeledVehicleMovementComponent* Vehicle : Vehicles)
	{
		PxVehicleWheels* PVehicle = Vehicle ? Vehicle->GetPhysXVehicle() : nullptr;
		if (!PVehicle) continue;

		Vehicle->PreSimulate(DeltaTime);
		B.PxVehicles.push_back(PVehicle);
		B.Active.push_back(Vehicle);
		NumWheels += PVehicle->mWheelsSimData.getNbWheels();
	}
	if (B.PxVehicles.empty()) return;

	EnsureQueryCapacity(NumWheels);
	if (!BatchQuery) return;

	const PxU32 NumVehicles = static_cast<PxU32>(B.PxVehicles.size());

	// 2. 모든 바퀴의 서스펜션 레이를 한 번에 쏜다.
	PxVehicleSuspensionRaycasts(BatchQuery, NumVehicles, B.PxVehicles.data(), QueryCapacity, B.RaycastResults.data());

	// 3. 레이 결과로 서스펜션 힘, 타이어 힘, 엔진·기어를 계산하고 차체 속도를 바꾼다.
	B.WheelResults.assign(NumWheels, PxWheelQueryResult());
	B.VehicleResults.resize(NumVehicles);
	uint32 Offset = 0;
	for (PxU32 i = 0; i < NumVehicles; ++i)
	{
		const PxU32 Count = B.PxVehicles[i]->mWheelsSimData.getNbWheels();
		B.VehicleResults[i].wheelQueryResults = B.WheelResults.data() + Offset;
		B.VehicleResults[i].nbWheelQueryResults = Count;
		Offset += Count;
	}
	PxVehicleUpdates(DeltaTime, Scene->getGravity(), *FrictionPairs, NumVehicles, B.PxVehicles.data(), B.VehicleResults.data());

	// 4. 바퀴 상태(회전각, 조향각, 서스펜션 위치, 공중 여부)를 컴포넌트가 엔진 타입으로 옮겨 둔다.
	for (PxU32 i = 0; i < NumVehicles; ++i)
	{
		B.Active[i]->PostSimulate(B.VehicleResults[i]);
	}
}
