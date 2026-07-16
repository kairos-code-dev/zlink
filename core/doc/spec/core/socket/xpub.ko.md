[English](xpub.md) | [한국어](xpub.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md)

# 소켓 -- XPUB

구독 전달과 수동 제어를 지원하는 확장 발행자. XPUB는 구독자로부터 구독
이벤트를 수신하고 수동 구독 관리를 지원합니다.

## Pub 옵션 (`zlink_pub_option_t`)

`zlink_set_pub_option()` / `zlink_get_pub_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_PUB_OPT_VERBOSE` | 모든 구독 메시지를 업스트림 전달 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_VERBOSER` | 구독/해제 메시지를 업스트림 전달 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_MANUAL` | XPUB 수동 구독 관리 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_MANUAL_LAST_VALUE` | 수동 모드 최신 값 캐싱 (`int`; 0 또는 1) |
| `ZLINK_PUB_OPT_NODROP` | HWM 시 drop 대신 `EAGAIN` 반환 (`int`; 0 또는 1, 기본값 `1`) |
| `ZLINK_PUB_OPT_WELCOME_MSG` | 새 subscriber 연결 시 전송 메시지 (`binary`) |
| `ZLINK_PUB_OPT_TOPICS_COUNT` | 구독된 토픽 수 (`int`, 읽기 전용) |
| `ZLINK_PUB_OPT_APPROVE_SUBSCRIBE` | manual 모드 구독 승인 (`binary`) |
| `ZLINK_PUB_OPT_REJECT_SUBSCRIBE` | manual 모드 구독 거부 (`binary`) |

`ZLINK_PUB_OPT_NODROP`의 기본값은 `1`입니다. HWM(고수위 표시, High-Water Mark)이 찼을 때 조용히 drop하는
대신 `zlink_publish()`가 `ZLINK_SUBMIT_BACKPRESSURED`를 반환합니다. 호출자가
조용한 drop 동작을 원하면 명시적으로 `0`으로 설정해야 합니다.

## 함수

### zlink_set_pub_option

PUB/XPUB 소켓 전용 옵션을 설정합니다.

```c
zlink_config_result_t zlink_set_pub_option (void *handle_,
                           zlink_pub_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
```

PUB/XPUB 소켓 옵션을 설정합니다. 모든 소켓 타입에 공유되는 공통 옵션은
`zlink_set_option()`을 사용합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_get_pub_option`, `zlink_set_option`

---

### zlink_get_pub_option

pub 전용 옵션을 조회합니다.

```c
zlink_config_result_t zlink_get_pub_option (void *handle_,
                           zlink_pub_option_t option_,
                           void *optval_,
                           size_t *optvallen_);
```

PUB/XPUB 소켓 옵션의 현재 값을 가져옵니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_pub_option`

---

### zlink_publish

멀티파트 메시지를 발행합니다.

```c
zlink_submit_result_t zlink_publish (void *subject_,
                   const char *topic_id_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_);
```

지정된 subject에서 멀티파트 메시지를 발행합니다. 성공 시 모든 파트의
소유권이 라이브러리로 이전됩니다.

- raw `PUB` / `XPUB`: `topic_id_`는 NULL(첫 메시지 프레임이 wire prefix 규칙에
  따라 토픽을 운반)이거나 non-NULL(메시지 앞에 토픽 프레임을 덧붙임)일 수 있습니다.

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`. 실패 시에는
`zlink_submit_result_t` 값을 반환합니다. 상세 내부 errno는 진단을 위해
`zlink_errno()`로 유지됩니다.

**에러:** `subject_`가 NULL이면 `EFAULT`. subject가 raw PUB/XPUB 소켓이
아니면 `ENOTSUP`.

**참고:** `zlink_publish`, `zlink_set_subscription`, `zlink_subscribe`

---

### 논블로킹 publish

publish API를 이용한 논블로킹 발행입니다.

```c
zlink_submit_result_t zlink_publish (void *subject_,
                   const char *topic_id_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_);
```

논블로킹 발행은 `zlink_publish(..., ZLINK_DONTWAIT)` 로 처리합니다.
`ZLINK_SUBMIT_BACKPRESSURED`는 작업이 블로킹되는 경우,
`ZLINK_SUBMIT_NOT_CONNECTED`는 peer에 도달할 수 없는 경우에 반환됩니다.
[errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를 참조.

성공하면 모든 파트의 소유권이 라이브러리로 넘어갑니다. 실패하면
소유권은 호출자에게 남습니다.

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`, 실패 시 실패 이유를 나타내는 `zlink_submit_result_t` 값. [errno-map.ko.md](../errno-map.ko.md) 참조.

**참고:** `zlink_publish`

---

### zlink_xpub_recv_part

XPUB 소켓에서 구독 이벤트를 수신합니다.

```c
zlink_recv_result_t zlink_xpub_recv_part (void *xpub_,
                               const zlink_routing_id_t **source_rid_out_,
                               int *subscribed_out_,
                               char *topic_id_buf_,
                               size_t topic_id_capacity_,
                               size_t *topic_id_len_out_,
                               zlink_recv_flags_t flags_);
```

recv 모드에서 다음 구독 이벤트를 수신합니다. 성공 시
`*source_rid_out_`는 구독 피어의 라이브러리 소유 routing ID 포인터로
설정되고(이 소켓에 대한 다음 호출 전까지 유효), `*subscribed_out_`는
subscribe이면 1, unsubscribe이면 0입니다. `topic_id_buf_` /
`*topic_id_len_out_`에 토픽 바이트가 기록됩니다(binary-safe).
호출자는 `topic_id_capacity_`로 버퍼 크기를 전달하며,
토픽이 용량을 초과하면 `errno = EMSGSIZE`로 실패합니다.

적용 대상: raw XPUB만.

**반환값:** 성공 시 `ZLINK_RECV_OK`, 실패 시 `zlink_recv_result_t` 값.
`EAGAIN`/`ETERM` 외의 상세 errno(예: 토픽 용량 초과 `EMSGSIZE`, XPUB가 아닌
subject `EINVAL`)는 `ZLINK_RECV_INTERNAL_ERROR`로 표면화되며, `zlink_errno()`는
진단용 내부 errno를 그대로 유지합니다.

**에러:** `xpub_`가 NULL이면 `EFAULT`. `ZLINK_DONTWAIT`가 설정되고
이벤트가 없으면 `EAGAIN`. 토픽이 `topic_id_capacity_`를 초과하면
`EMSGSIZE`. subject가 XPUB가 아니면 `EINVAL`.

**참고:** `zlink_publish`

---

### zlink_send_ready_handler

send-ready 콜백을 설정하거나 교체합니다.

```c
zlink_handler_result_t zlink_send_ready_handler (
  void *s_, zlink_send_ready_handler_fn handler_, void *userdata_);
```

핸들러는 교체 전용입니다. NULL 전달은 유효하지 않습니다. 교체 성공 시 다음 쓰기
가능 전환부터 반영됩니다. 동일 핸들의 send-ready 콜백 내에서 재진입 호출하면
`errno=EDEADLK`로 실패합니다.

지원 대상은 raw `PAIR`, `PUB`, `XPUB`, `DEALER`, `ROUTER`, `STREAM`입니다.
send-ready는 수신 모드와 독립적입니다.
이 콜백과 `ZLINK_POLLOUT`은 같은 send-recovery readiness 축을 가리킵니다.
readiness 신호는 송신을 다시 시도할 가치가 있다는 뜻이며, 재시도가 반드시
성공한다는 보장은 아닙니다. 지원하지 않는 subject는 `ENOTSUP`를 반환합니다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`, 실패 시 `zlink_handler_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_send`
