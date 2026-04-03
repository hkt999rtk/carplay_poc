param(
    [string]$BuildPreset = "windows-ucrt64",
    [string]$PackageName = "carplay-poc-windows-binary-release",
    [string]$DateStamp = "",
    [string]$MsysRoot = "C:\msys64",
    [string]$FirmwareImage = ""
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
        $relativeExe = Get-RelativePathCompat -BasePath $RepoRoot -TargetPath $exe
        $relativeExe = $relativeExe -replace '\\', '/'
        $lddOutput = & $bash -lc "cd '$($RepoRoot -replace '\\', '/')' && export PATH=/ucrt64/bin:`$PATH && ldd '$relativeExe'"
        foreach ($line in $lddOutput) {
            if ($line -match '=>\s+(/[^ ]+\.dll)\s+\(') {
                $dllPath = Convert-MsysPathToWindows -PathText $Matches[1] -MsysRootPath $MsysRootPath
                if ($null -ne $dllPath -and
                    $dllPath.StartsWith((Join-Path $MsysRootPath "ucrt64"), [System.StringComparison]::OrdinalIgnoreCase)) {
                    [void]$dlls.Add($dllPath)
                }
            }
        }
    }

    return @($dlls)
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
if ([string]::IsNullOrWhiteSpace($DateStamp)) {
    $DateStamp = Get-Date -Format "yyyyMMdd"
}
if ([string]::IsNullOrWhiteSpace($PackageName)) {
    throw "PackageName must not be empty"
}
$archiveBaseName = "$PackageName-$DateStamp"
if ([string]::IsNullOrWhiteSpace($FirmwareImage)) {
    $desktop = [Environment]::GetFolderPath("Desktop")
    $FirmwareImage = Join-Path $desktop "flash_is.bin"
}

$distRoot = Join-Path $repoRoot "dist"
$packageRoot = Join-Path $distRoot $archiveBaseName
$zipPath = Join-Path $distRoot ($archiveBaseName + ".zip")
$hostBinDir = Join-Path $packageRoot "host\bin"
$hostTestDataDest = Join-Path $packageRoot "host\test_data"
$firmwareDir = Join-Path $packageRoot "firmware"
$toolsDir = Join-Path $packageRoot "tools"

