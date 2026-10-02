#Requires -Version 7.0
<#
.SYNOPSIS
    Build the LegendGame Windows x64 distributable package.

.DESCRIPTION
    Assembles dist/LegendGame-Windows-x64 from a Release build:
      - 8 formal EXEs
      - runtime DLLs (SDL3/libsodium from the build tree + VC++ redist CRT
        located via vswhere at packaging time; no hard-coded dev paths)
      - Data/ Config/ Assets/ (whole trees, so future Data/Assets/UI etc.
        are picked up automatically)
      - build-info.txt, Start-LegendGame.bat, Start-Client.bat,
        Stop-Local-Servers.bat
    Then runs a self-verification gate: any missing file fails the script
    with a non-zero exit code.

    All paths are relative to the repo root. The script is idempotent:
    the dist directory is rebuilt from scratch on every run.

    Must run on Windows with PowerShell 7+ (pwsh). The script itself is
    ASCII-only to avoid any PS 5.1 encoding pitfalls; batch files are
    generated with CRLF line endings.

.EXAMPLE
    pwsh -File Tools/Packaging/PackageWindows.ps1 `
        -BuildBinDir Build/bin/Release `
        -DistDir dist/LegendGame-Windows-x64 `
        -Version v0.27.0 -CommitSha abc1234
#>
[CmdletBinding()]
param(
    [string]$RepoRoot    = (Resolve-Path (Join-Path $PSScriptRoot '..\..')),
    [string]$BuildBinDir = (Join-Path $RepoRoot 'Build\bin\Release'),
    [string]$DistDir     = (Join-Path $RepoRoot 'dist\LegendGame-Windows-x64'),
    [string]$Version     = 'dev',
    [string]$CommitSha   = 'unknown',
    [string]$BuildTimeUtc = ([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --------------------------------------------------------------------------
# Manifests (single source of truth for the package layout)
# --------------------------------------------------------------------------
$FormalExes = @(
    'LegendClient', 'LegendMapEditor',
    'LegendLoginServer', 'LegendCharacterServer',
    'LegendGateway', 'LegendWorldServer',
    'LegendDbServer', 'LegendLogServer'
)
# Third-party DLLs produced/linked by our own build (already copied next to
# the exes by the POST_BUILD TARGET_RUNTIME_DLLS steps in CMake).
$BuildDlls = @('SDL3.dll', 'libsodium.dll')
# VC++ redist CRT DLLs every formal exe needs on machines without VS.
$CrtDlls = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll')
# Key data files that must exist or the game cannot start.
$KeyJsons = @(
    'Data\World\maps.json',
    'Data\Game\items.json',
    'Data\Game\quests.json',
    'Config\servers.json'
)

function Fail([string]$Message) {
    # Emit ::error:: so the failure reason shows up in the Actions page
    # Annotations without needing admin log download.
    Write-Host "::error::$Message"
    Write-Error $Message
    exit 1
}

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        Fail("$Description missing: $Path")
    }
}

function Require-Dir([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        Fail("$Description missing: $Path")
    }
}

function Diag([string]$Message) {
    Write-Host $Message
    # Mirror into Annotations so the lookup is diagnosable from the run page
    # without admin log download.
    $safe = $Message -replace "`r?`n", ' '
    Write-Host "::notice::$safe"
}

function Find-CrtUnderVcRoot([string]$VcRoot) {
    if (-not $VcRoot) { return $null }
    $redistBase = Join-Path $VcRoot 'VC\Redist\MSVC'
    if (-not (Test-Path -LiteralPath $redistBase -PathType Container)) { return $null }
    $latest = Get-ChildItem -LiteralPath $redistBase -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $latest) { return $null }
    $crt = Get-ChildItem -LiteralPath (Join-Path $latest.FullName 'x64') -Directory `
        -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $crt) { return $null }
    return $crt.FullName
}

