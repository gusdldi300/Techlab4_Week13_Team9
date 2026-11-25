#include "EnginePCH.h"

#include "Render/PostProcess/DepthOfFieldRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/RenderUtil.h"
#include "Render/Texture2D.h"
#include "Camera/CameraOptics.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

struct FDepthOfFieldSmallLightResources
{
    TUniquePtr<FStructuredBuffer> Regions;
    TUniquePtr<FStructuredBuffer> Count;
    TUniquePtr<FStructuredBuffer> Mask;
};

namespace
{
    constexpr uint32 MaximumSmallLightCount = 128;
    constexpr uint32 SmallLightRegionStride = 32; // HLSL의 uint4 Bounds + float4 Parameters.
    constexpr uint32 SmallLightPixelsPerRegion = 256;
}

bool FDepthOfFieldRenderer::Init()
{
    static constexpr const char* ShaderPaths[] =
    {
        "EngineResources/Shaders/DepthOfField/CoC.hlsl",
        "EngineResources/Shaders/DepthOfField/Downsample.hlsl",
        "EngineResources/Shaders/DepthOfField/LayerPyramid.hlsl",
        "EngineResources/Shaders/DepthOfField/DilateNearHorizontal.hlsl",
        "EngineResources/Shaders/DepthOfField/DilateNearVertical.hlsl",
        "EngineResources/Shaders/DepthOfField/FarBokeh.hlsl",
        "EngineResources/Shaders/DepthOfField/NearBokeh.hlsl",
        "EngineResources/Shaders/DepthOfField/Composite.hlsl"
    };

    static_assert(std::size(ShaderPaths) == PassCount);
    ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDepthOfFieldData));

    for (size_t Index = 0; Index < PassCount; ++Index)
    {
        FPipelineState& Pipeline = Pipelines[Index];
        // 미구현 셰이더를 기본 메시 셰이더로 대체하지 않는다.
        Pipeline.Shader = FRenderResourceManager::FindShaderProgram(ShaderPaths[Index]);
        Pipeline.Topology = EPrimitiveTopology::TriangleList;
        Pipeline.RasterizerState = ERasterizerState::SolidNone;
        Pipeline.BlendState = EBlendState::Opaque;
        Pipeline.DepthStencilState = EDepthStencilState::Disabled;
    }

    // 진단 셰이더의 준비 상태는 일반 DOF 준비 상태와 분리한다.
    DebugPipeline = Pipelines[static_cast<size_t>(EPass::CoC)];
    DebugPipeline.Shader = FRenderResourceManager::FindShaderProgram("EngineResources/Shaders/DepthOfField/DebugCoC.hlsl");
    DebugConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FVector4));

    SmallLightConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FVector4));
    SmallLightDetector = RenderCommand::CreateComputeShader(RenderUtil::CompileShader(
        "EngineResources/Shaders/DepthOfField/DetectSmallLights.hlsl",
        "mainCS", EShaderType::Compute));
    SmallLightPipelines[0] = Pipelines[static_cast<size_t>(EPass::FarBokeh)];
    SmallLightPipelines[1] = Pipelines[static_cast<size_t>(EPass::NearBokeh)];
    SmallLightPipelines[0].Shader = FRenderResourceManager::FindShaderProgram(
        "EngineResources/Shaders/DepthOfField/FarSmallLight.hlsl");
    SmallLightPipelines[1].Shader = FRenderResourceManager::FindShaderProgram(
        "EngineResources/Shaders/DepthOfField/NearSmallLight.hlsl");
    for (FPipelineState& Pipeline : SmallLightPipelines)
        Pipeline.BlendState = EBlendState::Additive;

    return IsReady();
}

bool FDepthOfFieldRenderer::IsReady() const
{
    if (!ConstantBuffer || !ConstantBuffer->GetBuffer())
        return false;

    return std::all_of(Pipelines.begin(), Pipelines.end(), [](const FPipelineState& Pipeline)
    {
        return Pipeline.Shader && Pipeline.Shader->VertexShader && Pipeline.Shader->PixelShader && Pipeline.Shader->VertexShader->IsValid() && Pipeline.Shader->PixelShader->IsValid();
    });
}

