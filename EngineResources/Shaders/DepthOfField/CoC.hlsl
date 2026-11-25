#include "DepthOfFieldCommon.hlsli"

Texture2D<float> SceneDepthTexture : register(t0);

struct FCoCOutput
{
    float LinearDepth : SV_Target0;
    float2 CoCAndDepthBlur : SV_Target1;
};

FDepthOfFieldVertexOutput mainVS(uint VertexID : SV_VertexID)
{
    return MakeDepthOfFieldVertex(VertexID);
}

bool HasValidCoCParameters()
{
    const bool bLensValid = all(isfinite(LensParameters)) && all(LensParameters > 0.0f) && LensParameters.z > LensParameters.x;
    const bool bBlurValid = all(isfinite(DepthBlurParameters)) && DepthBlurParameters.x >= 0.0f && DepthBlurParameters.y >= 0.0f && DepthBlurParameters.z > 0.0f && DepthBlurParameters.w > 0.0f;
    const bool bSizeValid = all(isfinite(FullSizeAndInvSize)) && all(FullSizeAndInvSize > 0.0f);
    return bLensValid && bBlurValid && bSizeValid;
}

bool TryReconstructDepthMeters(float2 PixelPosition, float DeviceDepth, out float DepthMeters)
{
    DepthMeters = DOFInvalidDepth;

    // SV_Position.xy는 이미 픽셀 중심 좌표다.
    const float2 UV = PixelPosition * FullSizeAndInvSize.zw;
    const float2 NdcPosition = float2(UV.x * 2.0f - 1.0f, 1.0f - UV.y * 2.0f);
    const float4 ViewPosition = mul(float4(NdcPosition, DeviceDepth, 1.0f), InverseProjection);

    if (!all(isfinite(ViewPosition)) || ViewPosition.w <= 0.0f)
        return false;

    // 이 엔진은 카메라 전방 축이 +X다.
    const float ReconstructedDepth = (ViewPosition.x / ViewPosition.w) * DepthBlurParameters.w;
    if (!isfinite(ReconstructedDepth) || ReconstructedDepth <= 0.0f)
        return false;

    DepthMeters = ReconstructedDepth;
    return true;
}

float ComputeSignedCoCRadius(float DepthMeters, bool bBackground)
{
    const float FocalLength = LensParameters.x;
    const float SensorHeight = LensParameters.y;
    const float FocusDistance = LensParameters.z;
    const float FStop = LensParameters.w;
    const float MaximumRadius = DepthBlurParameters.z;

    // 센서 위 CoC 지름을 원본 화면의 픽셀 반경으로 변환한다.
    const float CoCDiameterScale = (FocalLength * FocalLength) / (FStop * (FocusDistance - FocalLength));
    const float CoCRadiusScale = CoCDiameterScale * 0.5f * FullSizeAndInvSize.y / SensorHeight;
    if (!isfinite(CoCRadiusScale) || CoCRadiusScale <= 0.0f)
        return 0.0f;

    // 배경은 무한대 거리의 Far CoC로 취급한다.
    if (bBackground)
        return min(CoCRadiusScale, MaximumRadius);

    if (DepthMeters >= FocusDistance)
    {
        const float FarFactor = (DepthMeters - FocusDistance) / DepthMeters;
        return min(CoCRadiusScale * FarFactor, MaximumRadius);
    }

    // Near는 나누기 전에 한도를 검사해 매우 가까운 깊이도 처리한다.
    const float NearNumerator = CoCRadiusScale * (FocusDistance - DepthMeters);
    if (NearNumerator >= MaximumRadius * DepthMeters)
        return -MaximumRadius;

    return -NearNumerator / DepthMeters;
}

float ComputeAdditionalBlurRadius(float DepthMeters, bool bBackground)
{
    const float DistanceForHalfBlur = DepthBlurParameters.x;
    const float ConfiguredRadius = DepthBlurParameters.y;
    const float MaximumRadius = DepthBlurParameters.z;
    const float EffectiveMaximumRadius = min(ConfiguredRadius, MaximumRadius);

    // 0 거리 또는 0 반경은 추가 거리 블러 비활성이다.
    if (DistanceForHalfBlur <= 0.0f || EffectiveMaximumRadius <= 0.0f)
        return 0.0f;

    if (bBackground)
        return EffectiveMaximumRadius;

    const float BlurFraction = saturate(1.0f - exp2(-DepthMeters / DistanceForHalfBlur));
    return EffectiveMaximumRadius * BlurFraction;
}

FCoCOutput mainPS(FDepthOfFieldVertexOutput Input)
{
    FCoCOutput Output;
    Output.LinearDepth = DOFInvalidDepth;
    Output.CoCAndDepthBlur = float2(0.0f, 0.0f);

    if (!HasValidCoCParameters())
        return Output;

    const int2 PixelPosition = int2(Input.Position.xy);
    const float DeviceDepth = SceneDepthTexture.Load(int3(PixelPosition, 0));
    if (!isfinite(DeviceDepth) || DeviceDepth < 0.0f || DeviceDepth > 1.0f)
        return Output;

    // 정방향 깊이의 클리어 값은 배경으로 유지한다.
    const bool bBackground = DeviceDepth == 1.0f;
    float DepthMeters = DOFBackgroundDepth;
    if (!bBackground)
    {
        if (!TryReconstructDepthMeters(Input.Position.xy, DeviceDepth, DepthMeters))
            return Output;
    }

    Output.LinearDepth = DepthMeters;
    Output.CoCAndDepthBlur = float2(ComputeSignedCoCRadius(DepthMeters, bBackground), ComputeAdditionalBlurRadius(DepthMeters, bBackground));
    return Output;
}
