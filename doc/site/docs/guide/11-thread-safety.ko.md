[English](11-thread-safety.md) | [한국어](11-thread-safety.ko.md)

# 스레드 안전성 가이드

## 1. 한줄 요약

**네, zlink 핸들은 스레드 안전(thread-safe)합니다.** 소켓, SPOT, Discovery
핸들 하나를 여러 스레드에서 공유하면서 별도의 mutex나 lock 없이 API를
호출할 수 있습니다.

```
  Thread A --- zlink_send(socket, ...) ---+
  Thread B --- zlink_send(socket, ...) ---+--> same socket, no mutex needed
  Thread C --- zlink_send(socket, ...) ---+
```

**유일하게 스레드 안전하지 않은 것:** `zlink_msg_t`. 메시지 객체는 한 번에
한 스레드에서만 사용해야 합니다
([5절](#5-유일한-예외-zlink_msg_t는-스레드-안전하지-않습니다) 참고).

이 가이드의 나머지 부분에서는 `send`와 `connect`를 함께 쓸 때 어떤 일이
벌어지는지, `close`가 다른 스레드에서 아직 전송 중일 때 어떻게 동작하는지,
콜백에서 주의할 점은 무엇인지 설명합니다.

## 2. 세 가지 API 카테고리

모든 공개 API는 세 가지 카테고리로 나뉩니다:

| 카테고리 | 포함 API | 스레드 안전? | 참고 |
|---|---|---|---|
| **전송** | `send`, `publish`, `send_rid` | 예 -- 동시 호출 가능 | 핫 패스(hot path, 고빈도 데이터 경로) 최적화 |
| **설정·운영** | `bind`, `connect`, `set_option` 등 | 예 -- 순차 처리 | 메시지마다 호출은 비권장 |
| **정리** | `close`, `destroy` | 예 -- 명확한 에러 코드 | 사용 중이면 `ZLINK_CLOSE_BUSY` 반환 |

**요약하면:** `send`는 어느 스레드에서든 마음껏 호출하세요. 연결 추가,
구독 변경, 옵션 변경도 전송 중에 자유롭게 할 수 있습니다. 다 끝나면
핸들을 닫고 반환 코드를 확인하세요.

### 2.1 전송 (핫 패스)

다음 함수들은 같은 핸들에서 완전한 동시 호출을 허용합니다:

- `zlink_send()` — 원시 소켓
- `zlink_publish()` — SPOT
- 콜백 내에서 `send` / `publish` 호출도 안전

**순서:**
- 하나의 스레드에서는 `send`를 호출한 순서대로 메시지가 전달됩니다.
- 여러 스레드에서 동시에 보낼 때는 각 메시지가 온전하게 전달되지만,
  스레드 간 순서는 보장되지 않습니다 (먼저 도착한 스레드가 먼저 전송).

**예제 — 4개 워커 스레드, 1개 소켓 공유:**

```c
#include <zlink.h>
#include <pthread.h>
#include <string.h>

typedef struct { void *socket; int id; } worker_arg_t;

void *worker(void *arg)
{
    worker_arg_t *w = (worker_arg_t *)arg;
    char buf[64];
    for (int i = 0; i < 10000; i++) {
        int len = snprintf(buf, sizeof(buf), "worker-%d msg-%d", w->id, i);
        zlink_msg_t part;
        zlink_msg_init_size(&part, (size_t)len);
        memcpy(zlink_msg_data(&part), buf, (size_t)len);
        zlink_send(w->socket, &part, 1, ZLINK_SEND_FLAGS_NONE); /* mutex 불필요 */
    }
    return NULL;
}

int main(void)
{
    void *ctx = zlink_ctx_new();
    void *socket = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
    zlink_connect(socket, "tcp://127.0.0.1:5555");

    pthread_t threads[4];
    worker_arg_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = (worker_arg_t){socket, i};
        pthread_create(&threads[i], NULL, worker, &args[i]);
    }
    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);

    zlink_close(socket);
    zlink_ctx_term(ctx);
    return 0;
}
```

### 2.2 설정·운영 (설정 및 런타임 변경)

이 함수들은 스레드 안전하며, 라이브러리가 한 번에 하나씩 처리합니다.
가끔 하는 작업(새 엔드포인트 연결, 옵션 변경)에는 문제 없지만,
메시지마다 호출하는 건 피하세요.

포함:
- `zlink_bind()` / `zlink_connect()` / `zlink_disconnect()`
- `zlink_set_option()` / `zlink_get_option()`
- `zlink_set_subscription()` / `zlink_unset_subscription()`
- `zlink_spot_node_attach_discovery()`
- `zlink_socket_monitor_open()`
- `zlink_send_ready_handler()`
- `zlink_set_option()`
- `zlink_registry_add_peer()` / `zlink_registry_set_heartbeat()`
- 조회/스냅샷 함수

**전송과 설정을 자유롭게 섞을 수 있습니다.** 예를 들어, 한 스레드가
메시지를 보내는 동안 다른 스레드가 추가 엔드포인트를 연결할 수 있습니다:

```c
void *send_thread(void *arg)
{
    void *socket = arg;
    char buf[] = "data";
    for (int i = 0; i < 100000; i++)
        zlink_msg_t part;
        zlink_msg_init_size(&part, sizeof(buf) - 1);
        memcpy(zlink_msg_data(&part), buf, sizeof(buf) - 1);
        zlink_send(socket, &part, 1, 0);  /* hot path */
    return NULL;
}

void *setup_thread(void *arg)
{
    void *socket = arg;
    /* These are safe to call while send_thread is running */
    zlink_connect(socket, "tcp://10.0.0.2:5555");
    zlink_connect(socket, "tcp://10.0.0.3:5555");

    int hwm = 5000;
    zlink_set_option(socket, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm));
    return NULL;
}
```

`ZLINK_OPT_EVENTS`, `ZLINK_OPT_LAST_ENDPOINT` 같은 경량 읽기도 이 카테고리에
속하지만, 무거운 조회/스냅샷 호출보다 오버헤드가 적습니다.

## 3. 핸들별 요약표

모든 핸들 타입이 동일한 세 가지 카테고리 모델을 따릅니다:

| 핸들 | 전송 | 설정·운영 | 정리 |
|---|---|---|---|
| Socket | `send` | bind, connect, set_option 등 | `close` |
| SPOT | `publish` | subscribe, unsubscribe 등 | `destroy` |
| SPOT Node | *(송신 없음; 데이터 평면은 `Spot` 사용)* | bind, connect_peer 등 | `destroy` |
| Discovery | *(없음)* | connect_registry 등 | `destroy` |
| Registry | *(없음)* | bind, add_peer 등 | `destroy` |

## 4. 핸들 안전하게 닫기

다른 스레드가 아직 사용 중인 핸들을 닫아도 크래시하지 않습니다 —
zlink가 명확한 에러 코드를 반환합니다:

| 상황 | 동작 | 결과 |
|---|---|---|
| 다른 스레드가 핸들 사용 중 `close` 호출 | 닫기 **거부** | `ZLINK_CLOSE_BUSY` |
| `close` 수락 후 API 호출 | 호출 **거부** | `ZLINK_CLOSE_SHUTDOWN` (또는 해당 함수군 `*_TERMINATED`) |
| `close`/`destroy` 두 번 호출 | 즉시 반환 | `ZLINK_CLOSE_SHUTDOWN` |

`ZLINK_CLOSE_BUSY` 이후에는 핸들이 정상 상태로 돌아갑니다 — 아무것도
손상되지 않았으며, 계속 사용하거나 나중에 다시 닫을 수 있습니다.

**권장 종료 패턴:**

```c
#include <zlink.h>
#include <errno.h>
#include <stdatomic.h>

atomic_int g_running = 1;

/* Worker threads check g_running and also handle ESHUTDOWN */
void *sender(void *arg)
{
    void *socket = arg;
    while (atomic_load(&g_running)) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, 32);
        zlink_submit_result_t rc = zlink_send(socket, &part, 1, 0);
        if (rc == ZLINK_SUBMIT_TERMINATED)
            break;  /* handle is shutting down, stop gracefully */
    }
    return NULL;
}

void shutdown_socket(void *socket)
{
    /* Step 1: tell workers to stop */
    atomic_store(&g_running, 0);

    /* Step 2: give workers a moment to finish, then close */
    msleep(50);
    zlink_close(socket);
}
```

**콜백에서의 self-close:** send-ready 또는 monitor 콜백에서 자기 핸들의
`close`를 호출하면, 실제 닫기는 콜백이 반환될 때까지 지연됩니다.
콜백 내 use-after-free를 방지합니다.

## 5. 유일한 예외: zlink_msg_t는 스레드 안전하지 않습니다

핸들은 스레드 안전합니다. 메시지 객체는 아닙니다. 각 `zlink_msg_t`는
한 번에 하나의 스레드에서만 사용해야 합니다.

이는 보통 자연스럽습니다 — 각 스레드에서 스택이나 힙에 메시지를 만들면
됩니다:

```c
/* WRONG — two threads sharing the same msg */
zlink_msg_t msg;
zlink_msg_init_size(&msg, 100);
/* Thread A: */ zlink_send(socket, &msg, 1, 0);
/* Thread B: */ zlink_send(socket, &msg, 1, 0);  /* data race! */
```

```c
/* RIGHT — each thread makes its own msg */
/* Thread A */                       /* Thread B */
zlink_msg_t msg_a;                   zlink_msg_t msg_b;
zlink_msg_init_size(&msg_a, 100);    zlink_msg_init_size(&msg_b, 100);
memcpy(zlink_msg_data(&msg_a),...);  memcpy(zlink_msg_data(&msg_b),...);
zlink_send(socket, &msg_a, 1, 0);    zlink_send(socket, &msg_b, 1, 0);  /* safe */
```

**콜백 소유권:** 콜백이 `zlink_msg_t *parts`를 받으면 소유권이 콜백으로
넘어갑니다. 반환 전에 각 part를 `zlink_msg_close()`하고, 다른 스레드에서
접근하면 안 됩니다.

## 6. 콜백 규칙

모든 콜백(메시지, SPOT, XPUB, 모니터, send-ready)은 I/O 스레드에서
실행됩니다. 알아야 할 것:

**콜백에서 할 수 있는 것:**
- 같은 핸들로 `send` / `publish` 호출 — 이것이 권장되는
  request-reply 패턴입니다.
- 메시지 데이터를 읽고 자체 큐에 넣기.

**콜백에서 하면 안 되는 것:**
- **블로킹** (sleep, lock, 무거운 연산) — 해당 스레드의 모든 I/O가
  멈춥니다. 작업을 큐에 넣고 워커 스레드에서 처리하세요.
- **send-ready 콜백 안에서 자기 핸들러 교체** — `ZLINK_HANDLER_DEADLOCK`
  을 반환합니다.

**오프로드 패턴 — 콜백을 빠르게 유지:**

```c
void on_message(const zlink_routing_id_t *source_rid,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    for (size_t i = 0; i < part_count; i++) {
        /* Push to your own thread-safe queue */
        app_queue_push(app_queue,
                       zlink_msg_data(&parts[i]),
                       zlink_msg_size(&parts[i]));
        zlink_msg_close(&parts[i]);
    }
    /* Return quickly — a worker thread processes the queue */
}
```

## 7. 실용 패턴

### 7.1 멀티 스레드 워커 풀 (Socket)

여러 스레드가 하나의 소켓을 통해 전송 — 잠금 불필요:

```c
typedef struct { void *socket; int id; } socket_worker_t;

void *socket_worker(void *arg)
{
    socket_worker_t *w = (socket_worker_t *)arg;
    for (int i = 0; i < 50000; i++) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, 64);
        snprintf(zlink_msg_data(&part), 64, "worker-%d-%d", w->id, i);
        zlink_send(w->socket, &part, 1, 0);
    }
    return NULL;
}

void run_socket_pool(void *socket)
{
    pthread_t threads[4];
    socket_worker_t args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = (socket_worker_t){socket, i};
        pthread_create(&threads[i], NULL, socket_worker, &args[i]);
    }
    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);
}
```

### 7.2 전송 중 구독 변경

한 스레드가 발행하고, 다른 스레드가 런타임에 구독을 관리:

```c
/* Data thread: publishes at high frequency */
void *publisher(void *arg)
{
    void *spot = arg;
    for (int i = 0; i < 100000; i++) {
        zlink_msg_t part;
        zlink_msg_init_size(&part, 16);
        zlink_publish(spot, "prices", &part, 1, 0);
    }
    return NULL;
}

/* Control thread: adjusts subscriptions while publisher is running */
void *control(void *arg)
{
    void *spot = arg;
    msleep(100);
    zlink_set_subscription(spot, "audit.*");      /* safe while publishing */
    msleep(200);
    zlink_unset_subscription(spot, "audit.*");    /* also safe */
    return NULL;
}
```

## 8. 흔한 실수

| 실수 | 이유 | 해결 |
|---|---|---|
| 같은 `zlink_msg_t` 공유 | msg는 스레드 안전하지 않음 | 스레드별 별도 msg 생성 |
| 콜백 경로에서 무거운 작업 | 내부 worker 또는 I/O 경로 정체 | 큐로 오프로드 |
| `close` 후 API 호출 | `ZLINK_CLOSE_SHUTDOWN` (또는 `*_TERMINATED`) 반환 | 반환 코드 확인 |
| 메시지마다 `set_option` | 순차 처리 오버헤드 | 변경 시에만 호출 |

## 9. 에러 코드 요약표

| 결과 | 발생 시점 | 의미 |
|---|---|---|
| `ZLINK_CLOSE_BUSY` | 사용 중 `close` 호출 | 대기 후 재시도 |
| `ZLINK_CLOSE_SHUTDOWN` | `close` 후 API 호출 (또는 중복 `close`) | 핸들 종료 중 |
| `ZLINK_HANDLER_DEADLOCK` | 콜백 내 핸들러 교체 | 외부에서 교체 필요 |
| 함수군 `*_TERMINATED` | 컨텍스트 `term` 이후 데이터 호출 | 컨텍스트 종료됨 — 사용 중단 |

> 구현 세부 사항(admission gate, 순서 의미론, 비용 모델)은
> [Thread-Safety Internals](../internals/thread-safety.ko.md)를 참고.

---
[← 성능](10-performance.ko.md) | [소켓 옵션 →](12-socket-options.ko.md)
