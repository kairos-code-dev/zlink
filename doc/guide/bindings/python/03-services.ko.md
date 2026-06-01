[← 메시징](./02-messaging.ko.md) · [Python 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스 레이어

raw 소켓은 "주소를 아는 두 지점"을 잇습니다. 서비스 레이어는 그 위에 **동적
토폴로지**(이름으로 발견, 런타임에 생겼다 사라지는 단위)를 얹습니다. 개념의 정식
정의와 층별 멘탈 모델은
[서비스 개요 §멘탈 모델](../../07-0-services.ko.md)이
소유하며, 이 문서는 **각 기능의 역할·언제** + **Python 사용 형태**를 다룹니다.
서비스 객체는 컨텍스트 매니저(`with`)나 `.close()`로 닫습니다.

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

메시 노드(SpotNode)와 그 위의 메시징 엔드포인트(Spot)입니다. "방·스테이지·존"
같은 동적 단위가 전형적인 Spot입니다. 개념: [SPOT](../../07-3-spot.ko.md).

**왜 Spot인가 — 실행 직렬성.** 한 Spot으로 들어온 메시지는 **단일 실행 큐로 직렬
처리**됩니다. 룸 상태를 lock으로 보호할 필요 없이 동시성 문제가 사라집니다. 게임
룸·심볼 오더북·채팅방처럼 한 단위의 상태를 안전하게 갱신할 때 raw PUB/SUB 대신
Spot을 쓰는 이유입니다. 상태 데이터는 여전히 응용이 소유합니다.

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

Spot에 합류(join)해 그 Spot으로 들어온 메시지를 받는 **상태 보유 엔티티**입니다
(플레이어·세션·작업 큐). 개념: [Actor](../../07-4-actor.ko.md).

**왜 Actor인가 — 재접속 이전성.** Actor는 actor id로 식별되며 세션 연결과 별개로
존재합니다. 클라이언트가 끊겼다 다른 연결 서버로 재접속해도 같은 Actor로 다시
묶입니다 — "어느 서버에 붙어 있었는지"를 외부 저장소로 관리하던 일을 라이브러리가
가져갑니다. Actor는 raw 소켓의 대안이 아니라 **Spot 위에 얹는 한 단계 더 높은
모델**이며, Actor 메시지도 결국 Spot routed 평면 위로 흐릅니다.

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
