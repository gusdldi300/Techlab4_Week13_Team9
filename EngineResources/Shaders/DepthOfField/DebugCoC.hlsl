#include "DepthOfFieldCommon.hlsli"

Texture2D<float2> CoCAndDepthBlurTexture : register(t0);
Texture2D<float> LinearDepthTexture : register(t1);

cbuffer DepthOfFieldDebugConstants : register(b1)
{
    // 표시 모드, 최대 밝기에 대응하는 원본 픽셀 반경.
    float4 DebugParameters;
};

FDepthOfFieldVertexOutput mainVS(uint VertexID : SV_VertexID)
{
    return MakeDepthOfFieldVertex(VertexID);
}

float4 mainPS(FDepthOfFieldVertexOutput Input) : SV_Target0
{
    const int2 Pixel = int2(Input.Position.xy);
    const float LinearDepth = LinearDepthTexture.Load(int3(Pixel, 0));
    const float2 CoCData = CoCAndDepthBlurTexture.Load(int3(Pixel, 0));

    if (!IsValidDOFDepth(LinearDepth) || !all(isfinite(CoCData)))
        return float4(1.0f, 0.0f, 1.0f, 1.0f);

    const uint DebugView = uint(DebugParameters.x);
    const float DisplayRadius = max(DebugParameters.y, 0.5f);
    const float LensRadius = abs(CoCData.x);
    const float AdditionalRadius = max(CoCData.y, 0.0f);
    const float EffectiveRadius = min(max(LensRadius, AdditionalRadius), DepthBlurParameters.z);

    if (DebugView == 1)
    {
        const float Intensity = saturate(LensRadius / DisplayRadius);
        const float3 FocusColor = float3(0.0f, 1.0f, 0.0f);
        const float3 NearColor = float3(0.0f, 0.0f, 1.0f);
        const float3 FarColor = float3(1.0f, 0.0f, 0.0f);
        const float3 BlurColor = CoCData.x < 0.0f ? NearColor : FarColor;
        return float4(lerp(FocusColor, BlurColor, Intensity), 1.0f);
    }

    if (DebugView == 2)
    {
        const float Intensity = saturate(LensRadius / DisplayRadius);
        return float4(Intensity, Intensity, Intensity, 1.0f);
    }

    if (DebugView == 3)
    {
        const float Intensity = saturate(AdditionalRadius / DisplayRadius);
        return float4(Intensity, Intensity, 0.0f, 1.0f);
    }

    if (DebugView == 4)
    {
        const float Intensity = saturate(EffectiveRadius / DisplayRadius);
        return float4(Intensity, Intensity, Intensity, 1.0f);
    }

    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}
