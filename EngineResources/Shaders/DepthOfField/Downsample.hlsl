#include "SmallLightCommon.hlsli"

Texture2D<float4> SceneHDRTexture : register(t0);
Texture2D<float> LinearDepthTexture : register(t1);
Texture2D<float2> CoCAndDepthBlurTexture : register(t2);
StructuredBuffer<uint> SmallLightMask : register(t3);

struct FDownsampleOutput
{
    float4 NearColor : SV_Target0;
    float4 FarColor : SV_Target1;
    float2 NearParams : SV_Target2;
    float2 FarParams : SV_Target3;
};

FDepthOfFieldVertexOutput mainVS(uint VertexID : SV_VertexID)
{
    return MakeDepthOfFieldVertex(VertexID);
}

bool ShouldReplaceDOFRepresentative(float2 CurrentParams, float SampleRadius, float SampleDepth)
{
    if (!IsValidDOFDepth(CurrentParams.y))
        return true;

    // 최대 반경을 제공한 원본의 깊이를 함께 보관한다.
    if (SampleRadius != CurrentParams.x)
        return SampleRadius > CurrentParams.x;

    // 같은 반경이면 가까운 실제 표면을 우선한다.
    if (IsBackgroundDOFDepth(CurrentParams.y))
        return !IsBackgroundDOFDepth(SampleDepth);

    if (IsBackgroundDOFDepth(SampleDepth))
        return false;

    return SampleDepth < CurrentParams.y;
}

void AccumulateDOFLayer(float3 SampleColor, float SampleWeight, float SampleRadius, float SampleDepth, inout float4 LayerColor, inout float2 LayerParams)
{
    // RGB는 이미 커버리지가 곱해진 색상이다.
    LayerColor += float4(SampleColor * SampleWeight, SampleWeight);

    if (ShouldReplaceDOFRepresentative(LayerParams, SampleRadius, SampleDepth))
        LayerParams = float2(SampleRadius, SampleDepth);
}

FDownsampleOutput mainPS(FDepthOfFieldVertexOutput Input)
{
    FDownsampleOutput Output;
    Output.NearColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    Output.FarColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    Output.NearParams = float2(0.0f, DOFInvalidDepth);
    Output.FarParams = float2(0.0f, DOFInvalidDepth);

    const int2 FullSize = int2(FullSizeAndInvSize.xy);
    const int2 HalfSize = int2(HalfSizeAndInvSize.xy);
    if (any(FullSize <= 0) || any(HalfSize <= 0))
        return Output;

    if (!isfinite(DepthBlurParameters.z) || DepthBlurParameters.z <= 0.0f)
        return Output;

    const int2 HalfPixel = int2(Input.Position.xy);

    // 영역 경계를 정수 분자로 표현해 홀수 해상도에서도 원본 픽셀을 보존한다.
    const int2 StartNumerator = HalfPixel * FullSize;
    const int2 EndNumerator = (HalfPixel + int2(1, 1)) * FullSize;
    const int2 FirstPixel = StartNumerator / HalfSize;
    const int2 EndPixelExclusive = (EndNumerator + HalfSize - int2(1, 1)) / HalfSize;

    // 짝수 크기는 축당 2개, 홀수 크기는 최대 3개가 겹친다.
    [unroll]
    for (int OffsetY = 0; OffsetY < 3; ++OffsetY)
    {
        [unroll]
        for (int OffsetX = 0; OffsetX < 3; ++OffsetX)
        {
            const int2 SourcePixel = FirstPixel + int2(OffsetX, OffsetY);
            if (any(SourcePixel >= EndPixelExclusive))
                continue;

            const int2 SourceStart = SourcePixel * HalfSize;
            const int2 SourceEnd = (SourcePixel + int2(1, 1)) * HalfSize;
            const int2 Overlap = max(min(EndNumerator, SourceEnd) - max(StartNumerator, SourceStart), int2(0, 0));
            if (any(Overlap <= 0))
                continue;

            // 원본 픽셀이 축소 픽셀에서 차지하는 면적 비율이다.
            const float2 AxisWeights = float2(Overlap) / float2(FullSize);
            const float SampleWeight = AxisWeights.x * AxisWeights.y;
            const float SampleDepth = LinearDepthTexture.Load(int3(SourcePixel, 0));
            if (!IsValidDOFDepth(SampleDepth))
                continue;

            const float2 CoCData = CoCAndDepthBlurTexture.Load(int3(SourcePixel, 0));
            float3 SampleColor = SceneHDRTexture.Load(int3(SourcePixel, 0)).rgb;
            if (!all(isfinite(CoCData)) || !all(isfinite(SampleColor)))
                continue;

            [branch]
            if (SmallLightParameters.x > 0.0f)
            {
                const uint MaskIndex = SourcePixel.y * uint(FullSize.x) + SourcePixel.x;
                if (SmallLightMask[MaskIndex] != 0)
                    SampleColor = 0.0f;
            }

            // 분류에는 렌즈 CoC의 원래 부호를 사용한다.
            const bool bNear = CoCData.x < 0.0f;
            const float AdditionalRadius = max(CoCData.y, 0.0f);
            const float FullRadius = min(max(abs(CoCData.x), AdditionalRadius), DepthBlurParameters.z);

            // 합성의 블러 전환이 시작되는 0.5px 이하 샘플은 원본에만 남긴다.
            // 축소 전에 RGB와 커버리지를 함께 제외해야 초점 색상이 이웃 배경의
            // 최대 반경을 물려받아 퍼지거나, 색만 빠져 검은 테두리가 생기지 않는다.
            // 추가 거리 블러가 켜져 있으면 위에서 결합한 실제 반경을 기준으로 한다.
            if (FullRadius <= 0.5f)
                continue;

            const float HalfRadius = ConvertDOFRadiusFullToHalf(FullRadius);

            if (bNear)
            {
                AccumulateDOFLayer(SampleColor, SampleWeight, HalfRadius, SampleDepth, Output.NearColor, Output.NearParams);
            }
            else
            {
                AccumulateDOFLayer(SampleColor, SampleWeight, HalfRadius, SampleDepth, Output.FarColor, Output.FarParams);
            }
        }
    }

    return Output;
}
