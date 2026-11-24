#include "EnginePCH.h"
#include "ClothInstance.h"
#include "PhysicsCore.h"
#include "PhysicsScene.h"
#include "PhysXConversions.h"
#include <NvCloth/Factory.h>
#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/PhaseConfig.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <vector>

using namespace physx;

FClothInstance::~FClothInstance()
{
	// 정상 경로에서는 컴포넌트의 OnUnregister가 먼저 Term을 불러 둔다. 여기는 남은 것만 정리한다.
	Term();
}

bool FClothInstance::Init(const FClothInstanceInitParams& Params, FPhysicsScene* InScene)
{
	Term();

	nv::cloth::Factory* Factory = FPhysicsCore::Get().GetClothFactory();
	const uint32 NumParticles = static_cast<uint32>(Params.LocalPositions.Num());
	if (!Factory || !InScene || NumParticles == 0 || Params.Indices.Num() < 3 ||
		Params.InvMasses.Num() != Params.LocalPositions.Num())
	{
		KRS_LOG(Warning, "[NvCloth] Init skipped: factory/scene/mesh is invalid");
		return false;
	}

	// 1. 쿠킹: 메시 연결 구조에서 가로·세로·접힘·대각선 구속(Phase)을 만든다.
	nv::cloth::ClothMeshDesc MeshDesc;
	MeshDesc.points.data = Params.LocalPositions.GetData();
	MeshDesc.points.count = NumParticles;
	MeshDesc.points.stride = sizeof(FVector);
	MeshDesc.invMasses.data = Params.InvMasses.GetData();
	MeshDesc.invMasses.count = NumParticles;
	MeshDesc.invMasses.stride = sizeof(float);
	MeshDesc.triangles.data = Params.Indices.GetData();
	MeshDesc.triangles.count = static_cast<uint32>(Params.Indices.Num()) / 3;
	MeshDesc.triangles.stride = sizeof(uint32) * 3;

	// 쿠커는 중력 방향(로컬 공간, 정규화)으로 세로·가로 구속을 나눈다.
	const PxQuat Rotation = U2PQuat(Params.WorldRotation).getNormalized();
	PxVec3 LocalGravityDir = Rotation.rotateInv(U2PVector(Params.Gravity));
	LocalGravityDir = LocalGravityDir.isZero() ? PxVec3(0.0f, 0.0f, -1.0f) : LocalGravityDir.getNormalized();

	nv::cloth::Vector<int32_t>::Type PhaseTypes;
	Fabric = NvClothCookFabricFromMesh(Factory, MeshDesc, LocalGravityDir, &PhaseTypes, false);
	if (!Fabric)
	{
		KRS_LOG(Error, "[NvCloth] cook fabric failed");
		return false;
	}

	// 2. 입자(xyz = 로컬 위치, w = 역질량)로 Cloth를 만든다.
	std::vector<PxVec4> Particles(NumParticles);
	for (uint32 i = 0; i < NumParticles; ++i)
	{
		Particles[i] = PxVec4(U2PVector(Params.LocalPositions[i]), Params.InvMasses[i]);
	}
	nv::cloth::Cloth* Cloth = Factory->createCloth(
		nv::cloth::Range<const PxVec4>(Particles.data(), Particles.data() + Particles.size()), *Fabric);
	if (!Cloth)
	{
		KRS_LOG(Error, "[NvCloth] create cloth failed");
		Fabric->decRefCount();
		Fabric = nullptr;
		return false;
	}

	// 3. Phase 설정. 이게 없으면 구속이 하나도 풀리지 않아 천이 무한히 늘어난다.
	std::vector<nv::cloth::PhaseConfig> Phases(Fabric->getNumPhases());
	for (uint32 i = 0; i < Phases.size(); ++i)
	{
		Phases[i].mPhaseIndex = static_cast<uint16_t>(i);
		const bool bBending = i < PhaseTypes.size() && PhaseTypes[i] == nv::cloth::ClothFabricPhaseType::eBENDING;
		Phases[i].mStiffness = bBending ? Params.BendStiffness : Params.StretchStiffness;
		Phases[i].mStiffnessMultiplier = 1.0f;
		Phases[i].mCompressionLimit = 1.0f;
		Phases[i].mStretchLimit = 1.0f;
	}
	Cloth->setPhaseConfig(nv::cloth::Range<const nv::cloth::PhaseConfig>(Phases.data(), Phases.data() + Phases.size()));

	Cloth->setGravity(U2PVector(Params.Gravity));
	Cloth->setDamping(PxVec3(Params.Damping));
	Cloth->setSolverFrequency(Params.SolverFrequency);
	// 시작 위치로 바로 옮긴다. setTranslation으로 옮기면 원점에서 날아온 것으로 계산해 첫 스텝에 튄다.
	Cloth->teleportToLocation(U2PVector(Params.WorldLocation), Rotation);

	Handle.Cloth = Cloth;
	Scene = InScene;
	Scene->AddCloth(Handle);
	return true;
}

void FClothInstance::Term()
{
	if (Handle.IsValid())
	{
		// Solver는 천을 지우지 않고 가리키기만 하므로, 지우기 전에 반드시 뺀다.
		if (Scene)
		{
			Scene->RemoveCloth(Handle);
		}
		delete Handle.Cloth;
		Handle.Reset();
	}
	if (Fabric)
	{
		Fabric->decRefCount();
		Fabric = nullptr;
	}
	Scene = nullptr;
}

void FClothInstance::SetWorldTransform(const FVector& Location, const FQuat& Rotation)
{
	if (!Handle.IsValid()) return;
	Handle.Cloth->setTranslation(U2PVector(Location));
	Handle.Cloth->setRotation(U2PQuat(Rotation).getNormalized());
}

void FClothInstance::SetWind(const FVector& Velocity, float DragCoefficient, float LiftCoefficient)
{
	if (!Handle.IsValid()) return;
	// setWindVelocity는 월드 좌표 기준이라 천 로컬 공간으로 바꾸지 않고 그대로 넘긴다.
	Handle.Cloth->setWindVelocity(U2PVector(Velocity));
	// NvCloth는 계수가 [0, 1]을 벗어나면 assert를 낸다.
	Handle.Cloth->setDragCoefficient(std::clamp(DragCoefficient, 0.0f, 1.0f));
	Handle.Cloth->setLiftCoefficient(std::clamp(LiftCoefficient, 0.0f, 1.0f));
}

void FClothInstance::GetLocalPositions(TArray<FVector>& OutPositions) const
{
	if (!Handle.IsValid()) return;

	// const 버전은 천을 깨우지 않는다(입자를 고치지 않고 읽기만 하므로).
	const nv::cloth::Cloth* Cloth = Handle.Cloth;
	nv::cloth::MappedRange<const PxVec4> Particles = Cloth->getCurrentParticles();
	const uint32 Count = Particles.size();
	OutPositions.SetNum(static_cast<int32>(Count));
	for (uint32 i = 0; i < Count; ++i)
	{
		OutPositions[i] = FVector(Particles[i].x, Particles[i].y, Particles[i].z);
	}
}
