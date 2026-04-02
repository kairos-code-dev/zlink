# 성능 특성 및 튜닝 가이드

## 1. Transport별 성능 특성

| Transport | 상대 성능 | 지연시간 | 오버헤드 | 추천 용도 |
|-----------|-----------|---------|----------|-----------|
| inproc | ★★★★★ | 최저 | 없음 | 스레드 간 통신 |
| ipc | ★★★★☆ | 낮음 | 시스템콜 | 로컬 프로세스 간 |
| tcp | ★★★★☆ | 네트워크 | TCP 스택 | 서버 간 통신 |
| ws | ★★★☆☆ | 네트워크 | WebSocket 프레이밍 | 웹 클라이언트 |
| tls/wss | ★★★☆☆ | 네트워크 | 암호화 + 프레이밍 | 보안 필요 시 |

### Transport별 오버헤드 분석

```
inproc:  Lock-free pipe 직접 연결. 시스템콜 없음.
ipc:     Unix 도메인 소켓. TCP 스택 우회.
tcp:     TCP/IP 스택. Nagle 비활성화로 지연 최소화.
ws:      tcp + WebSocket 프레이밍(2~14B 헤더). Binary mode.
wss/tls: ws/tcp + TLS 암호화. 핸드셰이크 + 레코드 오버헤드.
```

## 2. I/O 스레드 수 설정 가이드

=== "C"

    ```c
    void *ctx = zlink_ctx_new();
    zlink_ctx_set(ctx, ZLINK_IO_THREADS, 4);
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

| I/O 스레드 | 추천 사용 사례 | 기준 |
|------------|---------------|------|
| 1 | 소규모 연결 (<100), 단순 패턴 | CPU 코어 1개 사용 |
| 2 (기본) | 일반적 사용 | 대부분의 시나리오에 적합 |
| 4 | 대규모 연결, 높은 처리량 | CPU 코어 4개 이상 |
| 코어 수 | 최대 처리량 | 전용 서버 |

### I/O 스레드 증가 시점

- 소켓 수 × 평균 메시지 레이트가 단일 스레드 처리량을 초과할 때
- 다수의 네트워크 연결(>100)을 동시 처리할 때
- WS/WSS 등 프레이밍 오버헤드가 큰 transport를 다량 사용할 때

### 주의사항

- I/O 스레드는 컨텍스트 생성 후, 소켓 생성 **전에** 설정
- inproc transport는 I/O 스레드를 사용하지 않음 (직접 파이프 연결)
- I/O 스레드를 과도하게 늘리면 컨텍스트 스위칭 오버헤드 발생

## 3. HWM (High Water Mark) 설정 가이드

HWM은 **연결별(per-connection) 큐 크기** 제한이다.
zlink에서 각 연결(pipe)은 독립적인 송수신 큐를 가지며,
HWM은 각 큐가 보관할 수 있는 최대 메시지 수를 설정한다.

=== "C"

    ```c
    int hwm = 100;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));
    zlink_set_option(socket, ZLINK_OPT_RCVHWM, &hwm, sizeof(hwm));
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

| 설정 | 기본값 | 설명 |
|------|--------|------|
| `ZLINK_OPT_SNDHWM` | 1000 | 각 연결의 송신 큐 최대 메시지 수 |
| `ZLINK_OPT_RCVHWM` | 1000 | 각 연결의 수신 큐 최대 메시지 수 |

### Backpressure 동작

HWM에 도달하면 소켓 타입과 송신 플래그에 따라 동작이 달라진다:

- **블로킹 송신** (`flags=0`): `zlink_send()`가 전송 큐에 공간이 생길 때까지 블로킹한다. `ZLINK_OPT_SNDTIMEO`로 대기 시간을 제한할 수 있다.
- **논블로킹 송신** (`ZLINK_DONTWAIT`): 즉시 `EAGAIN`을 반환한다. 애플리케이션이 재시도, 드롭, 외부 버퍼링을 결정한다.

