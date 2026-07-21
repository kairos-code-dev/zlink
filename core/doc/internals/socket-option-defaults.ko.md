[English](socket-option-defaults.md) | [한국어](socket-option-defaults.ko.md)

# 소켓 옵션 기본값 (코드 기준)

이 문서는 소켓 타입별로 각 소켓 옵션의 기본값을 정리한다. 일부 소켓 타입은 고유한 동작 특성에 맞게 전역 기본값을 재정의한다.

예제/가이드가 아니라 실제 구현 코드 기준의 기본값을 정리한 참조 문서다.

- `core/src/runtime/core/options.cpp` (`options_t` 생성자)
- `core/src/runtime/core/options_core_socket.cpp` (core socket 옵션 dispatch)
- `core/src/runtime/core/options_transport_network.cpp` (transport/network 옵션 dispatch)
- `core/src/runtime/core/options_protocol_metadata.cpp` (protocol/metadata 옵션 dispatch)
- `core/src/runtime/sockets/common/socket_base_lifecycle.cpp` (컨텍스트 상속 기본값)
- `core/src/runtime/sockets/*/*.cpp` (소켓 타입별 override)

각 옵션의 상세 동작과 영향 범위는
[소켓 옵션 상세 가이드](../guide/12-socket-options.ko.md)를 참고한다.

## 1. 공통 기본값 (별도 override가 없을 때)

| 옵션 | 기본값 | 비고 |
|---|---:|---|
| `ZLINK_OPT_AFFINITY` | `0` | I/O 스레드 어피니티 미설정 |
| `zlink_get_routing_id()` | 자동 | 소켓별 16바이트 랜덤 ID (`zlink_set_routing_id()`로 설정) |
| `ZLINK_OPT_SNDHWM` | auto-HWM balanced 값 | HWM(High Water Mark, 큐 상한) — profile, policy class, message unit으로 자동 계산. context auto-HWM을 끄면 기존 고정 기본값 `1000` 사용 |
| `ZLINK_OPT_RCVHWM` | auto-HWM balanced 값 | profile, policy class, message unit으로 계산. context auto-HWM을 끄면 기존 고정 기본값 `1000` 사용 |
| `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` | `0` | raw `0`은 소켓 타입 기본값 사용. STREAM은 `1024`, 그 외 소켓은 `4096` |
| `ZLINK_OPT_RATE` | `100` | 멀티캐스트 rate (kb/s) |
| `ZLINK_OPT_RECOVERY_IVL` | `10000` | 멀티캐스트 recovery interval (ms) |
| `ZLINK_OPT_SNDBUF` | `-1` | `SO_SNDBUF` 강제 설정 안 함 |
| `ZLINK_OPT_RCVBUF` | `-1` | `SO_RCVBUF` 강제 설정 안 함 |
| `ZLINK_OPT_TOS` | `0` | ToS/DSCP override 없음 |
| `ZLINK_OPT_LINGER` | 컨텍스트 상속 | 컨텍스트의 blocky 기본 모드가 켜져 있으면 `-1`, 아니면 `0` |
| `ZLINK_OPT_CONNECT_TIMEOUT` | `0` | 비활성 |
| `ZLINK_OPT_TCP_MAXRT` | `0` | 비활성 |
| `ZLINK_OPT_RECONNECT_IVL` | `100` | 초기 재연결 간격 (ms) |
| `ZLINK_OPT_RECONNECT_IVL_MAX` | `0` | 최대 재연결 간격 비활성 |
| `ZLINK_OPT_BACKLOG` | `100` | listener backlog |
| `ZLINK_OPT_MAXMSGSIZE` | `-1` | 무제한 |
| `ZLINK_OPT_MULTICAST_HOPS` | `1` | 멀티캐스트 TTL |
| `ZLINK_OPT_MULTICAST_MAXTPDU` | `1500` | 멀티캐스트 max TPDU |
| `ZLINK_OPT_RCVTIMEO` | `1000` | 기본 수신 타임아웃(ms) |
| `ZLINK_OPT_SNDTIMEO` | `1000` | 기본 송신 타임아웃(ms) |
| `ZLINK_OPT_IPV6` | 컨텍스트 상속 | 컨텍스트의 IPv6 기본값을 상속 (기본 `0`) |
| `ZLINK_OPT_IMMEDIATE` | `0` | 연결 중인 pipe를 즉시 등록 |
| `ZLINK_OPT_CONFLATE` | `0` | 비활성 |
| `ZLINK_OPT_INVERT_MATCHING` | `0` | 비활성 |
| `ZLINK_STREAM_OPT_NOTIFY` | `0` | 비활성 |
| `ZLINK_OPT_TCP_KEEPALIVE` | `-1` | OS 기본값 |
| `ZLINK_OPT_TCP_KEEPALIVE_CNT` | `-1` | OS 기본값 |
| `ZLINK_OPT_TCP_KEEPALIVE_IDLE` | `-1` | OS 기본값 |
| `ZLINK_OPT_TCP_KEEPALIVE_INTVL` | `-1` | OS 기본값 |
| `ZLINK_OPT_TCP_NODELAY` | `1` | 기본 활성 |
| `ZLINK_OPT_BINDTODEVICE` | 빈 문자열 | 디바이스 바인딩 없음 |
| `ZLINK_OPT_HANDSHAKE_IVL` | `30000` | ZMP 핸드셰이크 타임아웃 (ms, 0 = 비활성) |
| `ZLINK_OPT_ZMP_METADATA` | `0` | 비활성 |

