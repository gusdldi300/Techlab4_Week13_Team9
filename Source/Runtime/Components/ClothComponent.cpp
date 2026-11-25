#include "EnginePCH.h"
#include "ClothComponent.h"

#include "Asset/AssetManager.h"
#include "Engine/ClothSceneProxy.h"
#include "Engine/World.h"
#include "Physics/PhysicsScene.h"
#include "Render/ClothRenderData.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"

UClothComponent::UClothComponent()
{
	// 매 Tick 천 로컬 공간을 컴포넌트 위치로 옮기고 결과 입자를 읽는다.
	PrimaryComponentTick.bCanEverTick = true;
}

// FClothRenderData가 헤더에서는 불완전 타입이라 소멸자를 여기서 정의한다.
UClothComponent::~UClothComponent() = default;

UMaterial* UClothComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

FPrimitiveSceneProxy* UClothComponent::CreateSceneProxy()
{
	return new FClothSceneProxy(this);
}

const FClothRenderData* UClothComponent::PrepareRenderData()
{
	const int32 CountX = std::max(NumX, 2);
	const int32 CountY = std::max(NumY, 2);
	if (LocalPositions.Num() != CountX * CountY) return nullptr;

	if (!RenderData)
	{
		RenderData = MakeUnique<FClothRenderData>();
		bRenderTopologyDirty = true;
	}
	if (bRenderTopologyDirty)
	{
		RenderData->Build(CountX, CountY, Indices, bFlagLayout);
		bRenderTopologyDirty = false;
		bRenderVerticesDirty = true;
	}
	if (bRenderVerticesDirty)
	{
		// 천은 컴포넌트 스케일을 시뮬레이션에 넣지 않는다. 렌더러가 곱할 스케일을 미리 나눠 격자 선과 같은 크기로 그린다.
		const FVector Scale = GetWorldScale3D();
		const auto Inverse = [](float S) { return std::abs(S) > 1e-6f ? 1.0f / S : 1.0f; };
		RenderData->Update(LocalPositions, FVector(Inverse(Scale.X), Inverse(Scale.Y), Inverse(Scale.Z)));
		bRenderVerticesDirty = false;
	}
	return RenderData.get();
}

void UClothComponent::OnRegister()
{
	Super::OnRegister();
	BuildGrid();
	CreateClothState();
}

void UClothComponent::OnUnregister()
{
	// 월드를 내릴 때 컴포넌트 등록 해제가 PhysicsScene 해제보다 먼저 일어나므로 여기서 Solver에서 뺀다.
	DestroyClothState();
	Super::OnUnregister();
}

void UClothComponent::BuildGrid()
{
	const int32 CountX = std::max(NumX, 2);
	const int32 CountY = std::max(NumY, 2);
	const float Step = std::max(Spacing, 0.001f);
	const float HalfWidth = (CountX - 1) * Step * 0.5f;

	// 커튼: 로컬 XY 평면(수평)에 펼쳐 두고 Y = 0 줄을 고정한다. 시작하면 고정한 변을 축으로 아래로 떨어지며 흔들린다.
	// 깃발: 로컬 YZ 평면(수직)에 펼친다. 격자 X는 깃대를 따라 위(Z = 0)에서 아래로, 격자 Y는 깃대에서 +Y로 멀어진다.
	//       Y = 0 줄(깃대 쪽 세로 변)을 고정하므로 바람이 없으면 깃대를 따라 늘어지고, +Y 바람을 받으면 펼쳐져 펄럭인다.
	RestPositions.Empty();
	InvMasses.Empty();
	Indices.Empty();
	RestPositions.Reserve(CountX * CountY);
	InvMasses.Reserve(CountX * CountY);
	for (int32 Y = 0; Y < CountY; ++Y)
	{
		for (int32 X = 0; X < CountX; ++X)
		{
			RestPositions.Add(bFlagLayout
				? FVector(0.0f, Y * Step, -X * Step)
				: FVector(X * Step - HalfWidth, Y * Step, 0.0f));
			InvMasses.Add((bPinTopEdge && Y == 0) ? 0.0f : 1.0f);
		}
	}

	Indices.Reserve((CountX - 1) * (CountY - 1) * 6);
	for (int32 Y = 0; Y < CountY - 1; ++Y)
	{
		for (int32 X = 0; X < CountX - 1; ++X)
		{
			const uint32 I = static_cast<uint32>(Y * CountX + X);
			const uint32 Right = I + 1;
			const uint32 Down = I + static_cast<uint32>(CountX);
			const uint32 DownRight = Down + 1;
			Indices.Add(I);     Indices.Add(Right);     Indices.Add(Down);
			Indices.Add(Right); Indices.Add(DownRight); Indices.Add(Down);
		}
	}

	LocalPositions = RestPositions;

	// 격자가 바뀌면 렌더 버퍼를 다시 만들고, 프록시의 섹션(인덱스 수)과 바운드도 다시 계산하게 한다.
	bRenderTopologyDirty = true;
	bRenderVerticesDirty = true;
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->OnMeshRenderDataChanged();
	}
}

