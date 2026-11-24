#pragma once

#include "Camera/CameraOptics.h"
#include "Core/ConfigFile.h"

#include <locale>
#include <sstream>

enum class EDepthOfFieldDebugView : uint8
{
    None,
    SignedCoC,
    LensRadius,
    AdditionalBlur,
    EffectiveRadius,
    Count
};

struct FDepthOfFieldViewSettings
{
    // 카메라 원본 대신 렌더링용 사본에만 적용한다.
    bool bOverrideCamera = false;
    FDepthOfFieldSettings OverrideSettings;

    EDepthOfFieldDebugView DebugView = EDepthOfFieldDebugView::None;

    // 디버그에서 최대 밝기로 표시할 원본 화면의 픽셀 반경.
    float DebugRadiusFullResolutionPixels = 16.0f;
};

inline void SanitizeDepthOfFieldViewSettings(FDepthOfFieldViewSettings& Settings)
{
    const uint32 DebugIndex = static_cast<uint32>(Settings.DebugView);
    if (DebugIndex >= static_cast<uint32>(EDepthOfFieldDebugView::Count))
        Settings.DebugView = EDepthOfFieldDebugView::None;

    SanitizeFloat(Settings.DebugRadiusFullResolutionPixels, 0.5f, 256.0f, 16.0f);
    SanitizeDepthOfFieldSettings(Settings.OverrideSettings);
}

inline FDepthOfFieldViewSettings ReadDepthOfFieldViewSettings(const FConfigFile& Config)
{
    FDepthOfFieldViewSettings Settings;
    const FString Section = "DepthOfField";

    const auto ReadFloat = [&Config, &Section](const char* Key, float Fallback)
    {
        std::istringstream Stream(Config.GetString(Section, Key, ""));
        Stream.imbue(std::locale::classic());

        float Value = Fallback;
        if (!(Stream >> Value) || !std::isfinite(Value))
            return Fallback;

        Stream >> std::ws;
        return Stream.eof() ? Value : Fallback;
    };

    Settings.bOverrideCamera = Config.GetBool(Section, "OverrideCamera", Settings.bOverrideCamera);

    FDepthOfFieldSettings& Override = Settings.OverrideSettings;
    Override.bEnabled = Config.GetBool(Section, "Enabled", Override.bEnabled);
    Override.FStop = ReadFloat("FStop", Override.FStop);
    Override.MinFStop = ReadFloat("MinFStop", Override.MinFStop);
    Override.ApertureBladeCount = Config.GetInt(Section, "ApertureBladeCount", Override.ApertureBladeCount);
    Override.FocalDistanceMeters = ReadFloat("FocalDistanceMeters", Override.FocalDistanceMeters);
    Override.DepthBlurKmFor50Percent = ReadFloat("DepthBlurKmFor50Percent", Override.DepthBlurKmFor50Percent);
    Override.DepthBlurRadiusFractionOfHeight = ReadFloat("DepthBlurRadiusFractionOfHeight", Override.DepthBlurRadiusFractionOfHeight);

    const int32 DebugIndex = Config.GetInt(Section, "DebugView", 0);
    if (DebugIndex >= 0 && DebugIndex < static_cast<int32>(EDepthOfFieldDebugView::Count))
        Settings.DebugView = static_cast<EDepthOfFieldDebugView>(DebugIndex);

    Settings.DebugRadiusFullResolutionPixels = ReadFloat("DebugRadiusFullResolutionPixels", Settings.DebugRadiusFullResolutionPixels);
    SanitizeDepthOfFieldViewSettings(Settings);
    return Settings;
}
