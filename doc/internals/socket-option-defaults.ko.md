[English](socket-option-defaults.md) | [한국어](socket-option-defaults.ko.md)

# 소켓 옵션 기본값 (코드 기준)

이 문서는 예제/가이드가 아니라 실제 구현 코드 기준의 기본값을 정리한다.

- `core/src/core/options.cpp` (`options_t` 생성자)
- `core/src/sockets/socket_base.cpp` (컨텍스트 상속 기본값)
- `core/src/sockets/*.cpp` (소켓 타입별 override)

## 1. 공통 기본값 (별도 override가 없을 때)

| 옵션 | 기본값 | 비고 |
|---|---:|---|
| `ZLINK_AFFINITY` | `0` | I/O 스레드 어피니티 미설정 |
| `ZLINK_ROUTING_ID` | 자동 | 소켓별 16바이트 랜덤 ID |
| `ZLINK_SNDHWM` | `300000` | 송신 큐 HWM |
| `ZLINK_RCVHWM` | `300000` | 수신 큐 HWM |
| `ZLINK_RATE` | `100` | 멀티캐스트 rate (kb/s) |
| `ZLINK_RECOVERY_IVL` | `10000` | 멀티캐스트 recovery interval (ms) |
| `ZLINK_SNDBUF` | `-1` | `SO_SNDBUF` 강제 설정 안 함 |
| `ZLINK_RCVBUF` | `-1` | `SO_RCVBUF` 강제 설정 안 함 |
| `ZLINK_TOS` | `0` | ToS/DSCP override 없음 |
| `ZLINK_LINGER` | 컨텍스트 상속 | `ZLINK_BLOCKY=1`(기본)면 `-1`, 아니면 `0` |
| `ZLINK_CONNECT_TIMEOUT` | `0` | 비활성 |
| `ZLINK_TCP_MAXRT` | `0` | 비활성 |
| `ZLINK_RECONNECT_IVL` | `100` | 초기 재연결 간격 (ms) |
| `ZLINK_RECONNECT_IVL_MAX` | `0` | 최대 재연결 간격 비활성 |
| `ZLINK_BACKLOG` | `100` | listener backlog |
| `ZLINK_MAXMSGSIZE` | `-1` | 무제한 |
| `ZLINK_MULTICAST_HOPS` | `1` | 멀티캐스트 TTL |
| `ZLINK_MULTICAST_MAXTPDU` | `1500` | 멀티캐스트 max TPDU |
| `ZLINK_RCVTIMEO` | `-1` | 수신 타임아웃 무한 |
| `ZLINK_SNDTIMEO` | `-1` | 송신 타임아웃 무한 |
| `ZLINK_IPV6` | 컨텍스트 상속 | `zlink_ctx_get(ctx, ZLINK_IPV6)` 상속 (기본 `0`) |
| `ZLINK_IMMEDIATE` | `0` | connecting pipe 즉시 attach |
| `ZLINK_CONFLATE` | `0` | 비활성 |
| `ZLINK_INVERT_MATCHING` | `0` | 비활성 |
| `ZLINK_STREAM_NOTIFY` | `0` | 비활성 |
| `ZLINK_HEARTBEAT_IVL` | `0` | 비활성 |
| `ZLINK_HEARTBEAT_TTL` | `0` | 비활성 |
| `ZLINK_HEARTBEAT_TIMEOUT` | `-1` | heartbeat 활성 시 interval 기반 fallback |
| `ZLINK_TCP_KEEPALIVE` | `-1` | OS 기본값 |
| `ZLINK_TCP_KEEPALIVE_CNT` | `-1` | OS 기본값 |
| `ZLINK_TCP_KEEPALIVE_IDLE` | `-1` | OS 기본값 |
| `ZLINK_TCP_KEEPALIVE_INTVL` | `-1` | OS 기본값 |
| `ZLINK_TCP_NODELAY` | `1` | 기본 활성 |
| `ZLINK_BINDTODEVICE` | 빈 문자열 | 디바이스 바인딩 없음 |
| `ZLINK_ZMP_METADATA` | `0` | 비활성 |

## 2. 소켓 타입별 기본값 / Override

| 소켓 타입 | 옵션/동작 | 기본값 |
|---|---|---|
| `ZLINK_DEALER` | `ZLINK_PROBE_ROUTER` | `0` |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_MANDATORY` | `0` |
| `ZLINK_ROUTER` | `ZLINK_PROBE_ROUTER` | `0` |
| `ZLINK_ROUTER` | `ZLINK_ROUTER_HANDOVER` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_VERBOSE` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_VERBOSER` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_NODROP` | `0` (`_lossy=true`) |
| `ZLINK_XPUB` | `ZLINK_XPUB_MANUAL` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_MANUAL_LAST_VALUE` | `0` |
| `ZLINK_XPUB` | `ZLINK_ONLY_FIRST_SUBSCRIBE` | `0` |
| `ZLINK_XPUB` | `ZLINK_XPUB_WELCOME_MSG` | 빈 값 |
| `ZLINK_XSUB` | `ZLINK_ONLY_FIRST_SUBSCRIBE` | `0` |
| `ZLINK_XSUB` | `ZLINK_LINGER` override | 강제로 `0` |
| `ZLINK_SUB` | `ZLINK_LINGER` override | 강제로 `0` (XSUB 생성자 상속) |
| `ZLINK_SUB` | 구독 집합 | 생성 시 빈 상태 |
| `ZLINK_STREAM` | `ZLINK_BACKLOG` override | `65536` |
| `ZLINK_STREAM` | `ZLINK_SNDBUF` override | 미설정(` <0`)이면 `262144` |
| `ZLINK_STREAM` | `ZLINK_RCVBUF` override | 미설정(` <0`)이면 `262144` |
| `ZLINK_STREAM` | `ZLINK_CONNECT_ROUTING_ID` | 미지원 (`EOPNOTSUPP`) |

## 3. 읽기 전용 옵션의 초기 상태값

| 옵션 | 초기값 |
|---|---|
| `ZLINK_LAST_ENDPOINT` | 빈 문자열 |
| `ZLINK_RCVMORE` | `0` |
| `ZLINK_TOPICS_COUNT` (XPUB/XSUB) | `0` |

## 4. TLS 옵션 기본값 (TLS 빌드 시)

| 옵션 | 기본값 |
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

- `ZLINK_BLOCKY`는 소켓 옵션이 아니라 컨텍스트 옵션(`zlink_ctx_set/get`)이다.
- `ZLINK_CONFLATE`는 `DEALER`, `PUB`, `SUB`에서만 실질적으로 동작한다.
- STREAM의 추가 런타임 튜닝 항목은 `stream-socket.ko.md`를 참고한다.
