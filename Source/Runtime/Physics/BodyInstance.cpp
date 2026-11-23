#include "EnginePCH.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversions.h"
#include <algorithm>
#include <cmath>
#include "Components/PrimitiveComponent.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicalMaterial.h"

using namespace physx;

namespace
{
	// 크기가 0인 도형은 PhysX가 invalid geometry로 거부한다. 평면 메시도 얇은 도형이 되도록 최소값을 둔다.
	constexpr float MinExtent = 0.01f;

	// BodySetup이 없거나 질량을 못 구했을 때 쓰는 밀도(kg/m³). 1000은 물의 밀도이고 UBodySetup::CalculateMass의 기준과 같다.
	constexpr float DefaultDensity = 1000.0f;

	FVector AbsVector(const FVector& V)
	{
		return FVector(std::abs(V.X), std::abs(V.Y), std::abs(V.Z));
	}

	FVector MulVector(const FVector& A, const FVector& B)
	{
		return FVector(A.X * B.X, A.Y * B.Y, A.Z * B.Z);
	}

	// 도형 하나를 액터에 붙인다. 위치(Center)는 스케일을 곱해 옮기고, 회전은 그대로 쓴다.
	bool AttachShape(PxRigidActor& Actor, const PxGeometry& Geometry, PxMaterial& Material, const FVector& LocalCenter, const PxQuat& LocalRotation)
	{
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(Actor, Geometry, Material);
		if (!Shape)
		{
			return false;
		}
		Shape->setLocalPose(PxTransform(U2PVector(LocalCenter), LocalRotation.getNormalized()));
		return true;
	}

	// AggregateGeom의 도형을 모두 PhysX Shape로 만들어 붙이고, 붙인 개수를 돌려준다.
	// 반 크기 변환과 캡슐 축 변환은 엔진 데이터와 PhysX 규약이 다른 부분이라 이 함수 한 곳에서만 한다.
	int32 CreateShapes(PxRigidActor& Actor, const FKAggregateGeom& Geom, PxMaterial& Material, const FVector& Scale3D)
	{
		const FVector Scale = AbsVector(Scale3D);
		int32 NumCreated = 0;

		// 구는 비균일 스케일을 표현할 수 없다. 언리얼처럼 가장 작은 축 스케일을 쓴다.
		const float SphereScale = std::min({ Scale.X, Scale.Y, Scale.Z });
		for (const FKSphereElem& Elem : Geom.SphereElems)
		{
			const PxSphereGeometry Sphere(std::max(Elem.Radius * SphereScale, MinExtent));
			NumCreated += AttachShape(Actor, Sphere, Material, MulVector(Elem.Center, Scale3D), PxQuat(PxIdentity)) ? 1 : 0;
		}

		for (const FKBoxElem& Elem : Geom.BoxElems)
		{
			// FKBoxElem의 X·Y·Z는 전체 크기이고, PxBoxGeometry는 반 크기를 받는다.
			const PxBoxGeometry Box(
				std::max(Elem.X * Scale.X * 0.5f, MinExtent),
				std::max(Elem.Y * Scale.Y * 0.5f, MinExtent),
				std::max(Elem.Z * Scale.Z * 0.5f, MinExtent));
			NumCreated += AttachShape(Actor, Box, Material, MulVector(Elem.Center, Scale3D), U2PQuat(Elem.Rotation.Quaternion())) ? 1 : 0;
		}

		// 엔진 캡슐(FKSphylElem)은 Z축으로 서 있고, PhysX 캡슐은 X축으로 누워 있다.
		// Y축으로 -90도 돌리면 PhysX의 X축이 Z축을 향한다. 이 회전을 엔진 회전 뒤에 곱한다.
		const PxQuat CapsuleXToZ(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f));
		const float CapsuleRadiusScale = std::min(Scale.X, Scale.Y);
		for (const FKSphylElem& Elem : Geom.SphylElems)
		{
			// Length는 두 반구 중심 사이 거리이고, PxCapsuleGeometry는 그 절반(halfHeight)을 받는다.
			const PxCapsuleGeometry Capsule(
				std::max(Elem.Radius * CapsuleRadiusScale, MinExtent),
				std::max(Elem.Length * Scale.Z * 0.5f, MinExtent));
			const PxQuat Rotation = U2PQuat(Elem.Rotation.Quaternion()) * CapsuleXToZ;
			NumCreated += AttachShape(Actor, Capsule, Material, MulVector(Elem.Center, Scale3D), Rotation) ? 1 : 0;
		}

