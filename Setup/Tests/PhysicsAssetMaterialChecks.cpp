#include "EnginePCH.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditModel.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsScene.h"
#include "Physics/BodyInstance.h"
#include "Render/SkeletalMesh.h"
#include "Core/Misc/Paths.h"
#include <PxPhysicsAPI.h>
#include <cstdio>
#include <fstream>
#include <limits>

// Test-only access to the same PhysX lifetime entry points as FEngineLoop.
template<class Tag, typename Tag::Type Member> struct TestAccess
{
    friend typename Tag::Type GetTestMember(Tag) { return Member; }
};
struct InitTag { using Type = bool(*)(); friend Type GetTestMember(InitTag); };
struct ShutdownTag { using Type = void(*)(); friend Type GetTestMember(ShutdownTag); };
template struct TestAccess<InitTag, &FPhysicsCore::InitPhysX>;
template struct TestAccess<ShutdownTag, &FPhysicsCore::ShutdownPhysX>;

static int Checks = 0;
static void Check(bool Passed, const char* Name)
{
    ++Checks;
    if (!Passed) { std::printf("FAIL: %s\n", Name); std::exit(1); }
}
static bool Near(float A, float B) { return std::abs(A-B) <= std::max(1.0f, std::abs(B))*0.0001f; }

static void CheckCenterOfMass(FPhysicsScene* Scene)
{
    auto* Setup = NewObject<UBodySetup>();
    FBodyInstance Body;
    FBodyInstanceInitParams Params;
    Params.bSimulatePhysics = true;
    Params.Scale3D = FVector(2, 3, 4);
    Params.WorldLocation = FVector(12, -3, 40);
    Params.WorldRotation = FRotator(0, 40, 0).Quaternion();
    FVector Local, World;
    float PreviewMass = 0, LiveMass = 0;
    Check(!FBodyInstance::CalculateCenterOfMass(*Setup, Params.Scale3D, Local, PreviewMass), "Empty body has no preview COM");
    Check(!Body.GetCenterOfMass(World, LiveMass), "Uninitialized body has no live COM");
    FKBoxElem Small; Small.X = Small.Y = Small.Z = 2;
    Small.Center = FVector(1, 2, 3);
    Setup->AggGeom.BoxElems.Add(Small);
    Check(FBodyInstance::CalculateCenterOfMass(*Setup, Params.Scale3D, Local, PreviewMass) &&
        Near(Local.X, 2) && Near(Local.Y, 6) && Near(Local.Z, 12), "Single box COM includes local offset and scale");
    FKBoxElem Large; Large.X = Large.Y = Large.Z = 4;
    Large.Center = FVector(10, 2, 3);
    Setup->AggGeom.BoxElems.Add(Large);
    Check(FBodyInstance::CalculateCenterOfMass(*Setup, Params.Scale3D, Local, PreviewMass) &&
        Near(Local.X, 18) && Near(Local.Y, 6) && Near(Local.Z, 12), "Two boxes use volume weighted COM");
    auto CompareLive = [&]() {
        Body.InitBody(Setup, Params, nullptr, Scene);
        Check(FBodyInstance::CalculateCenterOfMass(*Setup, Params.Scale3D, Local, PreviewMass) &&
            Body.GetCenterOfMass(World, LiveMass), "Preview and live COM queries succeed");
        const FVector Expected = Params.WorldLocation + Params.WorldRotation.RotateVector(Local);
        Check(Near(World.X, Expected.X) && Near(World.Y, Expected.Y) && Near(World.Z, Expected.Z) &&
            Near(LiveMass, PreviewMass), "Preview matches actual PhysX COM and mass under rotation/nonuniform scale");
    };
    CompareLive();
    FKSphereElem Sphere; Sphere.Center = FVector(-5, 4, 8); Sphere.Radius = 3;
    Setup->AggGeom.SphereElems.Add(Sphere);
    FKSphylElem Capsule; Capsule.Center = FVector(7, -6, 5); Capsule.Radius = 2; Capsule.Length = 9;
    Capsule.Rotation = FRotator(20, 40, 15);
    Setup->AggGeom.SphylElems.Add(Capsule);
    CompareLive();
    Setup->MassOverrideKg = 2;
    CompareLive();
    for (auto& Box : Setup->AggGeom.BoxElems) Box.bContributeToMass = false;
    Setup->AggGeom.SphereElems[0].bContributeToMass = false;
    Setup->AggGeom.SphylElems[0].bContributeToMass = false;
    Setup->MassOverrideKg = 0;
    CompareLive(); // Covers the runtime's zero calculated mass fallback.
    Params.Scale3D = FVector(0.00001f, 0.00002f, 0.00003f);
    CompareLive(); // Includes the same minimum PhysX extents as runtime shapes.
    Body.TermBody();
    Check(!Body.GetCenterOfMass(World, LiveMass), "Released body has no stale COM");
    delete Setup;
}

