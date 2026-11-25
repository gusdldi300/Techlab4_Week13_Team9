#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetEditor.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Editor/SkeletalMeshEditor/Panels/BoneHierarchyPanel.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Render/SkeletalMesh.h"
#include "Core/Misc/Paths.h"
#include "Editor/Details/PropertyEditor.h"
#include "Editor/EditorUI/EditorStyle.h"

namespace
{
    class FPhysicsViewportPanel final : public FSkeletalPreviewViewportPanel
    {
    public:
        explicit FPhysicsViewportPanel(FPhysicsAssetEditor& InEditor)
            : FSkeletalPreviewViewportPanel(InEditor), PhysicsEditor(InEditor) {}
    private:
        FPhysicsAssetEditor& PhysicsEditor;
        void DrawContent() override
        {
            FSkeletalPreviewViewportPanel::DrawContent();
            const auto* Client = PhysicsEditor.GetViewportClient();
            const ImVec2 Size = ImGui::GetItemRectSize();
            if (!Client || Size.x < 1 || Size.y < 1 || static_cast<uint32>(Size.x) != Client->GetWidth() ||
                static_cast<uint32>(Size.y) != Client->GetHeight()) return;
            const ImVec2 Origin = ImGui::GetItemRectMin();
            const bool Hovered = ImGui::IsItemHovered();
            auto* Draw = ImGui::GetWindowDrawList();
            Draw->PushClipRect(Origin,ImVec2(Origin.x+Size.x,Origin.y+Size.y),true);
            for (const auto& Handle : PhysicsEditor.GetConstraintHandles())
            {
                const ImVec2 P(Origin.x+Handle.Position.X,Origin.y+Handle.Position.Y);
                const ImVec2 A(Origin.x+Handle.Anchor.X,Origin.y+Handle.Anchor.Y);
                const ImU32 Color = Handle.bParentFrame ? IM_COL32(30,235,255,255) : IM_COL32(255,225,35,255);
                const bool Selected = PhysicsEditor.IsConstraintFrameSelected(Handle.ChildBone,Handle.bParentFrame);
                Draw->AddLine(A,P,Color);
                Draw->AddCircleFilled(A,2,Color);
                if (Handle.bParentFrame)
                {
                    const ImVec2 Top(P.x,P.y-6), Right(P.x+6,P.y), Bottom(P.x,P.y+6), Left(P.x-6,P.y);
                    Draw->AddQuadFilled(Top,Right,Bottom,Left,Color);
                    Draw->AddQuad(Top,Right,Bottom,Left,Selected ? IM_COL32_WHITE : IM_COL32(0,0,0,255),Selected ? 2.0f : 1.0f);
                }
                else
                {
                    Draw->AddCircleFilled(P,6,Color);
                    Draw->AddCircle(P,6,Selected ? IM_COL32_WHITE : IM_COL32(0,0,0,255),0,Selected ? 2.0f : 1.0f);
                }
                const ImVec2 Mouse = ImGui::GetIO().MousePos;
                const float DX = Mouse.x-P.x, DY = Mouse.y-P.y;
                if (Hovered && !PhysicsEditor.IsGizmoDragging() && DX*DX+DY*DY <= 81)
                {
                    const int Owner = Handle.bParentFrame ? PhysicsEditor.GetEditModel().GetConstraintParent(Handle.ChildBone) : Handle.ChildBone;
                    ImGui::SetTooltip("%s Frame\nLocal to: %s",Handle.bParentFrame ? "Parent" : "Child",
                        PhysicsEditor.GetMesh()->GetRefSkeleton()[Owner].Name.c_str());
                }
            }
            Draw->PopClipRect();
        }
        void DrawViewportTools() override
        {
            if (!PhysicsEditor.IsPreviewSimulating())
            {
                ImGui::BeginDisabled(PhysicsEditor.GetGenerationPreview() != nullptr);
                if (ImGui::Button("Simulate")) PhysicsEditor.StartSimulation();
                ImGui::EndDisabled();
            }
            else
            {
                if (ImGui::Button("Stop")) { PhysicsEditor.StopSimulation(); return; }
                ImGui::SameLine();
                const bool Paused = PhysicsEditor.IsSimulationPaused();
                if (ImGui::Button(Paused ? "Resume" : "Pause")) PhysicsEditor.SetSimulationPaused(!Paused);
                ImGui::SameLine();
                ImGui::BeginDisabled(!PhysicsEditor.IsSimulationPaused());
                if (ImGui::Button("Step")) PhysicsEditor.StepSimulation();
                ImGui::EndDisabled();
                const auto Stats = PhysicsEditor.GetSimulationStats();
                ImGui::SameLine();
                ImGui::Text("Bodies: %d  Joints: %d  Steps: %llu",Stats.Bodies,Stats.Joints,
                    static_cast<unsigned long long>(PhysicsEditor.GetSimulationStepCount()));
                ImGui::Text("Joint gap: %.3f  Max mass ratio: %.2f", Stats.MaxJointSeparation, Stats.MaxJointMassRatio);
                ImGui::TextDisabled("Ctrl + LMB: grab and drag | Release LMB / Esc: let go");
                ImGui::SameLine();
                if (ImGui::Button("Grab settings")) ImGui::OpenPopup("GrabSettings");
                if (ImGui::BeginPopup("GrabSettings"))
                {
                    auto Settings = PhysicsEditor.GetGrabSettings();
                    bool Changed = ImGui::SliderFloat("Stiffness", &Settings.Stiffness, 100, 10000, "%.0f", ImGuiSliderFlags_Logarithmic);
                    Changed |= ImGui::SliderFloat("Damping", &Settings.Damping, 0, 500, "%.1f");
                    if (ImGui::Button("Reset")) { Settings = {}; Changed = true; }
                    if (Changed) PhysicsEditor.SetGrabSettings(Settings);
                    ImGui::EndPopup();
                }
            }
            ImGui::BeginDisabled(PhysicsEditor.IsPreviewSimulating());
            bool Floor = PhysicsEditor.IsSimulationFloorEnabled();
            if (ImGui::Checkbox("Floor (Z=0)",&Floor)) PhysicsEditor.SetSimulationFloorEnabled(Floor);
            ImGui::SameLine();
            float Gravity = PhysicsEditor.GetSimulationGravity();
            ImGui::SetNextItemWidth(EditorStyle::Scale(95));
            if (ImGui::DragFloat("Gravity Z",&Gravity,1.0f,-100000,100000,"%.1f",ImGuiSliderFlags_AlwaysClamp))
                PhysicsEditor.SetSimulationGravity(Gravity);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview units/s^2. -980 for centimeter meshes, -9.81 for meter meshes.");
            ImGui::EndDisabled();
            if (!PhysicsEditor.GetSimulationError().empty()) ImGui::TextWrapped("%s",PhysicsEditor.GetSimulationError().c_str());
            ImGui::BeginDisabled(PhysicsEditor.IsPreviewSimulating());
            if (ImGui::RadioButton("Move (W)", PhysicsEditor.GetGizmoMode() == EGizmoMode::Location))
                PhysicsEditor.SetGizmoMode(EGizmoMode::Location);
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate (E)", PhysicsEditor.GetGizmoMode() == EGizmoMode::Rotation))
                PhysicsEditor.SetGizmoMode(EGizmoMode::Rotation);
            ImGui::SameLine();
            ImGui::BeginDisabled(PhysicsEditor.GetEditTarget() != EPhysicsAssetEditTarget::Shape);
            if (ImGui::RadioButton("Scale (R)", PhysicsEditor.GetGizmoMode() == EGizmoMode::Scale))
                PhysicsEditor.SetGizmoMode(EGizmoMode::Scale);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shape-local axes. Capsule: X/Y radius, Z cylinder length. Center: uniform size.");
            ImGui::EndDisabled();
            ImGui::SameLine();
            int Space = static_cast<int>(PhysicsEditor.GetGizmoSpace());
            ImGui::SetNextItemWidth(EditorStyle::Scale(90));
            if (PhysicsEditor.GetGizmoMode() == EGizmoMode::Scale)
            {
                int ShapeSpace=0;
                ImGui::BeginDisabled();
                ImGui::Combo("##ShapeSpace",&ShapeSpace,"Local\0");
                ImGui::EndDisabled();
            }
            else if (ImGui::Combo("##ShapeSpace", &Space, "Local\0World\0")) PhysicsEditor.SetGizmoSpace(static_cast<EGizmoSpace>(Space));
            ImGui::SameLine();
            const char* Targets[] = { "Shape", "Child Frame", "Parent Frame" };
            const auto Target = PhysicsEditor.GetEditTarget();
            ImGui::SetNextItemWidth(EditorStyle::Scale(140));
            if (ImGui::BeginCombo("Edit",Targets[static_cast<int>(Target)]))
            {
                if (ImGui::Selectable(Targets[0],Target == EPhysicsAssetEditTarget::Shape))
                    PhysicsEditor.SelectEditTarget(EPhysicsAssetEditTarget::Shape);
                ImGui::BeginDisabled(PhysicsEditor.GetEditModel().FindConstraint(PhysicsEditor.GetSelectedBoneIndex()) < 0);
                for (int I=1; I<3; ++I)
                    if (ImGui::Selectable(Targets[I],static_cast<int>(Target) == I))
                        PhysicsEditor.SelectEditTarget(static_cast<EPhysicsAssetEditTarget>(I));
                ImGui::EndDisabled();
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            bool Visible = PhysicsEditor.AreBodiesVisible();
            if (ImGui::Checkbox("Bodies", &Visible)) PhysicsEditor.SetBodiesVisible(Visible);
            ImGui::SameLine();
            float Opacity = PhysicsEditor.GetBodyOpacity();
            ImGui::SetNextItemWidth(EditorStyle::Scale(140));
            if (ImGui::SliderFloat("Opacity", &Opacity, 0.02f, 0.45f, "%.2f")) PhysicsEditor.SetBodyOpacity(Opacity);
        }
        void HandleViewportShortcuts(bool Hovered, bool Focused) override
        {
            const ImGuiIO& IO = ImGui::GetIO();
            if (!Hovered || !Focused || IO.WantTextInput || IO.AppFocusLost || IO.KeyCtrl || IO.KeyAlt ||
                ImGui::IsMouseDown(ImGuiMouseButton_Right) || PhysicsEditor.IsGizmoDragging() || PhysicsEditor.IsPreviewSimulating()) return;
            if (ImGui::IsKeyPressed(ImGuiKey_W,false)) PhysicsEditor.SetGizmoMode(EGizmoMode::Location);
            if (ImGui::IsKeyPressed(ImGuiKey_E,false)) PhysicsEditor.SetGizmoMode(EGizmoMode::Rotation);
            if (ImGui::IsKeyPressed(ImGuiKey_R,false)) PhysicsEditor.SetGizmoMode(EGizmoMode::Scale);
        }
    };

