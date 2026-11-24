#include "EnginePCH.h"
#include "PhysicsScene_PhysX.h"
#include <PxPhysicsAPI.h>
#include "PhysicsCore.h"
#include "PhysicsDebugDraw.h"
#include "BodyInstance.h"
#include "Physics/PhysicsStats.h"
#include <NvCloth/Factory.h> 
#include <NvCloth/Solver.h>

using namespace physx;

namespace
{
    PxFilterFlags AssetFilterShader(PxFilterObjectAttributes A, PxFilterData FA,
        PxFilterObjectAttributes B, PxFilterData FB, PxPairFlags& Pairs, const void* Constants, PxU32 Size)
    {
        const bool TaggedA = FA.word3 == FBodyInstance::AssetCollisionFilterTag;
        const bool TaggedB = FB.word3 == FBodyInstance::AssetCollisionFilterTag;
        if (!TaggedA && !TaggedB) return PxDefaultSimulationFilterShader(A,FA,B,FB,Pairs,Constants,Size);
        if (PxFilterObjectIsTrigger(A) || PxFilterObjectIsTrigger(B))
        {
            Pairs = PxPairFlag::eTRIGGER_DEFAULT;
            return PxFilterFlag::eDEFAULT;
        }
        Pairs = PxPairFlag::eCONTACT_DEFAULT;
        return TaggedA && TaggedB && FA.word0 == FB.word0 ? PxFilterFlag::eCALLBACK : PxFilterFlag::eDEFAULT;
    }

    // The callback can read engine objects; the PhysX filter shader cannot dereference external data.
    class FAssetCollisionFilter final : public PxSimulationFilterCallback
    {
    public:
        PxFilterFlags pairFound(PxU32, PxFilterObjectAttributes, PxFilterData,
            const PxActor* A, const PxShape*, PxFilterObjectAttributes, PxFilterData,
            const PxActor* B, const PxShape*, PxPairFlags&) override
        {
            const auto* BodyA = static_cast<const FBodyInstance*>(A->userData);
            const auto* BodyB = static_cast<const FBodyInstance*>(B->userData);
            return BodyA && BodyB && BodyA->IsCollisionDisabledWith(BodyB)
                ? PxFilterFlag::eSUPPRESS : PxFilterFlag::eDEFAULT;
        }
        void pairLost(PxU32, PxFilterObjectAttributes, PxFilterData, PxFilterObjectAttributes, PxFilterData, bool) override {}
        bool statusChange(PxU32&, PxPairFlags&, PxFilterFlags&) override { return false; }
    };
    FAssetCollisionFilter AssetCollisionFilter;
}

std::unique_ptr<FPhysicsScene> MakePhysicsScene()
{
    return std::make_unique<FPhysicsScene_PhysX>();
}

FPhysicsScene_PhysX::~FPhysicsScene_PhysX()
{
    // Solver는 천을 참조만 하고 지우지 않는다. 천 컴포넌트가 RemoveCloth로 먼저 빠져 있어야 한다.
    delete ClothSolver;
    ClothSolver = nullptr;

    // createScene이 실패했거나 호출되지 않았으면 null이다.
    if (PhysXScene)
    {
        // 시뮬레이션 도중(simulate 후 fetchResults 전)에는 release할 수 없으므로 먼저 끝낸다.
        if (bSimulating)
        {
            PhysXScene->fetchResults(true);
            bSimulating = false;
        }
        // 관리자가 가진 배치 쿼리는 이 Scene이 만든 객체이므로 Scene보다 먼저 release한다.
        VehicleManager.reset();
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
    Desc.filterShader = AssetFilterShader;
    Desc.filterCallback = &AssetCollisionFilter;

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

    if (FPhysicsCore::Get().IsVehicleSDKInitialized())
    {
        VehicleManager = std::make_unique<FPhysXVehicleManager>(PhysXScene);
    }

    KRS_LOG(Info, "[Physics] Scene created");

    // Solver를 못 만들어도 강체 물리는 그대로 둔다. 이 Scene에서 천만 동작하지 않는다.
    if (nv::cloth::Factory* ClothFactory = FPhysicsCore::Get().GetClothFactory())
    {
        ClothSolver = ClothFactory->createSolver();
    }
    if (!ClothSolver)
    {
        KRS_LOG(Warning, "[NvCloth] createSolver failed. Cloth is disabled in this scene.");
    }

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

bool FPhysicsScene_PhysX::SetGravity(const FVector& Gravity)
{
    const PxVec3 Value(Gravity.X,Gravity.Y,Gravity.Z);
    if (!PhysXScene || bSimulating || !Value.isFinite()) return false;
    PxSceneWriteLock Lock(*PhysXScene);
    PhysXScene->setGravity(Value);
    return true;
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
        // 차량은 이번 스텝에 차체가 받을 속도 변화를 simulate 전에 계산해야 한다.
        // simulate 뒤로 미루면 서스펜션 힘이 한 스텝 늦게 들어가 스프링을 엇박자로 밀게 되고, 차가 점점 크게 출렁이다 튄다.
        if (VehicleManager)
        {
            VehicleManager->Update(FixedStep);
        }
        PhysXScene->simulate(FixedStep);
    }
    bSimulating = true;

    // simulate는 바로 돌아오므로 PhysX 워커가 강체를 계산하는 동안 게임 스레드가 천을 계산한다.
    StepCloth();
}

void FPhysicsScene_PhysX::StepCloth()
{
    if (!ClothSolver || ClothSolver->getNumCloths() == 0) return;

    // begin~end 사이에는 천을 넣거나 뺄 수 없다. 이 함수 안에서 한 번에 끝내므로 컴포넌트 코드가 끼어들지 않는다.
    ClothSolver->beginSimulation(FixedStep);
    for (int i = 0; i < ClothSolver->getSimulationChunkCount(); ++i)
    {
        ClothSolver->simulateChunk(i);
    }
    ClothSolver->endSimulation();
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
            if (!Body->GetOwnerComponent()) continue; // Bone actors are applied as a skeletal pose, not a component transform.
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

void FPhysicsScene_PhysX::DrawDebugBodies(FLineBatcher* Lines) const
{
    if (!Lines || !PhysXScene || bSimulating) return;
    PxSceneReadLock Lock(*PhysXScene);
    const PxActorTypeFlags Types = PxActorTypeFlag::eRIGID_STATIC | PxActorTypeFlag::eRIGID_DYNAMIC;
    PxActor* Actors[64];
    const PxU32 Count = PhysXScene->getNbActors(Types);
    for (PxU32 Offset = 0; Offset < Count;)
    {
        const PxU32 Read = PhysXScene->getActors(Types, Actors, 64, Offset);
        if (!Read) break;
        for (PxU32 I = 0; I < Read; ++I)
        {
            FPhysicsActorHandle Handle;
            Handle.Actor = Actors[I]->is<PxRigidActor>();
            FPhysicsDebugDraw::DrawActorShapes(Lines, Handle, FVector4(0.3f, 0.9f, 1.0f, 1.0f));
        }
        Offset += Read;
    }
}

void FPhysicsScene_PhysX::AddCloth(const FClothHandle& Handle)
{
    if (ClothSolver && Handle.IsValid())
    {
        ClothSolver->addCloth(Handle.Cloth);
    }
}

void FPhysicsScene_PhysX::RemoveCloth(const FClothHandle& Handle)
{
    if (ClothSolver && Handle.IsValid())
    {
        ClothSolver->removeCloth(Handle.Cloth);
    }
}
