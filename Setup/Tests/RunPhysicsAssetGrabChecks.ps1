#requires -Version 5.1
param(
    [switch]$EditorInput,
    [switch]$ConstraintVisuals,
    # Reuse existing runtime DLLs when an editor already has them loaded.
    [switch]$SkipPostBuild,
    # Link against existing editor objects without replacing a running editor executable.
    [switch]$SkipEditorBuild,
    # Optional workaround for MSVC C1902 on hosts with a mismatched PDB service.
    [switch]$EmbeddedDebugInfo
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
    $buildArgs = @('/p:Configuration=Release', '/p:Platform=x64', '/p:CL_MPCount=4', '/m:2', '/v:minimal', '/nologo')
    if ($SkipPostBuild) { $buildArgs += '/p:PostBuildEventUseInBuild=false' }
    if ($EmbeddedDebugInfo) {
        $props = Join-Path $root 'Build/PhysicsGrabBuild.props'
        [IO.File]::WriteAllText($props, '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemDefinitionGroup><ClCompile><DebugInformationFormat>OldStyle</DebugInformationFormat></ClCompile></ItemDefinitionGroup></Project>')
        $buildArgs += "/p:ForceImportBeforeCppTargets=$props"
    }
    & $msbuild Build/Projects/PhysicsAssetGrabChecks.vcxproj @buildArgs
    if ($LASTEXITCODE -ne 0) { throw 'Grab regression build failed.' }
    & ./Build/Bin/Release-windows-x86_64/PhysicsAssetGrabChecks.exe
    if ($LASTEXITCODE -ne 0) { throw 'Grab regression checks failed.' }

    if ($EditorInput -or $ConstraintVisuals) {
        if (-not $SkipEditorBuild) {
            & $msbuild Build/Projects/KurisuEditor.vcxproj @buildArgs
            if ($LASTEXITCODE -ne 0) { throw 'Editor build failed.' }
        }
        # Exercise the actual editor input methods without a native window. Reuse the freshly built
        # editor objects (including ImGui), with a console test entry point and a real D3D device.
        $testNames = @()
        if ($EditorInput) { $testNames += 'PhysicsAssetEditorGrabChecks' }
        if ($ConstraintVisuals) { $testNames += 'PhysicsAssetConstraintVisualChecks' }
        foreach ($testName in $testNames) {
            $project = [xml](Get-Content Build/Projects/PhysicsAssetGrabChecks.vcxproj -Raw)
            $editor = [xml](Get-Content Build/Projects/KurisuEditor.vcxproj -Raw)
            $ns = [Xml.XmlNamespaceManager]::new($project.NameTable)
            $ns.AddNamespace('m', $project.DocumentElement.NamespaceURI)
            $project.SelectSingleNode('//m:ItemGroup/m:ClCompile', $ns).SetAttribute('Include', "../../Setup/Tests/$testName.cpp")
            foreach ($group in $project.Project.PropertyGroup) {
                if ($group.TargetName) {
                    $group.TargetName = $testName
                    $group.IntDir = '$(ProjectDir)..\Intermediate\' + $testName + '\$(Configuration)\'
                }
            }
            $objects = (Get-ChildItem Build/Intermediate/Release-windows-x86_64/KurisuEditor -Filter '*.obj' | ForEach-Object { $_.FullName }) -join ';'
            foreach ($group in $project.Project.ItemDefinitionGroup) {
                $editorGroup = $editor.Project.ItemDefinitionGroup | Where-Object { $_.Condition -eq $group.Condition }
                $group.ClCompile.AdditionalIncludeDirectories = $editorGroup.ClCompile.AdditionalIncludeDirectories
                $group.Link.AdditionalDependencies = $objects + ';' + $editorGroup.Link.AdditionalDependencies
                $group.Link.AdditionalLibraryDirectories = $editorGroup.Link.AdditionalLibraryDirectories
            }
            $testProject = Join-Path $root "Build/Projects/$testName.vcxproj"
            $project.Save($testProject)
            & $msbuild $testProject @buildArgs /p:BuildProjectReferences=false
            if ($LASTEXITCODE -ne 0) { throw "$testName build failed." }
            & "./Build/Bin/Release-windows-x86_64/$testName.exe"
            if ($LASTEXITCODE -ne 0) { throw "$testName failed." }
        }
    }
}
finally { Pop-Location }
