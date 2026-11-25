#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Render/Vertex.h"
#include "Render/VertexFactory.h"

class FVertexBuffer;
class FIndexBuffer;

// 천 입자 위치로 양면 메시를 만들어 GPU에 올린다.
// UClothComponent가 소유하고, FClothSceneProxy가 그릴 때 정점 팩토리를 빌려 간다.
//
// 렌더 정점과 시뮬레이션 입자는 1:1이 아닐 수 있다. 메시에서 만든 천은 UV 경계마다 같은 위치의 렌더 정점이
// 여러 개 있고, 시뮬레이션에서는 그것을 입자 하나로 합친다. 그래서 렌더 정점마다 어느 입자 위치를 쓸지
// 대응표(RenderToParticle)로 받는다. 격자 천은 대응표가 0, 1, 2, ...인 경우다.
//
// 정점은 앞면 N개 + 뒷면 N개다. 뒷면은 노멀을 뒤집고 삼각형 감기 순서를 반대로 해서,
// 뒷면 컬링을 그대로 둔 채 양쪽에서 보이고 양쪽 모두 빛을 제대로 받는다.
class FClothRenderData
{
public:
	FClothRenderData();
	~FClothRenderData();

	FClothRenderData(const FClothRenderData&) = delete;
	FClothRenderData& operator=(const FClothRenderData&) = delete;

	// 렌더 정점 구성(UV, 입자 대응표, 삼각형)이 바뀌었을 때 버퍼를 새로 만든다.
	// UVs와 RenderToParticle은 렌더 정점 수만큼, RenderIndices는 렌더 정점 번호로 된 앞면 삼각형이다.
	void Build(const TArray<FVector2>& UVs, const TArray<uint32>& RenderToParticle, const TArray<uint32>& RenderIndices);

	// 입자 위치(천 로컬 공간, 스케일 없음)로 정점 위치·노멀·탄젠트를 다시 계산해 올린다.
	// 렌더러는 컴포넌트 월드 행렬(스케일 포함)을 곱하므로 InvScale로 미리 나눠 스케일을 상쇄한다.
	void Update(const TArray<FVector>& ParticlePositions, const FVector& InvScale);

	bool IsValid() const { return VertexFactory.IsValid() && IndexCount > 0; }
	const FVertexFactory* GetVertexFactory() const { return &VertexFactory; }
	uint32 GetIndexCount() const { return IndexCount; }

private:
	uint32 NumRenderVertices = 0;
	uint32 NumParticles = 0;
	uint32 IndexCount = 0;

	TArray<uint32> RenderToParticle;
	TArray<uint32> FrontIndices;     // 노멀·탄젠트 계산용 앞면 삼각형(렌더 정점 번호)
	TArray<FVertex> Vertices;        // 앞면 N개 + 뒷면 N개
	TArray<FVector> ParticleNormals; // 작업용: 입자마다 노멀. UV 경계 양쪽이 같은 노멀을 써서 이음매가 보이지 않는다
	TArray<FVector> Tangents;        // 작업용: 렌더 정점마다 U 방향
	TArray<FVector> Bitangents;      // 작업용: 렌더 정점마다 V 방향

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
	FLocalVertexFactory VertexFactory;
};
