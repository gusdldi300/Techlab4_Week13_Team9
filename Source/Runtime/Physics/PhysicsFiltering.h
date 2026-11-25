#pragma once
#include "Core/Types.h"
#include "CollisionChannel.h"

struct FCollisionFilterData {
	uint32 Word0, Word1, Word2, Word3;
	
	FCollisionFilterData() {
		Word0 = Word1 = Word2 = Word3 = 0;
	}
};

enum ECollisionFilterFlag : uint32 {
	CFF_ContactNotify = 0x01,	// 어디서 닿았는지 알림
	CFF_VehicleChassis = 0x02,	// 차량 차체 Shape(쿼리용). 바퀴 레이가 자기 차체를 맞히지 않게 거른다
};

static constexpr uint32 ChannelShift = 24;
static constexpr uint32 FlagMask = 0x00FFFFFF;

inline ECollisionChannel GetChannelFromWord3(uint32 Word3) {
	return static_cast<ECollisionChannel> (Word3 >> ChannelShift);
}

inline void BuildChannelMasks(const FCollisionResponseContainer& Responses, uint32& OutBlockMask, uint32& OutOverlapMask) {
	OutBlockMask = OutOverlapMask = 0;
	for (uint16 i = 0; i < (uint8) ECollisionChannel::Max; ++i) {
		ECollisionResponse Response = Responses.GetResponse((ECollisionChannel)i);
		if (Response == ECollisionResponse::Block) {
			OutBlockMask |= (1u << i);
		}
		else if (Response == ECollisionResponse::Overlap) {
			OutOverlapMask |= (1u << i);
		}
	}
}

inline void CreateShapeFilterData(ECollisionChannel ObjectType,	const FCollisionResponseContainer& Responses,
	uint32 OwnerID,	uint32 BodyIndex, bool bContactNotify, FCollisionFilterData& OutSimData, FCollisionFilterData& OutQueryData) {
	uint32 BlockMask = 0;
	uint32 OverlapMask = 0;
	BuildChannelMasks(Responses, BlockMask, OverlapMask);

	//Word0 : 인접한 뼈인가?
	OutSimData.Word0 = BodyIndex;
	OutQueryData.Word0 = 0;			//ActorID로 교체 필요!

	// Word1 : 내가 그 채널을 Block하는가?
	OutSimData.Word1 = BlockMask;
	OutQueryData.Word1 = BlockMask;

	// Word2 : 둘이 같은 래그돌인가?
	OutSimData.Word2 = OwnerID;
	OutQueryData.Word2 = OverlapMask;

	// Word3 : 상대채널 / 접촉알림플래그
	uint32 Word3 = ((uint32)ObjectType << ChannelShift);
	if (bContactNotify) Word3 |= CFF_ContactNotify;
	OutSimData.Word3 = Word3;
	OutQueryData.Word3 = Word3;
}