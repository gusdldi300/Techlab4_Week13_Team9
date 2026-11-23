#include "EnginePCH.h"
#include "BodySetup.h"
#include "PhysicalMaterial.h"


float UBodySetup::CalculateMass() const
{
	if (PhysMaterial == nullptr) return AggGeom.GetVolume() * 0.001f;

	return AggGeom.GetVolume() * PhysMaterial->GetDensity() * 0.001f;
}