$executables = @(
    (Join-Path $repoRoot ("build\" + $BuildPreset + "\wsh264\wsh264.exe")),
    (Join-Path $repoRoot ("build\" + $BuildPreset + "\bin\gateway.exe")),
    (Join-Path $repoRoot ("build\" + $BuildPreset + "\bin\gateway_client.exe"))
)

foreach ($exe in $executables) {
    if (-not (Test-Path $exe)) {
        throw "Missing executable: $exe"
    }
}

if (-not (Test-Path $FirmwareImage)) {
    throw "Missing firmware image: $FirmwareImage"
}

New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
if (Test-Path $packageRoot) {
    Remove-Item -Recurse -Force $packageRoot
}
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}

New-Item -ItemType Directory -Force -Path $hostBinDir, $hostTestDataDest, $firmwareDir, $toolsDir | Out-Null
Copy-Item -Path $executables -Destination $hostBinDir

$configSource = Join-Path $repoRoot "wsh264\config.json"
if (Test-Path $configSource) {
    Copy-Item -LiteralPath $configSource -Destination (Join-Path $packageRoot "host\config.json") -Force
}

$testDataSource = Join-Path $repoRoot "wsh264\test_data"
Copy-Item -Recurse -Force -Path (Join-Path $testDataSource "*") -Destination $hostTestDataDest

$dlls = Get-UcrtDllDependencies -ExecutablePaths $executables -RepoRoot $repoRoot -MsysRootPath $MsysRoot
foreach ($dll in $dlls) {
    Copy-Item -LiteralPath $dll -Destination $hostBinDir -Force
}

$firmwareDest = Join-Path $firmwareDir "flash_is.bin"
Copy-Item -LiteralPath $FirmwareImage -Destination $firmwareDest -Force

$imageToolZip = Join-Path $repoRoot "tools\amebapro-image-tool-v1.3 1.zip"
if (Test-Path $imageToolZip) {
    Copy-Item -LiteralPath $imageToolZip -Destination (Join-Path $toolsDir "amebapro-image-tool-v1.3 1.zip") -Force
}

$firmwareHash = (Get-FileHash -Algorithm SHA256 -Path $firmwareDest).Hash

$readmeLines = @(
    '# Windows Binary Release',
    '',
    'This package contains the validated Windows-side test binaries plus the latest Ameba',
    'firmware image.',
    '',
    '## Contents',
    '',
    '- `host/bin/wsh264.exe`',
    '- `host/bin/gateway_client.exe`',
    '- `host/bin/gateway.exe`',
    '- required MSYS2 UCRT64 runtime DLLs',
    '- `host/test_data/...`',
    '- `host/config.json`',
    '- `firmware/flash_is.bin`',
    '- `tools/amebapro-image-tool-v1.3 1.zip`',
    '- `checksums.txt`',
    '',
    '## Firmware image',
    '',
    '- File: `firmware/flash_is.bin`',
    ('- SHA256: `{0}`' -f $firmwareHash),
    '',
    'The current test firmware was validated with the Ameba board on the same AP as the',
    'Windows host and with the Ameba acting as the Wi-Fi/WebSocket/USB gateway.',
    '',
    '## Flashing the board on Windows',
    '',
    '1. Extract `tools/amebapro-image-tool-v1.3 1.zip`',
    '2. Run `ImageTool.exe`',
    '3. Put the board into download mode',
    '4. Click `Browse` and choose `firmware/flash_is.bin`',
    '5. Confirm the SHA256 shown by the tool matches the value above',
    '6. Click `Download`',
    '7. Wait until the `Download` button becomes enabled again',
    '',
    'After a successful download, the board immediately resets and starts the new firmware.',
    '',
    '## USB driver on Windows',
    '',
    'The validated USB path uses `WinUSB` for the Ameba device.',
    '',
    '1. Download Zadig from `https://zadig.akeo.ie/`',
    '2. Open Zadig and enable `Options -> List All Devices`',
    '3. Select the Ameba device (`AmebaPro Gateway`, or the matching `VID 0BDA PID 8195` entry)',
    '4. Choose `WinUSB` as the target driver',
    '5. Click `Install Driver` or `Replace Driver`',
    '',
    'Without the `WinUSB` driver, `gateway_client.exe --transport usb` will not be able to open the device.',
    '',
    '## Host-side demo flow',
    '',
    '`wsh264 -> websocket -> Ameba gateway -> chacha decrypt -> USB -> gateway_client`',
    '',
    'Make sure the Windows host and the Ameba board are on the same Wi-Fi access point.',
    '',
    'Open two PowerShell windows in this package directory.',
    '',
    '### 1. Start `wsh264`',
    '',
    '~~~powershell',
    '.\host\bin\wsh264.exe .\host\test_data\iphone_baseline.h264',
    '~~~',
    '',
    '### 2. Start `gateway_client`',
    '',
    '~~~powershell',
    '.\host\bin\gateway_client.exe --transport usb',
    '~~~',
    '',
    'If everything is working:',
    '',
    '- `wsh264` listens on TCP/WS port `8081`',
    '- the Ameba firmware connects upstream over Wi-Fi',
    '- the Windows client receives the decrypted relay over USB',
    '- video appears in the `gateway_client` window',
    '',
    '## Notes',
    '',
    '- `gateway.exe` is included for completeness, but it is not required for the validated',
    '  Ameba USB relay demo.',
    '- If you need to change Wi-Fi credentials or firmware behavior, use the source release',
    '  package and rebuild the Ameba image in WSL.'
)

Set-Content -Path (Join-Path $packageRoot "README.md") -Value $readmeLines -Encoding ascii

$checksumTargets = @()
$checksumTargets += Get-ChildItem -LiteralPath $packageRoot -Recurse -File | Select-Object -ExpandProperty FullName
Write-Checksums -Paths $checksumTargets -OutputPath (Join-Path $packageRoot "checksums.txt") -BaseDir $packageRoot

Compress-Archive -Path $packageRoot -DestinationPath $zipPath -Force

Write-Host "Binary package directory: $packageRoot"
Write-Host "Binary package zip: $zipPath"
