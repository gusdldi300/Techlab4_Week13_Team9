#requires -Version 5.1
param([Parameter(Mandatory)][string]$OutputPath)

$ErrorActionPreference = 'Stop'
# Setup\CrashDump 기준으로 두 단계 위를 구해 빌드 PC의 프로젝트 경로를 찾는다.
$RepoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot\..\..").Path

function Get-SourceLinkDocuments {
    # ZIP이 다른 저장소 안에 풀려 있어도 상위 저장소의 Git 정보를 사용하지 않는다.
    if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot '.git'))) {
        return @{}
    }
    if (-not (Get-Command git -CommandType Application -ErrorAction SilentlyContinue)) {
        return @{}
    }

    # origin의 HTTPS 또는 SSH 주소에서 GitHub의 OWNER/REPO를 구한다.
    $Origin = git -C $RepoRoot config --get remote.origin.url
    if ($LASTEXITCODE -eq 1) {
        return @{}
    }
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not read the Git origin for Source Link.'
    }
    $GitHubPattern = '^(?:https://github\.com/|git@github\.com:)(?<Repo>[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/?$'
    if ($Origin -notmatch $GitHubPattern) {
        return @{}
    }
    $GitHubRepo = $Matches['Repo']

    # 브랜치가 이동한 뒤에도 빌드 시점의 파일을 찾도록 전체 커밋 해시로 URL을 고정한다.
    $Commit = git -C $RepoRoot rev-parse --verify --quiet HEAD
    if ($LASTEXITCODE -eq 1) {
        return @{}
    }
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not resolve the Git commit for Source Link.'
    }

    # PDB에 기록되는 로컬 소스 경로를 해당 커밋의 GitHub 원본 파일에 대응시킨다.
    $SourceUrl = "https://raw.githubusercontent.com/$GitHubRepo/$Commit/*"
    return @{ "$RepoRoot\*" = $SourceUrl }
}

$Documents = Get-SourceLinkDocuments
if ($Documents.Count -eq 0) {
    Write-Host 'Source Link: GitHub commit information is unavailable; using an empty source mapping.'
}
$SourceLinkJson = @{ documents = $Documents } | ConvertTo-Json -Depth 3

[void][IO.Directory]::CreateDirectory((Split-Path -Parent $OutputPath))
[IO.File]::WriteAllText($OutputPath, $SourceLinkJson, [Text.UTF8Encoding]::new($false))
