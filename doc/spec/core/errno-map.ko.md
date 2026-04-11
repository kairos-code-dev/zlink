[English](errno-map.md) | [한국어](errno-map.ko.md)

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md)

# Submit Result 와 Request Completion

이 문서는 send, request, reply API의 결과를 정규화하는 기준을 정의한다.

공개 C API는 이제 send, request submit, reply submit에
`zlink_submit_result_t`를 반환한다. 내부 구현 경로는 계속 상세 `errno`를
사용하고, exported API 경계에서 그 값을 여기 정의한 공개 결과 enum으로
정규화한다.

## zlink_submit_result_t

```c
typedef enum zlink_submit_result_t
{
    /* Submit succeeded. */
    ZLINK_SUBMIT_OK = 0,

    /* Normal control-flow result. */
    ZLINK_SUBMIT_BACKPRESSURED = 1,
    ZLINK_SUBMIT_NOT_CONNECTED = 2,
    ZLINK_SUBMIT_NOT_FOUND = 3,

    /* Runtime / lifecycle failure. */
    ZLINK_SUBMIT_TERMINATED = 4,

    /* Caller contract violation. */
    ZLINK_SUBMIT_INVALID_HANDLE = 5,
    ZLINK_SUBMIT_INVALID_ARGUMENT = 6,
    ZLINK_SUBMIT_NOT_SUPPORTED = 7,
    ZLINK_SUBMIT_INVALID_STATE = 8,
    ZLINK_SUBMIT_THREAD_VIOLATION = 9,

    /* Internal failure. */
    ZLINK_SUBMIT_OUT_OF_MEMORY = 10,
    ZLINK_SUBMIT_SEQ_EXHAUSTED = 11,
    ZLINK_SUBMIT_INTERNAL_ERROR = 12
} zlink_submit_result_t;
```

## Submit Result 분류

### 정상 제어 흐름 결과

호출자가 직접 처리할 수 있는 정상 결과다.

| Result | 내부 errno | 의미 |
|---|---|---|
| `OK` | — | submit 성공 |
| `BACKPRESSURED` | `EAGAIN` | send 큐가 가득 찼거나 (HWM) 아직 쓰기 준비가 안 됨 |
| `NOT_CONNECTED` | `ENOTCONN`, `EHOSTUNREACH` | 대상 peer 또는 경로가 연결되지 않음 |
| `NOT_FOUND` | `ENOENT` | 대상 peer, spot, routed destination을 찾지 못함 |

### 런타임 / 수명주기 실패

| Result | 내부 errno | 의미 |
|---|---|---|
| `TERMINATED` | `ETERM` | context가 종료됨 |

### 호출자 계약 위반

호출자 쪽 버그를 나타낸다.

| Result | 내부 errno | 의미 |
|---|---|---|
| `INVALID_HANDLE` | `EFAULT` | NULL 핸들 또는 잘못된 포인터 |
| `INVALID_ARGUMENT` | `EINVAL` | 소켓 타입 불일치, NULL 핸들러, `request_seq` 0, 잘못된 routing ID |
| `NOT_SUPPORTED` | `ENOTSUP` | 지원하지 않는 작업 또는 잘못된 flags |
| `INVALID_STATE` | `EFSM` | 소켓이 잘못된 상태에 있음 |
| `THREAD_VIOLATION` | `EMTHREAD` | 허용된 스레드 모델을 위반함 |

### 내부 실패

| Result | 내부 errno | 의미 |
|---|---|---|
| `OUT_OF_MEMORY` | `ENOMEM` | 메모리 할당 실패 |
| `SEQ_EXHAUSTED` | `EBUSY` | request sequence 번호 공간 소진 (request 전용) |
| `INTERNAL_ERROR` | `EPROTO` 및 그 외 내부 submit 실패 | 내부 send/request/reply submit 오류 |

## zlink_request_result_t

```c
typedef enum zlink_request_result_t
{
    /* Reply completed successfully. */
    ZLINK_REQUEST_OK = 0,

    /* Completion failure visible to the requester. */
    ZLINK_REQUEST_TIMED_OUT = 1,
    ZLINK_REQUEST_NOT_FOUND = 2,
    ZLINK_REQUEST_TERMINATED = 3,
    ZLINK_REQUEST_PROTOCOL_ERROR = 4
} zlink_request_result_t;
```

이 enum은 request completion에만 적용되고 submit에는 적용되지 않는다.

| Completion | callback errno | 의미 |
|---|---|---|
| `OK` | `0` | reply payload를 정상 수신함 |
| `TIMED_OUT` | `ETIMEDOUT` | `timeout_ms` 안에 reply가 도착하지 않음 |
| `NOT_FOUND` | `ENOENT` | 대상이 없어 error reply로 요청이 완료됨 |
| `TERMINATED` | `ETERM` | request 경로가 명시적인 종료 completion을 방출하기 전까지는 예약값 |
| `PROTOCOL_ERROR` | `EPROTO` | reply envelope 또는 error reply payload가 잘못됨 |

