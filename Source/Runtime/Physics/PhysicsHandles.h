#pragma once

namespace physx
{
	class PxRigidActor;
	class PxShape;
	class PxJoint;
}

// 물리 엔진 객체를 가리키는 핸들 모음이다.
// 핸들은 포인터 하나 크기의 값 타입이고 소유권이 없다. 생성과 해제는 핸들을 들고 있는
// 엔진 객체(FBodyInstance, FConstraintInstance 등)가 책임진다.
// 엔진 코드는 PhysX 타입 대신 이 핸들을 주고받고, 물리 엔진을 바꿀 때는 이 파일의 정의만 교체한다.

// 강체 액터(PxRigidStatic / PxRigidDynamic) 하나를 가리킨다.
struct FPhysicsActorHandle
{
	physx::PxRigidActor* Actor = nullptr;

	bool IsValid() const { return Actor != nullptr; }
	void Reset() { Actor = nullptr; }

	bool operator==(const FPhysicsActorHandle& Other) const { return Actor == Other.Actor; }
	bool operator!=(const FPhysicsActorHandle& Other) const { return Actor != Other.Actor; }
};

// 액터에 붙은 충돌 Shape 하나를 가리킨다.
struct FPhysicsShapeHandle
{
	physx::PxShape* Shape = nullptr;

	bool IsValid() const { return Shape != nullptr; }
	void Reset() { Shape = nullptr; }

	bool operator==(const FPhysicsShapeHandle& Other) const { return Shape == Other.Shape; }
	bool operator!=(const FPhysicsShapeHandle& Other) const { return Shape != Other.Shape; }
};

// 두 액터를 잇는 관절 하나를 가리킨다. D6 Joint도 PxJoint로 들고 있다.
struct FPhysicsConstraintHandle
{
	physx::PxJoint* Joint = nullptr;

	bool IsValid() const { return Joint != nullptr; }
	void Reset() { Joint = nullptr; }

	bool operator==(const FPhysicsConstraintHandle& Other) const { return Joint == Other.Joint; }
	bool operator!=(const FPhysicsConstraintHandle& Other) const { return Joint != Other.Joint; }
};
