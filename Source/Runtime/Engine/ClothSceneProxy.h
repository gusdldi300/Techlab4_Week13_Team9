#pragma once

#include "PrimitiveSceneProxy.h"

class UClothComponent;

// 천 컴포넌트를 렌더러에 내보낸다. 정적 메시가 없는 동적 프록시라
// CPU 스키닝 스켈레탈 메시처럼 매 뷰마다 GetDynamicMeshElements로 패킷을 만든다.
class FClothSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FClothSceneProxy(UClothComponent* InComponent);

	void UpdateRenderState() override;
	bool HasDynamicMeshElements() const override { return true; }
	void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View) override;
};
