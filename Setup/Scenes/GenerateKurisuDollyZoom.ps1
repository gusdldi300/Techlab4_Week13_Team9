# Compose the demo using existing assets only; PowerShell 7, no extra packages.
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$ContentRoot = Join-Path $ProjectRoot 'DefaultProject/Content'
$ScenesRoot = Join-Path $ContentRoot 'Scenes'
$Scene = Get-Content -LiteralPath (Join-Path $ScenesRoot 'DOF_SunlitStation.scene') -Raw |
    ConvertFrom-Json -Depth 100
$Actors = $Scene.World.PersistentLevel.Actors
$CameraComponent = $Actors[0].Components |
    Where-Object { $_.Class -eq 'UCameraComponent' } |
    ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100
$CameraProperties = $CameraComponent.Properties
$CameraProperties.Transform.Location = @{ X = 2.5; Y = -1.65; Z = 1.80 }
$CameraProperties.Transform.Rotation = @{ Pitch = 0; Yaw = 0; Roll = 0 }
$CameraProperties.FieldOfView = 2 * [Math]::Atan(24.0 / (2 * 170.0)) * 180 / [Math]::PI
$CameraProperties.LensSettings.FocalLengthMm = 170.0
$CameraProperties.DepthOfFieldSettings.bEnabled = $true
$CameraProperties.DepthOfFieldSettings.FStop = 5.6
$CameraProperties.DepthOfFieldSettings.FocalDistanceMeters = 6.0
# CameraManager resolves the animated head after the final camera transform.
$CameraProperties.bAutoFocusEnabled = $true
$CameraProperties.AutoFocusTargetBoneName = 'Bip001-Head'
$CameraProperties.AutoFocusTargetOffset = @{ X = -0.06; Y = 0.0; Z = 0.04 }
$CameraProperties.AutoFocusFallbackOffset = @{ X = 0.0; Y = 0.0; Z = 1.42 }
$CameraProperties.AutoFocusInterpSpeed = 0.0
$CameraComponent.Parent = ''
$Actors[0] = [ordered]@{
    Class = 'ACameraActor'
    Name = 'DZ_Kurisu_UpperBody_Camera'
    ParentActor = ''
    ParentComponent = ''
    RootComponentName = 'CameraComponent'
    Properties = @{ bActorEnableCollision = $false }
    Components = @($CameraComponent, [ordered]@{
        Class = 'UScriptComponent'
        Name = 'DollyZoomScript'
        DefaultSubobjectName = ''
        Parent = ''
        Properties = @{ ScriptPath = 'Content/Scripts/KurisuDollyZoom.lua' }
    })
}

$Actors = @($Actors | Where-Object Name -ne 'SS_Focus_KomorebiStationSign')
$Scene.World.PersistentLevel.Actors = $Actors

# The source OBJ stores the bench's 43 quads before the timetable's 7 quads.
# Keep a separate mesh so other scenes retain their original timetable.
$BenchSource = Join-Path $ContentRoot 'Meshes/SunlitStation/Background_BenchAndTimetable.obj'
$BenchLines = @(Get-Content -LiteralPath $BenchSource)
$FaceLines = @(for ($Index = 0; $Index -lt $BenchLines.Count; $Index++) {
    if ($BenchLines[$Index].StartsWith('f ')) { $Index }
})
if ($FaceLines.Count -ne 50) {
    throw 'Bench source geometry changed; expected 43 bench and 7 timetable quads.'
}
$BenchLines = $BenchLines[0..$FaceLines[42]] -replace
    '^o Background_BenchAndTimetable$', 'o Dolly_BenchWithoutTimetable'
$BenchDestination = Join-Path $ContentRoot 'Meshes/SunlitStation/Dolly_BenchWithoutTimetable.obj'
$BenchLines | Set-Content -LiteralPath $BenchDestination -Encoding ascii
$BenchActor = $Actors | Where-Object Name -eq 'SS_Background_BenchAndTimetable'
$BenchMesh = $BenchActor.Components | Where-Object Class -eq 'UStaticMeshComponent'
$BenchMesh.Properties.StaticMesh = 'Meshes/SunlitStation/Dolly_BenchWithoutTimetable.obj'

foreach ($Actor in $Actors) {
    foreach ($Component in $Actor.Components) {
        foreach ($AssetProperty in @('StaticMesh', 'SkeletalMesh')) {
            $Asset = $Component.Properties.$AssetProperty
            # Names without a path (e.g. Arrow) are built-in engine primitives.
            if ($Asset -and $Asset.Contains('/') -and
                -not (Test-Path -LiteralPath (Join-Path $ContentRoot $Asset))) {
                throw "Missing existing asset: $Asset"
            }
        }
    }
}
$Destination = Join-Path $ScenesRoot 'DOF_KurisuDollyZoom.scene'
$Scene | ConvertTo-Json -Depth 100 |
    Set-Content -LiteralPath $Destination -Encoding utf8NoBOM
Write-Output "Created DOF_KurisuDollyZoom.scene: $($Actors.Count) actors, existing assets only."
