#include "EnginePCH.h"
#include "GameFramework/Actor/SonicCapeCharacter.h"
#include "GameFramework/Animation/SonicAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"
#include "Render/SkeletalMeshData.h"

namespace
{
	// 망토 로컬 축을 캐릭터 축에 맞추는 회전.
	// 천 기본(커튼) 배치는 로컬 XY 평면에 펼치고 Y = 0 줄을 고정한다. 이 회전으로
	//   로컬 X(망토 가로) → 캐릭터 오른쪽(+Y), 로컬 Y(고정 변에서 멀어지는 쪽) → 아래(−Z), 로컬 Z(면 법선) → 뒤(−X)
	// 가 되어, 윗변이 어깨에 매달리고 등 뒤로 세로로 늘어진 망토가 된다.
	// 위 세 축을 열로 둔 회전 행렬 [0 0 −1; 1 0 0; 0 −1 0]을 쿼터니언(x, y, z, w)으로 바꾼 값이다.
	const FQuat CapeBaseRotation(-0.5f, -0.5f, 0.5f, 0.5f);

	// 망토 메시. Blender에서 캐릭터 축(X 앞, Y 오른쪽, Z 위)으로 만들었고 원점이 윗변 가운데라 회전이 필요 없다.
	// 윗줄 정점이 빨간 정점 색이라 어깨선에 고정된다. 임포트 전이면 격자 망토로 대체한다.
	const char* CapeMeshPath = "FBX/SonicCape/SonicCape_Static.fbx.bin";
}

ASonicCapeCharacter::ASonicCapeCharacter()
{
	// ASonicCharacter와 같은 메시 배치: 믹사모 메시는 cm 단위라 0.01배, 원점이 발이라 캡슐 바닥까지 내린다
	CapsuleComponent->CapsuleHalfHeight = 0.9f;
	CapsuleComponent->CapsuleRadius = 0.35f;
	SkeletalMeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -0.9f), FRotator(0.0f, 0.0f, 0.0f));
	SkeletalMeshComponent->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

	CharacterMovementComponent->MaxWalkSpeed = 450.0f;
	CharacterMovementComponent->MaxAcceleration = 2400.0f;
	CharacterMovementComponent->bOrientRotationToMovement = true;
	CharacterMovementComponent->RotationRate = FRotator(0.0f, 720.0f, 0.0f);

	// 망토: 약 0.5m × 0.6m. 캡슐에 붙여 몸 방향(Yaw)을 따라 돌고, 위치는 Tick에서 등뼈를 따라간다.
	// 캡슐에 붙이는 이유: 스켈레탈 메시는 0.01배 스케일이라 상대 위치가 cm 단위가 되고, 뼈 회전은 클립마다 축이 달라 흔들린다.
	CapeComponent = CreateDefaultSubobject<UClothComponent>("CapeComponent");
	CapeComponent->SetupAttachment(CapsuleComponent);
	CapeComponent->SetRelativeLocationAndRotation(FVector(-0.25f, 0.0f, 0.5f), CapeBaseRotation.ToFRotator());
	CapeComponent->SourceMeshPath = CapeMeshPath;
	CapeComponent->bFlagLayout = false;
	CapeComponent->NumX = 11;            // 가로 정점 수 → 0.5m
	CapeComponent->NumY = 13;            // 세로 정점 수 → 0.6m
	CapeComponent->Spacing = 0.05f;
	CapeComponent->bPinTopEdge = true;
	CapeComponent->VerticalStiffness = 1.0f;
	CapeComponent->HorizontalStiffness = 1.0f;
	CapeComponent->BendStiffness = 0.3f;  // 낮을수록 잘 접힌다
	CapeComponent->Damping = 0.05f;
	CapeComponent->WindGust = 0.35f;
	CapeComponent->WindGustFrequency = 1.2f;
	CapeComponent->DragCoefficient = 0.6f;
	CapeComponent->LiftCoefficient = 0.5f;

	if (FPaths::HasProject())
	{
		if (USkeletalMesh* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>("FBX/Sonic_SadIdle/Sonic_SadIdle.fbx.bin"))
		{
			SkeletalMeshComponent->SetSkeletalMesh(Mesh);
		}
	}
}

