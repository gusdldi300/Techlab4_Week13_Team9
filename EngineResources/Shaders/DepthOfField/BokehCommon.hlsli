#ifndef DOF_BOKEH_COMMON_HLSLI
#define DOF_BOKEH_COMMON_HLSLI

#include "Aperture.hlsli"

// 기본 절반 해상도와 9단계 피라미드. C++ 입력 슬롯 순서와 일치시킨다.
Texture2D<float4> LayerColors[10] : register(t0);
Texture2D<float2> LayerParams[10] : register(t10);

#if DOF_NEAR_LAYER
Texture2D<float> SearchRadiusTexture : register(t20);
#else
Texture2D<float2> SearchParamsTexture : register(t20);
#endif
Texture2D<float> FullLinearDepthTexture : register(t21);

int2 GetDOFLayerSize(uint Level)
{
    const int Divisor = 1 << Level;
    return (int2(HalfSizeAndInvSize.xy) + int2(Divisor - 1, Divisor - 1)) / Divisor;
}

float4 LoadDOFLayerColor(uint Level, int2 Pixel)
{
    // Shader Model 5의 텍스처 배열은 상수 인덱스로 접근한다.
    switch (Level)
    {
    case 0: return LayerColors[0].Load(int3(Pixel, 0));
    case 1: return LayerColors[1].Load(int3(Pixel, 0));
    case 2: return LayerColors[2].Load(int3(Pixel, 0));
    case 3: return LayerColors[3].Load(int3(Pixel, 0));
    case 4: return LayerColors[4].Load(int3(Pixel, 0));
    case 5: return LayerColors[5].Load(int3(Pixel, 0));
    case 6: return LayerColors[6].Load(int3(Pixel, 0));
    case 7: return LayerColors[7].Load(int3(Pixel, 0));
    case 8: return LayerColors[8].Load(int3(Pixel, 0));
    case 9: return LayerColors[9].Load(int3(Pixel, 0));
    }

    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}

float2 LoadDOFLayerParams(uint Level, int2 Pixel)
{
    switch (Level)
    {
    case 0: return LayerParams[0].Load(int3(Pixel, 0));
    case 1: return LayerParams[1].Load(int3(Pixel, 0));
    case 2: return LayerParams[2].Load(int3(Pixel, 0));
    case 3: return LayerParams[3].Load(int3(Pixel, 0));
    case 4: return LayerParams[4].Load(int3(Pixel, 0));
    case 5: return LayerParams[5].Load(int3(Pixel, 0));
    case 6: return LayerParams[6].Load(int3(Pixel, 0));
    case 7: return LayerParams[7].Load(int3(Pixel, 0));
    case 8: return LayerParams[8].Load(int3(Pixel, 0));
    case 9: return LayerParams[9].Load(int3(Pixel, 0));
    }

    return float2(0.0f, DOFInvalidDepth);
}

void GetDOFSampleBounds(float2 CenterUV, float2 RadiusUV, uint Level, out int2 FirstPixel, out int2 LastPixel)
{
    const int2 LayerSize = GetDOFLayerSize(Level);
    FirstPixel = clamp(int2(floor((CenterUV - RadiusUV) * float2(LayerSize))), int2(0, 0), LayerSize - int2(1, 1));
    LastPixel = clamp(int2(floor((CenterUV + RadiusUV) * float2(LayerSize))), int2(0, 0), LayerSize - int2(1, 1));
}

bool IsDOFSourceVisible(float SourceDepth, float TargetDepth)
{
    if (!IsValidDOFDepth(SourceDepth) || !IsValidDOFDepth(TargetDepth))
        return false;

    if (IsBackgroundDOFDepth(TargetDepth))
        return true;

    if (IsBackgroundDOFDepth(SourceDepth))
        return false;

    // 대표 깊이에 기반한 보수적인 가시성 근사다.
    const float DepthTolerance = max(0.001f, TargetDepth * 0.01f);
    return SourceDepth <= TargetDepth + DepthTolerance;
}

