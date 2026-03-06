# 測試總覽

## 依賴與環境
- CMake 3.12 以上（建置 C 程式）
- Node.js（建議 18+）與 npm（下載 `ws` 套件）
- Python 3（驅動整合腳本）
- Valgrind（可選，若安裝則自動執行記憶體檢測）

首次執行時 `make test` 會在 `tests/js/` 下自動執行 `npm install`，無須手動處理相依套件。

## 指令流程
- `make test`
  1. 重新執行 `cmake -S .. -B ../build` 與 `cmake --build`。
  2. 透過 `ctest` 跑既有 C 單元／整合測試。
  3. 針對三個 WebSocket 壓力情境跑完整矩陣：
     - **C server ↔ C client**：TEXT/BINARY 各 120 封包，client 數 1~4，分別以「同時連線」（concurrent）與「依序連線」（sequential，啟動間隔 50 ms）測試。
     - **JS server (`ws`) ↔ C client**：同樣的封包與 client 數矩陣，由 `node src/server.js` 與 `wsfs_js_server_stress` 協同驗證。
     - **C server ↔ JS client**：C 端為原生 server，JS 端以純 TCP + 手動 WebSocket 實作 client，同樣執行 1~4 client 的同步／異步測試。
  4. 若系統有安裝 Valgrind，額外跑代表性情境的 `memcheck`（C↔C、JS server↔C client、C server↔JS client 各一次），產生 leak 檢查報告。
  5. 匯總結果到 `tests/artifacts/report.json`，並保留各回合的 log 與報表。

- `make clean-artifacts`：移除 `tests/artifacts/` 底下產出的報告、log 與暫存資料。

## 生成檔案
```
tests/artifacts/
  logs/
    cmake_*.log
    c2c_{mode}_c{N}.stdout.log / .stderr.log
    js_server_{mode}_c{N}.stdout.log / .stderr.log
    c_server_{mode}_c{N}.stdout.log / .stderr.log
    …（含 Valgrind 時會多出 *.memcheck.log）
  reports/
    c2c_{mode}_c{N}.json
    js_server_{mode}_c{N}.json
    js_client_stress_{mode}_c{N}.json
    c_server_{mode}_c{N}.json
    js_client_{mode}_c{N}.json
    （若執行 Valgrind 會多出 *_valgrind*.json）
  report.json          # 總結所有步驟
```

每份報表皆以 JSON 記錄傳輸統計、連線次數、close code 與錯誤訊息，可直接由 CI 或額外腳本解析。

## 已執行測試
- `make test`
  - 跑完 C↔C / JS server↔C client / C server↔JS client 三大情境，涵蓋 client 數 1–4、`concurrent` 與 `sequential` 模式。
  - 產生 `tests/artifacts/report.json` 與對應 log／報表，結果全數通過。
- 未偵測到 Valgrind（系統未安裝），因此整合腳本自動略過 `memcheck`；若未來安裝後再執行 `make test` 會自動補跑記憶體檢測。
