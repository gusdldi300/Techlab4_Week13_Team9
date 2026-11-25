#include "SmallLightCommon.hlsli"

Texture2D<float4> SceneHDRTexture : register(t0);
Texture2D<float2> CoCTexture : register(t1);
Texture2D<float> LinearDepthTexture : register(t2);
RWStructuredBuffer<FDOFSmallLightRegion> LightRegions : register(u0);
RWStructuredBuffer<uint> LightCount : register(u1);
RWStructuredBuffer<uint> ExtractionMask : register(u2);

groupshared float MinimumLuminance[256];
groupshared float PeakLuminance[256];
groupshared uint PeakPixel[256];
groupshared float Neighborhood[DOFSmallLightWindowPixelCount];
groupshared uint ValidCandidate;
groupshared uint Dominated;
groupshared uint MinimumX;
groupshared uint MinimumY;
groupshared uint MaximumX;
groupshared uint MaximumY;
groupshared uint RegionIndex;
groupshared float ExtractionThreshold;
groupshared int2 PeakPosition;

bool HasSmallLightContrast(float Peak, float Background)
{
    return Peak > max(Background * SmallLightParameters.z,
        Background + SmallLightParameters.w);
}

[numthreads(16, 16, 1)]
void mainCS(uint3 GroupID : SV_GroupID, uint3 ThreadID : SV_GroupThreadID,
    uint ThreadIndex : SV_GroupIndex)
{
    const uint2 FullSize = uint2(FullSizeAndInvSize.xy);
    const int WindowRadius = int(DOFSmallLightWindowRadius);
    const uint2 Pixel = GroupID.xy * DOFSmallLightTileSize + ThreadID.xy;
    const bool InBounds = all(Pixel < FullSize);
    float Luminance = 0.0f;
    if (InBounds)
    {
        const float3 Color = SceneHDRTexture.Load(int3(Pixel, 0)).rgb;
        if (all(isfinite(Color)))
            Luminance = GetSmallLightLuminance(Color);
    }
    MinimumLuminance[ThreadIndex] = InBounds ? Luminance : 1.0e30f;
    PeakLuminance[ThreadIndex] = Luminance;
    PeakPixel[ThreadIndex] = InBounds ? Pixel.y * FullSize.x + Pixel.x : 0xffffffff;
    GroupMemoryBarrierWithGroupSync();

    // 타일마다 가장 밝은 원본 픽셀 하나만 후보로 삼는다.
    [unroll]
    for (uint Step = 128; Step > 0; Step >>= 1)
    {
        if (ThreadIndex < Step)
        {
            MinimumLuminance[ThreadIndex] = min(MinimumLuminance[ThreadIndex],
                MinimumLuminance[ThreadIndex + Step]);
            const float OtherPeak = PeakLuminance[ThreadIndex + Step];
            const uint OtherPixel = PeakPixel[ThreadIndex + Step];
            if (OtherPeak > PeakLuminance[ThreadIndex] ||
                (OtherPeak == PeakLuminance[ThreadIndex] && OtherPixel < PeakPixel[ThreadIndex]))
            {
                PeakLuminance[ThreadIndex] = OtherPeak;
                PeakPixel[ThreadIndex] = OtherPixel;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (ThreadIndex == 0)
    {
        PeakPosition = int2(PeakPixel[0] % FullSize.x, PeakPixel[0] / FullSize.x);
        float Background = MinimumLuminance[0];
        // 16px 타일을 꽉 채운 광원도 원본 크기 제한 밖의 배경과 비교한다.
        // 타일 바로 옆만 검사하면 큰 광원의 중심 타일을 놓칠 수 있다.
        [unroll]
        for (int Side = 0; Side < 4; ++Side)
        {
            const int2 Offset = Side == 0 ? int2(-WindowRadius, 0) :
                Side == 1 ? int2(WindowRadius, 0) :
                Side == 2 ? int2(0, -WindowRadius) : int2(0, WindowRadius);
            const int2 GuidePixel = clamp(PeakPosition + Offset,
                int2(0, 0), int2(FullSize) - 1);
            Background = min(Background, GetSmallLightLuminance(
                SceneHDRTexture.Load(int3(GuidePixel, 0)).rgb));
        }
        const float2 CoCData = CoCTexture.Load(int3(PeakPosition, 0));
        const float Depth = LinearDepthTexture.Load(int3(PeakPosition, 0));
        ValidCandidate = HasSmallLightContrast(PeakLuminance[0], Background) &&
            all(PeakPosition >= WindowRadius) &&
            all(PeakPosition + WindowRadius < int2(FullSize)) &&
            all(isfinite(CoCData)) && IsValidDOFDepth(Depth) &&
            GetSmallLightRadius(CoCData) >= 2.0f &&
            LightCount[0] < uint(SmallLightParameters.x);
        Dominated = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    if (ValidCandidate == 0)
        return;

    float LocalMinimum = 1.0e30f;
    uint LocalDominated = 0;
    [loop]
    for (uint Index = ThreadIndex; Index < DOFSmallLightWindowPixelCount; Index += 256)
    {
        const int2 Offset = int2(Index % DOFSmallLightWindowSize,
            Index / DOFSmallLightWindowSize) - WindowRadius;
        const int2 SourcePixel = PeakPosition + Offset;
        const float3 Color = SceneHDRTexture.Load(int3(SourcePixel, 0)).rgb;
        const float Value = all(isfinite(Color)) ? GetSmallLightLuminance(Color) : 0.0f;
        Neighborhood[Index] = Value;
        LocalMinimum = min(LocalMinimum, Value);
        const uint PixelIndex = SourcePixel.y * FullSize.x + SourcePixel.x;
        // 인접 타일에서 같은 광원을 중복 검출하지 않는다.
        if (Value > PeakLuminance[0] ||
            (Value == PeakLuminance[0] && PixelIndex < PeakPixel[0]))
            LocalDominated = 1;
    }
    MinimumLuminance[ThreadIndex] = LocalMinimum;
    if (LocalDominated != 0)
        InterlockedOr(Dominated, 1);
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint Step = 128; Step > 0; Step >>= 1)
    {
        if (ThreadIndex < Step)
            MinimumLuminance[ThreadIndex] = min(MinimumLuminance[ThreadIndex],
                MinimumLuminance[ThreadIndex + Step]);
        GroupMemoryBarrierWithGroupSync();
    }
    if (ThreadIndex == 0)
    {
        ValidCandidate = Dominated == 0 &&
            HasSmallLightContrast(PeakLuminance[0], MinimumLuminance[0]);
        ExtractionThreshold = MinimumLuminance[0] +
            (PeakLuminance[0] - MinimumLuminance[0]) * 0.02f;
        MinimumX = MinimumY = DOFSmallLightWindowSize;
        MaximumX = MaximumY = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    if (ValidCandidate == 0)
        return;

    uint2 LocalMinimumPosition = uint2(DOFSmallLightWindowSize, DOFSmallLightWindowSize);
    uint2 LocalMaximumPosition = 0;
    [loop]
    for (uint Index = ThreadIndex; Index < DOFSmallLightWindowPixelCount; Index += 256)
    {
        if (Neighborhood[Index] > ExtractionThreshold)
        {
            const uint2 Position = uint2(Index % DOFSmallLightWindowSize,
                Index / DOFSmallLightWindowSize);
            LocalMinimumPosition = min(LocalMinimumPosition, Position);
            LocalMaximumPosition = max(LocalMaximumPosition, Position);
        }
    }
    InterlockedMin(MinimumX, LocalMinimumPosition.x);
    InterlockedMin(MinimumY, LocalMinimumPosition.y);
    InterlockedMax(MaximumX, LocalMaximumPosition.x);
    InterlockedMax(MaximumY, LocalMaximumPosition.y);
    GroupMemoryBarrierWithGroupSync();
    if (ThreadIndex == 0)
    {
        const uint2 Size = uint2(MaximumX - MinimumX + 1, MaximumY - MinimumY + 1);
        // 큰 면, 줄무늬, 창문 가장자리는 기존 블러에 남긴다.
        ValidCandidate = all(Size <= uint(SmallLightParameters.y)) &&
            MinimumX > 0 && MinimumY > 0 &&
            MaximumX < DOFSmallLightWindowSize - 1 &&
            MaximumY < DOFSmallLightWindowSize - 1;
        if (ValidCandidate != 0)
        {
            InterlockedAdd(LightCount[0], 1, RegionIndex);
            ValidCandidate = RegionIndex < uint(SmallLightParameters.x);
            if (ValidCandidate != 0)
            {
                FDOFSmallLightRegion Region;
                Region.Bounds = uint4(
                    PeakPosition + int2(MinimumX, MinimumY) - WindowRadius, Size);
                Region.Parameters = float4(ExtractionThreshold, 0.0f, 0.0f, 0.0f);
                LightRegions[RegionIndex] = Region;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (ValidCandidate == 0)
        return;

    const uint2 Size = uint2(MaximumX - MinimumX + 1, MaximumY - MinimumY + 1);
    const uint2 RegionStart = uint2(
        PeakPosition + int2(MinimumX, MinimumY) - WindowRadius);
    [loop]
    for (uint Index = ThreadIndex; Index < Size.x * Size.y; Index += 256)
    {
        const uint2 SourcePixel = RegionStart + uint2(Index % Size.x, Index / Size.x);
        const float3 SourceColor = SceneHDRTexture.Load(int3(SourcePixel, 0)).rgb;
        const float2 SourceCoC = CoCTexture.Load(int3(SourcePixel, 0));
        const float SourceDepth = LinearDepthTexture.Load(int3(SourcePixel, 0));
        if (all(isfinite(SourceColor)) && all(isfinite(SourceCoC)) &&
            IsValidDOFDepth(SourceDepth) && GetSmallLightRadius(SourceCoC) >= 2.0f &&
            GetSmallLightLuminance(SourceColor) > ExtractionThreshold)
            ExtractionMask[SourcePixel.y * FullSize.x + SourcePixel.x] = RegionIndex + 1;
    }
}
