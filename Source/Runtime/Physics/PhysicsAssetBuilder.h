#pragma once

#include "Core/Types.h"

class USkeletalMesh;
class UPhysicsAsset;

namespace PhysicsAssetBuilder
{
    UPhysicsAsset* CreateTwoBodyTestAsset(USkeletalMesh& Mesh, int32 ParentBoneIndex, int32 ChildBoneIndex);
}