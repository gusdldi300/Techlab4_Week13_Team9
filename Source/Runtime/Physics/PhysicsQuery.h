#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"       
#include "Math/Vector.h"            
#include "Collision/HitResult.h"    
#include "CollisionChannel.h"   
#include "Math/Quat.h"
#include "ShapeElem.h"

class UWorld;
class AActor;
class UPrimitiveComponent;

struct FOverlapResult {
	AActor* Actor = nullptr;
	UPrimitiveComponent* Component = nullptr;
	int32 ItemIndex = INDEX_NONE;
	bool bBlockingHit = false;
};

struct FCollisionQueryParams {
	TArray <AActor*> IgnoreActors;
	TArray <UPrimitiveComponent*> IgnoreComponents;
	bool bTraceComplex = false;
	bool bFindInitialOverlaps = true;
};

struct FCollisionShape {
	ECollisionShape Type = ECollisionShape::Unknown;
	FVector HalfExtent = FVector::ZeroVector;

	static FCollisionShape MakeSphere(float Radius) {
		FCollisionShape Shape;
		Shape.Type = ECollisionShape::Sphere;
		Shape.HalfExtent = FVector(Radius, 0, 0);
		return Shape;
	}
	static FCollisionShape MakeBox(FVector HalfExtent) {
		FCollisionShape Shape;
		Shape.Type = ECollisionShape::Box;
		Shape.HalfExtent = HalfExtent;
		return Shape;
	}
	static FCollisionShape MakeCapsule(float Radius, float HalfHeight) {
		FCollisionShape Shape;
		Shape.Type = ECollisionShape::Sphyl;
		Shape.HalfExtent = FVector(Radius, 0, HalfHeight);
		return Shape;
	}
};

namespace FPhysicsQuery
{
	bool RaycastSingle(UWorld* World, FHitResult& OutHit,
		const FVector& Start, const FVector& End,
		ECollisionChannel Channel,
		const FCollisionQueryParams& Params = FCollisionQueryParams());

	bool RaycastMulti(UWorld* World, TArray<FHitResult>& OutHits,
		const FVector& Start, const FVector& End,
		ECollisionChannel Channel,
		const FCollisionQueryParams& Params = FCollisionQueryParams());

	bool SweepSingle(UWorld* World, FHitResult& OutHit,
		const FVector& Start, const FVector& End, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel,
		const FCollisionQueryParams& Params = FCollisionQueryParams());

	bool SweepMulti(UWorld* World, TArray<FHitResult>& OutHits,
		const FVector& Start, const FVector& End, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel,
		const FCollisionQueryParams& Params = FCollisionQueryParams());

	bool OverlapAnyTest(UWorld* World,
		const FVector& Pos, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel,
		const FCollisionQueryParams& Params = FCollisionQueryParams());

	bool OverlapMulti(UWorld* World, TArray<FOverlapResult>& OutOverlaps,
		const FVector& Pos, const FQuat& Rot,
		const FCollisionShape& Shape, ECollisionChannel Channel,
		const FCollisionQueryParams& Params = FCollisionQueryParams());
}
