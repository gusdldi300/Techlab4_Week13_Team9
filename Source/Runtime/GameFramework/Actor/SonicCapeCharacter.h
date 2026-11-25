#pragma once

#include "GameFramework/Actor/MyCharacter.h"
#include "SonicCapeCharacter.generated.h"

class UClothComponent;
class USonicAnimInstance;

// 플레이어가 조종하는 망토 단 소닉. 카메라·스프링 암·이동 입력은 AMyCharacter(UE 3인칭 템플릿)를 그대로 쓴다.
// 애니메이션은 USonicAnimInstance의 Idle / Run만 쓴다(움직이면 Run, 서면 Idle).
// 망토는 UClothComponent(NvCloth)다. 매 Tick 등뼈 위치를 따라가고, 달리는 속도의 반대 방향 바람을 받아 뒤로 흩날린다.
// 조작: WASD 이동 / Space 점프 / 마우스 카메라
UCLASS()
class ASonicCapeCharacter : public AMyCharacter
{
	GENERATED_BODY()

public:
	ASonicCapeCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere)
	FString IdleAnimationPath = "FBX/Sonic_SadIdle/Sonic_SadIdle.anim.bin";
	UPROPERTY(EditAnywhere)
	FString RunAnimationPath = "FBX/Sonic_Running/Sonic_Running.anim.bin";

	// 달리기 속도에 곱하는 배율. 1이면 달리기 클립의 발 속도와 같다
	UPROPERTY(EditAnywhere)
	float RunSpeedScale = 1.0f;
	// 이 속도(cm/s)보다 빠르면 Run 클립을 튼다
	UPROPERTY(EditAnywhere)
	float RunAnimThreshold = 50.0f;

	// 망토 윗변을 붙일 뼈 이름(일부만 맞아도 된다). 못 찾으면 캡슐 기준 위치에 그대로 둔다
	UPROPERTY(EditAnywhere)
	FString CapeBoneName = "Spine2";
	// 뼈에서 등 뒤로 뺄 거리(m). 너무 작으면 망토가 몸(가시)을 뚫고 보인다
	UPROPERTY(EditAnywhere)
	float CapeBackOffset = 0.22f;
	// 뼈에서 위로 올릴 거리(m)
	UPROPERTY(EditAnywhere)
	float CapeUpOffset = 0.05f;
	// 서 있을 때도 부는 약한 바람(월드 m/s). 달리면 여기에 몸 속도의 반대 방향이 더해진다
	UPROPERTY(EditAnywhere)
	FVector AmbientWind = FVector(0.0f, 0.0f, 0.0f);
	// 몸 속도를 맞바람으로 바꿀 때 곱하는 배율. 0이면 달려도 바람이 없다
	UPROPERTY(EditAnywhere)
	float RunWindScale = 1.0f;

	UClothComponent* CapeComponent = nullptr;

private:
	USonicAnimInstance* GetSonicAnimInstance() const;
	void FindCapeBone();
	void UpdateCape();

	int32 CapeBoneIndex = INDEX_NONE;
};
