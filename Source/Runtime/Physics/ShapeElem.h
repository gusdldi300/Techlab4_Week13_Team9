#pragma once

#include "Core/Types.h"
#include "Core/NameTypes.h"
#include "Math/EngineMath.h"
#include "Math/Transform.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

enum class ECollisionShape : uint8{
	Sphere, Box, Sphyl, Unknown
};

struct FKShapeElem {
	FKShapeElem(){}
	FKShapeElem(ECollisionShape InShapeType) : ShapeType(InShapeType){}

	FName Name;
	ECollisionShape ShapeType = ECollisionShape::Unknown;
	bool bContributeToMass = true;
};

struct FKSphereElem : public FKShapeElem {
	FKSphereElem():FKShapeElem(ECollisionShape::Sphere){}

	FVector Center = FVector(0.0f, 0.0f, 0.0f);
	float Radius = 1.0f;

	float GetVolume() const { return 1.3333f * PI * Radius * Radius * Radius; }
	FTransform GetTransform() const { return FTransform(FRotator(0.0f, 0.0f, 0.0f), Center); }
};


struct FKBoxElem : public FKShapeElem {
	FKBoxElem() :FKShapeElem(ECollisionShape::Box) {}

	FVector Center = FVector(0.0f, 0.0f, 0.0f);
	FRotator Rotation = FRotator(0.0f, 0.0f,0.0f);
	float X = 1.0f, Y = 1.0f, Z = 1.0f;

	float GetVolume() const { return X * Y * Z; }
	FTransform GetTransform() const { return FTransform(Rotation, Center); }
};

struct FKSphylElem : public FKShapeElem {
	FKSphylElem() :FKShapeElem(ECollisionShape::Sphyl) {}

	FVector Center = FVector(0.0f, 0.0f, 0.0f);
	FRotator Rotation = FRotator(0.0f, 0.0f, 0.0f);
	float Radius = 1.0f;
	float Length = 1.0f;	// 두 반구 중심 사이 선분 길이

	float GetVolume() const { return (1.3333f * Radius + Length) * PI * Radius * Radius; }
	FTransform GetTransform() const { return FTransform(Rotation, Center); }
};