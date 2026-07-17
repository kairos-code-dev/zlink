[English](02-core-api.md) | [한국어](02-core-api.ko.md)

<!-- zlink-nav:start -->
[← 개요](01-overview.ko.md) | [소켓 패턴 →](03-0-socket-patterns.ko.md)
<!-- zlink-nav:end -->

# Core C API 상세 가이드

## 1. Context API

Context는 zlink의 최상위 객체다. I/O thread pool과 socket을 관리한다.

> I/O 스레드가 내부에서 어떤 일을 하는지(이벤트 루프, 명령 처리,
> 소켓 할당) 상세 설명은
> [I/O Thread 내부 구조](../internals/io-thread.ko.md)를 참고.

```c
/* Create */
void *ctx = zlink_ctx_new();

/* Configure — increase I/O threads for multi-connection servers */
zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);     /* default 4 */

/* Query */
/* 조회 (error_out 는 성공 시 ZLINK_CONFIG_OK) */
zlink_config_result_t err;
int io_threads = zlink_ctx_get(ctx, ZLINK_IO_THREADS, &err);

/* 자원 해제 없이 블로킹 호출만 중단 (선택) */
zlink_ctx_shutdown(ctx);  /* block된 zlink_send/recv를 깨움. term은 여전히 필요 */

/* Terminate */
zlink_ctx_term(ctx);  /* Returns after all sockets are closed */
```

### Context 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `ZLINK_IO_THREADS` | `4` | I/O thread 수 |
| `ZLINK_MAX_SOCKETS` | `4095` | 최대 socket 수 |
| `ZLINK_SOCKET_LIMIT` | — | 읽기 전용: 실제 socket 한도 |
| `ZLINK_THREAD_PRIORITY` | `-1` | I/O thread OS 우선순위 |
| `ZLINK_THREAD_SCHED_POLICY` | `-1` | I/O thread OS 스케줄링 정책 |
| `ZLINK_MAX_MSGSZ` | `INT_MAX` | 최대 message 크기(바이트). 기본 `INT_MAX`로 사실상 무제한이며 set은 음수 아닌 값만 허용 |
| `ZLINK_MSG_T_SIZE` | — | 읽기 전용: `sizeof(zlink_msg_t)` |
| `ZLINK_THREAD_AFFINITY_CPU_ADD` | — | I/O thread CPU 선호도 집합에 CPU 추가 |
| `ZLINK_THREAD_AFFINITY_CPU_REMOVE` | — | I/O thread CPU 선호도 집합에서 CPU 제거 |
| `ZLINK_THREAD_NAME_PREFIX` | — | I/O thread 이름 prefix (`zlink_ctx_set_data`로 설정) |
| `ZLINK_CTX_OPT_BLOCKY` | `1` | 레거시: context 종료 시 block |
| `ZLINK_CTX_OPT_AUTO_HWM_ENABLE` | `1` | 자동 HWM 크기 조정 활성화 |
| `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS` | `3000` | 자동 HWM 재계산 debounce 간격 (ms) |
| `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` | `ZLINK_AUTO_HWM_PROFILE_BALANCED` | 자동 HWM 크기 조정 profile |
| `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES` | `0` | 자동 HWM 계획에 쓰는 메시지당 바이트 단위 (`0`: 런타임 기본값) |

`ZLINK_THREAD_NAME_PREFIX`는 문자열을 인자로 받으므로 `zlink_ctx_set()` 대신
`zlink_ctx_set_data()`를 사용합니다.

`zlink_ctx_auto_hwm_recalculate()`는 context의 모든 socket에 대해 자동 HWM을
즉시 재계산합니다 — profile이나 socket별 메시지 단위 크기를 변경한 뒤 정상
debounce 간격을 기다리지 않고 즉시 반영할 때 유용합니다.

## 2. Socket API

공개 소켓 핸들 API는 기본적으로 스레드 안전(thread-safe)하다. 여러 스레드에서
같은 소켓 핸들을 공유해 send/recv/bind/connect 등을 호출할 수 있다.

> 세부 threading 규칙은 [Thread Safety 가이드](11-thread-safety.ko.md)를 참고.

