[Rust 가이드](./index.ko.md) · [다음: 메시징 →](./02-messaging.ko.md)

# 시작하기

## 설치

`Cargo.toml`에 추가합니다.

```toml
[dependencies]
zlink = "6.0"
```

- **Rust 1.85** 이상 (edition 2024).
- 네이티브 코어가 빌드 시 함께 링크됩니다.

```rust
use zlink::{Context, Message, Received, RecvFlags};
```

---

## 5분 예제 — PING/ACK

```rust
use zlink::{Context, Message, Received, RecvFlags};

// 서버
let ctx = Context::new().unwrap();
let server = ctx.pair_socket().unwrap();
server.bind("tcp://127.0.0.1:5555").unwrap();

let mut received = Received::empty();
server.recv(&mut received, RecvFlags::NONE).unwrap();
println!("{}", received.parts()[0].as_str().unwrap()); // PING

let ack = Message::try_from(b"ACK").unwrap();
received.send().message(ack).submit().unwrap();
```

```rust
// 클라이언트
let ctx = Context::new().unwrap();
let client = ctx.pair_socket().unwrap();
client.connect("tcp://127.0.0.1:5555").unwrap();

let ping = Message::try_from(b"PING").unwrap();
client.send().message(ping).submit().unwrap();

let mut received = Received::empty();
client.recv(&mut received, RecvFlags::NONE).unwrap();
println!("{}", received.parts()[0].as_str().unwrap()); // ACK
```

---

## 핵심 타입

### 컨텍스트

```rust
let ctx = Context::new().expect("context creation failed");
// ctx가 drop되면 하위 소켓의 블로킹 작업이 중단됩니다
```

### 메시지

`Message`는 페이로드 프레임 하나를 소유합니다. `send`로 전달하면 소유권이
이전(move)되어 컴파일러가 이후 사용을 막아줍니다.

```rust
// 바이트 슬라이스에서 생성
let msg = Message::try_from(b"payload").unwrap();

// 크기 지정 빈 프레임
let mut msg = Message::with_size(256).unwrap();
msg.data_mut().copy_from_slice(&data);

// 전송 — msg는 여기서 move됨
socket.send().message(msg).submit().unwrap();
// msg를 다시 쓰면 컴파일 에러 → 소유권 안전성을 타입으로 보장
```

수신된 메시지 읽기:

```rust
let part = &received.parts()[0];
let bytes: &[u8] = part.as_bytes();
let text: &str = part.as_str().unwrap();   // UTF-8
let size = part.size();
```

### Received — 수신 봉투

```rust
let mut received = Received::empty();   // 재사용 가능
socket.recv(&mut received, RecvFlags::NONE).unwrap();

let parts = received.parts();                       // &[Message]
let rid: Option<&RoutingId> = received.routing_id(); // ROUTER/SPOT
let seq: Option<u64> = received.request_seq();
```

### 라우팅 ID

```rust
let rid = RoutingId::from_bytes(b"server-01");
socket.set_routing_id(&rid).unwrap();
```

---

## 소유권 규칙

Rust의 소유권 시스템이 대부분을 컴파일 타임에 강제합니다.

| 상황 | 규칙 |
|------|------|
| `submit()` 성공 | `Message`가 이미 move됨 — 추가 처리 불필요 |
| `submit()` 실패 | `Result::Err` 반환, 빌더가 내부 상태 정리 |
| `recv()` | `&mut Received`로 in-place 수신, drop 시 파트 해제 |
| 비동기 요청 | 회신 `Vec<Message>` 소유, 각 `Message`는 drop으로 해제 |

```rust
// 에러 처리 패턴
let msg = Message::try_from(b"data").unwrap();
match socket.send().message(msg).submit() {
    Ok(()) => { /* 전송됨 */ }
    Err(e) => eprintln!("send failed: {e}"),
}
```
