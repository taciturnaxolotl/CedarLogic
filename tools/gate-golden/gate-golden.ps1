<#
.SYNOPSIS
  Golden / permutation render harness for CedarLogic gate visuals (Workstream G).

.DESCRIPTION
  Renders every library gate through the headless single-gate hook
  (--render-gate[-skia]) across a matrix of rotations and engines, so a change
  to the gate-drawing pipeline can be checked against a captured baseline.

  capture : render the whole matrix into -OutDir.
  compare : diff a -Candidate set against a -Baseline set, per image, using an
            exact hash first and a pixel diff when they differ -- so untouched
            gates (must stay byte-identical) are separated from gates whose
            geometry changed on purpose (smooth curves), which are reported with
            their differing-pixel count and max channel delta for review.

.EXAMPLE
  # Capture the current renderer as the baseline before touching anything:
  ./gate-golden.ps1 -Mode capture -OutDir ../../.gate-golden/baseline

  # After changing the renderer, capture again and compare:
  ./gate-golden.ps1 -Mode capture -OutDir ../../.gate-golden/candidate
  ./gate-golden.ps1 -Mode compare -Baseline ../../.gate-golden/baseline -Candidate ../../.gate-golden/candidate
#>
[CmdletBinding()]
param(
    [ValidateSet("capture", "compare")]
    [string]$Mode = "capture",

    [string]$Exe = "build-skia/Release/CedarLogic.exe",
    [string]$GateDefs = "res/cl_gatedefs.xml",

    [string]$OutDir,        # capture: destination for PNGs
    [string]$Baseline,      # compare: reference set
    [string]$Candidate,     # compare: set under test

    [int[]]$Angles = @(0, 90, 180, 270),
    [ValidateSet("skia", "gl")]
    [string[]]$Engines = @("skia", "gl"),
    [int]$Size = 256,

    [string[]]$Gates,       # optional subset; default = every gate in GateDefs
    [int]$Throttle = 8      # max concurrent render processes (capture)
)

$ErrorActionPreference = "Stop"

# Resolve paths relative to the repo root (two levels up from this script).
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
function Resolve-RepoPath([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    return (Join-Path $repoRoot $p)
}

# Parse the gate names out of cl_gatedefs.xml: each gate opens with
# "<gate> <name>NAME</name>". Hotspots also carry <name>, so match only the one
# that immediately follows a <gate> tag.
function Get-GateNames([string]$xmlPath) {
    $text = Get-Content -Raw -LiteralPath $xmlPath
    $rx = [regex]'<gate>\s*<name>([^<]+)</name>'
    $names = foreach ($m in $rx.Matches($text)) { $m.Groups[1].Value.Trim() }
    return $names
}

# Load a PNG into a flat BGRA byte array plus its dimensions.
function Get-PixelData([string]$path) {
    Add-Type -AssemblyName System.Drawing
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    try {
        $rect = [System.Drawing.Rectangle]::new(0, 0, $bmp.Width, $bmp.Height)
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $bytes = [byte[]]::new($data.Stride * $bmp.Height)
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
            return [pscustomobject]@{ W = $bmp.Width; H = $bmp.Height; Bytes = $bytes }
        }
        finally { $bmp.UnlockBits($data) }
    }
    finally { $bmp.Dispose() }
}

# Count differing pixels and the largest per-channel delta between two PNGs.
function Compare-Pixels([string]$a, [string]$b) {
    $pa = Get-PixelData $a
    $pb = Get-PixelData $b
    if ($pa.W -ne $pb.W -or $pa.H -ne $pb.H) {
        return [pscustomobject]@{ DimMismatch = $true; DiffPixels = -1; MaxDelta = 255 }
    }
    $ba = $pa.Bytes; $bb = $pb.Bytes
    $diffPixels = 0; $maxDelta = 0
    for ($i = 0; $i -lt $ba.Length; $i += 4) {
        $d0 = [math]::Abs($ba[$i] - $bb[$i])
        $d1 = [math]::Abs($ba[$i + 1] - $bb[$i + 1])
        $d2 = [math]::Abs($ba[$i + 2] - $bb[$i + 2])
        $d3 = [math]::Abs($ba[$i + 3] - $bb[$i + 3])
        $m = [math]::Max([math]::Max($d0, $d1), [math]::Max($d2, $d3))
        if ($m -ne 0) {
            $diffPixels++
            if ($m -gt $maxDelta) { $maxDelta = $m }
        }
    }
    return [pscustomobject]@{ DimMismatch = $false; DiffPixels = $diffPixels; MaxDelta = $maxDelta }
}

