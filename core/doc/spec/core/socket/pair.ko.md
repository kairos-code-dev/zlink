[English](pair.md) | [한국어](pair.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md)

# 소켓 -- PAIR

1:1 양방향 소켓. 양쪽 모두 송신과 수신이 가능합니다. 타입 전용 옵션은
없습니다.

## 적용 함수

### zlink_send

소켓에서 멀티파트 메시지를 송신합니다.

```c
zlink_submit_result_t zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_);
```

소켓 `s_`에서 `parts_` 배열의 `part_count_`개 파트로 구성된 멀티파트 메시지를
송신합니다. 성공 시 배열 내 모든 파트의 소유권이 라이브러리로 이전되며, 호출자는
이후 접근할 수 없습니다. 실패 시 소유권은 호출자에게 유지됩니다. `flags_`
매개변수는 0 또는 `ZLINK_DONTWAIT`일 수 있습니다.

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`, 실패 시 실패 이유를 나타내는 `zlink_submit_result_t` 값. [errno-map.ko.md](../errno-map.ko.md) 참조.

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우 `BACKPRESSURED`.
Context가 종료된 경우 `TERMINATED`. [errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를 참조.

**참고:** `zlink_send`, `zlink_recv`

---

### 논블로킹 send

같은 `zlink_send` 진입점을 `ZLINK_DONTWAIT`와 함께 사용합니다.

```c
zlink_submit_result_t zlink_send (void *s_,
                zlink_msg_t *parts_,
                size_t part_count_,
                zlink_send_flags_t flags_);
```

논블로킹 전송은 `zlink_send(..., ZLINK_DONTWAIT)` 로 처리합니다.
`ZLINK_SUBMIT_BACKPRESSURED`는 작업이 블로킹되는 경우,
`ZLINK_SUBMIT_NOT_CONNECTED`는 peer에 도달할 수 없는 경우에 반환됩니다.
[errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를 참조.

성공하면 모든 파트의 소유권이 라이브러리로 넘어갑니다. 실패하면
소유권은 호출자에게 남습니다.

**참고:** `zlink_send`

---

### zlink_recv

소켓에서 멀티파트 메시지를 수신합니다.

```c
zlink_recv_result_t zlink_recv (void *s_,
                 zlink_routing_id_t *source_rid_out_,
                 zlink_msg_t **parts_out_,
                 size_t *part_count_out_,
                 zlink_recv_flags_t flags_);
```

소켓 `s_`에서 완전한 멀티파트 메시지를 수신합니다. 성공 시 `*parts_out_`는
라이브러리가 할당한 `*part_count_out_`개 메시지 파트 배열을 가리키며,
`*source_rid_out_`는 송신자의 routing id로 설정됩니다 (해당하는 경우). 파트
배열과 각 파트의 소유권이 호출자에게 이전되며, 호출자는 모든 파트를 닫거나
`zlink_multipart_close()`를 호출하고 배열을 해제해야 합니다. PAIR는 수신 콜백
표면을 제공하지 않으며, 수신은 poller로 `ZLINK_POLLIN`을 확인한 뒤 이 함수로
가져오는 방식만 사용합니다 — 소켓 자체는 양방향이며 `zlink_send`를 지원합니다.
메시지가 없을 때 즉시 반환하려면 `ZLINK_DONTWAIT`를 전달하세요.

**반환값:** 성공 시 `ZLINK_RECV_OK`, 실패 시 `zlink_recv_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우, 또는
`ZLINK_OPT_RCVTIMEO`가 만료된 경우 `EAGAIN`. Context가 종료된 경우 `ETERM`.

**참고:** `zlink_send`, `zlink_multipart_close`

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
