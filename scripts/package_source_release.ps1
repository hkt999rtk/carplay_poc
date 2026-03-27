param(
    [string]$PackageName = "carplay-poc-source-release"
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

function Get-RelativePathCompat {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BasePath,
        [Parameter(Mandatory = $true)]
        [string]$TargetPath
    )

    $baseUri = New-Object System.Uri(($BasePath.TrimEnd('\') + '\'))
    $targetUri = New-Object System.Uri($TargetPath)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString())
}

function Copy-TreeFiltered {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDir,
        [Parameter(Mandatory = $true)]
        [string]$DestDir
    )

    $excludeDirNames = @(".git", "build", "dist", "tmp", ".local")
    $excludeFileNames = @(".git")
    $excludeFilePatterns = @("*.o", "*.obj", "*.d", "*.su", "*.a", "*.exe", "*.dll", "*.pdb")

    New-Item -ItemType Directory -Force -Path $DestDir | Out-Null

    Get-ChildItem -LiteralPath $SourceDir -Force | ForEach-Object {
        $target = Join-Path $DestDir $_.Name
        if ($_.PSIsContainer) {
            if ($excludeDirNames -contains $_.Name) {
                return
            }
            Copy-TreeFiltered -SourceDir $_.FullName -DestDir $target
            return
        }

        if ($excludeFileNames -contains $_.Name) {
            return
        }

        foreach ($pattern in $excludeFilePatterns) {
            if ($_.Name -like $pattern) {
                return
            }
        }

        Copy-Item -LiteralPath $_.FullName -Destination $target -Force
    }
}

function Write-Checksums {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Paths,
        [Parameter(Mandatory = $true)]
        [string]$OutputPath,
        [Parameter(Mandatory = $true)]
        [string]$BaseDir
    )

    $lines = foreach ($path in $Paths) {
        if (-not (Test-Path $path)) {
            continue
        }
        $hash = (Get-FileHash -Algorithm SHA256 -Path $path).Hash
        $relative = Get-RelativePathCompat -BasePath $BaseDir -TargetPath $path
        "$hash  $relative"
    }
    Set-Content -Path $OutputPath -Value $lines -Encoding ascii
}

$repoRoot = Get-RepoRoot
$distRoot = Join-Path $repoRoot "dist"
$packageRoot = Join-Path $distRoot $PackageName
$zipPath = Join-Path $distRoot ($PackageName + ".zip")
$overlayRoot = Join-Path $packageRoot "firmware_overlay\pristine_20260325_2\sdk-ameba-v5.2g_gcc"
$toolsDest = Join-Path $packageRoot "tools"
$scriptsDest = Join-Path $packageRoot "scripts"

$rootFiles = @(
    ".gitattributes",
    ".gitignore",
    ".gitmodules",
    "AGENTS.md",
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
    "command_list.json"
)

$treeDirs = @(
    "ameba_gateway",
    "docs",
    "gateway",
    "gateway_client",
    "scripts",
    "wsh264",
    "3rd_party\chacha",
    "3rd_party\wsfs"
)

$overlayFiles = @(
    ".scratch\pristine_20260325_2\sdk-ameba-v5.2g_gcc\component\common\example\high_load_memory_use\example_high_load_memory_use.c",
    ".scratch\pristine_20260325_2\sdk-ameba-v5.2g_gcc\project\realtek_amebapro_v0_example\GCC-RELEASE\application.is.mk",
    ".scratch\pristine_20260325_2\sdk-ameba-v5.2g_gcc\project\realtek_amebapro_v0_example\inc\platform_opts.h"
)

$toolFiles = @(
    "tools\sdk-ameba-v5.2g_gcc.tar.gz",
    "tools\amebapro-image-tool-v1.3 1.zip"
)

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
if (Test-Path $packageRoot) {
    Remove-Item -Recurse -Force $packageRoot
}
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
New-Item -ItemType Directory -Force -Path $toolsDest | Out-Null

foreach ($file in $rootFiles) {
    $src = Join-Path $repoRoot $file
    if (Test-Path $src) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $packageRoot $file) -Force
    }
}

foreach ($dir in $treeDirs) {
    $src = Join-Path $repoRoot $dir
    if (Test-Path $src) {
        Copy-TreeFiltered -SourceDir $src -DestDir (Join-Path $packageRoot $dir)
    }
}

foreach ($tool in $toolFiles) {
    $src = Join-Path $repoRoot $tool
    if (Test-Path $src) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $packageRoot $tool) -Force
    }
}

