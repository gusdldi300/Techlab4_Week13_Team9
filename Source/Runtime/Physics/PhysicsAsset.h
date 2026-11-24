#pragma once

#include "UObject/Object.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Core/NameTypes.h"

class UBodySetup;
class UPhysicsConstraintTemplate;

#include "PhysicsAsset.generated.h"

UCLASS()
class UPhysicsAsset : public UObject{
	GENERATED_BODY()

public:
	~UPhysicsAsset() override;
	int32 FindBodyIndex(FName BoneName) const;
	void UpdateBodySetupIndexMap();
	void DisableCollision(int32 BodyA, int32 BodyB);
	bool IsCollisionEnabled(int32 BodyA, int32 BodyB) const;

public:
	TArray <UBodySetup*> BodySetups;
	TMap <FName, int32, FNameHash> BonesSetupIndexMap;

	TArray <UPhysicsConstraintTemplate*> PhysicsConstraint;	
	TSet <uint64> CollisionDisableTable;	// 충돌무시 셋!
};
