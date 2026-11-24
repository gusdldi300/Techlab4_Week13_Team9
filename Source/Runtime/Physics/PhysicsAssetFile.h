#pragma once

#include "Core/EngineString.h"
#include <filesystem>

class USkeletalMesh;
class UPhysicsAsset;

// 메시 옆의 기본 물리 에셋 파일. 에디터와 런타임이 같은 로더를 사용한다.
namespace PhysicsAssetFile
{
    // Validated in-memory snapshots for editor transactions; identical schema to disk.
    bool ToText(const FString& MeshPath, const USkeletalMesh& Mesh, const UPhysicsAsset& Asset,
        FString& OutText, FString& OutError);
    bool ApplyText(const FString& Text, USkeletalMesh& Mesh, UPhysicsAsset*& OutAsset, FString& OutError);
    bool IsPhysicsAssetPath(const FString& Path);
    std::filesystem::path GetDefaultPath(const FString& MeshPath);
    bool ReadPreviewMesh(const std::filesystem::path& Path, FString& OutMeshPath, FString& OutError);
    bool Read(const std::filesystem::path& Path, USkeletalMesh& Mesh,
        UPhysicsAsset*& OutAsset, FString& OutError);
    bool Write(const std::filesystem::path& Path, const FString& MeshPath,
        const USkeletalMesh& Mesh, const UPhysicsAsset& Asset, FString& OutError,
        bool bReplaceExisting = true);
}
