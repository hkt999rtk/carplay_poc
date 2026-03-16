# carplay_poc

這個 repo 目前主要聚焦在 relay / playback pipeline：

- `wsh264`: WebSocket server，提供控制命令與 ChaCha20 加密後的 audio/video bitstream，預設 port `8081`
- `gateway`: 收到下游 client 後，才向 `wsh264` 建立 upstream websocket 連線，解密後轉成 framed byte stream
- `gateway_client`: 連到 `gateway`，在 host 上直接播放 H.264 video 與 PCM audio

另外 repo 也保留 root-level 的 `tinyhttpd` library 與 `genbin` resource generator，但原本 `ameba/` 內那條 HTTP server / FreeRTOS demo path 已移除，不再是目前支援或維護的執行流程。

完整協議與設計說明在 [ws_server/SPEC.md](ws_server/SPEC.md)。

## 初始化

`ws_server/third_party/chacha` 現在是 submodule，clone 後先做：

```bash
git submodule update --init --recursive
```

## Ameba SDK 檔案整理

- AmebaPro SDK 壓縮檔放在 `tools/`：`tools/sdk-ameba-v5.2g_gcc.tar.gz`
- 只有 gateway 的 Ameba firmware build 需要這份 SDK
- 本機開發前先解壓到 `./.local`

```bash
mkdir -p ./.local
tar -xzf tools/sdk-ameba-v5.2g_gcc.tar.gz -C ./.local
```

- 解壓後 SDK 根目錄應為 `./.local/sdk-ameba-v5.2g_gcc/`
- `./.local/` 已加入 `.gitignore`，不直接進 Git

### macOS 需求

- 安裝 Arm GNU toolchain，預設使用 `/Applications/ARM/bin/arm-none-eabi-gcc`
- 安裝 Docker Desktop，讓 macOS wrapper 代跑 SDK 內的 `elf2bin.linux` / `checksum.linux`
- 安裝 `gawk`（`postbuild.sh` 會用到）
- macOS host 只借用 `arm-none-eabi-gcc` / `g++` / `objcopy` 這類 cross-compiler frontend binary
- `libc.a`、`libm.a`、`libnosys.a`、`libgcc.a` 與其他 target-side runtime / support libs，必須強制使用 SDK bundled toolchain 內那一份，不要混用 host Arm package 自帶的 target libs
- 換句話說：macOS build 的核心原則是「host compiler binary from macOS, target runtime libs from SDK」

```bash
brew install gawk
```

## Build

### Root utilities

root-level 目前只保留 `tinyhttpd` library 與 `genbin` resource generator。建議固定用 out-of-tree build：

```bash
cmake -S . -B build
cmake --build build
```

clean build：

```bash
cmake -S . -B build_clean
cmake --build build_clean
```

主要輸出在 `build*/`：

- `libtinyhttpd.a`
- `genbin`
- `htdocs.bin`
- `htdocs_bin.c`

### WebSocket server / relay / client

```bash
cmake -S ws_server -B ws_server/build
cmake --build ws_server/build
```

- `wsh264`
- `gateway`
- `gateway_client`
- `crypto_proto_tests`

如果是 Linux host，且你只需要 relay，不需要本機播放端，才可以改成只 build `gateway`：

```bash
cmake -S ws_server -B ws_server/build
cmake --build ws_server/build --target gateway
```

這個 `gateway-only` 流程只適用於 Linux host relay build；不代表所有平台或所有開發流程都可以跳過 `gateway_client`。

### Ameba firmware build

```bash
cd ./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE
make clean
make
```

如果是在 macOS，改用 repo 內的 wrapper script：

```bash
./scripts/build_ameba_firmware.sh --macos-host-toolchain
```

如需指定 Arm toolchain 位置：

```bash
ARM_GCC_TOOLCHAIN=/path/to/arm/bin ./scripts/build_ameba_firmware.sh --macos-host-toolchain
```

macOS 下還要特別注意：

- 這個 `ARM_GCC_TOOLCHAIN` 只是在指定 host 上要執行哪個 `arm-none-eabi-gcc`
- 最後 link 時吃進去的 `libc` / `libgcc` / `libnosys` 等 target library，不應該來自 host Arm package，而要強制回到 `./.local/sdk-ameba-v5.2g_gcc/tools/arm-none-eabi-gcc/` 底下 SDK bundled 那份
- 如果這兩邊混用，最容易出現的就是 link-time incompatibility，例如 newlib / startup object / specs 行為和 SDK Makefile 的假設不一致
- `--macos-host-toolchain` 這個 flag 是刻意把 macOS 分流和 Linux 原生 SDK `make` 隔開；Linux 仍然維持原本 `cd .../GCC-RELEASE && make clean && make` 的行為
- macOS wrapper 目前會重用 SDK sample `FW/1115_EVB_WITHOUT_DEVID/flash_is.bin` 的 prefix 來組 `application_is/flash_is.bin`；如需改成別的 sample，可用 `AMEBA_FLASH_TEMPLATE=/path/to/FW/.../flash_is.bin`

