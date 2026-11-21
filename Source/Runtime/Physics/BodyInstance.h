#pragma once
#include "Physics/PhysicsHandles.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

// UPrimitiveComponent가 FBodyInstance를 값 멤버로 가지므로 여기서 그 헤더를 include하면 순환 include가 된다.
class UPrimitiveComponent;
class FPhysicsScene;

// 바디를 만들 때 필요한 정보. 컴포넌트가 채워서 InitBody에 넘긴다.
struct FBodyInstanceInitParams
{
	FVector WorldLocation;
	FQuat   WorldRotation;
	// 충돌 박스의 반 크기와 중심. 컴포넌트 로컬 바운드에 월드 스케일을 곱한 값이다.
	// 실제 모양(UBodySetup)이 준비되기 전까지는 이 박스 하나로 바디를 만든다.
	FVector BoxHalfExtent;
	FVector BoxCenterOffset;
	// true면 시뮬레이션되는 Dynamic, false면 움직이지 않는 Static으로 만든다.
	bool bSimulatePhysics = false;
};

//BodySetup(설계도)으로 만든 물리 바디 하나의 실행 상태이자, PxRigidActor를 소유하는 래퍼
class FBodyInstance
{
public:
	FBodyInstance() = default;
	~FBodyInstance();
	FBodyInstance(const FBodyInstance&) = delete;
	FBodyInstance& operator=(const FBodyInstance&) = delete;

	// 물리 액터를 만들어 Scene에 넣는다. 이미 바디가 있으면 먼저 정리한다.
	// Scene이 null이면(에디터 월드) 아무것도 하지 않는다.
	void InitBody(const FBodyInstanceInitParams& Params, UPrimitiveComponent* InOwnerComponent, FPhysicsScene* Scene);
	// 물리 액터를 release한다. release하면 Scene에서도 자동으로 빠진다.
	void TermBody();

	bool IsValidBodyInstance() const { return ActorHandle.IsValid(); }
	UPrimitiveComponent* GetOwnerComponent() const { return OwnerComponent; }

	// 관절(FConstraintInstance)처럼 이 바디의 액터가 필요한 물리 코드가 쓴다.
	const FPhysicsActorHandle& GetActorHandle() const { return ActorHandle; }

	void SyncComponentToBody();

private:
	//월드 포즈를 갖고 충돌 Shape를 붙일 수 있는 강체 액터를 가리키는 핸들
	//핸들 자체는 소유권이 없고, 액터의 생성과 해제(release)는 이 FBodyInstance가 책임진다
	FPhysicsActorHandle ActorHandle;
	//이 인스턴스를 소유한 컴포넌트
	UPrimitiveComponent* OwnerComponent = nullptr;



};
