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

	void ConvertPxHitToHit(const PxLocationHit& Hit, FHitResult & HitResult, FVector Start, FVector End, bool bBlock) {
		// PxLocationHit -> FHitResult 타입으로 변환
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

		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			HitResult.Time = 0.0f;
			HitResult.Location = Start;
		}
		else
		{
			HitResult.Time = Hit.distance / Length;
			HitResult.Location = Start + Delta.Normalized() * Hit.distance;
		}
	}

	bool CreatePxGeometry(const FCollisionShape& Shape, PxGeometryHolder& OutGeom) {
		switch (Shape.Type) {
		case ECollisionShape::Sphere:
			OutGeom = PxSphereGeometry(Shape.HalfExtent.X);
			return true;
		case ECollisionShape::Box:
			OutGeom = PxBoxGeometry(Shape.HalfExtent.X, Shape.HalfExtent.Y, Shape.HalfExtent.Z);
			return true;
		case ECollisionShape::Sphyl:
			OutGeom = PxCapsuleGeometry(Shape.HalfExtent.X, Shape.HalfExtent.Z);
			return true;
		}
		return false;
	}

	class FQueryFilterCallback : public PxQueryFilterCallback
	{
	public:
		FQueryFilterCallback(ECollisionChannel InChannel, const FCollisionQueryParams& InParams)
			: Channel(InChannel), Params(InParams) {}

		PxQueryHitType::Enum preFilter(const PxFilterData& filterData, const PxShape* shape,
			const PxRigidActor* actor, PxHitFlags& queryFlags) override
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

		// ePOSTFILTER를 켜지 않으므로 호출되지 않는다. 순수 가상 함수여서 정의만 둔다.
		PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit) override
		{
			return PxQueryHitType::eNONE;
		}

	private:
		ECollisionChannel Channel;
		const FCollisionQueryParams& Params;
	};

	PxQueryFilterData CreatePxOverlapFilterData(bool bAnyHit) {
		PxQueryFlags Flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC
			| PxQueryFlag::ePREFILTER | PxQueryFlag::eNO_BLOCK;

		if (bAnyHit) Flags |= PxQueryFlag::eANY_HIT;
		return PxQueryFilterData(Flags);
	}
}

namespace FPhysicsQuery
{
	bool RaycastSingle(UWorld* World, FHitResult& OutHit,
		const FVector& Start, const FVector& End,
		ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		// 값 셋팅 후 raycast 진행 
		PxScene * Scene = GetPxScene(World);
		if (Scene == nullptr) return false;

		OutHit.Init(Start, End);
		FVector Direction = End - Start;
		if (Direction.Size() < KINDA_SMALL_NUMBER) return false;

		FQueryFilterCallback FilterCall(Channel, Params);
		PxRaycastBuffer RayBuffer;

		Scene->raycast(U2PVector(Start), U2PVector(Direction.Normalized()), Direction.Size(),
			RayBuffer, PxHitFlag::eDEFAULT, CreatePxQueryFilterData(),&FilterCall);
		
		// Block 판정 받은 shape 가 있다면 변환
		if (RayBuffer.hasBlock) {
			ConvertPxHitToHit(RayBuffer.block, OutHit, Start, End, true);
			return true;
		}

		return false;
	}


	bool RaycastMulti(UWorld* World, TArray<FHitResult>& OutHits,
		const FVector& Start, const FVector& End,
		ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		// 값 셋팅 후 raycast 진행 
		PxScene* Scene = GetPxScene(World);
		if (Scene == nullptr) return false;

		OutHits.Empty();
		FVector Direction = End - Start;
		if (Direction.Size() < KINDA_SMALL_NUMBER) return false;

		// 멀티 설정 
		PxRaycastHit TouchBuffer[32];
		FQueryFilterCallback FilterCall(Channel, Params);
		PxRaycastBuffer RayBuffer(TouchBuffer, 32);

		Scene->raycast(U2PVector(Start), U2PVector(Direction.Normalized()), Direction.Size(),
			RayBuffer, PxHitFlag::eDEFAULT, CreatePxQueryFilterData(), &FilterCall);

		// Touch 된 것 OutHits 에 넣기
		for (PxU32 i = 0; i < RayBuffer.getNbTouches(); ++i) {
			FHitResult Hit;
			ConvertPxHitToHit(RayBuffer.getTouch(i), Hit, Start, End, false);
			OutHits.Add(Hit);
		}
		
		// Block 된 것 OutHits 에 넣기
		if (RayBuffer.hasBlock) {
			FHitResult Hit;
			ConvertPxHitToHit(RayBuffer.block, Hit, Start, End, true);
			OutHits.Add(Hit);
		}

		if (!OutHits.IsEmpty()) return true;

		return false;
	}


	bool SweepSingle(UWorld* World, FHitResult& OutHit,
		const FVector& Start, const FVector& End, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		PxGeometryHolder Holder;
		if (!CreatePxGeometry(Shape, Holder)) return false;

		// 회전 보정
		PxQuat Rotation = U2PQuat(Rot);
		if (Shape.Type == ECollisionShape::Sphyl) Rotation = Rotation * PxQuat(-PxHalfPi, PxVec3(0, 1, 0));
		
		PxTransform PxTrans(U2PVector(Start), Rotation.getNormalized());
		
		// 값 셋팅 후 raycast 진행 
		PxScene* Scene = GetPxScene(World);
		if (Scene == nullptr) return false;

		OutHit.Init(Start, End);
		FVector Direction = End - Start;
		if (Direction.Size() < KINDA_SMALL_NUMBER) return false;

		FQueryFilterCallback FilterCall(Channel, Params);
		PxSweepBuffer SweepBuffer;

		Scene->sweep(Holder.any(), PxTrans, U2PVector(Direction.Normalized()), Direction.Size(),
			SweepBuffer, PxHitFlag::eDEFAULT, CreatePxQueryFilterData(), &FilterCall);

		// Block 판정 받은 shape 가 있다면 변환
		if (SweepBuffer.hasBlock) {
			ConvertPxHitToHit(SweepBuffer.block, OutHit, Start, End, true);
			return true;
		}

		return false;
	}


