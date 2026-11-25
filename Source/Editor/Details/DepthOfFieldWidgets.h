#pragma once

#include "Camera/CameraOptics.h"
#include "imgui.h"

namespace DepthOfFieldWidgets
{
    inline void BeginField(const char* Label)
    {
        // Details 패널의 좁은 값 열에서도 단위와 입력 칸을 함께 표시한다.
        ImGui::TextWrapped("%s", Label);
        ImGui::SetNextItemWidth(-1.0f);
    }

    inline bool DrawSettings(FDepthOfFieldSettings& Settings)
    {
        ImGui::PushID("DepthOfFieldSettings");
        bool bChanged = false;

        bChanged |= ImGui::Checkbox("Enabled", &Settings.bEnabled);
        BeginField("Maximum Aperture (min f-number)");
        bChanged |= ImGui::DragFloat("##MinFStop", &Settings.MinFStop, 0.05f, 0.7f, 64.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        BeginField("F-Stop");
        bChanged |= ImGui::DragFloat("##FStop", &Settings.FStop, 0.05f, Settings.MinFStop, 64.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        BeginField("Aperture Blades");
        bChanged |= ImGui::SliderInt("##ApertureBlades", &Settings.ApertureBladeCount, 3, 16);
        BeginField("Focal Distance (m)");
        bChanged |= ImGui::DragFloat("##FocalDistance", &Settings.FocalDistanceMeters, 0.1f, 0.001f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        BeginField("Depth Blur km for 50%");
        bChanged |= ImGui::DragFloat("##DepthBlurDistance", &Settings.DepthBlurKmFor50Percent, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        float RadiusPercent = Settings.DepthBlurRadiusFractionOfHeight * 100.0f;
        BeginField("Depth Blur Radius (% of height)");
        if (ImGui::DragFloat("##DepthBlurRadius", &RadiusPercent, 0.01f, 0.0f, 25.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
        {
            Settings.DepthBlurRadiusFractionOfHeight = RadiusPercent * 0.01f;
            bChanged = true;
        }

        ImGui::TextWrapped("0 km or 0 radius disables additional blur.");

        if (ImGui::Button("Reset DOF"))
        {
            Settings = FDepthOfFieldSettings{};
            bChanged = true;
        }

        if (bChanged)
            SanitizeDepthOfFieldSettings(Settings);

        ImGui::PopID();
        return bChanged;
    }

    inline bool DrawLensSettings(FCameraLensSettings& Lens, float& VerticalFov, bool bShowFov)
    {
        ImGui::PushID("CameraLensSettings");
        bool bChanged = false;
        bool bUseFocalLength = Lens.bUseFocalLength;

        if (ImGui::Checkbox("Use Focal Length", &bUseFocalLength))
        {
            // 기준을 바꾸기 전에 현재 화각을 FOV와 렌즈 값에 반영한다.
            ResolveCameraLens(Lens, VerticalFov);
            Lens.bUseFocalLength = bUseFocalLength;
            bChanged = true;
        }

        BeginField("Sensor Height (mm)");
        bChanged |= ImGui::DragFloat("##SensorHeight", &Lens.SensorHeightMm, 0.1f, 1.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

        ImGui::BeginDisabled(!Lens.bUseFocalLength);
        BeginField("Focal Length (mm)");
        bChanged |= ImGui::DragFloat("##FocalLength", &Lens.FocalLengthMm, 0.1f, 0.001f, 100000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();

        if (bShowFov)
        {
            ImGui::BeginDisabled(Lens.bUseFocalLength);
            BeginField("Vertical FOV");
            bChanged |= ImGui::DragFloat("##VerticalFov", &VerticalFov, 0.1f, 1.0f, 179.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
        }

        if (ImGui::Button("Reset Lens / FOV"))
        {
            Lens = FCameraLensSettings{};
            VerticalFov = 60.0f;
            bChanged = true;
        }

        if (bChanged)
            ResolveCameraLens(Lens, VerticalFov);

        FCameraLensSettings ResolvedLens = Lens;
        float EffectiveFov = VerticalFov;
        ResolveCameraLens(ResolvedLens, EffectiveFov);

        ImGui::TextWrapped("Effective Vertical FOV: %.2f deg", EffectiveFov);
        ImGui::TextWrapped("Effective Focal Length: %.3f mm", ResolvedLens.FocalLengthMm);

        ImGui::PopID();
        return bChanged;
    }
}