### 2.1 Socket 생성 및 닫기

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
/* ... use ... */
zlink_close(socket);
```

### 2.2 Socket Type 상수

| 상수 | 값 | 설명 |
|------|-----|------|
| `ZLINK_SOCKET_PAIR` | 0x1001 | 1:1 bidirectional pair 소켓 |
| `ZLINK_SOCKET_PUB` | 0x1002 | Publisher 소켓 |
| `ZLINK_SOCKET_SUB` | 0x1003 | Subscriber 소켓 |
| `ZLINK_SOCKET_DEALER` | 0x1004 | Async dealer 소켓 |
| `ZLINK_SOCKET_ROUTER` | 0x1005 | Router 소켓 |
| `ZLINK_SOCKET_XPUB` | 0x1006 | Extended publisher 소켓 |
| `ZLINK_SOCKET_XSUB` | 0x1007 | Extended subscriber 소켓 |
| `ZLINK_SOCKET_STREAM` | 0x1008 | Raw 소켓 |

### 2.3 연결 관리

```c
/* Bind (server) */
zlink_bind(socket, "tcp://*:5555");

/* Connect (client) */
zlink_connect(socket, "tcp://127.0.0.1:5555");

/* Unbind */
zlink_unbind(socket, "tcp://*:5555");
zlink_disconnect(socket, "tcp://127.0.0.1:5555");

/* routing id로 특정 peer 연결 해제 (ROUTER socket) */
zlink_routing_id_t peer_rid = /* ... */;
zlink_disconnect_rid(socket, &peer_rid);
```

### 2.4 Socket Option

```c
/* Set option */
int hwm = 5000;
zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));

/* Get option */
int value;
size_t len = sizeof(value);
zlink_get_option(socket, ZLINK_OPT_SNDHWM, &value, &len);
```

주요 옵션:

| 옵션 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `ZLINK_OPT_SNDHWM` | int | 자동 | 활성 auto-HWM 프로파일(`balanced` 기본), 소켓 역할, message unit에서 산출. 컨텍스트에 `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`로 프로파일을 전역 변경하거나 소켓별로 수동 지정할 수 있다. 프로파일 값 및 소켓별 설정은 [소켓 옵션 가이드](12-socket-options.ko.md)를 참고 |
| `ZLINK_OPT_RCVHWM` | int | 자동 | `SNDHWM`과 동일: 수동 설정이 없으면 프로파일 기반으로 결정 |
| `ZLINK_OPT_SNDTIMEO` | int | 1000 | 송신 타임아웃(ms). 무제한 대기는 `-1`을 명시적으로 설정 |
| `ZLINK_OPT_RCVTIMEO` | int | 1000 | 수신 타임아웃(ms). 무제한 대기는 `-1`을 명시적으로 설정 |
| `ZLINK_OPT_LINGER` | int | -1 | Socket close 시 linger (ms) |

Routing ID는 전용 함수로 설정/조회한다:
`zlink_set_routing_id()` / `zlink_get_routing_id()`.
구독 관리는 `zlink_set_subscription()`을 사용한다.

`ZLINK_OPT_EVENTS`, `ZLINK_OPT_LAST_ENDPOINT` 같은 option은
runtime 중간에도 보통 사용한다. 반면 HWM, timeout, TLS 같은 대부분의
tuning option은 초기 설정 단계에서 사용한다.

상세 option 카테고리와 전체 option 레퍼런스는
[소켓 옵션 가이드](12-socket-options.ko.md)를 참고.

## 3. Message Send/Recv

### 3.1 Send

```c
/* Simple send */
zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "Hello", 5);
zlink_send(socket, &part, 1, 0);

