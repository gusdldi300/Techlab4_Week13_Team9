#include "EnginePCH.h"
#include "Physics/PhysicsAssetBuilder.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Render/SkeletalMesh.h"
#include "Core/Misc/Paths.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditModel.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsAssetInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMeshObject.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include <cstdio>
#include <fstream>
#include <limits>

static int Checks = 0;
static void Check(bool Passed, const char* Name)
{
    ++Checks;
    if (!Passed) { std::printf("FAIL: %s\n", Name); std::exit(1); }
}
static bool Near(const FVector& A, const FVector& B) { return (A-B).Size() < 0.001f; }

template<class Tag, typename Tag::Type Member> struct TestAccess
{ friend typename Tag::Type GetTestMember(Tag) { return Member; } };
struct InitTag { using Type = bool(*)(); friend Type GetTestMember(InitTag); };
struct ShutdownTag { using Type = void(*)(); friend Type GetTestMember(ShutdownTag); };
struct MeshTag { using Type = USkeletalMesh* USkinnedMeshComponent::*; friend Type GetTestMember(MeshTag); };
struct ObjectTag { using Type = TUniquePtr<FSkeletalMeshObject> USkinnedMeshComponent::*; friend Type GetTestMember(ObjectTag); };
template struct TestAccess<InitTag, &FPhysicsCore::InitPhysX>;
template struct TestAccess<ShutdownTag, &FPhysicsCore::ShutdownPhysX>;
template struct TestAccess<MeshTag, &USkinnedMeshComponent::SkeletalMesh>;
template struct TestAccess<ObjectTag, &USkinnedMeshComponent::MeshObject>;
// Run real component/bone pose conversion without creating GPU buffers.
class FNoRenderMeshObject : public FSkeletalMeshObject
{
public:
    explicit FNoRenderMeshObject(USkeletalMesh* Mesh) : FSkeletalMeshObject(Mesh) {}
    void Update(const TArray<FMatrix>&) override {}
    const FVertexFactory* GetVertexFactory(uint32) const override { return nullptr; }
};

static void Simulate(USkeletalMesh& Mesh, const char* Name)
{
    auto* Component = NewObject<USkeletalMeshComponent>();
    Component->*GetTestMember(MeshTag{}) = &Mesh;
    Component->*GetTestMember(ObjectTag{}) = MakeUnique<FNoRenderMeshObject>(&Mesh);
    Component->ResetPose();
    Component->RefreshBoneTransformsNow();
    auto Scene = MakePhysicsScene();
    Scene->CreatePhysicsScene();
    Check(Scene->SetGravity(FVector(0,0,-980)), "Preview gravity");
    FBodyInstance Floor;
    FBodyInstanceInitParams Params;
    Params.WorldLocation = FVector(0,0,-5);
    FKBoxElem Box; Box.X=Box.Y=2000; Box.Z=10;
    Params.FallbackGeom.BoxElems.Add(Box);
    Floor.InitBody(nullptr,Params,nullptr,Scene.get());
    FPhysicsAssetInstance Instance;
    Check(Instance.Build(*Component,Scene.get()) && Instance.SetSimulating(*Component,true), "Build generated ragdoll in PhysX");
    Check(Instance.GetDebugStats().MaxJointSeparation < 0.001f, "Generated joint anchors coincide before simulation");
    float MaxGap=0, MaxBoneStretch=1;
    const auto Start = Component->GetBoneSpaceTransforms();
    for (int Step=0; Step<600; ++Step)
    {
        Instance.SyncFromAnimation(*Component);
        Scene->StartFrame(1.0f/60); Scene->EndFrame();
        Instance.ApplyToMesh(*Component);
        Component->RefreshBoneTransformsNow();
        const auto Stats = Instance.GetDebugStats();
        MaxGap = std::max(MaxGap,Stats.MaxJointSeparation);
        Check(std::isfinite(Stats.MaxJointSeparation), "Finite joint gap");
        for (int I=0; I<Start.Num(); ++I)
        {
            if (Mesh.GetRefSkeleton()[I].ParentIndex < 0 || Start[I].Location.Size() < 0.1f) continue;
            MaxBoneStretch = std::max(MaxBoneStretch,Component->GetBoneLocalTransform(I).Location.Size()/Start[I].Location.Size());
        }
    }
    const auto Stats = Instance.GetDebugStats();
    std::printf("%s simulation: max gap %.5f, max local length ratio %.3f, joint mass ratio %.2f\n",Name,MaxGap,MaxBoneStretch,Stats.MaxJointMassRatio);
    Check(Stats.MaxJointMassRatio <= 8.01f, "Balanced adjacent masses");
    Check(MaxGap < 0.11f, "Ragdoll joint gap bounded over 600 steps");
    Check(MaxBoneStretch < 1.05f, "No large bone stretching");
    Instance.Clear(); Floor.TermBody();
    delete Component;
}

