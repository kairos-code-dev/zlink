[English](socket-option-defaults.md) | [한국어](socket-option-defaults.ko.md)

# 소켓 옵션 기본값 (코드 기준)

이 문서는 소켓 타입별로 각 소켓 옵션의 기본값을 정리한다. 일부 소켓 타입은 고유한 동작 특성에 맞게 전역 기본값을 재정의한다.

이 문서는 예제/가이드가 아니라 실제 구현 코드 기준의 기본값을 정리한다.

- `core/src/core/options.cpp` (`options_t` 생성자)
- `core/src/core/options_core_socket.cpp` (core socket 옵션 dispatch)
- `core/src/core/options_transport_network.cpp` (transport/network 옵션 dispatch)
- `core/src/core/options_protocol_metadata.cpp` (protocol/metadata 옵션 dispatch)
- `core/src/sockets/socket_base_lifecycle.cpp` (컨텍스트 상속 기본값)
- `core/src/sockets/*.cpp` (소켓 타입별 override)

각 옵션의 상세 동작과 영향 범위는
[소켓 옵션 상세 가이드](../guide/12-socket-options.ko.md)를 참고한다.

## 1. 공통 기본값 (별도 override가 없을 때)

| 옵션 | 기본값 | 비고 |
|---|---:|---|
| `ZLINK_OPT_AFFINITY` | `0` | I/O 스레드 어피니티 미설정 |
| `zlink_get_routing_id()` | 자동 | 소켓별 16바이트 랜덤 ID (`zlink_set_routing_id()`로 설정) |
| `ZLINK_OPT_SNDHWM` | `1000` | 송신 큐 HWM |
| `ZLINK_OPT_RCVHWM` | `1000` | 수신 큐 HWM |
| `ZLINK_OPT_RATE` | `100` | 멀티캐스트 rate (kb/s) |
| `ZLINK_OPT_RECOVERY_IVL` | `10000` | 멀티캐스트 recovery interval (ms) |
| `ZLINK_OPT_SNDBUF` | `-1` | `SO_SNDBUF` 강제 설정 안 함 |
| `ZLINK_OPT_RCVBUF` | `-1` | `SO_RCVBUF` 강제 설정 안 함 |
| `ZLINK_OPT_TOS` | `0` | ToS/DSCP override 없음 |
| `ZLINK_OPT_LINGER` | 컨텍스트 상속 | `ZLINK_OPT_BLOCKY=1`(기본)면 `-1`, 아니면 `0` |
| `ZLINK_OPT_CONNECT_TIMEOUT` | `0` | 비활성 |
| `ZLINK_OPT_TCP_MAXRT` | `0` | 비활성 |
| `ZLINK_OPT_RECONNECT_IVL` | `100` | 초기 재연결 간격 (ms) |
| `ZLINK_OPT_RECONNECT_IVL_MAX` | `0` | 최대 재연결 간격 비활성 |
| `ZLINK_OPT_BACKLOG` | `100` | listener backlog |
| `ZLINK_OPT_MAXMSGSIZE` | `-1` | 무제한 |
| `ZLINK_OPT_MULTICAST_HOPS` | `1` | 멀티캐스트 TTL |
| `ZLINK_OPT_MULTICAST_MAXTPDU` | `1500` | 멀티캐스트 max TPDU |
| `ZLINK_OPT_RCVTIMEO` | `-1` | 수신 타임아웃 무한 |
| `ZLINK_OPT_SNDTIMEO` | `-1` | 송신 타임아웃 무한 |
| `ZLINK_OPT_IPV6` | 컨텍스트 상속 | `zlink_ctx_get(ctx, ZLINK_IPV6)` 상속 (기본 `0`) |
| `ZLINK_OPT_IMMEDIATE` | `0` | connecting pipe 즉시 attach |
| `ZLINK_OPT_CONFLATE` | `0` | 비활성 |
| `ZLINK_OPT_INVERT_MATCHING` | `0` | 비활성 |
| `ZLINK_STREAM_OPT_NOTIFY` | `0` | 비활성 |
| `ZLINK_OPT_HEARTBEAT_IVL` | `0` | 비활성 |
| `ZLINK_OPT_HEARTBEAT_TTL` | `0` | 비활성 |
| `ZLINK_OPT_HEARTBEAT_TIMEOUT` | `-1` | heartbeat 활성 시 interval fallback |
| `ZLINK_OPT_TCP_KEEPALIVE` | `-1` | OS 기본값 |
| `ZLINK_OPT_TCP_KEEPALIVE_CNT` | `-1` | OS 기본값 |
| `ZLINK_OPT_TCP_KEEPALIVE_IDLE` | `-1` | OS 기본값 |
| `ZLINK_OPT_TCP_KEEPALIVE_INTVL` | `-1` | OS 기본값 |
| `ZLINK_OPT_TCP_NODELAY` | `1` | 기본 활성 |
| `ZLINK_OPT_BINDTODEVICE` | 빈 문자열 | 디바이스 바인딩 없음 |
| `ZLINK_OPT_HANDSHAKE_IVL` | `30000` | ZMP 핸드셰이크 타임아웃 (ms, 0 = 비활성) |
| `ZLINK_OPT_PRIORITY` | `0` | 소켓 우선순위 |
| `ZLINK_OPT_BUSY_POLL` | `0` | busy-poll 비활성 |
| `ZLINK_OPT_MONITOR_EVENT_VERSION` | `1` | 모니터 이벤트 버전 |
| `ZLINK_OPT_IN_BATCH_SIZE` | `8192` | 수신 배치 크기 (바이트) |
| `ZLINK_OPT_OUT_BATCH_SIZE` | `8192` | 송신 배치 크기 (바이트) |
| `ZLINK_OPT_ZERO_COPY` | `1` | 제로카피 활성 |
| `ZLINK_OPT_ZMP_METADATA` | `0` | 비활성 |

