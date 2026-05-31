[← 서비스](./03-services.ko.md) · [Rust 가이드](./index.ko.md) · [다음: 레퍼런스 →](./05-reference.ko.md)

# 운영

---

## 소켓 옵션

```rust
let opts = socket.options();
opts.set_send_hwm(1000).unwrap();
opts.set_recv_hwm(1000).unwrap();
opts.set_send_timeout(std::time::Duration::from_millis(500)).unwrap();
opts.set_linger(std::time::Duration::ZERO).unwrap();

// DEALER 전용
dealer.options().set_request_timeout(std::time::Duration::from_secs(2)).unwrap();

// ROUTER 전용
router.options().set_mandatory(true).unwrap();
```

자동 HWM:

```rust
// 컨텍스트 설정은 Context에 직접 있습니다
ctx.set_auto_hwm_enabled(true).unwrap();
ctx.set_auto_hwm_profile(zlink::AutoHwmProfile::Balanced).unwrap();
```

---

## TLS 보안

```rust
socket.set_tls_server("cert.pem", "key.pem", false).unwrap();
socket.set_tls_client("ca.pem", "server-hostname", false).unwrap();

server.bind("tls+tcp://0.0.0.0:5556").unwrap();
client.connect("tls+tcp://server.example.com:5556").unwrap();
```

---

## 모니터링

```rust
use zlink::SocketMonitor;

let monitor = SocketMonitor::open(&socket).unwrap();
let event = monitor.recv().unwrap();
if event.is_connection_ready() {
    println!("피어 연결됨");
}

// 스냅샷
let status = monitor.snapshot().unwrap();
if status.is_ready() {
    // 준비됨
}
```

---

## 폴러 / 타이머

```rust
let poller = zlink::Poller::new().unwrap();
// add_socket(소켓, 이벤트(i16), 슬롯). 이벤트 상수는 zlink::POLLIN 등
poller.add_socket(&socket1, zlink::POLLIN, 1).unwrap();
poller.add_socket(&socket2, zlink::POLLIN, 2).unwrap();

// wait는 미리 만든 슬라이스를 채우고 준비된 개수를 반환합니다(타임아웃 ms, i64)
let mut events = vec![zlink::PollEvent::default(); 16];
let n = poller.wait(&mut events, 100).unwrap();
for ev in &events[..n] {
    match ev.slot {   // slot은 필드입니다
        1 => { /* socket1 */ }
        2 => { /* socket2 */ }
        _ => {}
    }
}
```

타이머: 간격은 **나노초**(`u64`)입니다.

```rust
let timer = zlink::Timer::new().unwrap();
timer.start(500_000_000, 0).unwrap();   // 500ms, repeat 0 = 무한
// recv는 발화 횟수(Option<u64>)를 반환합니다
if let Some(count) = timer.recv().unwrap() {
    println!("발화 {count}회");
}
```

---

## 스레딩

| 항목 | 규칙 |
|------|------|
| `Context` | `Sync` — 스레드 간 공유 가능 (`Arc<Context>`) |
| 소켓 | `Send`이지만 한 스레드에서만 사용. 동시 접근 금지 |
| `Message::as_bytes()` | 메시지 수명 동안만 유효 |

```rust
use std::sync::Arc;
let ctx = Arc::new(Context::new().unwrap());

let ctx2 = ctx.clone();
std::thread::spawn(move || {
    let socket = ctx2.dealer_socket().unwrap();
    // 이 스레드에서만 socket 사용
});
```

---

## 네이티브 버전

```rust
let (major, minor, patch) = zlink::version();   // (i32, i32, i32) 튜플
println!("zlink {major}.{minor}.{patch}");
```
