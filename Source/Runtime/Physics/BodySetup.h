#pragma once

#include "AggregateGeom.h"
#include "BodySetup.generated.h"

class UPhysicalMaterial;

enum class EPhysicsType : uint8 {
	Default, Kinematic, Simulated
};

UCLASS()
class UBodySetup : public UObject{
	GENERATED_BODY()

public:
	float CalculateMass() const;

public:
	FKAggregateGeom AggGeom;
	UPhysicalMaterial* PhysMaterial = nullptr;
	FName BoneName;	// 이 바디는 어느뼈에 붙을것인가?
	EPhysicsType PhysicsType = EPhysicsType::Default;
};