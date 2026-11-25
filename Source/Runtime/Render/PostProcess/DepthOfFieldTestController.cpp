#include "EnginePCH.h"
#include "Render/PostProcess/DepthOfFieldTestController.h"

#include "Asset/AssetManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Input/KeyCode.h"
#include "Render/PostProcess/DepthOfFieldStats.h"
#include "Text/Font.h"
#include "UI/UIDrawList.h"

#include <array>
#include <format>

namespace
{
    constexpr std::array<float, 5> FocusDistances{ 1.5f, 3.0f, 5.0f, 10.0f, 20.0f };
    constexpr std::array<float, 8> FStops{ 1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f };
    constexpr std::array<int32, 5> BladeCounts{ 3, 5, 7, 9, 16 };
    constexpr std::array<float, 4> BlurRadii{ 0.0f, 0.01f, 0.02f, 0.04f };
    constexpr std::array<float, 3> EdgeFocusDistances{ 3.0f, 1.25f, 1.5f };
    constexpr std::array<float, 3> MaximumApertures{ 1.4f, 2.0f, 2.8f };
    constexpr const char* TestNames[]{
        "초점 거리", "F-Stop", "보케 날 수", "추가 거리 블러",
        "깊이 경계", "렌즈와 센서", "최대 개방 조리개"
    };

    void DrawPanel(FUIDrawList& DrawList, const UFont& Font, const TArray<FString>& Lines,
        float X, float Y, float Width, float FontSize)
    {
        const float LineHeight = FUIDrawList::MeasureText("Ag", Font, FontSize).Y;
        constexpr float Padding = 8.0f;
        DrawList.AddRect({ X, Y, Width, LineHeight * Lines.Num() + Padding * 2.0f },
            FLinearColor(0.015f, 0.02f, 0.03f, 0.92f));
        for (int32 Index = 0; Index < Lines.Num(); ++Index)
        {
            const FLinearColor Color = Index == 0
                ? FLinearColor(1.0f, 0.85f, 0.2f, 1.0f) : FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
            DrawList.AddText({ X + Padding, Y + Padding + Index * LineHeight,
                Width - Padding * 2.0f, LineHeight }, Lines[Index], Font, FontSize, Color, 0);
        }
    }
}

void FDepthOfFieldTestController::Initialize(const FString& CameraName)
{
    const bool bTestScene = CameraName.find("DOF_InteractiveLab_Camera") != FString::npos;
    if (bTestScene != bEnabled)
    {
        bEnabled = bTestScene;
        ResetTest(0);
    }
}

int32 FDepthOfFieldTestController::GetPresetCount() const
{
    constexpr int32 Counts[]{ 5, 8, 5, 4, 3, 3, 3 };
    return Counts[TestIndex];
}

void FDepthOfFieldTestController::ResetTest(int32 NewTestIndex)
{
    TestIndex = NewTestIndex;
    PresetIndex = TestIndex == 0 ? 1 : 0;
    bShowOriginal = false;
    bCameraEnabled = true;
    bMaximumAperture = false;
    bFocusPlane = false;
    DebugView = EDepthOfFieldDebugView::None;
}

bool FDepthOfFieldTestController::InputKey(int32 Key, bool bDown)
{
    if (!bEnabled) return false;
    // 테스트 구도를 고정하고 Pawn의 이동·시점 입력을 소비한다.
    if (!bDown) return true;
    if (Key >= '1' && Key <= '7') ResetTest(Key - '1');
    else if (Key == '0') ResetTest(TestIndex);
    else if (Key == static_cast<int32>(EKeyCode::Left))
        PresetIndex = (PresetIndex + GetPresetCount() - 1) % GetPresetCount();
    else if (Key == static_cast<int32>(EKeyCode::Right))
        PresetIndex = (PresetIndex + 1) % GetPresetCount();
    else if (Key == 'B') bShowOriginal = !bShowOriginal;
    else if (Key == 'E') bCameraEnabled = !bCameraEnabled;
    else if (Key == 'O') bMaximumAperture = !bMaximumAperture;
    else if (Key == 'P') bFocusPlane = !bFocusPlane;
    else if (Key == 'V')
    {
        DebugView = static_cast<EDepthOfFieldDebugView>(
            (static_cast<int32>(DebugView) + 1) % static_cast<int32>(EDepthOfFieldDebugView::Count));
    }
    return true;
}

