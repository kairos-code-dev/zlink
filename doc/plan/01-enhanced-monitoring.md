# 모니터링 강화 스펙 (Enhanced Monitoring)

> **우선순위**: 1 (Core Feature)
> **상태**: Draft
> **버전**: 1.6
> **의존성**: [00-routing-id-unification.md](00-routing-id-unification.md)

## 목차
1. [현재 상태 분석](#1-현재-상태-분석)
2. [설계 원칙](#2-설계-원칙)
3. [제안 API](#3-제안-api)
4. [사용 예시](#4-사용-예시)
5. [구현 영향 범위](#5-구현-영향-범위)
6. [검증 방법](#6-검증-방법)

---

## 1. 현재 상태 분석

### 1.1 현재 모니터링 API

```c
int zmq_socket_monitor(void *socket, const char *endpoint, int events);
```

- 호출 시 내부 모니터 소켓이 `endpoint`에 bind
- 사용자는 별도의 ZMQ_PAIR 소켓을 만들어 `endpoint`에 connect 후 이벤트 수신
- (현 상태) Version 1/2 이벤트 형식 존재

### 1.2 현재 이벤트 형식

| 버전 | 형식 |
|------|------|
| Version 1 | `[event(2B) + value(4B)][endpoint_uri]` |
| Version 2 | `[event(8B)][value_count(8B)][values...][local_addr][remote_addr]` |

### 1.3 현재 ZMP 핸드셰이크 (구현됨)

```
HELLO body: [control_type][socket_type][routing_id_len][routing_id(0~255B)]
```

- 양방향 routing_id 교환 동작함
- `_peer_routing_id`에 상대방 ID 저장됨

### 1.4 핵심 문제점

| 문제 | 설명 |
|------|------|
| **routing_id 미포함** | 모니터링 이벤트에 routing_id 정보 없음 |
| **recv_routing_id 기본값** | ROUTER만 `true`, 나머지는 `false` |
| **자동 ID 생성** | ROUTER/STREAM만 지원 |
| **사용 불편** | 이벤트를 받기 위해 별도 소켓을 만들어야 함 |
| **메트릭스 부재** | 메시지 수, 바이트, 드롭 등 통계 없음 |
| **연결 이벤트 의미 혼동** | `CONNECTED/ACCEPTED`는 전송계층 연결, 실제 송수신 가능 시점은 핸드셰이크 완료라 사용자 혼동 |
| **모니터링 백프레셔 정책 부재** | 모니터 소켓 지연 시 이벤트 드롭/차단 기준 없음 |

---

## 2. 설계 원칙

### 2.1 통일된 routing_id 정책

**모든 소켓에서 routing_id를 보유하도록 확장**하되,
**자동 생성 포맷은 5바이트로 통일**한다:

| 항목 | 현재 | 변경 |
|------|------|------|
| `ZMQ_ROUTING_ID` 설정 | 모든 소켓 가능 | 유지 |
| 미설정 시 자동 생성 | ROUTER/STREAM만 | **모든 소켓** |
| 자동 생성 포맷 | ROUTER 5B, STREAM 4B | **모든 소켓 5B** |
| routing_id type | `void* + size` | **`zmq_routing_id_t`** |
| `ZMQ_CONNECT_ROUTING_ID` | ROUTER/STREAM용 | **유지** |

> Note: `recv_routing_id`는 메시지 프레임 전달 여부에만 영향. 
> 모니터링용 peer routing_id 저장은 별도 경로로 항상 수행한다.
> 모니터링 API는 **Polling 방식만 제공**한다.

### 2.2 동작 흐름

```
1. 소켓 생성
   - routing_id 미설정 시 자동 생성
   - 모든 소켓: [0x00][uint32] (5B)

2. 모니터 활성화
   - zmq_socket_monitor*(socket, endpoint, events)
   - 내부 모니터 소켓이 endpoint에 bind
   - 사용자는 PAIR 소켓으로 endpoint에 connect
   - 또는 zmq_socket_monitor_open으로 자동 생성/연결

3. 핸드셰이크
   - 양쪽이 routing_id(가변 길이) 전송
   - 양쪽이 상대방 routing_id 저장
   - 핸드셰이크 완료 시 `ZMQ_EVENT_CONNECTION_READY` 발생

4. 모니터링 이벤트 수신
   - 사용자는 monitor socket에서 zmq_monitor_recv 호출
   - 다중 소켓 모니터링은 zmq_poll로 조합
```

### 2.3 연결 완료 이벤트 명확화

- **연결 완료 기준**을 *핸드셰이크 완료 시점*으로 정의한다.
- 이벤트 명칭은 `ZMQ_EVENT_CONNECTION_READY`로 통일한다.
- `CONNECTED/ACCEPTED`는 **전송계층 레벨** 이벤트로 유지하되, 필수 수신 이벤트가 아님을 명확히 한다.
- 사용자 가이드는 다음을 권장:
  - **연결 완료**가 필요하면 `CONNECTION_READY`만 구독
  - 상세 진단이 필요할 때만 `CONNECTED/ACCEPTED/CONNECT_DELAYED` 등을 추가

### 2.4 모니터링 이벤트 전달 정책

- 모니터링은 **데이터 경로를 차단하지 않아야 한다.**
- 모니터 소켓이 느릴 경우, 이벤트는 **드롭될 수 있다.**
- 드롭 수는 `zmq_socket_stats_t.monitor_events_dropped`로 노출한다.

### 2.5 호환성 방침

- **기존 버전과의 호환성은 고려하지 않는다.**
- 기존 이벤트/포맷/함수는 필요 시 교체 또는 제거한다.

### 2.6 Thread-safe/Proxy 소켓 모니터링 정책

- 모니터링 대상은 **사용자가 가진 소켓 핸들(Proxy 포함)**이다.
- 실제 이벤트는 **내부 Raw Socket**의 상태를 반영한다.
- Thread-safe 소켓이라면 `zmq_socket_monitor*` 호출은 **직렬화되어 안전**하다.
- Thread-safe가 아닌 소켓은 **소켓 소유 스레드에서만** 모니터 설정/해제를 수행한다.
- 모니터 소켓은 별도 소켓이므로 **기본적으로 thread-safe하지 않다**.

---

## 3. 제안 API

### 3.1 이벤트 의미/권장 구독

- `ZMQ_EVENT_CONNECTED` / `ZMQ_EVENT_ACCEPTED`
  - 전송계층 연결 성립(소켓/FD 수준)
  - 핸드셰이크 전이므로 routing_id는 아직 없음
  - 애플리케이션 로직에서는 선택 이벤트로 취급
- `ZMQ_EVENT_CONNECTION_READY`
  - **연결 완료** 기준(핸드셰이크 성공 후 실제 송수신 가능)
  - peer routing_id가 확보된 시점에 포함 가능
- `ZMQ_EVENT_HANDSHAKE_FAILED_*`
  - `CONNECTION_READY` 이전 실패 원인을 전달
- `ZMQ_EVENT_DISCONNECTED`
  - 세션 종료(핸드셰이크 이전/이후 모두 발생 가능)

#### 이벤트 정의 표 (요약)

| 이벤트 | 의미 | 발생 시점 | routing_id |
|--------|------|-----------|------------|
| `CONNECTED` | connect 완료(전송계층) | TCP/TLS/IPC connect 직후 | 없음 |
| `ACCEPTED` | accept 완료(전송계층) | 리스너 accept 직후 | 없음 |
| `CONNECTION_READY` | **연결 완료**(핸드셰이크 성공) | HELLO/READY 완료 | 가능 |
| `HANDSHAKE_FAILED_*` | 핸드셰이크 실패 | READY 이전 | 없음 |
| `DISCONNECTED` | 세션 종료 | 어떤 단계든 | 가능 |

> routing_id는 핸드셰이크 완료 이후에만 확보 가능하므로  
> `CONNECTED/ACCEPTED/HANDSHAKE_FAILED_*`에서는 size=0을 원칙으로 한다.

권장 구독 프리셋:

- **기본**: `CONNECTION_READY | DISCONNECTED`
- **디버깅**: 기본 + `CONNECTED | ACCEPTED | CONNECT_DELAYED | CONNECT_RETRIED`
- **보안/핸드셰이크 진단**: 기본 + `HANDSHAKE_FAILED_*`

이벤트 순서(성공 경로):

```
CONNECTED/ACCEPTED -> CONNECTION_READY -> DISCONNECTED
```

실패 경로는 `HANDSHAKE_FAILED_*` 이후 `DISCONNECTED`로 종료될 수 있다.
기존 `HANDSHAKE_SUCCEEDED` 명칭은 **제거**하고
`CONNECTION_READY`로 통일한다.

```c
#define ZMQ_EVENT_CONNECTION_READY 0x1000 /* HANDSHAKE_SUCCEEDED 대체 */
```

#### 시퀀스 예시

성공(연결 완료까지):

```
CONNECT_DELAYED? -> CONNECT_RETRIED? -> CONNECTED/ACCEPTED -> CONNECTION_READY
```

성공 후 종료:

```
CONNECTION_READY -> DISCONNECTED
```

핸드셰이크 실패(인증/프로토콜):

```
CONNECTED/ACCEPTED -> HANDSHAKE_FAILED_* -> DISCONNECTED
```

전송계층 실패(연결 실패/타임아웃):

```
CONNECT_DELAYED -> CONNECT_RETRIED -> DISCONNECTED
```

> 실제 발생 이벤트는 전송/타임아웃 설정에 따라 일부 생략될 수 있다.

### 3.2 새로운 이벤트 구조

```c
typedef struct {
    uint64_t event;           // 이벤트 타입
    uint64_t value;           // 이벤트 값 (errno, fd 등)
    zmq_routing_id_t routing_id; // 상대방 routing_id (size=0이면 없음)
    char     local_addr[256]; // 로컬 주소
    char     remote_addr[256];// 원격 주소
} zmq_monitor_event_t;
```

- 연결 완료 이벤트는 `ZMQ_EVENT_CONNECTION_READY` 사용 (핸드셰이크 완료 시 발생)
- `routing_id`는 다음 이벤트에서만 **존재 가능**:
  - `CONNECTION_READY`, `DISCONNECTED`
  - 그 외 이벤트는 `routing_id.size = 0`을 기본값으로 한다.

### 3.3 모니터 활성화

```c
/* 기존 API (호환성 유지하지 않음, 필요 시 대체/제거) */
ZMQ_EXPORT int zmq_socket_monitor(
    void *socket,
    const char *endpoint,
    int events
);
```

### 3.4 모니터 소켓 자동 생성 (권장)

```c
/*
 * zmq_socket_monitor_open - 모니터 소켓 자동 생성
 *
 * @param socket  모니터링 대상 소켓
 * @param events  모니터링 이벤트 마스크
 * @return        성공: 모니터링 수신용 PAIR 소켓, 실패: NULL
 *
 * 동작:
 *   - 내부에서 inproc endpoint를 자동 생성
 *   - 내부 모니터 소켓 bind + 외부 PAIR 소켓 connect까지 완료
 *   - 기본 이벤트 포맷은 본 문서의 **단일 포맷** 사용
 *   - 동일 소켓에 대해 재호출 시 기존 모니터를 종료하고 최신 설정으로 교체
 *
 * 주의:
 *   - monitor_socket이 먼저 닫히면 이후 이벤트는 드롭될 수 있음
 *   - `ZMQ_EVENT_MONITOR_STOPPED`는 종료 시 마지막 이벤트로 전달 가능
 *   - monitor_open은 `zmq_socket_monitor(...)`의 간편 래퍼로 간주
 *
 * 종료:
 *   - 모니터 종료 시 zmq_socket_monitor(socket, NULL, 0) 호출 권장
 *   - 반환된 monitor_socket은 사용 후 zmq_close로 정리
 */
ZMQ_EXPORT void *zmq_socket_monitor_open(
    void *socket,
    int events
);
```

- 권장 사용:
  - 앱 로직은 `CONNECTION_READY/DISCONNECTED` 중심
  - 상세 원인 추적 시에만 `HANDSHAKE_FAILED_*` 및 `CONNECTED/ACCEPTED` 추가
- 예상 에러:
  - `ENOTSOCK`: 유효하지 않은 소켓
  - `ETERM`: 컨텍스트 종료 상태
  - `EPROTONOSUPPORT`: inproc 미지원/사용 불가
  - `ENOMEM`: 리소스 부족

스레드/재호출 가이드:

- 모니터 설정은 **해당 소켓을 사용하는 스레드에서만** 호출한다.
- monitor_open 재호출 시 기존 monitor_socket은 사용자 책임으로 닫는다.
- 동일 소켓에 대해 **동시에 여러 모니터를 활성화하지 않는다**.

### 3.5 모니터링 수신 API (Polling)

```c
/*
 * zmq_monitor_recv - 모니터링 이벤트 수신
 *
 * @param monitor_socket  모니터링 수신용 PAIR 소켓
 * @param event           수신된 이벤트 (out)
 * @param flags           0 또는 ZMQ_DONTWAIT
 * @return                0: 성공, -1: 에러 또는 타임아웃
 *
 * 에러:
 *   EAGAIN    - non-blocking 모드에서 이벤트 없음
 *   ETIMEDOUT - 타임아웃 만료 (zmq_poll로 제어 시)
 *
 * 참고:
 *   - 다중 소켓 모니터링은 zmq_poll로 조합
 *   - ZMQ_DONTWAIT는 즉시 반환
 *   - 단일 포맷 이벤트를 파싱하여 zmq_monitor_event_t로 변환
 */
ZMQ_EXPORT int zmq_monitor_recv(
    void *monitor_socket,
    zmq_monitor_event_t *event,
    int flags
);
```

### 3.6 모니터 이벤트 포맷 (단일)

```
Frame 1: event (uint64_t)
Frame 2: value_count (uint64_t)
Frame 3..N: values[] (uint64_t)
Frame N+1: routing_id (0~255 bytes)
Frame N+2: local_addr (string)
Frame N+3: remote_addr (string)
```

- routing_id frame size가 0이면 `routing_id.size = 0`
- 본 문서의 **단일 포맷**을 표준으로 사용한다.

### 3.6.1 필드 의미 보강

- `event`: 이벤트 타입
- `value`: 이벤트별 보조 값
  - `CONNECTED/ACCEPTED/CLOSED/DISCONNECTED`: fd 또는 0 (플랫폼별로 상이)
  - `*_FAILED_*`: `errno` 또는 프로토콜 에러 코드
  - `CONNECTION_READY`: 0 (예약)
- `local_addr` / `remote_addr`: 가능한 경우에만 채움

### 3.7 메트릭스 API

```c
typedef struct {
    uint64_t msgs_sent;        // 송신 메시지 수
    uint64_t msgs_received;    // 수신 메시지 수
    uint64_t bytes_sent;       // 송신 바이트
    uint64_t bytes_received;   // 수신 바이트
    uint64_t msgs_dropped;     // 드롭된 메시지 수
    uint64_t monitor_events_dropped; // 드롭된 모니터 이벤트 수
    uint32_t queue_size;       // 현재 큐 크기
    uint32_t hwm_reached;      // HWM 도달 횟수
    uint32_t peer_count;       // 연결된 피어 수
} zmq_socket_stats_t;

int zmq_socket_stats(void *socket, zmq_socket_stats_t *stats);
```

### 3.8 피어 정보 조회 (모든 소켓)

```c
typedef struct {
    zmq_routing_id_t routing_id; // 피어 routing_id
    char     remote_addr[256];   // 원격 주소
    uint64_t connected_time;     // 연결 시간 (Unix timestamp)
    uint64_t msgs_sent;          // 송신 메시지 수
    uint64_t msgs_received;      // 수신 메시지 수
} zmq_peer_info_t;

ZMQ_EXPORT int zmq_socket_peer_info(
    void *socket,
    const zmq_routing_id_t *routing_id,
    zmq_peer_info_t *info
);

ZMQ_EXPORT int zmq_socket_peer_routing_id(
    void *socket,
    int index,
    zmq_routing_id_t *out
);

ZMQ_EXPORT int zmq_socket_peer_count(void *socket);

ZMQ_EXPORT int zmq_socket_peers(
    void *socket,
    zmq_peer_info_t *peers,
    size_t *count
);
```

---

## 4. 사용 예시

### 4.1 모니터 활성화 + 단일 소켓 Polling

```c
void *server = zmq_socket(ctx, ZMQ_ROUTER);
zmq_bind(server, "tcp://*:5555");

/* 모니터 활성화 + 모니터 소켓 자동 생성 (단일 포맷) */
void *mon = zmq_socket_monitor_open(server, ZMQ_EVENT_ALL);

zmq_monitor_event_t ev;
int rc = zmq_monitor_recv(mon, &ev, ZMQ_DONTWAIT);
if (rc == 0) {
    printf("event=%llu routing_id=", (unsigned long long)ev.event);
    for (uint8_t i = 0; i < ev.routing_id.size; ++i)
        printf("%02x", ev.routing_id.data[i]);
    printf("\n");
}

/* 모니터 종료 */
zmq_socket_monitor(server, NULL, 0);
zmq_close(mon);
```

### 4.2 모니터링 Polling (다중 소켓)

```c
void *sock_a = zmq_socket(ctx, ZMQ_ROUTER);
void *sock_b = zmq_socket(ctx, ZMQ_DEALER);
/* ... bind/connect 생략 ... */

void *mon_a = zmq_socket_monitor_open(sock_a, ZMQ_EVENT_ALL);
void *mon_b = zmq_socket_monitor_open(sock_b, ZMQ_EVENT_ALL);

zmq_pollitem_t items[] = {
    {mon_a, 0, ZMQ_POLLIN, 0},
    {mon_b, 0, ZMQ_POLLIN, 0},
};

int rc = zmq_poll(items, 2, 1000);
if (rc > 0) {
    if (items[0].revents & ZMQ_POLLIN) {
        zmq_monitor_event_t ev;
        if (zmq_monitor_recv(mon_a, &ev, ZMQ_DONTWAIT) == 0) {
            /* handle ev */
        }
    }
    if (items[1].revents & ZMQ_POLLIN) {
        zmq_monitor_event_t ev;
        if (zmq_monitor_recv(mon_b, &ev, ZMQ_DONTWAIT) == 0) {
            /* handle ev */
        }
    }
}

/* 모니터 종료 */
zmq_socket_monitor(sock_a, NULL, 0);
zmq_socket_monitor(sock_b, NULL, 0);
zmq_close(mon_a);
zmq_close(mon_b);
```

---

## 5. 구현 영향 범위

### 5.1 수정 필요 파일

| 파일 | 변경 내용 |
|------|----------|
| `include/zmq.h` | `ZMQ_EVENT_CONNECTION_READY`, `zmq_socket_monitor_open`, `zmq_monitor_recv` 선언 |
| `src/api/zmq.cpp` | monitor_open 구현 + recv 파서 구현 |
| `src/core/options.hpp` | 모든 소켓 routing_id 자동 생성 |
| `src/sockets/socket_base.cpp` | 모니터 단일 포맷 프레임 추가 (routing_id 포함) + CONNECTION_READY 이벤트 발생 |
| `src/sockets/socket_base.hpp` | 피어 관리 자료구조 추가 |
| `src/sockets/dealer.cpp` | 상대방 ID 저장 로직 추가 |
| `src/sockets/pub.cpp`, `sub.cpp` | 상대방 ID 저장 로직 추가 |
| `src/core/pipe.hpp` | 메트릭스 카운터 추가 |
| `src/engine/asio/asio_zmp_engine.cpp` | 이벤트에 routing_id 전달 |
| `tests/testutil_monitoring.cpp` | CONNECTION_READY 이벤트 명칭 반영 |

---

## 6. 검증 방법

### 6.1 단위 테스트

**테스트 파일:** `test_monitor_enhanced.cpp`

| 테스트 케이스 | 설명 |
|--------------|------|
| `test_auto_routing_id_generation` | 모든 소켓 타입에서 routing_id 자동 생성 확인 |
| `test_bidirectional_id_exchange` | 핸드셰이크 후 양방향 ID 저장 확인 |
| `test_connection_ready_event` | 핸드셰이크 완료 시 CONNECTION_READY 이벤트 발생 확인 |
| `test_monitor_open_basic` | monitor_open이 모니터 소켓을 자동 생성/연결하는지 확인 |
| `test_monitor_event_routing_id` | 연결/끊김 이벤트에 routing_id 포함 확인 |
| `test_monitor_recv_parsing` | 모니터 이벤트 파싱 검증 |
| `test_metrics_accuracy` | 메트릭스 정확성 검증 |
| `test_peer_enumeration` | 피어 목록 조회 검증 |

---

## 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 0.1 | 2025-01-25 | 초안 작성 |
| 0.2 | 2025-01-25 | 00번 스펙 반영: routing_id를 uint32_t로 변경 |
| 0.3 | 2026-01-25 | routing_id_t 표준화 + 문자열 alias 유지 |
| 0.4 | 2026-01-25 | 자동 생성 routing_id 4B 통일 (폐기) |
| 0.5 | 2026-01-25 | 자동 생성 값 uint32 통일, 포맷 5B/4B 유지 (폐기) |
| 0.6 | 2026-01-25 | 자동 생성 포맷 5B 통일 (STREAM 포함) |
| 0.7 | 2026-01-26 | 모니터링 Polling API 추가 |
| 0.8 | 2026-01-26 | 콜백 제거, Polling 기반 정리 |
| 0.9 | 2026-01-26 | monitor socket 기반 polling + 단일 포맷 정의 |
| 1.0 | 2026-01-26 | CONNECTION_READY 이벤트 명칭 정리 + monitor_open API 추가 |
| 1.1 | 2026-01-26 | 호환성 고려 제거 명시 + 단일 포맷 기준화 |
| 1.2 | 2026-01-26 | thread-safe 모니터 정책 및 monitor_open_ex 추가 |
| 1.3 | 2026-01-26 | CONNECTION_READY 값 확정 + 이벤트 타입 64-bit화 |
| 1.4 | 2026-01-26 | 모니터 포맷 버전 API 제거, 단일 포맷으로 고정 |
| 1.5 | 2026-01-26 | monitor_open_ex 제거, 단일 모니터 소켓 사용으로 정리 |
| 1.6 | 2026-01-26 | routing_id 변환 함수 의존 제거 (예시 정리) |
