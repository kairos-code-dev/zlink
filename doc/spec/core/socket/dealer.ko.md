[English](dealer.md) | [한국어](dealer.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md)

# 소켓 -- DEALER

공정 큐잉 수신과 라운드 로빈 송신을 사용하는 비동기 요청 소켓.
DEALER는 request-reply 패턴에서 요청 측입니다.

## Dealer 옵션 (`zlink_dealer_option_t`)

`zlink_set_dealer_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_DEALER_OPT_PROBE` | 연결 시 빈 메시지로 아이덴티티 설정 (`int`; 0 또는 1) |
| `ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS` | `zlink_dealer_request()` 기본 요청 타임아웃 (ms, `uint32_t`) |

## 함수

### zlink_set_dealer_option

DEALER 소켓 전용 옵션을 설정합니다.

```c
int zlink_set_dealer_option (void *handle_,
                              zlink_dealer_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

DEALER 소켓 옵션을 설정합니다. 모든 소켓 타입에 공유되는 공통 옵션은
`zlink_set_option()`을 사용하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_option`

---

### zlink_send

소켓에서 멀티파트 메시지를 송신합니다.

```c
int zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_);
```

소켓 `s_`에서 `parts_` 배열의 `part_count_`개 파트로 구성된 멀티파트 메시지를
송신합니다. 성공 시 배열 내 모든 파트의 소유권이 라이브러리로 이전되며, 호출자는
이후 접근할 수 없습니다. 실패 시 소유권은 호출자에게 유지됩니다. `flags_`
매개변수는 0 또는 `ZLINK_DONTWAIT`일 수 있습니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우 `EAGAIN`. Context가
종료된 경우 `ETERM`.

**참고:** `zlink_send`, `zlink_recv`

---

### 논블로킹 send

논블로킹 송신으로 결과를 출력 매개변수를 통해 반환합니다.

```c
int zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_);
```

논블로킹 전송은 `zlink_send(..., ZLINK_DONTWAIT)` 로 처리합니다.
바인딩은 errno를 `zlink_send_result_t`로 바꿔서 노출할 수 있습니다.

**참고:** `zlink_send`

---

### zlink_dealer_request

비동기 요청을 송신하고 응답 핸들러를 등록합니다.

```c
int zlink_dealer_request (void *dealer_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          uint32_t timeout_ms_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_);
```

DEALER 소켓에서 멀티파트 요청을 송신하고, 응답이 도착하거나 타임아웃이
만료되면 호출될 `handler_`를 등록합니다. 성공 시 모든 파트의 소유권이
라이브러리로 이전됩니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_send`, `zlink_reply_handler_fn`

---

### zlink_recv

소켓에서 멀티파트 메시지를 수신합니다.

```c
int zlink_recv (void *s_,
                zlink_routing_id_t *source_rid_out_,
                zlink_msg_t **parts_out_,
                size_t *part_count_out_,
                zlink_send_flags_t flags_);
```

소켓 `s_`에서 완전한 멀티파트 메시지를 수신합니다. 성공 시 `*parts_out_`는
라이브러리가 할당한 `*part_count_out_`개 메시지 파트 배열을 가리키며,
`*source_rid_out_`는 송신자의 routing id로 설정됩니다 (해당하는 경우). 파트
배열과 각 파트의 소유권이 호출자에게 이전되며, 호출자는 모든 파트를 close하거나
`zlink_multipart_close()`를 호출하고 배열을 해제해야 합니다. 소켓이 recv
모드여야 합니다 (핸들러 미부착). `zlink_recv_handler()`로 수신 핸들러가
부착된 경우 `errno=EBUSY`로 실패합니다. 메시지가 없을 때 즉시 반환하려면
`ZLINK_DONTWAIT`를 전달하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우, 또는
`ZLINK_OPT_RCVTIMEO`가 만료된 경우 `EAGAIN`. 수신 핸들러가 부착된 경우 `EBUSY`.
Context가 종료된 경우 `ETERM`.

**참고:** `zlink_send`, `zlink_recv_handler`, `zlink_multipart_close`

---

### zlink_recv_handler

소켓에 메시지 수신 핸들러를 부착합니다.

```c
int zlink_recv_handler (void *s_,
                        zlink_socket_msg_handler_fn handler_,
                        void *userdata_);
```

멀티파트 수신 subject에 메시지 수신 핸들러를 부착합니다. 지원 대상은 raw
`PAIR`, `DEALER`, `ROUTER`, `STREAM`입니다. attach 이후 같은
subject의 direct recv와 data-plane poller `ZLINK_POLLIN`은 `errno=EBUSY`로
실패합니다. 동일 subject에 대한 두 번째 attach도 `errno=EBUSY`입니다.
지원하지 않는 subject는 `ENOTSUP`를 반환합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 핸들러가 NULL이면 `EINVAL`. 소켓 타입이 메시지 핸들러를
허용하지 않으면 `ENOTSUP`. 핸들러가 이미 부착된 경우 `EBUSY`.

**참고:** `zlink_subscribe_handler`, `zlink_socket`, `zlink_close`

---

### zlink_send_ready_handler

send-ready 콜백을 설정하거나 교체합니다.

```c
int zlink_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_, void *userdata_);
```

핸들러는 교체 전용입니다. NULL 전달은 유효하지 않습니다. 교체 성공 시 다음 쓰기
가능 전환부터 반영됩니다. 동일 핸들의 send-ready 콜백 내에서 재진입 호출하면
`errno=EDEADLK`로 실패합니다.

지원 대상은 raw `PAIR`, `PUB`, `XPUB`, `DEALER`, `ROUTER`, `STREAM`,
`spot`, `spot_node`입니다. send-ready는 receive callback 모드와
독립적입니다. attach 이후 같은 subject의 data-plane poller
`ZLINK_POLLOUT`은 `errno=EBUSY`로 실패합니다. 지원하지 않는 subject는
`ENOTSUP`를 반환합니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_send`