void FDepthOfFieldTestController::BuildSettings(FCameraLensSettings& Lens,
    FDepthOfFieldSettings& Settings, float& StationOffset) const
{
    Lens = {};
    Lens.bUseFocalLength = true;
    Lens.FocalLengthMm = 85.0f;
    Lens.SensorHeightMm = 24.0f;
    Settings = {};
    Settings.bEnabled = bCameraEnabled;
    Settings.FStop = 1.4f;
    Settings.FocalDistanceMeters = 3.0f;
    StationOffset = 0.0f;

    switch (TestIndex)
    {
    case 0: Settings.FocalDistanceMeters = FocusDistances[PresetIndex]; break;
    case 1: Settings.FStop = FStops[PresetIndex]; break;
    case 2:
        StationOffset = 100.0f;
        Settings.FStop = 2.8f;
        Settings.FocalDistanceMeters = 1.5f;
        Settings.ApertureBladeCount = BladeCounts[PresetIndex];
        break;
    case 3:
        Settings.FStop = 16.0f;
        Settings.DepthBlurKmFor50Percent = 0.01f;
        Settings.DepthBlurRadiusFractionOfHeight = BlurRadii[PresetIndex];
        break;
    case 4:
        StationOffset = 200.0f;
        Settings.FocalDistanceMeters = EdgeFocusDistances[PresetIndex];
        break;
    case 5:
        Lens.FocalLengthMm = PresetIndex == 1 ? 50.0f : 85.0f;
        Lens.SensorHeightMm = PresetIndex == 2 ? 36.0f : 24.0f;
        break;
    case 6:
        StationOffset = 100.0f;
        Settings.FStop = 2.8f;
        Settings.FocalDistanceMeters = 1.5f;
        Settings.ApertureBladeCount = 5;
        Settings.MinFStop = MaximumApertures[PresetIndex];
        break;
    }
    if (bMaximumAperture && (TestIndex == 2 || TestIndex == 6))
        Settings.FStop = Settings.MinFStop;
}

void FDepthOfFieldTestController::Apply(FSceneView& View, FSceneRenderOptions& Options) const
{
    if (!bEnabled) return;
    float StationOffset = 0.0f;
    BuildSettings(View.LensSettings, View.DepthOfFieldSettings, StationOffset);
    ResolveCameraLens(View.LensSettings, View.FieldOfView);
    SanitizeDepthOfFieldSettings(View.DepthOfFieldSettings);
    FinalizeCameraOptics(View.LensSettings, View.DepthOfFieldSettings, View.FieldOfView);

    FMinimalViewInfo Camera;
    Camera.Location = FVector(0.0f, StationOffset, 1.5f);
    Camera.Rotation = FRotator(0.0f, 0.0f, 0.0f);
    Camera.FOV = View.FieldOfView;
    Camera.AspectRatio = View.AspectRatio;
    View.ViewLocation = Camera.Location;
    View.ViewRotation = Camera.Rotation;
    View.ViewForward = FVector(1.0f, 0.0f, 0.0f);
    View.NearClip = Camera.NearClip;
    View.FarClip = Camera.FarClip;
    View.bIsPerspective = true;
    View.bIsWireframe = false;
    View.ViewMatrix = Camera.CalculateViewMatrix();
    View.ProjectionMatrix = Camera.CalculateProjectionMatrix();
    View.ViewProjectionMatrix = View.ViewMatrix * View.ProjectionMatrix;
    View.Frustum = ExtractFrustumPlanes(View.ViewProjectionMatrix);
    View.FrustumCorners = GenerateFrustumCorners(View.ViewProjectionMatrix.Inverse());
    View.OverlayColor.A = 0.0f;
    View.LetterboxAmount = 0.0f;
    View.VignetteIntensity = 0.0f;

    Options.bDrawPrimitives = true;
    Options.bEnableDepthOfField = !bShowOriginal;
    Options.DepthOfField = {};
    Options.DepthOfField.DebugView = bShowOriginal ? EDepthOfFieldDebugView::None : DebugView;
    Options.DepthOfField.DebugRadiusFullResolutionPixels = 48.0f;
    Options.DepthOfField.bDrawFocusPlane = bFocusPlane && !bShowOriginal;
    Options.bShowSceneDepth = false;
    Options.bShowWorldNormal = false;
    Options.bShowLightCulling = false;
    Options.bShowCascade = false;
    Options.bEnableFog = false;
    Options.LightingModel = ELightingModel::Unlit;
}

