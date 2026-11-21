#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversions.h"
#include <algorithm>
#include "Components/PrimitiveComponent.h"

using namespace physx;

namespace
{
	// 반 크기가 0인 박스는 PhysX가 invalid geometry로 거부한다. 평면 메시도 얇은 박스가 되도록 최소값을 둔다.
	constexpr float MinHalfExtent = 0.01f;

	// 바디 질량은 부피 × 밀도로 계산한다. 1000kg/m³은 물의 밀도다.
	constexpr float DefaultDensity = 1000.0f;
}

FBodyInstance::~FBodyInstance()
{
	TermBody();
}

void FBodyInstance::InitBody(const FBodyInstanceInitParams& Params, UPrimitiveComponent* InOwnerComponent, FPhysicsScene* Scene)
{
	// 다시 만드는 경우 이전 액터가 남지 않도록 먼저 정리한다.
	TermBody();

	if (!Scene)
	{
		return;
	}

	PxPhysics*  Physics  = FPhysicsCore::Get().GetPhysics();
	PxMaterial* Material = FPhysicsCore::Get().GetDefaultMaterial();
	if (!Physics || !Material)
	{
		return;
	}

	const PxTransform Pose = U2PTransform(Params.WorldLocation, Params.WorldRotation);

	PxRigidActor* Actor = nullptr;
	if (Params.bSimulatePhysics)
	{
		Actor = Physics->createRigidDynamic(Pose);
	}
	else
	{
		Actor = Physics->createRigidStatic(Pose);
	}

	if (!Actor)
	{
		KRS_LOG(Error, "[Physics] Failed to create rigid actor");
		return;
	}

	const PxVec3 HalfExtent(
		std::max(Params.BoxHalfExtent.X, MinHalfExtent),
		std::max(Params.BoxHalfExtent.Y, MinHalfExtent),
		std::max(Params.BoxHalfExtent.Z, MinHalfExtent));

	// 이 액터만 쓰는 Shape를 만들어 붙인다. 메시의 중심이 원점이 아니면 그만큼 Shape를 옮긴다.
	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Actor, PxBoxGeometry(HalfExtent), *Material);
	if (!Shape)
	{
		KRS_LOG(Error, "[Physics] Failed to create box shape");
		Actor->release();
		return;
	}
	Shape->setLocalPose(PxTransform(U2PVector(Params.BoxCenterOffset)));

	if (PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>())
	{
		// 부르지 않으면 질량 1, 관성 기본값이 들어가 큰 상자와 작은 상자가 똑같이 움직인다.
		PxRigidBodyExt::updateMassAndInertia(*Dynamic, DefaultDensity);
	}

	// 충돌 콜백 등에서 PhysX 액터로부터 엔진 바디를 찾는 통로
	Actor->userData = this;

	ActorHandle.Actor = Actor;
	OwnerComponent = InOwnerComponent;

	Scene->AddActor(ActorHandle);
}

void FBodyInstance::TermBody()
{
	if (ActorHandle.IsValid())
	{
		// Scene에 들어 있으면 release가 Scene에서도 빼 준다. 붙어 있던 Shape도 함께 해제된다.
		ActorHandle.Actor->userData = nullptr;
		ActorHandle.Actor->release();
		ActorHandle.Reset();
	}
	OwnerComponent = nullptr;
}

void FBodyInstance::SyncComponentToBody()
{
	if (!ActorHandle.IsValid() || !OwnerComponent) return;

	const PxTransform Pose = ActorHandle.Actor->getGlobalPose();

	OwnerComponent->SetWorldLocationAndRotation(P2UVector(Pose.p), P2UQuat(Pose.q).ToFRotator(), false);


}
