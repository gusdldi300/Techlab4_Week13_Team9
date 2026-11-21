#include "EnginePCH.h"
#include "PhysicsScene_PhysX.h"
#include <PxPhysicsAPI.h>
#include "PhysicsCore.h"
#include "BodyInstance.h"
using namespace physx;

std::unique_ptr<FPhysicsScene> MakePhysicsScene()
{
    return std::make_unique<FPhysicsScene_PhysX>();
}

FPhysicsScene_PhysX::~FPhysicsScene_PhysX()
{
    // createScene이 실패했거나 호출되지 않았으면 null이다.
    if (PhysXScene)
    {
        // 시뮬레이션 도중(simulate 후 fetchResults 전)에는 release할 수 없으므로 먼저 끝낸다.
        if (bSimulating)
        {
            PhysXScene->fetchResults(true);
            bSimulating = false;
        }
        PhysXScene->release();
        PhysXScene = nullptr;
    }

}

void FPhysicsScene_PhysX::CreatePhysicsScene()
{
	PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();

    PxSceneDesc Desc(Physics->getTolerancesScale());          
    Desc.gravity = PxVec3(0.0f, 0.0f, -9.81f);           
    Desc.cpuDispatcher = FPhysicsCore::Get().GetDispatcher();  
    Desc.filterShader = PxDefaultSimulationFilterShader;

    Desc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;

    PhysXScene = Physics->createScene(Desc);
    if (!PhysXScene)
    {
        KRS_LOG(Error, "[Physics] createScene failed");
        return;
    }

    //Scene 하나가 PVD에 무엇을 보낼지 고르는 설정
    if (PxPvdSceneClient* Pvd = PhysXScene->getScenePvdClient())  // 5
    {
        //관절의 위치·축(로컬 프레임), Twist·Swing 제한 범위
        Pvd->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
        //매 프레임의 접촉점과 접촉 법선
        Pvd->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
        //Raycast·Sweep·Overlap의 광선·도형과 맞은 결과
        Pvd->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
    }

    KRS_LOG(Info, "[Physics] Scene created");

}

void FPhysicsScene_PhysX::StartFrame(float DeltaTime)
{
    if (!PhysXScene || bSimulating) return;

    // 프레임이 크게 튀어도(디버거 정지 등) 한 번에 너무 많이 몰아서 돌리지 않는다
    Accumulator = std::min(Accumulator + DeltaTime, FixedStep * 4.0f);
    if (Accumulator < FixedStep) return;

    Accumulator -= FixedStep;
    PhysXScene->simulate(FixedStep);
    bSimulating = true;
}

void FPhysicsScene_PhysX::EndFrame()
{
    if (!bSimulating) return;

    PhysXScene->fetchResults(true);
    bSimulating = false;
    PxU32 Count = 0;
    PxActor** Actors = PhysXScene->getActiveActors(Count);
    for (PxU32 i = 0; i < Count; ++i)
    {
        if (FBodyInstance* Body = static_cast<FBodyInstance*>(Actors[i]->userData))
        {
            Body->SyncComponentToBody();
        }
    }

}

void FPhysicsScene_PhysX::AddActor(const FPhysicsActorHandle& Handle)
{
    if (!PhysXScene || !Handle.IsValid())
    {
        return;
    }

    // simulate와 fetchResults 사이에는 Scene을 수정할 수 없다.
    // 지금은 StartFrame과 EndFrame을 연달아 부르므로 이 경우가 생기지 않는다.
    if (bSimulating)
    {
        KRS_LOG(Warning, "[Physics] AddActor called during simulation. Ignored.");
        return;
    }

    PhysXScene->addActor(*Handle.Actor);
}