function Find-VcRedistCrtDir {
    # Locate the VC++ redist CRT directory (no hard-coded dev-machine paths,
    # no hard-coded VS major version).
    # Strategy 1: vswhere, no -requires filter first (latest VS of any kind).
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    Diag "CRT lookup: vswhere candidate = $vswhere"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installPath = (& $vswhere -latest -property installationPath 2>$null |
            Select-Object -First 1)
        Diag "CRT lookup: vswhere (no filter) installationPath = '$installPath'"
        $dir = Find-CrtUnderVcRoot $installPath
        if ($dir) { Diag "CRT lookup: HIT $dir"; return $dir }
        $installPath2 = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools `
            -property installationPath 2>$null | Select-Object -First 1)
        Diag "CRT lookup: vswhere (VC.Tools filter) installationPath = '$installPath2'"
        $dir = Find-CrtUnderVcRoot $installPath2
        if ($dir) { Diag "CRT lookup: HIT $dir"; return $dir }
        Diag 'CRT lookup: vswhere paths yielded no CRT dir'
    } else {
        Diag 'CRT lookup: vswhere.exe not found'
    }
    # Strategy 2: glob over ANY VS version/edition under Program Files.
    foreach ($pf in @(${env:ProgramFiles}, ${env:ProgramFiles(x86)})) {
        if (-not $pf) { continue }
        $vsRoot = Join-Path $pf 'Microsoft Visual Studio'
        if (-not (Test-Path -LiteralPath $vsRoot -PathType Container)) {
            Diag "CRT lookup: missing $vsRoot"; continue
        }
        $vers = Get-ChildItem -LiteralPath $vsRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
        Diag "CRT lookup: under $vsRoot versions: $(($vers | ForEach-Object { $_.Name }) -join ', ')"
        foreach ($ver in $vers) {
            $editions = Get-ChildItem -LiteralPath $ver.FullName -Directory -ErrorAction SilentlyContinue
            foreach ($ed in $editions) {
                $dir = Find-CrtUnderVcRoot $ed.FullName
                if ($dir) { Diag "CRT lookup: HIT $dir"; return $dir }
            }
        }
    }
    # Strategy 3: VCToolsRedistDir env (set by some CI images; points at the
    # redist version dir, CRT lives under x64\Microsoft.VC*.CRT).
    if (${env:VCToolsRedistDir}) {
        Diag "CRT lookup: trying VCToolsRedistDir = ${env:VCToolsRedistDir}"
        $crt = Get-ChildItem -LiteralPath (Join-Path ${env:VCToolsRedistDir} 'x64') `
            -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($crt) { Diag "CRT lookup: HIT $($crt.FullName)"; return $crt.FullName }
    }
    Diag 'CRT lookup: all strategies exhausted, no CRT dir found'
    return $null
}


Write-Host "=== LegendGame Windows packaging ==="
Write-Host "RepoRoot   : $RepoRoot"
Write-Host "BuildBinDir: $BuildBinDir"
Write-Host "DistDir    : $DistDir"
Write-Host "Version    : $Version"
Write-Host "CommitSha  : $CommitSha"

Require-Dir $RepoRoot 'Repo root'
Require-Dir $BuildBinDir 'Release build output directory'

