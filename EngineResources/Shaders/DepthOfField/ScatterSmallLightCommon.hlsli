#include "SmallLightCommon.hlsli"
#include "Aperture.hlsli"

Texture2D<float4> SceneHDRTexture : register(t0);
Texture2D<float2> CoCTexture : register(t1);
Texture2D<float> LinearDepthTexture : register(t2);
StructuredBuffer<FDOFSmallLightRegion> LightRegions : register(t3);
StructuredBuffer<uint> LightCount : register(t4);
StructuredBuffer<uint> ExtractionMask : register(t5);

struct FSmallLightVertexOutput
{
    float4 Position : SV_Position;
    nointerpolation float2 SourcePosition : TEXCOORD0;
    nointerpolation float Radius : TEXCOORD1;
    nointerpolation float Depth : TEXCOORD2;
    nointerpolation float3 Color : TEXCOORD3;
};

FSmallLightVertexOutput MakeSmallLightVertex(uint VertexID, uint InstanceID)
{
    FSmallLightVertexOutput Output = (FSmallLightVertexOutput)0;
    Output.Position = float4(2.0f, 2.0f, 0.0f, 1.0f);
    const uint RegionIndex = InstanceID / DOFSmallLightPixelsPerRegion;
    if (RegionIndex >= min(LightCount[0], uint(SmallLightParameters.x)))
        return Output;
    const FDOFSmallLightRegion Region = LightRegions[RegionIndex];
    const uint PixelIndex = InstanceID % DOFSmallLightPixelsPerRegion;
    const uint2 GridPosition = uint2(PixelIndex % DOFSmallLightSampleGridSize,
        PixelIndex / DOFSmallLightSampleGridSize);
    // 큰 후보도 16x16개 샘플 예산으로 그린다. 최대 2x2 원본 픽셀을 묶되,
    // 추출 마스크에 속한 픽셀의 색/위치/반경만 사용해 배경이 섞이지 않게 한다.
    const uint SampleStep = (max(Region.Bounds.z, Region.Bounds.w) +
        DOFSmallLightSampleGridSize - 1) / DOFSmallLightSampleGridSize;
    const uint2 Offset = GridPosition * SampleStep;
    if (any(Offset >= Region.Bounds.zw))
        return Output;

    float3 AccumulatedColor = 0.0f;
    float2 WeightedPosition = 0.0f;
    float WeightedRadius = 0.0f;
    float WeightedInverseDepth = 0.0f;
    float TotalWeight = 0.0f;
    [unroll]
    for (uint SampleY = 0; SampleY < 2; ++SampleY)
    {
        [unroll]
        for (uint SampleX = 0; SampleX < 2; ++SampleX)
        {
            if (SampleX >= SampleStep || SampleY >= SampleStep)
                continue;
            const uint2 PixelOffset = Offset + uint2(SampleX, SampleY);
            if (any(PixelOffset >= Region.Bounds.zw))
                continue;
            const uint2 SourcePixel = Region.Bounds.xy + PixelOffset;
            const uint MaskIndex = SourcePixel.y * uint(FullSizeAndInvSize.x) + SourcePixel.x;
            if (ExtractionMask[MaskIndex] != RegionIndex + 1)
                continue;
            const float2 CoCData = CoCTexture.Load(int3(SourcePixel, 0));
            const bool bNear = CoCData.x < 0.0f;
#if DOF_SMALL_LIGHT_NEAR
            if (!bNear)
                continue;
#else
            if (bNear)
                continue;
#endif
            const float3 Color = SceneHDRTexture.Load(int3(SourcePixel, 0)).rgb;
            const float Depth = LinearDepthTexture.Load(int3(SourcePixel, 0));
            const float Weight = max(GetSmallLightLuminance(Color), 1.0e-6f);
            AccumulatedColor += Color;
            WeightedPosition += (float2(SourcePixel) + 0.5f) * Weight;
            WeightedRadius += GetSmallLightRadius(CoCData) * Weight;
            WeightedInverseDepth +=
                (IsBackgroundDOFDepth(Depth) ? 0.0f : rcp(max(Depth, 1.0e-6f))) * Weight;
            TotalWeight += Weight;
        }
    }
    if (TotalWeight <= 0.0f)
        return Output;
    Output.SourcePosition = WeightedPosition / TotalWeight;
    Output.Radius = WeightedRadius / TotalWeight;
    Output.Depth = WeightedInverseDepth > 0.0f ?
        TotalWeight / WeightedInverseDepth : DOFBackgroundDepth;
    // 평균 대신 합을 사용해 묶인 원본 픽셀들의 광량을 보존한다.
    Output.Color = AccumulatedColor;
    static const float2 Corners[6] =
    {
        float2(-1, -1), float2(-1, 1), float2(1, -1),
        float2(1, -1), float2(-1, 1), float2(1, 1)
    };
    const float2 OutputPixelSize = FullSizeAndInvSize.xy * HalfSizeAndInvSize.zw;
    const float Padding = 0.5f * max(1.0f, 0.5f * length(OutputPixelSize));
    const float2 UV = (Output.SourcePosition +
        Corners[VertexID] * (Output.Radius + Padding)) * FullSizeAndInvSize.zw;
    Output.Position = float4(UV.x * 2.0f - 1.0f, 1.0f - UV.y * 2.0f, 0.0f, 1.0f);
    return Output;
}

float4 ComputeSmallLightPixel(FSmallLightVertexOutput Input)
{
    const float2 UV = Input.Position.xy * HalfSizeAndInvSize.zw;
    const float2 OffsetFull = UV * FullSizeAndInvSize.xy - Input.SourcePosition;
    const float2 OutputPixelSize = FullSizeAndInvSize.xy * HalfSizeAndInvSize.zw;
    const float OuterRadius = Input.Radius +
        0.5f * max(1.0f, 0.5f * length(OutputPixelSize));
    if (dot(OffsetFull, OffsetFull) > OuterRadius * OuterRadius)
        return 0.0f;
    const int2 FullSize = int2(FullSizeAndInvSize.xy);
    const int2 TargetPixel = clamp(int2(UV * FullSizeAndInvSize.xy),
        int2(0, 0), FullSize - 1);
    const float TargetDepth = LinearDepthTexture.Load(int3(TargetPixel, 0));
    if (!IsValidDOFDepth(TargetDepth))
        return 0.0f;
    if (!IsBackgroundDOFDepth(TargetDepth) &&
        (IsBackgroundDOFDepth(Input.Depth) ||
            Input.Depth > TargetDepth + max(0.001f, TargetDepth * 0.01f)))
        return 0.0f;

    const FDOFAperture Aperture = MakeDOFAperture();
    // 모든 광원은 같은 출력 픽셀 폭으로 필터링한다. 소스 축소 격자와 무관하다.
    const float Coverage = GetDOFApertureCoverage(
        Aperture, OffsetFull, Input.Radius, OutputPixelSize);
    float Weight = Coverage / max(Aperture.UnitArea * Input.Radius * Input.Radius, 1.0f);
#if DOF_SMALL_LIGHT_NEAR
    Weight *= ComputeDOFBlurAmount(Input.Radius);
#endif
    // 원본 픽셀 하나의 에너지만 추가한다. 기존 블러의 알파는 유지한다.
    return float4(Input.Color * Weight, 0.0f);
}
