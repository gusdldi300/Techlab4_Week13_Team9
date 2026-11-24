#pragma once

#include "Physics/ShapeElem.h"
#include "Render/Buffer.h"
#include "Render/PipelineState.h"
#include "Containers/Array.h"
#include "Collision/Ray.h"

struct FKAggregateGeom;
struct FSceneView;

struct FPhysicsShapeSelection
{
    int32 Bone = -1;
    ECollisionShape Type = ECollisionShape::Unknown;
    int32 Index = -1;
    bool operator==(const FPhysicsShapeSelection&) const = default;
};

// Rendering and picking share exactly the same world-space surface.
struct FPhysicsPreviewShape
{
    FPhysicsShapeSelection Selection;
    FTransform Pose;
    FVector HalfExtent;
    float Radius = 0;
    float HalfLength = 0;
};

class FPhysicsAssetPreview
{
public:
    static void AppendShapes(TArray<FPhysicsPreviewShape>& Out, int32 Bone,
        const FKAggregateGeom& Geometry, const FTransform& BoneWorld);
    static FPhysicsShapeSelection Pick(const TArray<FPhysicsPreviewShape>& Shapes, const FRay& Ray);
    void Render(const TArray<FPhysicsPreviewShape>& Shapes, const FPhysicsShapeSelection& Selected,
        const FSceneView& View, float Opacity);

private:
    TUniquePtr<FVertexBuffer> Vertices;
    TUniquePtr<FConstantBuffer> Constants;
    uint32 Capacity = 0;
};
