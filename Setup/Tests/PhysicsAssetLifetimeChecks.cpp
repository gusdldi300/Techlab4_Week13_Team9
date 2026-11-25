#include "EnginePCH.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"
#include <cstdio>

int main()
{
    const int32 InitialCount = GUObjectArray.Num();
    for (int32 Iteration = 0; Iteration < 100; ++Iteration)
    {
        auto* Mesh = NewObject<USkeletalMesh>();
        auto* Asset = NewObject<UPhysicsAsset>(Mesh);
        Mesh->SetPhysicsAsset(Asset);
        delete Asset;
        if (Mesh->GetPhysicsAsset() != nullptr) return 1;
        delete Mesh;

        Mesh = NewObject<USkeletalMesh>();
        Asset = NewObject<UPhysicsAsset>(Mesh);
        Mesh->SetPhysicsAsset(Asset);
        delete Mesh;
        delete Asset; // The Outer address remains, but its object has already been freed.

        Mesh = NewObject<USkeletalMesh>();
        auto* PreviousAsset = NewObject<UPhysicsAsset>(Mesh);
        Asset = NewObject<UPhysicsAsset>(Mesh);
        Mesh->SetPhysicsAsset(Asset);
        delete PreviousAsset;
        if (Mesh->GetPhysicsAsset() != Asset) return 2;
        delete Asset;
        delete Mesh;

        auto* Outer = NewObject<UObject>();
        Asset = NewObject<UPhysicsAsset>(Outer);
        delete Asset;
        delete Outer;

        Outer = NewObject<UObject>();
        Asset = NewObject<UPhysicsAsset>(Outer);
        delete Outer;
        delete Asset;

        delete NewObject<UPhysicsAsset>();
    }
    if (GUObjectArray.Num() != InitialCount) return 3;
    std::printf("PASS: 600 physics asset lifetime scenarios; no registered objects leaked.\n");
    return 0;
}
