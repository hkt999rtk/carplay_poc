param(
    [string]$BuildPreset = "windows-ucrt64",
    [string]$PackageName = "windows-demo-package",
    [string]$MsysRoot = "C:\msys64"
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

function Convert-MsysPathToWindows {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PathText,
        [Parameter(Mandatory = $true)]
        [string]$MsysRootPath
    )

    if ($PathText -match '^/ucrt64/(.+)$') {
        return Join-Path $MsysRootPath ("ucrt64\" + ($Matches[1] -replace '/', '\'))
    }

    if ($PathText -match '^/clang64/(.+)$') {
        return Join-Path $MsysRootPath ("clang64\" + ($Matches[1] -replace '/', '\'))
    }

    if ($PathText -match '^/mingw64/(.+)$') {
        return Join-Path $MsysRootPath ("mingw64\" + ($Matches[1] -replace '/', '\'))
    }

    if ($PathText -match '^/([a-zA-Z])/(.+)$') {
        return ($Matches[1].ToUpper() + ":\\" + ($Matches[2] -replace '/', '\'))
    }

    return $null
}

function Get-UcrtDllDependencies {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$ExecutablePaths,
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,
        [Parameter(Mandatory = $true)]
        [string]$MsysRootPath
    )

    $bash = Join-Path $MsysRootPath "usr\bin\bash.exe"
    if (-not (Test-Path $bash)) {
        throw "MSYS2 bash not found at $bash"
    }

    $dlls = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($exe in $ExecutablePaths) {
        if (-not (Test-Path $exe)) {
            throw "Executable not found: $exe"
        }

        $relativeExe = Get-RelativePathCompat -BasePath $RepoRoot -TargetPath $exe
        $relativeExe = $relativeExe -replace '\\', '/'
        $lddOutput = & $bash -lc "cd '$($RepoRoot -replace '\\', '/')' && export PATH=/ucrt64/bin:`$PATH && ldd '$relativeExe'"
        foreach ($line in $lddOutput) {
            if ($line -match '=>\s+(/[^ ]+\.dll)\s+\(') {
                $dllPath = Convert-MsysPathToWindows -PathText $Matches[1] -MsysRootPath $MsysRootPath
                if ($null -ne $dllPath -and $dllPath.StartsWith((Join-Path $MsysRootPath "ucrt64"), [System.StringComparison]::OrdinalIgnoreCase)) {
                    [void]$dlls.Add($dllPath)
                }
            }
        }
    }

    return @($dlls)
}

$repoRoot = Get-RepoRoot
$distRoot = Join-Path $repoRoot "dist"
$packageRoot = Join-Path $distRoot $PackageName
$zipPath = Join-Path $distRoot ($PackageName + ".zip")
$binDir = Join-Path $packageRoot "bin"
$testDataSource = Join-Path $repoRoot "wsh264\test_data"
$testDataDest = Join-Path $packageRoot "test_data"
$configSource = Join-Path $repoRoot "wsh264\config.json"
$readmePath = Join-Path $packageRoot "README.md"

$executables = @(
    (Join-Path $repoRoot ("build\" + $BuildPreset + "\wsh264\wsh264.exe")),
    (Join-Path $repoRoot ("build\" + $BuildPreset + "\bin\gateway.exe")),
    (Join-Path $repoRoot ("build\" + $BuildPreset + "\bin\gateway_client.exe"))
)

foreach ($exe in $executables) {
    if (-not (Test-Path $exe)) {
        throw "Required executable is missing: $exe"
    }
}

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
if (Test-Path $packageRoot) {
    Remove-Item -Recurse -Force $packageRoot
}
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}

New-Item -ItemType Directory -Force -Path $binDir | Out-Null
Copy-Item -Path $executables -Destination $binDir

if (Test-Path $configSource) {
    Copy-Item -Path $configSource -Destination $packageRoot
}

if (-not (Test-Path $testDataSource)) {
    throw "Test data directory not found: $testDataSource"
}
Copy-Item -Recurse -Force -Path $testDataSource -Destination $testDataDest

$dlls = Get-UcrtDllDependencies -ExecutablePaths $executables -RepoRoot $repoRoot -MsysRootPath $MsysRoot
foreach ($dll in $dlls) {
    Copy-Item -Path $dll -Destination $binDir
}

$readme = @'
# Windows Demo Package

This package contains the Windows demo binaries for the host-side video pipeline:

- bin\wsh264.exe
- bin\gateway.exe
- bin\gateway_client.exe

It also includes the runtime DLLs required by those executables, the default config.json,
and the test media files under test_data\.

## Folder layout

- bin\ - executables and dependent DLLs
- test_data\ - H.264 and audio test files
- config.json - default wsh264 configuration

## Quick start

Open three PowerShell windows in this package directory.

### 1. Start wsh264

~~~powershell
.\bin\wsh264.exe .\test_data\iphone_baseline.h264
~~~

### 2. Start gateway

~~~powershell
.\bin\gateway.exe --listen-port 19000 --upstream-host 127.0.0.1 --upstream-port 8081
~~~

### 3. Start gateway_client

~~~powershell
.\bin\gateway_client.exe --transport tcp --host 127.0.0.1 --port 19000
~~~

If the pipeline is working, gateway_client should connect to gateway and display decoded
H.264 video on screen.

## USB testing on Windows

If you want to test `gateway_client.exe --transport usb`, Windows should use the
generic `WinUSB` driver for the Ameba device instead of the default mass-storage
binding.

Recommended steps:

1. Download `Zadig` from:
   - https://zadig.akeo.ie/
2. Open `Zadig`
3. In Zadig, open `Options -> List All Devices`
4. Select the Ameba device shown as one of:
   - `USB Mass Storage`
   - `VID 0BDA / PID 8195`
5. Choose `WinUSB` as the target driver
6. Click `Install Driver` or `Replace Driver`
7. Replug or reset the device if Windows does not refresh automatically

After `WinUSB` is installed, you can run a simple USB bulk ping/pong test:

~~~powershell
.\bin\gateway_client.exe --transport usb --usb-ping --usb-ping-count 10
~~~

## Notes

- gateway_client is intended to be tested with --transport tcp in this package.
- USB testing requires `WinUSB` to be installed for the `VID 0BDA / PID 8195` device.
- wsh264 looks for sound.raw next to the selected .h264 file, so keep the media files
  together inside test_data\.
- System DLLs from Windows are not bundled; MSYS2 UCRT64 runtime DLLs required by the build
  are already included in bin\.
'@

Set-Content -Path $readmePath -Value $readme -Encoding ascii

Compress-Archive -Path $packageRoot -DestinationPath $zipPath -Force

Write-Host "Package directory: $packageRoot"
Write-Host "Package zip: $zipPath"
Write-Host "Executables:"
foreach ($exe in $executables) {
    Write-Host ("  - " + [System.IO.Path]::GetFileName($exe))
}
Write-Host "Copied DLL count: $($dlls.Count)"
