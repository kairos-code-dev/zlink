# 소켓 옵션 상세 가이드

이 문서는 `zlink_set_option()` / `zlink_get_option()`으로 설정하는 소켓 옵션
각각의 **동작**, **영향 범위**, **기본값**, **소켓 타입별 차이**를 상세히 설명한다.
API 시그니처만 다루는 [socket API 레퍼런스](../api/socket.ko.md)와 달리,
이 가이드는 **옵션이 런타임에 무엇을 바꾸는지**에 초점을 둔다.

## 옵션 소유권 카테고리

내부적으로 옵션은 세 카테고리로 분류된다.

| 카테고리 | 설명 | 대표 옵션 |
|----------|------|-----------|
| **Core Socket** | 소켓 핵심 동작 | SNDHWM, RCVHWM, LINGER, SNDTIMEO, RCVTIMEO |
| **Transport/Network** | 네트워크/transport 정책 | SNDBUF, RCVBUF, TCP_*, TOS, CONNECT_TIMEOUT |
| **Protocol/Metadata** | 프로토콜 수준 메타데이터 | ZMP_METADATA, HEARTBEAT_* |

---

## 1. 메시지 큐 — SNDHWM / RCVHWM

| 항목 | 설명 |
|------|------|
| **하는 일** | pipe의 최대 메시지 수를 제한 |
| **적용 위치** | `pipe_t::check_write()` |
| **기본값** | `1000` (메시지 수) |
| **0** | 무제한 |
| **영향** | HWM 도달 시 block 또는 `EAGAIN` 반환. LWM 이하로 drain되면 복구 |

**LWM (Low Water Mark) 공식:** `(HWM + 1) / 2`

HWM=100이면 LWM=50. 큐가 100에서 block되고, 50 이하로 drain되어야 재개된다.
이 간격이 writable/non-writable 진동을 방지하는 히스테리시스다.

**소켓 타입별 차이:** 모든 소켓에 동일하게 적용. 서비스(SPOT)도
내부 소켓에 fan-out으로 적용.

=== "C"

    ```c
    int sndhwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &sndhwm, sizeof(sndhwm));
    int rcvhwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof(rcvhwm));
    ```

=== "C++"

    ```cpp
    socket.set_option(zlink::sndhwm, 5000);
    socket.set_option(zlink::rcvhwm, 5000);
    ```

=== "Java"

    ```java
    socket.setOption(SocketOptions.SNDHWM, 5000);
    socket.setOption(SocketOptions.RCVHWM, 5000);
    ```

=== "Python"

    ```python
    socket.options.send_high_water_mark = 5000
    socket.options.receive_high_water_mark = 5000
    ```

=== "Node/TypeScript"

    ```typescript
    socket.options.sendHwm = 5000;
    socket.options.recvHwm = 5000;
    ```

=== "C#/.NET"

    ```csharp
    socket.CommonOptions.SendHighWaterMark = 5000;
    socket.CommonOptions.ReceiveHighWaterMark = 5000;
    ```

=== "Rust"

    ```rust
    socket.set_send_hwm(5000)?;
    socket.set_recv_hwm(5000)?;
    ```

=== "Go"

    ```go
    socket.SetSendHWM(5000)
    socket.SetRecvHWM(5000)
    ```

---

## 2. 종료 대기 — LINGER

| 항목 | 설명 |
|------|------|
| **하는 일** | 종료 시 미전송 메시지 대기 시간 결정 |
| **적용 위치** | `session_base_t::process_term()` |
| **기본값** | 컨텍스트 상속 (`BLOCKY=1`이면 `-1`) |
| **-1** | 무한 대기 |
| **0** | 즉시 종료 — 미전송 메시지 폐기 |
| **>0** | 지정 시간(ms)까지 대기 후 강제 종료 |

**실제 동작:** linger > 0이면 타이머를 설정하고, 타이머 만료 시 pipe를
강제 종료한다. `pipe->terminate(linger != 0)`으로 delay 여부를 전달.

**소켓 타입별 차이:**
- `XSUB`, `SUB`: 생성 시 linger를 강제로 `0`으로 override (구독 소켓은
  종료 시 대기할 필요 없음)

=== "C"

    ```c
    /* 종료 시 미전송 메시지를 최대 1초 대기 */
    int linger = 1000;
    zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    ```

