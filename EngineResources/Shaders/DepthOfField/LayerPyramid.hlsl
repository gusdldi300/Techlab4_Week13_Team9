#include "DepthOfFieldCommon.hlsli"

Texture2D<float4> SourceColorTexture : register(t0);
Texture2D<float2> SourceParamsTexture : register(t1);

struct FLayerPyramidOutput
{
    float4 Color : SV_Target0;
    float2 Params : SV_Target1;
};

FDepthOfFieldVertexOutput mainVS(uint VertexID : SV_VertexID)
{
    return MakeDepthOfFieldVertex(VertexID);
}

bool ShouldReplacePyramidRepresentative(float2 CurrentParams, float2 SampleParams)
{
    if (!IsValidDOFDepth(CurrentParams.y))
        return true;

    // 최대 반경과 그 반경을 제공한 깊이를 한 쌍으로 유지한다.
    if (SampleParams.x != CurrentParams.x)
        return SampleParams.x > CurrentParams.x;

    if (IsBackgroundDOFDepth(CurrentParams.y))
        return !IsBackgroundDOFDepth(SampleParams.y);

    if (IsBackgroundDOFDepth(SampleParams.y))
        return false;

    return SampleParams.y < CurrentParams.y;
}

FLayerPyramidOutput mainPS(FDepthOfFieldVertexOutput Input)
{
    FLayerPyramidOutput Output;
    Output.Color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    Output.Params = float2(0.0f, DOFInvalidDepth);

    uint SourceWidth;
    uint SourceHeight;
    SourceColorTexture.GetDimensions(SourceWidth, SourceHeight);
    const int2 SourceSize = int2(SourceWidth, SourceHeight);
    const int2 TargetSize = (SourceSize + int2(1, 1)) / 2;
    const int2 TargetPixel = int2(Input.Position.xy);
    const int2 StartNumerator = TargetPixel * SourceSize;
    const int2 EndNumerator = (TargetPixel + int2(1, 1)) * SourceSize;
    const int2 FirstPixel = StartNumerator / TargetSize;
    const int2 EndPixelExclusive = (EndNumerator + TargetSize - int2(1, 1)) / TargetSize;

    // 홀수 크기도 같은 UV 영역의 면적 평균으로 축소한다.
    [unroll]
    for (int OffsetY = 0; OffsetY < 3; ++OffsetY)
    {
        [unroll]
        for (int OffsetX = 0; OffsetX < 3; ++OffsetX)
        {
            const int2 SourcePixel = FirstPixel + int2(OffsetX, OffsetY);
            if (any(SourcePixel >= EndPixelExclusive))
                continue;

            const int2 SourceStart = SourcePixel * TargetSize;
            const int2 SourceEnd = (SourcePixel + int2(1, 1)) * TargetSize;
            const int2 Overlap = max(min(EndNumerator, SourceEnd) - max(StartNumerator, SourceStart), int2(0, 0));
            if (any(Overlap <= 0))
                continue;

            const float4 SampleColor = SourceColorTexture.Load(int3(SourcePixel, 0));
            const float2 SampleParams = SourceParamsTexture.Load(int3(SourcePixel, 0));
            if (!all(isfinite(SampleColor)) || SampleColor.a <= 0.0f || !all(isfinite(SampleParams)) || !IsValidDOFDepth(SampleParams.y))
                continue;

            const float2 AxisWeights = float2(Overlap) / float2(SourceSize);
            const float SampleWeight = AxisWeights.x * AxisWeights.y;
            // 색상은 이미 커버리지가 곱해져 있다.
            Output.Color += SampleColor * SampleWeight;

            if (ShouldReplacePyramidRepresentative(Output.Params, SampleParams))
                Output.Params = SampleParams;
        }
    }

    // 반경 단위는 피라미드 단계와 무관하게 기본 절반 해상도를 유지한다.
    return Output;
}
