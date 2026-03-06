# tinyhttpd

`tinyhttpd` 是一個用於嵌入式 AI Demo 的小型伺服器專案，包含兩個必需服務：

- HTTP 服務：提供 Web UI 與靜態資源（`amebatest`）
- WebSocket 服務：提供 H.264/AI 事件串流與控制命令（`wsh264`）

前端頁面在 `htdocs/`，主要透過 `ws://<host>:8081` 與後端互動。

## 快速開始

`ws_server/` 已經直接放在 repo 內，不需要額外初始化 submodule。

### 1) 編譯 HTTP 服務 (`amebatest`)

```bash
cd build
cmake ..
make
```

### 2) 編譯 WebSocket 服務 (`wsh264`)

```bash
cd ws_server/build
cmake ..
make
```

### 3) 啟動服務

需要同時啟動兩個服務：

1. 啟動 WebSocket 服務（port `8081`）  
   `wsh264` 需要一個 H.264 bitstream 檔案參數。

```bash
cd ws_server/build
./wsh264 ameba_pro_640_360.h264
# 或使用測試資料
# ./wsh264 ../test_data/iphone_baseline.h264
```

2. 啟動 HTTP 服務（port `9090`）

```bash
cd build
./amebatest
```

3. 開啟瀏覽器

```text
http://localhost:9090
```

## 測試流程 (Smoke Test)

以下流程用來快速確認「HTTP 路由 / WebSocket / 前端解碼」都正常：

### 1) 驗證 HTTP 路由

```bash
curl -s -D - http://127.0.0.1:9090/ -o /dev/null | sed -n '1,8p'
curl -s -D - http://127.0.0.1:9090/main.html -o /dev/null | sed -n '1,8p'
```

預期：
- `GET /` 回 `302 Found`，且 `Location: /main.html`
- `GET /main.html` 回 `200 OK`

### 2) 驗證 WebSocket 服務有在 listening

```bash
lsof -iTCP:8081 -sTCP:LISTEN -n -P
```

預期：可看到 `wsh264` listening 在 `*:8081`。

### 3) 驗證前端是否成功連線並解碼

1. 開啟瀏覽器進入 `http://127.0.0.1:9090/main.html`
2. 打開 DevTools Console
3. 觀察以下訊息

預期：
- 若支援原生解碼：`WebCodecs enabled, codec = ...`
- 若不支援：會自動 fallback 到 WASM 解碼

## 主要模組

- `minihttpd.cpp` / `minihttpd.h`
  - tiny HTTP server 核心
  - callback 路由機制
  - 多 worker thread 處理連線

- `genbin/`
  - 將 `htdocs/` 打包成 `htdocs.bin`，再轉成 `ameba/htdocs_bin.c`
  - 文字類型資源（html/css/js/wasm/map）會 gzip 後嵌入

- `ameba/`
  - 以 `htdocs_bin.c` 註冊靜態路由
  - `/` 會 302 到 `/main.html`
  - 目前範例主程式使用 `httpd_start(9090, ...)`

- `ws_server/`（local directory）
  - WebSocket 協定與事件處理
  - `wsh264` 範例會讀取本地測試資料（H.264、`sound.raw`、`oid.json`）並推送到前端
  - 支援命令如 `start_stream`、`stop_stream`、`get_model_status`、`get_version`、`get_config` 等

## 常用命令

```bash
# 重新編譯 HTTP 服務
cd build && cmake .. && make

# 重新編譯 WebSocket 服務
cd ws_server/build && cmake .. && make
```

## 專案結構

```text
.
├── minihttpd.cpp / minihttpd.h   # HTTP core
├── genbin/                        # static asset packer
├── htdocs/                        # web UI
├── ameba/                         # embedded target app
├── ministd/                       # lightweight STL-like containers
└── ws_server/                     # websocket server (vendored local directory)
```

## 注意事項

- 目前 README 內容以現行程式碼為準：HTTP 預設埠是 `9090`（不是 `8080`）。
- 如果只開 HTTP 不開 WebSocket，頁面可載入但 AI 串流與控制功能不會正常。
- `wsh264` 若沒有提供 bitstream 參數，會直接印出 usage 並結束。
