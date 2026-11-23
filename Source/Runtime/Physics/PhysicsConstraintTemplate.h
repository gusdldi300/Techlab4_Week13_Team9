#pragma once

#include "UObject/Object.h"
#include "Math/Vector.h"
#include "Core/NameTypes.h"

#include "PhysicsConstraintTemplate.generated.h"

enum class EConstraintMotion : uint8 {
	Free, Limited, Locked
};

UCLASS()
class UPhysicsConstraintTemplate : public UObject{
	GENERATED_BODY()

public:
	  UPhysicsConstraintTemplate();

public:
	FName ConstraintBone1;				// ConstraintBone1 : 자식뼈
	FName ConstraintBone2;				// ConstraintBone2 : 부모뼈
	FVector Pos1, PriAxis1, SecAxis1;	// PriAxis: 주축(Twist축), SecAxis(보조축/Swing방향)
	FVector Pos2, PriAxis2, SecAxis2;	// SecAxis는 PriAxis 에 수직!

	EConstraintMotion XMotion, YMotion, ZMotion;
	float LinearLimit;
	EConstraintMotion Swing1Motion, Swing2Motion;	// Swing : 꺾기(축 기울어짐) 
	float Swing1LimitDegrees, Swing2LimitDegrees;
	EConstraintMotion TwistMotion;					// Twist : 비틀기(축중심 빙글)
	float TwistLimitDegrees;

};