int main()
{
    FString Error;
    const auto Root = std::filesystem::current_path();
    Check(FPaths::InitEngineDir(), "Engine paths");
    Check(FPaths::SetProject(Root/"DefaultProject/DefaultProject.kproject", &Error), "Project paths");
    const auto Scratch = Root/"Build/MaterialEditChecks";
    std::filesystem::create_directories(Scratch);
    const auto MeshFile = Scratch/"mesh.fbx.bin";
    { std::ofstream File(MeshFile); File << "fixture"; }
    auto* Mesh = NewObject<USkeletalMesh>();
    Mesh->SetPath(FPaths::ToUtf8(MeshFile));
    auto& Bones = const_cast<TArray<FBoneInfo>&>(Mesh->GetRefSkeleton());
    FBoneInfo RootBone; RootBone.Name = "Root"; Bones.Add(RootBone);
    FBoneInfo Child; Child.Name = "Child"; Child.ParentIndex = 0; Child.LocalBindPose.Location = FVector(0,0,10); Bones.Add(Child);
    Mesh->SetPhysicsAsset(NewObject<UPhysicsAsset>(Mesh));
    FPhysicsAssetEditModel Model(*Mesh, Mesh->GetPath());
    Model.ResetHistory();
    Check(Model.AddShape(0, ECollisionShape::Box) && Model.AddShape(1, ECollisionShape::Box), "Fixture bodies");
    Model.MarkSaved();
    float Density, Friction, Restitution;
    Check(Model.GetBodyMaterial(0, Density, Friction, Restitution) && Density == 1 &&
        Friction == FPhysicsCore::DefaultFriction && Restitution == FPhysicsCore::DefaultRestitution, "Unassigned runtime defaults");
    Check(Model.SetBodyMaterial(0, Density, Friction, Restitution) && !Model.GetBody(0)->PhysMaterial && !Model.IsDirty(), "Unchanged defaults allocate nothing");
    Check(!Model.SetBodyMaterial(-1, 1, 0, 0), "Invalid bone rejected");
    for (float Invalid : { -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
    {
        Check(!Model.SetBodyMaterial(0, Invalid, 0, 0), "Invalid density rejected");
        Check(!Model.SetBodyMaterial(0, 1, Invalid, 0), "Invalid friction rejected");
        Check(!Model.SetBodyMaterial(0, 1, 0, Invalid), "Invalid restitution rejected");
    }
    Check(!Model.SetBodyMaterial(0, 0, 0, 0) && !Model.SetBodyMaterial(0, 1, 0, 1.01f), "Material bounds rejected");
    Check(!Model.IsDirty() && !Model.GetBody(0)->PhysMaterial, "Rejected inputs do not mutate state");
    const float InitialMass = Model.GetBody(0)->CalculateMass();
    Check(Model.SetBodyMaterial(0, 2, 0.2f, 0.8f), "Create owned material");
    Check(Model.GetBody(0)->PhysMaterial->GetOuter() == Model.GetBody(0) && !Model.GetBody(1)->PhysMaterial, "Only edited body owns material");
    Check(Near(Model.GetBody(0)->CalculateMass(), InitialMass*2), "Density changes mass");
    Model.Commit(true);
    Check(Model.SetBodyMaterial(0, 3, 0.4f, 0.9f), "Continue drag");
    Model.Commit(false);
    Check(Model.Undo() && !Model.GetBody(0)->PhysMaterial && !Model.IsDirty(), "One undo restores null material and save point");
    Check(Model.Redo() && Model.GetBodyMaterial(0, Density, Friction, Restitution) &&
        Density == 3 && Friction == 0.4f && Restitution == 0.9f, "Redo restores material values");
    const auto AssetFile = PhysicsAssetFile::GetDefaultPath(Mesh->GetPath());
    Check(PhysicsAssetFile::Write(AssetFile, Mesh->GetPath(), *Mesh, *Mesh->GetPhysicsAsset(), Error), "Save material");
    Check(Model.SetBodyMaterial(0, 4, 0, 0), "Edit after save");
    UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
    Check(PhysicsAssetFile::Read(AssetFile, *Mesh, Asset, Error), "Reload material");
    Model.ResetHistory();
    Check(Model.GetBodyMaterial(0, Density, Friction, Restitution) && Density == 3 &&
        Friction == 0.4f && Restitution == 0.9f && !Model.IsDirty(), "Disk round trip");

    // A rejected snapshot must roll back both existing and newly allocated materials.
    const float Width = Model.GetBody(0)->AggGeom.BoxElems[0].X;
    Model.GetBody(0)->AggGeom.BoxElems[0].X = -1;
    Check(!Model.SetBodyMaterial(0, 8, 0.2f, 0.2f) && Model.GetBody(0)->PhysMaterial->GetDensity() == 3,
        "Capture failure restores existing material");
    Check(!Model.SetBodyMaterial(1, 8, 0.2f, 0.2f) && !Model.GetBody(1)->PhysMaterial,
        "Capture failure releases newly created material");
    Model.GetBody(0)->AggGeom.BoxElems[0].X = Width;

    // Borrowed material must survive editing without changing another body's settings.
    auto* Shared = NewObject<UPhysicalMaterial>(Mesh);
    Model.GetBody(1)->PhysMaterial = Shared;
    Model.ResetHistory();
    Check(Model.SetBodyMaterial(1, 5, 0.6f, 0.2f) && Model.GetBody(1)->PhysMaterial != Shared, "Borrowed material detached on edit");
    Check(Shared->GetDensity() == 1 && Shared->GetFriction() == 0.7f && Shared->GetRestitution() == 0.3f, "Borrowed material remains unchanged");

    Check(GetTestMember(InitTag{})(), "PhysX initialization");
    {
        auto Scene = MakePhysicsScene();
        Scene->CreatePhysicsScene();
        CheckCenterOfMass(Scene.get());
        FBodyInstance Body;
        FBodyInstanceInitParams Params;
        Params.bSimulatePhysics = true;
        Body.InitBody(Model.GetBody(0), Params, nullptr, Scene.get());
        Check(Body.IsValidBodyInstance(), "PhysX body creation");
        auto* Dynamic = Body.GetActorHandle().Actor->is<physx::PxRigidDynamic>();
        Check(Dynamic && Near(Dynamic->getMass(), Model.GetBody(0)->CalculateMass()), "PhysX mass uses edited density");
        physx::PxShape* Shape = nullptr;
        Dynamic->getShapes(&Shape, 1);
        physx::PxMaterial* Material = nullptr;
        Shape->getMaterials(&Material, 1);
        Check(Material && Material->getStaticFriction() == 0.4f && Material->getDynamicFriction() == 0.4f &&
            Material->getRestitution() == 0.9f, "PhysX shape receives friction and restitution");
        Check(Model.SetBodyMaterial(0, 6, 0.1f, 0.1f), "Edit before restart");
        Body.TermBody();
        Body.InitBody(Model.GetBody(0), Params, nullptr, Scene.get());
        Dynamic = Body.GetActorHandle().Actor->is<physx::PxRigidDynamic>();
        Check(Dynamic && Near(Dynamic->getMass(), InitialMass*6), "Restart applies new density");
        Dynamic->getShapes(&Shape, 1); Shape->getMaterials(&Material, 1);
        Check(Material->getStaticFriction() == 0.1f && Material->getRestitution() == 0.1f, "Restart applies new contact material");
    }
    GetTestMember(ShutdownTag{})();
    std::printf("PASS: %d Physics Asset material checks\n", Checks);
}
