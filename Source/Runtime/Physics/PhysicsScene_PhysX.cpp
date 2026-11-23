#include "EnginePCH.h"
#include "PhysicsScene_PhysX.h"
#include <PxPhysicsAPI.h>
#include "PhysicsCore.h"
#include "BodyInstance.h"
#include "Physics/PhysicsStats.h"
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

    // 같은 Stat은 한 프레임 안에서 합산되므로 StartFrame과 EndFrame의 합이 Total이 된다.
    SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysicsTotal);
    Stats.FixedStep = FixedStep;
    Stats.NumStepsLastFrame = 0;

    // 프레임이 크게 튀어도(디버거 정지 등) 한 번에 너무 많이 몰아서 돌리지 않는다
    Accumulator = std::min(Accumulator + DeltaTime, FixedStep * MaxStepsPerFrame);
    if (Accumulator < FixedStep) return;

    // 밀린 시간이 두 스텝 이상이면(60fps 미만) 마지막 하나만 남기고 여기서 바로 끝까지 돌린다.
    // 이렇게 해야 30fps에서도 한 프레임에 2스텝을 돌아 물리가 실제 시간과 같은 속도로 움직인다.
    while (Accumulator >= FixedStep * 2.0f)
    {
        Accumulator -= FixedStep;
        BeginStep();
        FinishStep();
    }

    // 마지막 스텝은 시작만 해 두고, 끝내는 일은 EndFrame이 맡는다.
    Accumulator -= FixedStep;
    BeginStep();
}

void FPhysicsScene_PhysX::EndFrame()
{
    if (!bSimulating) return;

    SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysicsTotal);
    FinishStep();
}

void FPhysicsScene_PhysX::BeginStep()
{
    {
        // Scene을 바꾸는 호출은 쓰기 잠금 안에서 한다. 쓰기는 혼자만 할 수 있다.
        SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysicsSimulate);
        PxSceneWriteLock WriteLock(*PhysXScene);
        PhysXScene->simulate(FixedStep);
    }
    bSimulating = true;
}

void FPhysicsScene_PhysX::FinishStep()
{
    {
        // fetchResults는 액터 포즈와 질의용 공간 구조를 고쳐 쓰므로 쓰기 잠금이 필요하다.
        // 이 동안 다른 스레드의 읽기(레이캐스트, 포즈 읽기)는 끝날 때까지 기다린다.
        SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysicsFetchResults);
        PxSceneWriteLock WriteLock(*PhysXScene);
        PhysXScene->fetchResults(true);
    }
    bSimulating = false;
    ++Stats.NumSteps;
    ++Stats.NumStepsLastFrame;

    // 결과 포즈를 읽어 컴포넌트에 반영한다. 읽기끼리는 다른 스레드와 동시에 할 수 있다.
    // 주의: 이 잠금 안에서 Scene을 바꾸는 호출(쓰기)을 하면 같은 스레드에서 교착이 생긴다.
    //       SyncComponentToBody가 부르는 컴포넌트 코드에서 바디 포즈를 다시 쓰지 않도록 한다.
    PxSceneReadLock ReadLock(*PhysXScene);

    // 스텝이 끝난 직후의 통계를 엔진 타입으로 옮겨 둔다. 에디터의 stat physics는 이 복사본만 읽는다.
    PxSimulationStatistics SimStats;
    PhysXScene->getSimulationStatistics(SimStats);
    Stats.NumStaticBodies        = SimStats.nbStaticBodies;
    Stats.NumDynamicBodies       = SimStats.nbDynamicBodies;
    Stats.NumKinematicBodies     = SimStats.nbKinematicBodies;
    Stats.NumActiveDynamicBodies = SimStats.nbActiveDynamicBodies;
    Stats.NumContactPairs        = SimStats.nbDiscreteContactPairsWithContacts;
    Stats.NumNewTouches          = SimStats.nbNewTouches;
    Stats.NumLostTouches         = SimStats.nbLostTouches;
    Stats.NumActiveConstraints   = SimStats.nbActiveConstraints;

    SCOPE_CYCLE_COUNTER(PhysicsStats::STAT_PhysicsSyncComponents);
    PxU32 Count = 0;
    PxActor** Actors = PhysXScene->getActiveActors(Count);
    uint32 NumSynced = 0;
    for (PxU32 i = 0; i < Count; ++i)
    {
        if (FBodyInstance* Body = static_cast<FBodyInstance*>(Actors[i]->userData))
        {
            Body->SyncComponentToBody();
            ++NumSynced;
        }
    }
    Stats.NumSyncedComponents = NumSynced;

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

    PxSceneWriteLock WriteLock(*PhysXScene);
    PhysXScene->addActor(*Handle.Actor);
}
