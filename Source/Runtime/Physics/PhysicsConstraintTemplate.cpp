#include "EnginePCH.h"
#include "PhysicsConstraintTemplate.h"

UPhysicsConstraintTemplate::UPhysicsConstraintTemplate()
{
    Pos1 = Pos2 = FVector::ZeroVector;
    PriAxis1 = PriAxis2 = FVector(1, 0, 0);
    SecAxis1 = SecAxis2 = FVector(0, 1, 0);
	XMotion = YMotion = ZMotion = EConstraintMotion::Locked;
	Swing1Motion = Swing2Motion = TwistMotion = EConstraintMotion::Limited;
	Swing1LimitDegrees = 45;
	Swing2LimitDegrees = 45;
	TwistLimitDegrees = 15;
	LinearLimit = 0;

}
