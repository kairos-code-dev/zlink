# Node.js Binding API Spec

> **적용 그룹**: recv only (callback 미사용)
> **perf 정책**: [`doc/perf/PERF_POLICY.md`](../doc/perf/PERF_POLICY.md) 참조
> **사유**: single-threaded event loop 런타임에서 I/O thread callback을
> JS로 전달하려면 `napi_threadsafe_function` 경유가 필요하며, event loop
> 큐잉으로 latency 측정 기준점이 I/O thread 시점에서 event loop dispatch
> 시점으로 이동한다. perf 측정 경로는 recv 모델만 사용한다.

---

## 1. Context

```js
const ctx = new Context();
ctx.ioThreads = 2;
ctx.maxSockets = 1024;
ctx.shutdown();
ctx.close();
```

---

## 2. Socket

### 2.1 소켓 타입

| 타입 | 클래스 |
|------|--------|
| PAIR | `PairSocket` |
| DEALER | `DealerSocket` |
| ROUTER | `RouterSocket` |
| PUB | `PubSocket` |
| SUB | `SubSocket` |
| XPUB | `XPubSocket` |
| XSUB | `XSubSocket` |
| STREAM | `StreamSocket` |

### 2.2 Endpoint

```js
socket.bind("tcp://*:5555");
socket.connect("tcp://localhost:5555");
socket.unbind("tcp://*:5555");
socket.disconnect("tcp://localhost:5555");
```

### 2.3 Send

| 방식 | 메서드 | 반환 |
|------|--------|------|
| blocking | `send(data)` | void (실패 시 throw) |
| blocking + flags | `send(data, flags)` | void |
| non-blocking | `send(data, DONTWAIT)` | `SendResult` (EAGAIN 시) |
| routed | `send(data, { routingId })` | void |
| async | `asend(data)` | `Promise<void>` |

### 2.4 Recv

| 방식 | 메서드 | 반환 |
|------|--------|------|
| blocking | `recv()` | `Received` |
| blocking + flags | `recv(flags)` | `Received` |
| non-blocking | `recv(DONTWAIT)` | `Received \| null` (EAGAIN 시 null) |
| async | `arecv()` | `Promise<Received>` |

- `Received`: routingId + message parts
- blocking `recv()` 는 Worker thread 또는 perf 용도
- `arecv()` 는 event loop 통합, 애플리케이션 용도

### 2.5 Callback (애플리케이션 API — perf 미사용)

```js
// 애플리케이션 코드에서 사용 가능하지만 perf에서는 사용하지 않음
socket.onReceive((received) => { ... });
socket.onSendReady(() => { ... });
socket.onSubscribe((routingId, topicId, received) => { ... });
```

> **perf 제외 사유**: I/O thread → JS callback 전달에 `napi_threadsafe_function`
> 경유 필요. event loop 큐잉으로 latency 측정 기준점이 I/O thread 수신 시점이
> 아닌 event loop dispatch 시점이 되어 측정 의미가 달라진다.

### 2.6 Socket Options

```js
socket.options.linger = 0;
socket.options.sendHighWaterMark = 1000;
socket.options.receiveHighWaterMark = 1000;
socket.options.sendTimeout = 200;
socket.options.receiveTimeout = 200;
```

### 2.7 TLS

```js
socket.setTlsServer(certPem, keyPem, requireClientCert);
socket.setTlsClient(caCertPem, hostname, trustSystem);
```

---

## 3. Poller

```js
const poller = new ZlinkPoller();
poller.add(socket, POLLIN);

// sync — perf 용
const events = poller.poll(1000);  // timeout ms
for (const { socket, revents } of events) {
    while (true) {
        const msg = socket.recv(DONTWAIT);
        if (msg === null) break;  // EAGAIN
        // process
    }
}

// async — event loop 통합
const events = await poller.apoll(1000);

poller.remove(socket);
poller.close();
```

| 메서드 | 설명 |
|--------|------|
| `add(socket, events)` | 소켓 등록 (POLLIN, POLLOUT 등) |
| `poll(timeoutMs)` | sync blocking poll, `PollEvent[]` 반환 |
| `apoll(timeoutMs)` | async poll, `Promise<PollEvent[]>` 반환 |
| `remove(socket)` | 소켓 제거 |

- sync `poll()`: N-API에서 직접 `zlink_poller_poll()` 호출
- async `apoll()`: `uv_poll_t`로 socket fd를 libuv event loop에 등록

---

## 4. Monitor

```js
const monitor = socket.monitor(
    ZlinkEvent.CONNECTION_READY | ZlinkEvent.DISCONNECTED);

// sync
const event = monitor.recv();

// async
const event = await monitor.arecv();

monitor.close();
```

---

## 5. Pub/Sub

```js
// Publisher
const pub = new PubSocket(ctx);
pub.publish("topic", data);
const result = pub.publish("topic", data, DONTWAIT);

// Subscriber
const sub = new SubSocket(ctx);
sub.setSubscription("topic");
const msg = sub.subscribe();           // blocking
const msg = sub.subscribe(DONTWAIT);   // non-blocking, null if EAGAIN

// async
const msg = await sub.asubscribe();
```

---

## 6. Spot

```js
const spot = new Spot(ctx);
spot.node().bind("tcp://*:5555");
spot.node().connectPeer("tcp://peer:5555");

// publish
spot.publish("topic", data);

// subscribe — sync
const msg = spot.subscribe();

// subscribe — async
for await (const msg of spot.asubscribe("topic")) {
    // process
}

spot.close();
```

---

## 7. Async API 정리

애플리케이션 용도로 libuv 통합 async API를 제공한다.
perf 측정 경로에서는 사용하지 않는다.

| sync | async | 구현 |
|------|-------|------|
| `recv()` | `arecv()` | `uv_poll_t` fd 등록 → readable 시 Promise resolve |
| `send()` | `asend()` | `uv_poll_t` fd 등록 → writable 시 Promise resolve |
| `poller.poll()` | `poller.apoll()` | libuv event loop 통합 |
| `monitor.recv()` | `monitor.arecv()` | `uv_poll_t` fd 등록 |

---

## 8. Perf 적용 패턴

Node.js는 **recv only** 그룹으로, single/multi 모두 `--recv recv` 로 실행한다.

### single suite

```js
const poller = new ZlinkPoller();
poller.add(socket, POLLIN);

while (active) {
    const events = poller.poll(100);
    for (const { socket, revents } of events) {
        while (true) {
            const msg = socket.recv(DONTWAIT);
            if (msg === null) break;
            accountMetric(msg);
        }
    }
}
```

### multi suite

```js
const poller = new ZlinkPoller();
for (const s of clientSockets) {
    poller.add(s, POLLIN | POLLOUT);
}

while (active) {
    const events = poller.poll(100);
    for (const { socket: s, revents } of events) {
        if (revents & POLLIN) {
            while (true) {
                const msg = s.recv(DONTWAIT);
                if (msg === null) break;
                accountMetric(msg);
            }
        }
        if (revents & POLLOUT) drainPending(s);
    }
}
```

### native bridge

| 항목 | 방식 |
|------|------|
| C 연동 | N-API (`node-addon-api`) |
| I/O 통합 (async) | `uv_poll_t`로 socket fd를 libuv event loop에 등록 |
| blocking recv | N-API에서 직접 `zlink_recv()` 호출 |
| poller | `zlink_poller_poll()` wrapping |
| stdout flush | 매 제어 메시지 후 `process.stdout.write()` (auto-flush) |
