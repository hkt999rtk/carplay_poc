# tinyhttpd / carplay_poc

這個 repo 目前包含兩條主要服務：

- `amebatest`: HTTP server，提供 `htdocs/` 前端頁面，預設 port `9090`
- `wsh264`: WebSocket server，提供控制命令與 ChaCha20 加密後的 audio/video bitstream，預設 port `8081`

另外在 `ws_server/` 內新增了兩個本機測試 binary：

- `gateway`: 收到下游 client 後，才向 `wsh264` 建立 upstream websocket 連線，解密後轉成 framed byte stream
- `gateway_client`: 連到 `gateway`，在 host 上直接播放 H.264 video 與 PCM audio

完整協議與設計說明在 [ws_server/SPEC.md](/Users/kevinhuang/work/carplay_poc/ws_server/SPEC.md)。

## 初始化

`ws_server/third_party/chacha` 現在是 submodule，clone 後先做：

```bash
git submodule update --init --recursive
```

## Build

### HTTP server

```bash
cd build
cmake ..
make
```

### WebSocket server / relay / client

```bash
cd ws_server/build
cmake ..
make
```

這會編出：

- `wsh264`
- `gateway`
- `gateway_client`
- `crypto_proto_tests`

## 直接跑 browser

先啟動 `wsh264`：

```bash
cd ws_server/test_data
../build/wsh264 iphone_baseline.h264
```

再啟動 HTTP server：

```bash
cd build
./amebatest
```

瀏覽器打開：

```text
http://127.0.0.1:9090/main.html
```

`main.html` 是精簡過的 viewer，只保留 websocket 連線、ChaCha20 解密、H.264 顯示與 PCM audio 播放。
browser 會直接連到 `ws://127.0.0.1:8081`，先收 `crypto_init`，再由 JavaScript 解密 binary media packet 並播放。

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
./gateway_client --host 127.0.0.1 --port 19000
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