=== "C++"

    ```cpp
    socket.set_option(zlink::linger, 1000);
    ```

=== "Java"

    ```java
    socket.setOption(SocketOptions.LINGER, 1000);
    ```

=== "Python"

    ```python
    socket.options.linger_ms = 1000
    ```

=== "Node/TypeScript"

    ```typescript
    socket.options.linger = 1000;
    ```

=== "C#/.NET"

    ```csharp
    socket.CommonOptions.Linger = TimeSpan.FromMilliseconds(1000);
    ```

=== "Rust"

    ```rust
    socket.set_linger(Duration::from_millis(1000))?;
    ```

=== "Go"

    ```go
    socket.SetLinger(1000 * time.Millisecond)
    ```

---

## 3. 타임아웃 — SNDTIMEO / RCVTIMEO

| 항목 | 설명 |
|------|------|
| **하는 일** | send/recv 최대 대기 시간 설정 |
| **적용 위치** | `zlink_send()` / `zlink_recv()` blocking 경로 |
| **기본값** | `-1` (무한 대기) |
| **0** | non-blocking과 동일 (즉시 반환) |
| **>0** | 지정 시간(ms)까지 대기 후 `EAGAIN` 반환 |

**서비스 적용:** SPOT에서 pub/sub 내부 소켓에 전파.

=== "C"

    ```c
    /* send/recv를 500ms 후 포기 */
    int sndtimeo = 500;
    zlink_set_option(socket, ZLINK_OPT_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
    int rcvtimeo = 500;
    zlink_set_option(socket, ZLINK_OPT_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
    ```

=== "C++"

    ```cpp
    socket.set_option(zlink::sndtimeo, 500);
    socket.set_option(zlink::rcvtimeo, 500);
    ```

=== "Java"

    ```java
    socket.setOption(SocketOptions.SNDTIMEO, 500);
    socket.setOption(SocketOptions.RCVTIMEO, 500);
    ```

=== "Python"

    ```python
    socket.options.send_timeout_ms = 500
    socket.options.receive_timeout_ms = 500
    ```

=== "Node/TypeScript"

    ```typescript
    socket.options.sendTimeout = 500;
    socket.options.recvTimeout = 500;
    ```

=== "C#/.NET"

    ```csharp
    socket.CommonOptions.SendTimeout = TimeSpan.FromMilliseconds(500);
    socket.CommonOptions.ReceiveTimeout = TimeSpan.FromMilliseconds(500);
    ```

=== "Rust"

    ```rust
    socket.set_send_timeout(Duration::from_millis(500))?;
    socket.set_recv_timeout(Duration::from_millis(500))?;
    ```

=== "Go"

    ```go
    socket.SetSendTimeout(500 * time.Millisecond)
    socket.SetRecvTimeout(500 * time.Millisecond)
    ```

---

## 4. 연결 타임아웃 — CONNECT_TIMEOUT

| 항목 | 설명 |
|------|------|
| **하는 일** | `connect()` 비동기 시도의 타임아웃 설정 |
| **적용 위치** | `asio_*_connecter` -- `add_connect_timer()` |
| **기본값** | `0` (비활성 -- OS TCP 타임아웃 의존) |
| **>0** | 지정 시간(ms) 내 미완료 시 reconnect |

**OS 타임아웃과의 관계:** TCP 스택 자체의 SYN 재전송 타임아웃(보통 ~2분)보다
짧게 설정하여 빠른 failover를 구현할 때 유용하다.

=== "C"

    ```c
    /* 3초 이내 미연결 시 재시도 */
    int timeout = 3000;
    zlink_set_option(socket, ZLINK_OPT_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    ```

=== "C++"

    ```cpp
    socket.set_option(zlink::connect_timeout, 3000);
    ```

=== "Java"

    ```java
    socket.setOption(SocketOptions.CONNECT_TIMEOUT, 3000);
    ```

=== "Python"

    ```python
    socket.set_option(zlink.SocketOption.CONNECT_TIMEOUT, 3000)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.options.connectTimeout = 3000;
    ```

=== "C#/.NET"

    ```csharp
    socket.CommonOptions.ConnectTimeout = TimeSpan.FromMilliseconds(3000);
    ```

=== "Rust"

    ```rust
    socket.set_connect_timeout(Duration::from_millis(3000))?;
    ```

