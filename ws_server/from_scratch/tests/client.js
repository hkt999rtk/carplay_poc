const WebSocket = require('ws');

// 連線到你的 server
const ws = new WebSocket('ws://172.29.244.70:9001');

// 當連線建立
ws.on('open', function open() {
  console.log('✅ Connected to server');
  ws.send('Hello from client!');
});

// 收到伺服器訊息
ws.on('message', function message(data) {
  console.log('📩 Received:', data.toString());
});

// 連線關閉
ws.on('close', () => {
  console.log('❌ Connection closed');
});

// 錯誤處理
ws.on('error', (err) => {
  console.error('⚠️ Error:', err);
});
