[← 메시징](./02-messaging.ko.md) · [Node.js 가이드](./index.ko.md) · [다음: 운영 →](./04-operations.ko.md)

# 서비스

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
