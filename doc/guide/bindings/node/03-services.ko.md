[← 메시징](./02-messaging.ko.md) · [Node.js 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스 레이어

raw 소켓은 "주소를 아는 두 지점"을 잇습니다. 서비스 레이어는 그 위에 **동적
토폴로지**(이름으로 발견, 런타임에 생겼다 사라지는 단위)를 얹습니다. 개념의 정식
정의와 층별 멘탈 모델은
[서비스 개요 §멘탈 모델](../../07-0-services.ko.md#12-멘탈-모델--어느-층을-언제-쓰나)이
소유하며, 이 문서는 **각 기능의 역할·언제** + **Node 사용 형태**를 다룹니다. 모든
서비스 객체는 `createX(ctx)`로 만들고 `.close()`로 닫습니다.

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

```javascript
const registry = zlink.createRegistry(ctx);
registry.bind('tcp://127.0.0.1:7400', 'tcp://127.0.0.1:7401');

const entries = registry.topology();
for (const e of entries) {
  console.log(e.channelName, e.state);
}
registry.close();
```

---

## Discovery

```javascript
const discovery = zlink.createDiscovery(ctx, zlink.AutoConnectType.Fanout, 'prices');
discovery.connectRegistry('tcp://127.0.0.1:7401');

const pub = zlink.createPubSocket(ctx);
pub.attachDiscovery(discovery);
pub.bind('tcp://127.0.0.1:5600');
```

자동 연결 방식: `AutoConnectType.Fanout`, `RouteMesh`, `ClientServer`,
`DealerMesh`, `SpotMesh`.

---

## SpotNode / Spot

메시 노드(SpotNode)와 그 위의 메시징 엔드포인트(Spot)입니다. "방·스테이지·존"
같은 동적 단위가 전형적인 Spot입니다. 개념: [SPOT](../../07-3-spot.ko.md).

**왜 Spot인가 — 실행 직렬성.** 한 Spot으로 들어온 메시지는 **단일 실행 큐로 직렬
처리**됩니다. 룸 상태를 lock으로 보호할 필요 없이 동시성 문제가 사라집니다. 게임
룸·심볼 오더북·채팅방처럼 한 단위의 상태를 안전하게 갱신할 때 raw PUB/SUB 대신
Spot을 쓰는 이유입니다. 상태 데이터는 여전히 응용이 소유합니다.

```javascript
const pubNode = zlink.createSpotNode(ctx);
const subNode = zlink.createSpotNode(ctx);

pubNode.setRoutingId(zlink.RoutingId.from(Buffer.from('node-pub')));
subNode.setRoutingId(zlink.RoutingId.from(Buffer.from('node-sub')));
pubNode.setPubBind('tcp://127.0.0.1:5700');
subNode.setPubBind('tcp://127.0.0.1:5701');
pubNode.connectPeer('tcp://127.0.0.1:5701');
subNode.connectPeer('tcp://127.0.0.1:5700');

const publisher = pubNode.createSpot();
const subscriber = subNode.createSpot();
subscriber.setSubscription('market:BTC');

// 발행
publisher.publish('market:BTC').message(Buffer.from('67000.00')).submit();

// 구독 수신
const topic = new zlink.TopicMessage();
if (subscriber.subscribe(topic)) {
  console.log(topic.topic, topic.parts[0].data().toString());
}
topic.close();
```

비동기 요청:

```javascript
const reply = await spot.requestToChannel('quotes')
  .message(Buffer.from('{"symbol":"BTC"}'))
  .timeout(5000)
  .submitAsync();
try {
  console.log(reply[0].data().toString());
} finally {
  for (const part of reply) part.close();
}
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

```javascript
const node = zlink.createSpotNode(ctx);
const spot = node.createSpot();
const actor = node.createActor('player-42');
const ref = actor.ref();

// 스팟 조인 (콜백 방식)
actor.join(spot)
  .message(Buffer.from('join'))
  .timeout(5000)
  .submit((result, parts) => {
    for (const p of parts) p.close();
    if (result.result === zlink.RequestResult.Ok) {
      // 조인 성공
    }
  });

// 스팟에서 조인 수락 — recvActorJoin은 요청(또는 null)을 반환
const req = spot.recvActorJoin(zlink.RecvFlags.None);
if (req) {
  spot.replyActorJoin(req, 0).message(Buffer.from('welcome')).submit();
  req.message.close();
}

// 액터 메시지 수신 — recvPart는 ActorPart(또는 null)를 반환
const part = actor.recvPart(zlink.RecvFlags.DontWait);
if (part) {
  console.log(part.message.data().toString());
  part.message.close();
}
```
