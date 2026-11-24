#include "EnginePCH.h"
#include "PhysicsAssetPreview.h"
#include "Physics/AggregateGeom.h"
#include "Render/StaticMeshData.h"
#include "Render/SceneView.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include <algorithm>
#include <cmath>

namespace
{
    struct FSurfaceTriangle
    {
        FVector P[3], N[3];
    };

    template<typename Emit>
    void Surface(const FPhysicsPreviewShape& Shape, Emit&& Triangle)
    {
        const auto Add = [&](FVector A, FVector B, FVector C, FVector NA, FVector NB, FVector NC) {
            FSurfaceTriangle T;
            const FVector P[] = { A, B, C }, N[] = { NA, NB, NC };
            for (int I = 0; I < 3; ++I)
            {
                T.P[I] = Shape.Pose.Location + Shape.Pose.Rotation.RotateVector(P[I]);
                T.N[I] = Shape.Pose.Rotation.RotateVector(N[I]);
            }
            if ((T.P[1] - T.P[0]).Cross(T.P[2] - T.P[0]).SizeSquared() > 1e-16f) Triangle(T);
        };
        if (Shape.Selection.Type == ECollisionShape::Box)
        {
            const FVector H = Shape.HalfExtent;
            const FVector Axes[] = { FVector(1,0,0), FVector(0,1,0), FVector(0,0,1) };
            const float E[] = { H.X, H.Y, H.Z };
            for (int Axis = 0; Axis < 3; ++Axis)
                for (int Sign : { -1, 1 })
                {
                    const FVector N = Axes[Axis] * static_cast<float>(Sign);
                    const FVector Center = N * E[Axis];
                    const FVector U = Axes[(Axis + 1) % 3] * E[(Axis + 1) % 3];
                    const FVector V = Axes[(Axis + 2) % 3] * E[(Axis + 2) % 3];
                    Add(Center-U-V, Center+U-V, Center+U+V, N,N,N);
                    Add(Center-U-V, Center+U+V, Center-U+V, N,N,N);
                }
            return;
        }
        constexpr int Sides = 32, HemiRings = 8;
        const bool Capsule = Shape.Selection.Type == ECollisionShape::Sphyl;
        // Separate equators produce a cylinder between two true hemispheres, including Length == 0.
        const int Rings = Capsule ? 2 * HemiRings + 1 : 2 * HemiRings;
        const auto Vertex = [&](int Ring, int Side, FVector& Position, FVector& Normal) {
            float Latitude, Offset = 0;
            if (Capsule)
            {
                const bool Upper = Ring > HemiRings;
                Latitude = Upper ? (Ring-HemiRings-1) * (PI*0.5f/HemiRings)
                    : -PI*0.5f + Ring * (PI*0.5f/HemiRings);
                Offset = Upper ? Shape.HalfLength : -Shape.HalfLength;
            }
            else Latitude = -PI*0.5f + Ring * (PI / (2*HemiRings));
            const float Longitude = Side * (2*PI/Sides);
            Normal = FVector(cosf(Latitude)*cosf(Longitude), cosf(Latitude)*sinf(Longitude), sinf(Latitude));
            Position = Normal * Shape.Radius + FVector(0,0,Offset);
        };
        for (int Ring = 0; Ring < Rings; ++Ring)
            for (int Side = 0; Side < Sides; ++Side)
            {
                FVector A,B,C,D,NA,NB,NC,ND;
                Vertex(Ring,Side,A,NA); Vertex(Ring,Side+1,B,NB);
                Vertex(Ring+1,Side+1,C,NC); Vertex(Ring+1,Side,D,ND);
                Add(A,B,C,NA,NB,NC); Add(A,C,D,NA,NC,ND);
            }
    }
}