void ASonicCapeCharacter::BeginPlay()
{
	// AMyCharacter::BeginPlay는 UMyAnimInstance를 끼우므로 건너뛰고, 소닉 클립 경로를 채운 인스턴스를 직접 만든다
	ACharacter::BeginPlay();

	// 씬에 저장된 이동 컴포넌트 값(다른 캐릭터에서 옮겨 온 설정)이 생성자 값을 덮을 수 있으므로 다시 맞춘다
	CharacterMovementComponent->MaxAcceleration = 2400.0f;
	CharacterMovementComponent->bOrientRotationToMovement = true;
	CharacterMovementComponent->RotationRate = FRotator(0.0f, 720.0f, 0.0f);

	if (SkeletalMeshComponent->GetAnimBlueprintPath().empty() && !Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
	{
		USonicAnimInstance* Instance = NewObject<USonicAnimInstance>(SkeletalMeshComponent);
		Instance->IdleAnimationPath = IdleAnimationPath;
		Instance->RunAnimationPath = RunAnimationPath;
		SkeletalMeshComponent->SetAnimInstance(Instance);
	}

	// 메시 망토는 캐릭터 축으로 만들었으니 회전 없이, 격자 망토(대체)는 커튼 배치를 세우는 회전으로 붙인다.
	// 씬 파일에 저장된 상대 회전이 생성자 값을 덮으므로 여기서 다시 맞춘다. 아직 시작 전이라 관성 튐이 없다.
	CapeComponent->SetRelativeRotation((CapeComponent->IsMeshCloth() ? FQuat::Identity() : CapeBaseRotation).ToFRotator());

	FindCapeBone();
	UpdateCape();
}

void ASonicCapeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (USonicAnimInstance* Anim = GetSonicAnimInstance())
	{
		// 달리기 클립은 제자리 포즈라 속도를 클립의 발 속도에 맞춰야 발이 덜 미끄러진다
		CharacterMovementComponent->MaxWalkSpeed = Anim->GetRunSpeed() * std::max(RunSpeedScale, 0.0f);

		const FVector Velocity = GetVelocity();
		const float HorizontalSpeedCm = FVector(Velocity.X, Velocity.Y, 0.0f).Size() * WorldToCm;
		Anim->SetWantsToRun(HorizontalSpeedCm > RunAnimThreshold);
	}

	UpdateCape();
}

void ASonicCapeCharacter::FindCapeBone()
{
	CapeBoneIndex = INDEX_NONE;
	if (CapeBoneName.empty()) return;

	// 믹사모 뼈 이름은 "mixamorig:Spine2"처럼 접두어가 붙으므로 일부만 맞으면 쓴다
	const TArray<FBoneInfo>& Bones = SkeletalMeshComponent->GetBones();
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (Bones[i].Name.find(CapeBoneName) != FString::npos)
		{
			CapeBoneIndex = i;
			break;
		}
	}
	if (CapeBoneIndex == INDEX_NONE)
	{
		KRS_LOG(Warning, "[SonicCape] bone containing '{}' not found. The cape stays at its capsule-relative position", CapeBoneName);
	}
}

void ASonicCapeCharacter::UpdateCape()
{
	if (!CapeComponent) return;

	// 1. 위치: 등뼈에서 등 뒤·위로 조금 뺀 곳에 윗변을 붙인다. 회전은 캡슐(몸 Yaw)을 그대로 따른다.
	//    천은 매 Tick 컴포넌트 위치로 로컬 공간을 옮기므로, 몸이 움직이면 이동량만큼 관성으로 끌려간다.
	if (CapeBoneIndex != INDEX_NONE)
	{
		FMatrix BoneMatrix = SkeletalMeshComponent->GetBoneWorldMatrix(CapeBoneIndex);
		const FVector4 Origin = BoneMatrix.GetOrigin();
		const FQuat BodyRotation = GetActorRotation().Quaternion();
		const FVector Location = FVector(Origin.X, Origin.Y, Origin.Z)
			- BodyRotation.GetForwardVector() * CapeBackOffset
			+ FVector(0.0f, 0.0f, CapeUpOffset);
		CapeComponent->SetWorldLocation(Location, false);
	}

	// 2. 바람: 천은 로컬 공간에서 시뮬레이션되므로 몸이 일정한 속도로 달리면 공기 저항이 생기지 않는다.
	//    공기가 몸 속도의 반대로 분다고 보고 맞바람을 넣어야 망토가 뒤로 날린다.
	CapeComponent->WindVelocity = AmbientWind - GetVelocity() * RunWindScale;
}

USonicAnimInstance* ASonicCapeCharacter::GetSonicAnimInstance() const
{
	return Cast<USonicAnimInstance>(SkeletalMeshComponent->GetAnimInstance());
}