## 2. 소켓 타입별 기본값 / Override

| 소켓 타입 | 옵션/동작 | 기본값 |
|---|---|---|
| `ZLINK_SOCKET_DEALER` | `ZLINK_DEALER_OPT_PROBE` | `0` |
| `ZLINK_SOCKET_ROUTER` | `ZLINK_ROUTER_OPT_MANDATORY` | `1` |
| `ZLINK_SOCKET_ROUTER` | `ZLINK_ROUTER_OPT_PROBE` | `0` |
| 공통 socket | `ZLINK_OPT_RID_DUPLICATE_POLICY` | `ZLINK_RID_DUPLICATE_REJECT` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_VERBOSE` | `0` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_VERBOSER` | `0` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_NODROP` | `1` (`_lossy=false`) |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_MANUAL` | `0` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_MANUAL_LAST_VALUE` | `0` |
| `ZLINK_SOCKET_XPUB` | `ZLINK_PUB_OPT_WELCOME_MSG` | 빈 값 |
| `ZLINK_SOCKET_XSUB` | `ZLINK_OPT_LINGER` override | 강제로 `0` |
| `ZLINK_SOCKET_SUB` | `ZLINK_OPT_LINGER` override | 강제로 `0` (XSUB 생성 경로 상속) |
| `ZLINK_SOCKET_SUB` | 구독 집합 | 생성 시 빈 상태 |
| `ZLINK_SOCKET_STREAM` | `ZLINK_OPT_BACKLOG` override | `65536` |
| `ZLINK_SOCKET_STREAM` | `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | 미지원 (`EOPNOTSUPP`) |

## 3. 읽기 전용 옵션의 초기 상태값

| 옵션 | 초기값 |
|---|---|
| `ZLINK_OPT_LAST_ENDPOINT` | 빈 문자열 |
| `ZLINK_PUB_OPT_TOPICS_COUNT` (XPUB) | `0` |
| `ZLINK_SUB_OPT_TOPICS_COUNT` (XSUB/SUB) | `0` |

## 4. TLS helper 관련 메모

TLS는 공개 helper API인 `zlink_set_tls_server()`와
`zlink_set_tls_client()`로 설정한다.
공개 헤더에는 `ZLINK_TLS_*` 옵션 이름이 없으므로, 이 문서도 별도의
TLS 상수 표는 두지 않는다.

helper API를 호출하기 전까지는 인증서 경로와 CA 경로가 비어 있다.
`require_client_cert`, `trust_system` 같은 정책 값은 helper 호출 때
함께 넘긴다.

## 5. 주의 사항

- 기본 `ZLINK_OPT_LINGER` 값은 컨텍스트의 blocky 모드에서 온다.
- 기본 `ZLINK_OPT_SNDHWM` / `ZLINK_OPT_RCVHWM` 값은 balanced profile의 context
  auto-HWM에서 온다. context auto-HWM을 끄면 기존 고정값 `1000`을 사용한다.
  수동 설정이 있으면 자동값보다 우선한다.
- `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`은 planner profile을 고른다. 공개 값은
  `COMPACT`, `LOW_LATENCY`, `BALANCED`, `THROUGHPUT`이고 기본값은 `BALANCED`다.
- deprecated context memory budget과 bootstrap context 옵션은 호환을 위해 남긴
  no-op 필드다. 이 옵션들은 소켓 기본값이나 HWM 계산에 영향을 주지 않는다.
- `auto_hwm_effective_message_bytes`는 소켓별 값이다. raw socket
  `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` override가 양수이면 그 값을 먼저 쓰고
  아니면 context `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` 양수 값을 쓴다.
  둘 다 없으면 STREAM 기본 `1024` 또는 non-STREAM 기본 `4096`을 쓴다.
- planner는 policy class(`fanout`, `spot_data`, `routed`, `peer_queue`,
  `stream`, `recv_ingress`, `control`), profile별 per-connection 단위 예산,
  메시지 크기별 cap을 고른다. 최종 HWM은 최소 `1`, 최대 해당 size cap으로
  제한된다.
- SPOT publish 계획은 전체 spot 수나 connection 수가 늘어도 per-connection HWM을
  낮추지 않는다.
- `ZLINK_OPT_CONFLATE`는 `ZLINK_SOCKET_DEALER`,
  `ZLINK_SOCKET_PUB`, `ZLINK_SOCKET_SUB`에서만 실질적으로 동작한다.
- STREAM의 추가 런타임 튜닝 항목은 `stream-socket.ko.md`를 참고한다.
