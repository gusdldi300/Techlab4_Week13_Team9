#pragma once

#include "Core/Types.h"
#include "ShapeElem.h"
#include "Containers/Array.h"

struct FKAggregateGeom {
	int32 GetElementCount() const { return SphereElems.Num() + BoxElems.Num() + SphylElems.Num(); }
		
	void EmptyElements() {
		SphereElems.Empty();
		BoxElems.Empty();
		SphylElems.Empty();
	}

	float GetVolume() const {
		float Total = 0.0f;
		for (auto& Shape : SphereElems) {
			if (Shape.bContributeToMass) Total += Shape.GetVolume();
		}
		for (auto& Shape : BoxElems) {
			if (Shape.bContributeToMass) Total += Shape.GetVolume();
		}
		for (auto& Shape : SphylElems) {
			if (Shape.bContributeToMass) Total += Shape.GetVolume();
		}

		return Total;
	}

	TArray <FKSphereElem> SphereElems;
	TArray <FKBoxElem> BoxElems;
	TArray <FKSphylElem> SphylElems;
};