FDepthOfFieldData FDepthOfFieldRenderer::BuildData(const FSceneView& View, uint32 Width, uint32 Height, const FDepthOfFieldQualitySettings& Quality)
{
    const uint32 HalfWidth = Width / 2 + Width % 2;
    const uint32 HalfHeight = Height / 2 + Height % 2;

    FCameraLensSettings Lens = View.LensSettings;
    FDepthOfFieldSettings Settings = View.DepthOfFieldSettings;
    float FinalFov = View.FieldOfView;

    // 최종 FOV를 유지한 채 렌더용 사본을 검증한다.
    SanitizeDepthOfFieldSettings(Settings);
    FinalizeCameraOptics(Lens, Settings, FinalFov);

    float MaximumRadius = Quality.MaxBlurRadiusFullResolutionPixels;
    SanitizeFloat(MaximumRadius, 1.0f, 256.0f, 64.0f);
    const uint32 NearSamples = std::clamp(Quality.NearSampleCount, 8u, 256u);
    const uint32 FarSamples = std::clamp(Quality.FarSampleCount, 8u, 256u);

    const float MetersPerWorldUnit = 0.01f * WorldToCm;
    FDepthOfFieldData Data{};
    Data.InverseProjection = View.ProjectionMatrix.Inverse();
    Data.FullSizeAndInvSize = FVector4(static_cast<float>(Width), static_cast<float>(Height), 1.0f / Width, 1.0f / Height);
    Data.HalfSizeAndInvSize = FVector4(static_cast<float>(HalfWidth), static_cast<float>(HalfHeight), 1.0f / HalfWidth, 1.0f / HalfHeight);
    Data.LensParameters = FVector4(Lens.FocalLengthMm * 0.001f, Lens.SensorHeightMm * 0.001f, Settings.FocalDistanceMeters, Settings.FStop);
    Data.ApertureParameters = FVector4(Settings.MinFStop, static_cast<float>(Settings.ApertureBladeCount), static_cast<float>(NearSamples), static_cast<float>(FarSamples));
    Data.DepthBlurParameters = FVector4(Settings.DepthBlurKmFor50Percent * 1000.0f, Settings.DepthBlurRadiusFractionOfHeight * Height, MaximumRadius, MetersPerWorldUnit);
    return Data;
}

