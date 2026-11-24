#pragma once

#include "Core/Types.h"
#include "AggregateGeom.h"
#include "Math/Vector4.h"

class FLineBatcher;
class UWorld;
struct FPhysicsActorHandle;

namespace FPhysicsDebugDraw {
	void DrawWorldBodies(UWorld* World, FLineBatcher* Lines);
	// Caller holds the owning physics scene read lock.
	void DrawActorShapes(FLineBatcher* Lines, const FPhysicsActorHandle& Actor, const FVector4& Color);
	void DrawAggregateGeom(FLineBatcher* LineBatcher, const FKAggregateGeom& AggGeom,
		const FTransform& WorldTransform, const FVector4& Color);
}