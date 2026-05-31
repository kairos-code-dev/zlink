[← 서비스](./03-services.ko.md) · [Node.js 가이드](./index.ko.md) · [다음: 레퍼런스 →](./05-reference.ko.md)

# 운영

---

## 소켓 옵션

```javascript
const opts = socket.options();
opts.sendHwm = 1000;
opts.recvHwm = 1000;
opts.sendTimeout = 500;   // 밀리초
opts.recvTimeout = 500;
opts.linger = 0;

// DEALER 전용
dealer.options().requestTimeout = 2000;

// ROUTER 전용
router.options().mandatory = true;

// 자동 HWM
ctx.options().autoHwmEnabled = true;
ctx.options().autoHwmProfile = zlink.AutoHwmProfile.Balanced;
```

---

## TLS 보안

```javascript
socket.setTlsServer('cert.pem', 'key.pem', false);
socket.setTlsClient('ca.pem', 'server-hostname', false);

server.bind('tls+tcp://0.0.0.0:5556');
client.connect('tls+tcp://server.example.com:5556');
```

---

## 모니터링

```javascript
const monitor = socket.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
try {
  const event = monitor.recv();
  if (event.event === zlink.MonitorEventType.ConnectionReady) {
    console.log('피어 연결됨');
  }
} finally {
  monitor.close();
}
```

---

## 폴러 / 타이머

```javascript
const poller = zlink.createPoller();
// 인자 순서: (소켓, 이벤트 배열, 슬롯)
poller.add(socket1, [zlink.PollEventFlag.PollIn], 1);
poller.add(socket2, [zlink.PollEventFlag.PollIn], 2);

// wait는 미리 만든 버퍼를 채우고 준비된 개수를 반환합니다
const events = zlink.createPollEvents(16);
const n = poller.wait(events, 100); // 타임아웃 밀리초
for (let i = 0; i < n; i++) {
  switch (events.slot(i)) {
    case 1: /* socket1 */ break;
    case 2: /* socket2 */ break;
  }
}
poller.close();
```

타이머: 간격·반복은 **bigint 나노초**입니다.

```javascript
const timer = zlink.createTimer();
timer.start(500_000_000n, 0n); // 500ms = 5억 ns, repeat 0n = 무한
const count = timer.recv();     // bigint | null (발화 횟수)
timer.close();
```

---

## 스레딩

Node는 단일 스레드 이벤트 루프 모델입니다.

| 항목 | 규칙 |
|------|------|
| `Context`·소켓 | 메인 이벤트 루프에서 사용 |
| 블로킹 `recv()` | 이벤트 루프를 막으므로 짧게 사용하거나 논블로킹 + 폴러 권장 |
| `submitAsync()` | Promise 기반 — 이벤트 루프를 막지 않음 |
| Worker 스레드 | SpotNode 핸들은 Worker 간 공유 불가 |

```javascript
// 권장: 비동기 패턴
const reply = await dealer.request().message(buf).submitAsync();

// 또는 폴러로 논블로킹
const events = zlink.createPollEvents(16);
const n = poller.wait(events, 0); // 즉시 반환
```

---

## 네이티브 버전

```javascript
const [major, minor, patch] = zlink.version(); // [number, number, number]
console.log(`zlink ${major}.${minor}.${patch}`);
```
