#requires -Version 5.1
param(
    [switch]$PrepareAsset,
    [switch]$SkipEditorBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$msbuild = & $vswhere -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/amd64/MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio MSBuild was not found.' }
Push-Location -LiteralPath $root
try {
    & ./Setup/Build/premake5.exe --file=Setup/Build/premake5.lua --physics-grab-checks vs2026
    if ($LASTEXITCODE -ne 0) { throw 'Project generation failed.' }
    $props = Join-Path $root 'Build/RagdollDemoBuild.props'
    [IO.File]::WriteAllText($props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemDefinitionGroup><ClCompile><DebugInformationFormat>OldStyle</DebugInformationFormat></ClCompile></ItemDefinitionGroup></Project>')
    $buildArgs = @('/p:Configuration=Release', '/p:Platform=x64', '/p:CL_MPCount=4', '/m:2', '/v:minimal', '/nologo', "/p:ForceImportBeforeCppTargets=$props")
    if (-not $SkipEditorBuild) {
        & $msbuild Build/Projects/KurisuEditor.vcxproj @buildArgs
        if ($LASTEXITCODE -ne 0) { throw 'Editor build failed.' }
    }
    # Reuse the runtime console harness configuration and libraries, including PhysX and NvCloth.
    $project = [xml](Get-Content Build/Projects/PhysicsAssetGrabChecks.vcxproj -Raw)
    $ns = [Xml.XmlNamespaceManager]::new($project.NameTable)
    $ns.AddNamespace('m', $project.DocumentElement.NamespaceURI)
    $project.SelectSingleNode('//m:ItemGroup/m:ClCompile', $ns).SetAttribute('Include', '../../Setup/Tests/RagdollDemoChecks.cpp')
    foreach ($group in $project.Project.PropertyGroup) {
        if ($group.TargetName) {
            $group.TargetName = 'RagdollDemoChecks'
            $group.IntDir = '$(ProjectDir)..\Intermediate\RagdollDemoChecks\$(Configuration)\'
        }
    }
    $project.Save((Join-Path $root 'Build/Projects/RagdollDemoChecks.vcxproj'))
    & $msbuild Build/Projects/RagdollDemoChecks.vcxproj @buildArgs /p:BuildProjectReferences=false /p:PostBuildEventUseInBuild=false
    if ($LASTEXITCODE -ne 0) { throw 'Demo regression build failed.' }
    if ($PrepareAsset) {
        & ./Build/Bin/Release-windows-x86_64/RagdollDemoChecks.exe --prepare-asset
        if ($LASTEXITCODE -ne 0) { throw 'Demo asset generation failed.' }
    }
    & ./Build/Bin/Release-windows-x86_64/RagdollDemoChecks.exe
    if ($LASTEXITCODE -ne 0) { throw 'Demo regression checks failed.' }
}
finally { Pop-Location }