bool FDepthOfFieldRenderer::CreateResources(FRenderGraphBuilder& Graph, uint32 Width, uint32 Height, FDepthOfFieldPassResources& OutResources)
{
    OutResources = {};
    if (Width == 0 || Height == 0)
        return false;

    // 홀수 해상도의 마지막 행·열을 보존한다.
    const uint32 HalfWidth = Width / 2 + Width % 2;
    const uint32 HalfHeight = Height / 2 + Height % 2;

    OutResources.LinearDepth = Graph.CreateTexture({ Width, Height, ETextureFormat::R32_Float }, "DOF.LinearDepth");
    OutResources.CoCAndDepthBlur = Graph.CreateTexture({ Width, Height, ETextureFormat::R16G16_Float }, "DOF.CoCAndDepthBlur");
    OutResources.NearColor = Graph.CreateTexture({ HalfWidth, HalfHeight, ETextureFormat::R16G16B16A16_Float }, "DOF.NearColor");
    OutResources.FarColor = Graph.CreateTexture({ HalfWidth, HalfHeight, ETextureFormat::R16G16B16A16_Float }, "DOF.FarColor");
    OutResources.NearParams = Graph.CreateTexture({ HalfWidth, HalfHeight, ETextureFormat::R32G32_Float }, "DOF.NearParams");
    OutResources.FarParams = Graph.CreateTexture({ HalfWidth, HalfHeight, ETextureFormat::R32G32_Float }, "DOF.FarParams");

    uint32 LevelWidth = HalfWidth;
    uint32 LevelHeight = HalfHeight;
    FRenderGraphTextureRef PreviousNearColor = OutResources.NearColor;
    FRenderGraphTextureRef PreviousNearParams = OutResources.NearParams;
    FRenderGraphTextureRef PreviousFarColor = OutResources.FarColor;
    FRenderGraphTextureRef PreviousFarParams = OutResources.FarParams;

    for (uint32 Level = 0; Level < DOFPyramidLevelCount; ++Level)
    {
        // 1×1 이후에는 마지막 텍스처를 공유한다.
        if (LevelWidth != 1 || LevelHeight != 1)
        {
            LevelWidth = LevelWidth / 2 + LevelWidth % 2;
            LevelHeight = LevelHeight / 2 + LevelHeight % 2;
            const std::string Suffix = std::to_string(Level + 1);
            PreviousNearColor = Graph.CreateTexture({ LevelWidth, LevelHeight, ETextureFormat::R16G16B16A16_Float }, ("DOF.NearColorPyramid" + Suffix).c_str());
            PreviousNearParams = Graph.CreateTexture({ LevelWidth, LevelHeight, ETextureFormat::R32G32_Float }, ("DOF.NearParamsPyramid" + Suffix).c_str());
            PreviousFarColor = Graph.CreateTexture({ LevelWidth, LevelHeight, ETextureFormat::R16G16B16A16_Float }, ("DOF.FarColorPyramid" + Suffix).c_str());
            PreviousFarParams = Graph.CreateTexture({ LevelWidth, LevelHeight, ETextureFormat::R32G32_Float }, ("DOF.FarParamsPyramid" + Suffix).c_str());
            if (!PreviousNearColor || !PreviousNearParams || !PreviousFarColor || !PreviousFarParams)
                return false;
        }

        OutResources.NearColorPyramid[Level] = PreviousNearColor;
        OutResources.NearParamsPyramid[Level] = PreviousNearParams;
        OutResources.FarColorPyramid[Level] = PreviousFarColor;
        OutResources.FarParamsPyramid[Level] = PreviousFarParams;
    }
    OutResources.NearSpreadHorizontal = Graph.CreateTexture({ HalfWidth, HalfHeight, ETextureFormat::R16_Float }, "DOF.NearSpreadHorizontal");
    OutResources.NearSpreadRadius = Graph.CreateTexture({ HalfWidth, HalfHeight, ETextureFormat::R16_Float }, "DOF.NearSpreadRadius");
    OutResources.NearBokeh = Graph.CreateTexture({ HalfWidth, HalfHeight, ETextureFormat::R16G16B16A16_Float }, "DOF.NearBokeh");
    OutResources.FarBokeh = Graph.CreateTexture({ HalfWidth, HalfHeight, ETextureFormat::R16G16B16A16_Float }, "DOF.FarBokeh");
    OutResources.CompositeHDR = Graph.CreateTexture({ Width, Height, ETextureFormat::R16G16B16A16_Float }, "DOF.CompositeHDR");

    return OutResources.LinearDepth && OutResources.CoCAndDepthBlur &&
        OutResources.NearColor && OutResources.FarColor &&
        OutResources.NearParams && OutResources.FarParams &&
        OutResources.NearSpreadHorizontal && OutResources.NearSpreadRadius &&
        OutResources.NearBokeh && OutResources.FarBokeh && OutResources.CompositeHDR;
}

