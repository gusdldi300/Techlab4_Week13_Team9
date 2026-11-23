#pragma once

#include "AggregateGeom.h"
#include "BodySetup.generated.h"

class UPhysicalMaterial;

enum class EPhysicsType : uint8 {
	Default, Kinematic, Simulated
};

UCLASS()
class UBodySetup : public UObject{
	GENERATED_BODY()

public:
	float CalculateMass() const;

	// 모양(AggGeom)과 PhysicsType을 저장·복원한다. 맨 앞에 형식 버전을 써서 옛 파일을 구분한다.
	// PhysMaterial은 저장하지 않는다. 재질 에셋을 경로로 찾는 체계가 생기면 그때 경로를 함께 저장한다.
	using Super::Serialize;
	void Serialize(FArchive& Ar) override;

	// 바이너리 파일로 읽고 쓴다. 메시 옆에 "<메시 경로>.bodysetup"으로 둔다.
	bool LoadFromFile(const FString& Path);
	bool SaveToFile(const FString& Path);

public:
	FKAggregateGeom AggGeom;
	UPhysicalMaterial* PhysMaterial = nullptr;
	FName BoneName;	// 이 바디는 어느뼈에 붙을것인가?
	EPhysicsType PhysicsType = EPhysicsType::Default;
};