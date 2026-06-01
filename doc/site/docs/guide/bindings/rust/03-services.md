[← 메시징](./02-messaging.md) · [Rust 가이드](./index.md) · [다음: 운영 →](./04-operations.md)

# 서비스 레이어

raw 소켓은 "주소를 아는 두 지점"을 잇습니다. 서비스 레이어는 그 위에 **동적
토폴로지**(이름으로 발견, 런타임에 생겼다 사라지는 단위)를 얹습니다. 개념의 정식
정의와 층별 멘탈 모델은
[서비스 개요 §멘탈 모델](../../07-0-services.md)이
소유하며, 이 문서는 **각 기능의 역할·언제** + **Rust 사용 형태**를 다룹니다. 서비스
타입은 `Drop`으로 자동 정리됩니다.

> 핵심: 서비스 레이어는 **상태 저장소가 아닙니다.** 룸·세션 데이터는 응용이
> 소유합니다. SPOT은 그 상태에 닿는 메시지를 **단일 실행 큐로 직렬 처리**(lock
> 불필요)하고, Actor는 세션이 어느 서버에 붙어 있든 **같은 엔티티로 이어 줍니다**.

## 언제 서비스 레이어가 필요한가

| 상황 | 권장 |
|------|------|
| 주소가 고정된 소수 노드 | raw 소켓([02 메시징](./02-messaging.md))으로 충분 |
| 노드가 동적으로 늘고 줄어 이름으로 찾아야 함 | **Registry + Discovery** |
| 방·스테이지·존처럼 런타임에 생기는 라우팅 단위 | **SpotNode / Spot** |
| 세션/플레이어처럼 정체성을 갖는 엔티티 | **Actor** |

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

pub_node.set_routing_id(&zlink::RoutingId::from(b"node-pub")).unwrap();
sub_node.set_routing_id(&zlink::RoutingId::from(b"node-sub")).unwrap();
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

Spot에 합류(join)해 그 Spot으로 들어온 메시지를 받는 **상태 보유 엔티티**입니다
(플레이어·세션·작업 큐). 개념: [Actor](../../07-4-actor.md).

**왜 Actor인가 — 재접속 이전성.** Actor는 actor id로 식별되며 세션 연결과 별개로
존재합니다. 클라이언트가 끊겼다 다른 연결 서버로 재접속해도 같은 Actor로 다시
묶입니다 — "어느 서버에 붙어 있었는지"를 외부 저장소로 관리하던 일을 라이브러리가
가져갑니다. Actor는 raw 소켓의 대안이 아니라 **Spot 위에 얹는 한 단계 더 높은
모델**이며, Actor 메시지도 결국 Spot routed 평면 위로 흐릅니다.

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
    // ActorReceived는 first_part()/parts() 메서드로 파트에 접근합니다
    println!("{}", received.first_part().unwrap().as_str().unwrap());
}
```