int main()
{
    std::setvbuf(stdout,nullptr,_IONBF,0);
    FString Error;
    const auto Root = std::filesystem::current_path();
    Check(FPaths::InitEngineDir(), "Engine paths");
    Check(FPaths::SetProject(Root/"DefaultProject/DefaultProject.kproject", &Error), "Project paths");
    Check(GetTestMember(InitTag{})(), "PhysX initialization");
    const auto Scratch = Root/"Build/GenerationChecks";
    std::filesystem::create_directories(Scratch);
    const auto MeshPath = Scratch/"mesh.fbx.bin";
    { std::ofstream File(MeshPath); File << "fixture"; }
    auto* Mesh = NewObject<USkeletalMesh>();
    Mesh->SetPath(FPaths::ToUtf8(MeshPath));
    PhysicsAssetBuilder::FAutoGenerateSettings Settings;
    Settings.bHumanoid = false;
    Check(!PhysicsAssetBuilder::CreateFromSkeleton(*Mesh, Settings, Error) && !Error.empty(), "Empty skeleton rejected");
    auto& Bones = const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton());
    const auto AddBone = [&](const char* Name, int Parent, FVector Position) {
        FBoneInfo Bone; Bone.Name = Name; Bone.ParentIndex = Parent; Bone.LocalBindPose.Location = Position;
        Bones.Add(Bone);
    };
    AddBone("Root", -1, FVector::ZeroVector);
    AddBone("Helper", 0, FVector(0,0,10));
    AddBone("Arm", 1, FVector(0.01f,0,0));
    AddBone("Hand", 2, FVector(10,0,0));
    AddBone("End", 3, FVector(0,0,-2));
    Bones[1].LocalBindPose.Rotation = FQuat::MakeFromAxisAngle(FVector(0,0,1), PI/2);
    auto* Asset = PhysicsAssetBuilder::CreateFromSkeleton(*Mesh, Settings, Error);
    Check(Asset && Error.empty(), "Generate skeleton asset");
    Check(!Mesh->GetPhysicsAsset(), "Generation does not replace mesh asset");
    Check(Asset->BodySetups.Num() == 3 && Asset->PhysicsConstraint.Num() == 2, "Skip short helper and unweighted leaf");
    Check(Asset->FindBodyIndex(FName("Helper")) < 0, "Short body omitted");
    const auto& Capsule = Asset->BodySetups[1]->AggGeom.SphylElems[0];
    Check(Near(Capsule.Center, FVector(5,0,0)), "Capsule centered on segment");
    Check(Near(FQuat(Capsule.Rotation).RotateVector(FVector(0,0,1)), FVector(1,0,0)), "Capsule follows bone direction");
    Check(Capsule.Radius == 1.5f && Capsule.Length == 7, "Capsule total length matches segment");
    const auto* Joint = Asset->PhysicsConstraint[0];
    Check(Joint->ConstraintBone1 == FName("Arm") && Joint->ConstraintBone2 == FName("Root"), "Nearest generated ancestor");
    const FMatrix Relative = Bones[2].LocalBindPose.GetLocalMatrix() * Bones[1].LocalBindPose.GetLocalMatrix();
    Check(Near(Joint->Pos2, Relative.TransformPosition(Joint->Pos1)), "Anchor includes skipped helper transform");
    Check(Near(Joint->PriAxis2, Bones[1].LocalBindPose.Rotation.RotateVector(Joint->PriAxis1)), "Constraint rotation includes skipped helper");
    Check(!Asset->IsCollisionEnabled(0,1) && Asset->IsCollisionEnabled(0,2), "Connected pair collision disabled");
    Check(Near(FQuat(Asset->BodySetups[2]->AggGeom.SphylElems[0].Rotation).RotateVector(FVector(0,0,1)), FVector(0,0,-1)), "Negative Z capsule orientation");
    Mesh->SetPhysicsAsset(Asset);
    const auto AssetPath = PhysicsAssetFile::GetDefaultPath(Mesh->GetPath());
    Check(PhysicsAssetFile::Write(AssetPath, Mesh->GetPath(), *Mesh, *Asset, Error), "Save generated asset");
    UPhysicsAsset* Reloaded = nullptr;
    Check(PhysicsAssetFile::Read(AssetPath, *Mesh, Reloaded, Error), "Load generated asset");
    Check(Reloaded->BodySetups.Num() == 3 && Reloaded->PhysicsConstraint.Num() == 2 &&
        !Reloaded->IsCollisionEnabled(0,1), "Generated geometry and connections round trip");

    Settings.MinBoneLengthRatio = std::numeric_limits<float>::quiet_NaN();
    Check(!PhysicsAssetBuilder::CreateFromSkeleton(*Mesh, Settings, Error), "Nonfinite settings rejected");
    Settings.MinBoneLengthRatio = 0.05f;
    Bones[1].ParentIndex = 2;
    Check(!PhysicsAssetBuilder::CreateFromSkeleton(*Mesh, Settings, Error), "Invalid hierarchy rejected");
    Bones[1].ParentIndex = 0;
    auto& Render = const_cast<FSkeletalMeshRenderData&>(Mesh->GetRenderData());
    FSkeletalMeshLODRenderData LOD;
    for (int I=0; I<5; ++I) LOD.BoneBounds.Add(FBox{FVector(1), FVector(-1)});
    LOD.BoneBounds[4] = FBox{FVector(-1,8,7), FVector(1,12,9)};
    Render.LODRenderData.Add(std::move(LOD));
    auto* WithLeaf = PhysicsAssetBuilder::CreateFromSkeleton(*Mesh, Settings, Error);
    Check(WithLeaf && WithLeaf->FindBodyIndex(FName("End")) >= 0, "Weighted leaf receives a body");

    // Real imported skeleton, without GPU resource creation or changing content files.
    const auto SonicPath = Root/"DefaultProject/Content/FBX/Sonic_Running/Sonic_Running.fbx.bin";
    FFbxBakedMesh Data;
    Check(FFbxMeshBake::Read(FPaths::ToUtf8(SonicPath), Data, Error) && Data.Kind == EFbxMeshKind::Skeletal, "Read Sonic skeletal fixture");
    auto* Sonic = NewObject<USkeletalMesh>();
    Sonic->SetPath(FPaths::ToUtf8(SonicPath));
    const_cast<TArray<FBoneInfo>&>(Sonic->GetRefSkeleton()) = Data.SkeletalMesh.Bones;
    auto* SonicAsset = PhysicsAssetBuilder::CreateFromSkeleton(*Sonic, Settings, Error);
    Check(SonicAsset && SonicAsset->BodySetups.Num() > 5 && SonicAsset->PhysicsConstraint.Num() > 4, "Generate real Sonic skeleton");
    FString Text;
    Check(PhysicsAssetFile::ToText(Sonic->GetPath(), *Sonic, *SonicAsset, Text, Error), "Validate real generated asset serialization");
    std::printf("PASS: %d generation checks; Sonic: %d bodies, %d constraints\n", Checks,
        SonicAsset->BodySetups.Num(), SonicAsset->PhysicsConstraint.Num());

    for (const char* Name : {"Kurisu_Idle", "Kurisu_Running", "Sonic_Running", "Sonic_HeadHit"})
    {
        const auto Path = Root/"DefaultProject/Content/FBX"/Name/(FString(Name)+".fbx.bin");
        FFbxBakedMesh Fixture;
        Check(FFbxMeshBake::Read(FPaths::ToUtf8(Path), Fixture, Error), "Read humanoid fixture");
        auto* Character = NewObject<USkeletalMesh>();
        const auto DraftPath = Scratch/(FString(Name)+"_draft.fbx.bin");
        { std::ofstream File(DraftPath); File << "fixture"; }
        Character->SetPath(FPaths::ToUtf8(DraftPath));
        const_cast<TArray<FBoneInfo>&>(Character->GetRefSkeleton()) = Fixture.SkeletalMesh.Bones;
        FSkeletalMeshLODRenderData CharacterLOD;
        CharacterLOD.BindVertices = std::move(Fixture.SkeletalMesh.BindVertices);
        CharacterLOD.SkinWeights = std::move(Fixture.SkeletalMesh.SkinWeight);
        CharacterLOD.BindAABB = Fixture.SkeletalMesh.BindAABB;
        const_cast<FSkeletalMeshRenderData&>(Character->GetRenderData()).LODRenderData.Add(std::move(CharacterLOD));
        PhysicsAssetBuilder::FAutoGenerateSettings Humanoid;
        auto* Generated = PhysicsAssetBuilder::CreateFromSkeleton(*Character, Humanoid, Error);
        Check(Generated && Generated->BodySetups.Num() >= 12 && Generated->BodySetups.Num() <= 22, "Main humanoid bodies only");
        Check(Generated->PhysicsConstraint.Num() == Generated->BodySetups.Num()-1, "Humanoid forms one connected tree");
        float TotalMass = 0;
        for (auto* Body : Generated->BodySetups)
        {
            const auto BoneName = Body->BoneName.ToString();
            Check(BoneName != "Armature" && !BoneName.starts_with("Bone") && BoneName.find("Finger") == FString::npos, "Root and accessory bones excluded");
            Check(Body->AggGeom.GetVolume() > 0 && std::isfinite(Body->AggGeom.GetVolume()), "Finite positive fitted shape");
            Check(Body->MassOverrideKg > 0 && Body->CalculateMass() == Body->MassOverrideKg, "Generated mass independent of volume");
            TotalMass += Body->CalculateMass();
        }
        Check(std::abs(TotalMass-Humanoid.TotalMassKg)<0.001f, "Total generated mass matches setting");
        if (FString(Name).starts_with("Sonic")) Check(Generated->FindBodyIndex(FName("mixamorig:Spine1"))<0, "Unsupported Sonic spine body skipped");
        FString Snapshot;
        Check(PhysicsAssetFile::ToText(Character->GetPath(), *Character, *Generated, Snapshot, Error), "Validate fitted humanoid");
        json LegacyJson = json::parse(Snapshot);
        for (auto& Body : LegacyJson["Bodies"]) Body.erase("MassOverrideKg");
        UPhysicsAsset* LegacyAsset = nullptr;
        Check(PhysicsAssetFile::ApplyText(LegacyJson.dump(),*Character,LegacyAsset,Error), "Legacy Physics Asset without mass override loads");
        Check(LegacyAsset->BodySetups[0]->MassOverrideKg == 0, "Legacy density mass preserved");
        PhysicsAssetBuilder::DestroyGeneratedAsset(LegacyAsset);
        json InvalidMass = json::parse(Snapshot);
        InvalidMass["Bodies"][0]["MassOverrideKg"] = -1;
        UPhysicsAsset* InvalidAsset = nullptr;
        Check(!PhysicsAssetFile::ApplyText(InvalidMass.dump(),*Character,InvalidAsset,Error) && !InvalidAsset, "Invalid mass rejected before mutation");
        auto* BinaryBody = NewObject<UBodySetup>();
        BinaryBody->MassOverrideKg = 3.0f;
        TArray<uint8> Binary;
        FMemoryWriter Writer(Binary); BinaryBody->Serialize(Writer);
        auto* BinaryLoaded = NewObject<UBodySetup>();
        FMemoryReader Reader(Binary,true); BinaryLoaded->Serialize(Reader);
        Check(!Reader.IsError() && BinaryLoaded->MassOverrideKg == 3.0f, "BodySetup binary mass round trip");
        // Version 1 has the same fields before the newly appended mass value.
        Binary[4]=1; Binary[5]=Binary[6]=Binary[7]=0;
        for (int B=0; B<4; ++B) Binary.RemoveLast();
        FMemoryReader LegacyReader(Binary,true); BinaryLoaded->Serialize(LegacyReader);
        Check(!LegacyReader.IsError() && BinaryLoaded->MassOverrideKg == 0, "Version 1 BodySetup remains compatible");
        delete BinaryLoaded; delete BinaryBody;
        const auto DraftAssetPath = PhysicsAssetFile::GetDefaultPath(Character->GetPath());
        std::filesystem::remove(DraftAssetPath);
        FPhysicsAssetEditModel Model(*Character, Character->GetPath());
        Model.ResetHistory();
        Check(Model.ReplaceWithGenerated(*Generated) && Model.IsDirty(), "Create is an unsaved change");
        Simulate(*Character, Name);
        Check(!std::filesystem::exists(DraftAssetPath), "Generation creates no disk file");
        Check(Model.Undo() && !Character->GetPhysicsAsset() && !Model.IsDirty(), "Undo new creation restores no asset");
        Check(Model.Redo() && Character->GetPhysicsAsset() && Model.IsDirty(), "Redo restores unsaved creation");
        Check(Model.RestoreSaved() && !Character->GetPhysicsAsset() && !Model.IsDirty(), "Discard new creation detaches draft");
        Check(Model.ReplaceWithGenerated(*Generated), "Recreate draft");
        Check(PhysicsAssetFile::Write(DraftAssetPath, Character->GetPath(), *Character, *Character->GetPhysicsAsset(), Error), "Explicit save writes file");
        Model.MarkSaved();
        Check(!Model.IsDirty(), "Explicit save clears dirty state");
        const int MassBone = Character->FindBoneIndex(Character->GetPhysicsAsset()->BodySetups[0]->BoneName.ToString());
        const float OriginalMass = Model.GetBody(MassBone)->MassOverrideKg;
        Check(!Model.SetBodyMass(MassBone,-1) && !Model.IsDirty(), "Invalid mass edit rejected");
        Check(Model.SetBodyMass(MassBone,0) && Model.IsDirty(), "Mass override can be disabled");
        Check(Model.Undo() && Model.GetBody(MassBone)->MassOverrideKg == OriginalMass && !Model.IsDirty(), "Mass edit undo restores generated mass");
        FString SavedSnapshot;
        Check(PhysicsAssetFile::ToText(Character->GetPath(), *Character, *Character->GetPhysicsAsset(), SavedSnapshot, Error), "Capture saved snapshot");
        auto* FirstBody = Generated->BodySetups[0];
        FirstBody->AggGeom.BoxElems[0].X *= 0.8f;
        Check(Model.ReplaceWithGenerated(*Generated) && Model.IsDirty(), "Regeneration is unsaved");
        UPhysicsAsset* DiskAsset = nullptr;
        Check(PhysicsAssetFile::Read(DraftAssetPath, *Character, DiskAsset, Error), "Read saved asset after regeneration");
        FString DiskText;
        Check(PhysicsAssetFile::ToText(Character->GetPath(), *Character, *DiskAsset, DiskText, Error) && DiskText == SavedSnapshot, "Regeneration leaves disk untouched");
        Check(Model.Undo() && !Model.IsDirty(), "Undo regeneration restores saved state");
        Check(Model.Redo() && Model.IsDirty() && Model.RestoreSaved() && !Model.IsDirty(), "Discard regeneration restores saved state");
        std::printf("%s humanoid: %d bodies, %d constraints\n", Name, Generated->BodySetups.Num(), Generated->PhysicsConstraint.Num());
        PhysicsAssetBuilder::DestroyGeneratedAsset(Generated);
    }
    std::printf("PASS: %d total generation and draft checks\n", Checks);
    GetTestMember(ShutdownTag{})();
    return 0;
}