		return NumCreated;
	}
}

FBodyInstance::~FBodyInstance()
{
	TermBody();
}

void FBodyInstance::InitBody(const UBodySetup* Setup, const FBodyInstanceInitParams& Params, UPrimitiveComponent* InOwnerComponent, FPhysicsScene* Scene)
{
	// 다시 만드는 경우 이전 액터가 남지 않도록 먼저 정리한다.
	TermBody();

	if (!Scene)
	{
		return;
	}

	PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	if (!Physics)
	{
		return;
	}

	// 모양은 BodySetup(설계도)에서 가져오고, 없으면 컴포넌트가 준 대체 모양을 쓴다.
	const FKAggregateGeom& Geom = Setup ? Setup->AggGeom : Params.FallbackGeom;
	if (Geom.GetElementCount() == 0)
	{
		KRS_LOG(Warning, "[Physics] Body has no collision shapes. Skipped");
		return;
	}

	// 엔진 PhysicalMaterial이 있으면 그 값으로 PxMaterial을 만들고, 없으면 기본 재질을 빌려 쓴다.
	// 만든 PxMaterial은 Shape가 참조 카운트를 잡으므로 Shape를 붙인 뒤 바로 release한다. 액터가 사라지면 함께 해제된다.
	const UPhysicalMaterial* EngineMaterial = Setup ? Setup->PhysMaterial : nullptr;
	PxMaterial* Material = nullptr;
	bool bOwnsMaterial = false;
	if (EngineMaterial)
	{
		Material = Physics->createMaterial(EngineMaterial->GetFriction(), EngineMaterial->GetFriction(), EngineMaterial->GetRestitution());
		bOwnsMaterial = (Material != nullptr);
	}
	if (!Material)
	{
		Material = FPhysicsCore::Get().GetDefaultMaterial();
	}
	if (!Material)
	{
		return;
	}

	// BodySetup의 PhysicsType이 Default가 아니면 컴포넌트 설정보다 우선한다.
	const EPhysicsType PhysicsType = Setup ? Setup->PhysicsType : EPhysicsType::Default;
	const bool bDynamic = Params.bSimulatePhysics || PhysicsType != EPhysicsType::Default;

	const PxTransform Pose = U2PTransform(Params.WorldLocation, Params.WorldRotation);
	PxRigidActor* Actor = bDynamic
		? static_cast<PxRigidActor*>(Physics->createRigidDynamic(Pose))
		: static_cast<PxRigidActor*>(Physics->createRigidStatic(Pose));

	if (!Actor)
	{
		KRS_LOG(Error, "[Physics] Failed to create rigid actor");
		if (bOwnsMaterial) Material->release();
		return;
	}

	const int32 NumShapes = CreateShapes(*Actor, Geom, *Material, Params.Scale3D);
	if (bOwnsMaterial)
	{
		Material->release();
	}
	if (NumShapes == 0)
	{
		KRS_LOG(Error, "[Physics] Failed to create collision shapes");
		Actor->release();
		return;
	}

	if (PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>())
	{
		if (PhysicsType == EPhysicsType::Kinematic)
		{
			// 키네마틱은 시뮬레이션이 움직이지 않고 코드가 정한 위치로만 움직인다. 부딪힌 상대는 밀어낸다.
			Dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
		}

		// 질량은 BodySetup이 정하고(엔진 데이터), PhysX는 그 질량과 Shape 모양으로 관성만 계산한다.
		// BodySetup의 부피는 메시 로컬 크기 기준이라 스케일만큼 부피가 늘어난 비율을 곱한다.
		const float LocalMass = Setup ? Setup->CalculateMass() : Geom.GetVolume() * DefaultDensity;
		const float ScaleVolume = std::abs(Params.Scale3D.X * Params.Scale3D.Y * Params.Scale3D.Z);
		const float Mass = LocalMass * ScaleVolume;
		if (Mass > 0.0f)
		{
			PxRigidBodyExt::setMassAndUpdateInertia(*Dynamic, Mass);
		}
		else
		{
			// 질량에 기여하는 도형이 없으면(bContributeToMass가 모두 false) 0이 된다. 질량 0은 PhysX가 무한 질량으로 다루므로 밀도로 대신 구한다.
			PxRigidBodyExt::updateMassAndInertia(*Dynamic, DefaultDensity);
		}
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