## 2. 소켓 타입별 기본값 / Override

| 소켓 타입 | 옵션/동작 | 기본값 |
|---|---|---|
| `ZLINK_DEALER` | `ZLINK_DEALER_OPT_PROBE` | `0` |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_OPT_MANDATORY` | `1` (router_option API로 설정) |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_OPT_PROBE` | `0` |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_OPT_HANDOVER` | `1` |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_VERBOSE` | `0` (pub_option API로 설정) |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_VERBOSER` | `0` |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_NODROP` | `1` (`_lossy=false`) |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_MANUAL` | `0` |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_MANUAL_LAST_VALUE` | `0` |
| `ZLINK_XPUB` | `ZLINK_PUB_OPT_WELCOME_MSG` | 빈 값 |
| `ZLINK_XSUB` | `ZLINK_OPT_LINGER` override | 강제로 `0` |
| `ZLINK_SUB` | `ZLINK_OPT_LINGER` override | 강제로 `0` (XSUB 생성자 상속) |
| `ZLINK_SUB` | 구독 집합 | 생성 시 빈 상태 |
| `ZLINK_STREAM` | `ZLINK_OPT_BACKLOG` override | `65536` |
| `ZLINK_STREAM` | `ZLINK_OPT_SNDBUF` override | 미설정(` <0`)이면 `262144` |
| `ZLINK_STREAM` | `ZLINK_OPT_RCVBUF` override | 미설정(` <0`)이면 `262144` |
| `ZLINK_STREAM` | `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | 미지원 (`ENOTSUP`) |

## 3. 읽기 전용 옵션의 초기 상태값

| 옵션 | 초기값 |
|---|---|
| `ZLINK_OPT_LAST_ENDPOINT` | 빈 문자열 |
| `ZLINK_PUB_OPT_TOPICS_COUNT` (XPUB) | `0` |
| `ZLINK_SUB_OPT_TOPICS_COUNT` (XSUB/SUB) | `0` |

## 4. TLS 옵션 기본값 (TLS 빌드 시)

> **참고:** 공개 API에서 TLS는 `zlink_set_tls_server()` / `zlink_set_tls_client()`로
> 설정합니다. 아래 상수는 내부 기본값입니다.

| 옵션 (내부) | 기본값 |
|---|---:|
| `ZLINK_TLS_VERIFY` | `1` |
| `ZLINK_TLS_REQUIRE_CLIENT_CERT` | `0` |
| `ZLINK_TLS_TRUST_SYSTEM` | `1` |
| `ZLINK_TLS_CERT` | 빈 문자열 |
| `ZLINK_TLS_KEY` | 빈 문자열 |
| `ZLINK_TLS_CA` | 빈 문자열 |
| `ZLINK_TLS_HOSTNAME` | 빈 문자열 |
| `ZLINK_TLS_PASSWORD` | 빈 문자열 |

## 5. 주의 사항

- `ZLINK_OPT_BLOCKY`는 소켓 옵션이 아니라 컨텍스트 옵션(`zlink_ctx_set/get`)이다.
- `ZLINK_OPT_CONFLATE`는 `DEALER`, `PUB`, `SUB`에서만 실질적으로 동작한다.
- STREAM의 추가 런타임 튜닝 항목은 `stream-socket.ko.md`를 참고한다.
