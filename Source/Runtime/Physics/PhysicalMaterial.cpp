#include "EnginePCH.h"
#include "PhysicalMaterial.h"

void UPhysicalMaterial::Serialize(FArchive& Ar)
{
	Ar << Friction << Restitution << Density;
}
