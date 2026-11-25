#include "EnginePCH.h"
#include "ClothComponent.h"

#include "Asset/AssetManager.h"
#include "Engine/ClothSceneProxy.h"
#include "Engine/World.h"
#include "Physics/PhysicsScene.h"
#include "Render/ClothRenderData.h"
#include "Render/LineBatcher.h"
#include "Render/Mesh.h"
#include "Render/StaticMeshData.h"
#include "UObject/UObjectIterator.h"

#include <cmath>
#include <tuple>
#include <unordered_map>

UClothComponent::UClothComponent()
{
	// 매 Tick 천 로컬 공간을 컴포넌트 위치로 옮기고 결과 입자를 읽는다.
	PrimaryComponentTick.bCanEverTick = true;
}

// FClothRenderData가 헤더에서는 불완전 타입이라 소멸자를 여기서 정의한다.
UClothComponent::~UClothComponent() = default;

UMaterial* UClothComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	if (SourceMesh)
	{
		if (UMaterial* MeshMaterial = SourceMesh->GetMaterial(0)) return MeshMaterial;
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

FPrimitiveSceneProxy* UClothComponent::CreateSceneProxy()
{
	return new FClothSceneProxy(this);
}

const FClothRenderData* UClothComponent::PrepareRenderData()
{
	if (LocalPositions.Num() != RestPositions.Num() || RenderIndices.Num() < 3) return nullptr;

	if (!RenderData)
	{
		RenderData = MakeUnique<FClothRenderData>();
		bRenderTopologyDirty = true;
	}
	if (bRenderTopologyDirty)
	{
		RenderData->Build(RenderUVs, RenderToParticle, RenderIndices);
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
	BuildCloth();
	CreateClothState();
}

void UClothComponent::OnUnregister()
{
	// 월드를 내릴 때 컴포넌트 등록 해제가 PhysicsScene 해제보다 먼저 일어나므로 여기서 Solver에서 뺀다.
	DestroyClothState();
	Super::OnUnregister();
}

void UClothComponent::BuildCloth()
{
	if (!SourceMeshPath.empty() && BuildFromMesh())
	{
		OnShapeRebuilt();
		return;
	}
	BuildGrid();
	OnShapeRebuilt();
}

void UClothComponent::BuildGrid()
{
	SourceMesh = nullptr;

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

	// 격자는 렌더 정점과 입자가 1:1이다. 깃발은 U = 깃대에서 멀어지는 방향(격자 Y), V = 위에서 아래(격자 X)다.
	RenderUVs.Empty();
	RenderToParticle.Empty();
	RenderUVs.Reserve(CountX * CountY);
	RenderToParticle.Reserve(CountX * CountY);
	for (int32 Y = 0; Y < CountY; ++Y)
	{
		for (int32 X = 0; X < CountX; ++X)
		{
			const float GridU = static_cast<float>(X) / (CountX - 1);
			const float GridV = static_cast<float>(Y) / (CountY - 1);
			RenderUVs.Add(bFlagLayout ? FVector2(GridV, GridU) : FVector2(GridU, GridV));
			RenderToParticle.Add(static_cast<uint32>(Y * CountX + X));
		}
	}
	RenderIndices = Indices;
}

bool UClothComponent::BuildFromMesh()
{
	SourceMesh = UAssetManager::GetAssetByPath<UStaticMesh>(SourceMeshPath);
	if (!SourceMesh)
	{
		KRS_LOG(Warning, "[Cloth] mesh '{}' not found. Falling back to grid cloth", SourceMeshPath);
		return false;
	}
	const FStaticMeshData& Mesh = SourceMesh->GetMeshData();
	if (Mesh.Vertices.Num() < 3 || Mesh.Indices.Num() < 3)
	{
		KRS_LOG(Warning, "[Cloth] mesh '{}' has no triangles. Falling back to grid cloth", SourceMeshPath);
		SourceMesh = nullptr;
		return false;
	}

	// 1. 정점 합치기: 임포터는 UV·노멀·색이 다르면 같은 위치에도 정점을 따로 만든다.
	//    그대로 쿠킹하면 쪼개진 정점끼리 구속이 없어 그 선을 따라 천이 찢어지므로, 위치가 같으면 입자 하나로 합친다.
	//    0.1mm 격자로 반올림한 위치를 키로 쓴다.
	const float Scale = SourceMeshScale;
	const float Quantum = 1e-4f;
	using FPositionKey = std::tuple<int64, int64, int64>;
	struct FKeyHash
	{
		size_t operator()(const FPositionKey& K) const
		{
			const uint64 A = static_cast<uint64>(std::get<0>(K));
			const uint64 B = static_cast<uint64>(std::get<1>(K));
			const uint64 C = static_cast<uint64>(std::get<2>(K));
			return static_cast<size_t>((A * 73856093ull) ^ (B * 19349663ull) ^ (C * 83492791ull));
		}
	};
	std::unordered_map<FPositionKey, uint32, FKeyHash> PositionToParticle;

	RestPositions.Empty();
	InvMasses.Empty();
	RenderUVs.Empty();
	RenderToParticle.Empty();
	RenderUVs.Reserve(Mesh.Vertices.Num());
	RenderToParticle.Reserve(Mesh.Vertices.Num());
	for (const FVertex& V : Mesh.Vertices)
	{
		const FVector P = V.Position * Scale;
		const FPositionKey Key(
			static_cast<int64>(std::llround(P.X / Quantum)),
			static_cast<int64>(std::llround(P.Y / Quantum)),
			static_cast<int64>(std::llround(P.Z / Quantum)));
		uint32 Particle = 0;
		auto It = PositionToParticle.find(Key);
		if (It == PositionToParticle.end())
		{
			Particle = static_cast<uint32>(RestPositions.Num());
			PositionToParticle.emplace(Key, Particle);
			RestPositions.Add(P);
			InvMasses.Add(1.0f);
		}
		else
		{
			Particle = It->second;
		}

		// 2. 고정: 빨간 정점(R이 크고 G·B가 작다)을 고정한다. 합쳐진 정점 중 하나라도 빨가면 그 입자를 고정한다.
		//    정점 색이 없는 메시는 흰색(1, 1, 1)으로 들어오므로 아무것도 고정되지 않는다.
		if (V.Color.X > 0.5f && V.Color.Y < 0.5f && V.Color.Z < 0.5f)
		{
			InvMasses[Particle] = 0.0f;
		}
		RenderUVs.Add(V.UV);
		RenderToParticle.Add(Particle);
	}

	// 3. 삼각형: 렌더용은 메시 그대로, 시뮬레이션용은 입자 번호로 바꾼다. 합친 뒤 두 꼭짓점이 겹친 삼각형은 뺀다.
	RenderIndices = Mesh.Indices;
	Indices.Empty();
	Indices.Reserve(Mesh.Indices.Num());
	for (int32 i = 0; i + 2 < Mesh.Indices.Num(); i += 3)
	{
		const uint32 A = RenderToParticle[Mesh.Indices[i]];
		const uint32 B = RenderToParticle[Mesh.Indices[i + 1]];
		const uint32 C = RenderToParticle[Mesh.Indices[i + 2]];
		if (A == B || B == C || C == A) continue;
		Indices.Add(A); Indices.Add(B); Indices.Add(C);
	}

	int32 NumPinned = 0;
	for (float W : InvMasses) NumPinned += (W == 0.0f) ? 1 : 0;
	KRS_LOG(Info, "[Cloth] mesh '{}': {} render vertices -> {} particles ({} pinned), {} triangles",
		SourceMeshPath, Mesh.Vertices.Num(), RestPositions.Num(), NumPinned, Indices.Num() / 3);
	if (NumPinned == 0)
	{
		KRS_LOG(Warning, "[Cloth] mesh '{}' has no red vertices. Nothing is pinned, so the whole cloth will fall", SourceMeshPath);
	}
	if (Indices.Num() < 3)
	{
		SourceMesh = nullptr;
		return false;
	}
	return true;
}

void UClothComponent::OnShapeRebuilt()
{
	LocalPositions = RestPositions;

	// 모양이 바뀌면 렌더 버퍼를 다시 만들고, 프록시의 섹션(인덱스 수)과 바운드도 다시 계산하게 한다.
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
		Property.Name == "Damping" || Property.Name == "SolverFrequency" ||
		Property.Name == "SourceMeshPath" || Property.Name == "SourceMeshScale")
	{
		BuildCloth();
		if (ClothInstance.IsValid())
		{
			CreateClothState();   // Init이 기존 천을 먼저 Term한다
		}
	}
}

FBox UClothComponent::CalcLocalBounds() const
{
	if (SourceMesh && RestPositions.Num() > 0)
	{
		// 메시 천: 정지 모양의 바운드에서, 고정점을 축으로 어느 쪽으로든 펄럭일 수 있도록 가장 긴 변만큼 사방으로 넓힌다.
		FVector Min = RestPositions[0];
		FVector Max = RestPositions[0];
		for (const FVector& P : RestPositions)
		{
			Min = FVector(std::min(Min.X, P.X), std::min(Min.Y, P.Y), std::min(Min.Z, P.Z));
			Max = FVector(std::max(Max.X, P.X), std::max(Max.Y, P.Y), std::max(Max.Z, P.Z));
		}
		const FVector Size = Max - Min;
		const float Reach = std::max({ Size.X, Size.Y, Size.Z });
		FBox MeshBounds;
		MeshBounds.Min = Min - FVector(Reach, Reach, Reach);
		MeshBounds.Max = Max + FVector(Reach, Reach, Reach);
		return MeshBounds;
	}

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
	if (SourceMesh)
	{
		// 메시 천: 시뮬레이션 삼각형의 변을 그린다(공유 변은 두 번 그려지지만 디버그용이라 둔다).
		if (LocalPositions.Num() != RestPositions.Num()) return;
		const FVector Location = GetWorldLocation();
		const FQuat Rotation = GetWorldRotation().Quaternion();
		auto ToWorld = [&](uint32 I) { return Location + Rotation.RotateVector(LocalPositions[I]); };
		for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
		{
			const FVector A = ToWorld(Indices[i]), B = ToWorld(Indices[i + 1]), C = ToWorld(Indices[i + 2]);
			LineBatcher->AddLine(A, B, Color);
			LineBatcher->AddLine(B, C, Color);
			LineBatcher->AddLine(C, A, Color);
		}
		return;
	}

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
