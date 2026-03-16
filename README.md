# carplay_poc

這個 repo 現在保留四塊：

- `wsh264/`: host 端 WebSocket H.264/audio source
- `gateway/`: host relay，接 `wsh264` upstream 並輸出解密後的 framed stream
- `gateway_client/`: host player，接 `gateway` downstream 並用 FFmpeg + SDL 播放
- AmebaPro firmware build 與 macOS 燒錄流程

舊的 `httpd` / `tinyhttpd` / `genbin` / `htdocs` / `ministd` / `ws_server` 已移除。

## 初始化

`3rd_party/chacha` 是 submodule，clone 後先做：

```bash
git submodule update --init --recursive
```

## Build layout

這個 repo 的 build 現在分成兩條：

- host tools: `wsh264`、`gateway`、`gateway_client`，統一走根目錄 CMake
- Ameba firmware: 走 SDK 內建 GNU Make，macOS 再由 wrapper script 幫忙處理

Host build 不再靠不同 host 各自維護一套 Makefile，而是共用同一組
`CMakeLists.txt`，再用少量 `WIN32` / `_WIN32` 分支處理平台差異。

根目錄也提供了 [`CMakePresets.json`](/C:/Users/hkt99/work/carplay_poc/CMakePresets.json)，
目前預留三個 host preset：

- `linux-debug`
- `macos-debug`
- `windows-ucrt64`

## Host build quick start

macOS / Linux 先確認 `pkg-config` 能找到 `gateway_client` 需要的套件。

macOS:

```bash
brew install pkg-config sdl2 ffmpeg libusb
```

Linux:

```bash
sudo apt install pkg-config libsdl2-dev libavcodec-dev libavutil-dev libswscale-dev libusb-1.0-0-dev ninja-build cmake
```

整套 host binaries 用 preset 建置：

macOS:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
```

Linux:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
```

如果只想編單一 target：

```bash
cmake --build --preset macos-debug --target wsh264
cmake --build --preset macos-debug --target gateway
cmake --build --preset macos-debug --target gateway_client
```

執行檔會放在 `build/<preset>/...`，例如：

- `build/macos-debug/wsh264/wsh264`
- `build/macos-debug/bin/gateway`
- `build/macos-debug/bin/gateway_client`
- `build/windows-ucrt64/wsh264/wsh264.exe`
- `build/windows-ucrt64/bin/gateway.exe`
- `build/windows-ucrt64/bin/gateway_client.exe`

## Windows host build

Windows 目前建議走 `MSYS2 UCRT64`，不要直接用原生 MSVC。這樣可以沿用
`pkg-config`、FFmpeg、SDL2、`libusb` 與大部分 POSIX 相容層。

