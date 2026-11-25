#pragma once

#include "GameFramework/WheeledVehicle.h"
#include "RedBuggy.generated.h"

// 레드 버기 모델(DefaultProject/SourceArt/RedBuggy)을 쓰는 4륜 차.
// 동작(입력·물리·바퀴 표시)은 AWheeledVehicle 그대로이고, 메시 경로와 차체·바퀴 치수, 구동 설정만 바꾼다.
// 몬스터 바퀴라 크고 넓으며, 뒷바퀴가 앞바퀴보다 크다(반지름 0.657 / 0.559m). 그래서 뒷축 치수를 따로 준다.
// 치수는 Blender에서 잰 값이다(차체 원점 = 앞뒤 축 가운데, 지면에서 0.75m 위).
UCLASS()
class ARedBuggy : public AWheeledVehicle
{
	GENERATED_BODY()

public:
	ARedBuggy();
};
