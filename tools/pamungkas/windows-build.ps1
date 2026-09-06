[CmdletBinding()]
param(
    [switch]$Initialize,
    [switch]$CheckOnly,
    [ValidateSet('Component', 'Release', 'Static', 'Debug')]
    [string]$Configuration = 'Release',
    [ValidateRange(40, 2048)]
    [int]$MinimumFreeGB = 120
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Fail([string]$Message) {
    throw "PAMUNGKAS BUILD NO-GO: $Message"
}

function Require-Command([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) {
        Fail "Required command '$Name' was not found in PATH."
    }
    return $cmd.Source
}

function Get-CommandVersion([string]$Name, [string[]]$Arguments) {
    try {
        return ((& $Name @Arguments 2>&1) | Out-String).Trim()
    } catch {
        return "unavailable: $($_.Exception.Message)"
    }
}

$braveDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$expectedSuffix = [IO.Path]::Combine('src', 'brave')
$normalized = $braveDir.TrimEnd('\', '/') -replace '/', '\'
if (-not $normalized.EndsWith($expectedSuffix, [StringComparison]::OrdinalIgnoreCase)) {
    Fail "brave-core must be checked out at <project>\src\brave. Current path: $braveDir"
}

$packageJson = Join-Path $braveDir 'package.json'
if (-not (Test-Path -LiteralPath $packageJson -PathType Leaf)) {
    Fail "package.json is missing from brave-core root."
}

$git = Require-Command 'git'
$node = Require-Command 'node'
$pnpm = Require-Command 'pnpm'
$python = Require-Command 'python'

Push-Location $braveDir
try {
    $branch = (& git branch --show-current).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $branch) {
        Fail "Unable to determine current Git branch."
    }
    if ($branch -ne 'pamungkas/portable-oscrypt-poc') {
        Fail "Expected branch pamungkas/portable-oscrypt-poc, found '$branch'."
    }

    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $commit) {
        Fail "Unable to resolve current commit."
    }

    $status = (& git status --porcelain=v1)
    if ($LASTEXITCODE -ne 0) {
        Fail "git status failed."
    }
    if ($status) {
        Fail "Working tree is not clean. Commit or discard local changes before building."
    }

    $projectRoot = (Resolve-Path (Join-Path $braveDir '..\..')).Path
    $driveRoot = [IO.Path]::GetPathRoot($projectRoot)
    $drive = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$($driveRoot.TrimEnd('\'))'"
    if (-not $drive) {
        Fail "Unable to determine free disk space for $driveRoot."
    }
    $freeGB = [math]::Round($drive.FreeSpace / 1GB, 1)
    if ($freeGB -lt $MinimumFreeGB) {
        Fail "Free disk space is $freeGB GB; safety minimum is $MinimumFreeGB GB."
    }

    $package = Get-Content -LiteralPath $packageJson -Raw | ConvertFrom-Json
    $chromiumTag = $package.config.projects.chrome.tag
    if (-not $chromiumTag) {
        Fail "Pinned Chromium tag could not be read from package.json."
    }

    $evidence = [ordered]@{
        status = 'PREFLIGHT_PASS'
        generated_at_utc = [DateTime]::UtcNow.ToString('o')
        brave_core_branch = $branch
        brave_core_commit = $commit
        brave_core_version = $package.version
        chromium_tag = $chromiumTag
        configuration = $Configuration
        initialize_requested = [bool]$Initialize
        free_disk_gb = $freeGB
        minimum_free_disk_gb = $MinimumFreeGB
        tools = [ordered]@{
            git = Get-CommandVersion 'git' @('--version')
            node = Get-CommandVersion 'node' @('--version')
            pnpm = Get-CommandVersion 'pnpm' @('--version')
            python = Get-CommandVersion 'python' @('--version')
        }
    }

    $evidenceDir = Join-Path $projectRoot '.pamungkas\evidence'
    New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
    $evidencePath = Join-Path $evidenceDir 'windows-build-preflight.json'
    $evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $evidencePath -Encoding UTF8
    Write-Host "PAMUNGKAS preflight PASS. Evidence: $evidencePath"
    Write-Host "Brave Core $($package.version) / Chromium $chromiumTag / $commit"
    Write-Host "Free disk: $freeGB GB"

    if ($CheckOnly) {
        Write-Host 'CheckOnly requested; no init or compile was started.'
        exit 0
    }

    $chromiumMarker = Join-Path $projectRoot 'src\chrome\browser'
    if ($Initialize) {
        Write-Host 'Initializing Brave/Chromium build workspace...'
        & pnpm run init
        if ($LASTEXITCODE -ne 0) {
            Fail "pnpm run init failed with exit code $LASTEXITCODE."
        }
    } elseif (-not (Test-Path -LiteralPath $chromiumMarker -PathType Container)) {
        Fail "Chromium workspace is not initialized. Re-run with -Initialize."
    }

    Write-Host "Starting Brave $Configuration build..."
    if ($Configuration -eq 'Component') {
        & pnpm run build
    } else {
        & pnpm run build $Configuration
    }
    if ($LASTEXITCODE -ne 0) {
        Fail "Brave $Configuration build failed with exit code $LASTEXITCODE."
    }

    $success = [ordered]@{
        status = 'BUILD_PASS'
        generated_at_utc = [DateTime]::UtcNow.ToString('o')
        brave_core_branch = $branch
        brave_core_commit = $commit
        brave_core_version = $package.version
        chromium_tag = $chromiumTag
        configuration = $Configuration
    }
    $successPath = Join-Path $evidenceDir 'windows-build-result.json'
    $success | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $successPath -Encoding UTF8
    Write-Host "PAMUNGKAS build PASS. Evidence: $successPath"
} finally {
    Pop-Location
}
