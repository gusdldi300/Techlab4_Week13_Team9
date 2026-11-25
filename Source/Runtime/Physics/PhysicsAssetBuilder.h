#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"

class USkeletalMesh;
class UPhysicsAsset;

namespace PhysicsAssetBuilder
{
    struct FAutoGenerateSettings
    {
        bool bHumanoid = true;
        float MinVertexWeight = 0.35f;
        float TotalMassKg = 60.0f;
        // Relative to the longest bone segment, so imported mesh units do not matter.
        float MinBoneLengthRatio = 0.05f;
        float RadiusRatio = 0.15f;
    };

    // Returns an unattached asset. The caller owns it until assigning it to the mesh.
    UPhysicsAsset* CreateFromSkeleton(USkeletalMesh& Mesh, const FAutoGenerateSettings& Settings, FString& Error);
    void DestroyGeneratedAsset(UPhysicsAsset* Asset);
    UPhysicsAsset* CreateTwoBodyTestAsset(USkeletalMesh& Mesh, int32 ParentBoneIndex, int32 ChildBoneIndex);
}
