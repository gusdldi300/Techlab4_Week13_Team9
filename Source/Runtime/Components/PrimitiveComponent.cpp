#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"


#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Engine/World.h"

namespace
{
	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	//SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{

}

void UPrimitiveComponent::OnRegister()
{
	Super::OnRegister();
	CreatePhysicsState();
}

bool UPrimitiveComponent::ShouldCreatePhysicsState() const
{
	// 물리에 참여하겠다고 표시한 컴포넌트만 바디를 만든다.
	// 표시 없이 모든 프리미티브에 만들면 텍스트·빌보드 같은 장식까지 충돌 박스가 생긴다.
	return bSimulatePhysics || bBlockComponent;
}

void UPrimitiveComponent::CreatePhysicsState()
{
	UWorld* World = GetWorld();
	FPhysicsScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	if (!Scene || !ShouldCreatePhysicsState())
	{
		return;   // 에디터 월드에는 물리 Scene이 없다
	}

	FBodyInstanceInitParams Params;
	Params.WorldLocation    = GetWorldLocation();
	Params.WorldRotation    = GetWorldRotation().Quaternion();
	Params.Scale3D          = GetWorldScale3D();
	Params.bSimulatePhysics = bSimulatePhysics;

	const UBodySetup* Setup = GetBodySetup();
	if (!Setup)
	{
		// BodySetup이 없으면 메시 로컬 바운드를 박스 하나로 대신 쓴다. 스케일은 InitBody가 곱한다.
		// 메시가 없으면(로컬 바운드가 비어 있으면) 만들 모양이 없다.
		const FBox LocalBounds = CalcLocalBounds();
		const FVector LocalSize = LocalBounds.Max - LocalBounds.Min;
		if (LocalSize.X <= 0.0f && LocalSize.Y <= 0.0f && LocalSize.Z <= 0.0f)
		{
			return;
		}

		FKBoxElem Box;
		Box.Center = (LocalBounds.Min + LocalBounds.Max) * 0.5f;
		Box.X = LocalSize.X;   // FKBoxElem은 전체 크기다. 반 크기 변환은 InitBody가 한다.
		Box.Y = LocalSize.Y;
		Box.Z = LocalSize.Z;
		Params.FallbackGeom.BoxElems.Add(Box);
	}

	BodyInstance.InitBody(Setup, Params, this, Scene);
}

void UPrimitiveComponent::DestroyPhysicsState()
{
	BodyInstance.TermBody();
}

void UPrimitiveComponent::RecreatePhysicsState()
{
	if (!IsRegistered()) return;
	DestroyPhysicsState();
	CreatePhysicsState();
}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
	// Scene loading registers default components before their mesh/collision properties are read.
	// Create the missing body now that the saved geometry and transform are available.
	if (!BodyInstance.IsValidBodyInstance()) CreatePhysicsState();
}

void UPrimitiveComponent::OnUnregister()
{
	DestroyPhysicsState();
	Super::OnUnregister();
	// 삭제/등록 해제된 도형을 상대의 OverlapInfos에 남기지 않는다.
	while (!OverlapInfos.IsEmpty())
	{
		UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
		EndComponentOverlap(Other);
		if (Other) Other->EndComponentOverlap(this);
	}
}


void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

FPrimitiveSceneProxy* UPrimitiveComponent::CreateSceneProxy()
{
	return new FPrimitiveSceneProxy(this);
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	return Super::MoveComponent(Delta, NewRotation, bSweep, Hit);
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "bCastShadow" ||
		Property.Name == "bReceiveShadow" || 
		Property.Name == "bReceivesDecals")
	{
		MarkRenderStateDirty();
	}
}

void UPrimitiveComponent::SetCastShadow(bool NewValue)
{
	if (bCastShadow != NewValue)
	{
		bCastShadow = NewValue;
	}
}

void UPrimitiveComponent::SetReceiveShadow(bool NewValue)
{
	if (bReceiveShadow != NewValue)
	{
		bReceiveShadow = NewValue;
	}
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T)) return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}



bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* Other) const
{
	if (!Other) return false;

	for (const FOverlapInfo& Info : OverlapInfos)
	{
		if (Info.OtherComp == Other)
			return true;
	}

	return false;
}

void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp || OtherComp == this || IsOverlappingComponent(OtherComp))
	{
		return;
	}

	OverlapInfos.Add({ OtherComp });

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentBeginOverlap.Broadcast(this, OtherActor, OtherComp);
	if (Owner && OtherActor)
	{
		Owner->NotifyActorBeginOverlap(OtherActor);
	}
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) return;

	bool bRemoved = false;

	for (int32 Index = 0;Index < OverlapInfos.Num(); ++Index)
	{
		if (OverlapInfos[Index].OtherComp == OtherComp)
		{
			OverlapInfos.RemoveAt(Index, 1);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
		return;


	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentEndOverlap.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyActorEndOverlap(OtherActor);
	}
}

void UPrimitiveComponent::DispatchHit(UPrimitiveComponent* OtherComp, const FHitResult& HitResult)
{
	if (!OtherComp) 
		return;

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentHit.Broadcast(this, OtherActor, OtherComp, HitResult);

	if (Owner && OtherActor)
	{
		Owner->NotifyHit(OtherActor);
	}
}