foreach ($overlay in $overlayFiles) {
    $src = Join-Path $repoRoot $overlay
    if (-not (Test-Path $src)) {
        continue
    }
    $relative = $overlay.Substring(".scratch\pristine_20260325_2\sdk-ameba-v5.2g_gcc\".Length)
    $dst = Join-Path $overlayRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

$wifiLocal = Join-Path $repoRoot "ameba_gateway\gateway_wifi_local_config.h"
if (Test-Path $wifiLocal) {
    Copy-Item -LiteralPath $wifiLocal -Destination (Join-Path $packageRoot "ameba_gateway\gateway_wifi_local_config.h") -Force
}

$applyOverlay = @'
param()

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$overlayRoot = Join-Path $repoRoot "firmware_overlay\pristine_20260325_2\sdk-ameba-v5.2g_gcc"
$sdkRoot = Join-Path $repoRoot ".scratch\pristine_20260325_2\sdk-ameba-v5.2g_gcc"

function Get-RelativePathCompat {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BasePath,
        [Parameter(Mandatory = $true)]
        [string]$TargetPath
    )

    $baseUri = New-Object System.Uri(($BasePath.TrimEnd('\') + '\'))
    $targetUri = New-Object System.Uri($TargetPath)
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString())
}

if (-not (Test-Path $overlayRoot)) {
    throw "Missing overlay root: $overlayRoot"
}

if (-not (Test-Path $sdkRoot)) {
    throw "Missing SDK root: $sdkRoot"
}

Get-ChildItem -LiteralPath $overlayRoot -Recurse -File | ForEach-Object {
    $relative = Get-RelativePathCompat -BasePath $overlayRoot -TargetPath $_.FullName
    $dst = Join-Path $sdkRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $dst -Force
}

Write-Host "Applied pristine SDK overlay to $sdkRoot"
'@
Set-Content -Path (Join-Path $scriptsDest "apply_pristine_sdk_overlay.ps1") -Value $applyOverlay -Encoding ascii

$sourceReadme = @'
# Source Release

This package contains the source code and build assets for the Windows host tools and
the Ameba firmware.

## What is included

- Host tool sources:
  - `wsh264/`
  - `gateway/`
  - `gateway_client/`
- Ameba gateway sources:
  - `ameba_gateway/`
- Vendored third-party sources:
  - `3rd_party/chacha/`
  - `3rd_party/wsfs/`
- Documentation:
  - `README.md`
  - `docs/amebapro_sdk_notes.md`
- Build scripts:
  - `scripts/build_ameba_firmware_wsl.sh`
  - `scripts/apply_pristine_sdk_overlay.ps1`
- SDK and flashing tools:
  - `tools/sdk-ameba-v5.2g_gcc.tar.gz`
  - `tools/amebapro-image-tool-v1.3 1.zip`
- Pristine SDK overlay files:
  - `firmware_overlay/pristine_20260325_2/sdk-ameba-v5.2g_gcc/...`

This source release is self-contained; no Git submodule checkout is required.

## Host build on Windows

Install `MSYS2 UCRT64`, then in an `MSYS2 UCRT64` shell run:

~~~bash
cmake --preset windows-ucrt64
cmake --build --preset windows-ucrt64
~~~

That builds:

- `build/windows-ucrt64/wsh264/wsh264.exe`
- `build/windows-ucrt64/bin/gateway.exe`
- `build/windows-ucrt64/bin/gateway_client.exe`

## Firmware build in WSL

The validated firmware workflow is:

- edit `ameba_gateway/gateway_wifi_local_config.h` if you need different Wi-Fi credentials
- extract the SDK archive into `.scratch/pristine_20260325_2/`
- apply the overlay
- build in WSL

From PowerShell:

~~~powershell
New-Item -ItemType Directory -Force -Path .\.scratch\pristine_20260325_2 | Out-Null
tar -xzf .\tools\sdk-ameba-v5.2g_gcc.tar.gz -C .\.scratch\pristine_20260325_2
powershell -ExecutionPolicy Bypass -File .\scripts\apply_pristine_sdk_overlay.ps1
$wslScript = wsl wslpath ((Resolve-Path .\scripts\build_ameba_firmware_wsl.sh).Path)
wsl -d Ubuntu-22.04 -u root -- bash $wslScript
~~~

Notes:

- The WSL build script is the recommended firmware path.
- The script copies the generated `flash_is.bin` to the current Windows user's Desktop by default.
- If the final SDK postbuild stage returns a non-zero code but `application_is/flash_is.bin` exists, the image is usually still usable.

## Flashing the firmware on Windows

1. Extract `tools/amebapro-image-tool-v1.3 1.zip`
2. Run `ImageTool.exe`
3. Put the Ameba board into download mode
4. Click `Browse` and choose the built `flash_is.bin`
5. Verify the SHA256 shown by the tool
6. Click `Download`
7. Wait until the `Download` button becomes enabled again

After a successful download, the board immediately resets and starts the new firmware.

## Current validated demo flow

The validated end-to-end milestone is:

`wsh264 -> websocket -> Ameba gateway -> chacha decrypt -> USB -> gateway_client`

The Windows host and Ameba board must join the same Wi-Fi network for the upstream leg.
'@
Set-Content -Path (Join-Path $packageRoot "README.md") -Value $sourceReadme -Encoding ascii

$checksumTargets = @()
$checksumTargets += Get-ChildItem -LiteralPath $packageRoot -Recurse -File |
    Where-Object { $_.FullName -notlike "*.zip" } |
    Select-Object -ExpandProperty FullName
Write-Checksums -Paths $checksumTargets -OutputPath (Join-Path $packageRoot "checksums.txt") -BaseDir $packageRoot

Compress-Archive -Path $packageRoot -DestinationPath $zipPath -Force

Write-Host "Source package directory: $packageRoot"
Write-Host "Source package zip: $zipPath"
