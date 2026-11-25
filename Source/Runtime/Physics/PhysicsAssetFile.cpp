#include "EnginePCH.h"
#include "PhysicsAssetFile.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicsConstraintTemplate.h"
#include "Render/SkeletalMesh.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <stdexcept>

namespace
{
    constexpr int MaxItems = 4096;
    constexpr std::streamoff MaxFileBytes = 16 * 1024 * 1024;

    void Require(bool Condition, const char* Message)
    {
        if (!Condition) throw std::runtime_error(Message);
    }

    int ReadInt(const json& V, int Minimum, int Maximum)
    {
        Require(V.is_number_integer(), "Expected an integer.");
        const double Number = V.get<double>();
        Require(Number >= Minimum && Number <= Maximum, "Integer out of range.");
        return V.get<int>();
    }

    float ReadFloat(const json& V, float Minimum = -1e8f, float Maximum = 1e8f)
    {
        Require(V.is_number(), "Expected a number.");
        const float Number = V.get<float>();
        Require(std::isfinite(Number) && Number >= Minimum && Number <= Maximum, "Invalid numeric value.");
        return Number;
    }

    FString ReadName(const json& V, bool bAllowEmpty = false)
    {
        FString Name = V.get<FString>();
        Require(Name.size() <= 1024 && Name.find('\0') == FString::npos && (bAllowEmpty || !Name.empty()), "Invalid name.");
        return Name;
    }

    const json& Array(const json& V)
    {
        Require(V.is_array() && V.size() <= MaxItems, "Invalid array or too many elements.");
        return V;
    }

    FVector ReadVector(const json& V)
    {
        Require(V.is_array() && V.size() == 3, "Expected a three-component vector.");
        return FVector(ReadFloat(V[0]), ReadFloat(V[1]), ReadFloat(V[2]));
    }

    json Vector(const FVector& V) { return json::array({V.X, V.Y, V.Z}); }
    json Rotation(const FRotator& R) { return json::array({R.Pitch, R.Yaw, R.Roll}); }
    FRotator ReadRotation(const json& V)
    {
        const FVector R = ReadVector(V);
        return FRotator(R.X, R.Y, R.Z);
    }

    template<class T> void ReadShapeCommon(const json& J, T& Shape)
    {
        Shape.Name = FName(ReadName(J.at("Name"), true));
        Shape.bContributeToMass = J.at("ContributeToMass").get<bool>();
        Shape.Center = ReadVector(J.at("Center"));
    }

    FKAggregateGeom ReadGeometry(const json& J)
    {
        FKAggregateGeom G;
        for (const auto& S : Array(J.at("Spheres")))
        {
            FKSphereElem E;
            ReadShapeCommon(S, E);
            E.Radius = ReadFloat(S.at("Radius"), 0.000001f);
            G.SphereElems.Add(E);
        }
        for (const auto& S : Array(J.at("Boxes")))
        {
            FKBoxElem E;
            ReadShapeCommon(S, E);
            E.Rotation = ReadRotation(S.at("Rotation"));
            const FVector Size = ReadVector(S.at("Size"));
            Require(Size.X > 0 && Size.Y > 0 && Size.Z > 0, "Box sizes must be positive.");
            E.X = Size.X; E.Y = Size.Y; E.Z = Size.Z;
            G.BoxElems.Add(E);
        }
        for (const auto& S : Array(J.at("Capsules")))
        {
            FKSphylElem E;
            ReadShapeCommon(S, E);
            E.Rotation = ReadRotation(S.at("Rotation"));
            E.Radius = ReadFloat(S.at("Radius"), 0.000001f);
            E.Length = ReadFloat(S.at("Length"), 0);
            G.SphylElems.Add(E);
        }
        return G;
    }

    json Geometry(const FKAggregateGeom& G)
    {
        json Result = {{"Spheres", json::array()}, {"Boxes", json::array()}, {"Capsules", json::array()}};
        const auto Common = [](const auto& E) {
            return json{{"Name", E.Name.ToString()}, {"ContributeToMass", E.bContributeToMass}, {"Center", Vector(E.Center)}};
        };
        for (const auto& E : G.SphereElems)
        {
            json S = Common(E); S["Radius"] = E.Radius;
            Result["Spheres"].push_back(S);
        }
        for (const auto& E : G.BoxElems)
        {
            json S = Common(E); S["Rotation"] = Rotation(E.Rotation); S["Size"] = json::array({E.X, E.Y, E.Z});
            Result["Boxes"].push_back(S);
        }
        for (const auto& E : G.SphylElems)
        {
            json S = Common(E); S["Rotation"] = Rotation(E.Rotation); S["Radius"] = E.Radius; S["Length"] = E.Length;
            Result["Capsules"].push_back(S);
        }
        return Result;
    }