=== "Go"

    ```go
    socket.SetConnectTimeout(3000 * time.Millisecond)
    ```

---

## 5. 재연결 — RECONNECT_IVL / RECONNECT_IVL_MAX

| 항목 | 설명 |
|------|------|
| **하는 일** | 재연결 시도 간격 제어 |
| **적용 위치** | 모든 connecter -- `get_new_reconnect_ivl()` |
| **RECONNECT_IVL 기본값** | `100` ms |
| **RECONNECT_IVL_MAX 기본값** | `0` (비활성 — 고정 간격 사용) |

**재연결 알고리즘:**
- `MAX == 0`: 고정 간격 + 랜덤 jitter (0 ~ `IVL`)
- `MAX > 0`: **지수 백오프** -- 매 실패마다 2배 증가, MAX에서 캡
  (예: 100 -> 200 -> 400 -> ... -> max)

**음수:** `RECONNECT_IVL < 0`이면 자동 재연결을 비활성화한다.

=== "C"

    ```c
    /* 지수 백오프: 초기 200ms, ��한 30s */
    int ivl = 200;
    zlink_set_option(socket, ZLINK_OPT_RECONNECT_IVL, &ivl, sizeof(ivl));
    int ivl_max = 30000;
    zlink_set_option(socket, ZLINK_OPT_RECONNECT_IVL_MAX, &ivl_max, sizeof(ivl_max));
    ```

=== "C++"

    ```cpp
    socket.set_option(zlink::reconnect_ivl, 200);
    socket.set_option(zlink::reconnect_ivl_max, 30000);
    ```

=== "Java"

    ```java
    socket.setOption(SocketOptions.RECONNECT_IVL, 200);
    socket.setOption(SocketOptions.RECONNECT_IVL_MAX, 30000);
    ```

=== "Python"

    ```python
    socket.set_option(zlink.SocketOption.RECONNECT_IVL, 200)
    socket.set_option(zlink.SocketOption.RECONNECT_IVL_MAX, 30000)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.options.reconnectInterval = 200;
    socket.options.reconnectIntervalMax = 30000;
    ```

=== "C#/.NET"

    ```csharp
    socket.SetOption(SocketOptions.ReconnectIvl, 200);
    socket.SetOption(SocketOptions.ReconnectIvlMax, 30000);
    ```

=== "Rust"

    ```rust
    socket.set_reconnect_interval(Duration::from_millis(200))?;
    socket.set_reconnect_interval_max(Duration::from_secs(30))?;
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionReconnectIvl, 200)
    socket.SetOption(zlink.OptionReconnectIvlMax, 30000)
    ```

---

## 6. TCP Keepalive — TCP_KEEPALIVE / TCP_KEEPALIVE_CNT / TCP_KEEPALIVE_IDLE / TCP_KEEPALIVE_INTVL

| 옵션 | 하는 일 | 기본값 |
|------|---------|--------|
| `TCP_KEEPALIVE` | SO_KEEPALIVE 활성화/비활성화 | `-1` (OS 기본) |
| `TCP_KEEPALIVE_CNT` | keepalive probe 실패 횟수 제한 | `-1` (OS 기본) |
| `TCP_KEEPALIVE_IDLE` | 첫 probe까지 유휴 시간 (초) | `-1` (OS 기본) |
| `TCP_KEEPALIVE_INTVL` | probe 사이 간격 (초) | `-1` (OS 기본) |

**적용 위치:** `tcp.cpp`의 `tune_tcp_keepalives()` -- OS socket option 전달.

**`-1`의 의미:** "이 값을 변경하지 않는다" — OS 기본 keepalive 설정을 유지.

**TCP에만 적용.** IPC, inproc, WebSocket에는 해당 없음.

**권장 설정 예:**

=== "C"

    ```c
    // 60초 유휴 후 10초 간격으로 3회 probe, 실패 시 연결 끊김
    zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE, &(int){1}, sizeof(int));
    zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_IDLE, &(int){60}, sizeof(int));
    zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_INTVL, &(int){10}, sizeof(int));
    zlink_set_option(s, ZLINK_OPT_TCP_KEEPALIVE_CNT, &(int){3}, sizeof(int));
    ```