/* Multipart send */
zlink_msg_t parts[2];
zlink_msg_init_size(&parts[0], 6);
memcpy(zlink_msg_data(&parts[0]), "header", 6);
zlink_msg_init_size(&parts[1], 4);
memcpy(zlink_msg_data(&parts[1]), "body", 4);
zlink_send(socket, parts, 2, 0);
```

기본적으로 `zlink_send()`는 송신 큐가 가득 차면(HWM(High-Water Mark, 큐 최대 허용 메시지 수) 도달) 블록한다.
`ZLINK_DONTWAIT` 플래그를 사용하면 대기 없이 즉시
`ZLINK_SUBMIT_BACKPRESSURED` 를 반환한다.
고급 배압(backpressure, 수신 측이 처리를 따라오지 못할 때 송신 측에 전달되는 흐름 제어 신호) 패턴은
[Performance 가이드](10-performance.ko.md)를 참고.

#### Logical Multipart Send

`zlink_send()`, `zlink_publish()` 등 공개/서비스 surface의
멀티파트 송신은 내부적으로 공통 **logical multipart send** 모듈을 사용한다.
이 모듈은 다음 의미를 공통으로 보장한다.

- **nonblocking**: one-shot 시도 후 실패 시 partial local state rollback
- **blocking**: `sndtimeo` deadline까지 whole-message 단위 재시도
- **재시도 대상**: `ZLINK_SUBMIT_BACKPRESSURED`, `EINTR` 만 재시도, 그 외 결과는 즉시 실패
- **whole-message 보장**: 멀티파트 메시지는 전체가 성공하거나 전체가 실패한다

멀티파트 메시지는 전체가 큐에 들어가거나 전체가 실패한다. 일부만 큐에 들어가는 경우는 없다.

> Wire 수준 프레임 구조는
> [ZMP 프로토콜](../internals/protocol-zmp.ko.md)을 참고.

### 3.2 Recv

zlink 소켓은 두 가지 recv mode를 지원한다:

#### Pull Mode (synchronous)

핸들러를 부착하지 않으면 `zlink_recv()`로 직접 메시지를 받을 수 있다.
소켓은 기본적으로 pull mode로 시작한다.

```c
void *socket = zlink_socket(ctx, ZLINK_SOCKET_PAIR);
zlink_bind(socket, "tcp://*:5556");

/* Blocking recv */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
zlink_recv_result_t rc = zlink_recv(
    socket, &source_rid, &parts, &part_count, 0 /* flags */);
