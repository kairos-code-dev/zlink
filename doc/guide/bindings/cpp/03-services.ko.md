[← 메시징](./02-messaging.ko.md) · [C++ 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스

서비스 타입은 `zlink::service` 네임스페이스에 있습니다.

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