## Submit 매트릭스

`Y` = submit 경로가 이 정규화 결과를 낼 수 있음.
`—` = submit 경로가 이 정규화 결과를 내지 않음.

### Send 함수

| Result | `zlink_send` | `zlink_send_rid` | `zlink_publish` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | — | Y | — |
| `NOT_FOUND` | — | — | — |
| `TERMINATED` | Y | Y | Y |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | Y | Y | Y |
| `THREAD_VIOLATION` | Y | Y | Y |
| `OUT_OF_MEMORY` | — | — | — |
| `SEQ_EXHAUSTED` | — | — | — |
| `INTERNAL_ERROR` | — | — | — |

### Socket request 함수

| Result | `zlink_dealer_request` | `zlink_router_request` |
|---|---|---|
| `OK` | Y | Y |
| `BACKPRESSURED` | Y | Y |
| `NOT_CONNECTED` | — | — |
| `NOT_FOUND` | — | — |
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
| `NOT_CONNECTED` | — |
| `NOT_FOUND` | — |
| `TERMINATED` | Y |
| `INVALID_HANDLE` | Y |
| `INVALID_ARGUMENT` | Y |
| `NOT_SUPPORTED` | Y |
| `INVALID_STATE` | Y |
| `THREAD_VIOLATION` | Y |
| `OUT_OF_MEMORY` | — |
| `SEQ_EXHAUSTED` | — |
| `INTERNAL_ERROR` | Y |

### SPOT request 함수

| Result | `zlink_spot_request_spot` | `zlink_spot_request_router` | `zlink_router_request_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | — | — | — |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | — | — | Y |
| `THREAD_VIOLATION` | — | — | Y |
| `OUT_OF_MEMORY` | Y | Y | Y |
| `SEQ_EXHAUSTED` | — | — | Y |
| `INTERNAL_ERROR` | Y | Y | Y |

### SPOT send 함수

| Result | `zlink_spot_send_spot` | `zlink_spot_send_router` | `zlink_router_send_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | — | — | — |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | — | — | Y |
| `THREAD_VIOLATION` | — | — | Y |
| `OUT_OF_MEMORY` | — | — | — |
| `SEQ_EXHAUSTED` | — | — | — |
| `INTERNAL_ERROR` | Y | Y | Y |

### SPOT reply 함수

| Result | `zlink_spot_reply_spot` | `zlink_spot_reply_router` | `zlink_router_reply_spot` |
|---|---|---|---|
| `OK` | Y | Y | Y |
| `BACKPRESSURED` | Y | Y | Y |
| `NOT_CONNECTED` | Y | Y | Y |
| `NOT_FOUND` | Y | Y | Y |
| `TERMINATED` | — | — | — |
| `INVALID_HANDLE` | Y | Y | Y |
| `INVALID_ARGUMENT` | Y | Y | Y |
| `NOT_SUPPORTED` | Y | Y | Y |
| `INVALID_STATE` | — | — | Y |
| `THREAD_VIOLATION` | — | — | Y |
| `OUT_OF_MEMORY` | — | — | — |
| `SEQ_EXHAUSTED` | — | — | — |
| `INTERNAL_ERROR` | Y | Y | Y |

## Request Callback Completion

`zlink_reply_handler_fn`의 첫 번째 인자 `errno_`는 submit이 아니라 request
completion 결과다.

현재 구현에서 확인되는 completion `errno_` 값은 `0`, `ETIMEDOUT`,
`ENOENT`, `EPROTO`다. `ETERM`은 예약값이며, 현재 request 코드에는 이를
명시적으로 completion으로 방출하는 경로가 아직 없다.

## 적용 대상 함수

`zlink_submit_result_t`는 아래 15개 함수의 submit 결과에 적용된다.

| 분류 | 함수 |
|---|---|
| Send | `zlink_send`, `zlink_send_rid`, `zlink_publish` |
| Socket request | `zlink_dealer_request`, `zlink_router_request` |
| Socket reply | `zlink_router_reply` |
| SPOT request | `zlink_spot_request_spot`, `zlink_spot_request_router`, `zlink_router_request_spot` |
| SPOT send | `zlink_spot_send_spot`, `zlink_spot_send_router`, `zlink_router_send_spot` |
| SPOT reply | `zlink_spot_reply_spot`, `zlink_spot_reply_router`, `zlink_router_reply_spot` |

`zlink_request_result_t`는 `zlink_reply_handler_fn`으로 전달되는
completion 결과에 적용된다.
