# ZLINK STREAM 소켓 상세 스펙

- 작성일: 2026-02-14
- 기준 소스:
  - `core/src/sockets/socket_base.cpp`
  - `core/src/sockets/stream.cpp`
  - `core/src/transports/*/asio_*_connecter.cpp`
- 목적: STREAM 소켓의 현재 구현/정책을 서버 전용 기준으로 확정

---

## 1. 스펙 범위

본 스펙은 `ZLINK_STREAM`의 **현재 기준 동작**을 정의한다.

핵심 정책:
- STREAM은 **서버 전용 소켓**이다.
- STREAM 소켓의 `connect`(클라이언트 경로)는 지원하지 않는다.
- 외부 클라이언트는 OS raw TCP/TLS/WS/WSS 클라이언트로 접속한다.

---

## 2. 핵심 정의

| 항목 | 값 |
|---|---|
| 소켓 타입 ID | `ZLINK_STREAM = 11` |
| 역할 | 서버 전용 (bind/listen/accept) |
| wire 프로토콜 | `4-byte length (big-endian) + body` |
| API 메시지 형태 | `[routing_id(4B)][payload]` 2프레임 |
| routing_id 크기 | 고정 4바이트 (`uint32`) |
| 이벤트 표현 | payload `0x01`(connect), `0x00`(disconnect) |
| 지원 transport(서버) | `tcp`, `tls`, `ws`, `wss` |

유효 조합:
- `STREAM(server)` ↔ `external raw client`

비유효 조합:
- `STREAM` ↔ `STREAM`
- `STREAM` ↔ zlink 내부 소켓(`PAIR/PUB/SUB/DEALER/ROUTER/...`)

---

## 3. API 동작 규칙

### 3.1 생성/바인드

```c
void *s = zlink_socket(ctx, ZLINK_STREAM);
zlink_bind(s, "tcp://0.0.0.0:8080");
```

### 3.2 connect 금지

```c
void *s = zlink_socket(ctx, ZLINK_STREAM);
int rc = zlink_connect(s, "tcp://127.0.0.1:8080");
// rc == -1, errno == EOPNOTSUPP
```

정책 근거:
- STREAM은 서버 역할만 수행한다.
- 클라이언트 역할은 raw client 스택으로 분리한다.

### 3.3 송수신 규칙

수신:
1. frame0: `routing_id`(4B)
2. frame1: `payload`

송신:
1. frame0: `routing_id`(4B, `ZLINK_SNDMORE`)
2. frame1: `payload`

---

## 4. wire 프로토콜

네트워크 wire 형식:

```
+----------------------+-------------------+
| body_len (4B, BE)    | body (N bytes)    |
+----------------------+-------------------+
```

- `body_len`은 payload 길이
- 엔디안은 big-endian 고정
- ws/wss/tls도 동일 payload 모델을 유지

---

## 5. routing_id 정책

### 5.1 자동 할당

- 서버가 연결 단위로 `uint32` routing_id를 자동 할당
- API 노출은 4바이트 big-endian

### 5.2 `ZLINK_CONNECT_ROUTING_ID` 정책

- STREAM에서 `ZLINK_CONNECT_ROUTING_ID` 설정은 **비지원**
- `zlink_setsockopt(stream, ZLINK_CONNECT_ROUTING_ID, ...)` -> `EOPNOTSUPP`

즉, STREAM은 connect-side alias 기반 routing_id 수동 지정 경로를 갖지 않는다.

---

## 6. 옵션 정책

### 6.1 지원 옵션(주요)

- `ZLINK_MAXMSGSIZE`
- `ZLINK_SNDHWM`, `ZLINK_RCVHWM`
- `ZLINK_SNDBUF`, `ZLINK_RCVBUF`
- `ZLINK_BACKLOG`
- `ZLINK_LINGER`
- `ZLINK_RCVTIMEO`, `ZLINK_SNDTIMEO`
- `ZLINK_TLS_CERT`, `ZLINK_TLS_KEY`, `ZLINK_TLS_CA`, `ZLINK_TLS_HOSTNAME`, `ZLINK_TLS_TRUST_SYSTEM`

### 6.2 비지원/변경 옵션

- `ZLINK_CONNECT_ROUTING_ID` (STREAM에서는 비지원, `EOPNOTSUPP`)

---

## 7. 이벤트 모델

payload 1바이트 특수 이벤트:
- `0x01`: connect
- `0x00`: disconnect

서버는 connect/disconnect 이벤트를 읽어 세션 상태를 관리해야 한다.

---

## 8. 구현 반영 포인트

### 8.1 connect 차단

- `core/src/sockets/socket_base.cpp`
  - `connect_internal()`에서 `options.type == ZLINK_STREAM` 시 `EOPNOTSUPP`

### 8.2 STREAM 내 connect-routing-id 제거

- `core/src/sockets/stream.cpp`
  - `xsetsockopt(ZLINK_CONNECT_ROUTING_ID)` -> `EOPNOTSUPP`
  - `identify_peer()`는 자동 4B ID만 사용

### 8.3 transport connecter의 STREAM client 경로 차단

- `core/src/transports/tcp/asio_tcp_connecter.cpp`
- `core/src/transports/ipc/asio_ipc_connecter.cpp`
- `core/src/transports/tls/asio_tls_connecter.cpp`
- `core/src/transports/ws/asio_ws_connecter.cpp`

위 connecter들은 STREAM type으로 들어오면 엔진 attach 없이 종료한다.

---

## 9. 테스트 기준

### 9.1 단위 테스트

- `core/tests/test_stream_socket.cpp`
- `core/tests/test_stream_fastpath.cpp`
- `core/tests/routing-id/test_connect_rid_string_alias.cpp`
- `core/tests/routing-id/test_stream_routing_id_size.cpp`

모두 STREAM 서버 + raw client 시나리오를 사용한다.

### 9.2 시나리오 테스트

- `core/tests/scenario/stream/zlink/run_stream_scenarios.sh`
- `core/tests/scenario/stream/zlink/test_scenario_stream_zlink.cpp`

고정 파라미터:
- `ccu=10000`, `size=1024`, `inflight=30`
- `warmup=3`, `measure=10`, `drain-timeout=10`
- `connect-concurrency=256`, `backlog=32768`, `hwm=1000000`
- `io_threads=32`, `latency_sample_rate=16`

---

## 10. 마이그레이션 가이드

기존 STREAM client 사용 코드가 있다면 아래 순서로 교체한다.

1. `zlink_socket(..., ZLINK_STREAM)` + `zlink_connect(...)` 제거
2. raw client(TCP/TLS/WS/WSS) 구현으로 대체
3. 서버 응답은 기존처럼 `rid + payload` 2프레임 유지
4. `ZLINK_CONNECT_ROUTING_ID` 의존 코드 제거

---

## 11. 기존 스펙 대비 변경 요약

변경/제거된 항목:
- `STREAM ↔ STREAM` 유효 조합 삭제
- STREAM의 `connect` 경로 삭제 (`EOPNOTSUPP`)
- STREAM의 `ZLINK_CONNECT_ROUTING_ID` 지원 삭제
- 테스트/시나리오의 STREAM 클라이언트 경로를 raw client 경로로 정렬

유지된 항목:
- wire 포맷(`4B length + body`)
- API 2프레임 형식(`[routing_id][payload]`)
- connect/disconnect 이벤트(`0x01/0x00`)

