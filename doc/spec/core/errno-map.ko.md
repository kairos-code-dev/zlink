[English](errno-map.md) | [한국어](errno-map.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# 함수별 결과 Enum

이 문서는 모든 공개 C API 함수의 결과를 정규화하는 기준을 정의한다.
각 함수는 API 전체에서 숫자 범위가 겹치지 않는 결과 enum을 반환하므로,
하나의 `int` 에러 코드만으로도 항상 의미를 식별할 수 있다.

내부 구현 경로는 계속 상세 `errno`를 사용하고, exported API 경계에서
그 값을 여기 정의한 공개 결과 enum으로 정규화한다.

## zlink_errno — 내부 에러 상세

`zlink_errno()`는 호출 스레드의 raw 내부 `errno` 값을 반환한다.
**result enum이 `INTERNAL_ERROR` 또는 동등한 최종 코드를 반환할 때만
유용하다.** 그 외에는 result enum 자체가 에러 정보 전부를 담고 있다.

```c
zlink_submit_result_t rc = zlink_send(s, parts, count, 0);
if (rc == ZLINK_SUBMIT_INTERNAL_ERROR) {
    int detail = zlink_errno();          /* 예: EPROTO, ENOMEM */
    const char *msg = zlink_strerror(detail);
    log("internal error: %s", msg);
}
```

- 일반 결과 코드 (`BACKPRESSURED`, `BUSY`, `TERMINATED` 등)에는
  `zlink_errno()` 호출이 **필요 없다** — result enum으로 충분하다.
- `zlink_errno()`는 thread-local last-error 저장소 (GetLastError 패턴)다.
  가장 최근 실패만 보존된다.

---

## zlink_submit_result_t

send, request submit, reply submit 함수에 적용된다.

```c
typedef enum zlink_submit_result_t
{
    /* Submit succeeded. */
    ZLINK_SUBMIT_OK = 0,

    /* Normal control-flow result. */
    ZLINK_SUBMIT_BACKPRESSURED    = 1,
    ZLINK_SUBMIT_NOT_CONNECTED    = 2,
    ZLINK_SUBMIT_NOT_FOUND        = 3,

    /* Runtime / lifecycle failure. */
    ZLINK_SUBMIT_TERMINATED       = 4,

    /* Caller contract violation. */
    ZLINK_SUBMIT_INVALID_HANDLE   = 5,
    ZLINK_SUBMIT_INVALID_ARGUMENT = 6,
    ZLINK_SUBMIT_NOT_SUPPORTED    = 7,
    ZLINK_SUBMIT_INVALID_STATE    = 8,
    ZLINK_SUBMIT_THREAD_VIOLATION = 9,

    /* Internal failure. */
    ZLINK_SUBMIT_OUT_OF_MEMORY    = 10,
    ZLINK_SUBMIT_SEQ_EXHAUSTED    = 11,
    ZLINK_SUBMIT_INTERNAL_ERROR   = 12
} zlink_submit_result_t;
```

### Submit Result 분류

#### 정상 제어 흐름 결과

호출자가 직접 처리할 수 있는 정상 결과다.

| Result | 내부 errno | 의미 |
|---|---|---|
| `OK` | -- | submit 성공 |
| `BACKPRESSURED` | `EAGAIN` | send 큐가 가득 찼거나 (HWM) 아직 쓰기 준비가 안 됨 |
| `NOT_CONNECTED` | `ENOTCONN`, `EHOSTUNREACH` | 대상 peer 또는 경로가 연결되지 않음 |
| `NOT_FOUND` | `ENOENT` | 대상 peer, spot, routed destination을 찾지 못함 |

#### 런타임 / 수명주기 실패

| Result | 내부 errno | 의미 |
|---|---|---|
| `TERMINATED` | `ETERM` | context가 종료됨 |

#### 호출자 계약 위반

호출자 쪽 버그를 나타낸다.

| Result | 내부 errno | 의미 |
|---|---|---|
| `INVALID_HANDLE` | `EFAULT` | NULL 핸들 또는 잘못된 포인터 |
| `INVALID_ARGUMENT` | `EINVAL` | 소켓 타입 불일치, NULL 핸들러, `request_seq` 0, 잘못된 routing ID |
| `NOT_SUPPORTED` | `ENOTSUP` | 지원하지 않는 작업 또는 잘못된 flags |
| `INVALID_STATE` | `EFSM` | 소켓이 잘못된 상태에 있음 |
| `THREAD_VIOLATION` | `EMTHREAD` | 허용된 스레드 모델을 위반함 |

#### 내부 실패

| Result | 내부 errno | 의미 |
|---|---|---|
| `OUT_OF_MEMORY` | `ENOMEM` | 메모리 할당 실패 |
| `SEQ_EXHAUSTED` | `EBUSY` | request sequence 번호 공간 소진 (request 전용) |
| `INTERNAL_ERROR` | `EPROTO` 및 그 외 내부 submit 실패 | 내부 send/request/reply submit 오류 |

### 적용 대상 함수

| 분류 | 함수 |
|---|---|
| Send | `zlink_send`, `zlink_send_rid`, `zlink_publish` |
| Socket request | `zlink_dealer_request`, `zlink_router_request` |
| Socket reply | `zlink_router_reply` |
| SPOT request | `zlink_spot_request_spot`, `zlink_spot_request_router`, `zlink_router_request_spot` |
| SPOT send | `zlink_spot_send_spot`, `zlink_spot_send_router`, `zlink_router_send_spot` |
| SPOT reply | `zlink_spot_reply_spot`, `zlink_spot_reply_router`, `zlink_router_reply_spot` |

---

## zlink_request_result_t

`zlink_reply_handler_fn`으로 전달되는 request completion에 적용된다.
이 enum은 submit 결과와 별개로, 성공적으로 submit된 후의 요청 결과를 나타낸다.

```c
typedef enum zlink_request_result_t
{
    ZLINK_REQUEST_OK              = 0,
    ZLINK_REQUEST_TIMED_OUT       = 101,
    ZLINK_REQUEST_NOT_FOUND       = 102,
    ZLINK_REQUEST_TERMINATED      = 103,
    ZLINK_REQUEST_PROTOCOL_ERROR  = 104
} zlink_request_result_t;
```

### Request Completion 분류

| Result | callback errno | 의미 |
|---|---|---|
| `OK` | `0` | reply payload를 정상 수신함 |
| `TIMED_OUT` | `ETIMEDOUT` | `timeout_ms` 안에 reply가 도착하지 않음 |
| `NOT_FOUND` | `ENOENT` | 대상이 없어 error reply로 요청이 완료됨 |
| `TERMINATED` | `ETERM` | request 경로가 명시적인 종료 completion을 방출하기 전까지는 예약값 |
| `PROTOCOL_ERROR` | `EPROTO` | reply envelope 또는 error reply payload가 잘못됨 |

### 적용 대상 함수

`zlink_reply_handler_fn`의 첫 번째 인자 `errno_`는 request completion
결과다. 현재 구현에서 확인되는 completion `errno_` 값은 `0`,
`ETIMEDOUT`, `ENOENT`, `EPROTO`다. `ETERM`은 예약값이며, 현재 request
코드에는 이를 명시적으로 completion으로 방출하는 경로가 아직 없다.

---

## zlink_recv_result_t

recv, subscribe, subscription event, monitor recv, timer recv 함수에
적용된다.

```c
typedef enum zlink_recv_result_t
{
    ZLINK_RECV_OK                 = 0,
    ZLINK_RECV_NO_DATA            = 201,  /* EAGAIN    */
    ZLINK_RECV_BUSY               = 202,  /* EBUSY     */
    ZLINK_RECV_TERMINATED         = 203,  /* ETERM     */
    ZLINK_RECV_INVALID_HANDLE     = 204,  /* EFAULT    */
    ZLINK_RECV_NOT_SUPPORTED      = 205   /* ENOTSUP   */
} zlink_recv_result_t;
```

### Recv Result 분류

| Result | 내부 errno | 의미 |
|---|---|---|
| `OK` | -- | 데이터를 정상 수신함 |
| `NO_DATA` | `EAGAIN` | 비차단 모드에서 수신할 데이터가 없음 |
| `BUSY` | `EBUSY` | 핸들러가 이미 등록되어 있음 |
| `TERMINATED` | `ETERM` | context가 종료됨 |
| `INVALID_HANDLE` | `EFAULT` | NULL 또는 잘못된 핸들 |
| `NOT_SUPPORTED` | `ENOTSUP` | recv를 지원하지 않는 소켓 타입 |

### 적용 대상 함수

| 분류 | 함수 |
|---|---|
| Recv | `zlink_recv` |
| Subscribe | `zlink_subscribe` |
| Subscription event | `zlink_subscription_event` |
| Monitor recv | `zlink_monitor_recv` |
| Timer recv | `zlink_timer_recv` |

---

## zlink_handler_result_t

모든 핸들러 등록 함수에 적용된다: recv handler, subscribe handler,
send-ready handler, router handler, spot handler, router-spot handler,
spot dispatch-event handler, monitor handler.

```c
typedef enum zlink_handler_result_t
{
    ZLINK_HANDLER_OK              = 0,
    ZLINK_HANDLER_INVALID_ARGUMENT = 301, /* EINVAL    */
    ZLINK_HANDLER_BUSY            = 302,  /* EBUSY     */
    ZLINK_HANDLER_NOT_SUPPORTED   = 303,  /* ENOTSUP   */
    ZLINK_HANDLER_DEADLOCK        = 304,  /* EDEADLK   */
    ZLINK_HANDLER_INVALID_HANDLE  = 305   /* EFAULT    */
} zlink_handler_result_t;
```

### Handler Result 분류

| Result | 내부 errno | 의미 |
|---|---|---|
| `OK` | -- | 핸들러 등록 성공 |
| `INVALID_ARGUMENT` | `EINVAL` | NULL 핸들러 |
| `BUSY` | `EBUSY` | 핸들러가 이미 등록되어 있음 |
| `NOT_SUPPORTED` | `ENOTSUP` | 지원하지 않는 subject |
| `DEADLOCK` | `EDEADLK` | 재진입 호출 (send-ready handler 전용) |
| `INVALID_HANDLE` | `EFAULT` | NULL 또는 잘못된 핸들 |

### 적용 대상 함수

| 분류 | 함수 |
|---|---|
| Recv handler | `zlink_recv_handler` |
| Subscribe handler | `zlink_subscribe_handler` |
| Send-ready handler | `zlink_send_ready_handler` |
| Router handler | `zlink_router_handler` |
| Spot handler | `zlink_spot_handler` |
| Router-spot handler | `zlink_router_spot_handler` |
| Spot dispatch-event handler | `zlink_spot_dispatch_event_handler` |
| Monitor handler | `zlink_monitor_handler` |

---

## zlink_close_result_t

close와 destroy 함수에 적용된다.

```c
typedef enum zlink_close_result_t
{
    ZLINK_CLOSE_OK                = 0,
    ZLINK_CLOSE_BUSY              = 401,  /* EBUSY     */
    ZLINK_CLOSE_SHUTDOWN          = 402,  /* ESHUTDOWN */
    ZLINK_CLOSE_INVALID_HANDLE    = 403   /* EFAULT    */
} zlink_close_result_t;
```

### Close Result 분류

| Result | 내부 errno | 의미 |
|---|---|---|
| `OK` | -- | close/destroy 성공 |
| `BUSY` | `EBUSY` | 진행 중인 콜백 또는 API 호출이 있음 |
| `SHUTDOWN` | `ESHUTDOWN` | 이미 닫혀 있음 |
| `INVALID_HANDLE` | `EFAULT` | NULL 또는 잘못된 핸들 |

### 적용 대상 함수

| 분류 | 함수 |
|---|---|
| Close | `zlink_close` |
| Destroy | `zlink_destroy` |

---

## zlink_bind_result_t

bind 함수에 적용된다.

```c
typedef enum zlink_bind_result_t
{
    ZLINK_BIND_OK                 = 0,
    ZLINK_BIND_INVALID_ARGUMENT   = 501,  /* EINVAL    */
    ZLINK_BIND_ADDR_IN_USE        = 502,  /* EADDRINUSE */
    ZLINK_BIND_NOT_SUPPORTED      = 503,  /* ENOTSUP   */
    ZLINK_BIND_INVALID_HANDLE     = 504   /* EFAULT    */
} zlink_bind_result_t;
```

### Bind Result 분류

| Result | 내부 errno | 의미 |
|---|---|---|
| `OK` | -- | bind 성공 |
| `INVALID_ARGUMENT` | `EINVAL` | 잘못된 endpoint |
| `ADDR_IN_USE` | `EADDRINUSE` | 주소가 이미 바인딩되어 있음 |
| `NOT_SUPPORTED` | `ENOTSUP` | 지원하지 않는 transport |
| `INVALID_HANDLE` | `EFAULT` | NULL 또는 잘못된 핸들 |

### 적용 대상 함수

| 분류 | 함수 |
|---|---|
| Bind | `zlink_bind` |

---

## zlink_connect_result_t

connect, disconnect, unbind 함수에 적용된다.

```c
typedef enum zlink_connect_result_t
{
    ZLINK_CONNECT_OK              = 0,
    ZLINK_CONNECT_INVALID_ARGUMENT = 601, /* EINVAL    */
    ZLINK_CONNECT_NOT_SUPPORTED   = 602,  /* ENOTSUP   */
    ZLINK_CONNECT_INVALID_HANDLE  = 603   /* EFAULT    */
} zlink_connect_result_t;
```

### Connect Result 분류

| Result | 내부 errno | 의미 |
|---|---|---|
| `OK` | -- | connect/disconnect/unbind 성공 |
| `INVALID_ARGUMENT` | `EINVAL` | 잘못된 endpoint |
| `NOT_SUPPORTED` | `ENOTSUP` | 지원하지 않는 transport |
| `INVALID_HANDLE` | `EFAULT` | NULL 또는 잘못된 핸들 |

### 적용 대상 함수

| 분류 | 함수 |
|---|---|
| Connect | `zlink_connect` |
| Disconnect | `zlink_disconnect` |
| Unbind | `zlink_unbind` |

---

## zlink_config_result_t

set/get option, 설정, snapshot, message, poller, proxy 함수에 적용된다.

```c
typedef enum zlink_config_result_t
{
    ZLINK_CONFIG_OK               = 0,
    ZLINK_CONFIG_INVALID_HANDLE   = 701,  /* EFAULT    */
    ZLINK_CONFIG_INVALID_ARGUMENT = 702,  /* EINVAL    */
    ZLINK_CONFIG_NOT_SUPPORTED    = 703   /* ENOTSUP   */
} zlink_config_result_t;
```

### Config Result 분류

| Result | 내부 errno | 의미 |
|---|---|---|
| `OK` | -- | 설정 성공 |
| `INVALID_HANDLE` | `EFAULT` | NULL 또는 잘못된 핸들 |
| `INVALID_ARGUMENT` | `EINVAL` | 잘못된 파라미터 |
| `NOT_SUPPORTED` | `ENOTSUP` | 지원하지 않는 옵션 |

### 적용 대상 함수

| 분류 | 함수 |
|---|---|
| Socket option | `zlink_set_option`, `zlink_get_option` |
| Routing ID | `zlink_set_routing_id`, `zlink_get_routing_id` |
| TLS | `zlink_set_tls` |
| Subscription | `zlink_set_subscription`, `zlink_unset_subscription`, `zlink_subscription_at` |
| Discovery | `zlink_attach_discovery` |
| Registry/discovery config | registry와 discovery 설정 함수 |
| Snapshot | snapshot 함수 |
| Message | message 함수 |
| Poller | poller 함수 |
| Proxy | proxy 함수 |

---

## Submit 매트릭스

`Y` = submit 경로가 이 정규화 결과를 낼 수 있음.
`--` = submit 경로가 이 정규화 결과를 내지 않음.

### Send 함수

| Result | `zlink_send` | `zlink_send_rid` | `zlink_publish` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | -- | Y | -- |
| `NOT_FOUND` | -- | -- | -- |
| `TERMINATED` | Y | Y | Y |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | Y | Y | Y |
| `THREAD_VIOLATION` | Y | Y | Y |
| `OUT_OF_MEMORY` | -- | -- | -- |
| `SEQ_EXHAUSTED` | -- | -- | -- |
| `INTERNAL_ERROR` | -- | -- | -- |

### Socket request 함수

| Result | `zlink_dealer_request` | `zlink_router_request` |
|---|---|---|
| `OK` | Y | Y |
| `BACKPRESSURED` | Y | Y |
| `NOT_CONNECTED` | -- | -- |
| `NOT_FOUND` | -- | -- |
| `TERMINATED` | Y | Y |
| `INVALID_HANDLE` | Y | Y |
| `INVALID_ARGUMENT` | Y | Y |
| `NOT_SUPPORTED` | Y | Y |
| `INVALID_STATE` | Y | Y |
| `THREAD_VIOLATION` | Y | Y |
| `OUT_OF_MEMORY` | Y | Y |
| `SEQ_EXHAUSTED` | Y | Y |
| `INTERNAL_ERROR` | Y | Y |

### Socket reply 함수

| Result | `zlink_router_reply` |
|---|---|
| `OK` | Y |
| `BACKPRESSURED` | Y |
| `NOT_CONNECTED` | -- |
| `NOT_FOUND` | -- |
| `TERMINATED` | Y |
| `INVALID_HANDLE` | Y |
| `INVALID_ARGUMENT` | Y |
| `NOT_SUPPORTED` | Y |
| `INVALID_STATE` | Y |
| `THREAD_VIOLATION` | Y |
| `OUT_OF_MEMORY` | -- |
| `SEQ_EXHAUSTED` | -- |
| `INTERNAL_ERROR` | Y |

### SPOT request 함수

| Result | `zlink_spot_request_spot` | `zlink_spot_request_router` | `zlink_router_request_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | -- | -- | -- |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | -- | -- | Y |
| `THREAD_VIOLATION` | -- | -- | Y |
| `OUT_OF_MEMORY` | Y | Y | Y |
| `SEQ_EXHAUSTED` | -- | -- | Y |
| `INTERNAL_ERROR` | Y | Y | Y |

### SPOT send 함수

| Result | `zlink_spot_send_spot` | `zlink_spot_send_router` | `zlink_router_send_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | -- | -- | -- |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | -- | -- | Y |
| `THREAD_VIOLATION` | -- | -- | Y |
| `OUT_OF_MEMORY` | -- | -- | -- |
| `SEQ_EXHAUSTED` | -- | -- | -- |
| `INTERNAL_ERROR` | Y | Y | Y |

### SPOT reply 함수

| Result | `zlink_spot_reply_spot` | `zlink_spot_reply_router` | `zlink_router_reply_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | -- | -- | -- |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | -- | -- | Y |
| `THREAD_VIOLATION` | -- | -- | Y |
| `OUT_OF_MEMORY` | -- | -- | -- |
| `SEQ_EXHAUSTED` | -- | -- | -- |
| `INTERNAL_ERROR` | Y | Y | Y |

---

## 전체 함수 목록 (결과 enum별)

| Result enum | 함수 |
|---|---|
| `zlink_submit_result_t` | `zlink_send`, `zlink_send_rid`, `zlink_publish`, `zlink_dealer_request`, `zlink_router_request`, `zlink_router_reply`, `zlink_spot_request_spot`, `zlink_spot_request_router`, `zlink_router_request_spot`, `zlink_spot_send_spot`, `zlink_spot_send_router`, `zlink_router_send_spot`, `zlink_spot_reply_spot`, `zlink_spot_reply_router`, `zlink_router_reply_spot` |
| `zlink_request_result_t` | `zlink_reply_handler_fn` (completion callback) |
| `zlink_recv_result_t` | `zlink_recv`, `zlink_subscribe`, `zlink_subscription_event`, `zlink_monitor_recv`, `zlink_timer_recv` |
| `zlink_handler_result_t` | `zlink_recv_handler`, `zlink_subscribe_handler`, `zlink_send_ready_handler`, `zlink_router_handler`, `zlink_spot_handler`, `zlink_router_spot_handler`, `zlink_spot_dispatch_event_handler`, `zlink_monitor_handler` |
| `zlink_close_result_t` | `zlink_close`, `zlink_destroy` |
| `zlink_bind_result_t` | `zlink_bind` |
| `zlink_connect_result_t` | `zlink_connect`, `zlink_disconnect`, `zlink_unbind` |
| `zlink_config_result_t` | `zlink_set_option`, `zlink_get_option`, `zlink_set_routing_id`, `zlink_get_routing_id`, `zlink_set_tls`, `zlink_set_subscription`, `zlink_unset_subscription`, `zlink_subscription_at`, `zlink_attach_discovery` 및 registry/discovery/snapshot/message/poller/proxy 설정 함수 |
