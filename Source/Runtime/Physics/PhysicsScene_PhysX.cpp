#include "EnginePCH.h"
#include "PhysicsScene_PhysX.h"
#include <PxPhysicsAPI.h>
#include "PhysicsCore.h"
using namespace physx;

void FPhysicsScene_PhysX::CreatePhysicsScene()
{
	PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();

    PxSceneDesc Desc(Physics->getTolerancesScale());          
    Desc.gravity = PxVec3(0.0f, 0.0f, -9.81f);           
    Desc.cpuDispatcher = FPhysicsCore::Get().GetDispatcher();  
    Desc.filterShader = PxDefaultSimulationFilterShader;    

    gPxScene = Physics->createScene(Desc);
    if (!gPxScene)
    {
        KRS_LOG(Error, "[Physics] createScene failed");
        return;
    }

    //Scene 하나가 PVD에 무엇을 보낼지 고르는 설정
    if (PxPvdSceneClient* Pvd = gPxScene->getScenePvdClient())  // 5
    {
        //관절의 위치·축(로컬 프레임), Twist·Swing 제한 범위
        Pvd->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
        //매 프레임의 접촉점과 접촉 법선
        Pvd->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
        //Raycast·Sweep·Overlap의 광선·도형과 맞은 결과
        Pvd->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
    }


}