	bool SweepMulti(UWorld* World, TArray<FHitResult>& OutHits,
		const FVector& Start, const FVector& End, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		PxGeometryHolder Holder;
		if (!CreatePxGeometry(Shape, Holder)) return false;

		// 회전 보정
		PxQuat Rotation = U2PQuat(Rot);
		if (Shape.Type == ECollisionShape::Sphyl) Rotation = Rotation * PxQuat(-PxHalfPi, PxVec3(0, 1, 0));

		PxTransform PxTrans(U2PVector(Start), Rotation.getNormalized());

		// 값 셋팅 후 raycast 진행 
		PxScene* Scene = GetPxScene(World);
		if (Scene == nullptr) return false;

		OutHits.Empty();
		FVector Direction = End - Start;
		if (Direction.Size() < KINDA_SMALL_NUMBER) return false;

		// 멀티 설정 
		PxSweepHit TouchBuffer[32];
		FQueryFilterCallback FilterCall(Channel, Params);
		PxSweepBuffer SweepBuffer(TouchBuffer, 32);

		Scene->sweep(Holder.any(), PxTrans, U2PVector(Direction.Normalized()), Direction.Size(),
			SweepBuffer, PxHitFlag::eDEFAULT, CreatePxQueryFilterData(), &FilterCall);

		// Touch 된 것 OutHits 에 넣기
		for (PxU32 i = 0; i < SweepBuffer.getNbTouches(); ++i) {
			FHitResult Hit;
			ConvertPxHitToHit(SweepBuffer.getTouch(i), Hit, Start, End, false);
			OutHits.Add(Hit);
		}

		// Block 된 것 OutHits 에 넣기
		if (SweepBuffer.hasBlock) {
			FHitResult Hit;
			ConvertPxHitToHit(SweepBuffer.block, Hit, Start, End, true);
			OutHits.Add(Hit);
		}

		if (!OutHits.IsEmpty()) return true;
		return false;

	}

	bool OverlapAnyTest(UWorld* World,	const FVector& Pos, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		PxGeometryHolder Holder;
		if (!CreatePxGeometry(Shape, Holder)) return false;

		// 회전 보정
		PxQuat Rotation = U2PQuat(Rot);
		if (Shape.Type == ECollisionShape::Sphyl) Rotation = Rotation * PxQuat(-PxHalfPi, PxVec3(0, 1, 0));

		PxTransform PxTrans(U2PVector(Pos), Rotation.getNormalized());

		// 값 셋팅 후 raycast 진행 
		PxScene* Scene = GetPxScene(World);
		if (Scene == nullptr) return false;

		FQueryFilterCallback FilterCall(Channel, Params);
		PxOverlapBuffer OverlapBuffer;

		Scene->overlap(Holder.any(), PxTrans, OverlapBuffer, CreatePxOverlapFilterData(true), &FilterCall);

		return OverlapBuffer.getNbAnyHits() > 0;
	}

	bool OverlapMulti(UWorld* World, TArray<FOverlapResult>& OutOverlaps, const FVector& Pos, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel, const FCollisionQueryParams& Params)
	{
		PxGeometryHolder Holder;
		if (!CreatePxGeometry(Shape, Holder)) return false;

		// 회전 보정
		PxQuat Rotation = U2PQuat(Rot);
		if (Shape.Type == ECollisionShape::Sphyl) Rotation = Rotation * PxQuat(-PxHalfPi, PxVec3(0, 1, 0));

		PxTransform PxTrans(U2PVector(Pos), Rotation.getNormalized());

		// 값 셋팅 후 raycast 진행 
		OutOverlaps.Empty();
		PxScene* Scene = GetPxScene(World);
		if (Scene == nullptr) return false;

		// 멀티 설정 
		PxOverlapHit TouchBuffer[32];
		FQueryFilterCallback FilterCall(Channel, Params);
		PxOverlapBuffer OverlapBuffer(TouchBuffer, 32);

		Scene->overlap(Holder.any(), PxTrans, OverlapBuffer, CreatePxOverlapFilterData(false), &FilterCall);

		// 소유 컴포넌트 아는것만 넣자!
		for (PxU32 i = 0; i < OverlapBuffer.getNbTouches(); ++i) {
			const PxOverlapHit& Hit = OverlapBuffer.getTouch(i);
			const FBodyInstance* BodyInstance = static_cast<const FBodyInstance*>(Hit.actor->userData);
			if (BodyInstance == nullptr) continue;

			UPrimitiveComponent* Component = BodyInstance->GetOwnerComponent();
			if (Component == nullptr) continue;

			FOverlapResult Result;
			Result.Component = Component;
			Result.Actor = Component->GetOwner();
			OutOverlaps.Add(Result);
		}

		return !OutOverlaps.IsEmpty();
	}
}
