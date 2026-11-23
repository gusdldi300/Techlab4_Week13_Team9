#pragma once

#include "MeshComponent.h"
#include "StaticMeshComponent.generated.h"

UCLASS()
class UStaticMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UStaticMeshComponent();
	virtual ~UStaticMeshComponent();

	virtual void BeginPlay();
	virtual void TickComponent(float DeltaTime);

	virtual const FStaticMeshData* GetMeshData() const override { return StaticMesh ? &StaticMesh->GetMeshData() : nullptr; }

	// 메시가 바뀌면 슬롯 구성이 달라지므로 덮어쓰기를 비운다
	void SetStaticMesh(UStaticMesh* InStaticMesh);
	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	// 메시가 소유한 물리 설계도를 빌려준다. 메시가 없으면 null이고, 그때는 부모의 바운드 박스 대체 경로를 탄다.
	virtual const UBodySetup* GetBodySetup() const override;
	virtual UStaticMesh* GetRenderMesh() const override { return StaticMesh; }

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

	bool IsEditorIcon() const { return bEditorIcon; }
	void SetEditorIcon(bool IsEditorIcon) { bEditorIcon = IsEditorIcon; }

private:
	UPROPERTY(EditAnywhere)
	UStaticMesh* StaticMesh = nullptr;
	// Mesh의 Section별 Material과 Texture를 보존해 각각의 렌더 패킷으로 제출한다.
	//virtual void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue) override;

	// 조명 Icon Debug arrow용
	bool bEditorIcon = false;
};
