$ErrorActionPreference = 'Stop'
$sceneDirectory = Join-Path $PSScriptRoot '../../DefaultProject/Content/Scenes'
$sources = @(
    @{ File = 'DOF_FocusLens.scene'; Offset = 0.0; Prefix = 'Lab_Focus_' },
    @{ File = 'DOF_BokehHDR.scene'; Offset = 100.0; Prefix = 'Lab_Bokeh_' },
    @{ File = 'DOF_DepthEdges.scene'; Offset = 200.0; Prefix = 'Lab_Edges_' }
)
$actors = [Collections.Generic.List[object]]::new()
$outputScene = $null
foreach ($source in $sources) {
    $scene = Get-Content -LiteralPath (Join-Path $sceneDirectory $source.File) `
        -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($null -eq $outputScene) { $outputScene = $scene }
    foreach ($actor in $scene.World.PersistentLevel.Actors) {
        if ($source.Offset -ne 0 -and $actor.Class -ne 'AStaticMeshActor') { continue }
        if ($source.Prefix -eq 'Lab_Focus_' -and
            $actor.Name -eq 'DOF_ContinuousFocus_DepthRuler') { continue }
        $actor.Name = if ($actor.Class -eq 'ADefaultPawn') {
            'DOF_InteractiveLab_Camera'
        } else {
            $source.Prefix + $actor.Name
        }
        $root = @($actor.Components | Where-Object { $_.Name -eq $actor.RootComponentName })
        if ($root.Count -ne 1) { throw "Invalid root: $($actor.Name)" }
        $root[0].Properties.Transform.Location.Y += $source.Offset
        $actors.Add($actor)
    }
}
$outputScene.World.PersistentLevel.Actors = $actors.ToArray()
$outputPath = Join-Path $sceneDirectory 'DOF_InteractiveLab.scene'
$sceneJson = $outputScene | ConvertTo-Json -Depth 100
[IO.File]::WriteAllText([IO.Path]::GetFullPath($outputPath), $sceneJson,
    [Text.UTF8Encoding]::new($false))
Write-Output "Generated DOF_InteractiveLab.scene: $($actors.Count) actors, three test stations."
