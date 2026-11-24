#include "EnginePCH.h"
#include "PhysicsDebugDraw.h"
#include "Render/LineBatcher.h"
#include "Math/Box.h"
#include "Physics/PhysXConversions.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsScene.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include <algorithm>
#include <cmath>

static void AddWireArc(FLineBatcher* LineBatcher, const FVector& Center,
    const FVector& AxisA, const FVector& AxisB, float Radius,
    float StartRad, float EndRad, const FVector4& Color, int32 Segments = 24)
{
    auto PointAt = [&](float T) { return Center + AxisA * (cos(T) * Radius) + AxisB * (sin(T) * Radius); };
    FVector Prev = PointAt(StartRad);
    for (int32 i = 1; i <= Segments; ++i) {
        const FVector P = PointAt(StartRad + (EndRad - StartRad) * i / Segments);
        LineBatcher->AddLine(Prev, P, Color);
        Prev = P;
    }
}


static void DrawWireCapsule(FLineBatcher* Lines, const FVector& WorldPosition,
    const FQuat& WorldRotation, float R, float HalfLength, const FVector4& Color)
{
    const FVector Up = WorldRotation.RotateVector(FVector(0, 0, 1));
    const FVector X = WorldRotation.RotateVector(FVector(1, 0, 0));
    const FVector Y = WorldRotation.RotateVector(FVector(0, 1, 0));
    FVector A = WorldPosition + Up * HalfLength;
    FVector B = WorldPosition - Up * HalfLength;

    AddWireArc(Lines, A, X, Y, R, 0, 2 * PI, Color);    // 위 고리
    AddWireArc(Lines, B, X, Y, R, 0, 2 * PI, Color);    // 아래 고리
    AddWireArc(Lines, A, X, Up, R, 0, PI, Color);   // 위 반구
    AddWireArc(Lines, A, Y, Up, R, 0, PI, Color);   // 위 반구
    AddWireArc(Lines, B, X, Up, R, 0, -PI, Color);  // 아래 반구
    AddWireArc(Lines, B, Y, Up, R, 0, -PI, Color);  // 아래 반구

    for (const FVector& Side : { X, -X, Y, -Y }) {
        Lines->AddLine(A + Side * R, B + Side * R, Color);
    }
}

namespace FPhysicsDebugDraw {
    void DrawAggregateGeom(FLineBatcher* LineBatcher, const FKAggregateGeom& AggGeom,
        const FTransform& WorldTransform, const FVector4& Color) {

        if (!LineBatcher) return;
        const FVector SignedScale = WorldTransform.Scale;
        const FVector Scale(std::abs(SignedScale.X), std::abs(SignedScale.Y), std::abs(SignedScale.Z));
        const auto ScaledCenter = [&](const FVector& V) {
            return FVector(V.X * SignedScale.X, V.Y * SignedScale.Y, V.Z * SignedScale.Z);
        };
        constexpr float MinExtent = 0.01f; // Match BodyInstance::CreateShapes.
        for (auto& Sphere : AggGeom.SphereElems) {
            FVector WorldPosition = WorldTransform.Location + WorldTransform.Rotation.RotateVector(ScaledCenter(Sphere.Center));
            LineBatcher->AddSphere(WorldPosition, std::max(Sphere.Radius * std::min({ Scale.X, Scale.Y, Scale.Z }), MinExtent), Color);
        }


        for (auto& Box : AggGeom.BoxElems) {
            FVector Half(std::max(Box.X * Scale.X * 0.5f, MinExtent),
                std::max(Box.Y * Scale.Y * 0.5f, MinExtent), std::max(Box.Z * Scale.Z * 0.5f, MinExtent));
            FBox LocalBox = FBox{ -Half, Half };

            FVector WorldPosition = WorldTransform.Location + WorldTransform.Rotation.RotateVector(ScaledCenter(Box.Center));
            FQuat WorldRotation = WorldTransform.Rotation * Box.Rotation.Quaternion();
            FTransform BoxTransform(WorldRotation, WorldPosition);

            LineBatcher->AddOrientedBox(LocalBox, BoxTransform.GetLocalMatrix(), Color);
        }


        for (auto& Sphyl : AggGeom.SphylElems) {
            FVector WorldPosition = WorldTransform.Location + WorldTransform.Rotation.RotateVector(ScaledCenter(Sphyl.Center));
            FQuat WorldRotation = WorldTransform.Rotation * Sphyl.Rotation.Quaternion();

            const float R = std::max(Sphyl.Radius * std::min(Scale.X, Scale.Y), MinExtent);
            const float HalfLength = std::max(Sphyl.Length * Scale.Z * 0.5f, MinExtent);
            DrawWireCapsule(LineBatcher, WorldPosition, WorldRotation, R, HalfLength, Color);
        }
    }

