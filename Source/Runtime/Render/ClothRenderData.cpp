#include "EnginePCH.h"
#include "ClothRenderData.h"

#include "Render/Buffer.h"
#include "Render/RenderCommand.h"

#include <cstring>

FClothRenderData::FClothRenderData() = default;
FClothRenderData::~FClothRenderData() = default;

void FClothRenderData::Build(const TArray<FVector2>& UVs, const TArray<uint32>& InRenderToParticle, const TArray<uint32>& RenderIndices)
{
	NumRenderVertices = static_cast<uint32>(UVs.Num());
	RenderToParticle = InRenderToParticle;
	FrontIndices = RenderIndices;
	IndexCount = 0;
	NumParticles = 0;
	VertexBuffer.reset();
	IndexBuffer.reset();
	VertexFactory.Init(nullptr, nullptr);

	if (NumRenderVertices < 3 || RenderToParticle.Num() != UVs.Num() || RenderIndices.Num() < 3) return;
	for (uint32 Particle : RenderToParticle) NumParticles = std::max(NumParticles, Particle + 1);

	// UV와 색은 구성이 같으면 변하지 않으므로 여기서 한 번만 채운다.
	Vertices.SetNum(static_cast<int32>(NumRenderVertices * 2));
	for (uint32 i = 0; i < NumRenderVertices; ++i)
	{
		Vertices[i].UV = UVs[i];
		Vertices[i + NumRenderVertices].UV = UVs[i];
	}

	// 뒷면은 같은 삼각형을 감기 순서만 바꿔서 붙인다 (a, b, c) → (a', c', b').
	TArray<uint32> AllIndices;
	AllIndices.Reserve(RenderIndices.Num() * 2);
	for (uint32 Index : RenderIndices) AllIndices.Add(Index);
	for (int32 i = 0; i + 2 < RenderIndices.Num(); i += 3)
	{
		AllIndices.Add(RenderIndices[i] + NumRenderVertices);
		AllIndices.Add(RenderIndices[i + 2] + NumRenderVertices);
		AllIndices.Add(RenderIndices[i + 1] + NumRenderVertices);
	}

	IndexBuffer = RenderCommand::CreateStaticIndexBuffer(AllIndices.GetData(), static_cast<uint32>(AllIndices.Num()));
	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FVertex) * NumRenderVertices * 2, sizeof(FVertex));
	if (!IndexBuffer || !VertexBuffer)
	{
		KRS_LOG(Error, "[Cloth] render buffer creation failed");
		IndexBuffer.reset();
		VertexBuffer.reset();
		return;
	}
	VertexFactory.Init(VertexBuffer.get(), IndexBuffer.get());
	IndexCount = static_cast<uint32>(AllIndices.Num());
}

void FClothRenderData::Update(const TArray<FVector>& InPositions, const FVector& InvScale)
{
	if (!IsValid() || static_cast<uint32>(InPositions.Num()) < NumParticles) return;

	// 1. 위치: 렌더 정점마다 대응하는 입자 위치를 가져와 스케일을 상쇄한다.
	for (uint32 i = 0; i < NumRenderVertices; ++i)
	{
		const FVector& P = InPositions[RenderToParticle[i]];
		Vertices[i].Position = FVector(P.X * InvScale.X, P.Y * InvScale.Y, P.Z * InvScale.Z);
	}

	// 2. 노멀: 입자에 닿은 삼각형 면 노멀을 더해 정규화한다. 엔진 규칙은 Cross(B - A, C - A)가 앞면 방향이다.
	//    입자 기준으로 모으므로 UV 경계에서 쪼개진 렌더 정점도 같은 노멀을 받아 이음매가 보이지 않는다.
	// 3. 탄젠트: 삼각형마다 UV 변화량으로 U·V 방향을 구해 렌더 정점에 모은다(UV 경계 양쪽은 UV가 다르므로 따로 모은다).
	ParticleNormals.SetNum(static_cast<int32>(NumParticles));
	Tangents.SetNum(static_cast<int32>(NumRenderVertices));
	Bitangents.SetNum(static_cast<int32>(NumRenderVertices));
	for (FVector& N : ParticleNormals) N = FVector(0.0f, 0.0f, 0.0f);
	for (FVector& T : Tangents) T = FVector(0.0f, 0.0f, 0.0f);
	for (FVector& B : Bitangents) B = FVector(0.0f, 0.0f, 0.0f);

	for (int32 i = 0; i + 2 < FrontIndices.Num(); i += 3)
	{
		const uint32 A = FrontIndices[i], B = FrontIndices[i + 1], C = FrontIndices[i + 2];
		const FVector& PA = Vertices[A].Position;
		const FVector E1 = Vertices[B].Position - PA;
		const FVector E2 = Vertices[C].Position - PA;
		const FVector FaceNormal = FVector::Cross(E1, E2);
		ParticleNormals[RenderToParticle[A]] += FaceNormal;
		ParticleNormals[RenderToParticle[B]] += FaceNormal;
		ParticleNormals[RenderToParticle[C]] += FaceNormal;

		const FVector2 D1 = Vertices[B].UV - Vertices[A].UV;
		const FVector2 D2 = Vertices[C].UV - Vertices[A].UV;
		const float Det = D1.X * D2.Y - D2.X * D1.Y;
		if (std::abs(Det) < 1e-12f) continue;   // UV가 한 점·한 선으로 뭉친 삼각형은 방향을 정할 수 없다
		const float R = 1.0f / Det;
		const FVector T = (E1 * D2.Y - E2 * D1.Y) * R;
		const FVector Bt = (E2 * D1.X - E1 * D2.X) * R;
		Tangents[A] += T; Tangents[B] += T; Tangents[C] += T;
		Bitangents[A] += Bt; Bitangents[B] += Bt; Bitangents[C] += Bt;
	}

	for (uint32 i = 0; i < NumRenderVertices; ++i)
	{
		FVector N = ParticleNormals[RenderToParticle[i]];
		N = N.Length() > 1e-8f ? N.Normalized() : FVector(0.0f, 0.0f, 1.0f);

		// 노멀에 수직이 되게 U 방향을 다듬는다. UV가 없으면 노멀에 수직인 아무 축이나 쓴다.
		FVector T = Tangents[i] - N * N.Dot(Tangents[i]);
		if (T.Length() <= 1e-8f)
		{
			const FVector Axis = std::abs(N.Z) < 0.9f ? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
			T = FVector::Cross(Axis, N);
		}
		T = T.Normalized();
		const float Handedness = FVector::Cross(N, T).Dot(Bitangents[i]) < 0.0f ? -1.0f : 1.0f;

		FVertex& Front = Vertices[i];
		Front.Normal = N;
		Front.Tangent = FVector4(T.X, T.Y, T.Z, Handedness);

		// 뒷면: 노멀을 뒤집으면 Cross(N, T)도 뒤집히므로 비탄젠트 방향을 지키려고 부호도 뒤집는다.
		FVertex& Back = Vertices[i + NumRenderVertices];
		Back.Position = Front.Position;
		Back.Normal = N * -1.0f;
		Back.Tangent = FVector4(T.X, T.Y, T.Z, -Handedness);
	}

	void* Dst = RenderCommand::MapWriteDiscard(VertexBuffer.get());
	if (!Dst) return;
	std::memcpy(Dst, Vertices.GetData(), sizeof(FVertex) * Vertices.Num());
	RenderCommand::Unmap(VertexBuffer.get());
}
