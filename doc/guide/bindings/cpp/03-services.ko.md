[← 메시징](./02-messaging.ko.md) · [C++ 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스 레이어

raw 소켓은 "주소를 아는 두 지점"을 잇습니다. 서비스 레이어는 그 위에 **동적
토폴로지**(이름으로 발견, 런타임에 생겼다 사라지는 단위)를 얹습니다. 개념의 정식
정의와 층별 멘탈 모델은
[서비스 개요 §멘탈 모델](../../07-0-services.ko.md)이
소유하며, 이 문서는 **각 기능의 역할·언제** + **C++ 사용 형태**를 다룹니다. 서비스
타입은 `zlink::service` 네임스페이스의 move-only RAII 타입입니다.

> 핵심: 서비스 레이어는 **상태 저장소가 아닙니다.** 룸·세션 데이터는 응용이
> 소유합니다. SPOT은 그 상태에 닿는 메시지를 **단일 실행 큐로 직렬 처리**(lock
> 불필요)하고, Actor는 세션이 어느 서버에 붙어 있든 **같은 엔티티로 이어 줍니다**.

## 언제 서비스 레이어가 필요한가

| 상황 | 권장 |
|------|------|
| 주소가 고정된 소수 노드 | raw 소켓([02 메시징](./02-messaging.ko.md))으로 충분 |
| 노드가 동적으로 늘고 줄어 이름으로 찾아야 함 | **Registry + Discovery** |
| 방·스테이지·존처럼 런타임에 생기는 라우팅 단위 | **SpotNode / Spot** |
| 세션/플레이어처럼 정체성을 갖는 엔티티 | **Actor** |

---

## Registry

```cpp
zlink::context_t ctx;
zlink::service::registry_t registry (ctx);

registry.bind ("tcp://127.0.0.1:7400", "tcp://127.0.0.1:7401");
registry.set_broadcast_interval (50);   // 밀리초

// 토폴로지 조회
auto entries = registry.topology ();
for (const auto &e : entries) {
    std::printf ("%s\n", e.channel_name ().c_str ());
}
```

---

## Discovery

```cpp
zlink::context_t ctx;
zlink::service::registry_t registry (ctx);
zlink::service::discovery_t discovery (
    ctx, zlink::auto_connect_type::fanout, "prices");

registry.bind ("tcp://127.0.0.1:7400", "tcp://127.0.0.1:7401");
discovery.connect_registry ("tcp://127.0.0.1:7401");

zlink::pub_socket_t pub (ctx);
pub.attach_discovery (discovery);
pub.bind ("tcp://127.0.0.1:5600");

// 발견된 피어 조회
auto peers = discovery.member_peers ();
```

자동 연결 방식: `zlink::auto_connect_type::fanout`, `route_mesh`,
`client_server`, `dealer_mesh`, `spot_mesh`.

---

## SpotNode / Spot

메시 노드(SpotNode)와 그 위의 메시징 엔드포인트(Spot)입니다. "방·스테이지·존"
같은 동적 단위가 전형적인 Spot입니다. 개념: [SPOT](../../07-3-spot.ko.md).

**왜 Spot인가 — 실행 직렬성.** 한 Spot으로 들어온 메시지는 **단일 실행 큐로 직렬
처리**됩니다. 룸 상태를 lock으로 보호할 필요 없이 동시성 문제가 사라집니다. 게임
룸·심볼 오더북·채팅방처럼 한 단위의 상태를 안전하게 갱신할 때 raw PUB/SUB 대신
Spot을 쓰는 이유입니다. 상태 데이터는 여전히 응용이 소유합니다.

```cpp
zlink::context_t ctx;
zlink::service::spot_node_t node (ctx);
zlink::service::spot_t spot = node.create_spot ();

node.set_routing_id (zlink::routing_id_t::from_bytes (
    reinterpret_cast<const uint8_t*> ("node-1"), 6));
node.set_pub_bind ("tcp://127.0.0.1:5700");
node.connect_peer ("tcp://10.0.0.2:5700");

// 구독 설정
spot.set_subscription ("market:BTC");

// 발행
zlink::message_t msg = zlink::message_t::from_string ("67000.00");
spot.publish ("market:BTC").message (msg).submit ();

// 구독 수신
zlink::topic_message_t inbound;
if (spot.subscribe (inbound, zlink::recv_flags_t::dontwait)
    == static_cast<int> (zlink::recv_result_t::ok)) {
    std::printf ("%s: %s\n",
        inbound.topic ().c_str (),
        inbound.parts ()[0].to_string ().c_str ());
    inbound.close ();
}
```

디스패치 핸들러로 이벤트 처리:

```cpp
spot.set_dispatch_handler (
    [&] (zlink::service::spot_t &s, const zlink::spot_dispatch_info_t &info) {
        if (info.event == zlink::spot_dispatch_event_t::subscribe_readable) {
            zlink::topic_message_t msg;
            s.subscribe (msg, zlink::recv_flags_t::dontwait);
            // 처리
            msg.close ();
        }
    });
```

---

## Actor

Spot에 합류(join)해 그 Spot으로 들어온 메시지를 받는 **상태 보유 엔티티**입니다
(플레이어·세션·작업 큐). 개념: [Actor](../../07-4-actor.ko.md).

**왜 Actor인가 — 재접속 이전성.** Actor는 actor id로 식별되며 세션 연결과 별개로
존재합니다. 클라이언트가 끊겼다 다른 연결 서버로 재접속해도 같은 Actor로 다시
묶입니다 — "어느 서버에 붙어 있었는지"를 외부 저장소로 관리하던 일을 라이브러리가
가져갑니다. Actor는 raw 소켓의 대안이 아니라 **Spot 위에 얹는 한 단계 더 높은
모델**이며, Actor 메시지도 결국 Spot routed 평면 위로 흐릅니다.

```cpp
zlink::context_t ctx;
zlink::service::spot_node_t node (ctx);
zlink::service::spot_t spot = node.create_spot ();
zlink::service::actor_t actor = node.create_actor ("player-42");
zlink::actor_ref_t ref = actor.ref ();

// 스팟 조인 (콜백 방식)
zlink::message_t payload = zlink::message_t::from_string ("join");
actor.join (spot)
    .message (payload)
    .flags (zlink::recv_flags_t::dontwait)
    .timeout (std::chrono::milliseconds (5000))
    .submit (
        [&] (const zlink::actor_join_result_t &result,
             std::vector<zlink::message_t> parts) {
            if (result.result == zlink::request_result_t::ok) {
                // 조인 성공 — parts는 콜백 종료 시 자동 해제
            }
        });

// 스팟에서 조인 수락 (디스패치 핸들러 내)
auto request = spot.recv_actor_join (zlink::recv_flags_t::dontwait);
if (request.has_value ()) {
    zlink::message_t reply = zlink::message_t::from_string ("welcome");
    spot.reply_actor_join (*request, 0).message (reply).submit ();
}

// 스팟 떠나기
actor.leave (spot).submit_async ().get ();
```