=== "C++"

    ```cpp
    // 60초 유휴 후 10초 간격으로 3회 probe, 실패 시 연결 끊김
    s.set_option(ZLINK_OPT_TCP_KEEPALIVE, 1);
    s.set_option(ZLINK_OPT_TCP_KEEPALIVE_IDLE, 60);
    s.set_option(ZLINK_OPT_TCP_KEEPALIVE_INTVL, 10);
    s.set_option(ZLINK_OPT_TCP_KEEPALIVE_CNT, 3);
    ```

=== "Java"

    ```java
    // 60초 유휴 후 10초 간격으로 3회 probe, 실패 시 연결 끊김
    s.setOption(Option.TCP_KEEPALIVE, 1);
    s.setOption(Option.TCP_KEEPALIVE_IDLE, 60);
    s.setOption(Option.TCP_KEEPALIVE_INTVL, 10);
    s.setOption(Option.TCP_KEEPALIVE_CNT, 3);
    ```

=== "Python"

    ```python
    # 60초 유휴 후 10초 간격으로 3회 probe, 실패 시 연결 끊김
    s.set_option(zlink.Option.TCP_KEEPALIVE, 1)
    s.set_option(zlink.Option.TCP_KEEPALIVE_IDLE, 60)
    s.set_option(zlink.Option.TCP_KEEPALIVE_INTVL, 10)
    s.set_option(zlink.Option.TCP_KEEPALIVE_CNT, 3)
    ```

=== "Node/TypeScript"

    ```typescript
    // 60초 유휴 후 10초 간격으로 3회 probe, 실패 시 연결 끊김
    s.setOption(zlink.Option.TCP_KEEPALIVE, 1);
    s.setOption(zlink.Option.TCP_KEEPALIVE_IDLE, 60);
    s.setOption(zlink.Option.TCP_KEEPALIVE_INTVL, 10);
    s.setOption(zlink.Option.TCP_KEEPALIVE_CNT, 3);
    ```

=== "C#/.NET"

    ```csharp
    // 60초 유휴 후 10초 간격으로 3회 probe, 실패 시 연결 끊김
    s.SetOption(Option.TcpKeepalive, 1);
    s.SetOption(Option.TcpKeepaliveIdle, 60);
    s.SetOption(Option.TcpKeepaliveIntvl, 10);
    s.SetOption(Option.TcpKeepaliveCnt, 3);
    ```

=== "Rust"

    ```rust
    // 60초 유휴 후 10초 간격으로 3회 probe, 실패 시 연결 끊김
    s.set_option(zlink::Option::TcpKeepalive, 1)?;
    s.set_option(zlink::Option::TcpKeepaliveIdle, 60)?;
    s.set_option(zlink::Option::TcpKeepaliveIntvl, 10)?;
    s.set_option(zlink::Option::TcpKeepaliveCnt, 3)?;
    ```

=== "Go"

    ```go
    // 60초 유휴 후 10초 간격으로 3회 probe, 실패 시 연결 끊김
    s.SetOption(zlink.OptionTcpKeepalive, 1)
    s.SetOption(zlink.OptionTcpKeepaliveIdle, 60)
    s.SetOption(zlink.OptionTcpKeepaliveIntvl, 10)
    s.SetOption(zlink.OptionTcpKeepaliveCnt, 3)
    ```

---

## 7. TCP 재전송 — TCP_MAXRT

| 항목 | 설명 |
|------|------|
| **하는 일** | TCP 재전송 최대 타임아웃 설정 |
| **적용 위치** | `tcp.cpp` -- `setsockopt(TCP_USER_TIMEOUT)` |
| **기본값** | `0` (비활성 -- OS 기본값 사용) |
| **>0** | 지정 시간(ms) 이상 실패 시 연결 포기 |

`TCP_USER_TIMEOUT` 커널 옵션이 있는 시스템(Linux 2.6.37+)에서만 동작한다.
Keepalive보다 빠른 dead peer 감지가 필요할 때 사용.

---

## 8. Nagle 알고리즘 — TCP_NODELAY

| 항목 | 설명 |
|------|------|
| **하는 일** | Nagle 알고리즘 비활성화 (즉시 전송) |
| **적용 위치** | `tcp.cpp` — `setsockopt(TCP_NODELAY)` |
| **기본값** | `1` (TCP_NODELAY 활성 = Nagle 비활성) |