float4 ComputeDOFBokeh(FDepthOfFieldVertexOutput Input)
{
    const int2 HalfPixel = int2(Input.Position.xy);
    const float2 CenterUV = Input.Position.xy * HalfSizeAndInvSize.zw;
#if DOF_NEAR_LAYER
    const float SearchRadiusHalf = SearchRadiusTexture.Load(int3(HalfPixel, 0));
    const uint SampleBudget = uint(clamp(ApertureParameters.z, 8.0f, 256.0f));
#else
    const float SearchRadiusHalf = SearchParamsTexture.Load(int3(HalfPixel, 0)).x;
    const uint SampleBudget = uint(clamp(ApertureParameters.w, 8.0f, 256.0f));
#endif
    if (!isfinite(SearchRadiusHalf) || SearchRadiusHalf <= 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    const float SearchRadiusFull = ConvertDOFRadiusHalfToFull(SearchRadiusHalf);
    if (SearchRadiusFull < 0.5f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    const int2 FullSize = int2(FullSizeAndInvSize.xy);
    const int2 FullPixel = clamp(int2(CenterUV * FullSizeAndInvSize.xy), int2(0, 0), FullSize - int2(1, 1));
    const float TargetDepth = FullLinearDepthTexture.Load(int3(FullPixel, 0));
    if (!IsValidDOFDepth(TargetDepth))
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    const float2 RadiusUV = ConvertDOFRadiusHalfToUV(SearchRadiusHalf);
    uint SelectedLevel = 0;
    int2 FirstPixel = int2(0, 0);
    int2 LastPixel = int2(0, 0);

    // 전체 영역을 훑을 수 있는 단계로 낮춰 점 샘플 사이의 누락을 줄인다.
    [loop]
    for (uint Level = 0; Level < 10; ++Level)
    {
        GetDOFSampleBounds(CenterUV, RadiusUV, Level, FirstPixel, LastPixel);
        const int2 SampleCount = LastPixel - FirstPixel + int2(1, 1);
        SelectedLevel = Level;
        if (SampleCount.x * SampleCount.y <= int(SampleBudget))
            break;
    }

    const int2 LayerSize = GetDOFLayerSize(SelectedLevel);
    const float2 CellSizeFull = FullSizeAndInvSize.xy / float2(LayerSize);
    const float CellAreaFull = CellSizeFull.x * CellSizeFull.y;
    const FDOFAperture Aperture = MakeDOFAperture();
    float4 AccumulatedColor = float4(0.0f, 0.0f, 0.0f, 0.0f);

    [loop]
    for (int SourceY = FirstPixel.y; SourceY <= LastPixel.y; ++SourceY)
    {
        [loop]
        for (int SourceX = FirstPixel.x; SourceX <= LastPixel.x; ++SourceX)
        {
            const int2 SourcePixel = int2(SourceX, SourceY);
            const float2 Params = LoadDOFLayerParams(SelectedLevel, SourcePixel);
            if (!all(isfinite(Params)) || !IsDOFSourceVisible(Params.y, TargetDepth))
                continue;

            const float RadiusFull = ConvertDOFRadiusHalfToFull(Params.x);
            if (RadiusFull < 0.5f)
                continue;

            const float2 SourceUV = (float2(SourcePixel) + 0.5f) / float2(LayerSize);
            const float2 OffsetFull = (CenterUV - SourceUV) * FullSizeAndInvSize.xy;
            const float ApertureCoverage = GetDOFApertureCoverage(Aperture, OffsetFull, RadiusFull, CellSizeFull);
            if (ApertureCoverage <= 0.0f)
                continue;

            const float4 SampleColor = LoadDOFLayerColor(SelectedLevel, SourcePixel);
            if (!all(isfinite(SampleColor)) || SampleColor.a <= 0.0f)
                continue;

            const float KernelArea = max(Aperture.UnitArea * RadiusFull * RadiusFull, CellAreaFull);
            const float SampleWeight = CellAreaFull * ApertureCoverage / KernelArea;
            // 면적 밀도로 적분하며 HDR 색상에 커버리지를 다시 곱하지 않는다.
            AccumulatedColor += SampleColor * SampleWeight;
        }
    }

    return PackDOFBokeh(AccumulatedColor);
}

#endif
