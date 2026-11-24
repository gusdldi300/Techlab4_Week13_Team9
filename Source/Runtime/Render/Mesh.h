#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/StaticMeshData.h"
#include "Render/MeshRenderData.h"
#include "Render/VertexFactory.h"
#include <unordered_map>
#include <vector>
#include "Mesh.generated.h"

class FPrimitiveSceneProxy;
class UBodySetup;

UCLASS()
class UStaticMesh : public URenderAsset
{
	GENERATED_BODY()
public:
    UStaticMesh() : SortID(NextSortID++) {}
	virtual ~UStaticMesh() override;

	FStaticMeshData MeshData;

	// Renderer가 실제로 Bind할 런타임 재질 객체
	TArray<UMaterial*> Materials;

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;

	const FStaticMeshData& GetMeshData() const { return MeshData; }

	// ---- 물리 (담당: A) ----
	// 이 메시의 물리 모양 설계도. 메시가 소유하고, 이 메시를 쓰는 컴포넌트들은 빌려 쓰기만 한다.
	UBodySetup* GetBodySetup() const { return BodySetup; }
	// BodySetup을 만든다. 설계도 파일(BodySetupPath)이 있으면 그것을 읽고, 없거나 깨졌으면 메시 바운드 박스 하나로 채운다.
	// 이미 있으면 다시 만든다. 메시 데이터가 준비된 뒤(로드 직후)에 부른다.
	void CreateBodySetup(const FString& BodySetupPath = {});
	UMaterial* GetMaterial(uint32 SlotIndex) const;

public:
    FString SourceObjPath;
    FVertexBuffer* GetVertexBuffer() const { return VertexBuffer.get(); }
    FIndexBuffer* GetIndexBuffer() const { return IndexBuffer.get(); }
    // 버퍼를 다시 만들 수 있으므로 꺼낼 때마다 현재 버퍼로 맞춘다
    const FVertexFactory* GetVertexFactory() const
    {
        VertexFactory.Init(VertexBuffer.get(), IndexBuffer.get());
        return &VertexFactory;
    }
    void RebuildRenderData();
    const FBox& GetRenderBounds() const { return RenderBounds; }
    TSharedPtr<const FMeshRenderState> GetRenderState(const std::vector<UMaterial*>& Materials);
    void RegisterProxy(FPrimitiveSceneProxy* Proxy);
    void UnregisterProxy(FPrimitiveSceneProxy* Proxy);

    const uint16 SortID;
private:
    static inline uint16 NextSortID = 0;
    struct FMaterialSlotsHash
    {
        size_t operator()(const std::vector<UMaterial*>& Slots) const;
    };
    FBox RenderBounds;
    mutable FLocalVertexFactory VertexFactory;
    std::unordered_map<std::vector<UMaterial*>, TWeakPtr<const FMeshRenderState>, FMaterialSlotsHash> RenderStates;
    TArray<FPrimitiveSceneProxy*> RenderProxies;
	// BodySetup이 메시보다 먼저 지워질 때 BodySetup 소멸자가 부른다. 아직 그것을 가리키고 있으면 끊는다.
	friend class UBodySetup;
	void DetachBodySetup(UBodySetup* Setup);

	// 메시가 소유한다. 소멸자와 CreateBodySetup에서 delete한다.
	// TUniquePtr는 생성 코드가 헤더에서 불완전 타입을 지우려 해서 쓰지 못한다.
	UBodySetup* BodySetup = nullptr;
};