bool FDepthOfFieldRenderer::AddSmallLightDetection(FRenderGraphBuilder& Graph,
    const FDepthOfFieldData& Data, FRenderGraphTextureRef SceneHDR,
    const FDepthOfFieldPassResources& Resources, const FDepthOfFieldQualitySettings& Quality,
    std::shared_ptr<FDepthOfFieldSmallLightResources>& OutSmallLights,
    FVector4& OutParameters)
{
    OutSmallLights.reset();
    OutParameters = {};
    const uint32 LightLimit = std::min(Quality.MaxSmallLightCount, MaximumSmallLightCount);
    const auto IsPipelineReady = [](const FPipelineState& Pipeline)
    {
        return Pipeline.Shader && Pipeline.Shader->VertexShader &&
            Pipeline.Shader->PixelShader && Pipeline.Shader->VertexShader->IsValid() &&
            Pipeline.Shader->PixelShader->IsValid();
    };
    // 분리 경로가 준비되지 않으면 광원을 제거하지 않고 기존 DOF를 그대로 사용한다.
    if (!LightLimit || Data.DepthBlurParameters.Z < 2.0f ||
        !SmallLightConstantBuffer || !SmallLightConstantBuffer->GetBuffer() ||
        !SmallLightDetector || !SmallLightDetector->IsValid() ||
        !IsPipelineReady(SmallLightPipelines[0]) || !IsPipelineReady(SmallLightPipelines[1]))
        return true;

    const uint32 Width = static_cast<uint32>(Data.FullSizeAndInvSize.X);
    const uint32 Height = static_cast<uint32>(Data.FullSizeAndInvSize.Y);
    const uint64 PixelCount = static_cast<uint64>(Width) * Height;
    if (!PixelCount || PixelCount > UINT32_MAX / sizeof(uint32))
        return true;

    if (!SmallLightResources ||
        SmallLightResources->Mask->GetCapacity() < static_cast<uint32>(PixelCount))
    {
        auto NewResources = std::make_shared<FDepthOfFieldSmallLightResources>();
        NewResources->Regions = RenderCommand::CreateRWStructuredBuffer(
            SmallLightRegionStride, MaximumSmallLightCount);
        NewResources->Count = RenderCommand::CreateRWStructuredBuffer(sizeof(uint32), 1);
        NewResources->Mask = RenderCommand::CreateRWStructuredBuffer(
            sizeof(uint32), static_cast<uint32>(PixelCount));
        const auto IsBufferReady = [](const TUniquePtr<FStructuredBuffer>& Buffer)
        {
            return Buffer && Buffer->GetBuffer() && Buffer->GetSRV() && Buffer->GetUAV();
        };
        if (!IsBufferReady(NewResources->Regions) || !IsBufferReady(NewResources->Count) ||
            !IsBufferReady(NewResources->Mask))
            return true;
        SmallLightResources = std::move(NewResources);
    }

    const auto Lights = SmallLightResources;
    const FVector4 Parameters(static_cast<float>(LightLimit),
        static_cast<float>(std::clamp(Quality.SmallLightMaxDiameterPixels, 2u, 32u)),
        4.0f, 0.01f);
    FRenderGraphComputePassDesc Pass;
    Pass.Name = "DOF.DetectSmallLights";
    Pass.ReadTextures = { SceneHDR, Resources.CoCAndDepthBlur, Resources.LinearDepth };
    Pass.WriteResources = { Lights->Regions->GetBuffer(),
        Lights->Count->GetBuffer(), Lights->Mask->GetBuffer() };
    FComputeShader* Shader = SmallLightDetector.get();
    FConstantBuffer* Constants = ConstantBuffer.get();
    FConstantBuffer* LightConstants = SmallLightConstantBuffer.get();
    const auto CoC = Resources.CoCAndDepthBlur;
    const auto Depth = Resources.LinearDepth;
    auto Execute = [&Graph, Lights, Shader, Constants, LightConstants, Data,
        Parameters, SceneHDR, CoC, Depth, Width, Height]()
    {
        // 같은 버퍼를 다음 뷰/프레임에 재사용해도 이전 추출 마스크는 남기지 않는다.
        RenderCommand::ClearUnorderedAccess(Lights->Count.get());
        RenderCommand::ClearUnorderedAccess(Lights->Mask.get());
        RenderCommand::UpdateBufferData(Constants, &Data, sizeof(Data));
        RenderCommand::UpdateBufferData(LightConstants, &Parameters, sizeof(Parameters));
        RenderCommand::BindComputeShader(Shader);
        RenderCommand::BindConstantBuffer(0, Constants, EShaderBindFlagBits::Compute);
        RenderCommand::BindConstantBuffer(1, LightConstants, EShaderBindFlagBits::Compute);
        RenderCommand::BindShaderResource(0, Graph.Resolve(SceneHDR), EShaderBindFlagBits::Compute);
        RenderCommand::BindShaderResource(1, Graph.Resolve(CoC), EShaderBindFlagBits::Compute);
        RenderCommand::BindShaderResource(2, Graph.Resolve(Depth), EShaderBindFlagBits::Compute);
        RenderCommand::BindUnorderedAccess(0, Lights->Regions.get());
        RenderCommand::BindUnorderedAccess(1, Lights->Count.get());
        RenderCommand::BindUnorderedAccess(2, Lights->Mask.get());
        RenderCommand::Dispatch((Width + 15) / 16, (Height + 15) / 16, 1);
        RenderCommand::UnbindUnorderedAccess(0);
        RenderCommand::UnbindUnorderedAccess(1);
        RenderCommand::UnbindUnorderedAccess(2);
        RenderCommand::UnbindShaderResources(0, 3, EShaderBindFlagBits::Compute);
        RenderCommand::BindConstantBuffer(0, nullptr, EShaderBindFlagBits::Compute);
        RenderCommand::BindConstantBuffer(1, nullptr, EShaderBindFlagBits::Compute);
        RenderCommand::BindComputeShader(nullptr);
    };
    if (!Graph.AddComputePass(std::move(Pass), std::move(Execute)))
        return false;
    OutSmallLights = Lights;
    OutParameters = Parameters;
    return true;
}

