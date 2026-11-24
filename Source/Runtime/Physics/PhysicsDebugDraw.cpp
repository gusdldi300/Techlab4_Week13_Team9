#include "EnginePCH.h"
#include "PhysicsDebugDraw.h"
#include "Render/LineBatcher.h"
#include "Math/Box.h"

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

namespace FPhysicsDebugDraw {
	void DrawAggregateGeom(FLineBatcher* LineBatcher, const FKAggregateGeom& AggGeom,
		const FTransform& WorldTransform, const FVector4& Color) {

		for (auto& Sphere : AggGeom.SphereElems) {
			FVector WorldPosition = WorldTransform.Location + WorldTransform.Rotation.RotateVector(Sphere.Center);
			LineBatcher->AddSphere(WorldPosition, Sphere.Radius, Color);
		}


		for (auto& Box : AggGeom.BoxElems) {
			FVector Half = FVector(Box.X, Box.Y, Box.Z) * 0.5f;
			FBox LocalBox = FBox{ -Half, Half };

			FVector WorldPosition = WorldTransform.Location + WorldTransform.Rotation.RotateVector(Box.Center);
			FQuat WorldRotation = WorldTransform.Rotation * Box.Rotation.Quaternion();
			FTransform BoxTransform(WorldRotation, WorldPosition);

			LineBatcher->AddOrientedBox(LocalBox, BoxTransform.GetLocalMatrix(), Color);
		}


		for (auto& Sphyl : AggGeom.SphylElems) {
			FVector WorldPosition = WorldTransform.Location + WorldTransform.Rotation.RotateVector(Sphyl.Center);
			FQuat WorldRotation = WorldTransform.Rotation * Sphyl.Rotation.Quaternion();

			FVector Up = WorldRotation.RotateVector(FVector(0, 0, 1));
			FVector X = WorldRotation.RotateVector(FVector(1, 0, 0));
			FVector Y = WorldRotation.RotateVector(FVector(0, 1, 0));

			float R = Sphyl.Radius;

			FVector A = WorldPosition + Up * (Sphyl.Length * 0.5f);
			FVector B = WorldPosition - Up * (Sphyl.Length * 0.5f);

			AddWireArc(LineBatcher, A, X, Y, R, 0, 2 * PI, Color);	// 위 고리
			AddWireArc(LineBatcher, B, X, Y, R, 0, 2 * PI, Color);	// 아래 고리 
			AddWireArc(LineBatcher, A, X, Up, R, 0, PI, Color);	// 위 반구
			AddWireArc(LineBatcher, A, Y, Up, R, 0, PI, Color);	// 위 반구
			AddWireArc(LineBatcher, B, X, Up, R, 0, -PI, Color);	// 아래 반구
			AddWireArc(LineBatcher, B, Y, Up, R, 0, -PI, Color);	// 아래 반구 

			for (const FVector& Side : { X, -X, Y, -Y }) {
				LineBatcher->AddLine(A + Side * R, B + Side * R, Color);
			}
		}
	}
}