    void ReadHeader(const json& J)
    {
        Require(J.at("Format") == "KurisuPhysicsAsset", "Not a Physics Asset file.");
        ReadInt(J.at("Version"), 1, 1);
        Require(!ReadName(J.at("PreviewMesh")).empty(), "Missing preview mesh.");
    }

    json ReadDocument(const std::filesystem::path& Path)
    {
        std::ifstream File(Path, std::ios::binary | std::ios::ate);
        Require(static_cast<bool>(File), "Cannot open Physics Asset file.");
        const auto Size = File.tellg();
        Require(Size > 0 && Size <= MaxFileBytes, "Invalid Physics Asset file size.");
        FString Text(static_cast<size_t>(Size), '\0');
        File.seekg(0);
        Require(static_cast<bool>(File.read(Text.data(), static_cast<std::streamsize>(Size))), "Cannot read Physics Asset file.");
        const json J = json::parse(Text);
        ReadHeader(J);
        return J;
    }

    FString ResolvePreviewMesh(const json& J, const std::filesystem::path& Path)
    {
        const FString Key = FAssetUtils::GetAssetKey(J.at("PreviewMesh").get<FString>());
        Require(!Key.empty(), "Preview mesh could not be resolved.");
        Require(std::filesystem::is_regular_file(FPaths::FromUtf8(FAssetUtils::ResolveAssetFilePath(Key))), "Preview mesh file does not exist.");
        const auto Default = PhysicsAssetFile::GetDefaultPath(Key);
        Require(!Default.empty() && std::filesystem::weakly_canonical(Default) == std::filesystem::weakly_canonical(Path),
            "The default Physics Asset must be stored beside its preview mesh.");
        return Key;
    }

    void Validate(const json& J, const USkeletalMesh& Mesh)
    {
        ReadHeader(J);
        std::unordered_set<FString> Bodies;
        for (const auto& B : Array(J.at("Bodies")))
        {
            const FString Name = ReadName(B.at("Bone"));
            const int BoneIndex = Mesh.FindBoneIndex(Name);
            Require(BoneIndex >= 0, "A body refers to a missing skeletal bone.");
            // Match FName's case-insensitive identity, as used by the runtime body map.
            Require(Bodies.insert(Mesh.GetRefSkeleton()[BoneIndex].Name).second, "Duplicate body bone.");
            ReadInt(B.at("PhysicsType"), 0, 2);
            if (B.contains("MassOverrideKg")) ReadFloat(B.at("MassOverrideKg"), 0);
            Require(ReadGeometry(B.at("Geometry")).GetElementCount() > 0, "A body must contain a shape.");
            if (!B.at("Material").is_null())
            {
                const auto& M = B.at("Material");
                ReadFloat(M.at("Friction"), 0);
                ReadFloat(M.at("Restitution"), 0, 1);
                ReadFloat(M.at("Density"), 0.001f);
            }
        }
        std::unordered_set<FString> JointNames;
        for (const auto& C : Array(J.at("Constraints")))
        {
            const FString Child = ReadName(C.at("Child"));
            const FString Parent = ReadName(C.at("Parent"));
            Require(Bodies.count(Child) && Bodies.count(Parent) && Child != Parent, "Constraint bodies are missing or identical.");
            Require(JointNames.insert(Child).second, "A child has more than one parent constraint.");
            ReadVector(C.at("Pos1")); ReadVector(C.at("Pos2"));
            for (const auto* Suffix : {"1", "2"})
            {
                const FVector Primary = ReadVector(C.at(FString("PriAxis") + Suffix));
                const FVector Secondary = ReadVector(C.at(FString("SecAxis") + Suffix));
                const float Dot = Primary.X * Secondary.X + Primary.Y * Secondary.Y + Primary.Z * Secondary.Z;
                Require(std::abs(Primary.SizeSquared() - 1.0f) < 0.001f && std::abs(Secondary.SizeSquared() - 1.0f) < 0.001f
                    && std::abs(Dot) < 0.001f, "Constraint axes must be orthogonal unit vectors.");
            }
            for (const auto* Key : {"XMotion", "YMotion", "ZMotion", "Swing1Motion", "Swing2Motion", "TwistMotion"})
                ReadInt(C.at(Key), 0, 2);
            ReadFloat(C.at("LinearLimit"), 0);
            ReadFloat(C.at("Swing1LimitDegrees"), 0, 180);
            ReadFloat(C.at("Swing2LimitDegrees"), 0, 180);
            ReadFloat(C.at("TwistLimitDegrees"), 0, 180);
            if (C.at("XMotion") == 1 || C.at("YMotion") == 1 || C.at("ZMotion") == 1)
                Require(C.at("LinearLimit").get<float>() > 0, "Limited linear motion requires a positive limit.");
            for (const auto* Prefix : {"Swing1", "Swing2", "Twist"})
            {
                if (C.at(FString(Prefix) + "Motion") == 1)
                {
                    const float Angle = C.at(FString(Prefix) + "LimitDegrees").get<float>();
                    Require(Angle > 0 && Angle < 180, "Limited angular motion requires a limit between 0 and 180 degrees.");
                }
            }
        }
        const int Count = static_cast<int>(J.at("Bodies").size());
        for (const auto& Pair : Array(J.at("DisabledCollisions")))
        {
            Require(Pair.is_array() && Pair.size() == 2, "Invalid collision pair.");
            const int A = ReadInt(Pair[0], 0, Count - 1);
            const int B = ReadInt(Pair[1], 0, Count - 1);
            Require(A != B, "A collision pair cannot refer to itself.");
        }
    }

