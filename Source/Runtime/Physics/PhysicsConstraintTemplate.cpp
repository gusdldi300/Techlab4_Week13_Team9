#include "EnginePCH.h"
#include "PhysicsConstraintTemplate.h"

UPhysicsConstraintTemplate::UPhysicsConstraintTemplate()
{
	XMotion = YMotion = ZMotion = EConstraintMotion::Locked;
	Swing1Motion = Swing2Motion = TwistMotion = EConstraintMotion::Limited;
	Swing1LimitDegrees = 45;
	Swing2LimitDegrees = 45;
	TwistLimitDegrees = 15;
	LinearLimit = 0;

}
