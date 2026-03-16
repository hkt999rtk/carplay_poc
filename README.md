# carplay_poc

這個 repo 現在保留兩塊：

- `wsh264/`: host 端 WebSocket H.264/audio source
- AmebaPro firmware build 與 macOS 燒錄流程

舊的 `httpd` / `tinyhttpd` / `genbin` / `htdocs` / `gateway` / `gateway_client` / `ministd` / `ws_server` 已移除。

## 初始化

`3rd_party/chacha` 是 submodule，clone 後先做：

```bash
git submodule update --init --recursive
```

## `wsh264`

`wsh264` 會啟動 WebSocket server，預設 port `8081`，並送出加密後的 video/audio stream。

建置：

```bash
cmake -S . -B build
cmake --build build --target wsh264
```

也可以獨立建 `wsh264` 子專案：

```bash
cmake -S wsh264 -B wsh264/build
cmake --build wsh264/build --target wsh264
```

執行：

```bash
./build/wsh264 wsh264/test_data/iphone_baseline.h264
```

注意：

- `wsh264` 會自動從輸入 H.264 檔所在目錄找同層 `sound.raw`
- `wsh264` 目前相關 code 都在 `wsh264/` 目錄下
- `wsh264/config.json` 仍是目前保留的預設設定檔

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

Linux 直接走 SDK 原生流程：

```bash
cd ./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE
make clean
make
```

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
