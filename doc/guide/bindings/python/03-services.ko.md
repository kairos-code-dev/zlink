[← 메시징](./02-messaging.ko.md) · [Python 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스

---

## Registry

```python
with zlink.create_context() as ctx:
    with zlink.create_registry(ctx) as registry:
        registry.bind("tcp://127.0.0.1:7400", "tcp://127.0.0.1:7401")
        entries = registry.topology()
        for e in entries:
            print(e.channel_name, e.state)
```

---

## Discovery

```python
with zlink.create_context() as ctx:
    with zlink.create_discovery(ctx, zlink.AutoConnectType.FANOUT, "prices") as disc:
        disc.connect_registry("tcp://127.0.0.1:7401")

        with zlink.create_pub_socket(ctx) as pub:
            pub.attach_discovery(disc)
            pub.bind("tcp://127.0.0.1:5600")
```

자동 연결 방식: `AutoConnectType.FANOUT`, `ROUTE_MESH`, `CLIENT_SERVER`,
`DEALER_MESH`, `SPOT_MESH`.

---

## SpotNode / Spot

```python
with zlink.create_context() as ctx:
    with zlink.create_spot_node(ctx) as pub_node, \
         zlink.create_spot_node(ctx) as sub_node:

        pub_node.set_routing_id(b"node-pub")
        sub_node.set_routing_id(b"node-sub")
        pub_node.set_pub_bind("tcp://127.0.0.1:5700")
        sub_node.set_pub_bind("tcp://127.0.0.1:5701")
        pub_node.connect_peer("tcp://127.0.0.1:5701")
        sub_node.connect_peer("tcp://127.0.0.1:5700")

        with pub_node.create_spot() as publisher, \
             sub_node.create_spot() as subscriber:

            publisher.set_routing_id(b"spot-pub")
            subscriber.set_routing_id(b"spot-sub")
            subscriber.set_subscription(b"market:BTC")

            # 발행
            publisher.publish(b"market:BTC").message(b"67000.00").submit()

            # 구독 수신
            received = zlink.create_topic_message()
            if subscriber.subscribe_into(received):
                with received:
                    print(received.topic, received.to_bytes_list())
```

---

## Actor

```python
with zlink.create_context() as ctx:
    with zlink.create_spot_node(ctx) as node, \
         node.create_spot() as spot:

        actor = node.actor("player-42")     # 액터 생성·획득
        ref = actor.ref()

        import threading

        join_done = threading.Event()

        def on_join(result, parts):
            for p in parts:
                p.close()
            if result.result == zlink.RequestResult.OK:
                join_done.set()

        # timeout은 초 단위(float)
        actor.join(spot).message(b"join").timeout(5.0).submit(on_join)

        # 스팟에서 조인 수락 — recv_actor_join이 요청 객체를 반환
        item = spot.recv_actor_join(flags=zlink.RecvFlags.NONE)
        spot.reply_actor_join(item, 0).message(b"ok").submit()
        item.message.close()

        join_done.wait(timeout=6)

        # 액터 메시지 수신 — recv_part는 파트(또는 None)를 반환
        part = actor.recv_part(flags=zlink.RecvFlags.DONT_WAIT)
        if part is not None:
            print(part.message.to_bytes())
            part.message.close()
```
