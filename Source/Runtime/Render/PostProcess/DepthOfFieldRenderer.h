#pragma once

#include "Render/Buffer.h"
#include "Render/PipelineState.h"
#include "Render/SceneView.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/PostProcess/DepthOfFieldData.h"
#include "Render/PostProcess/DepthOfFieldViewSettings.h"

#include <array>
#include <memory>
#include <initializer_list>
#include <vector>

inline constexpr uint32 DOFPyramidLevelCount = 9;
struct FDepthOfFieldSmallLightResources;

struct FDepthOfFieldQualitySettings
{
    // 카메라 설정과 렌더링 비용 제한을 분리한다.
    float MaxBlurRadiusFullResolutionPixels = 64.0f;
    uint32 NearSampleCount = 64;
    uint32 FarSampleCount = 64;
    // 0이면 기존 DOF만 사용한다. 광원 후보와 후보당 픽셀 수에 각각 상한을 둔다.
    uint32 MaxSmallLightCount = 32;
    uint32 SmallLightMaxDiameterPixels = 32;
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

    // 넓은 보케 영역을 제한된 샘플 수로 적분할 면적 피라미드.
    std::array<FRenderGraphTextureRef, DOFPyramidLevelCount> NearColorPyramid;
    std::array<FRenderGraphTextureRef, DOFPyramidLevelCount> NearParamsPyramid;
    std::array<FRenderGraphTextureRef, DOFPyramidLevelCount> FarColorPyramid;
    std::array<FRenderGraphTextureRef, DOFPyramidLevelCount> FarParamsPyramid;

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

    bool AddDebugPasses(FRenderGraphBuilder& Graph, const FSceneView& View, FRenderGraphTextureRef SceneDepth, FRenderGraphTextureRef OutputColor, const FDepthOfFieldQualitySettings& Quality, const FDepthOfFieldViewSettings& Settings);

private:
    enum class EPass : uint8
    {
        CoC,
        Downsample,
        LayerPyramid,
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

    bool AddStage(FRenderGraphBuilder& Graph, EPass Stage, const FDepthOfFieldData& Data,
        std::vector<FRenderGraphTextureRef> Inputs,
        std::initializer_list<FRenderGraphTextureRef> Outputs,
        std::shared_ptr<FDepthOfFieldSmallLightResources> SmallLights = {},
        const FVector4& SmallLightParameters = {});

    bool AddSmallLightDetection(FRenderGraphBuilder& Graph, const FDepthOfFieldData& Data,
        FRenderGraphTextureRef SceneHDR, const FDepthOfFieldPassResources& Resources,
        const FDepthOfFieldQualitySettings& Quality,
        std::shared_ptr<FDepthOfFieldSmallLightResources>& OutSmallLights,
        FVector4& OutParameters);
    bool AddSmallLightScatter(FRenderGraphBuilder& Graph, const FDepthOfFieldData& Data,
        FRenderGraphTextureRef SceneHDR, const FDepthOfFieldPassResources& Resources,
        std::shared_ptr<FDepthOfFieldSmallLightResources> SmallLights,
        const FVector4& Parameters, bool bNear);

    std::array<FPipelineState, PassCount> Pipelines;
    TUniquePtr<FConstantBuffer> ConstantBuffer;
    FPipelineState DebugPipeline;
    TUniquePtr<FConstantBuffer> DebugConstantBuffer;
    TUniquePtr<FComputeShader> SmallLightDetector;
    TUniquePtr<FConstantBuffer> SmallLightConstantBuffer;
    std::array<FPipelineState, 2> SmallLightPipelines;
    std::shared_ptr<FDepthOfFieldSmallLightResources> SmallLightResources;
};
