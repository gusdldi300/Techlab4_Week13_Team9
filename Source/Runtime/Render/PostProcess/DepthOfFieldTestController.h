#pragma once

#include "Core/EngineString.h"
#include "Render/SceneView.h"
#include "Render/SceneRenderOptions.h"

class FUIDrawList;
struct FDepthOfFieldStats;

// 전용 테스트 씬의 카메라 이름으로만 활성화되는 시연 도구.
// 원본 카메라와 에디터 설정은 수정하지 않는다.
class FDepthOfFieldTestController
{
public:
    void Initialize(const FString& CameraName);
    bool IsEnabled() const { return bEnabled; }
    bool InputKey(int32 Key, bool bDown);
    void Apply(FSceneView& View, FSceneRenderOptions& Options) const;
    void Paint(FUIDrawList& DrawList, const FSceneView& View,
        const FDepthOfFieldStats& Stats, uint32 Width, uint32 Height) const;

private:
    int32 GetPresetCount() const;
    void ResetTest(int32 NewTestIndex);
    void BuildSettings(FCameraLensSettings& Lens, FDepthOfFieldSettings& Settings,
        float& StationOffset) const;

    bool bEnabled = false;
    int32 TestIndex = 0;
    int32 PresetIndex = 1;
    bool bShowOriginal = false;
    bool bCameraEnabled = true;
    bool bMaximumAperture = false;
    bool bFocusPlane = false;
    EDepthOfFieldDebugView DebugView = EDepthOfFieldDebugView::None;
};
