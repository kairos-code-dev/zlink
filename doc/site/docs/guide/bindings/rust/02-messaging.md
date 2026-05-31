[← 시작하기](./01-getting-started.md) · [Rust 가이드](./index.md) · [다음: 서비스 →](./03-services.md)

# 메시징

소켓 패턴별 Rust API 사용법을 설명합니다.

---

## PAIR

```rust
use zlink::{Context, Message, Received, RecvFlags};

let ctx = Context::new().unwrap();
let server = ctx.pair_socket().unwrap();
let client = ctx.pair_socket().unwrap();

server.bind("tcp://127.0.0.1:5560").unwrap();
client.connect("tcp://127.0.0.1:5560").unwrap();

let msg = Message::try_from(b"hello").unwrap();
client.send().message(msg).submit().unwrap();

let mut received = Received::empty();
server.recv(&mut received, RecvFlags::NONE).unwrap();
println!("{}", received.parts()[0].as_str().unwrap());
```

---

## DEALER / ROUTER

### 단순 송수신

```rust
use zlink::{Context, Message, Received, RecvFlags, RoutingId};

let ctx = Context::new().unwrap();
let router = ctx.router_socket().unwrap();
let dealer = ctx.dealer_socket().unwrap();

let rid = RoutingId::from_bytes(b"client-01");
dealer.set_routing_id(&rid).unwrap();

router.bind("tcp://127.0.0.1:5561").unwrap();
dealer.connect("tcp://127.0.0.1:5561").unwrap();

// 요청
let req = Message::try_from(b"get-price").unwrap();
dealer.send().message(req).submit().unwrap();

// 서버: 수신 후 회신
let mut request = Received::empty();
router.recv(&mut request, RecvFlags::NONE).unwrap();
let resp = Message::try_from(b"101.25").unwrap();
request.send().message(resp).submit().unwrap();

// 클라이언트: 응답 수신
let mut response = Received::empty();
dealer.recv(&mut response, RecvFlags::NONE).unwrap();
println!("{}", response.parts()[0].as_str().unwrap()); // 101.25
```

### 비동기 요청

`async`/`await`로 응답을 받습니다 (런타임은 자유 선택).

```rust
let req = Message::try_from(b"ping").unwrap();
let reply: Vec<Message> = dealer.request()
    .message(req)
    .timeout(std::time::Duration::from_secs(2))
    .submit_async()
    .await
    .unwrap();
println!("{}", reply[0].as_str().unwrap()); // pong
// reply의 각 Message는 Vec drop 시 자동 해제
```

서버 회신:

```rust
let mut request = Received::empty();
router.recv(&mut request, RecvFlags::NONE).unwrap();
if let Some(seq) = request.request_seq() {
    let rid = request.routing_id().unwrap().clone();
    let reply = Message::try_from(b"pong").unwrap();
    router.reply(&rid, seq).message(reply).submit().unwrap();
}
```

---

## PUB / SUB

```rust
use zlink::{Context, Message, RecvFlags, TopicMessage};

let ctx = Context::new().unwrap();
let pub_sock = ctx.pub_socket().unwrap();
let sub_sock = ctx.sub_socket().unwrap();

sub_sock.set_subscription("prices").unwrap();
pub_sock.bind("tcp://127.0.0.1:5562").unwrap();
sub_sock.connect("tcp://127.0.0.1:5562").unwrap();

let msg = Message::try_from(b"101.25").unwrap();
pub_sock.publish("prices").message(msg).submit().unwrap();

let mut topic = TopicMessage::empty();
if sub_sock.subscribe(&mut topic, RecvFlags::NONE).unwrap() {
    println!("{}: {}", topic.topic(), topic.parts()[0].as_str().unwrap());
}
```

---

## XPUB / XSUB

```rust
let xpub = ctx.xpub_socket().unwrap();
let sub = ctx.sub_socket().unwrap();

xpub.bind("tcp://127.0.0.1:5563").unwrap();
sub.connect("tcp://127.0.0.1:5563").unwrap();
sub.set_subscription("events").unwrap();

let mut event = zlink::SubscriptionEvent::empty();
if xpub.receive_subscription_event(&mut event, RecvFlags::NONE).unwrap() {
    println!("subscribed={} topic={}", event.subscribed(), event.topic());
}
```

---

## STREAM

```rust
use std::io::Write;

let ctx = Context::new().unwrap();
let server = ctx.stream_socket().unwrap();
server.bind("tcp://127.0.0.1:5564").unwrap();

let mut conn = std::net::TcpStream::connect("127.0.0.1:5564").unwrap();
conn.write_all(b"hello").unwrap();

let mut received = Received::empty();
server.recv(&mut received, RecvFlags::NONE).unwrap();
println!("{}", received.parts()[0].as_str().unwrap()); // hello

let reply = Message::try_from(b"world").unwrap();
received.send().message(reply).submit().unwrap();
```

---

## 논블로킹 수신

```rust
let mut received = Received::empty();
match socket.recv(&mut received, RecvFlags::DONT_WAIT) {
    Ok(true) => { /* 수신됨 */ }
    Ok(false) => { /* 메시지 없음 */ }
    Err(e) => return Err(e),
}
```

---

## 멀티파트 전송

```rust
let header = Message::try_from(b"cmd:buy").unwrap();
let body = Message::try_from(b"{\"qty\":10}").unwrap();
dealer.send().message(header).message(body).submit().unwrap();
```
