[← 서비스](./03-services.md) · [Rust 가이드](./index.md) · [다음: 레퍼런스 →](./05-reference.md)

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
ctx.options().set_auto_hwm_enabled(true).unwrap();
ctx.options().set_auto_hwm_profile(zlink::AutoHwmProfile::Balanced).unwrap();
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
poller.add(&socket1, 1, zlink::PollEventFlags::POLL_IN).unwrap();
poller.add(&socket2, 2, zlink::PollEventFlags::POLL_IN).unwrap();

let events = poller.wait(std::time::Duration::from_millis(100)).unwrap();
for ev in &events {
    match ev.slot() {
        1 => { /* socket1 */ }
        2 => { /* socket2 */ }
        _ => {}
    }
}
```

타이머:

```rust
let timer = zlink::Timer::new().unwrap();
timer.start(std::time::Duration::from_millis(500), 0).unwrap();
let count = timer.recv().unwrap();   // 발화까지 대기
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
let v = zlink::runtime_version();
println!("zlink {}.{}.{}", v.major, v.minor, v.patch);
```