if (rc == ZLINK_RECV_OK) {
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

/* Non-blocking recv */
rc = zlink_recv(
    socket, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
if (rc == ZLINK_RECV_NO_DATA) {
    /* 수신할 메시지가 없음 */
}
```

#### Callback Mode

콜백 수신(`zlink_recv_handler`)은 raw `STREAM` 소켓에서만 지원된다. 다른
소켓 타입에 부착하면 `ZLINK_HANDLER_NOT_SUPPORTED` 를 반환한다. 소켓 생성
후 핸들러를 부착하면 메시지 도착 시 I/O 스레드에서 비동기로 호출된다. 한 번
부착하면 소켓 수명 동안 해제할 수 없다. 핸들러가 부착된 상태에서
`zlink_recv()`를 호출하면 `ZLINK_RECV_BUSY` 를 반환한다.

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        printf("Frame %zu: %.*s\n", i,
               (int)zlink_msg_size(&parts[i]),
               (char *)zlink_msg_data(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
}

void *socket = zlink_socket(ctx, ZLINK_SOCKET_STREAM);
zlink_recv_handler(socket, on_message, NULL);
```

> 두 mode의 비교와 고급 pattern은
> [Performance 가이드](10-performance.ko.md)를 참고.

### 3.3 Send Flag

| Flag | 설명 |
|--------|------|
| `ZLINK_DONTWAIT` | Non-blocking 모드 (send/recv 불가 시 즉시 `BACKPRESSURED` / `NO_DATA` 반환) |

## 4. Handler Type

zlink의 수신 모델은 두 가지 기본 방식으로 나뉜다.

- **Pull mode (기본)**: `zlink_recv()` 등 수신 함수를 직접 호출해 데이터를 읽는다.
  주로 poller 루프 안에서 사용하며, 어느 스레드에서 언제 읽을지를 호출자가 제어한다.
- **Callback mode**: `*_handler()` 함수로 핸들러를 등록하면 I/O 스레드에서 메시지를
  콜백으로 직접 전달받는다. 한 번 등록하면 소켓 수명 동안 해제할 수 없다.
  콜백이 등록된 소켓에 `zlink_recv()`를 호출하면 `ZLINK_RECV_BUSY`가 반환된다.

대부분의 소켓 타입은 두 방식 중 하나만 지원한다. SPOT과 STREAM은 예외다.

- **MeshNode/Spot/Actor**: `zlink_mesh_node_set_ready_handler()`(또는 poller 등록)를 통합 readiness 신호로 사용한다. record는 wakeup 뒤 ready/claim/receive batch로 drain한다.
- **STREAM**: `zlink_recv()`, raw 콜백, packet 콜백 세 가지 수신 모드 중
  하나를 선택하면 이후 모드 변경이 불가하다.

콜백은 I/O 스레드에서 호출된다. 콜백 안에서 파일 I/O나 데이터베이스 조회처럼 오래 걸리는 차단 작업은 피해야 한다.
느린 처리가 필요하면 작업 큐에 넣고 별도 스레드에서 처리한다.

소켓 타입별 전용 등록 함수는 다음과 같다:

| Socket Type | 등록 호출 | Callback Signature |
|---|---|---|
| STREAM (raw) | `zlink_recv_handler()` | `fn(rid, parts, count, userdata)` |
| STREAM (packet) | `zlink_stream_packet_handler()` | `fn(stream, source_rid, header, body, userdata)` |
| ROUTER (routed) | recv-only — `zlink_router_recv()` | N/A. `zlink_router_request()` 의 reply 는 별도 완료 콜백으로 전달 |
| MeshNode (ready wakeup) | `zlink_mesh_node_set_ready_handler()` — wakeup 전용 readiness 콜백. record는 claim receive batch로 drain | `zlink_mesh_ready_domain_mask_t fn(node, mask, userdata)` |
| SPOT (service-aware subscribe recv) | `zlink_spot_subscribe(spot, ..., service_name_out, topic_id_out, ...)` | N/A — recv 기반; `SUBSCRIBE_READABLE` 이벤트 후 drain |
| SPOT (service-aware routed recv) | `zlink_spot_recv(spot, ...)` | N/A — recv 기반; `ROUTED_READABLE` 이벤트 후 drain |
| PAIR / DEALER / SUB / XSUB | recv-only — `zlink_recv()` 또는 `zlink_subscribe()` | N/A |
| DEALER / ROUTER request | `zlink_dealer_request()` / `zlink_router_request()` 에 전달 | `fn(zlink_request_result_t result, parts, count, userdata)` |
| Timer | `zlink_timer_handler()` | `fn(timer, fire_count, userdata)` |
| PUB | N/A | Send-only socket |

## 5. Error 처리

zlink 공개 C API 는 **함수별 typed result enum** 을 사용한다. 각 함수는
`zlink_<category>_result_t` 를 반환하며 `0` 은 항상 `OK` 이고 non-zero 값이
구체 실패 모드를 식별한다. 전체 enum 정의는
[core/errno-map.md](../spec/core/04-errno-map.ko.md) 를 참조한다.

8 개 result enum 카테고리:

| Enum | 적용 함수군 |
|------|------------|
| `zlink_submit_result_t` | send / publish / request submit / reply submit |
| `zlink_request_result_t` | request completion (callback) |
| `zlink_recv_result_t` | recv / subscribe / monitor recv / timer recv |
| `zlink_handler_result_t` | handler 등록 (`zlink_*_handler()`) |
| `zlink_close_result_t` | close / destroy |
| `zlink_bind_result_t` | bind |
| `zlink_connect_result_t` | connect / disconnect / unbind |
| `zlink_config_result_t` | option 설정/조회, snapshot, poller 변경, message lifecycle, timer config |

```c
zlink_msg_t part;
zlink_msg_init_size(&part, size);
memcpy(zlink_msg_data(&part), data, size);
zlink_submit_result_t rc = zlink_send(socket, &part, 1, 0);
if (rc != ZLINK_SUBMIT_OK) {
    /* 대표 값: BACKPRESSURED, NOT_CONNECTED, NOT_FOUND, TERMINATED,
       INVALID_HANDLE, INVALID_ARGUMENT, NOT_SUPPORTED, INVALID_STATE,
       THREAD_VIOLATION, OUT_OF_MEMORY, INTERNAL_ERROR */
    printf("send failed: %d\n", (int)rc);
    if (rc == ZLINK_SUBMIT_INTERNAL_ERROR) {
        /* INTERNAL_ERROR 는 coarse bucket; zlink_errno() 로 상세 원인 조회 */
        int internal = zlink_errno();
        printf("  internal errno: %s\n", zlink_strerror(internal));
    }
}
```

**`zlink_errno()` 는 `INTERNAL_ERROR` 상세 조회 전용이다.** 다른 result code
는 자기 서술적이므로 `zlink_errno()` 조회가 불필요하다.

언어 바인딩은 이 8 카테고리 구조를 typed exception/error 하위 타입으로
노출한다. 자세한 계약은
[bindings Per-Function Error Type Hierarchy](../../../bindings/doc/spec/README.ko.md)
참조.

## 6. Timer API

타이머는 소켓과 동일한 recv/콜백/poller 모델을 지원하는 일급 이벤트 소스다. 소켓처럼 poller에 등록하거나 콜백을 붙일 수 있다.

### 6.1 일반 Timer

```c
void *timer = zlink_timer_new();

/* Start: 100ms 간격, 무한 반복 (0 = infinite) */
zlink_timer_start(timer, 100000000ULL, 0);  /* interval_ns, repeat_count */

/* Pull 모드 */
uint64_t fire_count;
zlink_recv_result_t rc = zlink_timer_recv(timer, &fire_count);
/* rc 값: ZLINK_RECV_OK, NO_DATA (timer 미실행 + 큐 빔; running timer는 fire까지 대기),
   TERMINATED, INVALID_HANDLE, NOT_SUPPORTED */

/* Callback 모드 */
void on_fire(void *timer, uint64_t fire_count, void *userdata) {
    /* timer 이벤트 처리 */
}
zlink_timer_handler(timer, on_fire, NULL);

/* 정지 및 해제 */
zlink_timer_stop(timer);
zlink_timer_destroy(&timer);
```

### 6.2 SPOT Timer

Spot 타이머는 일반 타이머와 같은 스케줄링 기계를 쓰되 해당 Spot의 generation에 묶인다. Spot이 파괴되거나 이동해 generation이 끝나면 tick이 더 이상 전달되지 않는다.

```c
void *spot_timer = zlink_spot_timer_new(spot);
zlink_timer_start(spot_timer, 50000000ULL, 10);  /* 50ms, 10회 반복 */
```

### 6.3 Poller 통합

타이머를 소켓, file descriptor와 함께 poller에 등록할 수 있다.

```c
zlink_poller_add_timer(poller, timer, user_data);
/* ... zlink_poller_wait()가 timer 이벤트를 반환 ... */
zlink_poller_remove_timer(poller, timer);
```

### 핵심 규칙

| 규칙 | 설명 |
|------|------|
| `repeat_count=0` | 무한 반복 |
| `repeat_count=N` | 정확히 N회 fire 후 자동 정지 |
| recv vs callback | 충돌 시 `ZLINK_RECV_BUSY` / `ZLINK_HANDLER_BUSY` 반환 (socket 과 동일) |
| 소비 모델 배타 | 한 timer는 recv/callback/poller 중 하나만 — 콜백이 활성이면 poller 등록은 `BUSY`로 거부 |
| 일반 timer | 프로세스 전역 공유 스케줄러 사용 |
| SPOT timer | Spot generation에 묶임 — generation 종료 후 tick 미전달 |

## 7. DEALER/ROUTER 예제

```c
#include <zlink.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    void *ctx = zlink_ctx_new();

    /* ROUTER (server) */
    void *router = zlink_socket(ctx, ZLINK_SOCKET_ROUTER);
    zlink_bind(router, "tcp://*:5555");

    /* DEALER (client) */
    void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
    zlink_connect(dealer, "tcp://127.0.0.1:5555");

    /* DEALER → ROUTER */
    zlink_msg_t req;
    zlink_msg_init_size(&req, 7);
    memcpy(zlink_msg_data(&req), "request", 7);
    zlink_send(dealer, &req, 1, 0);

    /* Server loop: watch a poller for ZLINK_POLLIN on router, drain with
       zlink_router_recv(), and reply with zlink_router_reply() or
       zlink_send_rid(). Client drains replies with zlink_recv() in its
       own poller loop. */

    zlink_close(dealer);
    zlink_close(router);
    zlink_ctx_term(ctx);
    return 0;
}
```

---
<!-- zlink-nav:bottom:start -->
[← 개요](01-overview.ko.md) | [소켓 패턴 →](03-0-socket-patterns.ko.md)
<!-- zlink-nav:bottom:end -->
