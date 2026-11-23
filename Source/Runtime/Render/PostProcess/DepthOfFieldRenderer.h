#pragma once

#include "Render/Buffer.h"
#include "Render/PipelineState.h"
#include "Render/SceneView.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/PostProcess/DepthOfFieldData.h"

#include <array>
#include <initializer_list>

struct FDepthOfFieldQualitySettings
{
    // 카메라 설정과 렌더링 비용 제한을 분리한다.
    float MaxBlurRadiusFullResolutionPixels = 64.0f;
    uint32 NearSampleCount = 48;
    uint32 FarSampleCount = 32;
};

struct FDepthOfFieldPassResources
{
    // 전체 해상도.
    FRenderGraphTextureRef LinearDepth;
    FRenderGraphTextureRef CoCAndDepthBlur;

    // 절반 해상도: RGB는 색상 * 커버리지, A는 커버리지.
    FRenderGraphTextureRef NearColor;
    FRenderGraphTextureRef FarColor;

    // 절반 해상도: 반경과 대표 깊이.
    FRenderGraphTextureRef NearParams;
    FRenderGraphTextureRef FarParams;

    // 전경의 보수적인 탐색 범위를 두 방향으로 확장한다.
    FRenderGraphTextureRef NearSpreadHorizontal;
    FRenderGraphTextureRef NearSpreadRadius;

    FRenderGraphTextureRef NearBokeh;
    FRenderGraphTextureRef FarBokeh;

    // 전체 해상도 HDR 합성 결과.
    FRenderGraphTextureRef CompositeHDR;
};

class FDepthOfFieldRenderer
{
public:
    bool Init();
    bool IsReady() const;

    // 비활성이면 OutColor에 입력 HDR을 그대로 반환한다.
    bool AddPasses(FRenderGraphBuilder& Graph, const FSceneView& View, FRenderGraphTextureRef SceneHDR, FRenderGraphTextureRef SceneDepth, bool bEnabled, const FDepthOfFieldQualitySettings& Quality, FRenderGraphTextureRef& OutColor);

private:
    enum class EPass : uint8
    {
        CoC,
        Downsample,
        DilateNearHorizontal,
        DilateNearVertical,
        FarBokeh,
        NearBokeh,
        Composite,
        Count
    };

    static constexpr size_t PassCount = static_cast<size_t>(EPass::Count);

    static bool CreateResources(FRenderGraphBuilder& Graph, uint32 Width, uint32 Height, FDepthOfFieldPassResources& OutResources);
    static FDepthOfFieldData BuildData(const FSceneView& View, uint32 Width, uint32 Height, const FDepthOfFieldQualitySettings& Quality);

    bool AddStage(FRenderGraphBuilder& Graph, EPass Stage, const FDepthOfFieldData& Data, std::initializer_list<FRenderGraphTextureRef> Inputs, std::initializer_list<FRenderGraphTextureRef> Outputs);

    std::array<FPipelineState, PassCount> Pipelines;
    TUniquePtr<FConstantBuffer> ConstantBuffer;
};
