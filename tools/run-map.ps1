[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^MAP(0[1-9]|[12][0-9]|3[0-2])$')]
    [string]$Map,

    [Parameter(Mandatory = $true)]
    [string]$Wad,

    [Alias('Output')]
    [string]$OutputDirectory = 'build/cache/manual',

    [switch]$NoMusic,

    [switch]$AllKeys,

    [switch]$Dlss5,

    [switch]$FrameGeneration,

    [switch]$RayLighting,

    [switch]$Fullscreen,

    [switch]$Voxels
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'level-tools.ps1')

try {
    $Map = $Map.ToUpperInvariant()
    $paths = Get-LevelPaths $Wad $OutputDirectory -NeedViewer
    $scenePath = Convert-Level $paths $Map -Voxels:$Voxels

    $viewerArguments = @($scenePath)
    if ($NoMusic) { $viewerArguments += '--no-music' }
    if ($AllKeys) { $viewerArguments += '--all-keys' }
    if ($Dlss5 -or $FrameGeneration) { $viewerArguments += '--dlss5' }
    if ($FrameGeneration) { $viewerArguments += '--fg' }
    if ($RayLighting) { $viewerArguments += '--ray-lighting' }
    if ($Fullscreen) { $viewerArguments += '--fullscreen' }
    if ($Voxels) { $viewerArguments += '--voxels' }
    & $paths.Viewer @viewerArguments
    exit $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
