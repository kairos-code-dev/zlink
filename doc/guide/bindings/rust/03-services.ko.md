[← 메시징](./02-messaging.ko.md) · [Rust 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스

---

## Registry

```rust
use zlink::{Context, Registry};

let ctx = Context::new().unwrap();
let registry = Registry::new(&ctx).unwrap();
registry.bind("tcp://127.0.0.1:7400", "tcp://127.0.0.1:7401").unwrap();

for entry in registry.topology().unwrap() {
    // 토폴로지 엔트리 멤버는 필드입니다
    println!("{}: {:?}", entry.channel_name, entry.state);
}
```

---

## Discovery

```rust
use zlink::{AutoConnectType, Context, Discovery};

let ctx = Context::new().unwrap();
let discovery = Discovery::new(&ctx, AutoConnectType::Fanout, "prices").unwrap();
discovery.connect_registry("tcp://127.0.0.1:7401").unwrap();

let pub_sock = ctx.pub_socket().unwrap();
pub_sock.attach_discovery(&discovery).unwrap();
pub_sock.bind("tcp://127.0.0.1:5600").unwrap();
```

자동 연결 방식: `AutoConnectType::Fanout`, `RouteMesh`, `ClientServer`,
`DealerMesh`, `SpotMesh`.

---

## SpotNode / Spot

```rust
use zlink::{Context, Message, RecvFlags, SpotNode, TopicMessage};

let ctx = Context::new().unwrap();
let pub_node = SpotNode::new(&ctx).unwrap();
let sub_node = SpotNode::new(&ctx).unwrap();

pub_node.set_routing_id(&zlink::RoutingId::from_bytes(b"node-pub")).unwrap();
sub_node.set_routing_id(&zlink::RoutingId::from_bytes(b"node-sub")).unwrap();
pub_node.set_pub_bind("tcp://127.0.0.1:5700").unwrap();
sub_node.set_pub_bind("tcp://127.0.0.1:5701").unwrap();
pub_node.connect_peer("tcp://127.0.0.1:5701").unwrap();
sub_node.connect_peer("tcp://127.0.0.1:5700").unwrap();

let publisher = pub_node.create_spot().unwrap();
let subscriber = sub_node.create_spot().unwrap();
subscriber.set_subscription("market:BTC").unwrap();

// 발행
let msg = Message::try_from(b"67000.00").unwrap();
publisher.publish("market:BTC").message(msg).submit().unwrap();

// 구독 수신
let mut topic = TopicMessage::empty();
if subscriber.subscribe(&mut topic, RecvFlags::NONE).unwrap() {
    println!("{}: {}", topic.topic(), topic.parts()[0].as_str().unwrap());
}
```

비동기 요청:

```rust
let req = Message::try_from(b"{\"symbol\":\"BTC\"}").unwrap();
let reply: Vec<Message> = spot.request_to_channel("quotes")
    .message(req)
    .timeout(std::time::Duration::from_secs(5))
    .submit_async()
    .await
    .unwrap();
```

---

## Actor

```rust
use zlink::{Context, Message, RecvFlags, SpotNode};

let ctx = Context::new().unwrap();
let node = SpotNode::new(&ctx).unwrap();
let spot = node.create_spot().unwrap();

let actor = node.create_actor("player-42").unwrap();
let actor_ref = actor.actor_ref();

// 스팟 조인 (콜백 방식)
let payload = Message::try_from(b"join").unwrap();
actor.join(&spot)
    .message(payload)
    .timeout(std::time::Duration::from_secs(5))
    .submit(|result, parts| {
        // parts는 콜백 안에서 자동 해제됨
        if result.result == zlink::RequestResult::Ok {
            // 조인 성공
        }
    })
    .unwrap();

// 스팟에서 조인 수락 — recv_actor_join은 요청을 반환합니다
// (논블로킹이 필요하면 recv_actor_join_with_flags(RecvFlags::DONT_WAIT))
let req = spot.recv_actor_join().unwrap();
let reply = Message::try_from(b"welcome").unwrap();
spot.reply_actor_join(&req, 0).message(reply).submit().unwrap();

// 액터 메시지 수신 — &mut ActorReceived에 채우고 bool을 반환합니다
let mut received = zlink::ActorReceived::empty();
if actor.recv(&mut received, RecvFlags::DONT_WAIT).unwrap() {
    // message는 필드입니다
    println!("{}", received.message.as_str().unwrap());
}
```
