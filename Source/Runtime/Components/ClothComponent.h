#pragma once

#include "MeshComponent.h"
#include "Physics/ClothInstance.h"
#include "ClothComponent.generated.h"

class FLineBatcher;
class FClothRenderData;
class UWorld;
class AActor;

// 월드에 있는 천 컴포넌트를 격자 선으로 그린다. bDrawDebugGrid를 켰거나 선택된 천만 그린다.
void DrawClothComponents(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);

// NvCloth로 시뮬레이션하는 사각형 천. NumX × NumY 격자를 펼치고 격자 Y = 0 줄을 고정한다.
// 기본(커튼) 배치는 로컬 XY 평면(수평)에 펼쳐 윗변에 매달리고, 깃발 배치는 로컬 YZ 평면(수직)에 펼쳐 깃대(로컬 Z축)에 매달린다.
// 시뮬레이션은 PhysicsScene이 있는 월드(PIE, Game)에서만 하고, 결과는 머티리얼 슬롯 0으로 양면 메시를 그린다.
UCLASS()
class UClothComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UClothComponent();
	~UClothComponent() override;

	void TickComponent(float DeltaTime) override;
	void PostEditChangeProperty(const FProperty& Property) override;
	FBox CalcLocalBounds() const override;

	// 렌더링: 머티리얼 슬롯 하나("Cloth"). 디테일 패널에서 텍스처를 놓으면 이 슬롯 덮어쓰기가 된다.
	int32 GetNumMaterials() const override { return 1; }
	FString GetMaterialSlotName(int32 SlotIndex) const override { return "Cloth"; }
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// 프록시가 그리기 직전에 부른다. 입자가 바뀌었으면 정점 버퍼를 다시 올리고 렌더 데이터를 돌려준다.
	const FClothRenderData* PrepareRenderData();
	// 양면 삼각형 인덱스 수(앞면 + 뒷면)
	uint32 GetRenderIndexCount() const { return static_cast<uint32>(Indices.Num()) * 2; }

	void DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const;

	UPROPERTY(EditAnywhere)
	bool bFlagLayout = false;        // 깃발 배치: 로컬 Z축이 깃대, 천은 +Y로 뻗는다. NumX = 세로 정점 수, NumY = 가로 정점 수

	UPROPERTY(EditAnywhere)
	bool bDrawDebugGrid = false;     // 메시 위에 시뮬레이션 격자 선을 겹쳐 그린다(선택한 천은 항상 그린다)

	UPROPERTY(EditAnywhere)
	int32 NumX = 20;                 // 가로 정점 수

	UPROPERTY(EditAnywhere)
	int32 NumY = 20;                 // 세로 정점 수

	UPROPERTY(EditAnywhere)
	float Spacing = 0.05f;           // 정점 간격(m). 기본값이면 약 1m × 1m 천

	UPROPERTY(EditAnywhere)
	bool bPinTopEdge = true;         // 윗변 정점을 고정한다

	UPROPERTY(EditAnywhere)
	float StretchStiffness = 1.0f;

	UPROPERTY(EditAnywhere)
	float BendStiffness = 0.5f;

	UPROPERTY(EditAnywhere)
	float Damping = 0.1f;

	UPROPERTY(EditAnywhere)
	float SolverFrequency = 120.0f;

	UPROPERTY(EditAnywhere)
	FVector WindVelocity = FVector(0.0f, 0.0f, 0.0f);   // 월드 기준 바람(m/s). 0이면 바람 없음

	UPROPERTY(EditAnywhere)
	float WindGust = 0.5f;           // 돌풍 세기. 0이면 일정한 바람, 1이면 0 ~ 2배 사이로 출렁인다

	UPROPERTY(EditAnywhere)
	float WindGustFrequency = 1.5f;  // 돌풍이 1초에 몇 번 오가는지

	UPROPERTY(EditAnywhere)
	float DragCoefficient = 0.5f;    // [0, 1] 바람 방향으로 미는 힘

	UPROPERTY(EditAnywhere)
	float LiftCoefficient = 0.6f;    // [0, 1] 비스듬한 면을 옆으로 띄우는 힘. 펄럭임을 만든다

protected:
	void OnRegister() override;
	void OnUnregister() override;

private:
	// 속성으로 로컬 격자(정지 위치, 역질량, 삼각형)를 만들고 표시용 위치를 정지 위치로 되돌린다.
	void BuildGrid();
	// 월드에 PhysicsScene이 있으면 천을 만들어 넣는다.
	void CreateClothState();
	void DestroyClothState();
	// 이번 Tick의 바람(돌풍 포함)을 천에 넣는다.
	void UpdateWind(float DeltaTime);

	FClothInstance ClothInstance;

	TArray<FVector> RestPositions;     // 로컬 정지 위치
	TArray<float>   InvMasses;
	TArray<uint32>  Indices;
	// 그릴 위치(로컬). 시뮬레이션 중이면 매 Tick 갱신하고, 아니면 정지 위치다.
	TArray<FVector> LocalPositions;
	// 돌풍 계산용 누적 시간(초)
	float WindTime = 0.0f;

	// 그릴 메시의 GPU 자원. 컴포넌트가 소유하고 프록시는 빌려 쓴다. 디바이스가 준비된 뒤 처음 그릴 때 만든다.
	TUniquePtr<FClothRenderData> RenderData;
	bool bRenderTopologyDirty = true;   // 격자 크기·삼각형·UV가 바뀜 → 버퍼 재생성
	bool bRenderVerticesDirty = true;   // 입자 위치가 바뀜 → 정점 재계산·업로드
};