**`1` (기본, 권장):** 작은 메시지도 지연 없이 즉시 전송. 메시징 시스템에 적합.
**`0`:** Nagle 활성 — 작은 패킷을 모아서 전송. 대역폭 효율은 높지만 지연 증가.

---

## 9. ZMP 하트비트 — HEARTBEAT_IVL / HEARTBEAT_TTL / HEARTBEAT_TIMEOUT

| 옵션 | 하는 일 | 기본값 |
|------|---------|--------|
| `HEARTBEAT_IVL` | PING 메시지 송신 간격 (ms) | `0` (비활성) |
| `HEARTBEAT_TTL` | 원격 피어에 전달되는 TTL (0.1초 단위) | `0` |
| `HEARTBEAT_TIMEOUT` | PONG 응답 대기 시간 (ms) | `-1` (IVL 값 사용) |

**적용 위치:** `asio_zmp_engine` -- ZMP 프로토콜 수준 PING/PONG 교환.

**동작 흐름:**
1. `IVL` 간격마다 PING 전송 (TTL 값 포함)
2. 원격 피어는 TTL 시간 안에 메시지/PONG을 받지 못하면 연결 종료
3. 로컬에서는 TIMEOUT 시간 안에 PONG을 받지 못하면 연결 끊김 감지

**TCP Keepalive와의 차이:** TCP keepalive는 OS 수준 프로브이고,
ZMP 하트비트는 애플리케이션 프로토콜 수준이다. 둘 다 설정하면 더 빠른 쪽이
먼저 감지한다.

=== "C"

    ```c
    /* 5초마다 PING, 원격 TTL 15초, 로컬 PONG 타임아웃 10초 */
    int hb_ivl = 5000;
    zlink_set_option(socket, ZLINK_OPT_HEARTBEAT_IVL, &hb_ivl, sizeof(hb_ivl));
    int hb_ttl = 150;  /* 0.1초 단위 → 15초 */
    zlink_set_option(socket, ZLINK_OPT_HEARTBEAT_TTL, &hb_ttl, sizeof(hb_ttl));
    int hb_timeout = 10000;
    zlink_set_option(socket, ZLINK_OPT_HEARTBEAT_TIMEOUT, &hb_timeout, sizeof(hb_timeout));
    ```

=== "C++"

    ```cpp
    socket.set_option(zlink::heartbeat_ivl, 5000);
    socket.set_option(zlink::heartbeat_ttl, 150);      // 0.1초 단위 → 15초
    socket.set_option(zlink::heartbeat_timeout, 10000);
    ```

=== "Java"

    ```java
    socket.setOption(SocketOptions.HEARTBEAT_IVL, 5000);
    socket.setOption(SocketOptions.HEARTBEAT_TTL, 150);      // 0.1초 단위 → 15초
    socket.setOption(SocketOptions.HEARTBEAT_TIMEOUT, 10000);
    ```

=== "Python"

    ```python
    socket.set_option(zlink.SocketOption.HEARTBEAT_IVL, 5000)
    socket.set_option(zlink.SocketOption.HEARTBEAT_TTL, 150)      # 0.1초 단위 → 15초
    socket.set_option(zlink.SocketOption.HEARTBEAT_TIMEOUT, 10000)
    ```

=== "Node/TypeScript"

    ```typescript
    socket.options.heartbeatInterval = 5000;
    socket.options.heartbeatTtl = 150;      // 0.1초 단위 → 15초
    socket.options.heartbeatTimeout = 10000;
    ```

=== "C#/.NET"

    ```csharp
    socket.SetOption(SocketOptions.HeartbeatIvl, 5000);
    socket.SetOption(SocketOptions.HeartbeatTtl, 150);      // 0.1초 단위 → 15초
    socket.SetOption(SocketOptions.HeartbeatTimeout, 10000);
    ```

=== "Rust"

    ```rust
    socket.set_heartbeat_interval(Duration::from_secs(5))?;
    socket.set_heartbeat_ttl(Duration::from_secs(15))?;
    socket.set_heartbeat_timeout(Duration::from_secs(10))?;
    ```

=== "Go"

    ```go
    socket.SetOption(zlink.OptionHeartbeatIvl, 5000)
    socket.SetOption(zlink.OptionHeartbeatTtl, 150)      // 0.1초 단위 → 15초
    socket.SetOption(zlink.OptionHeartbeatTimeout, 10000)
    ```

