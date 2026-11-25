#include "EnginePCH.h"
#include "ClothRenderData.h"

#include "Render/Buffer.h"
#include "Render/RenderCommand.h"

#include <cstring>

FClothRenderData::FClothRenderData() = default;
FClothRenderData::~FClothRenderData() = default;

void FClothRenderData::Build(int32 InCountX, int32 InCountY, const TArray<uint32>& Indices, bool bFlagUV)
{
	CountX = InCountX;
	CountY = InCountY;
	NumParticles = static_cast<uint32>(CountX * CountY);
	bFlagLayoutUV = bFlagUV;
	FrontIndices = Indices;
	IndexCount = 0;
	VertexBuffer.reset();
	IndexBuffer.reset();
	VertexFactory.Init(nullptr, nullptr);

	if (CountX < 2 || CountY < 2 || Indices.Num() < 3) return;

	// UV와 색은 격자가 같으면 변하지 않으므로 여기서 한 번만 채운다.
	Vertices.SetNum(static_cast<int32>(NumParticles * 2));
	for (int32 Y = 0; Y < CountY; ++Y)
	{
		for (int32 X = 0; X < CountX; ++X)
		{
			const float U = bFlagUV ? static_cast<float>(Y) / (CountY - 1) : static_cast<float>(X) / (CountX - 1);
			const float V = bFlagUV ? static_cast<float>(X) / (CountX - 1) : static_cast<float>(Y) / (CountY - 1);
			const uint32 I = static_cast<uint32>(Y * CountX + X);
			Vertices[I].UV = FVector2(U, V);
			Vertices[I + NumParticles].UV = FVector2(U, V);
		}
	}

	// 뒷면은 같은 삼각형을 감기 순서만 바꿔서 붙인다 (a, b, c) → (a', c', b').
	TArray<uint32> AllIndices;
	AllIndices.Reserve(Indices.Num() * 2);
	for (uint32 Index : Indices) AllIndices.Add(Index);
	for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
	{
		AllIndices.Add(Indices[i] + NumParticles);
		AllIndices.Add(Indices[i + 2] + NumParticles);
		AllIndices.Add(Indices[i + 1] + NumParticles);
	}

	IndexBuffer = RenderCommand::CreateStaticIndexBuffer(AllIndices.GetData(), static_cast<uint32>(AllIndices.Num()));
	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FVertex) * NumParticles * 2, sizeof(FVertex));
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
	if (!IsValid() || static_cast<uint32>(InPositions.Num()) != NumParticles) return;

	Positions.SetNum(static_cast<int32>(NumParticles));
	for (uint32 i = 0; i < NumParticles; ++i)
	{
		const FVector& P = InPositions[i];
		Positions[i] = FVector(P.X * InvScale.X, P.Y * InvScale.Y, P.Z * InvScale.Z);
	}

	// 노멀: 정점에 닿은 삼각형 면 노멀을 더해 정규화한다. 엔진 규칙은 Cross(B - A, C - A)가 앞면 방향이다.
	Normals.SetNum(static_cast<int32>(NumParticles));
	for (FVector& N : Normals) N = FVector(0.0f, 0.0f, 0.0f);
	for (int32 i = 0; i + 2 < FrontIndices.Num(); i += 3)
	{
		const uint32 A = FrontIndices[i], B = FrontIndices[i + 1], C = FrontIndices[i + 2];
		const FVector FaceNormal = FVector::Cross(Positions[B] - Positions[A], Positions[C] - Positions[A]);
		Normals[A] += FaceNormal;
		Normals[B] += FaceNormal;
		Normals[C] += FaceNormal;
	}

	// 탄젠트는 U가 커지는 격자 방향, 비탄젠트 부호는 V가 커지는 방향으로 정한다.
	const auto Index = [this](int32 X, int32 Y) { return static_cast<uint32>(Y * CountX + X); };
	for (int32 Y = 0; Y < CountY; ++Y)
	{
		for (int32 X = 0; X < CountX; ++X)
		{
			const uint32 I = Index(X, Y);
			FVector N = Normals[I];
			N = N.Length() > 1e-8f ? N.Normalized() : FVector(0.0f, 0.0f, 1.0f);

			// 깃발 배치는 U가 격자 Y, V가 격자 X 방향이다.
			const FVector AlongX = Positions[Index(std::min(X + 1, CountX - 1), Y)] - Positions[Index(std::max(X - 1, 0), Y)];
			const FVector AlongY = Positions[Index(X, std::min(Y + 1, CountY - 1))] - Positions[Index(X, std::max(Y - 1, 0))];
			const FVector UDir = bFlagLayoutUV ? AlongY : AlongX;
			const FVector VDir = bFlagLayoutUV ? AlongX : AlongY;

			FVector T = UDir - N * N.Dot(UDir);
			T = T.Length() > 1e-8f ? T.Normalized() : FVector(1.0f, 0.0f, 0.0f);
			const float Handedness = FVector::Cross(N, T).Dot(VDir) < 0.0f ? -1.0f : 1.0f;

			FVertex& Front = Vertices[I];
			Front.Position = Positions[I];
			Front.Normal = N;
			Front.Tangent = FVector4(T.X, T.Y, T.Z, Handedness);

			// 뒷면: 노멀을 뒤집으면 Cross(N, T)도 뒤집히므로 비탄젠트 방향을 지키려고 부호도 뒤집는다.
			FVertex& Back = Vertices[I + NumParticles];
			Back.Position = Positions[I];
			Back.Normal = N * -1.0f;
			Back.Tangent = FVector4(T.X, T.Y, T.Z, -Handedness);
		}
	}

	void* Dst = RenderCommand::MapWriteDiscard(VertexBuffer.get());
	if (!Dst) return;
	std::memcpy(Dst, Vertices.GetData(), sizeof(FVertex) * Vertices.Num());
	RenderCommand::Unmap(VertexBuffer.get());
}
