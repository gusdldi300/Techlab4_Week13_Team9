#include "EnginePCH.h"
#include "PhysicsCore.h"
#include <PxPhysicsAPI.h> 

bool FPhysicsCore::InitPhysX()
{
    using namespace physx;

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

    KRS_LOG(Info, "[Physics] PhysX {}.{}.{} initialized",
        PX_PHYSICS_VERSION_MAJOR, PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX);
    return true;
}

void FPhysicsCore::ShutdownPhysX()
{


}
