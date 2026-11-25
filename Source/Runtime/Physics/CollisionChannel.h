#pragma once

#include "Core/Types.h"
#include "UObject/Class.h"
#include "CollisionChannel.generated.h"

UENUM()
enum class ECollisionChannel : uint8{
	WorldStatic, WorldDynamic, Pawn, PhysicsBody, Vehicle, Visibility, Camera, Max
};

UENUM()
enum class ECollisionResponse : uint8{
	Ignore=0, Overlap=1, Block=2, Max
};

// 채널마다 어떻게 반응할 것인가?
USTRUCT()
struct FCollisionResponseContainer{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	ECollisionResponse WorldStatic = ECollisionResponse::Block;

	UPROPERTY(EditAnywhere)
	ECollisionResponse WorldDynamic = ECollisionResponse::Block;

	UPROPERTY(EditAnywhere)
	ECollisionResponse Pawn = ECollisionResponse::Block;

	UPROPERTY(EditAnywhere)
	ECollisionResponse PhysicsBody = ECollisionResponse::Block;

	UPROPERTY(EditAnywhere)
	ECollisionResponse Vehicle = ECollisionResponse::Block;

	UPROPERTY(EditAnywhere)
	ECollisionResponse Visibility = ECollisionResponse::Block;

	UPROPERTY(EditAnywhere)
	ECollisionResponse Camera = ECollisionResponse::Block;

	ECollisionResponse* Slots(){ return &WorldStatic; }
	const ECollisionResponse* Slots() const { return &WorldStatic; }

	void SetAllChannels(ECollisionResponse Response){
		for(uint8 i = 0; i < (uint8)ECollisionChannel::Max; ++i){
			Slots()[i] = Response;
		}
	}

	void SetResponse(ECollisionChannel Channel, ECollisionResponse Response){
		Slots()[(uint8)Channel] = Response;
	}

	ECollisionResponse GetResponse(ECollisionChannel Channel) const{
		return Slots()[(uint8)Channel];
	}
};
static_assert(sizeof(FCollisionResponseContainer) == (size_t)ECollisionChannel::Max,
	"응답 필드는 채널 수만큼 빈틈없이 놓여야 한다");

inline ECollisionResponse BlendResponse(const FCollisionResponseContainer& TA, const ECollisionChannel ChannelA, 
										const FCollisionResponseContainer& TB, const ECollisionChannel ChannelB) {
	ECollisionResponse ResponseA = TA.GetResponse(ChannelB);
	ECollisionResponse ResponseB = TB.GetResponse(ChannelA);
	if (ResponseA == ECollisionResponse::Ignore || ResponseB == ECollisionResponse::Ignore) return ECollisionResponse::Ignore;
	else if (ResponseA == ECollisionResponse::Block && ResponseB == ECollisionResponse::Block) return ECollisionResponse::Block;
	else return ECollisionResponse::Overlap;
}