bool FDepthOfFieldRenderer::AddSmallLightScatter(FRenderGraphBuilder& Graph,
    const FDepthOfFieldData& Data, FRenderGraphTextureRef SceneHDR,
    const FDepthOfFieldPassResources& Resources,
    std::shared_ptr<FDepthOfFieldSmallLightResources> SmallLights,
    const FVector4& Parameters, bool bNear)
{
    if (!SmallLights)
        return true;
    FRenderGraphRasterPassDesc Pass;
    Pass.Name = bNear ? "DOF.NearSmallLights" : "DOF.FarSmallLights";
    Pass.ReadTextures = { SceneHDR, Resources.CoCAndDepthBlur, Resources.LinearDepth };
    // 기존 면적 블러를 유지하고 추출한 광원의 RGB만 가산한다.
    Pass.ColorAttachments.push_back({ bNear ? Resources.NearBokeh : Resources.FarBokeh,
        ERenderGraphLoadAction::Load, {} });
    const FPipelineState Pipeline = SmallLightPipelines[bNear ? 1 : 0];
    FConstantBuffer* Constants = ConstantBuffer.get();
    FConstantBuffer* LightConstants = SmallLightConstantBuffer.get();
    auto Execute = [Pipeline, Constants, LightConstants, Data, Parameters, SmallLights]()
    {
        constexpr auto ShaderStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;
        RenderCommand::BindPipelineState(Pipeline);
        RenderCommand::BindVertexBuffer(nullptr);
        RenderCommand::UpdateBufferData(Constants, &Data, sizeof(Data));
        RenderCommand::UpdateBufferData(LightConstants, &Parameters, sizeof(Parameters));
        RenderCommand::BindConstantBuffer(0, Constants, ShaderStages);
        RenderCommand::BindConstantBuffer(1, LightConstants, ShaderStages);
        RenderCommand::BindShaderResource(3, SmallLights->Regions.get(), EShaderBindFlagBits::Vertex);
        RenderCommand::BindShaderResource(4, SmallLights->Count.get(), EShaderBindFlagBits::Vertex);
        RenderCommand::BindShaderResource(5, SmallLights->Mask.get(), EShaderBindFlagBits::Vertex);
        const uint32 InstanceCount = static_cast<uint32>(Parameters.X) * SmallLightPixelsPerRegion;
        RenderCommand::DrawInstanced(6, InstanceCount);
        RenderCommand::UnbindShaderResources(3, 3, EShaderBindFlagBits::Vertex);
        RenderCommand::BindConstantBuffer(1, nullptr, ShaderStages);
        RenderCommand::BindConstantBuffer(0, nullptr, EShaderBindFlagBits::Vertex);
    };
    return Graph.AddRasterPass(std::move(Pass), std::move(Execute));
}

