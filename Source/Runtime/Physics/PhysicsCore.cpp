#include "EnginePCH.h"
#include "PhysicsCore.h"
#include <PxPhysicsAPI.h>
#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>

// 헤더에서 선언한 static 멤버의 정의. 없으면 exe를 링크할 때 LNK2001이 난다.
physx::PxFoundation*            FPhysicsCore::gFoundation = nullptr;
physx::PxDefaultAllocator       FPhysicsCore::gAllocator;
physx::PxDefaultErrorCallback   FPhysicsCore::gErrorCallback;
physx::PxPvd*                   FPhysicsCore::gPvd = nullptr;
physx::PxPvdTransport*          FPhysicsCore::gTransport = nullptr;
physx::PxPhysics*               FPhysicsCore::gPhysics = nullptr;
physx::PxDefaultCpuDispatcher*  FPhysicsCore::gDispatcher = nullptr;
physx::PxMaterial*              FPhysicsCore::gDefaultMaterial = nullptr;
nv::cloth::Factory* FPhysicsCore::gClothFactory = nullptr;

namespace
{
    // NvCloth assert를 엔진 로그로 보낸다. InitializeNvCloth에 넘긴 뒤 프로그램이 끝날 때까지 살아 있어야 하므로 전역에 둔다.
    struct FClothAssertHandler : nv::cloth::PxAssertHandler
    {
        void operator()(const char* Exp, const char* File, int Line, bool& bIgnore) override
        {
            KRS_LOG(Error, "[NvCloth] assert {} ({}:{})", Exp, File, Line);
        }
    };
    FClothAssertHandler gClothAssertHandler;
}


physx::PxCpuDispatcher* FPhysicsCore::GetDispatcher()
{
    return gDispatcher;
}

bool FPhysicsCore::InitPhysX()
{
    using namespace physx;

    nv::cloth::InitializeNvCloth(&gAllocator, &gErrorCallback, &gClothAssertHandler, nullptr);
    gClothFactory = NvClothCreateFactoryCPU();
    if (!gClothFactory) { KRS_LOG(Error, "[NvCloth] CPU factory 생성 실패"); }

    gFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, gAllocator, gErrorCallback);
    if (!gFoundation)
    {
        KRS_LOG(Error, "[Physics] PxCreateFoundation failed");
        return false;
    }

    // PVD는 없어도 엔진은 동작하므로 실패해도 경고만 남기고 계속 진행한다.
    gPvd = PxCreatePvd(*gFoundation);
    gTransport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    if (gPvd && gTransport && gPvd->connect(*gTransport, PxPvdInstrumentationFlag::eALL))
    {
        KRS_LOG(Info, "[Physics] Connected to PVD (127.0.0.1:5425)");
    }
    else
    {
        KRS_LOG(Warning, "[Physics] PVD not connected. Start PVD before the engine to capture.");
    }

    gPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *gFoundation, PxTolerancesScale(), true, gPvd);
    if (!gPhysics)
    {
        KRS_LOG(Error, "[Physics] PxCreatePhysics failed");
        ShutdownPhysX();   // 이미 만든 PVD, Transport, Foundation 정리
        return false;
    }

    gDispatcher = PxDefaultCpuDispatcherCreate(2);
    if (!gDispatcher)
    {
        KRS_LOG(Error, "[Physics] PxDefaultCpuDispatcherCreate failed");
        ShutdownPhysX();   // 이미 만든 PVD, Transport, Foundation 정리
        return false;
    }

    // createMaterial(정지 마찰, 운동 마찰, 반발 계수)
    gDefaultMaterial = gPhysics->createMaterial(0.5f, 0.5f, 0.1f);
    if (!gDefaultMaterial)
    {
        KRS_LOG(Error, "[Physics] createMaterial failed");
        ShutdownPhysX();
        return false;
    }

    KRS_LOG(Info, "[Physics] PhysX {}.{}.{} initialized",
        PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX);
    return true;
}

void FPhysicsCore::ShutdownPhysX()
{
    // 재질은 Physics가 만든 객체이므로 Physics보다 먼저 release한다.
    if (gDefaultMaterial)
    {
        gDefaultMaterial->release();
        gDefaultMaterial = nullptr;
    }

    if (gDispatcher)
    { 
        gDispatcher->release(); 
        gDispatcher = nullptr; 
    }


    // 모든 Scene의 Solver와 천이 지워진 뒤에 해제한다. 남은 Cloth·Solver의 소멸자가 Factory 자원을 쓸 수 있다.
    if (gClothFactory)
    {
        NvClothDestroyFactory(gClothFactory);
        gClothFactory = nullptr;
    }

    if (gPhysics)
    {
        gPhysics->release();
        gPhysics = nullptr;
    }

    if (gPvd)
    {
        gPvd->release(); 
        gPvd = nullptr;
    }

    if (gTransport)
    {
        gTransport->release(); 
        gTransport = nullptr;
    }

    if (gFoundation)
    {
        gFoundation->release(); 
        gFoundation = nullptr;
    }


}
