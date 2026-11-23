#include "EnginePCH.h"

#include "Render/PostProcess/DepthOfFieldRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Camera/CameraOptics.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

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

bool FDepthOfFieldRenderer::AddStage(FRenderGraphBuilder& Graph, EPass Stage, const FDepthOfFieldData& Data, std::vector<FRenderGraphTextureRef> Inputs, std::initializer_list<FRenderGraphTextureRef> Outputs)
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

    // 뷰별 데이터와 PSO를 값으로 캡처한다. 텍스처 바인딩은 Graph가 담당한다.
    auto ExecutePass = [Pipeline, Constants, Data]()
    {
        RenderCommand::BindPipelineState(Pipeline);
        RenderCommand::BindVertexBuffer(nullptr);
        RenderCommand::BindSamplerState(0, ESamplerState::PointClamp, EShaderBindFlagBits::Pixel);
        RenderCommand::BindSamplerState(1, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
        RenderCommand::UpdateBufferData(Constants, &Data, sizeof(Data));
        RenderCommand::BindConstantBuffer(0, Constants, EShaderBindFlagBits::Pixel);
        RenderCommand::Draw(3);
    };

    return Graph.AddRasterPass(std::move(Pass), std::move(ExecutePass));
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
    if (!AddStage(Graph, EPass::Downsample, Data, { SceneHDR, Resources.LinearDepth, Resources.CoCAndDepthBlur }, { Resources.NearColor, Resources.FarColor, Resources.NearParams, Resources.FarParams }))
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

    // 원본 HDR은 최종 합성까지 읽기 전용으로 유지한다.
    if (!AddStage(Graph, EPass::Composite, Data, { SceneHDR, Resources.LinearDepth, Resources.CoCAndDepthBlur, Resources.FarBokeh, Resources.NearBokeh, Resources.FarParams, Resources.NearParams }, { Resources.CompositeHDR }))
        return false;

    OutColor = Resources.CompositeHDR;
    return true;
}