bool FDepthOfFieldRenderer::AddStage(FRenderGraphBuilder& Graph, EPass Stage,
    const FDepthOfFieldData& Data, std::vector<FRenderGraphTextureRef> Inputs,
    std::initializer_list<FRenderGraphTextureRef> Outputs,
    std::shared_ptr<FDepthOfFieldSmallLightResources> SmallLights,
    const FVector4& SmallLightParameters)
{
    static constexpr const char* PassNames[] =
    {
        "DOF.CoC",
        "DOF.Downsample",
        "DOF.LayerPyramid",
        "DOF.DilateNearHorizontal",
        "DOF.DilateNearVertical",
        "DOF.FarBokeh",
        "DOF.NearBokeh",
        "DOF.Composite"
    };

    static_assert(std::size(PassNames) == PassCount);
    const size_t PassIndex = static_cast<size_t>(Stage);
    FRenderGraphRasterPassDesc Pass{};
    Pass.Name = PassNames[PassIndex];
    Pass.ReadTextures = std::move(Inputs);

    for (FRenderGraphTextureRef Output : Outputs)
    {
        // 풀에서 재사용하더라도 그리지 않은 영역에 이전 값이 남지 않게 한다.
        Pass.ColorAttachments.push_back({ Output, ERenderGraphLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 0.0f } });
    }

    const FPipelineState Pipeline = Pipelines[PassIndex];
    FConstantBuffer* Constants = ConstantBuffer.get();
    FConstantBuffer* LightConstants = SmallLightConstantBuffer.get();
    const bool bReadSmallLightMask = Stage == EPass::Downsample || Stage == EPass::Composite;
    const uint32 MaskSlot = Stage == EPass::Downsample ? 3u : 5u;

    // 뷰별 데이터와 PSO를 값으로 캡처한다. 텍스처 바인딩은 Graph가 담당한다.
    auto ExecutePass = [Pipeline, Constants, Data, LightConstants,
        bReadSmallLightMask, MaskSlot, SmallLights, SmallLightParameters]()
    {
        RenderCommand::BindPipelineState(Pipeline);
        RenderCommand::BindVertexBuffer(nullptr);
        RenderCommand::BindSamplerState(0, ESamplerState::PointClamp, EShaderBindFlagBits::Pixel);
        RenderCommand::BindSamplerState(1, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
        RenderCommand::UpdateBufferData(Constants, &Data, sizeof(Data));
        RenderCommand::BindConstantBuffer(0, Constants, EShaderBindFlagBits::Pixel);
        if (bReadSmallLightMask)
        {
            if (LightConstants)
                RenderCommand::UpdateBufferData(
                    LightConstants, &SmallLightParameters, sizeof(SmallLightParameters));
            RenderCommand::BindConstantBuffer(1, LightConstants, EShaderBindFlagBits::Pixel);
            RenderCommand::BindShaderResource(MaskSlot,
                SmallLights ? SmallLights->Mask.get() : nullptr, EShaderBindFlagBits::Pixel);
        }
        RenderCommand::Draw(3);
        if (bReadSmallLightMask)
        {
            RenderCommand::UnbindShaderResources(MaskSlot, 1, EShaderBindFlagBits::Pixel);
            RenderCommand::BindConstantBuffer(1, nullptr, EShaderBindFlagBits::Pixel);
        }
    };

    return Graph.AddRasterPass(std::move(Pass), std::move(ExecutePass));
}

bool FDepthOfFieldRenderer::AddDebugPasses(FRenderGraphBuilder& Graph, const FSceneView& View, FRenderGraphTextureRef SceneDepth, FRenderGraphTextureRef OutputColor, const FDepthOfFieldQualitySettings& Quality, const FDepthOfFieldViewSettings& Settings)
{
    if (!IsReady() || !View.bIsPerspective || View.bIsWireframe)
        return false;

    if (!DebugConstantBuffer || !DebugConstantBuffer->GetBuffer())
        return false;

    if (!DebugPipeline.Shader || !DebugPipeline.Shader->VertexShader || !DebugPipeline.Shader->PixelShader)
        return false;

    if (!DebugPipeline.Shader->VertexShader->IsValid() || !DebugPipeline.Shader->PixelShader->IsValid())
        return false;

    FDepthOfFieldViewSettings DebugSettings = Settings;
    SanitizeDepthOfFieldViewSettings(DebugSettings);
    if (DebugSettings.DebugView == EDepthOfFieldDebugView::None)
        return false;

    FRenderGraphTextureDesc DepthDesc{};
    FRenderGraphTextureDesc OutputDesc{};
    if (!Graph.GetTextureDesc(SceneDepth, DepthDesc) || !Graph.GetTextureDesc(OutputColor, OutputDesc))
        return false;

    if (DepthDesc.Width != OutputDesc.Width || DepthDesc.Height != OutputDesc.Height)
        return false;

    if (OutputDesc.Format != ETextureFormat::R8G8B8A8_UNorm)
        return false;

    const FRenderGraphTextureRef LinearDepth = Graph.CreateTexture({ DepthDesc.Width, DepthDesc.Height, ETextureFormat::R32_Float }, "DOF.Debug.LinearDepth");
    const FRenderGraphTextureRef CoCAndDepthBlur = Graph.CreateTexture({ DepthDesc.Width, DepthDesc.Height, ETextureFormat::R16G16_Float }, "DOF.Debug.CoCAndDepthBlur");
    if (!LinearDepth || !CoCAndDepthBlur)
        return false;

    const FDepthOfFieldData Data = BuildData(View, DepthDesc.Width, DepthDesc.Height, Quality);

    // 디버그에는 CoC까지만 필요하며 보케 중간 자원은 만들지 않는다.
    if (!AddStage(Graph, EPass::CoC, Data, { SceneDepth }, { LinearDepth, CoCAndDepthBlur }))
        return false;

    FRenderGraphRasterPassDesc Pass{};
    Pass.Name = "DOF.Debug";
    Pass.ReadTextures = { CoCAndDepthBlur, LinearDepth };
    Pass.ColorAttachments = { { OutputColor, ERenderGraphLoadAction::Clear, { 0.0f, 0.0f, 0.0f, 1.0f } } };

    const FPipelineState Pipeline = DebugPipeline;
    const FVector4 DebugData = FVector4(static_cast<float>(DebugSettings.DebugView), DebugSettings.DebugRadiusFullResolutionPixels, 0.0f, 0.0f);
    FConstantBuffer* Constants = ConstantBuffer.get();
    FConstantBuffer* DebugConstants = DebugConstantBuffer.get();

    auto ExecuteDebug = [Pipeline, Constants, DebugConstants, Data, DebugData]()
    {
        RenderCommand::BindPipelineState(Pipeline);
        RenderCommand::BindVertexBuffer(nullptr);
        RenderCommand::UpdateBufferData(Constants, &Data, sizeof(Data));
        RenderCommand::UpdateBufferData(DebugConstants, &DebugData, sizeof(DebugData));
        RenderCommand::BindConstantBuffer(0, Constants, EShaderBindFlagBits::Pixel);
        RenderCommand::BindConstantBuffer(1, DebugConstants, EShaderBindFlagBits::Pixel);
        RenderCommand::Draw(3);
        RenderCommand::BindConstantBuffer(1, nullptr, EShaderBindFlagBits::Pixel);
    };

    return Graph.AddRasterPass(std::move(Pass), std::move(ExecuteDebug));
}