void FPhysicsAssetPreview::AppendShapes(TArray<FPhysicsPreviewShape>& Out, int32 Bone,
    const FKAggregateGeom& Geometry, const FTransform& BoneWorld)
{
    const FVector S(std::abs(BoneWorld.Scale.X), std::abs(BoneWorld.Scale.Y), std::abs(BoneWorld.Scale.Z));
    const auto Add = [&](ECollisionShape Type, int Index, const FTransform& Local) -> FPhysicsPreviewShape& {
        FPhysicsPreviewShape Shape;
        Shape.Selection = { Bone, Type, Index };
        Shape.Pose = FTransform((BoneWorld.Rotation * Local.Rotation).Normalized(),
            BoneWorld.GetLocalMatrix().TransformPosition(Local.Location));
        Out.Add(Shape);
        return Out.Last();
    };
    // Same signed centers and dimension scaling as BodyInstance/PhysicsDebugDraw.
    for (int I=0; I<Geometry.SphereElems.Num(); ++I)
    {
        const auto& E = Geometry.SphereElems[I];
        Add(ECollisionShape::Sphere,I,E.GetTransform()).Radius = std::max(E.Radius * std::min({S.X,S.Y,S.Z}),0.01f);
    }
    for (int I=0; I<Geometry.BoxElems.Num(); ++I)
    {
        const auto& E = Geometry.BoxElems[I];
        Add(ECollisionShape::Box,I,E.GetTransform()).HalfExtent = FVector(
            std::max(E.X*S.X*0.5f,0.01f),std::max(E.Y*S.Y*0.5f,0.01f),std::max(E.Z*S.Z*0.5f,0.01f));
    }
    for (int I=0; I<Geometry.SphylElems.Num(); ++I)
    {
        const auto& E = Geometry.SphylElems[I];
        auto& Shape = Add(ECollisionShape::Sphyl,I,E.GetTransform());
        Shape.Radius = std::max(E.Radius * std::min(S.X,S.Y),0.01f);
        Shape.HalfLength = std::max(E.Length*S.Z*0.5f,0.01f);
    }
}

FPhysicsShapeSelection FPhysicsAssetPreview::Pick(const TArray<FPhysicsPreviewShape>& Shapes, const FRay& Ray)
{
    FPhysicsShapeSelection Hit;
    float Nearest = FLT_MAX;
    for (const auto& Shape : Shapes)
        Surface(Shape, [&](const FSurfaceTriangle& T) {
            float Distance;
            if (RayIntersectsTriangle(Ray,T.P[0],T.P[1],T.P[2],Distance) && Distance >= 0 && Distance < Nearest)
            {
                Nearest = Distance;
                Hit = Shape.Selection;
            }
        });
    return Hit;
}

void FPhysicsAssetPreview::Render(const TArray<FPhysicsPreviewShape>& Shapes,
    const FPhysicsShapeSelection& Selected, const FSceneView& View, float Opacity)
{
    struct FDrawTriangle { FLineBatchVertex V[3]; float Depth; };
    TArray<FDrawTriangle> Triangles;
    const FVector Light = FVector(-0.4f,-0.6f,1).Normalized();
    for (const auto& Shape : Shapes)
    {
        const bool bSelected = Shape.Selection == Selected;
        const FVector4 Color = bSelected ? FVector4(0.1f,0.7f,1,Opacity*1.6f) : FVector4(0.55f,0.4f,0.9f,Opacity);
        Surface(Shape, [&](const FSurfaceTriangle& T) {
            FDrawTriangle Draw;
            FVector Center = (T.P[0]+T.P[1]+T.P[2])/3.0f;
            Draw.Depth = (Center-View.ViewLocation).Dot(View.ViewForward);
            for (int I=0; I<3; ++I)
            {
                const float Shade = 0.5f + 0.5f * std::abs(T.N[I].Dot(Light));
                Draw.V[I] = {T.P[I], FVector2(), FVector4(Color.X*Shade,Color.Y*Shade,Color.Z*Shade,std::min(Color.W,0.8f))};
            }
            Triangles.Add(Draw);
        });
    }
    if (Triangles.IsEmpty()) return;
    std::stable_sort(Triangles.begin(),Triangles.end(),[](const auto& A,const auto& B){ return A.Depth > B.Depth; });
    TArray<FLineBatchVertex> Data;
    Data.Reserve(Triangles.Num()*3);
    for (const auto& T : Triangles) for (const auto& V : T.V) Data.Add(V);
    if (Capacity < static_cast<uint32>(Data.Num()))
    {
        Capacity = static_cast<uint32>(Data.Num());
        Vertices = RenderCommand::CreateDynamicVertexBuffer(Capacity*sizeof(FLineBatchVertex),sizeof(FLineBatchVertex));
    }
    if (!Constants) Constants = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));
    FPipelineState State;
    State.Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/BoundingBoxShader.hlsl");
    State.Topology = EPrimitiveTopology::TriangleList;
    State.RasterizerState = ERasterizerState::SolidNone;
    State.BlendState = EBlendState::AlphaBlend;
    // Editor overlay: show the complete collision surface through the preview mesh, without depth writes.
    State.DepthStencilState = EDepthStencilState::Disabled;
    RenderCommand::UpdateBufferData(Vertices.get(),Data.GetData(),Data.Num()*sizeof(FLineBatchVertex));
    RenderCommand::UpdateBufferData(Constants.get(),&View.ViewProjectionMatrix,sizeof(FMatrix));
    RenderCommand::BindPipelineState(State);
    RenderCommand::BindVertexBuffer(Vertices.get());
    RenderCommand::BindConstantBuffer(0,Constants.get(),EShaderBindFlagBits::Vertex);
    RenderCommand::Draw(Data.Num());
}
