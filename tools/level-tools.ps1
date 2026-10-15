function Resolve-LevelPath([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path (Join-Path $PSScriptRoot '..') $Path
    }
    return [IO.Path]::GetFullPath($Path)
}

function Get-LevelPaths([string]$Wad, [string]$OutputDirectory, [switch]$NeedViewer) {
    $wadPath = Resolve-LevelPath $Wad
    if (-not (Test-Path -LiteralPath $wadPath -PathType Leaf)) {
        throw "WAD file not found: $wadPath"
    }
    $converter = Resolve-LevelPath 'build/wad-convert.exe'
    $viewer = Resolve-LevelPath 'build/doom-raylib.exe'
    if (-not (Test-Path -LiteralPath $converter -PathType Leaf) -or
        ($NeedViewer -and -not (Test-Path -LiteralPath $viewer -PathType Leaf))) {
        throw 'Build the tools first: cmake --preset clang; cmake --build --preset clang'
    }
    $outputPath = Resolve-LevelPath $OutputDirectory
    if (Test-Path -LiteralPath $outputPath) {
        if (-not (Test-Path -LiteralPath $outputPath -PathType Container)) {
            throw "Output path is not a directory: $outputPath"
        }
    } else {
        New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
    }
    return [pscustomobject]@{Wad = $wadPath; Output = $outputPath; Converter = $converter; Viewer = $viewer}
}

function Convert-Level($Paths, [string]$Map, [switch]$Voxels) {
    $scenePath = Join-Path $Paths.Output "$Map.drmap"
    $musicPath = Join-Path $Paths.Output "$Map.mid"
    $arguments = @($Paths.Wad, $scenePath, $Map)
    if ($Voxels) {
        $arguments += @('--voxels', (Resolve-LevelPath 'voxels'),
            '--voxeldef', (Resolve-LevelPath 'VOXELDEF1.txt'), '--voxeldef', (Resolve-LevelPath 'voxeldef.txt'))
    }
    & $Paths.Converter @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "wad-convert failed for $Map with exit code $LASTEXITCODE"
    }
    foreach ($file in @($scenePath, $musicPath)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "wad-convert did not write $file"
        }
    }
    if ($Voxels -and -not (Test-Path -LiteralPath (Join-Path $Paths.Output "$Map.drvox") -PathType Leaf)) {
        throw "wad-convert did not write $Map.drvox"
    }
    return $scenePath
}
