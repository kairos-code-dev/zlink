[English](./dealer.md) | [한국어](./dealer.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](./README.ko.md)

# 소켓 -- DEALER

공정 큐잉(fair-queue, 여러 peer의 메시지를 순서대로 공평하게 수신) 수신과 라운드 로빈(round-robin, 연결된 peer를 순서대로 돌아가며 선택) 송신을 사용하는 비동기 요청 소켓.
DEALER는 request-reply 패턴에서 요청 측입니다.

## Dealer 옵션 (`zlink_dealer_option_t`)

`zlink_set_dealer_option()` 및 `zlink_get_dealer_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_DEALER_OPT_PROBE` | 연결 시 빈 메시지로 아이덴티티 설정 (`int`; 0 또는 1) |
| `ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS` | `zlink_dealer_request()` 기본 요청 타임아웃 (ms, `uint32_t`) |
| `ZLINK_DEALER_OPT_WEIGHT` | 연결된 peer에게 광고하는 로컬 peer 가중치 (`int`; `0..100`, 기본값 `100`) |

## 가중치 기반 outbound 선택

DEALER는 연결된 peer 중 광고된 가중치가 `0`인 대상을 후보 집합에서
자동으로 제외합니다. 양수 가중치를 가진 peer만 outbound 후보로 사용됩니다.

- 알려진 peer의 양수 가중치가 모두 같으면 기존 round-robin(순환 선택) 동작을 유지합니다.
- peer들의 양수 가중치가 서로 다르면 DEALER는 가중치 비율에 맞는
  weighted schedule을 사용합니다. 예를 들어 가중치 `100` peer는
  가중치 `50` peer보다 두 배 자주 선택됩니다.
- 일부 peer만 `0`이면 DEALER는 남은 양수 가중치 peer들 사이에서만 분배합니다.
- 알고 있는 peer가 모두 `0`이면 `zlink_send()`와
  `zlink_dealer_request()`는 `ZLINK_SUBMIT_NOT_ADMITTED`로 실패합니다.
  연결 자체는 유지되므로, 어떤 peer든 다시 양수 가중치로 돌아오면
  자동으로 후보가 됩니다.
- peer 가중치는 best-effort runtime 신호로 전파됩니다. 경합 상황에서는
  같은 거절이 `ZLINK_SUBMIT_NOT_CONNECTED`로 먼저 관찰될 수도 있습니다.
- peer 가중치 변화는 socket monitor의
  `ZLINK_EVENT_PEER_WEIGHT_CHANGED`로 관찰할 수 있으며, peer는
  `routing_id`로 식별합니다.

## 자동 HWM 기본값

DEALER는 context auto HWM(고수위 표시, High-Water Mark) 정책에서 `peer_queue` policy class로 분류됩니다.
활성 auto-HWM profile이 단위 예산과 메시지 크기 cap을 고르며, 기본 profile은
`balanced`입니다. 사용자가 `SNDHWM`, `RCVHWM`, `SNDBUF`, `RCVBUF`를 직접
설정하면 자동값보다 그 값이 우선합니다.

## 함수

### zlink_set_dealer_option

DEALER 소켓 전용 옵션을 설정합니다.

```c
zlink_config_result_t zlink_set_dealer_option (void *handle_,
                              zlink_dealer_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

DEALER 소켓 옵션을 설정합니다. 모든 소켓 타입에 공유되는 공통 옵션은
`zlink_set_option()`을 사용하세요.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_option`

---

### zlink_get_dealer_option

DEALER 소켓 전용 옵션을 읽습니다.

```c
zlink_config_result_t zlink_get_dealer_option (void *handle_,
                              zlink_dealer_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

DEALER 소켓 옵션 값을 `optval_`에 씁니다. `optvallen_`은 입력 시 버퍼
용량이고, 반환 시 실제로 쓴 바이트 수입니다. 모든 소켓 타입에 공유되는
공통 옵션은 `zlink_get_option()`을 사용하세요.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_get_option`

---

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

**에러:** `BACKPRESSURED` 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우.
`NOT_ADMITTED` 알고 있는 peer가 모두 가중치 `0`인 경우.
`TERMINATED` context가 종료된 경우. [errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를 참조한다.

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
`ZLINK_SUBMIT_NOT_CONNECTED`는 peer에 도달할 수 없는 경우,
`ZLINK_SUBMIT_NOT_ADMITTED`는 알고 있는 ROUTER의 가중치가 모두 `0`인 경우에
반환됩니다. [errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를
참조한다.

**참고:** `zlink_send`

---

### zlink_dealer_request

비동기 요청을 송신하고 응답 핸들러를 등록합니다.

```c
zlink_submit_result_t zlink_dealer_request (void *dealer_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_,
                          zlink_send_flags_t flags_,
                          uint32_t timeout_ms_);
```

DEALER 소켓에서 멀티파트 요청을 송신하고, 응답이 도착하거나 타임아웃이
만료되면 호출될 `handler_`를 등록합니다. 성공 시 모든 파트의 소유권이
라이브러리로 이전됩니다.

**반환값:** request submit이 수락되면 `ZLINK_SUBMIT_OK`를 반환합니다.
실패 시에는 `zlink_submit_result_t` 값을 반환합니다. reply completion은
별도로 `zlink_reply_handler_fn`을 통해 전달됩니다.

**참고:** `zlink_send`, `zlink_reply_handler_fn`

---

### zlink_dealer_reply_part

`zlink_dealer_recv_part()`로 받은 DEALER request에 대한 reply 파트 하나를
보냅니다.

```c
zlink_submit_result_t zlink_dealer_reply_part (void *dealer_,
                              uint64_t request_seq_,
                              zlink_msg_t *part_,
                              zlink_part_flag_t part_flag_);
```

`request_seq_`는 `zlink_dealer_recv_part()`가 반환한 0이 아닌 sequence여야
합니다. 성공하면 `part_`의 소유권은 라이브러리로 이전됩니다. 실패하면
호출자에게 소유권이 남습니다. 다만 잘못된 인자처럼 다른 `*_part` helper와
같은 규칙을 따르는 경로에서는 유효하지 않은 send part를 소비할 수 있습니다.

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`, 실패 시 실패 이유를 나타내는 `zlink_submit_result_t` 값. [errno-map.ko.md](../errno-map.ko.md) 참조.

**참고:** `zlink_dealer_recv_part`, `zlink_dealer_request_part`

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
`zlink_multipart_close()`를 호출하고 배열을 해제해야 합니다. DEALER는
`zlink_dealer_request()`의 completion callback을 제외하면 data-plane
수신이 recv-only입니다. poller의 `ZLINK_POLLIN`과 함께 사용해 서버 루프에서
readable을 관찰한 뒤 이 함수로 데이터를 가져오는 방식을 기본 경로로 합니다.
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

지원 대상은 raw `PAIR`, `PUB`, `XPUB`, `DEALER`, `ROUTER`, `STREAM`,
`spot`, `spot_node`입니다. send-ready는 수신 모드와 독립적입니다.
이 콜백과 `ZLINK_POLLOUT`은 같은 send-recovery readiness 축을 가리킵니다.
readiness 신호는 송신을 다시 시도할 가치가 있다는 뜻이며, 재시도가 반드시
성공한다는 보장은 아닙니다. 지원하지 않는 subject는 `ENOTSUP`를 반환합니다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`, 실패 시 `zlink_handler_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_send`
