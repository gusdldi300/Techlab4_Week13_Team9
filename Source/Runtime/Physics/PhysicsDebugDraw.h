#pragma once

#include "Core/Types.h"
#include "AggregateGeom.h"
#include "Math/Vector4.h"

class FLineBatcher;

namespace FPhysicsDebugDraw {
	void DrawAggregateGeom(FLineBatcher* LineBatcher, const FKAggregateGeom& AggGeom,
		const FTransform& WorldTransform, const FVector4& Color);
}