# --------------------------------------------------------------------------
# 1. Fresh dist directory (idempotent)
# --------------------------------------------------------------------------
if (Test-Path -LiteralPath $DistDir) {
    Write-Host "Cleaning existing dist dir..."
    Remove-Item -LiteralPath $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

# --------------------------------------------------------------------------
# 2. Formal executables
# --------------------------------------------------------------------------
Write-Host "Copying 8 formal executables..."
foreach ($name in $FormalExes) {
    $src = Join-Path $BuildBinDir "$name.exe"
    Require-File $src "Formal executable $name.exe"
    Copy-Item -LiteralPath $src -Destination $DistDir -Force
}

# --------------------------------------------------------------------------
# 3. Third-party runtime DLLs from the build tree (allow-listed on purpose:
#    CI-only DLLs such as the Mesa software-GL opengl32.dll must NOT ship)
# --------------------------------------------------------------------------
Write-Host "Copying build-tree runtime DLLs..."
foreach ($dll in $BuildDlls) {
    $src = Join-Path $BuildBinDir $dll
    Require-File $src "Runtime DLL $dll"
    Copy-Item -LiteralPath $src -Destination $DistDir -Force
}

# --------------------------------------------------------------------------
# 4. VC++ redist CRT (for PCs without Visual Studio installed)
# --------------------------------------------------------------------------
Write-Host "Locating VC++ redist CRT via vswhere..."
$crtDir = Find-VcRedistCrtDir
if (-not $crtDir) {
    Fail('VC++ redist CRT directory not found via vswhere; ' +
         'cannot produce a redistributable package without the CRT.')
}
Write-Host "CRT dir: $crtDir"
$missingCrt = @()
foreach ($dll in $CrtDlls) {
    $src = Join-Path $crtDir $dll
    if (Test-Path -LiteralPath $src -PathType Leaf) {
        Copy-Item -LiteralPath $src -Destination $DistDir -Force
    } else {
        $missingCrt += $dll
    }
}
# msvcp140.dll + vcruntime140.dll are mandatory; the _1/concrt ones are
# best-effort across VS versions.
foreach ($must in @('msvcp140.dll', 'vcruntime140.dll')) {
    if ($missingCrt -contains $must) {
        Fail("Mandatory CRT DLL missing in redist dir: $must")
    }
}
if ($missingCrt.Count -gt 0) {
    Write-Host "NOTE: optional CRT DLLs not present (ok): $($missingCrt -join ', ')"
}

# --------------------------------------------------------------------------
# 5. Data / Config / Assets (whole trees)
# --------------------------------------------------------------------------
Write-Host "Copying Data/, Config/, Assets/..."
foreach ($tree in @('Data', 'Config', 'Assets')) {
    $src = Join-Path $RepoRoot $tree
    Require-Dir $src "Source tree $tree"
    Copy-Item -LiteralPath $src -Destination (Join-Path $DistDir $tree) -Recurse -Force
}
# Never ship runtime leftovers even if they exist in the working tree.
foreach ($junk in @('Data\legend_account.db', 'Data\legend_account.db-wal',
                    'Data\legend_account.db-shm', 'Data\World\.backup',
                    'Data\Game\.backup')) {
    $p = Join-Path $DistDir $junk
    if (Test-Path -LiteralPath $p) {
        Write-Host "Removing runtime leftover from package: $junk"
        Remove-Item -LiteralPath $p -Recurse -Force
    }
}

# --------------------------------------------------------------------------
# 6. build-info.txt (ASCII, technical build metadata)
# --------------------------------------------------------------------------
Write-Host "Writing build-info.txt..."
$shortSha = if ($CommitSha.Length -ge 7) { $CommitSha.Substring(0, 7) } else { $CommitSha }
$buildInfo = @(
    'LegendGame',
    "Version: $Version",
    "Commit: $CommitSha",
    "ShortCommit: $shortSha",
    'Build: Release',
    'Platform: Windows x64',
    "Build Time: $BuildTimeUtc"
) -join "`r`n"
[IO.File]::WriteAllText((Join-Path $DistDir 'build-info.txt'),
    $buildInfo + "`r`n", [Text.Encoding]::ASCII)

# --------------------------------------------------------------------------
# 7. Launcher batch files (ASCII + CRLF so they run on any Windows codepage)
# --------------------------------------------------------------------------
function Write-BatFile([string]$Name, [string]$Content) {
    $normalized = ($Content -replace "`r?`n", "`r`n").TrimStart("`r`n")
    [IO.File]::WriteAllText((Join-Path $DistDir $Name),
        $normalized + "`r`n", [Text.Encoding]::ASCII)
    Write-Host "Generated $Name"
}

$startGame = @'
@echo off
rem ============================================================
rem  LegendGame one-click launcher (local topology + client)
rem  Starts the 6 services in GUI mode (no console windows),
rem  waits for the ports, then starts LegendClient.
rem ============================================================
setlocal
cd /d "%~dp0"

start "LegendDbServer"        "LegendDbServer.exe"        --config Config/servers.json
start "LegendLogServer"       "LegendLogServer.exe"       --config Config/servers.json
start "LegendLoginServer"     "LegendLoginServer.exe"     --config Config/servers.json
start "LegendCharacterServer" "LegendCharacterServer.exe" --config Config/servers.json
start "LegendWorldServer"     "LegendWorldServer.exe"     --config Config/servers.json
start "LegendGateway"         "LegendGateway.exe"         --config Config/servers.json

echo Waiting for services to become ready...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ports=7500,7600,7100,7400,7200,7300; $deadline=(Get-Date).AddSeconds(45); do { $ready=$true; foreach($p in $ports){ $c=New-Object Net.Sockets.TcpClient; try{ $c.Connect('127.0.0.1',$p) }catch{ $ready=$false }finally{ $c.Close() } }; if(-not $ready){ Start-Sleep -Milliseconds 500 } } until($ready -or (Get-Date) -ge $deadline); if(-not $ready){ Write-Host 'ERROR: services did not become ready in time. Check the service windows.'; exit 1 }"
if errorlevel 1 (
    echo Failed to start local servers. See the messages above.
    pause
    exit /b 1
)

start "LegendClient" "LegendClient.exe"
echo LegendGame started. You can close this window; the game keeps running.
endlocal
exit /b 0
'@

$startClient = @'
@echo off
rem ============================================================
rem  LegendGame client-only launcher
rem  Use this to connect to a remote server (edit Config/servers.json
rem  gateway host/port first).
rem ============================================================
setlocal
cd /d "%~dp0"
start "LegendClient" "LegendClient.exe"
endlocal
'@

$stopServers = @'
@echo off
rem ============================================================
rem  Stop local LegendGame servers started from THIS directory.
rem  Only touches processes whose executable lives under this folder,
rem  and only asks their windows to close gracefully (WM_CLOSE).
rem  Never force-kills anything.
rem ============================================================
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root=(Get-Location).Path; " ^
  "$names=@('LegendDbServer','LegendLogServer','LegendLoginServer','LegendCharacterServer','LegendWorldServer','LegendGateway'); " ^
  "$procs=Get-Process -ErrorAction SilentlyContinue | Where-Object { $names -contains $_.ProcessName } | Where-Object { try { $_.Path -and $_.Path.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) } catch { $false } }; " ^
  "if(-not $procs){ Write-Host 'No local server processes found under this directory.'; exit 0 }; " ^
  "foreach($p in $procs){ Write-Host ('Closing '+$p.ProcessName+' ...'); $p.CloseMainWindow() | Out-Null }; " ^
  "$deadline=(Get-Date).AddSeconds(15); " ^
  "do { Start-Sleep -Milliseconds 500; $alive=@($procs | Where-Object { -not $_.HasExited }) } until($alive.Count -eq 0 -or (Get-Date) -ge $deadline); " ^
  "if($alive.Count -gt 0){ Write-Host 'These processes did not exit gracefully, please close their windows manually:'; $alive | ForEach-Object { Write-Host (' - '+$_.ProcessName+' (pid '+$_.Id+')') }; exit 1 }; " ^
  "Write-Host 'All local servers stopped.'"
if errorlevel 1 pause
endlocal
'@

Write-BatFile 'Start-LegendGame.bat' $startGame
Write-BatFile 'Start-Client.bat' $startClient
Write-BatFile 'Stop-Local-Servers.bat' $stopServers

# --------------------------------------------------------------------------
# 8. Packaging self-verification gate (any miss => non-zero exit)
# --------------------------------------------------------------------------
Write-Host "=== Packaging self-verification ==="
$errors = 0
function Check-File([string]$RelPath) {
    $p = Join-Path $DistDir $RelPath
    if (Test-Path -LiteralPath $p -PathType Leaf) {
        Write-Host "  OK  $RelPath"
    } else {
        Write-Host "  MISS $RelPath"
        $script:errors++
    }
}
function Check-Dir([string]$RelPath) {
    $p = Join-Path $DistDir $RelPath
    if (Test-Path -LiteralPath $p -PathType Container) {
        Write-Host "  OK  $RelPath/"
    } else {
        Write-Host "  MISS $RelPath/"
        $script:errors++
    }
}

foreach ($name in $FormalExes) { Check-File "$name.exe" }
foreach ($dll in $BuildDlls)   { Check-File $dll }
foreach ($dll in @('msvcp140.dll', 'vcruntime140.dll')) { Check-File $dll }
foreach ($j in $KeyJsons)      { Check-File $j }
Check-Dir 'Data'
Check-Dir 'Data\World'
Check-Dir 'Data\Game'
Check-Dir 'Data\Assets'
Check-Dir 'Config'
Check-Dir 'Assets'
Check-File 'build-info.txt'
Check-File 'Start-LegendGame.bat'
Check-File 'Start-Client.bat'
Check-File 'Stop-Local-Servers.bat'

# Forbidden leftovers must not be in a user package.
foreach ($rel in @('.git', '.github', 'CMakeFiles', 'CMakeCache.txt',
                   'testlogs', 'Logs')) {
    $p = Join-Path $DistDir $rel
    if (Test-Path -LiteralPath $p) {
        Write-Host "  FORBIDDEN present in package: $rel"
        $script:errors++
    }
}
# No debug symbols in the user package.
$pdbs = Get-ChildItem -LiteralPath $DistDir -Filter '*.pdb' -Recurse -ErrorAction SilentlyContinue
if ($pdbs -and $pdbs.Count -gt 0) {
    Write-Host "  FORBIDDEN pdb files in package: $($pdbs.Count)"
    $script:errors++
}

if ($errors -gt 0) {
    Fail("Packaging self-verification FAILED with $errors problem(s).")
}

Write-Host "=== PACKAGING VERIFICATION PASS ==="
Write-Host "Package directory: $DistDir"
Get-ChildItem -LiteralPath $DistDir | ForEach-Object { Write-Host "  $($_.Name)" }
