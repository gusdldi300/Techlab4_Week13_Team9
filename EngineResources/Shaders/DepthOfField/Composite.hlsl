#include "DepthOfFieldCommon.hlsli"

Texture2D<float4> SceneHDRTexture : register(t0);
Texture2D<float> LinearDepthTexture : register(t1);
Texture2D<float2> CoCAndDepthBlurTexture : register(t2);
Texture2D<float4> FarBokehTexture : register(t3);
Texture2D<float4> NearBokehTexture : register(t4);

FDepthOfFieldVertexOutput mainVS(uint VertexID : SV_VertexID)
{
    return MakeDepthOfFieldVertex(VertexID);
}

float ComputeDOFGuideWeight(float TargetDepth, float GuideDepth)
{
    if (!IsValidDOFDepth(TargetDepth) || !IsValidDOFDepth(GuideDepth))
        return 0.0f;

    const bool bTargetBackground = IsBackgroundDOFDepth(TargetDepth);
    const bool bGuideBackground = IsBackgroundDOFDepth(GuideDepth);

    // 배경의 0은 실제 거리 0m와 구분한다.
    if (bTargetBackground || bGuideBackground)
        return bTargetBackground && bGuideBackground ? 1.0f : 0.0f;

    const float DepthTolerance = max(0.001f, min(TargetDepth, GuideDepth) * 0.01f);
    const float DepthDifference = abs(TargetDepth - GuideDepth);
    return saturate(1.0f - DepthDifference / DepthTolerance);
}

void UpsampleDOFLayers(float2 UV, float TargetDepth, out float4 FarLayer, out float4 NearLayer)
{
    FarLayer = float4(0.0f, 0.0f, 0.0f, 0.0f);
    NearLayer = float4(0.0f, 0.0f, 0.0f, 0.0f);

    const int2 HalfSize = int2(HalfSizeAndInvSize.xy);
    const int2 FullSize = int2(FullSizeAndInvSize.xy);
    const float2 HalfPosition = UV * HalfSizeAndInvSize.xy - 0.5f;
    const int2 FirstPixel = int2(floor(HalfPosition));
    const float2 Fraction = frac(HalfPosition);
    float WeightSum = 0.0f;

    [unroll]
    for (int OffsetY = 0; OffsetY < 2; ++OffsetY)
    {
        [unroll]
        for (int OffsetX = 0; OffsetX < 2; ++OffsetX)
        {
            const int2 HalfPixel = FirstPixel + int2(OffsetX, OffsetY);
            if (any(HalfPixel < 0) || any(HalfPixel >= HalfSize))
                continue;

            const float WeightX = OffsetX == 0 ? 1.0f - Fraction.x : Fraction.x;
            const float WeightY = OffsetY == 0 ? 1.0f - Fraction.y : Fraction.y;
            const float SpatialWeight = WeightX * WeightY;
            if (SpatialWeight <= 0.0f)
                continue;

            // 5단계에서 이 출력 픽셀의 깊이를 읽은 위치와 일치시킨다.
            const float2 GuideUV = (float2(HalfPixel) + 0.5f) * HalfSizeAndInvSize.zw;
            const int2 GuidePixel = clamp(int2(GuideUV * FullSizeAndInvSize.xy), int2(0, 0), FullSize - int2(1, 1));
            const float GuideDepth = LinearDepthTexture.Load(int3(GuidePixel, 0));
            const float DepthWeight = ComputeDOFGuideWeight(TargetDepth, GuideDepth);
            const float SampleWeight = SpatialWeight * DepthWeight;
            if (SampleWeight <= 0.0f)
                continue;

            const float4 FarSample = FarBokehTexture.Load(int3(HalfPixel, 0));
            const float4 NearSample = NearBokehTexture.Load(int3(HalfPixel, 0));
            if (!all(isfinite(FarSample)) || !all(isfinite(NearSample)))
                continue;

            // RGB와 커버리지를 같은 가중치로 보간한다.
            FarLayer += FarSample * SampleWeight;
            NearLayer += NearSample * SampleWeight;

            // 빈 보케 샘플도 포함해 낮은 커버리지가 1로 변하지 않게 한다.
            WeightSum += SampleWeight;
        }
    }

    if (WeightSum > 0.000001f)
    {
        FarLayer /= WeightSum;
        NearLayer /= WeightSum;
    }
    else
    {
        FarLayer = float4(0.0f, 0.0f, 0.0f, 0.0f);
        NearLayer = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
}

float4 mainPS(FDepthOfFieldVertexOutput Input) : SV_Target0
{
    const int2 FullPixel = int2(Input.Position.xy);
    const float4 OriginalColor = SceneHDRTexture.Load(int3(FullPixel, 0));
    if (!all(isfinite(OriginalColor)))
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    const float TargetDepth = LinearDepthTexture.Load(int3(FullPixel, 0));
    const float2 CoCData = CoCAndDepthBlurTexture.Load(int3(FullPixel, 0));
    if (!IsValidDOFDepth(TargetDepth) || !all(isfinite(CoCData)))
        return OriginalColor;

    const float2 UV = Input.Position.xy * FullSizeAndInvSize.zw;
    float4 FarLayer;
    float4 NearLayer;
    UpsampleDOFLayers(UV, TargetDepth, FarLayer, NearLayer);

    // 4단계와 같은 정책으로 렌즈 반경과 추가 블러를 결합한다.
    const float AdditionalRadius = max(CoCData.y, 0.0f);
    const float EffectiveRadius = min(max(abs(CoCData.x), AdditionalRadius), DepthBlurParameters.z);
    const float FarBlend = CoCData.x >= 0.0f ? ComputeDOFBlurAmount(EffectiveRadius) : 0.0f;

    const float FarCoverage = saturate(FarLayer.a);
    const float NearCoverage = saturate(NearLayer.a);

    // Far RGB에는 커버리지가 이미 포함되어 있다.
    const float3 BackgroundColor = OriginalColor.rgb * (1.0f - FarCoverage * FarBlend) + FarLayer.rgb * FarBlend;

    // Near는 원본 샘플에서 전환했으므로 대상 픽셀의 CoC로 제한하지 않는다.
    const float3 CompositeColor = NearLayer.rgb + BackgroundColor * (1.0f - NearCoverage);

    // HDR 범위를 유지한다. 출력 A는 DOF 커버리지가 아니다.
    return float4(clamp(CompositeColor, -65504.0f, 65504.0f), OriginalColor.a);
}