---

## 10. 즉시 연결 — IMMEDIATE

| 항목 | 설명 |
|------|------|
| **하는 일** | 즉시 attach vs 연결 완료 후 attach |
| **적용 위치** | `socket_base_endpoint.cpp` |
| **기본값** | `0` (즉시 attach) |

**`0` (기본):** connect() 호출 즉시 pipe를 attach. 연결 완료 전에도 `send()`가
가능하고, 메시지는 큐에 쌓인다.

**`1`:** 연결이 실제로 완료된 후에만 pipe가 attach된다. 연결 전 `send()`는
block되거나 `EAGAIN`을 반환한다. 또한 hiccup(일시적 연결 끊김) 시 pipe가 즉시
제거된다.

---

## 11. 최신 값만 유지 — CONFLATE

| 항목 | 설명 |
|------|------|
| **하는 일** | 최근 메시지 1개만 유지, 이전 폐기 |
| **적용 위치** | `pipe.cpp` -- `ypipe_conflate_t` |
| **기본값** | `0` (비활성) |
| **유효 소켓** | `DEALER`, `PUB`, `SUB`에서만 동작 |

활성화 시 HWM 설정은 무시된다. 멀티파트 메시지는 conflate 모드에서 수신할 수
없다. 센서 데이터처럼 "최신 값만 의미 있는" 시나리오에 적합.

---

## 12. OS 소켓 버퍼 — SNDBUF / RCVBUF

| 항목 | 설명 |
|------|------|
| **하는 일** | 커널 소켓 송수신 버퍼 크기 설정 |
| **적용 위치** | `tcp.cpp` -- `setsockopt(SO_SNDBUF/SO_RCVBUF)` |
| **기본값** | `-1` (OS 기본값 유지) |
| **0** | OS 기본값 사용 |
| **>0** | 지정 크기(바이트)로 설정 |

HWM과 독립적이다. HWM은 zlink pipe 수준의 메시지 수 제한이고,
SNDBUF/RCVBUF는 OS 커널 소켓 버퍼의 바이트 크기이다.

**소켓 타입별 차이:**
- `STREAM`: 미설정(`<0`)이면 `262144` (256KB)로 자동 override

---

## 13. IP 서비스 품질 — TOS

| 항목 | 설명 |
|------|------|
| **하는 일** | IP TOS (DSCP/ECN) 필드 설정 |
| **적용 위치** | TCP 소켓 설정 시 `setsockopt(IP_TOS)` |
| **기본값** | `0` (best-effort) |

QoS 정책이 있는 네트워크에서 트래픽 우선순위를 지정할 때 사용.

---

## 14. 연결 대기열 — BACKLOG

| 항목 | 설명 |
|------|------|
| **하는 일** | `listen()` accept 대기열 최대 길이 |
| **적용 위치** | `asio_tcp_listener.cpp` |
| **기본값** | `100` |

**소켓 타입별 차이:**
- `STREAM`: `65536`으로 자동 override (다수 외부 클라이언트 수용)

---

## 15. I/O 스레드 어피니티 — AFFINITY

| 항목 | 설명 |
|------|------|
| **하는 일** | 소켓을 특정 I/O 스레드에 할당 |
| **적용 위치** | `socket_base` -- `choose_io_thread()` |
| **기본값** | `0` (모든 I/O 스레드 사용 가능) |
| **타입** | `uint64_t` 비트마스크 |

비트 N이 1이면 I/O 스레드 N을 사용 가능. `0`은 모든 스레드 허용.
I/O 스레드가 여러 개(`ZLINK_IO_THREADS > 1`)일 때 특정 소켓을 특정 스레드에
고정하여 CPU 친화성을 높일 수 있다.

---

## 16. 최대 메시지 크기 — MAXMSGSIZE

| 항목 | 설명 |
|------|------|
| **하는 일** | 수신 메시지 최대 크기 제한 |
| **적용 위치** | 세션/엔진 수준 메시지 크기 검증 |
| **기본값** | `-1` (무제한) |
| **>0** | 지정 크기(바이트) 초과 메시지 거부 |

신뢰할 수 없는 피어로부터의 OOM 공격을 방지하는 데 유용.

