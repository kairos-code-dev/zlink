# Python Binding API Spec

> **적용 그룹**: recv only (callback 미사용)
> **perf 정책**: [`doc/perf/PERF_POLICY.md`](../doc/perf/PERF_POLICY.md) 참조
> **사유**: GIL 제약으로 I/O thread callback 시 GIL acquire contention 발생.
> perf 측정 경로는 recv 모델만 사용한다.

---

## 1. Context

```python
ctx = Context()
ctx.io_threads = 2
ctx.max_sockets = 1024
ctx.shutdown()
ctx.close()

# context manager
with Context() as ctx:
    ...
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

```python
socket.bind("tcp://*:5555")
socket.connect("tcp://localhost:5555")
socket.unbind("tcp://*:5555")
socket.disconnect("tcp://localhost:5555")
```

### 2.3 Send

| 방식 | 메서드 | 반환 |
|------|--------|------|
| blocking | `send(payload)` | None (실패 시 `ZlinkError`) |
| non-blocking | `try_send(payload)` | `SendResult` |
| routed | `send(payload, routing_id=rid)` | None |
| multipart | `send([msg1, msg2])` | None |

- `SendResult`: `SENT`, `EAGAIN`, `ERROR`
- flags 인자: `send(payload, flags=DONTWAIT)`

### 2.4 Recv

| 방식 | 메서드 | 반환 |
|------|--------|------|
| blocking | `recv()` | `Received` |
| blocking + flags | `recv(flags=DONTWAIT)` | `Received` 또는 `None` (EAGAIN) |
| non-blocking | `try_recv()` | `Optional[Received]` |

- `Received`: routing_id + message parts
- blocking `recv()` 호출 시 GIL release → native `zlink_recv()` → GIL reacquire

### 2.5 Callback (애플리케이션 API — perf 미사용)

```python
# 애플리케이션 코드에서 사용 가능하지만 perf에서는 사용하지 않음
socket.on_receive(lambda received: ...)
socket.on_send_ready(lambda: ...)
socket.on_subscribe(lambda routing_id, topic, received: ...)
```

> **perf 제외 사유**: I/O thread에서 Python callback 호출 시 GIL acquire 필요.
> GIL contention으로 I/O thread가 block → throughput 저하.
> callback 호출 시점이 GIL 가용 시점에 따라 달라져 latency 측정 기준점 불확정.

### 2.6 Socket Options

```python
socket.options.linger_ms = 0
socket.options.send_high_water_mark = 1000
socket.options.receive_high_water_mark = 1000
socket.options.send_timeout_ms = 200
socket.options.receive_timeout_ms = 200
```

### 2.7 TLS

```python
socket.set_tls_server(cert_pem, key_pem, require_client_cert)
socket.set_tls_client(ca_cert_pem, hostname, trust_system)
```

---

## 3. Poller

```python
poller = ZlinkPoller()
poller.add_socket(socket, POLLIN)

events = poller.poll(1000)  # timeout ms
for sock, revents in events:
    while True:
        msg = sock.recv(flags=DONTWAIT)
        if msg is None:
            break  # EAGAIN
        # process

poller.remove_socket(socket)
poller.close()

# context manager
with ZlinkPoller() as poller:
    ...
```

| 메서드 | 설명 |
|--------|------|
| `add_socket(socket, events)` | 소켓 등록 (POLLIN, POLLOUT 등) |
| `poll(timeout_ms)` | blocking poll, `list[(socket, revents)]` 반환 |
| `remove_socket(socket)` | 소켓 제거 |

- `poll()` 호출 시 GIL release → native poll 대기 → GIL reacquire

---

## 4. Monitor

```python
monitor = socket.monitor_open(
    ZlinkEvent.CONNECTION_READY | ZlinkEvent.DISCONNECTED)

# blocking
event = monitor.recv()

# non-blocking
event = monitor.try_recv()

monitor.close()
```

---

## 5. Pub/Sub

```python
# Publisher
pub = PubSocket(ctx)
pub.publish("topic", message)
result = pub.try_publish("topic", message)

# Subscriber
sub = SubSocket(ctx)
sub.set_subscription("topic")
msg = sub.subscribe()            # blocking
msg = sub.try_subscribe()        # non-blocking
```

---

## 6. Spot

```python
spot = Spot(ctx)
spot.node().bind("tcp://*:5555")
spot.node().connect_peer("tcp://peer:5555")

# publish
spot.publish("topic", message)
result = spot.try_publish("topic", message)

# subscribe
msg = spot.subscribe()           # blocking
msg = spot.try_subscribe()       # non-blocking

spot.close()
```

---

## 7. Async API (asyncio)

애플리케이션 용도로 asyncio 통합 API를 제공한다.
perf 측정 경로에서는 사용하지 않는다.

```python
# async recv
msg = await socket.arecv()

# async send
await socket.asend(data)

# async poller
events = await poller.apoll(1000)

# async subscribe
async for msg in spot.asubscribe("topic"):
    ...

# async monitor
event = await monitor.arecv()
```

| sync | async | 구현 |
|------|-------|------|
| `recv()` | `arecv()` | `loop.add_reader(fd)` → fd readable 시 future resolve |
| `send()` | `asend()` | `loop.add_writer(fd)` → fd writable 시 future resolve |
| `poller.poll()` | `poller.apoll()` | executor에서 native poll 실행 |
| `monitor.recv()` | `monitor.arecv()` | `loop.add_reader(fd)` |

---

## 8. Perf 적용 패턴

Python은 **recv only** 그룹으로, single/multi 모두 `--recv recv` 로 실행한다.

### single suite

```python
poller = ZlinkPoller()
poller.add_socket(socket, POLLIN)

while active:
    events = poller.poll(100)
    for sock, revents in events:
        while True:
            msg = sock.recv(flags=DONTWAIT)
            if msg is None:
                break
            account_metric(msg)
```

### multi suite

```python
# client — poller + recv drain
poller = ZlinkPoller()
for s in client_sockets:
    poller.add_socket(s, POLLIN | POLLOUT)

while active:
    events = poller.poll(100)
    for sock, revents in events:
        if revents & POLLIN:
            while True:
                msg = sock.recv(flags=DONTWAIT)
                if msg is None:
                    break
                account_metric(msg)
        if revents & POLLOUT:
            drain_pending(sock)
```

### native bridge

| 항목 | 방식 |
|------|------|
| C 연동 | cffi 또는 C extension (ctypes는 perf overhead 큼) |
| blocking recv | GIL release → `zlink_recv()` → GIL reacquire |
| poller | GIL release → `zlink_poller_poll()` → GIL reacquire |
| stdout flush | 매 제어 메시지 후 `sys.stdout.flush()` 즉시 호출 |
