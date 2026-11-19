#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$toolDirectory = Split-Path -Parent $PSScriptRoot
$projectRoot = (Resolve-Path -LiteralPath "$toolDirectory\..\..").Path
$testRoot = Join-Path $projectRoot ("Build\Tests\SourceLink\" + [guid]::NewGuid())
$fixtureRoot = Join-Path $testRoot 'ZIP project with spaces'
$fixtureTools = Join-Path $fixtureRoot 'Setup\CrashDump'
[void][IO.Directory]::CreateDirectory($fixtureTools)
$generator = Join-Path $fixtureTools 'GenerateSourceLink.ps1'
Copy-Item -LiteralPath (Join-Path $toolDirectory 'GenerateSourceLink.ps1') -Destination $generator
$outputPath = Join-Path $fixtureRoot 'Intermediate\sourcelink.json'
$powerShell = Join-Path $PSHOME 'powershell.exe'
$git = (Get-Command git -CommandType Application | Select-Object -First 1).Source

function Invoke-FixtureGit([string[]]$Arguments) {
    & $git -C $fixtureRoot @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Fixture Git command failed: $Arguments"
    }
}

function Invoke-Generator([string]$Destination = $outputPath, [switch]$ExpectFailure) {
    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = $powerShell
    $startInfo.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -OutputPath "{1}"' -f `
        $generator, $Destination
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        $standardOutput = $process.StandardOutput.ReadToEnd()
        $standardError = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if ($ExpectFailure) {
            if ($process.ExitCode -eq 0) {
                throw 'The generator unexpectedly accepted an invalid output path.'
            }
        } elseif ($process.ExitCode -ne 0) {
            throw "Generator failed: $standardOutput $standardError"
        }
    } finally {
        $process.Dispose()
    }
}

function Assert-EmptyMapping([string]$Scenario) {
    Invoke-Generator
    $json = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    if ($null -eq $json.documents -or @($json.documents.PSObject.Properties).Count -ne 0) {
        throw "Expected an empty mapping: $Scenario"
    }
    Write-Host "PASS: $Scenario"
}

# This fixture is inside the real repository: a ZIP must not inherit its parent's Git metadata.
Assert-EmptyMapping 'ZIP inside another Git repository'
Invoke-FixtureGit @('init', '--quiet')
Assert-EmptyMapping 'Repository without an origin'
Invoke-FixtureGit @('remote', 'add', 'origin', 'https://github.com/example/source-link-test.git')
Assert-EmptyMapping 'Repository without a commit'
Invoke-FixtureGit @('-c', 'user.name=Source Link Test', '-c', 'user.email=test@example.invalid',
    '-c', 'commit.gpgsign=false', '-c', "core.hooksPath=$testRoot\no-hooks",
    'commit', '--quiet', '--allow-empty', '-m', 'Source Link fixture')
$commit = & $git -C $fixtureRoot rev-parse --verify HEAD
if ($LASTEXITCODE -ne 0) {
    throw 'Could not read the fixture commit.'
}

foreach ($origin in @('https://github.com/example/source-link-test.git',
    'git@github.com:example/source-link-test.git')) {
    Invoke-FixtureGit @('remote', 'set-url', 'origin', $origin)
    Invoke-Generator
    $json = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
    $properties = @($json.documents.PSObject.Properties)
    $expectedUrl = "https://raw.githubusercontent.com/example/source-link-test/$commit/*"
    if ($properties.Count -ne 1 -or $properties[0].Name -ne "$fixtureRoot\*" -or
        $properties[0].Value -ne $expectedUrl) {
        throw "Incorrect source mapping for $origin"
    }
    Write-Host "PASS: $origin"
}

$originalPath = $env:PATH
try {
    $env:PATH = Join-Path $env:SystemRoot 'System32'
    Assert-EmptyMapping 'Git unavailable; existing mapping is replaced'
} finally {
    $env:PATH = $originalPath
}
Invoke-FixtureGit @('remote', 'set-url', 'origin', 'https://example.invalid/project.git')
Assert-EmptyMapping 'Origin without supported GitHub mapping'

$blockingFile = Join-Path $fixtureRoot 'blocking-file'
[IO.File]::WriteAllText($blockingFile, 'This file cannot contain a child directory.')
Invoke-Generator -Destination (Join-Path $blockingFile 'sourcelink.json') -ExpectFailure
Write-Host 'PASS: output write errors still fail'
Write-Host "Source Link checks passed. Fixtures: $testRoot"
