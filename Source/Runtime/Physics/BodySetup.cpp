#include "EnginePCH.h"
#include "BodySetup.h"
#include "PhysicalMaterial.h"


float UBodySetup::CalculateMass() const
{
	if (PhysMaterial == nullptr) return AggGeom.GetVolume() * 1000.0f;

	return AggGeom.GetVolume() * PhysMaterial->GetDensity() * 1000.0f;
}
