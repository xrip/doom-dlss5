[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Wad,

    [Alias('Output')]
    [string]$OutputDirectory = 'build/cache/manual',

    [switch]$Voxels
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'level-tools.ps1')

try {
    $paths = Get-LevelPaths $Wad $OutputDirectory

    for ($number = 1; $number -le 32; $number++) {
        $map = 'MAP{0:D2}' -f $number
        Write-Host "Converting $map"
        Convert-Level $paths $map -Voxels:$Voxels | Out-Null
    }

    Write-Host "Converted MAP01 through MAP32 to $($paths.Output)"
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