> 상세한 흐름 제어 패턴(DONTWAIT + send-ready handler)은
> 아래 [Send/Recv 흐름 제어](#4-sendrecv-흐름-제어) 섹션을 참고.

### 복구 메커니즘 (LWM)

전송 큐가 HWM에 도달하면 해당 연결의 pipe가 non-writable이 된다.
수신 측이 메시지를 소비하여 큐가 **Low Water Mark (LWM)** 이하로 drain되면
`activate_write` 시그널이 발생하여 다시 writable로 전환된다.

LWM 공식: **`(HWM + 1) / 2`**

이 시점에:
- 블로킹 `zlink_send()` 호출이 재개된다.
- send-ready 핸들러가 호출된다 (설치된 경우).

이 히스테리시스(hysteresis -- 상한과 하한을 다르게 두어 상태 전환에
간격을 만드는 기법)는 writable/non-writable 상태 간의 빠른 진동을 방지한다.

```
예: HWM = 100
    → LWM = (100 + 1) / 2 = 50
    → 큐가 100에 도달하면 블로킹
    → 수신 측이 소비하여 큐가 50 이하가 되면 재개
```

### 실전 HWM 권장값

| 시나리오 | 권장 HWM | 근거 |
|----------|----------|------|
| 일반 소켓/서비스 | ~100 | 연결당 메모리 사용량을 제한하면서 버스트 흡수 |
| STREAM (1000+ CCU) | ~10 | 대규모 동시 접속 시 총 메모리를 연결 수에 비례하여 제한 |
| 기본값 | 1000 | 소규모 연결에서는 충분하지만, 연결 수 증가 시 조정 필요 |

### HWM 동작 패턴

| 소켓 | HWM 초과 시 동작 |
|------|-----------------|
| PUB | 메시지 **드롭** (Slow Subscriber 보호) |
| DEALER | **블록** (기본) 또는 `EAGAIN` (`ZLINK_DONTWAIT`) |
| ROUTER | `ROUTER_MANDATORY` 시 `EHOSTUNREACH`, 아니면 드롭 |
| PAIR | **블록** (기본) 또는 `EAGAIN` |

### 메모리 계산

HWM은 연결별(per-connection)이므로, 총 메모리는 HWM × 메시지 크기 × 연결 수이다.

```
예상 메모리 = SNDHWM × 평균_메시지_크기 × 연결_수

예 1: 일반 서비스 — HWM=100, 메시지=1KB, 연결=1000
      = 100 × 1KB × 1000 = ~100MB

예 2: STREAM 대규모 — HWM=10, 메시지=1KB, 연결=10000
      = 10 × 1KB × 10000 = ~100MB
```

## 4. Send/Recv 흐름 제어

### 4.1 Send Backpressure

sender가 receiver보다 빠르면 메시지가 전송 큐에 누적된다. High Water
Mark(HWM)이 큐 깊이를 제한하며, HWM 도달 시 동작은 소켓 타입과 송신
플래그에 따라 다르다 (위 [HWM 동작 패턴](#hwm-동작-패턴) 참고).

#### 블로킹 송신 (기본)

`flags=0`이면 `zlink_send()`는 전송 큐에 공간이 생길 때까지 블로킹한다.
`ZLINK_OPT_SNDTIMEO`로 블로킹 시간을 제한할 수 있다.

| SNDTIMEO 값 | 동작 |
|-------------|------|
| -1 (기본) | 무한 블로킹 |
| 0 | 즉시 `EAGAIN` 반환 (`ZLINK_DONTWAIT`와 동일) |
| N (ms) | 최대 N밀리초 블로킹 후 `EAGAIN` |

=== "C"

    ```c
    /* Block for at most 1 second */
    int timeout = 1000;
    zlink_set_option(socket, ZLINK_OPT_SNDTIMEO, &timeout, sizeof(timeout));

    zlink_msg_t part;
    zlink_msg_init_size(&part, size);
    memcpy(zlink_msg_data(&part), data, size);
    int rc = zlink_send(socket, &part, 1, 0);
    if (rc == -1 && zlink_errno() == EAGAIN) {
        /* Timed out — queue is still full */
        zlink_msg_close(&part);
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

#### 논블로킹 송신 (DONTWAIT)

`ZLINK_DONTWAIT`를 전달하면 HWM 도달 시 즉시 `EAGAIN`을 반환한다.
애플리케이션이 재시도, 드롭, 외부 버퍼링을 결정한다.

=== "C"

    ```c
    zlink_msg_t part;
    zlink_msg_init_size(&part, size);
    memcpy(zlink_msg_data(&part), data, size);
    int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
    if (rc == -1 && zlink_errno() == EAGAIN) {
        /* HWM reached — handle backpressure */
        zlink_msg_close(&part);
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

#### Send-Ready 핸들러 (이벤트 기반 Backpressure)

`zlink_send_ready_handler()`는 소켓이 non-writable에서
writable로 전환될 때 호출되는 콜백을 설치한다. `ZLINK_DONTWAIT`와
조합하면 반응형 흐름 제어가 가능하다:

1. `ZLINK_DONTWAIT`로 전송.
2. `EAGAIN` 시 전송 중단.
3. send-ready 콜백이 호출되면 전송 재개.

이 API는 모든 send-capable handle(raw 소켓, SPOT, SPOT Node)에서
동일하게 동작한다. 기본적으로 송신 백프레셔는 poller `ZLINK_POLLOUT`으로
감지하며, `zlink_send_ready_handler()`를 등록하면 해당 콜백으로 전환된다.
콜백 등록 이후 data-plane `ZLINK_POLLOUT`은 `EBUSY`로 실패한다.

**동작 규칙:**
- 여러 번 호출하여 콜백을 교체할 수 있다 (이전 핸들러를 atomic으로 덮어씀).
- `NULL` 전달은 `EINVAL` — 한번 등록하면 해제는 불가하고 다른 함수로 교체만 가능하다.
- 자기 콜백 내에서 교체 불가 (`EDEADLK`). 콜백 밖에서는 자유롭게 교체 가능.
- 등록 이후 data-plane poller `ZLINK_POLLOUT`은 `EBUSY`로 실패한다.

!!! note "C API definition -- each binding wraps this into its idiomatic type."

    ```c
    typedef struct {
        void *socket;
        const char *pending_data;
        size_t pending_size;
    } app_state_t;

    void on_send_ready(void *subject, void *userdata)
    {
        app_state_t *state = (app_state_t *)userdata;
        if (state->pending_data) {
            zlink_msg_t part;
            zlink_msg_init_size(&part, state->pending_size);
            memcpy(zlink_msg_data(&part), state->pending_data, state->pending_size);
            int rc = zlink_send(state->socket, &part, 1, ZLINK_DONTWAIT);
            if (rc >= 0)
                state->pending_data = NULL;
            else
                zlink_msg_close(&part);
            /* If still EAGAIN, callback will fire again on next transition */
        }
    }

    /* Install the handler */
    app_state_t state = { .socket = socket };
    zlink_send_ready_handler(socket, on_send_ready, &state);

    /* Send loop */
    zlink_msg_t part;
    zlink_msg_init_size(&part, size);
    memcpy(zlink_msg_data(&part), data, size);
    int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
    if (rc == -1 && zlink_errno() == EAGAIN) {
        zlink_msg_close(&part);
        /* Buffer for retry when send-ready fires */
        state.pending_data = data;
        state.pending_size = size;
    }
    ```

### 4.2 Low Water Mark과 Wake-Up

전송 큐가 HWM에 도달하면 소켓이 non-writable이 된다. 큐가 **low water
mark** `(HWM + 1) / 2`까지 drain되면 다시 writable로 전환된다. 이 시점에:

- 블로킹 `zlink_send()` 호출이 재개된다.
- send-ready 핸들러가 호출된다 (설치되고 armed 상태인 경우).

이 히스테리시스는 writable/non-writable 상태 간의 빠른 진동을 방지한다.

### 4.3 수신 측 흐름 제어

수신 큐는 최대 `ZLINK_OPT_RCVHWM` 메시지를 보관한다. 수신 큐가 가득 차면
sender에 pipe 레벨 backpressure가 적용된다.

=== "C"

    ```c
    int hwm = 500;
    zlink_set_option(socket, ZLINK_OPT_RCVHWM, &hwm, sizeof(hwm));
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

Callback 모드에서 느린 콜백은 I/O 스레드를 블로킹하여 수신 큐가
누적되게 한다. 무거운 작업은 별도 스레드로 오프로드해야 한다:

=== "C"

    ```c
    void on_message(const zlink_routing_id_t *rid,
                    zlink_msg_t *parts, size_t part_count,
                    void *userdata)
    {
        /* BAD: slow processing blocks I/O thread */
        // heavy_computation(parts);

        /* GOOD: enqueue and return quickly */
        work_queue_push(userdata, parts, part_count);
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

> 스레드 안전 작업 큐 패턴은
> [스레드 안전성 가이드](11-thread-safety.ko.md) 섹션 6을 참고.

### 4.4 Callback vs Pull 모드

zlink 소켓은 두 가지 수신 모드를 지원한다. 선택에 따라 스레딩과
흐름 제어 동작이 달라진다.

| 항목 | Callback 모드 | Pull 모드 |
|------|---|---|
| 트리거 | 메시지 도착 시 자동 | `zlink_recv()` 호출 |
| 실행 스레드 | I/O 스레드 | 애플리케이션 스레드 |
| 전환 | 한방향 (영구) | 기본; handler attach 후 불가 |
| DONTWAIT | N/A (항상 비동기) | 메시지 없으면 `EAGAIN` |
| Multipart | `parts[]` 배열로 한번에 | `parts_out` + `part_count_out`으로 전체 반환 |

### 4.5 완전한 Backpressure 예제

`ZLINK_DONTWAIT`, send-ready 핸들러, 애플리케이션 레벨 버퍼를 조합한
전체 예제:

=== "C"

    ```c
    #include <zlink.h>
    #include <string.h>
    #include <stdio.h>

    #define MAX_PENDING 1024

    typedef struct {
        void *socket;
        char *queue[MAX_PENDING];
        size_t sizes[MAX_PENDING];
        int head, tail, count;
    } sender_t;

    static void flush_queue(sender_t *s)
    {
        while (s->count > 0) {
            zlink_msg_t part;
            zlink_msg_init_size(&part, s->sizes[s->head]);
            memcpy(zlink_msg_data(&part), s->queue[s->head], s->sizes[s->head]);
            int rc = zlink_send(s->socket, &part, 1, ZLINK_DONTWAIT);
            if (rc == -1) {
                zlink_msg_close(&part);
                break; /* Still full — wait for next send-ready */
            }
            free(s->queue[s->head]);
            s->head = (s->head + 1) % MAX_PENDING;
            s->count--;
        }
    }

    static void on_send_ready(void *subject, void *userdata)
    {
        flush_queue((sender_t *)userdata);
    }

    int main(void)
    {
        void *ctx = zlink_ctx_new();
        void *socket = zlink_socket(ctx, ZLINK_DEALER);
        zlink_connect(socket, "tcp://127.0.0.1:5555");

        sender_t sender = { .socket = socket };
        zlink_send_ready_handler(socket, on_send_ready, &sender);

        for (int i = 0; i < 100000; i++) {
            char msg[64];
            int len = snprintf(msg, sizeof(msg), "msg-%d", i);

            zlink_msg_t part;
            zlink_msg_init_size(&part, len);
            memcpy(zlink_msg_data(&part), msg, len);
            int rc = zlink_send(socket, &part, 1, ZLINK_DONTWAIT);
            if (rc == -1 && zlink_errno() == EAGAIN) {
                zlink_msg_close(&part);
                /* Enqueue for later delivery */
                if (sender.count < MAX_PENDING) {
                    int idx = (sender.head + sender.count) % MAX_PENDING;
                    sender.queue[idx] = strdup(msg);
                    sender.sizes[idx] = len;
                    sender.count++;
                } else {
                    printf("Application buffer full — dropping message\n");
                }
            }
        }

        zlink_close(socket);
        zlink_ctx_term(ctx);
        return 0;
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

## 5. 소켓 옵션 튜닝 체크리스트

| 옵션 | 기본값 | 튜닝 포인트 |
|------|--------|-------------|
| `ZLINK_OPT_LINGER` | -1 (무한) | 테스트: 0, 프로덕션: 1000~5000ms |
| `ZLINK_OPT_SNDTIMEO` | -1 (무한) | 응답 시간 요구사항에 맞춰 설정 |
| `ZLINK_OPT_RCVTIMEO` | -1 (무한) | 폴링 루프에서 사용 시 설정 |
| `ZLINK_OPT_SNDHWM` | 1000 | 처리량에 맞춰 조정 |
| `ZLINK_OPT_RCVHWM` | 1000 | 처리량에 맞춰 조정 |
| `ZLINK_OPT_MAXMSGSIZE` | -1 (무제한) | STREAM 소켓에서 보안 설정 |

### LINGER 설정

=== "C"

    ```c
    /* Test environment: terminate immediately */
    int linger = 0;
    zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));

    /* Production: wait for unsent messages */
    int linger = 3000;  /* 3 seconds */
    zlink_set_option(socket, ZLINK_OPT_LINGER, &linger, sizeof(linger));
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

### 타임아웃 설정

=== "C"

    ```c
    /* Send timeout: EAGAIN after 1 second */
    int timeout = 1000;
    zlink_set_option(socket, ZLINK_OPT_SNDTIMEO, &timeout, sizeof(timeout));

    /* Receive timeout: EAGAIN after 500ms */
    int timeout = 500;
    zlink_set_option(socket, ZLINK_OPT_RCVTIMEO, &timeout, sizeof(timeout));
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

## 6. 성능 측정 방법

### 기본 처리량 측정

=== "C"

    ```c
    #include <time.h>

    int count = 100000;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < count; i++) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, size);
        memcpy(zlink_msg_data(&part), data, size);
        zlink_send(socket, &part, 1, 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Throughput: %.2f msg/s\n", count / elapsed);
    printf("Throughput: %.2f MB/s\n", (count * size) / elapsed / 1e6);
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

### 지연시간 측정 (Ping-Pong)

=== "C"

    ```c
    /* Client: send ping, measure until pong arrives in callback */
    clock_gettime(CLOCK_MONOTONIC, &start);
    zlink_msg_t ping;
    zlink_msg_init_size(&ping, 4);
    memcpy(zlink_msg_data(&ping), "ping", 4);
    zlink_send(socket, &ping, 1, 0);

    /* Handler callback receives "pong" reply and records end time */
    void on_pong(const zlink_routing_id_t *source_rid,
                 zlink_msg_t *parts, size_t part_count)
    {
        clock_gettime(CLOCK_MONOTONIC, &end);
        double rtt_us = ((end.tv_sec - start.tv_sec) * 1e6 +
                         (end.tv_nsec - start.tv_nsec) / 1e3);
        printf("RTT: %.1f us\n", rtt_us);
        for (size_t i = 0; i < part_count; i++)
            zlink_msg_close(&parts[i]);
    }
    ```

=== "C++"

    ```cpp
    // C++ equivalent -- see C tab for full logic
    ```

=== "Java"

    ```java
    // Java equivalent -- see C tab for full logic
    ```

=== "Python"

    ```python
    # Python equivalent -- see C tab for full logic
    ```

=== "Node/TypeScript"

    ```typescript
    // TypeScript equivalent -- see C tab for full logic
    ```

=== "C#/.NET"

    ```csharp
    // C# equivalent -- see C tab for full logic
    ```

=== "Rust"

    ```rust
    // Rust equivalent -- see C tab for full logic
    ```

=== "Go"

    ```go
    // Go equivalent -- see C tab for full logic
    ```

## 7. 성능 체크리스트

### 기본 설정

- [ ] I/O 스레드 수를 워크로드에 맞게 설정
- [ ] HWM을 예상 처리량에 맞게 조정
- [ ] LINGER를 적절히 설정 (테스트: 0, 프로덕션: 타임아웃)

### 메시지 최적화

- [ ] 소형 메시지(≤33B)는 VSM 활용 (inline 저장)
- [ ] 대용량 메시지는 zero-copy (`zlink_msg_init_data`) 활용
- [ ] 상수/static 페이로드는 `zlink_msg_init_data(..., NULL, NULL)`를 신중히 사용
- [ ] 불필요한 `zlink_msg_copy()` 회피

### Transport 최적화

- [ ] 로컬 통신은 inproc/ipc 사용
- [ ] 암호화가 불필요한 내부 통신은 tcp 사용
- [ ] WS/WSS 사용 시 메시지 크기별 성능 특성 고려

### 모니터링

- [ ] 성능 병목 시 모니터링 API로 연결 상태 확인
- [ ] Slow Subscriber 감지 (PUB/SUB 환경)
- [ ] HWM 도달 빈도 관찰

> Speculative I/O, Gather Write 등 내부 최적화 메커니즘의 상세는 [architecture.md](../internals/architecture.ko.md)를 참고.

---
[← Message API](09-message-api.ko.md) | [스레드 안전성 →](11-thread-safety.ko.md)
