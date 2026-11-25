#include "EnginePCH.h"
#include "PhysicsFilterShader.h"
#include "Physics/PhysicsFiltering.h"
using namespace physx;

physx::PxFilterFlags SimulationFilterShader(physx::PxFilterObjectAttributes attributes0, physx::PxFilterData filterData0,
	physx::PxFilterObjectAttributes attributes1, physx::PxFilterData filterData1,
	physx::PxPairFlags& pairFlags,
	const void* constantBlock, physx::PxU32 constantBlockSize)
{
	// 움직이지 않는것끼리 거르기
	if (PxFilterObjectIsKinematic(attributes0) && PxFilterObjectIsKinematic(attributes1)) return PxFilterFlag::eSUPPRESS;		// eSUPPRESS: 거르되 지켜봄
	if (PxFilterObjectIsKinematic(attributes0) && PxGetFilterObjectType(attributes1) == PxFilterObjectType::eRIGID_STATIC) return PxFilterFlag::eSUPPRESS;
	if (PxFilterObjectIsKinematic(attributes1) && PxGetFilterObjectType(attributes0) == PxFilterObjectType::eRIGID_STATIC) return PxFilterFlag::eSUPPRESS;

	const ECollisionChannel Channel0 = GetChannelFromWord3(filterData0.word3);	//word3 : (Channel << 24) | Flags
	const ECollisionChannel Channel1 = GetChannelFromWord3(filterData1.word3);

	const bool b0Blocks1 = (filterData0.word1 & (1u << (uint32)Channel1)) != 0;	
	const bool b1Blocks0 = (filterData1.word1 & (1u << (uint32)Channel0)) != 0;	

	if (!(b0Blocks1 && b1Blocks0)) return PxFilterFlag::eSUPPRESS;	// 채널판정 : 둘중 하나가 Block이 아니라면 거르기

	pairFlags = PxPairFlag::eCONTACT_DEFAULT;	// eCONTACT_DEFAULT : 접촉 실제로 만들고 물어라

	// 접촉 알림 플래그 설정 
	if ((filterData0.word3 | filterData1.word3) & CFF_ContactNotify) {
		pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_PERSISTS | PxPairFlag::eNOTIFY_CONTACT_POINTS;
		// eNOTIFY_TOUCH_FOUND: 닿기 시작한 프레임에 알림
		// eNOTIFY_TOUCH_PERSISTS: 닿아 있는 동안 매 프레임 알림
		// eNOTIFY_CONTACT_POINTS: 접촉 좌표, 법선 함께 전달
	}

	return PxFilterFlags();						// 빈 플래그 만들기
}
