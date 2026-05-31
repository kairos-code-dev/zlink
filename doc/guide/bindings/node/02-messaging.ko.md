[← 시작하기](./01-getting-started.ko.md) · [Node.js 가이드](./index.ko.md) · [다음: 서비스 →](./03-services.ko.md)

# 메시징

소켓 패턴별 Node.js API 사용법을 설명합니다.

---

## PAIR

```javascript
const ctx = zlink.createContext();
const server = zlink.createPairSocket(ctx);
const client = zlink.createPairSocket(ctx);

server.bind('tcp://127.0.0.1:5560');
client.connect('tcp://127.0.0.1:5560');

client.send().message(Buffer.from('hello')).submit();

const received = new zlink.Received();
server.recv(received);
console.log(received.parts[0].data().toString());
received.close();

client.close();
server.close();
ctx.close();
```

---

## DEALER / ROUTER

### 단순 송수신

```javascript
const router = zlink.createRouterSocket(ctx);
const dealer = zlink.createDealerSocket(ctx);

dealer.setRoutingId(zlink.RoutingId.from(Buffer.from('client-01')));
router.bind('tcp://127.0.0.1:5561');
dealer.connect('tcp://127.0.0.1:5561');

// 요청
dealer.send().message(Buffer.from('get-price')).submit();

// 서버: 수신 후 회신
const request = new zlink.Received();
router.recv(request);
try {
  request.send().message(Buffer.from('101.25')).submit();
} finally {
  request.close();
}

// 클라이언트: 응답 수신
const response = new zlink.Received();
dealer.recv(response);
console.log(response.parts[0].data().toString()); // 101.25
response.close();
```

### 비동기 요청

```javascript
const reply = await dealer.request()
  .message(Buffer.from('ping'))
  .timeout(2000)
  .submitAsync();
try {
  console.log(reply[0].data().toString()); // pong
} finally {
  for (const part of reply) part.close();
}
```

서버 회신:

```javascript
const request = new zlink.Received();
router.recv(request);
try {
  if (request.requestSeq !== undefined) {
    router.reply(request.routingId, request.requestSeq)
      .message(Buffer.from('pong'))
      .submit();
  }
} finally {
  request.close();
}
```

---

## PUB / SUB

```javascript
const pub = zlink.createPubSocket(ctx);
const sub = zlink.createSubSocket(ctx);

pub.bind('tcp://127.0.0.1:5562');
sub.connect('tcp://127.0.0.1:5562');
sub.setSubscription('prices');

pub.publish('prices').message(Buffer.from('101.25')).submit();

const topic = new zlink.TopicMessage();
if (sub.subscribe(topic)) {
  console.log(topic.topic, topic.parts[0].data().toString());
}
topic.close();
```

---

## XPUB / XSUB

```javascript
const xpub = zlink.createXPubSocket(ctx);
const sub = zlink.createSubSocket(ctx);

xpub.bind('tcp://127.0.0.1:5563');
sub.connect('tcp://127.0.0.1:5563');
sub.setSubscription('events');

const event = new zlink.SubscriptionEvent();
if (xpub.receiveSubscriptionEvent(event)) {
  console.log(event.subscribed, event.topic);
}
```

---

## STREAM

```javascript
const net = require('node:net');

const server = zlink.createStreamSocket(ctx);
server.bind('tcp://127.0.0.1:5564');

const conn = net.connect(5564, '127.0.0.1');
conn.write('hello');

const received = new zlink.Received();
server.recv(received);
try {
  console.log(received.parts[0].data().toString()); // hello
  received.send().message(Buffer.from('world')).submit();
} finally {
  received.close();
}
```

---

## 논블로킹 수신

`RecvFlags.DontWait`을 사용하면 메시지가 없을 때 `RecvError`(result=`NoData`)를
던집니다.

```javascript
const received = new zlink.Received();
try {
  if (socket.recv(received, zlink.RecvFlags.DontWait)) {
    // 처리
  }
} catch (error) {
  if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
    // 메시지 없음
  } else {
    throw error;
  }
} finally {
  received.close();
}
```

---

## 멀티파트 전송

```javascript
dealer.send()
  .message(Buffer.from('header'))
  .message(Buffer.from('body'))
  .submit();
```