function Invoke-Capture {
    $exePath = Resolve-RepoPath $Exe
    $xmlPath = Resolve-RepoPath $GateDefs
    if (-not (Test-Path $exePath)) { throw "Executable not found: $exePath" }
    if (-not $OutDir) { throw "-OutDir is required for capture." }
    $dest = Resolve-RepoPath $OutDir
    New-Item -ItemType Directory -Force -Path $dest | Out-Null

    $gateList = if ($Gates) { $Gates } else { Get-GateNames $xmlPath }
    Write-Host "Capturing $($gateList.Count) gates x $($Angles.Count) angles x $($Engines.Count) engines -> $dest"

    # Build the full work matrix.
    $jobs = foreach ($g in $gateList) {
        foreach ($a in $Angles) {
            foreach ($e in $Engines) {
                $flag = if ($e -eq "skia") { "--render-gate-skia" } else { "--render-gate" }
                $file = Join-Path $dest ("{0}__a{1}__{2}.png" -f $g, $a, $e)
                [pscustomobject]@{ Gate = $g; Angle = $a; Engine = $e; Flag = $flag; File = $file }
            }
        }
    }

    $total = $jobs.Count
    $done = 0
    $running = @()
    foreach ($j in $jobs) {
        while ($running.Count -ge $Throttle) {
            $running = @($running | Where-Object { -not $_.HasExited })
            if ($running.Count -ge $Throttle) { Start-Sleep -Milliseconds 40 }
        }
        # Quote any argument containing a space (some gate names -- e.g.
        # "3x8 Decoder Chip" -- and output paths do) so Start-Process passes each
        # as a single argv entry instead of splitting it.
        $argv = @($j.Flag, $j.Gate, $j.Angle, $j.File, $Size, $Size) | ForEach-Object {
            if ("$_" -match '\s') { '"' + $_ + '"' } else { "$_" }
        }
        $p = Start-Process $exePath -PassThru -WindowStyle Hidden -ArgumentList $argv
        $running += $p
        $done++
        if ($done % 25 -eq 0) { Write-Host "  launched $done / $total" }
    }
    # Drain.
    while ($running.Count -gt 0) {
        $running = @($running | Where-Object { -not $_.HasExited })
        if ($running.Count -gt 0) { Start-Sleep -Milliseconds 80 }
    }
    $produced = (Get-ChildItem $dest -Filter *.png).Count
    Write-Host "Capture complete: $produced PNGs in $dest"
}

function Invoke-Compare {
    if (-not $Baseline -or -not $Candidate) { throw "-Baseline and -Candidate are required for compare." }
    $baseDir = Resolve-RepoPath $Baseline
    $candDir = Resolve-RepoPath $Candidate

    $rows = @()
    $identical = 0; $changed = 0; $missing = 0
    foreach ($bf in Get-ChildItem $baseDir -Filter *.png | Sort-Object Name) {
        $cf = Join-Path $candDir $bf.Name
        if (-not (Test-Path $cf)) {
            $missing++
            $rows += [pscustomobject]@{ Image = $bf.Name; Status = "MISSING"; DiffPixels = ""; MaxDelta = "" }
            continue
        }
        $hb = (Get-FileHash $bf.FullName -Algorithm MD5).Hash
        $hc = (Get-FileHash $cf -Algorithm MD5).Hash
        if ($hb -eq $hc) {
            $identical++
            $rows += [pscustomobject]@{ Image = $bf.Name; Status = "IDENTICAL"; DiffPixels = 0; MaxDelta = 0 }
        }
        else {
            $px = Compare-Pixels $bf.FullName $cf
            $changed++
            $status = if ($px.DimMismatch) { "DIM_MISMATCH" } else { "CHANGED" }
            $rows += [pscustomobject]@{ Image = $bf.Name; Status = $status; DiffPixels = $px.DiffPixels; MaxDelta = $px.MaxDelta }
        }
    }

    $reportPath = Join-Path $candDir "compare-report.csv"
    $rows | Export-Csv -NoTypeInformation -Path $reportPath
    Write-Host ""
    Write-Host "=== Gate golden compare ==="
    Write-Host ("  identical : {0}" -f $identical)
    Write-Host ("  changed   : {0}" -f $changed)
    Write-Host ("  missing   : {0}" -f $missing)
    Write-Host "  report    : $reportPath"
    if ($changed -gt 0) {
        Write-Host ""
        Write-Host "Changed images (top 20 by differing pixels):"
        $rows | Where-Object { $_.Status -eq "CHANGED" -or $_.Status -eq "DIM_MISMATCH" } |
            Sort-Object { [int]$_.DiffPixels } -Descending | Select-Object -First 20 |
            Format-Table Image, Status, DiffPixels, MaxDelta -AutoSize
    }
}

switch ($Mode) {
    "capture" { Invoke-Capture }
    "compare" { Invoke-Compare }
}
