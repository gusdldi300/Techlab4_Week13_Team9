#pragma once

#include "UObject/Object.h"
#include "PhysicalMaterial.generated.h"


UCLASS()
class UPhysicalMaterial : public UObject{
	GENERATED_BODY()
public:
	using Super::Serialize;
	void Serialize(FArchive& Ar) override;

	float GetFriction() const { return Friction; }
	void SetFriction(float InFriction) { Friction = std::max(InFriction, 0.0f); }

	float GetRestitution() const { return Restitution; }
	void SetRestitution(float InRestitution) { Restitution = std::clamp(InRestitution, 0.0f, 1.0f); }

	float GetDensity() const { return Density; }
	void SetDensity(float InDensity) { Density = std::max(InDensity, 0.001f); }

private:
	float Friction = 0.7f;
	float Restitution = 0.3f;
	float Density = 1.0f;
};