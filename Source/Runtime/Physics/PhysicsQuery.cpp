#include "EnginePCH.h"
#include "PhysicsQuery.h"
#include "PhysicsScene_PhysX.h"
#include "PhysicsFiltering.h"
#include "PhysXConversions.h"
#include "Engine/World.h"
#include <PxPhysicsAPI.h>

using namespace physx;

namespace {
	physx::PxScene* GetPxScene(UWorld* World) {
		if (World == nullptr) return nullptr;

		FPhysicsScene* Scene = World->GetPhysicsScene();
		if (Scene == nullptr) return nullptr;

		FPhysicsScene_PhysX* PhysX = static_cast<FPhysicsScene_PhysX*>(Scene);

		return PhysX->GetNativeScene();
	}

	PxQueryFilterData CreatePxQueryFilterData() {
		return PxQueryFilterData(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
	}

	void ConvertRayHitToHit(const PxRaycastHit& Hit, FHitResult & HitResult, FVector Start, FVector End, bool bBlock) {
		// PxRaycastHit -> FHitResult 타입으로 변환
		HitResult.ImpactPoint = P2UVector(Hit.position);
		HitResult.ImpactNormal = P2UVector(Hit.normal);
		HitResult.Distance = Hit.distance;
		HitResult.FaceIndex = Hit.faceIndex;
		HitResult.TraceStart = Start;
		HitResult.TraceEnd = End;
		HitResult.bBlockingHit = bBlock;

		FBodyInstance* BodyInstance = static_cast<FBodyInstance*> (Hit.actor->userData);
		if(BodyInstance != nullptr)
			HitResult.HitComponent = BodyInstance->GetOwnerComponent();
		if ((End - Start).Size() <= KINDA_SMALL_NUMBER) HitResult.Time = 0.0f;
		else	HitResult.Time = Hit.distance / (End - Start).Size();
		HitResult.Location = P2UVector(Hit.position);
	}
}

namespace FPhysicsQuery
{
	bool RaycastSingle(UWorld* World, FHitResult& OutHit,
		const FVector& Start, const FVector& End,
		ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		return false;
	}

	bool RaycastMulti(UWorld* World, TArray<FHitResult>& OutHits,
		const FVector& Start, const FVector& End,
		ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		return false;
	}

	bool SweepSingle(UWorld* World, FHitResult& OutHit,
		const FVector& Start, const FVector& End, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		return false;
	}

	bool SweepMulti(UWorld* World, TArray<FHitResult>& OutHits,
		const FVector& Start, const FVector& End, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		return false;
	}

	bool OverlapAnyTest(UWorld* World,
		const FVector& Pos, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		return false;
	}

	bool OverlapMulti(UWorld* World, TArray<FOverlapResult>& OutOverlaps,
		const FVector& Pos, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		return false;
	}
}

PxQueryHitType::Enum FQueryFilterCallback::preFilter(const PxFilterData& filterData, const PxShape* shape, 
	const PxRigidActor* actor, PxHitFlags& queryFlags)
{
	// 바디 컴포넌트들이 무시 목록에 포함되어있다면 필터링
	if (const FBodyInstance* BodyInstance = static_cast<const FBodyInstance*>(actor->userData))
	{
		if (UPrimitiveComponent* BodyComponent = BodyInstance->GetOwnerComponent())
		{
			if (Params.IgnoreComponents.Contains(BodyComponent)) return PxQueryHitType::eNONE;
			if (Params.IgnoreActors.Contains(BodyComponent->GetOwner())) return PxQueryHitType::eNONE;
		}
	}

	// 채널&필터링시 필터링
	PxFilterData QueryData = shape->getQueryFilterData();
	if ((1u << (uint32)Channel) & QueryData.word1) return PxQueryHitType::eBLOCK;
	if ((1u << (uint32)Channel) & QueryData.word2) return PxQueryHitType::eTOUCH;
	return PxQueryHitType::eNONE;

}

PxQueryHitType::Enum FQueryFilterCallback::postFilter(const PxFilterData& filterData, const PxQueryHit& hit)
{
	return PxQueryHitType::eNONE();
}
