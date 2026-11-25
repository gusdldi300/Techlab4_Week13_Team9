#pragma once

#include "MeshComponent.h"
#include "Physics/ClothInstance.h"
#include "ClothComponent.generated.h"

class FLineBatcher;
class FClothRenderData;
class UWorld;
class AActor;
class UStaticMesh;

// 월드에 있는 천 컴포넌트를 격자 선으로 그린다. bDrawDebugGrid를 켰거나 선택된 천만 그린다.
void DrawClothComponents(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);

// NvCloth로 시뮬레이션하는 천. 모양은 두 가지 중 하나로 정한다.
// 1. 격자(SourceMeshPath가 비었을 때): NumX × NumY 격자를 펼치고 격자 Y = 0 줄을 고정한다.
//    기본(커튼) 배치는 로컬 XY 평면(수평)에 펼쳐 윗변에 매달리고, 깃발 배치는 로컬 YZ 평면(수직)에 펼쳐 깃대(로컬 Z축)에 매달린다.
// 2. 메시(SourceMeshPath가 있을 때): 스태틱 메시의 정점과 삼각형을 그대로 천으로 쓴다. 정점 색이 빨간 정점을 고정한다.
//    같은 위치에 쪼개진 정점(UV 경계, 각진 모서리)은 입자 하나로 합쳐 시뮬레이션하고, 그릴 때는 원래 UV로 나눠 그린다.
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
	// 메시 천이면 메시의 첫 머티리얼, 격자 천이면 기본 머티리얼
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// 프록시가 그리기 직전에 부른다. 입자가 바뀌었으면 정점 버퍼를 다시 올리고 렌더 데이터를 돌려준다.
	const FClothRenderData* PrepareRenderData();
	// 양면 삼각형 인덱스 수(앞면 + 뒷면)
	uint32 GetRenderIndexCount() const { return static_cast<uint32>(RenderIndices.Num()) * 2; }

	void DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const;

	// 메시에서 천을 만들었으면 true. 경로가 비었거나 메시를 읽지 못해 격자로 대체했으면 false다.
	bool IsMeshCloth() const { return SourceMesh != nullptr; }

	UPROPERTY(EditAnywhere)
	FString SourceMeshPath;          // 천으로 쓸 스태틱 메시(예: FBX/Cape/Cape_Static.fbx.bin). 비우면 격자 천

	UPROPERTY(EditAnywhere)
	float SourceMeshScale = 0.01f;   // 메시 단위 → 천 단위(m). FBX 임포터는 cm로 저장하므로 0.01

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

	// 강성(Stiffness): Solver가 한 번 반복할 때 구속 오차를 얼마나 메우는지 [0, 1]. 1이면 매번 쉬는 길이로 완전히 되돌린다.
	// 쿠커가 만든 Phase 4종(세로·가로·대각선·굽힘)에 각각 따로 준다.
	UPROPERTY(EditAnywhere)
	float VerticalStiffness = 1.0f;  // 세로(중력 방향) 구속. 낮을수록 아래로 늘어진다

	UPROPERTY(EditAnywhere)
	float HorizontalStiffness = 1.0f; // 가로(중력에 수직) 구속. 낮을수록 옆으로 벌어진다

	UPROPERTY(EditAnywhere)
	float ShearStiffness = 1.0f;     // 대각선 구속. 낮을수록 마름모로 찌그러져 천이 비스듬히 잘 늘어진다

	UPROPERTY(EditAnywhere)
	float BendStiffness = 0.5f;      // 굽힘 구속. 낮을수록 실크처럼 잘 접히고, 높을수록 가죽처럼 뻣뻣하다

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
	// 속성에 맞는 천 모양을 만든다. 메시 경로가 있고 읽을 수 있으면 메시, 아니면 격자다.
	void BuildCloth();
	// 로컬 격자(정지 위치, 역질량, 삼각형, 렌더 정점)를 만든다.
	void BuildGrid();
	// 메시 정점을 위치로 합쳐 입자를 만들고 정점 색으로 고정 입자를 정한다. 메시가 없거나 쓸 수 없으면 false.
	bool BuildFromMesh();
	// 모양이 바뀐 뒤 공통 처리: 표시용 위치를 정지 위치로 되돌리고 렌더 버퍼·프록시를 다시 만들게 한다.
	void OnShapeRebuilt();
	// 월드에 PhysicsScene이 있으면 천을 만들어 넣는다.
	void CreateClothState();
	void DestroyClothState();
	// 이번 Tick의 바람(돌풍 포함)을 천에 넣는다.
	void UpdateWind(float DeltaTime);

	FClothInstance ClothInstance;

	// 시뮬레이션 입자(로컬 공간, m)
	TArray<FVector> RestPositions;     // 로컬 정지 위치
	TArray<float>   InvMasses;
	TArray<uint32>  Indices;           // 입자 번호로 된 삼각형(쿠커에 넘긴다)

	// 그릴 정점. 격자는 입자와 1:1이고, 메시는 UV 경계에서 같은 입자를 여러 렌더 정점이 나눠 쓴다.
	TArray<FVector2> RenderUVs;
	TArray<uint32>   RenderToParticle; // 렌더 정점 → 입자 번호
	TArray<uint32>   RenderIndices;    // 렌더 정점 번호로 된 앞면 삼각형

	// 메시 천일 때 원본 메시. UAssetManager가 소유하고 여러 컴포넌트가 함께 쓰므로 빌려 쓰기만 한다(머티리얼 조회용).
	UStaticMesh* SourceMesh = nullptr;
	// 그릴 위치(로컬). 시뮬레이션 중이면 매 Tick 갱신하고, 아니면 정지 위치다.
	TArray<FVector> LocalPositions;
	// 돌풍 계산용 누적 시간(초)
	float WindTime = 0.0f;

	// 그릴 메시의 GPU 자원. 컴포넌트가 소유하고 프록시는 빌려 쓴다. 디바이스가 준비된 뒤 처음 그릴 때 만든다.
	TUniquePtr<FClothRenderData> RenderData;
	bool bRenderTopologyDirty = true;   // 격자 크기·삼각형·UV가 바뀜 → 버퍼 재생성
	bool bRenderVerticesDirty = true;   // 입자 위치가 바뀜 → 정점 재계산·업로드
};