    // Explicit reload disposes only objects owned by this asset; borrowed materials survive.
    void ReleaseContents(UPhysicsAsset& Asset)
    {
        for (auto* Joint : Asset.PhysicsConstraint)
            if (Joint && Joint->GetOuter() == &Asset) delete Joint;
        for (auto* Body : Asset.BodySetups)
        {
            if (!Body || Body->GetOuter() != &Asset) continue;
            if (Body->PhysMaterial && Body->PhysMaterial->GetOuter() == Body)
                delete Body->PhysMaterial;
            delete Body;
        }
        Asset.PhysicsConstraint.Empty();
        Asset.BodySetups.Empty();
        Asset.CollisionDisableTable.Reset();
    }
}

bool PhysicsAssetFile::IsPhysicsAssetPath(const FString& Path)
{
    FString Extension = FPaths::FromUtf8(Path).extension().string();
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    return Extension == ".physicsasset";
}

std::filesystem::path PhysicsAssetFile::GetDefaultPath(const FString& MeshPath)
{
    const FString File = FAssetUtils::ResolveAssetFilePath(MeshPath);
    return File.empty() ? std::filesystem::path{} : FPaths::FromUtf8(File + ".physicsasset");
}

bool PhysicsAssetFile::ReadPreviewMesh(const std::filesystem::path& Path, FString& OutMeshPath, FString& OutError)
{
    OutError.clear(); OutMeshPath.clear();
    try
    {
        const json J = ReadDocument(Path);
        OutMeshPath = ResolvePreviewMesh(J, Path);
        return true;
    }
    catch (const std::exception& E) { OutError = E.what(); return false; }
}

bool PhysicsAssetFile::Read(const std::filesystem::path& Path, USkeletalMesh& Mesh,
    UPhysicsAsset*& OutAsset, FString& OutError)
{
    OutError.clear();
    try
    {
        const json J = ReadDocument(Path);
        Require(ResolvePreviewMesh(J, Path) == FAssetUtils::GetAssetKey(Mesh.GetPath()), "Physics Asset belongs to another mesh.");
        return ApplyText(J.dump(), Mesh, OutAsset, OutError);
    }
    catch (const std::exception& E) { OutError = E.what(); return false; }
}