    void DrawActorShapes(FLineBatcher* Lines, const FPhysicsActorHandle& Handle, const FVector4& Color)
    {
        if (!Lines || !Handle.IsValid()) return;
        physx::PxShape* Shapes[32];
        const physx::PxU32 Count = Handle.Actor->getNbShapes();
        for (physx::PxU32 Offset = 0; Offset < Count;)
        {
            const physx::PxU32 Read = Handle.Actor->getShapes(Shapes, 32, Offset);
            if (!Read) break;
            for (physx::PxU32 I = 0; I < Read; ++I)
            {
                const physx::PxTransform Pose = Handle.Actor->getGlobalPose() * Shapes[I]->getLocalPose();
                const FTransform Transform(P2UQuat(Pose.q), P2UVector(Pose.p));
                physx::PxSphereGeometry Sphere;
                physx::PxBoxGeometry Box;
                physx::PxCapsuleGeometry Capsule;
                if (Shapes[I]->getSphereGeometry(Sphere))
                {
                    Lines->AddSphere(Transform.Location, Sphere.radius, Color);
                }
                else if (Shapes[I]->getBoxGeometry(Box))
                {
                    const FVector Half = P2UVector(Box.halfExtents);
                    Lines->AddOrientedBox(FBox{ -Half, Half }, Transform.GetLocalMatrix(), Color);
                }
                else if (Shapes[I]->getCapsuleGeometry(Capsule))
                {
                    // Actual PhysX capsules use X; AggregateGeom uses Z.
                    const physx::PxQuat ZToX(physx::PxHalfPi, physx::PxVec3(0, 1, 0));
                    DrawWireCapsule(Lines, Transform.Location, P2UQuat(Pose.q * ZToX),
                        Capsule.radius, Capsule.halfHeight, Color);
                }
            }
            Offset += Read;
        }
    }

    void DrawWorldBodies(UWorld* World, FLineBatcher* Lines)
    {
        if (!World || !Lines) return;
        if (const FPhysicsScene* Scene = World->GetPhysicsScene())
        {
            // Includes ragdoll aggregate bodies and their test floor once.
            Scene->DrawDebugBodies(Lines);
            return;
        }
        // Editor worlds preview the same setup/fallback as CreatePhysicsState.
        for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
        {
            const UPrimitiveComponent* Component = *It;
            if (Component->GetWorld() != World || !Component->IsRegistered()) continue;
            const UBodySetup* Setup = Component->GetBodySetup();
            FKAggregateGeom Fallback;
            if (!Setup)
            {
                if (!Component->bSimulatePhysics && !Component->bBlockComponent) continue;
                const FBox Bounds = Component->CalcLocalBounds();
                const FVector Size = Bounds.Max - Bounds.Min;
                if (Size.X <= 0 && Size.Y <= 0 && Size.Z <= 0) continue;
                FKBoxElem Box;
                Box.Center = (Bounds.Min + Bounds.Max) * 0.5f;
                Box.X = Size.X; Box.Y = Size.Y; Box.Z = Size.Z;
                Fallback.BoxElems.Add(Box);
            }
            const FTransform Transform(Component->GetWorldLocation(), Component->GetWorldScale3D(),
                Component->GetWorldRotation().Quaternion());
            DrawAggregateGeom(Lines, Setup ? Setup->AggGeom : Fallback, Transform,
                FVector4(0.3f, 0.9f, 1.0f, 1.0f));
        }
    }
}
