#pragma once

#include "Render/PostProcess/DepthOfFieldViewSettings.h"

// 최종 렌더용 사본에서 채우며, 각 뷰의 RenderData가 따로 보관한다.
struct FDepthOfFieldStats
{
    bool bHasData = false;
    const char* State = "Not rendered";
    bool bOverrideCamera = false;
    bool bFocusPlaneDrawn = false;
    EDepthOfFieldDebugView DebugView = EDepthOfFieldDebugView::None;
    FCameraLensSettings Lens;
    FDepthOfFieldSettings Settings;
    float MaximumRadiusFullResolutionPixels = 0.0f;
};
