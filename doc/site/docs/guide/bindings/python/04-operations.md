[← 서비스](./03-services.md) · [Python 가이드](./index.md) · [다음: 레퍼런스 →](./05-reference.md)

# 운영

---

## 소켓 옵션

```python
opts = socket.options()
opts.send_hwm = 1000
opts.recv_hwm = 1000
opts.send_timeout = 500   # 밀리초
opts.recv_timeout = 500
opts.linger = 0

# 자동 HWM
ctx.options().auto_hwm_enabled = True
ctx.options().auto_hwm_profile = zlink.AutoHwmProfile.BALANCED
```

---

## TLS 보안

```python
socket.set_tls_server("cert.pem", "key.pem", require_client_cert=False)
socket.set_tls_client("ca.pem", "server-hostname", trust_system=False)
```

`tls+tcp://` 엔드포인트 사용:

```python
server.bind("tls+tcp://0.0.0.0:5556")
client.connect("tls+tcp://server.example.com:5556")
```

---

## 모니터링

```python
with socket.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as monitor:
    event = monitor.recv()
    if event.is_connection_ready():
        print("피어 연결됨")
```

---

## 폴러 / 타이머

```python
poller = zlink.create_poller()
poller.add(socket1, slot=1, events=zlink.PollEventFlag.POLLIN)
poller.add(socket2, slot=2, events=zlink.PollEventFlag.POLLIN)

events = poller.wait(timeout_ms=100)
for ev in events:
    print(ev.slot, ev.revents)

poller.close()
```

타이머:

```python
timer = zlink.create_timer()
timer.start(interval_ms=500, repeat=0)

def on_fire(count):
    print(f"타이머 {count}회")

timer.on_fire(on_fire)
timer.close()
```

---

## 스레딩

| 항목 | 규칙 |
|------|------|
| `Context` | 스레드 간 공유 가능 |
| 소켓 | **하나의 스레드에서만** 사용 |
| `recv_into()` 블로킹 | `asyncio.to_thread(socket.recv_into, received)`로 오프로드 |

---

## 네이티브 라이브러리 버전

```python
v = zlink.runtime_version()
print(f"zlink {v.major}.{v.minor}.{v.patch}")
```