bool FDepthOfFieldRenderer::AddPasses(FRenderGraphBuilder& Graph, const FSceneView& View, FRenderGraphTextureRef SceneHDR, FRenderGraphTextureRef SceneDepth, bool bEnabled, const FDepthOfFieldQualitySettings& Quality, FRenderGraphTextureRef& OutColor)
{
    OutColor = SceneHDR;

    // 비활성·미준비 상태에서는 텍스처를 등록하지 않고 원본 HDR을 전달한다.
    if (!bEnabled || !View.DepthOfFieldSettings.bEnabled || !View.bIsPerspective || View.bIsWireframe || !IsReady())
        return true;

    FRenderGraphTextureDesc ColorDesc{};
    FRenderGraphTextureDesc DepthDesc{};
    if (!Graph.GetTextureDesc(SceneHDR, ColorDesc) || !Graph.GetTextureDesc(SceneDepth, DepthDesc))
        return false;

    if (ColorDesc.Width != DepthDesc.Width || ColorDesc.Height != DepthDesc.Height)
        return false;

    if (ColorDesc.Format != ETextureFormat::R16G16B16A16_Float)
        return false;

    FDepthOfFieldPassResources Resources{};
    if (!CreateResources(Graph, ColorDesc.Width, ColorDesc.Height, Resources))
        return false;

    const FDepthOfFieldData Data = BuildData(View, ColorDesc.Width, ColorDesc.Height, Quality);

    // MRT로 선형 깊이와 CoC를 함께 기록한다.
    if (!AddStage(Graph, EPass::CoC, Data, { SceneDepth }, { Resources.LinearDepth, Resources.CoCAndDepthBlur }))
        return false;

    // 원본 샘플별 분류 후 Near/Far 색상과 메타데이터를 축소한다.
    std::shared_ptr<FDepthOfFieldSmallLightResources> SmallLights;
    FVector4 SmallLightParameters{};
    if (!AddSmallLightDetection(Graph, Data, SceneHDR, Resources, Quality,
        SmallLights, SmallLightParameters))
        return false;
    if (!AddStage(Graph, EPass::Downsample, Data,
        { SceneHDR, Resources.LinearDepth, Resources.CoCAndDepthBlur },
        { Resources.NearColor, Resources.FarColor, Resources.NearParams, Resources.FarParams },
        SmallLights, SmallLightParameters))
        return false;

    FRenderGraphTextureRef PreviousNearColor = Resources.NearColor;
    FRenderGraphTextureRef PreviousNearParams = Resources.NearParams;
    FRenderGraphTextureRef PreviousFarColor = Resources.FarColor;
    FRenderGraphTextureRef PreviousFarParams = Resources.FarParams;

    for (uint32 Level = 0; Level < DOFPyramidLevelCount; ++Level)
    {
        // 1×1 이후 공유 핸들에는 같은 텍스처로 쓰는 패스를 등록하지 않는다.
        if (PreviousNearColor.Index != Resources.NearColorPyramid[Level].Index)
        {
            if (!AddStage(Graph, EPass::LayerPyramid, Data, { PreviousNearColor, PreviousNearParams }, { Resources.NearColorPyramid[Level], Resources.NearParamsPyramid[Level] }))
                return false;
        }

        if (PreviousFarColor.Index != Resources.FarColorPyramid[Level].Index)
        {
            if (!AddStage(Graph, EPass::LayerPyramid, Data, { PreviousFarColor, PreviousFarParams }, { Resources.FarColorPyramid[Level], Resources.FarParamsPyramid[Level] }))
                return false;
        }

        PreviousNearColor = Resources.NearColorPyramid[Level];
        PreviousNearParams = Resources.NearParamsPyramid[Level];
        PreviousFarColor = Resources.FarColorPyramid[Level];
        PreviousFarParams = Resources.FarParamsPyramid[Level];
    }

    // 탐색 범위는 보수적으로 확장하고 실제 기여는 보케 패스에서 판단한다.
    if (!AddStage(Graph, EPass::DilateNearHorizontal, Data, { Resources.NearParams }, { Resources.NearSpreadHorizontal }))
        return false;

    if (!AddStage(Graph, EPass::DilateNearVertical, Data, { Resources.NearSpreadHorizontal }, { Resources.NearSpreadRadius }))
        return false;

    // t0~t9는 색상, t10~t19는 대응하는 메타데이터다.
    std::vector<FRenderGraphTextureRef> NearInputs{ Resources.NearColor };
    std::vector<FRenderGraphTextureRef> FarInputs{ Resources.FarColor };
    NearInputs.insert(NearInputs.end(), Resources.NearColorPyramid.begin(), Resources.NearColorPyramid.end());
    FarInputs.insert(FarInputs.end(), Resources.FarColorPyramid.begin(), Resources.FarColorPyramid.end());
    NearInputs.push_back(Resources.NearParams);
    FarInputs.push_back(Resources.FarParams);
    NearInputs.insert(NearInputs.end(), Resources.NearParamsPyramid.begin(), Resources.NearParamsPyramid.end());
    FarInputs.insert(FarInputs.end(), Resources.FarParamsPyramid.begin(), Resources.FarParamsPyramid.end());
    NearInputs.push_back(Resources.NearSpreadRadius);
    FarInputs.push_back(Resources.FarParams);
    NearInputs.push_back(Resources.LinearDepth);
    FarInputs.push_back(Resources.LinearDepth);

    if (!AddStage(Graph, EPass::FarBokeh, Data, std::move(FarInputs), { Resources.FarBokeh }))
        return false;

    if (!AddStage(Graph, EPass::NearBokeh, Data, std::move(NearInputs), { Resources.NearBokeh }))
        return false;

    if (!AddSmallLightScatter(Graph, Data, SceneHDR, Resources,
        SmallLights, SmallLightParameters, false) ||
        !AddSmallLightScatter(Graph, Data, SceneHDR, Resources,
            SmallLights, SmallLightParameters, true))
        return false;

    // t0~t4는 Composite.hlsl의 입력 선언 순서와 일치한다.
    if (!AddStage(Graph, EPass::Composite, Data,
        { SceneHDR, Resources.LinearDepth, Resources.CoCAndDepthBlur,
            Resources.FarBokeh, Resources.NearBokeh }, { Resources.CompositeHDR },
        SmallLights, SmallLightParameters))
        return false;

    // 원본과 다른 HDR 텍스처로 출력해 읽기·쓰기 충돌을 피한다.
    OutColor = Resources.CompositeHDR;
    return true;
}