void UClothComponent::CreateClothState()
{
	UWorld* World = GetWorld();
	FPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (!Scene)
	{
		return;   // 에디터 월드에는 물리 Scene이 없다. 정지한 격자만 그린다.
	}

	FClothInstanceInitParams Params;
	Params.LocalPositions = RestPositions;
	Params.InvMasses = InvMasses;
	Params.Indices = Indices;
	Params.WorldLocation = GetWorldLocation();
	Params.WorldRotation = GetWorldRotation().Quaternion();
	Params.StretchStiffness = StretchStiffness;
	Params.BendStiffness = BendStiffness;
	Params.Damping = Damping;
	Params.SolverFrequency = SolverFrequency;
	ClothInstance.Init(Params, Scene);
}

void UClothComponent::DestroyClothState()
{
	ClothInstance.Term();
	LocalPositions = RestPositions;
	bRenderVerticesDirty = true;
}

void UClothComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!ClothInstance.IsValid()) return;

	// 다음 스텝에 쓸 로컬 공간 위치. 액터가 움직이면 이동량만큼 천이 끌려간다.
	ClothInstance.SetWorldTransform(GetWorldLocation(), GetWorldRotation().Quaternion());
	UpdateWind(DeltaTime);
	// 직전 스텝 결과를 읽는다. 한 프레임 늦지만 눈에 띄지 않는다.
	ClothInstance.GetLocalPositions(LocalPositions);
	bRenderVerticesDirty = true;
}

void UClothComponent::UpdateWind(float DeltaTime)
{
	WindTime += DeltaTime;

	// 일정한 바람만 주면 천이 한쪽으로 젖혀진 채 멈춘다. 주기가 서로 맞지 않는 사인 두 개를 곱해
	// 세기가 불규칙하게 오르내리게 해서 깃발처럼 계속 펄럭이게 한다.
	const float Omega = 2.0f * 3.14159265f * std::max(WindGustFrequency, 0.0f);
	const float Wave = std::sin(WindTime * Omega) * std::sin(WindTime * Omega * 0.37f + 1.3f);
	const float Scale = 1.0f + std::clamp(WindGust, 0.0f, 1.0f) * Wave;

	ClothInstance.SetWind(WindVelocity * Scale, DragCoefficient, LiftCoefficient);
}

void UClothComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "NumX" || Property.Name == "NumY" || Property.Name == "Spacing" ||
		Property.Name == "bPinTopEdge" || Property.Name == "bFlagLayout" || Property.Name == "StretchStiffness" || Property.Name == "BendStiffness" ||
		Property.Name == "Damping" || Property.Name == "SolverFrequency")
	{
		BuildGrid();
		if (ClothInstance.IsValid())
		{
			CreateClothState();   // Init이 기존 천을 먼저 Term한다
		}
	}
}

FBox UClothComponent::CalcLocalBounds() const
{
	// 고정한 변 아래로 천 길이만큼 늘어질 수 있으므로 그만큼 아래로 넓힌다.
	const float Width = (std::max(NumX, 2) - 1) * Spacing;
	const float Length = (std::max(NumY, 2) - 1) * Spacing;
	FBox Bounds;
	if (bFlagLayout)
	{
		// 깃발: 깃대(Z ≤ 0)를 따라 Width만큼, 깃대에서 Length만큼 뻗는다. 바람 방향이 바뀌거나 늘어질 때를 위해 사방으로 Length만큼 여유를 둔다.
		Bounds.Min = FVector(-Length, -Length, -(Width + Length));
		Bounds.Max = FVector(Length, Length, Length * 0.1f);
		return Bounds;
	}
	Bounds.Min = FVector(-Width * 0.5f, -Length, -Length);
	Bounds.Max = FVector(Width * 0.5f, Length, Length * 0.1f);
	return Bounds;
}

void UClothComponent::DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const
{
	const int32 CountX = std::max(NumX, 2);
	const int32 CountY = std::max(NumY, 2);
	if (LocalPositions.Num() != CountX * CountY) return;

	// 천 로컬 공간은 컴포넌트의 위치와 회전만 따른다(스케일은 시뮬레이션에 넣지 않는다).
	const FVector Location = GetWorldLocation();
	const FQuat Rotation = GetWorldRotation().Quaternion();
	auto ToWorld = [&](int32 X, int32 Y) { return Location + Rotation.RotateVector(LocalPositions[Y * CountX + X]); };

	for (int32 Y = 0; Y < CountY; ++Y)
	{
		for (int32 X = 0; X < CountX; ++X)
		{
			const FVector P = ToWorld(X, Y);
			if (X + 1 < CountX) LineBatcher->AddLine(P, ToWorld(X + 1, Y), Color);
			if (Y + 1 < CountY) LineBatcher->AddLine(P, ToWorld(X, Y + 1), Color);
		}
	}
}

void DrawClothComponents(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher) return;

	for (TObjectIterator<UClothComponent> It; It; ++It)
	{
		UClothComponent* Cloth = *It;
		if (!Cloth || Cloth->GetWorld() != World || !Cloth->IsRegistered()) continue;

		const bool bSelected = Cloth->GetOwner() && Cloth->GetOwner() == Selected;
		// 메시로 그리므로 격자 선은 켜 둔 천이나 선택한 천에만 겹쳐 그린다.
		if (!Cloth->bDrawDebugGrid && !bSelected) continue;
		const FVector4 Color = bSelected ? FVector4(1.0f, 0.85f, 0.2f, 1.0f) : FVector4(0.4f, 0.8f, 1.0f, 1.0f);
		Cloth->DrawDebug(LineBatcher, Color);
	}
}
