#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Render/Vertex.h"
#include "Render/VertexFactory.h"

class FVertexBuffer;
class FIndexBuffer;

// 천 격자의 입자 위치로 양면 메시를 만들어 GPU에 올린다.
// UClothComponent가 소유하고, FClothSceneProxy가 그릴 때 정점 팩토리를 빌려 간다.
// 정점은 앞면 N개 + 뒷면 N개다. 뒷면은 노멀을 뒤집고 삼각형 감기 순서를 반대로 해서,
// 뒷면 컬링을 그대로 둔 채 양쪽에서 보이고 양쪽 모두 빛을 제대로 받는다.
class FClothRenderData
{
public:
	FClothRenderData();
	~FClothRenderData();

	FClothRenderData(const FClothRenderData&) = delete;
	FClothRenderData& operator=(const FClothRenderData&) = delete;

	// 격자 크기, 삼각형, UV 배치가 바뀌었을 때 버퍼를 새로 만든다.
	// bFlagUV가 true면 U = 깃대에서 멀어지는 방향(격자 Y), V = 위에서 아래(격자 X)다.
	void Build(int32 InCountX, int32 InCountY, const TArray<uint32>& Indices, bool bFlagUV);

	// 입자 위치(천 로컬 공간, 스케일 없음)로 정점 위치·노멀·탄젠트를 다시 계산해 올린다.
	// 렌더러는 컴포넌트 월드 행렬(스케일 포함)을 곱하므로 InvScale로 미리 나눠 스케일을 상쇄한다.
	void Update(const TArray<FVector>& Positions, const FVector& InvScale);

	bool IsValid() const { return VertexFactory.IsValid() && IndexCount > 0; }
	const FVertexFactory* GetVertexFactory() const { return &VertexFactory; }
	uint32 GetIndexCount() const { return IndexCount; }

private:
	int32 CountX = 0;
	int32 CountY = 0;
	uint32 NumParticles = 0;
	uint32 IndexCount = 0;
	bool bFlagLayoutUV = false;

	TArray<uint32> FrontIndices;     // 노멀 계산용 앞면 삼각형
	TArray<FVertex> Vertices;        // 앞면 NumParticles개 + 뒷면 NumParticles개
	TArray<FVector> Positions;       // 스케일을 상쇄한 위치 (작업용)
	TArray<FVector> Normals;         // 작업용

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
	FLocalVertexFactory VertexFactory;
};