bool PhysicsAssetFile::ApplyText(const FString& Text, USkeletalMesh& Mesh,
    UPhysicsAsset*& OutAsset, FString& OutError)
{
    OutError.clear();
    try
    {
        Require(Text.size() <= MaxFileBytes, "Physics Asset is too large.");
        const json J = json::parse(Text);
        const auto Path = GetDefaultPath(Mesh.GetPath());
        const FString MeshKey = ResolvePreviewMesh(J, Path);
        Require(MeshKey == FAssetUtils::GetAssetKey(Mesh.GetPath()), "Physics Asset belongs to another mesh.");
        Validate(J, Mesh); // No UObject is created or modified before full validation succeeds.
        UPhysicsAsset* Asset = OutAsset ? OutAsset : NewObject<UPhysicsAsset>(&Mesh);
        TArray<UBodySetup*> NewBodies;
        TArray<UPhysicsConstraintTemplate*> NewJoints;
        for (const auto& B : J.at("Bodies"))
        {
            auto* Body = NewObject<UBodySetup>(Asset);
            Body->BoneName = FName(B.at("Bone").get<FString>());
            Body->PhysicsType = static_cast<EPhysicsType>(B.at("PhysicsType").get<int>());
            Body->MassOverrideKg = B.contains("MassOverrideKg") ? B.at("MassOverrideKg").get<float>() : 0;
            Body->AggGeom = ReadGeometry(B.at("Geometry"));
            if (!B.at("Material").is_null())
            {
                const auto& M = B.at("Material");
                Body->PhysMaterial = NewObject<UPhysicalMaterial>(Body);
                Body->PhysMaterial->SetFriction(M.at("Friction").get<float>());
                Body->PhysMaterial->SetRestitution(M.at("Restitution").get<float>());
                Body->PhysMaterial->SetDensity(M.at("Density").get<float>());
            }
            NewBodies.Add(Body);
        }
        for (const auto& C : J.at("Constraints"))
        {
            auto* Joint = NewObject<UPhysicsConstraintTemplate>(Asset);
            Joint->ConstraintBone1 = FName(C.at("Child").get<FString>());
            Joint->ConstraintBone2 = FName(C.at("Parent").get<FString>());
#define READ_VECTOR(Name) Joint->Name = ReadVector(C.at(#Name));
            READ_VECTOR(Pos1) READ_VECTOR(Pos2) READ_VECTOR(PriAxis1) READ_VECTOR(PriAxis2) READ_VECTOR(SecAxis1) READ_VECTOR(SecAxis2)
#undef READ_VECTOR
#define READ_MOTION(Name) Joint->Name = static_cast<EConstraintMotion>(C.at(#Name).get<int>());
            READ_MOTION(XMotion) READ_MOTION(YMotion) READ_MOTION(ZMotion) READ_MOTION(Swing1Motion) READ_MOTION(Swing2Motion) READ_MOTION(TwistMotion)
#undef READ_MOTION
#define READ_LIMIT(Name) Joint->Name = C.at(#Name).get<float>();
            READ_LIMIT(LinearLimit) READ_LIMIT(Swing1LimitDegrees) READ_LIMIT(Swing2LimitDegrees) READ_LIMIT(TwistLimitDegrees)
#undef READ_LIMIT
            NewJoints.Add(Joint);
        }
        ReleaseContents(*Asset);
        Asset->BodySetups = std::move(NewBodies);
        Asset->PhysicsConstraint = std::move(NewJoints);
        for (const auto& Pair : J.at("DisabledCollisions")) Asset->DisableCollision(Pair[0].get<int>(), Pair[1].get<int>());
        Asset->UpdateBodySetupIndexMap();
        OutAsset = Asset;
        return true;
    }
    catch (const std::exception& E) { OutError = E.what(); return false; }
}