---

## 17. IPv6 — IPV6

| 항목 | 설명 |
|------|------|
| **하는 일** | IPv6 활성화 (IPv4와 동시 사용) |
| **적용 위치** | `asio_tcp_connecter`, `asio_tcp_listener` |
| **기본값** | `0` (IPv4 전용, 컨텍스트 상속) |

`1`로 설정하면 `IPV6_V6ONLY=0`인 dual-stack 소켓을 생성한다.

---

## 18. 멀티캐스트 — MULTICAST_HOPS / MULTICAST_MAXTPDU

| 옵션 | 하는 일 | 기본값 |
|------|---------|--------|
| `MULTICAST_HOPS` | 멀티캐스트 패킷 TTL | `1` |
| `MULTICAST_MAXTPDU` | 최대 transport 데이터 유닛 크기 (바이트) | `1500` |

PGM transport에서만 적용. 현재 PGM은 임시 비활성화 상태.

---

## 19. 구독 매칭 반전 — INVERT_MATCHING

| 항목 | 설명 |
|------|------|
| **하는 일** | 구독 필터 매칭 결과 반전 |
| **적용 위치** | `xsub.cpp` |
| **기본값** | `0` (정상 매칭) |

`1`로 설정하면 구독하지 않은 토픽의 메시지를 받고, 구독한 토픽은 받지 않는다.

---

## 20. 네트워크 인터페이스 바인딩 — BINDTODEVICE

| 항목 | 설명 |
|------|------|
| **하는 일** | 특정 네트워크 인터페이스에 바인딩 |
| **적용 위치** | `tcp.cpp` -- `setsockopt(SO_BINDTODEVICE)` |
| **기본값** | 빈 문자열 (바인딩 없음) |

Linux `SO_BINDTODEVICE` 지원 시스템에서만 동작. 멀티호밍 서버에서 특정 NIC에
트래픽을 제한할 때 사용.

---

## 21. 핸드셰이크 타임아웃 — HANDSHAKE_IVL

| 항목 | 설명 |
|------|------|
| **하는 일** | ZMP 핸드셰이크 최대 시간 설정 |
| **적용 위치** | ZMP 엔진 핸드셰이크 타이머 |
| **기본값** | `30000` ms (30초) |
| **0** | 핸드셰이크 타임아웃 비활성 |

핸드셰이크가 이 시간 안에 완료되지 않으면 연결이 닫힌다.

---

## 22. ZMP 메타데이터 — ZMP_METADATA

| 항목 | 설명 |
|------|------|
| **하는 일** | ZMP 프로토콜 메타데이터 속성 첨부 |
| **적용 위치** | `asio_zmp_engine` READY 프레임 |
| **기본값** | `0` (비활성) |
| **타입** | binary |

---

## 소켓 타입별 기본값 override

일부 소켓 타입은 생성 시 공통 기본값을 자체적으로 override한다.

| 소켓 타입 | override 옵션 | 값 | 이유 |
|-----------|---------------|-----|------|
| `SUB` / `XSUB` | `LINGER` | `0` | 구독 소켓은 종료 시 대기 불필요 |
| `STREAM` | `BACKLOG` | `65536` | 다수 외부 클라이언트 수용 |
| `STREAM` | `SNDBUF` | `262144` (미설정 시) | 대용량 RAW 전송 대응 |
| `STREAM` | `RCVBUF` | `262144` (미설정 시) | 대용량 RAW 수신 대응 |

## 소켓 타입별 전용 옵션

공통 옵션 외에 소켓 타입별 전용 옵션은 전용 API로 설정한다.

| 소켓 | API | 대표 옵션 |
|------|-----|-----------|
| ROUTER | `zlink_set_router_option()` | `MANDATORY`, `HANDOVER`, `PROBE`, `CONNECT_ROUTING_ID` |
| DEALER | `zlink_set_dealer_option()` | `PROBE` |
| XPUB | `zlink_set_pub_option()` | `VERBOSE`, `VERBOSER`, `NODROP`, `MANUAL`, `WELCOME_MSG` |
| SUB/XSUB | `zlink_set_sub_option()` | 구독 관련 |
| STREAM | `zlink_set_stream_option()` | `NOTIFY` |

---
[← 스레드 안전성](11-thread-safety.ko.md)
