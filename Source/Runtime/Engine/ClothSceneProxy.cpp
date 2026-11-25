#include "EnginePCH.h"
#include "ClothSceneProxy.h"

#include "Components/ClothComponent.h"
#include "Render/ClothRenderData.h"

FClothSceneProxy::FClothSceneProxy(UClothComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
}

void FClothSceneProxy::UpdateRenderState()
{
	RenderState.reset();
	UpdateCommonFlags();

	UClothComponent* Cloth = static_cast<UClothComponent*>(GetComponent());
	UMaterial* Material = Cloth->GetMaterial(0);
	const uint32 IndexCount = Cloth->GetRenderIndexCount();
	if (!Material || IndexCount == 0) return;

	// 그림자 패스는 RenderState가 있는 프록시만 Caster로 받으므로 섹션 하나를 만들어 둔다.
	auto State = std::make_shared<FMeshRenderState>();
	State->Sections.Add(FCachedMeshSection{ Material, 0, IndexCount });
	RenderState = State;
}

void FClothSceneProxy::GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View)
{
	if (!RenderState || RenderState->Sections.Num() == 0) return;

	UClothComponent* Cloth = static_cast<UClothComponent*>(GetComponent());
	// 이번 프레임에 입자가 바뀌었으면 여기서 한 번만 GPU에 올린다. 같은 프레임의 다른 뷰·그림자 패스는 그대로 쓴다.
	const FClothRenderData* Data = Cloth->PrepareRenderData();
	if (!Data || !Data->IsValid()) return;

	const FCachedMeshSection& Section = GetSection(0);
	const FVector Delta = GetBounds().Center - View.CameraPosition;

	FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
	Packet.Proxy = this;
	Packet.VertexFactory = Data->GetVertexFactory();
	Packet.Material = Section.Material;
	Packet.StartIndex = 0;
	Packet.IndexCount = Data->GetIndexCount();   // 그림자 패스는 0을 건너뛰므로 꼭 채운다
	Packet.CameraToParticleDistance = Delta.Dot(Delta);
}
