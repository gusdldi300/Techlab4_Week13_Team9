#include "EnginePCH.h"
#include "PhysicsAsset.h"
#include "BodySetup.h"
#include "Render/SkeletalMesh.h"

UPhysicsAsset::~UPhysicsAsset()
{
	// Shutdown can destroy the Outer first. Check registration before Cast reads its class.
	UObject* Outer = GetOuter();
	if (!Outer || !GUObjectArray.Contains(Outer)) return;
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Outer))
	{
		if (Mesh->GetPhysicsAsset() == this) Mesh->SetPhysicsAsset(nullptr);
	}
}


int32 UPhysicsAsset::FindBodyIndex(FName BoneName) const
{
	const int32 * FoundIndex = BonesSetupIndexMap.Find(BoneName);
	if (FoundIndex == nullptr) return INDEX_NONE;
	return *FoundIndex;
}
void UPhysicsAsset::UpdateBodySetupIndexMap()
{
	BonesSetupIndexMap.Empty();
	for (int32 i = 0; i < BodySetups.Num(); i++) {
		if (BodySetups[i] == nullptr) continue;

		BonesSetupIndexMap.Add(BodySetups[i]->BoneName , i);
	}
}


static uint64 MakeBodyPairKey(int32 A, int32 B) {
	uint64 Key = 0;
	if (A > B) std::swap(A, B);
	Key |= (uint64)A << 32;
	Key |= (uint64)B;
	return Key;
}


void UPhysicsAsset::DisableCollision(int32 BodyA, int32 BodyB)
{
	uint64 Key = MakeBodyPairKey(BodyA, BodyB);
	CollisionDisableTable.Add(Key);
}

bool UPhysicsAsset::IsCollisionEnabled(int32 BodyA, int32 BodyB) const
{
	uint64 Key = MakeBodyPairKey(BodyA, BodyB);
	return !CollisionDisableTable.Contains(Key);
}
