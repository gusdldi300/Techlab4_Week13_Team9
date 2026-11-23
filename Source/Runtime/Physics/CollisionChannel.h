#pragma once

#include "Core/Types.h"

enum class ECollisionChannel : uint8{
	WorldStatic, WorldDynamic, Pawn, PhysicsBody, Vehicle, Visibility, Camera, Max
};

enum class ECollisionResponse : uint8{
	Ignore=0, Overlap=1, Block=2, Max
};

struct FCollisionResponseContainer{
	ECollisionResponse Responses[(uint8)ECollisionChannel::Max];	// 각 채널에 대해 어떻게 반응할 것인가?

	FCollisionResponseContainer(){
		SetAllChannels(ECollisionResponse::Block);
	}

	void SetAllChannels(ECollisionResponse Response){
		for(auto & Slot : Responses){
			Slot = Response;
		}
	}

	void SetResponse(ECollisionChannel Channel, ECollisionResponse Response){
		Responses[(uint8)Channel] = Response;
	}

	ECollisionResponse GetResponse(ECollisionChannel Channel) const{
		return Responses[(uint8)Channel];
	}
};

inline ECollisionResponse BlendResponse(const FCollisionResponseContainer& TA, const ECollisionChannel ChannelA, 
										const FCollisionResponseContainer& TB, const ECollisionChannel ChannelB) {
	ECollisionResponse ResponseA = TA.GetResponse(ChannelB);
	ECollisionResponse ResponseB = TB.GetResponse(ChannelA);
	if (ResponseA == ECollisionResponse::Ignore || ResponseB == ECollisionResponse::Ignore) return ECollisionResponse::Ignore;
	else if (ResponseA == ECollisionResponse::Block && ResponseB == ECollisionResponse::Block) return ECollisionResponse::Block;
	else return ECollisionResponse::Overlap;
}