這個 firmware build 才依賴 AmebaPro SDK。第一次編譯時會由 SDK Makefile 自動把 bundled Arm GCC 展開到 `./.local/sdk-ameba-v5.2g_gcc/tools/arm-none-eabi-gcc/asdk/linux/`。

要產生可直接燒錄的完整 image，這裡要用 `make`，因為 SDK 會先 build `ram_lp`，再 build `ram_is`；單跑 `make ram_is` 不會先產生 `application_lp/Debug/bin/application_lp.axf`，最後 image packaging 會不完整。

主要輸出檔：

- `./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/flash_is.bin`
- `./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/firmware_is.bin`
- `./.local/sdk-ameba-v5.2g_gcc/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/ota_is.bin`

### macOS burn / download

macOS 這邊目前驗證過可以用 CrossOver 26 啟動 `tools/amebapro-image-tool-v1.3 1.zip` 內的 `ImageTool.exe`。這個 tool 是 Windows `.NET` GUI downloader，不只是 image generator。

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

這個 tool 走的是 UART download，所以真正燒錄前還要把 macOS 的 serial device 映射成 bottle 內的 `COM` port。

先找裝置：

```bash
ls /dev/cu.*
```

正常要看到的會是像 `/dev/cu.usbserial-*`、`/dev/cu.usbmodem*`、`/dev/cu.SLAB_USBtoUART` 這類 USB-UART 節點。只有看到 `Bluetooth-Incoming-Port`、`debug-console` 這種系統裝置時，不要直接映射。

找到正確 UART 後，把它映射進 CrossOver bottle：

```bash
ln -sf /dev/cu.usbserial-XXXX \
  "$HOME/Library/Application Support/CrossOver/Bottles/amebapro-tool/dosdevices/com1"
```

之後再重開 `ImageTool.exe`，在 tool 裡選 `COM1`。

目前已確認：

- CrossOver 26 可以把 `ImageTool.exe` 視窗拉起來
- 如果沒有真正的 USB UART 節點，就沒有東西可映射到 `COM1`
- tool 在 CrossOver/Wine Mono 下仍可能出現部分 GUI / WMI / D3D warning，所以「能啟動」已確認，但「穩定燒錄」仍要看實際 UART driver 與板子連線狀態

gateway/Ameba 端目前的主線是：

- upstream: Wi-Fi websocket 連到 `wsh264`
- downstream: USB vendor bulk
- relay protocol: 沿用 `GWI1/GWP1`

若只是本機除錯，才建議把 Ameba compile-time 切回 TCP downstream。

## 跑 relay pipeline

### 1. 啟動 `wsh264`

```bash
cd ws_server/test_data
../build/wsh264 iphone_baseline.h264
```

### 2. 啟動 `gateway`

```bash
cd ws_server/build
./gateway --listen-port 19000 --upstream-host 127.0.0.1 --upstream-port 8081
```

### 3. 啟動 `gateway_client`

```bash
cd ws_server/build
./gateway_client
```

`gateway_client` 現在預設會用 `libusb` 等待 Ameba USB vendor bulk device (`0x0BDA:0x8195`)，claim interface 後直接讀 `GWI1/GWP1` stream。

如果只是要沿用 host-only 的舊 TCP relay 測試流程，可改用：

```bash
./gateway_client --transport tcp --host 127.0.0.1 --port 19000
```

`gateway` 只有在下游 client 連上後才會連 `wsh264`。同一時間只接受一個下游 client，多餘連線會直接拒絕。

## Tests

```bash
cd ws_server/build
ctest --output-on-failure
```

目前測試包含：

- native ChaCha / packet framing round-trip
- browser-side JavaScript decrypt helper
- `from_scratch` 既有 websocket 測試

## Low-latency behavior

這版設計明確偏向低延遲，不做 AV sync：

- 不做 playout timestamp 排程
- 不做 packet reordering
- `gateway_client` 會偏向保留最新 video packet
- audio queue 過深時會直接丟掉舊資料，避免延遲持續累積

## 專案重點路徑

- `htdocs/`: 最小 browser viewer (`main.html` + websocket/decode/display 所需 JS/wasm)
- `ws_server/wsh264/`: websocket server 與 upstream media source
- `ws_server/gateway/`: relay binary
- `ws_server/gateway_client/`: host playback client
- `ws_server/include/crypto_stream.h`: upstream crypto packet format
- `ws_server/include/gateway_proto.h`: downstream framed relay protocol
