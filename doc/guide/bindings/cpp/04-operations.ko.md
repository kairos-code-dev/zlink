[← 서비스](./03-services.ko.md) · [C++ 가이드](./index.ko.md) · [다음: 레퍼런스 →](./05-reference.ko.md)

# 운영

---

## 소켓 옵션

`options()`는 소켓별 옵션 퍼사드를 반환합니다. 같은 이름의 메서드가 인자 유무로
getter/setter 역할을 합니다.

```cpp
auto opts = socket.options ();

// setter (인자 있음)
opts.send_hwm (zlink::message_count_t::value (1000));
opts.recv_hwm (zlink::message_count_t::value (1000));
opts.send_timeout (std::chrono::milliseconds (500));
opts.recv_timeout (std::chrono::milliseconds (500));
opts.linger (std::chrono::milliseconds (0));

// getter (인자 없음)
std::chrono::milliseconds t = opts.send_timeout ();
std::string ep = opts.last_endpoint ();

// ROUTER 전용
auto router_opts = router.options ();
router_opts.mandatory (true);

// DEALER·ROUTER 공통 (양쪽 옵션 퍼사드에 존재)
auto dealer_opts = dealer.options ();
dealer_opts.request_timeout (std::chrono::milliseconds (2000));
```

자동 HWM:

```cpp
ctx.options ().auto_hwm_enabled (true);
ctx.options ().auto_hwm_profile (zlink::auto_hwm_profile::balanced);
```

---

## TLS 보안

```cpp
socket.set_tls_server ("cert.pem", "key.pem", false);
socket.set_tls_client ("ca.pem", "server-hostname", false);

server.bind ("tls+tcp://0.0.0.0:5556");
client.connect ("tls+tcp://server.example.com:5556");
```

---

## 모니터링

```cpp
auto monitor = socket.monitor_open (zlink::monitor_event::connection_ready);

// 블로킹 수신 (optional 반환)
std::optional<zlink::monitor_event_t> event = monitor.recv ();
if (event.has_value ()
    && event->event == zlink::monitor_event::connection_ready) {
    std::printf ("피어 연결됨: %s\n", event->remote_addr.c_str ());
}

// 논블로킹
auto ev = monitor.recv (zlink::recv_flags_t::dontwait);
if (!ev.has_value ()) {
    // 이벤트 없음
}

// 상태 스냅샷
auto status = monitor.status ();
```

기본값(모든 이벤트)으로 열 수도 있습니다: `socket.monitor_open ()`.

---

## 폴러 / 타이머

```cpp
zlink::poller_t poller;
poller.add (socket1, zlink::poll_event_flag_t::pollin, 1);
poller.add (socket2, zlink::poll_event_flag_t::pollin, 2);

std::vector<zlink::poll_event_t> events (16);
size_t n = poller.wait (events.data (), events.size (),
                        std::chrono::milliseconds (100));
for (size_t i = 0; i < n; ++i) {
    switch (events[i].slot) {
        case 1: /* socket1 */ break;
        case 2: /* socket2 */ break;
    }
}
```

타이머:

```cpp
zlink::timer_t timer;
timer.start (std::chrono::milliseconds (500), 0); // 0 = 무한 반복

timer.on_fire ([] (uint64_t count) {
    std::printf ("타이머 %llu회\n", (unsigned long long) count);
});

// 스팟 이벤트 루프에 바인딩된 타이머
zlink::timer_t spot_timer = zlink::timer_t::from_spot (spot);
```

---

## 스레딩

| 항목 | 규칙 |
|------|------|
| `context_t` | 스레드 간 공유 가능 |
| 소켓 | **하나의 스레드에서만** 사용. 동시 접근 금지 |
| 디스패치 핸들러 | zlink 내부 워커 스레드에서 호출됨 |
| `message_t::bytes()` | 메시지 수명 동안만 유효한 span |

```cpp
// 올바른 패턴: 소켓 per-스레드
std::thread worker ([&ctx] {
    zlink::dealer_socket_t socket (ctx);
    socket.connect ("tcp://...");
    // 이 스레드에서만 socket 사용
});
```

---

## 네이티브 버전

```cpp
int major, minor, patch;
zlink::version (major, minor, patch);
std::printf ("zlink %d.%d.%d\n", major, minor, patch);

if (zlink::has ("draft")) {
    // draft API 지원
}
```
