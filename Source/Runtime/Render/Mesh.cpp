#include "EnginePCH.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Physics/BodySetup.h"
#include <algorithm>
#include <cmath>
#include <limits>

UStaticMesh::~UStaticMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshDestroyed();
	delete BodySetup;
	BodySetup = nullptr;
	VertexBuffer = nullptr;
	IndexBuffer = nullptr;
}

UMaterial* UStaticMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex]) {
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t UStaticMesh::FMaterialSlotsHash::operator()(const std::vector<UMaterial*>& Slots) const
{
    size_t Hash = 0;
    for (const UMaterial* Material : Slots)
        Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
    return Hash;
}

void UStaticMesh::CreateBodySetup(const FString& BodySetupPath)
{
	delete BodySetup;
	BodySetup = NewObject<UBodySetup>(this);

	if (!BodySetupPath.empty() && BodySetup->LoadFromFile(BodySetupPath))
	{
		return;
	}

	// 설계도 파일이 없으면 바운드 박스 하나로 대신한다. 언리얼이 임포트할 때 단순 충돌을 자동으로 만드는 것과 같다.
	const FBox& Bounds = MeshData.AABB;
	const FVector Size = Bounds.Max - Bounds.Min;
	if (Size.X <= 0.0f && Size.Y <= 0.0f && Size.Z <= 0.0f)
	{
		return;   // 빈 메시는 모양 없는 BodySetup으로 둔다. InitBody가 도형 0개를 보고 바디를 만들지 않는다.
	}

	FKBoxElem Box;
	Box.Center = (Bounds.Min + Bounds.Max) * 0.5f;
	Box.X = Size.X;   // FKBoxElem은 전체 크기다. 반 크기 변환은 FBodyInstance::InitBody가 한다.
	Box.Y = Size.Y;
	Box.Z = Size.Z;
	BodySetup->AggGeom.BoxElems.Add(Box);
}

void UStaticMesh::RebuildRenderData()
{
    RenderBounds = MeshData.AABB;
    RenderStates.clear();
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshRenderDataChanged();
}

TSharedPtr<const FMeshRenderState> UStaticMesh::GetRenderState(const std::vector<UMaterial*>& Materials)
{
    auto& WeakState = RenderStates[Materials];
    if (auto State = WeakState.lock()) return State;
    auto State = MakeShared<FMeshRenderState>();
    for (const FStaticMeshSection& Section : MeshData.Sections)
    {
        UMaterial* Material = Section.MaterialSlotIndex < Materials.size()
            ? Materials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
        if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
    }
    WeakState = State;
    return State;
}

void UStaticMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void UStaticMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
}