    class FPhysicsSkeletonPanel final : public TAssetEditorPanel<FPhysicsAssetEditor>
    {
    public:
        explicit FPhysicsSkeletonPanel(FPhysicsAssetEditor& InEditor)
            : TAssetEditorPanel(InEditor, "Skeleton Tree", "PhysicsAssetPanel") {}

    protected:
        void DrawContent() override
        {
            FBoneHierarchyPanel::DrawTree(Editor, Search, sizeof(Search), &LastSelected);
        }

    private:
        char Search[128] = {};
        int32 LastSelected = -1;
    };

    class FPhysicsDetailsPanel final : public TAssetEditorPanel<FPhysicsAssetEditor>
    {
    public:
        explicit FPhysicsDetailsPanel(FPhysicsAssetEditor& InEditor)
            : TAssetEditorPanel(InEditor, "Details", "PhysicsAssetPanel") {}

    private:
        bool DrawBodyProperties(int32 BoneIndex, const UBodySetup& Body)
        {
            auto Geometry = Body.AggGeom;
            int Type = static_cast<int>(Body.PhysicsType);
            bool Changed = ImGui::Combo("Physics Type", &Type, "Default\0Kinematic\0Simulated\0");
            if (ImGui::CollapsingHeader("Physical Material", ImGuiTreeNodeFlags_DefaultOpen))
            {
                float Density, Friction, Restitution;
                auto& Model = Editor.GetEditModel();
                float MassKg = Body.MassOverrideKg;
                if (ImGui::DragFloat("Mass Override (kg)", &MassKg, 0.05f, 0, 1e8f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
                    Model.SetBodyMass(BoneIndex, MassKg);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zero uses density and volume. Positive mass is independent of shape size, at unit bone scale.");
                ImGui::Text("Mass at unit scale: %.3f kg", Body.CalculateMass());
                if (Model.GetBodyMaterial(BoneIndex, Density, Friction, Restitution))
                {
                    if (!Body.PhysMaterial) ImGui::TextDisabled("Using runtime defaults; editing creates a body material.");
                    bool MaterialChanged = ImGui::DragFloat("Density", &Density, 0.01f, 0.001f, 1e8f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Density determines mass only when Mass Override is zero.");
                    MaterialChanged |= ImGui::DragFloat("Friction", &Friction, 0.01f, 0.0f, 1e8f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    MaterialChanged |= ImGui::SliderFloat("Restitution", &Restitution, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    if (MaterialChanged) Model.SetBodyMaterial(BoneIndex, Density, Friction, Restitution);
                }
            }
            ECollisionShape RemoveType = ECollisionShape::Unknown;
            int32 RemoveIndex = -1;
            const auto Common = [&](auto& Shape) {
                bool Result = FPropertyEditor::DrawVector3("Location", Shape.Center.V, 0.0f);
                Result |= ImGui::Checkbox("Contribute to Mass", &Shape.bContributeToMass);
                return Result;
            };
            const auto SizeControl = [](const char* Label, float& Value, float Minimum) {
                return ImGui::DragFloat(Label, &Value, 0.05f, Minimum, 1e8f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            };
            ImGui::PushID("Spheres");
            for (int32 I = 0; I < Geometry.SphereElems.Num(); ++I)
            {
                ImGui::PushID(I);
                if (ImGui::CollapsingHeader("Sphere", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::Selectable("Edit in viewport",Editor.IsShapeSelected(ECollisionShape::Sphere,I))) Editor.SelectShape(ECollisionShape::Sphere,I);
                    auto& Shape = Geometry.SphereElems[I];
                    Changed |= Common(Shape);
                    Changed |= SizeControl("Radius", Shape.Radius, 0.000001f);
                    if (ImGui::Button("Remove Shape")) { RemoveType = ECollisionShape::Sphere; RemoveIndex = I; }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            ImGui::PushID("Boxes");
            for (int32 I = 0; I < Geometry.BoxElems.Num(); ++I)
            {
                ImGui::PushID(I);
                if (ImGui::CollapsingHeader("Box", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::Selectable("Edit in viewport",Editor.IsShapeSelected(ECollisionShape::Box,I))) Editor.SelectShape(ECollisionShape::Box,I);
                    auto& Shape = Geometry.BoxElems[I];
                    Changed |= Common(Shape);
                    Changed |= FPropertyEditor::DrawRotator("Rotation", Shape.Rotation);
                    FVector Size(Shape.X, Shape.Y, Shape.Z);
                    if (FPropertyEditor::DrawVector3("Size", Size.V, 1.0f))
                    {
                        Shape.X = std::max(Size.X, 0.000001f); Shape.Y = std::max(Size.Y, 0.000001f); Shape.Z = std::max(Size.Z, 0.000001f);
                        Changed = true;
                    }
                    if (ImGui::Button("Remove Shape")) { RemoveType = ECollisionShape::Box; RemoveIndex = I; }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            ImGui::PushID("Capsules");
            for (int32 I = 0; I < Geometry.SphylElems.Num(); ++I)
            {
                ImGui::PushID(I);
                if (ImGui::CollapsingHeader("Capsule", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::Selectable("Edit in viewport",Editor.IsShapeSelected(ECollisionShape::Sphyl,I))) Editor.SelectShape(ECollisionShape::Sphyl,I);
                    auto& Shape = Geometry.SphylElems[I];
                    Changed |= Common(Shape);
                    Changed |= FPropertyEditor::DrawRotator("Rotation", Shape.Rotation);
                    Changed |= SizeControl("Radius", Shape.Radius, 0.000001f);
                    Changed |= SizeControl("Length", Shape.Length, 0.0f);
                    if (ImGui::Button("Remove Shape")) { RemoveType = ECollisionShape::Sphyl; RemoveIndex = I; }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            if (Changed) Editor.GetEditModel().SetBodyProperties(BoneIndex, static_cast<EPhysicsType>(Type), Geometry);
            if (RemoveIndex >= 0)
            {
                Editor.GetEditModel().RemoveShape(BoneIndex, RemoveType, RemoveIndex);
                Editor.SelectBone(BoneIndex);
                return true;
            }
            return false;
        }

        bool DrawConstraintProperties(int BoneIndex)
        {
            auto& Model = Editor.GetEditModel();
            if (!Model.GetBody(BoneIndex)) return false;
            const int Parent = Model.GetConstraintParent(BoneIndex);
            const auto& Bones = Editor.GetMesh()->GetRefSkeleton();
            const char* ParentName = Parent >= 0 ? Bones[Parent].Name.c_str() : "Choose parent body...";
            if (ImGui::BeginCombo("Parent Body", ParentName))
            {
                bool Any = false, Changed = false;
                for (int I = 0; I < Bones.Num(); ++I)
                {
                    if (!Model.CanConnect(BoneIndex, I)) continue;
                    Any = true;
                    if (ImGui::Selectable(Bones[I].Name.c_str(), Parent == I))
                    {
                        if (Model.FindConstraint(BoneIndex) >= 0) Changed = Model.SetConstraintParent(BoneIndex, I);
                        else Changed = Model.AddConstraint(BoneIndex, I);
                    }
                }
                if (!Any) ImGui::TextDisabled("Add a body to an ancestor bone first.");
                ImGui::EndCombo();
                if (Changed) return true;
            }
            FPhysicsConstraintSettings S;
            if (!Model.GetConstraintSettings(BoneIndex, S)) return false;
            if (!ImGui::CollapsingHeader("Parent Constraint", ImGuiTreeNodeFlags_DefaultOpen)) return false;
            if (FrameBone != BoneIndex)
            {
                FrameBone = BoneIndex;
                ChildRotationCache = {}; ParentRotationCache = {};
            }
            if (ImGui::Selectable("Edit Child Frame in viewport",Editor.IsConstraintFrameSelected(BoneIndex,false)))
                Editor.SelectConstraintFrame(BoneIndex,false);
            bool Changed = FPropertyEditor::DrawVector3("Child Anchor", S.ChildFrame.Location.V, 0);
            FRotator ChildRotation = ChildRotationCache.QuatToRotator(S.ChildFrame.Rotation);
            if (FPropertyEditor::DrawRotator("Child Rotation", ChildRotation))
            {
                S.ChildFrame.Rotation = ChildRotationCache.RotatorToQuat(ChildRotation);
                Changed = true;
            }
            if (ImGui::Selectable("Edit Parent Frame in viewport",Editor.IsConstraintFrameSelected(BoneIndex,true)))
                Editor.SelectConstraintFrame(BoneIndex,true);
            Changed |= FPropertyEditor::DrawVector3("Parent Anchor", S.ParentFrame.Location.V, 0);
            FRotator ParentRotation = ParentRotationCache.QuatToRotator(S.ParentFrame.Rotation);
            if (FPropertyEditor::DrawRotator("Parent Rotation", ParentRotation))
            {
                S.ParentFrame.Rotation = ParentRotationCache.RotatorToQuat(ParentRotation);
                Changed = true;
            }
            const auto Motion = [&](const char* Label, EConstraintMotion& Value) {
                int Mode = static_cast<int>(Value);
                if (!ImGui::Combo(Label, &Mode, "Free\0Limited\0Locked\0")) return false;
                Value = static_cast<EConstraintMotion>(Mode);
                return true;
            };
            ImGui::TextDisabled("Linear Motion");
            Changed |= Motion("Linear X", S.XMotion);
            Changed |= Motion("Linear Y", S.YMotion);
            Changed |= Motion("Linear Z", S.ZMotion);
            if (S.XMotion == EConstraintMotion::Limited || S.YMotion == EConstraintMotion::Limited || S.ZMotion == EConstraintMotion::Limited)
            {
                if (S.LinearLimit <= 0) { S.LinearLimit = 0.01f; Changed = true; }
                Changed |= ImGui::DragFloat("Linear Limit", &S.LinearLimit, 0.05f, 0.000001f, 1e8f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            }
            const auto Angular = [&](const char* Label, EConstraintMotion& Mode, float& Limit) {
                bool Result = Motion(Label, Mode);
                if (Mode == EConstraintMotion::Limited)
                {
                    if (Limit <= 0 || Limit >= 180) { Limit = 45; Result = true; }
                    ImGui::PushID(Label);
                    Result |= ImGui::SliderFloat("Limit (degrees)", &Limit, 0.1f, 179.9f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::PopID();
                }
                return Result;
            };
            ImGui::TextDisabled("Angular Motion: X Twist / Y Swing 1 / Z Swing 2");
            Changed |= Angular("Swing 1", S.Swing1Motion, S.Swing1LimitDegrees);
            Changed |= Angular("Swing 2", S.Swing2Motion, S.Swing2LimitDegrees);
            Changed |= Angular("Twist", S.TwistMotion, S.TwistLimitDegrees);
            Changed |= ImGui::Checkbox("Disable Collision Between Bodies", &S.bDisableCollision);
            if (Changed) Model.SetConstraintSettings(BoneIndex, S);
            if (ImGui::Button("Align Parent to Child")) { Model.AlignParentFrame(BoneIndex); return true; }
            if (ImGui::Button("Reset Constraint Frames")) { Model.ResetConstraintFrames(BoneIndex); return true; }
            if (ImGui::Button("Remove Constraint")) { Model.RemoveConstraint(BoneIndex); return true; }
            return false;
        }

        int FrameBone = -1;
        FRotationConversionCache ChildRotationCache, ParentRotationCache;

    protected:
        void DrawContent() override
        {
            ImGui::BeginDisabled(Editor.IsPreviewSimulating());
            DrawDetails();
            ImGui::EndDisabled();
        }

        void DrawDetails()
        {
            ImGui::TextDisabled("Skeletal Mesh");
            ImGui::TextWrapped("%s", Editor.GetMeshAssetPath().c_str());
            if (!Editor.GetFileError().empty())
            {
                ImGui::TextWrapped("%s", Editor.GetFileError().c_str());
            }
            UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Automatic Generation", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& Settings = Editor.GetGenerationSettings();
                int Mode = Settings.bHumanoid ? 0 : 1;
                if (ImGui::Combo("Mode", &Mode, "Humanoid\0Skeleton\0")) Settings.bHumanoid = Mode == 0;
                if (Settings.bHumanoid)
                {
                    ImGui::TextWrapped("Main body bones only. Root, clothing, hair and fingers are excluded. Unrecognized rigs can use Skeleton mode.");
                    ImGui::SliderFloat("Vertex Weight", &Settings.MinVertexWeight, 0.1f, 1.0f, "%.2f");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fit from vertices whose strongest bone influence meets this threshold. Extreme samples are trimmed.");
                    ImGui::DragFloat("Total Mass (kg)", &Settings.TotalMassKg, 0.5f, 0.1f, 10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::TextWrapped("Mass is distributed by body part. Unsupported spine bodies are skipped; nearby bodies ignore mutual collision.");
                }
                else
                {
                    float MinimumPercent = Settings.MinBoneLengthRatio * 100;
                    if (ImGui::SliderFloat("Minimum Bone Length (%)", &MinimumPercent, 0, 100, "%.1f%%"))
                        Settings.MinBoneLengthRatio = MinimumPercent / 100;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skip segments shorter than this percentage of the longest bone segment.");
                    float RadiusPercent = Settings.RadiusRatio * 100;
                    if (ImGui::SliderFloat("Capsule Radius (%)", &RadiusPercent, 1, 49, "%.1f%%"))
                        Settings.RadiusRatio = RadiusPercent / 100;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Capsule radius as a percentage of its bone segment length.");
                }
                ImGui::TextWrapped("Generation stays in memory. Use Ctrl+S to save to disk.");
                if (!Asset && !Editor.GetGenerationPreview())
                {
                    if (ImGui::Button("Create Physics Asset")) { Editor.CreateAsset(); return; }
                    if (Editor.HasSavedAsset())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Load Saved Asset")) { Editor.RequestReload(); return; }
                    }
                }
                if (ImGui::Button(Asset ? "Preview Regeneration" : "Preview Generation")) Editor.PreviewGeneration();
            }
            if (auto* Preview = Editor.GetGenerationPreview())
            {
                ImGui::Text("Preview: %d bodies, %d constraints", Preview->BodySetups.Num(), Preview->PhysicsConstraint.Num());
                if (ImGui::BeginChild("GenerationBones", ImVec2(0,120), true))
                    for (auto* Body : Preview->BodySetups) ImGui::TextUnformatted(Body->BoneName.ToString().c_str());
                ImGui::EndChild();
                ImGui::TextWrapped("Apply replaces current bodies and constraints. You can undo it. The saved file is unchanged until Ctrl+S.");
                if (ImGui::Button("Apply Generated Asset")) { Editor.ApplyGeneration(); return; }
                ImGui::SameLine();
                if (ImGui::Button("Cancel Preview")) Editor.CancelGeneration();
                return;
            }
            if (!Asset)
            {
                ImGui::TextWrapped("No Physics Asset is assigned to this skeletal mesh.");
                return;
            }

            ImGui::Text("Bodies: %d   Constraints: %d",
                Asset->BodySetups.Num(), Asset->PhysicsConstraint.Num());
            const int32 BoneIndex = Editor.GetSelectedBoneIndex();
            const auto& Bones = Editor.GetMesh()->GetRefSkeleton();
            if (BoneIndex < 0 || BoneIndex >= Bones.Num())
            {
                ImGui::TextDisabled("Select a bone in the Skeleton Tree or viewport.");
                return;
            }
            const FName BoneName(Bones[BoneIndex].Name);
            ImGui::TextWrapped("Bone: %s", Bones[BoneIndex].Name.c_str());

            auto& Model = Editor.GetEditModel();
            if (!Model.GetError().empty()) ImGui::TextWrapped("%s", Model.GetError().c_str());
            if (!Editor.GetPreviewWarning().empty()) ImGui::TextWrapped("%s", Editor.GetPreviewWarning().c_str());
            bool ShowBodies = Editor.AreBodiesVisible();
            if (ImGui::Checkbox("Show Bodies", &ShowBodies)) Editor.SetBodiesVisible(ShowBodies);
            if (ImGui::BeginCombo("Add Shape", "Choose shape..."))
            {
                const auto Add = [&](const char* Label, ECollisionShape Type) {
                    if (!ImGui::Selectable(Label) || !Model.AddShape(BoneIndex,Type)) return;
                    const auto& G = Model.GetBody(BoneIndex)->AggGeom;
                    const int Count = Type == ECollisionShape::Sphere ? G.SphereElems.Num() :
                        Type == ECollisionShape::Box ? G.BoxElems.Num() : G.SphylElems.Num();
                    Editor.SelectShape(Type,Count-1);
                };
                Add("Sphere",ECollisionShape::Sphere);
                Add("Box",ECollisionShape::Box);
                Add("Capsule",ECollisionShape::Sphyl);
                ImGui::EndCombo();
            }
            if (UBodySetup* Body = Model.GetBody(BoneIndex))
            {
                if (ImGui::Button("Remove Body"))
                {
                    Model.RemoveBody(BoneIndex);
                    return;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Removes this body's shapes and connected constraints.");
                ImGui::PushID(BoneIndex);
                const bool Removed = DrawBodyProperties(BoneIndex, *Body);
                ImGui::PopID();
                if (Removed) return;
            }
            else ImGui::TextDisabled("No body is assigned to this bone.");

            ImGui::Separator();
            bool ShowConstraints = Editor.AreConstraintsVisible();
            if (ImGui::Checkbox("Show Constraints", &ShowConstraints)) Editor.SetConstraintsVisible(ShowConstraints);
            ImGui::PushID(BoneIndex);
            const bool LinkChanged = DrawConstraintProperties(BoneIndex);
            ImGui::PopID();
            if (LinkChanged) return;
            for (const auto* Joint : Asset->PhysicsConstraint)
            {
                if (!Joint || Joint->ConstraintBone2 != BoneName) continue;
                const int Child = Editor.GetMesh()->FindBoneIndex(Joint->ConstraintBone1.ToString());
                if (ImGui::Button(("Select Child: " + Joint->ConstraintBone1.ToString()).c_str()))
                {
                    Editor.SelectBone(Child);
                    return;
                }
            }
        }
    };
}

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
    : FDockedAssetEditorTab(InEditor, "PhysicsAssetDockSpace", "PhysicsAssetPanelClass")
    , PhysicsEditor(InEditor)
{
    const FString Filename = FPaths::ToUtf8(FPaths::FromUtf8(InEditor.GetMeshAssetPath()).filename());
    TabName = Filename + " (Physics Asset)###PhysicsAsset:" + InEditor.GetMeshAssetPath();
    RegisterPanel(MakeUnique<FPhysicsViewportPanel>(InEditor));
    RegisterPanel(MakeUnique<FPhysicsSkeletonPanel>(InEditor));
    RegisterPanel(MakeUnique<FPhysicsDetailsPanel>(InEditor));
}

void FPhysicsAssetEditorTab::BeforeRender()
{
    PhysicsEditor.SetViewportState(0, 0, {});
}

bool FPhysicsAssetEditorTab::DrawFileMenuItems()
{
    if (ImGui::MenuItem("Create Physics Asset", nullptr, false,
        !PhysicsEditor.GetPhysicsAsset() && !PhysicsEditor.HasSavedAsset())) PhysicsEditor.CreateAsset();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, PhysicsEditor.GetPhysicsAsset() != nullptr)) PhysicsEditor.Save();
    if (ImGui::MenuItem("Reload from Disk", nullptr, false, PhysicsEditor.HasSavedAsset())) PhysicsEditor.RequestReload();
    return true;
}

void FPhysicsAssetEditorTab::DrawEditMenu()
{
    auto& Model = PhysicsEditor.GetEditModel();
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !PhysicsEditor.IsPreviewSimulating() && !PhysicsEditor.GetGenerationPreview() && Model.CanUndo())) Model.Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !PhysicsEditor.IsPreviewSimulating() && !PhysicsEditor.GetGenerationPreview() && Model.CanRedo())) Model.Redo();
        ImGui::EndMenu();
    }
}

void FPhysicsAssetEditorTab::OnRender(bool bActive)
{
    FDockedAssetEditorTab::OnRender(bActive);
    const ImGuiIO& IO = ImGui::GetIO();
    const bool Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_DockHierarchy)
        || HasFocusedSubWindow();
    auto& Model = PhysicsEditor.GetEditModel();
    Model.Commit(PhysicsEditor.IsGizmoDragging() || ImGui::IsAnyItemActive());
    if (bActive && Focused && !PhysicsEditor.IsGizmoDragging() && !IO.WantTextInput && IO.KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) PhysicsEditor.Save();
        else if (!PhysicsEditor.IsPreviewSimulating() && !PhysicsEditor.GetGenerationPreview() && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            if (IO.KeyShift) Model.Redo(); else Model.Undo();
        }
        else if (!PhysicsEditor.IsPreviewSimulating() && !PhysicsEditor.GetGenerationPreview() && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Model.Redo();
    }
    const FString PopupName = "Unsaved Physics Asset Changes###PhysicsAssetClose:" + PhysicsEditor.GetAssetPath();
    if (PhysicsEditor.HasPendingAction() && !ImGui::IsPopupOpen(PopupName.c_str())) ImGui::OpenPopup(PopupName.c_str());
    if (ImGui::BeginPopupModal(PopupName.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Save changes before continuing?");
        if (!PhysicsEditor.GetFileError().empty()) ImGui::TextWrapped("%s", PhysicsEditor.GetFileError().c_str());
        if (ImGui::Button("Save and Continue"))
        {
            if (PhysicsEditor.CompletePendingAction(true)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard Changes"))
        {
            if (PhysicsEditor.CompletePendingAction(false)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            PhysicsEditor.CancelPendingAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID DockSpaceId, const ImVec2& Size)
{
    ImGui::DockBuilderAddNode(DockSpaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(DockSpaceId, Size);
    ImGuiID ViewportId = DockSpaceId;
    const ImGuiID SkeletonId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.22f, nullptr, &ViewportId);
    const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.30f, nullptr, &ViewportId);
    ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
    ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), SkeletonId);
    ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
    ImGui::DockBuilderFinish(DockSpaceId);
}

const char* FPhysicsAssetEditorTab::GetTabName() const
{
    const auto Separator = TabName.find("###");
    DisplayTabName = TabName.substr(0, Separator) + (PhysicsEditor.IsDirty() ? "*" : "") + TabName.substr(Separator);
    return DisplayTabName.c_str();
}