void FDepthOfFieldTestController::Paint(FUIDrawList& DrawList, const FSceneView& View,
    const FDepthOfFieldStats& Stats, uint32 Width, uint32 Height) const
{
    if (!bEnabled) return;
    UFont* Font = UAssetManager::GetAssetByPath<UFont>("Engine/Fonts/Pretendard.json");
    if (!Font) return;
    const float FontSize = 16.0f * std::clamp(static_cast<float>(Width) / 1100.0f, 0.65f, 1.15f);
    const float LineHeight = FUIDrawList::MeasureText("Ag", *Font, FontSize).Y;
    const float PanelWidth = static_cast<float>(Width) - 16.0f;

    TArray<FString> Header;
    Header.Add(std::format("DoF 기능 확인  |  {}. {}", TestIndex + 1, TestNames[TestIndex]));
    Header.Add("1 초점  2 F-Stop  3 보케  4 추가 블러  5 경계  6 렌즈  7 최대 개방");
    Header.Add("좌/우: 값 변경   B: 원본 비교   V: 시각화   P: 초점면   E: 활성화   0: 초기화");
    DrawPanel(DrawList, *Font, Header, 8.0f, 8.0f, PanelWidth, FontSize);

    const bool bSettingsMatch = Stats.bHasData
        && !Stats.bOverrideCamera
        && Stats.Settings.bEnabled == View.DepthOfFieldSettings.bEnabled
        && std::abs(Stats.Settings.FStop - View.DepthOfFieldSettings.FStop) < 0.001f
        && std::abs(Stats.Settings.FocalDistanceMeters - View.DepthOfFieldSettings.FocalDistanceMeters) < 0.001f
        && Stats.Settings.ApertureBladeCount == View.DepthOfFieldSettings.ApertureBladeCount
        && std::abs(Stats.Settings.MinFStop - View.DepthOfFieldSettings.MinFStop) < 0.001f
        && std::abs(Stats.Lens.FocalLengthMm - View.LensSettings.FocalLengthMm) < 0.001f
        && std::abs(Stats.Lens.SensorHeightMm - View.LensSettings.SensorHeightMm) < 0.001f
        && std::abs(Stats.Settings.DepthBlurKmFor50Percent
            - View.DepthOfFieldSettings.DepthBlurKmFor50Percent) < 0.00001f
        && std::abs(Stats.Settings.DepthBlurRadiusFractionOfHeight
            - View.DepthOfFieldSettings.DepthBlurRadiusFractionOfHeight) < 0.00001f;

    TArray<FString> Instructions;
    Instructions.Add(std::format("{}  |  설정 적용: {}  |  {}",
        bShowOriginal ? "원본 (DoF OFF)" : "DoF 결과", bSettingsMatch ? "일치" : "확인 필요", Stats.State));
    Instructions.Add(std::format("현재: f/{:.1f}  /  초점 {:.2f}m  /  렌즈 {:.0f}mm  /  센서 {:.0f}mm  /  {} blades",
        Stats.Settings.FStop, Stats.Settings.FocalDistanceMeters,
        Stats.Lens.FocalLengthMm, Stats.Lens.SensorHeightMm, Stats.Settings.ApertureBladeCount));

    if (!bShowOriginal && DebugView != EDepthOfFieldDebugView::None)
    {
        switch (DebugView)
        {
        case EDepthOfFieldDebugView::SignedCoC:
            Instructions.Add("확인: 초점 앞은 초록, 초점은 검정, 초점 뒤는 파랑이어야 합니다.");
            break;
        case EDepthOfFieldDebugView::LensRadius:
            Instructions.Add("확인: 초점 표적은 검정에 가깝고, 렌즈 흐림 반경이 클수록 밝아야 합니다.");
            break;
        case EDepthOfFieldDebugView::AdditionalBlur:
            Instructions.Add("확인: 추가 블러 OFF이면 검정. 활성일 때는 멀수록 노랑이 밝아야 합니다.");
            break;
        case EDepthOfFieldDebugView::EffectiveRadius:
            Instructions.Add("확인: Lens Radius와 Additional Blur 중 큰 반경이 최종 밝기에 대응합니다.");
            break;
        default: break;
        }
        Instructions.Add("V로 일반 화면까지 순환하면 체크무늬의 선명도와 실제 보케를 비교할 수 있습니다.");
    }
    else if (bShowOriginal || !bCameraEnabled)
        Instructions.Add("확인: 모든 불투명 패널이 선명한 원본입니다. B 또는 E로 DoF 결과와 비교하세요.");
    else if (TestIndex == 0)
        Instructions.Add(std::format("확인: {:.1f}m 표적의 체크와 숫자만 가장 선명해야 합니다.",
            View.DepthOfFieldSettings.FocalDistanceMeters));
    else if (TestIndex == 1)
        Instructions.Add("확인: 3m는 선명하게 유지되고, f-number가 커질수록 다른 패널의 흐림이 줄어야 합니다.");
    else if (TestIndex == 2 || TestIndex == 6)
    {
        Instructions.Add("확인: 같은 밝은 점의 윤곽을 비교하세요. O: 최대 개방 원형 / 조인 조리개 전환");
        Instructions.Add(bMaximumAperture
            ? "최대 개방 상태: 날 수에 관계없이 원형이어야 합니다. O로 조인 조리개로 복귀하세요."
            : (TestIndex == 2
                ? "조인 조리개에서는 날 수에 따라 윤곽이 변하고, 최대 개방에서는 원형이어야 합니다."
                : "f/2.8 고정: 최대 개방값이 2.8에 가까워질수록 윤곽이 원형에 가까워져야 합니다."));
    }
    else if (TestIndex == 3)
    {
        const float Radius = Stats.Settings.DepthBlurRadiusFractionOfHeight * Height;
        Instructions.Add(std::format("예상 추가 반경: 10m = {:.1f}px, 20m = {:.1f}px (수식값, GPU 실측 아님)",
            std::min(Radius, Stats.MaximumRadiusFullResolutionPixels) * 0.5f,
            std::min(Radius, Stats.MaximumRadiusFullResolutionPixels) * 0.75f));
        Instructions.Add("V로 Additional Blur 확인: 10m보다 20m가 밝아야 합니다. 반경 0이면 추가 블러 OFF.");
    }
    else if (TestIndex == 4)
    {
        Instructions.Add("확인: 좌상단 막대와 우상단 프레임의 퍼짐, 좌하단 배경색 침범과 검은 띠를 비교하세요.");
        Instructions.Add("우하단 투명 패널은 자체 깊이를 쓰지 않는 현재 방식의 한계 확인용입니다.");
    }
    else if (TestIndex == 5)
        Instructions.Add("확인: 85/24mm보다 50/24mm 또는 85/36mm에서 화각이 넓어져야 합니다.");
    DrawPanel(DrawList, *Font, Instructions, 8.0f,
        Height - LineHeight * Instructions.Num() - 24.0f, PanelWidth, FontSize);

    if (TestIndex == 0 || TestIndex == 1 || TestIndex == 3 || TestIndex == 5)
    {
        constexpr float HorizontalFactors[]{ -0.195764706f, -0.097882353f, 0.0f, 0.097882353f, 0.195764706f };
        for (size_t Index = 0; Index < FocusDistances.size(); ++Index)
        {
            const float Depth = FocusDistances[Index];
            const FVector4 Position(Depth, Depth * HorizontalFactors[Index], 1.5f + Depth * 0.095f, 1.0f);
            const FVector4 Clip = Position * View.ViewProjectionMatrix;
            if (Clip.W <= 0.0f) continue;
            const float PixelX = (Clip.X / Clip.W * 0.5f + 0.5f) * Width;
            const float PixelY = (0.5f - Clip.Y / Clip.W * 0.5f) * Height;
            const bool bInFocus = std::abs(Depth - View.DepthOfFieldSettings.FocalDistanceMeters) < 0.01f;
            const FString Label = std::format("{:.1f}m{}", Depth, bInFocus ? " [초점]" : "");
            const FVector2 Size = FUIDrawList::MeasureText(Label, *Font, FontSize);
            DrawList.AddRect({ PixelX - Size.X * 0.5f - 3.0f, PixelY, Size.X + 6.0f, LineHeight },
                FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
            DrawList.AddText({ PixelX - Size.X * 0.5f, PixelY, Size.X, LineHeight },
                Label, *Font, FontSize, bInFocus
                    ? FLinearColor(0.4f, 1.0f, 0.4f, 1.0f) : FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), 0);
        }
    }
}