bool PhysicsAssetFile::Write(const std::filesystem::path& Path, const FString& MeshPath,
    const USkeletalMesh& Mesh, const UPhysicsAsset& Asset, FString& OutError, bool bReplaceExisting)
{
    OutError.clear();
    std::filesystem::path Temporary;
    try
    {
        const FString MeshKey = FAssetUtils::GetAssetKey(MeshPath);
        Require(MeshKey == FAssetUtils::GetAssetKey(Mesh.GetPath()), "Physics Asset belongs to another mesh.");
        const auto Default = GetDefaultPath(MeshKey);
        Require(!Default.empty() && std::filesystem::weakly_canonical(Default) == std::filesystem::weakly_canonical(Path), "Invalid default Physics Asset path.");
        FString Text;
        if (!ToText(MeshPath, Mesh, Asset, Text, OutError)) return false;
        Temporary = Path;
        Temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        {
            std::ofstream File(Temporary, std::ios::binary | std::ios::trunc);
            Require(static_cast<bool>(File), "Cannot create Physics Asset temporary file.");
            File.write(Text.data(), static_cast<std::streamsize>(Text.size()));
            File.flush();
            Require(static_cast<bool>(File), "Cannot write Physics Asset file.");
            File.close();
            Require(!File.fail(), "Cannot finish Physics Asset file.");
        }
        const DWORD Flags = MOVEFILE_WRITE_THROUGH | (bReplaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
        Require(MoveFileExW(Temporary.c_str(), Path.c_str(), Flags) != FALSE, "Cannot commit Physics Asset file; destination may exist or be read-only.");
        return true;
    }
    catch (const std::exception& E)
    {
        OutError = E.what();
        if (!Temporary.empty()) { std::error_code Error; std::filesystem::remove(Temporary, Error); }
        return false;
    }
}

bool PhysicsAssetFile::ToText(const FString& MeshPath, const USkeletalMesh& Mesh,
    const UPhysicsAsset& Asset, FString& OutText, FString& OutError)
{
    OutText.clear(); OutError.clear();
    try
    {
        const FString MeshKey = FAssetUtils::GetAssetKey(MeshPath);
        Require(MeshKey == FAssetUtils::GetAssetKey(Mesh.GetPath()), "Physics Asset belongs to another mesh.");
        json J = {{"Format", "KurisuPhysicsAsset"}, {"Version", 1}, {"PreviewMesh", MeshKey},
            {"Bodies", json::array()}, {"Constraints", json::array()}, {"DisabledCollisions", json::array()}};
        for (const auto* B : Asset.BodySetups)
        {
            Require(B != nullptr, "Cannot save an empty body entry.");
            Require(std::isfinite(B->MassOverrideKg) && B->MassOverrideKg >= 0 && B->MassOverrideKg <= 1e8f, "Invalid body mass override.");
            json Material = nullptr;
            if (B->PhysMaterial) Material = {{"Friction", B->PhysMaterial->GetFriction()}, {"Restitution", B->PhysMaterial->GetRestitution()}, {"Density", B->PhysMaterial->GetDensity()}};
            J["Bodies"].push_back({{"Bone", B->BoneName.ToString()}, {"PhysicsType", static_cast<int>(B->PhysicsType)},
                {"Geometry", Geometry(B->AggGeom)}, {"Material", Material}});
            if (B->MassOverrideKg > 0) J["Bodies"].back()["MassOverrideKg"] = B->MassOverrideKg;
        }
        for (const auto* C : Asset.PhysicsConstraint)
        {
            Require(C != nullptr, "Cannot save an empty constraint entry.");
            json Joint = {{"Child", C->ConstraintBone1.ToString()}, {"Parent", C->ConstraintBone2.ToString()}};
#define WRITE_VECTOR(Name) Joint[#Name] = Vector(C->Name);
            WRITE_VECTOR(Pos1) WRITE_VECTOR(Pos2) WRITE_VECTOR(PriAxis1) WRITE_VECTOR(PriAxis2) WRITE_VECTOR(SecAxis1) WRITE_VECTOR(SecAxis2)
#undef WRITE_VECTOR
#define WRITE_MOTION(Name) Joint[#Name] = static_cast<int>(C->Name);
            WRITE_MOTION(XMotion) WRITE_MOTION(YMotion) WRITE_MOTION(ZMotion) WRITE_MOTION(Swing1Motion) WRITE_MOTION(Swing2Motion) WRITE_MOTION(TwistMotion)
#undef WRITE_MOTION
#define WRITE_LIMIT(Name) Joint[#Name] = C->Name;
            WRITE_LIMIT(LinearLimit) WRITE_LIMIT(Swing1LimitDegrees) WRITE_LIMIT(Swing2LimitDegrees) WRITE_LIMIT(TwistLimitDegrees)
#undef WRITE_LIMIT
            J["Constraints"].push_back(Joint);
        }
        std::vector<uint64> CollisionPairs(Asset.CollisionDisableTable.begin(), Asset.CollisionDisableTable.end());
        std::sort(CollisionPairs.begin(), CollisionPairs.end());
        for (const uint64 Pair : CollisionPairs)
            J["DisabledCollisions"].push_back(json::array({static_cast<uint32>(Pair >> 32), static_cast<uint32>(Pair)}));
        Validate(J, Mesh);
        OutText = J.dump(2) + "\n";
        Require(OutText.size() <= MaxFileBytes, "Physics Asset is too large.");
        return true;
    }
    catch (const std::exception& E) { OutText.clear(); OutError = E.what(); return false; }
}
