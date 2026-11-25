#pragma once

#include "SceneComponent.h"
#include "../Render/Shader.h"
#include "../Render/Mesh.h"
#include "Render/RenderPacket.h"
#include "Render/GeometryGenerator.h"
#include "Collision/HitResult.h"
#include "Core/Delegate/Delegate.h"
#include "Physics/BodyInstance.h"
#include "Physics/CollisionChannel.h"
#include "PrimitiveComponent.generated.h"

class FPrimitiveSceneProxy;
class AActor;
class UPrimitiveComponent;

// (Self, Other, OtherComp). 담당: C
DECLARE_DELEGATE(FComponentOverlapSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*);
DECLARE_DELEGATE(FComponentHitSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*, const FHitResult&);

struct FOverlapInfo
{
	UPrimitiveComponent* OtherComp = nullptr;
};

enum class EPrimitiveType
{
	Sphere,
	Cube,
	Cone,
	Plane
};

UCLASS()
class UPrimitiveComponent :public USceneComponent
{
	GENERATED_BODY()

	// Material은 여기서 등록하지 않는다. 컴포넌트마다 머티리얼을 다루는 방식이 달라서
	// (빌보드는 단일 머티리얼, 스태틱 메시는 슬롯별 덮어쓰기) 각자 등록한다.
public:
	UPrimitiveComponent();
	virtual ~UPrimitiveComponent();

	virtual void BeginPlay() override;

	// Todo: subuv
	//void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue);
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext);

	virtual const FStaticMeshData* GetMeshData() const { return nullptr; }

	// Outline처럼 컴포넌트를 통째로 한 번 더 그릴 때 쓰는 GPU 메시.
	// 메시가 없거나(텍스트) View에 따라 형상이 정해지는 컴포넌트(빌보드·파티클)는
	// 컴포넌트 월드 행렬만으로 같은 그림을 못 만들므로 nullptr을 돌려준다.
	virtual UStaticMesh* GetRenderMesh() const { return nullptr; }

	virtual int32 GetNumMaterials() const { return 0; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const { return nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) {}
	//
	// FShader* GetShader() const { return Shader.get(); };

	void SetVisible(bool bInVisible) { SetVisibility(bInVisible); }

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit);
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT);
	virtual FBox CalcLocalBounds() const override
	{
		const FStaticMeshData* Data = GetMeshData();
		return Data ? Data->AABB : Super::CalcLocalBounds();
	}

	virtual FPrimitiveSceneProxy* CreateSceneProxy();
	FPrimitiveSceneProxy* GetSceneProxy() const { return SceneProxy; }

	void MarkRenderStateDirty() override;

	virtual void OnTransformDirty() override;

	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr) override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	UPROPERTY(EditAnywhere)
	bool bCastShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceiveShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceivesDecals = true;

	// ---- 충돌 이벤트 (담당: C) ----
	FComponentOverlapSignature OnComponentBeginOverlap;
	FComponentOverlapSignature OnComponentEndOverlap;
	FComponentHitSignature OnComponentHit;

	UPROPERTY(EditAnywhere)
	bool bGenerateOverlapEvents = true;
	UPROPERTY(EditAnywhere)
	bool bBlockComponent = false;

	UPROPERTY(EditAnywhere)
	ECollisionChannel ObjectType = ECollisionChannel::WorldStatic;	// 내 타입

	UPROPERTY(EditAnywhere)
	FCollisionResponseContainer CollisionResponses;					// 채널마다 어떻게 설정?

	// ---- 물리 바디 (담당: A) ----
	// true면 PhysX가 움직이는 Dynamic 바디를 만든다.
	// false여도 bBlockComponent가 true면 움직이지 않는 Static 바디(바닥, 벽)를 만든다.
	UPROPERTY(EditAnywhere)
	bool bSimulatePhysics = false;

	// 이 컴포넌트의 물리 모양 설계도. 메시 에셋이 소유하고 컴포넌트는 빌려 쓰기만 한다.
	// 기본은 없음(null)이고, 없으면 메시 로컬 바운드 박스 하나로 바디를 만든다.
	// 메시 에셋에 BodySetup이 생기면 메시 컴포넌트가 오버라이드해서 그것을 돌려준다.
	virtual const UBodySetup* GetBodySetup() const { return nullptr; }

	FBodyInstance& GetBodyInstance() { return BodyInstance; }
	const FBodyInstance& GetBodyInstance() const { return BodyInstance; }

	bool IsOverlappingComponent(const UPrimitiveComponent* Other) const;
	const TArray<FOverlapInfo>& GetOverlapInfos() const { return OverlapInfos; }

	// UWorld::UpdateOverlaps가 겹침 상태가 바뀐 프레임에 호출한다
	void BeginComponentOverlap(UPrimitiveComponent* OtherComp);
	void EndComponentOverlap(UPrimitiveComponent* OtherComp);
	void DispatchHit(UPrimitiveComponent* OtherComp, const FHitResult & HitResult);

	// 등록된 뒤 위치나 설정이 바뀌었을 때 바디를 지금 상태로 다시 만든다. (언리얼의 RecreatePhysicsState)
	// 씬 파일 로드처럼 등록(바디 생성)이 저장된 Transform을 읽기 전에 일어나는 경우에 쓴다.
	void RecreatePhysicsState();

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	// 컴포넌트가 월드에 등록,해제될 때 물리 바디를 만들고 지운다. (언리얼의 CreatePhysicsState / DestroyPhysicsState)
	virtual bool ShouldCreatePhysicsState() const;
	void CreatePhysicsState();
	void DestroyPhysicsState();
	TArray<FOverlapInfo> OverlapInfos;

	virtual bool ShouldCreateRenderState() const override
	{
		return true;
	}

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;


	bool TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult);
	bool TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT);

private:
	friend class FScene;
	FPrimitiveSceneProxy* SceneProxy = nullptr;
	FBodyInstance BodyInstance;


};
