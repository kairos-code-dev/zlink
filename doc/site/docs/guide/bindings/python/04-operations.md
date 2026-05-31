[← 서비스](./03-services.md) · [Python 가이드](./index.md) · [다음: 레퍼런스 →](./05-reference.md)

# 운영

---

## 소켓 옵션

```python
opts = socket.options()
opts.send_high_water_mark = 1000
opts.receive_high_water_mark = 1000
opts.send_timeout_ms = 500       # 밀리초
opts.receive_timeout_ms = 500
opts.linger_ms = 0

# DEALER/ROUTER 전용 옵션 (옵션 퍼사드에 있음)
dealer.options().request_timeout_ms = 2000
router.options().mandatory = True

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

연결 준비 여부는 모니터 스냅샷으로 확인합니다.

```python
with socket.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as monitor:
    # 이벤트 스트림 수신
    event = monitor.recv()              # MonitorEvent
    # 현재 상태 스냅샷
    if monitor.status().is_ready():
        print("피어 연결됨")
```

---

## 폴러 / 타이머

```python
poller = zlink.create_poller()
# 인자 순서: (대상, 이벤트, 슬롯)
poller.add_socket(socket1, zlink.PollEventFlag.POLLIN, 1)
poller.add_socket(socket2, zlink.PollEventFlag.POLLIN, 2)

# wait는 미리 만든 버퍼를 채우고 준비된 개수를 반환합니다
events = zlink.create_poll_events(16)
n = poller.wait(events, 100)   # timeout 밀리초
for i in range(events.ready_count):
    print(events.slot(i), events.revents(i))

poller.close()
```

타이머: 간격은 **나노초** 정수입니다.

```python
timer = zlink.create_timer()
timer.start(500_000_000, 0)   # 500ms = 5억 ns, repeat 0 = 무한

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
major, minor, patch = zlink.version()   # (major, minor, patch) 튜플
print(f"zlink {major}.{minor}.{patch}")
```
