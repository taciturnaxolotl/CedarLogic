<#
.SYNOPSIS
  Golden render harness for wire ROUTING (Workstream H).

.DESCRIPTION
  Renders every wire-bearing .cdl fixture to a PNG via the headless Skia path,
  so a change to the wire router can be diffed against a captured baseline.
  Renders are byte-deterministic, so any diff is a real geometry/appearance
  change. Diff two capture dirs with the generic comparer in
  tools/gate-golden/gate-golden.ps1 -Mode compare.

.EXAMPLE
  ./wire-golden.ps1 -OutDir ../../.wire-golden/baseline
  # ... change the router ...
  ./wire-golden.ps1 -OutDir ../../.wire-golden/candidate
  ../gate-golden/gate-golden.ps1 -Mode compare -Baseline ../../.wire-golden/baseline -Candidate ../../.wire-golden/candidate
#>
[CmdletBinding()]
param(
    [string]$Exe = "build-skia/Release/CedarLogic.exe",
    [string]$Fixtures = "format/tests/fixtures",
    [string]$OutDir,
    [int]$Width = 1100,
    [int]$Height = 850,
    # Directory containing res/ (pinned so the exe loads a known gate library +
    # font). Defaults to repo root. See the CEDARLOGIC_RESOURCES_DIR note in
    # tools/gate-golden.
    [string]$ResourcesDir
)
$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
function RepoPath([string]$p) { if ([System.IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $repoRoot $p } }

if (-not $OutDir) { throw "-OutDir is required." }
$exePath = RepoPath $Exe
$fixDir  = RepoPath $Fixtures
$dest    = RepoPath $OutDir
if (-not (Test-Path $exePath)) { throw "Executable not found: $exePath" }
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$resDir = if ($ResourcesDir) { RepoPath $ResourcesDir } else { "$repoRoot" }
$env:CEDARLOGIC_RESOURCES_DIR = $resDir
Write-Host "Resources dir: $resDir"

$fixtures = @(Get-ChildItem -LiteralPath $fixDir -File | Where-Object { $_.Extension -ieq '.cdl' } | Sort-Object Name)
Write-Host "Rendering $($fixtures.Count) fixtures -> $dest"
foreach ($f in $fixtures) {
    # A stable output name (spaces -> _) so the comparer matches by filename.
    $stem = $f.BaseName.Replace(' ', '_')
    $out = Join-Path $dest ($stem + '.png')
    $p = Start-Process $exePath -PassThru -WindowStyle Hidden -ArgumentList `
        "--render-skia", ('"' + $f.FullName + '"'), ('"' + $out + '"'), $Width, $Height
    if ($p.WaitForExit(30000)) {
        Write-Host ("  {0,-28} exit={1}" -f $f.Name, $p.ExitCode)
    } else { $p.Kill(); Write-Host ("  {0,-28} TIMEOUT" -f $f.Name) }
}
$n = (Get-ChildItem $dest -Filter *.png).Count
Write-Host "Done: $n PNGs in $dest"
