#pragma once
#include "Physics/PhysicsHandles.h"
#include "Physics/AggregateGeom.h"
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Containers/Set.h"

// UPrimitiveComponent가 FBodyInstance를 값 멤버로 가지므로 여기서 그 헤더를 include하면 순환 include가 된다.
class UPrimitiveComponent;
class UBodySetup;
class FPhysicsScene;

// 바디를 만들 때 필요한 이 컴포넌트만의 정보. 모양 자체는 UBodySetup(설계도)에서 온다.
struct FBodyInstanceInitParams
{
	FVector WorldLocation;
	FQuat   WorldRotation;
	// BodySetup의 모양은 메시 로컬 크기다. 바디를 만들 때 이 스케일을 곱한다.
	FVector Scale3D = FVector(1.0f, 1.0f, 1.0f);
	// true면 시뮬레이션되는 Dynamic, false면 움직이지 않는 Static으로 만든다.
	bool bSimulatePhysics = false;
	// BodySetup이 없을 때만 쓰는 대체 모양(메시 로컬 바운드 박스). BodySetup이 있으면 무시한다.
	FKAggregateGeom FallbackGeom;
};

//BodySetup(설계도)으로 만든 물리 바디 하나의 실행 상태이자, PxRigidActor를 소유하는 래퍼
class FBodyInstance
{
public:
	FBodyInstance() = default;
	~FBodyInstance();
	FBodyInstance(const FBodyInstance&) = delete;
	FBodyInstance& operator=(const FBodyInstance&) = delete;

	// Setup의 모양·재질·질량으로 물리 액터를 만들어 Scene에 넣는다. 이미 바디가 있으면 먼저 정리한다.
	// Setup이 null이면 Params.FallbackGeom으로 만든다. Scene이 null이면(에디터 월드) 아무것도 하지 않는다.
	// Setup은 메시 에셋이 소유하는 공유 데이터다. 바디는 만드는 동안만 읽고 포인터를 보관하지 않는다.
	void InitBody(const UBodySetup* Setup, const FBodyInstanceInitParams& Params, UPrimitiveComponent* InOwnerComponent, FPhysicsScene* Scene);
	// 물리 액터를 release한다. release하면 Scene에서도 자동으로 빠진다.
	void TermBody();

	bool IsValidBodyInstance() const { return ActorHandle.IsValid(); }
	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }

	// 관절(FConstraintInstance)처럼 이 바디의 액터가 필요한 물리 코드가 쓴다.
	const FPhysicsActorHandle& GetActorHandle() const { return ActorHandle; }

	void SyncComponentToBody();
	// COM in world space, read from the live PhysX body after simulation has finished.
	bool GetCenterOfMass(FVector& WorldPosition, float& MassKg) const;
	// Uses the same scaled primitive geometry and mass policy as InitBody; no scene/actor allocation.
	// LocalPosition includes Scale3D already and must only be rotated/translated into world space.
	static bool CalculateCenterOfMass(const UBodySetup& Setup, const FVector& Scale3D, FVector& LocalPosition, float& MassKg);
	// Immutable while the scene simulates. Physics Asset instances fill this before their first step.
	static constexpr uint32 AssetCollisionFilterTag = 0x4B525341;
	void DisableCollisionWith(const FBodyInstance* Other) { DisabledCollisionBodies.Add(Other); }
	bool IsCollisionDisabledWith(const FBodyInstance* Other) const { return DisabledCollisionBodies.Contains(Other); }

private:
	//월드 포즈를 갖고 충돌 Shape를 붙일 수 있는 강체 액터를 가리키는 핸들
	//핸들 자체는 소유권이 없고, 액터의 생성과 해제(release)는 이 FBodyInstance가 책임진다
	FPhysicsActorHandle ActorHandle;
	//이 인스턴스를 소유한 컴포넌트
	UPrimitiveComponent* OwnerComponent = nullptr;
	TSet<const FBodyInstance*> DisabledCollisionBodies;



};