先安裝 [MSYS2](https://www.msys2.org/)，然後開 `MSYS2 UCRT64` shell 更新並安裝套件：

```bash
pacman -Suy
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-SDL2 \
  mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-libusb
```

在 `MSYS2 UCRT64` shell 內建置：

```bash
git submodule update --init --recursive
cmake --preset windows-ucrt64
cmake --build --preset windows-ucrt64
```

目前已驗證可在 Windows 上建置的 target：

- `wsh264`
- `gateway`
- `gateway_client`

如果要從一般 PowerShell 或 `cmd.exe` 直接執行，而不是從 `MSYS2 UCRT64` shell
內啟動，請把對應 DLL 一起放在執行檔旁，或把 `C:\msys64\ucrt64\bin` 加進
`PATH`。

Windows build 預設輸出目錄是：

- `build/windows-ucrt64/wsh264/wsh264.exe`
- `build/windows-ucrt64/bin/gateway.exe`
- `build/windows-ucrt64/bin/gateway_client.exe`

如果要在 Windows 上使用 `gateway_client --transport usb`，除了 build 時安裝
`libusb` 套件，還要確認目標 USB 裝置已綁定到 `WinUSB` 或其他 `libusb`
可用的 driver；否則 `libusb` 可能無法成功開啟裝置。

## Demo flow

這個 repo 目前驗證過的 host demo flow 是：

1. `wsh264` 當 video/audio source，開 WebSocket upstream 在 `127.0.0.1:8081`
2. `gateway` 連到 `wsh264`，把解密後的 framed stream 轉送到 `127.0.0.1:19000`
3. `gateway_client` 用 `--transport tcp` 連到 `gateway`，解 H.264 並顯示畫面

Windows 上建議直接照下面順序，在三個不同 terminal 視窗執行。

先啟動 `wsh264`：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
.\build\windows-ucrt64\wsh264\wsh264.exe wsh264\test_data\iphone_baseline.h264
```

再啟動 `gateway`：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
.\build\windows-ucrt64\bin\gateway.exe --listen-port 19000 --upstream-host 127.0.0.1 --upstream-port 8081
```

最後啟動 `gateway_client`，這裡 demo 一律使用 TCP，不走 USB：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
.\build\windows-ucrt64\bin\gateway_client.exe --transport tcp --host 127.0.0.1 --port 19000
```

如果畫面正常顯示，代表整條 `wsh264 -> gateway -> gateway_client` video pipe 已經打通。

## `wsh264`

`wsh264` 會啟動 WebSocket server，預設 port `8081`，並送出加密後的 video/audio stream。

建置：

```bash
cmake --build --preset macos-debug --target wsh264
```

如果你目前不是用 `macos-debug`，把上面的 preset 名稱換成 `linux-debug` 或
`windows-ucrt64` 即可。

也可以獨立建 `wsh264` 子專案，不經過根目錄 preset：

```bash
cmake -S wsh264 -B wsh264/build
cmake --build wsh264/build --target wsh264
```

執行：

```bash
./build/macos-debug/wsh264/wsh264 wsh264/test_data/iphone_baseline.h264
```

注意：

- `wsh264` 會自動從輸入 H.264 檔所在目錄找同層 `sound.raw`
- `wsh264` 目前相關 code 都在 `wsh264/` 目錄下
- `wsh264/config.json` 仍是目前保留的預設設定檔

## `gateway`

`gateway` 是 host relay，預設 listen `19000`，並連到 `wsh264` 的 `127.0.0.1:8081` upstream。

建置：

```bash
cmake --build --preset macos-debug --target gateway
```

執行：

```bash
./build/macos-debug/bin/gateway --listen-port 19000 --upstream-host 127.0.0.1 --upstream-port 8081
```

## `gateway_client`

`gateway_client` 是 host 端播放 client，連 `gateway` 的 downstream framed stream，自己解 H.264/PCM，並用 SDL 顯示 video 和播放 audio。支援：

- `--transport tcp`
- `--transport usb`

建置：

```bash
cmake --build --preset macos-debug --target gateway_client
```

執行：

```bash
./build/macos-debug/bin/gateway_client --transport tcp --host 127.0.0.1 --port 19000
```

或 USB：

```bash
./build/macos-debug/bin/gateway_client --transport usb --usb-vid 0x0BDA --usb-pid 0x8195
```

`gateway_client` 需要 `pkg-config` 可以找到：

- `sdl2`
- `libavcodec`
- `libavutil`
- `libswscale`
- `libusb-1.0`

## Ameba SDK 檔案整理

- AmebaPro SDK 壓縮檔放在 `tools/`：`tools/sdk-ameba-v5.2g_gcc.tar.gz`
- 本機開發前先解壓到 `./.local`

```bash
mkdir -p ./.local
tar -xzf tools/sdk-ameba-v5.2g_gcc.tar.gz -C ./.local
```

- 解壓後 SDK 根目錄應為 `./.local/sdk-ameba-v5.2g_gcc/`
- `./.local/` 已加入 `.gitignore`，不直接進 Git

## macOS 需求

- 安裝 Arm GNU toolchain，預設使用 `/Applications/ARM/bin/arm-none-eabi-gcc`
- 安裝 Docker Desktop，讓 macOS wrapper 代跑 SDK 內的 `elf2bin.linux` / `checksum.linux`
- 安裝 `gawk`（`postbuild.sh` 會用到）

```bash
brew install gawk
```

macOS host 只借用 `arm-none-eabi-gcc` / `g++` / `objcopy` 這類 cross-compiler frontend binary。
`libc.a`、`libm.a`、`libnosys.a`、`libgcc.a` 與其他 target-side runtime / support libs，必須強制使用 SDK bundled toolchain 內那一份，不要混用 host Arm package 自帶的 target libs。

## Build Firmware

Linux host 這一段是在 build AmebaPro firmware image，不是 build Linux executable。直接走 SDK 原生流程：

```bash
cd ./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE
make clean
make
```

這個 Linux host build 會先產生 `application_lp.axf`，再進一步包出 `application_is/flash_is.bin`。如果目標是可直接燒錄的主 image，主要看的是 `application_is/flash_is.bin`。

macOS 改用 repo 內的 wrapper script：

```bash
./scripts/build_ameba_firmware.sh --macos-host-toolchain
```

如需指定 Arm toolchain 位置：

```bash
ARM_GCC_TOOLCHAIN=/path/to/arm/bin ./scripts/build_ameba_firmware.sh --macos-host-toolchain
```

macOS 下還要特別注意：

- `ARM_GCC_TOOLCHAIN` 只是在指定 host 上要執行哪個 `arm-none-eabi-gcc`
- 最後 link 時吃進去的 `libc` / `libgcc` / `libnosys` 等 target library，不應該來自 host Arm package，而要強制回到 SDK bundled toolchain
- `--macos-host-toolchain` 是刻意把 macOS 分流和 Linux 原生 SDK `make` 隔開；Linux 仍然維持原本 `cd .../GCC-RELEASE && make clean && make`
- macOS wrapper 目前會重用 SDK sample `FW/1115_EVB_WITHOUT_DEVID/flash_is.bin` 的 prefix 來組 `application_is/flash_is.bin`；如需改成別的 sample，可用 `AMEBA_FLASH_TEMPLATE=/path/to/FW/.../flash_is.bin`

主要輸出檔：

- `./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/flash_is.bin`
- `./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/firmware_is.bin`
- `./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/ota_is.bin`

目前 Linux host 上，SDK 的 `make` 最後仍可能在 `otamerge` / `postbuild` 階段因既有 segfault 以 non-zero exit 結束；這不影響前面已經產生的 `flash_is.bin` 與 `firmware_is.bin`。如果目的只是產出可燒錄的主 image，這個 non-zero exit 可以暫時 waive，並以 `application_is/flash_is.bin` 作為主要燒錄檔。

## macOS Burn / Download

macOS 這邊目前驗證過可以用 CrossOver 26 啟動 `tools/amebapro-image-tool-v1.3 1.zip` 內的 `ImageTool.exe`。

建議先把 zip 解到本機目錄：

```bash
mkdir -p "$HOME/.local/amebapro-image-tool"
unzip -oq "tools/amebapro-image-tool-v1.3 1.zip" -d "$HOME/.local/amebapro-image-tool"
```

如果還沒有專用 bottle，可先建一個：

```bash
"/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/cxbottle" \
  --bottle amebapro-tool \
  --create
```

啟動 downloader：

```bash
"/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/cxstart" \
  --bottle amebapro-tool \
  --no-wait \
  "$HOME/.local/amebapro-image-tool/amebapro-image-tool-v1.3/ImageTool.exe"
```

找到正確 UART 後，把它映射進 CrossOver bottle：

```bash
ln -sf /dev/cu.usbserial-XXXX \
  "$HOME/Library/Application Support/CrossOver/Bottles/amebapro-tool/dosdevices/com1"
```
