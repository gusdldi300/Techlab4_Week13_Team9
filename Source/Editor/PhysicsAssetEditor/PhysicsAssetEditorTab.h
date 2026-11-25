#pragma once

#include "Editor/AssetEditor/DockedAssetEditorTab.h"

class FPhysicsAssetEditor;

class FPhysicsAssetEditorTab final : public FDockedAssetEditorTab
{
public:
    explicit FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor);
    const char* GetTabName() const override;
    void OnRender(bool bActive) override;

protected:
    void BeforeRender() override;
    bool DrawFileMenuItems() override;
    void DrawEditMenu() override;
    void BuildDefaultLayout(ImGuiID DockSpaceId, const ImVec2& Size) override;

private:
    FPhysicsAssetEditor& PhysicsEditor;
    FString TabName;
    mutable FString DisplayTabName;
};
