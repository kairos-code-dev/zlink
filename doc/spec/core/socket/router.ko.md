[English](./router.md) | [한국어](./router.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](./README.ko.md)

# 소켓 -- ROUTER

Routing id 기반 주소 지정, 아이덴티티 인식 수신, 지정 송신. ROUTER는
request-reply 패턴에서 응답 측입니다.

## Router 옵션 (`zlink_router_option_t`)

`zlink_set_router_option()` / `zlink_get_router_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_ROUTER_OPT_MANDATORY` | 라우팅 불가 시 `EHOSTUNREACH` 반환 (`int`; 0 또는 1, 기본값 `1`) |
| `ZLINK_ROUTER_OPT_PROBE` | 연결 시 빈 메시지로 아이덴티티 설정 (`int`; 0 또는 1) |
| `ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID` | 발신 연결의 routing id 설정 (`binary`) |
| `ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS` | `zlink_router_request()` 기본 요청 타임아웃 (ms, `uint32_t`) |
| `ZLINK_ROUTER_OPT_WEIGHT` | 연결된 peer에게 광고하는 로컬 peer 가중치 (`int`; `0..100`, 기본값 `100`) |

`ZLINK_ROUTER_OPT_MANDATORY`의 기본값은 `1`입니다.
중복 peer identity 처리는 공통 socket option인
`ZLINK_OPT_RID_DUPLICATE_POLICY`로 정합니다. 기본값은
`ZLINK_RID_DUPLICATE_REJECT`입니다.

- `MANDATORY=1`이 기본이므로 `zlink_send_rid()`는 연결되지 않은 peer를
  대상으로 지정하면 조용히 넘어가지 않고 `ZLINK_SUBMIT_NOT_CONNECTED`를
  반환합니다. 같은 이유로 ROUTER의 writable/`ZLINK_POLLOUT` 관찰값은
  실제로 보낼 수 있는 peer가 있을 때만 readiness로 surface됩니다.
- 기본 duplicate policy에서는 동일한 peer identity로 새 연결이 들어오면
  기존 pipe를 유지하고 새 중복 pipe는 등록하지 않습니다.

호출자가 다른 동작(peer 미도달 시 조용한 drop, 중복 identity 도착 시 새
pipe 인수)을 원하면 해당 옵션을 명시적으로 설정해야 합니다.

## Peer 가중치

ROUTER는 peer가 자신을 새 작업 대상으로 얼마나 자주 선택해야 하는지를
알리는 peer 가중치를 가집니다. 기본값은 `100`입니다. 점검이나 롤링
재시작 직전에는 가중치를 `0`으로 바꿔 peer가 새 작업을 더 이상 이
ROUTER로 보내지 않게 할 수 있습니다.

```c
zlink_config_result_t zlink_set_router_option (
  void *handle_,
  zlink_router_option_t option_,
  const void *optval_,
  size_t optvallen_);

zlink_config_result_t zlink_get_router_option (
  void *handle_,
  zlink_router_option_t option_,
  void *optval_,
  size_t *optvallen_);
```

가중치 규약:

| 값 | 의미 |
|---|---|
| `100` | 기본 가중치. 양수 가중치가 모두 같으면 peer는 round-robin(순환 선택) 동작을 유지합니다. |
| `1..99` | 양수이지만 선호도가 더 낮습니다. 후보에는 남아 있고 비율만 낮아집니다. |
| `0` | 새 outbound 선택 대상에서 제외됩니다. 이미 들어온 작업은 계속 처리합니다. |

특징:

- `0..100` 전환은 runtime에 양방향으로 허용됩니다.
- 로컬 ROUTER 자체의 recv/send/reply/handler-dispatch 동작은 peer 가중치와
  무관하게 평소처럼 진행됩니다. 즉 `0`은 "내가 멈춘다"가 아니라
  "남이 나를 새 작업 대상으로 고르지 말라"는 신호입니다.
- 가중치 변화는 연결된 peer에게 best-effort runtime 신호로 전파됩니다.
  peer는 자신의 가중치 cache를 갱신하며, 재연결 시 다시 동기화됩니다.
- 가중치 전환은 socket monitor의
  `ZLINK_EVENT_PEER_WEIGHT_CHANGED` 이벤트로 관찰할 수 있습니다.
  `routing_id`로 peer를 식별하고, `value`에는 새 `0..100` 가중치가 들어갑니다.

## ROUTER에서 시작하는 directed send와 peer 가중치

ROUTER가 다른 ROUTER에 directed send/request를 보낼 때는 target RID의
cache된 가중치를 먼저 확인합니다.

- target RID의 가중치가 양수면 평소처럼 submit합니다.
- target RID의 가중치가 `0`이면 `zlink_send_rid()`와
  `zlink_router_request()`는 모두 `ZLINK_SUBMIT_NOT_ADMITTED`로 즉시
  실패합니다.
- 가중치 cache 전파는 best-effort이므로, 경합 상황에서는 같은 거절이
  `ZLINK_SUBMIT_NOT_CONNECTED`로 먼저 관찰될 수도 있습니다.

reply 경로에는 이 판정을 적용하지 않습니다.
`zlink_router_reply()`는 이미 들어온 request에 대한 응답이라 peer
가중치와 관계없이 보낼 수 있습니다.

## 자동 HWM 기본값

ROUTER는 context auto HWM(고수위 표시, High-Water Mark) 정책에서 `routed` policy class로 분류됩니다.
활성 auto-HWM profile이 단위 예산과 메시지 크기 cap을 고르며, 기본 profile은
`balanced`입니다. 사용자가 `SNDHWM`, `RCVHWM`, `SNDBUF`, `RCVBUF`를 직접
설정하면 자동값보다 그 값이 우선합니다.

## 함수

### zlink_set_router_option

ROUTER 소켓 전용 옵션을 설정합니다.

```c
zlink_config_result_t zlink_set_router_option (void *handle_,
                              zlink_router_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

ROUTER 소켓 옵션을 설정합니다. 모든 소켓 타입에 공유되는 공통 옵션은
`zlink_set_option()`을 사용하세요.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_get_router_option`, `zlink_set_option`

---

### zlink_get_router_option

ROUTER 소켓 전용 옵션을 조회합니다.

```c
zlink_config_result_t zlink_get_router_option (void *handle_,
                              zlink_router_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

ROUTER 소켓 옵션의 현재 값을 가져옵니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_router_option`

---

### zlink_send_rid

routing id로 특정 피어에게 멀티파트 메시지를 송신합니다.

```c
zlink_submit_result_t zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_);
```

`target_rid_`로 식별되는 피어에게 멀티파트 메시지를 송신합니다. 성공 시
모든 파트의 소유권이 라이브러리로 이전됩니다. 실패 시 소유권은 호출자에게
유지됩니다.

적용 대상: ROUTER (directed reply), STREAM (피어 지정 send).

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`. 실패 시에는
`zlink_submit_result_t` 값을 반환합니다. 상세 내부 errno는 진단을 위해
`zlink_errno()`로 유지됩니다.

**에러:** `s_`가 NULL이면 `INVALID_HANDLE`. 작업이 블로킹되고
`ZLINK_DONTWAIT`가 설정된 경우 `BACKPRESSURED`. 대상 피어가 연결되지 않은 경우
`NOT_CONNECTED` (`ROUTER_MANDATORY=1`이 기본이므로 옵션을 명시적으로 끄지
않았다면 이 결과를 더 자주 보게 됩니다). 대상 RID의 가중치가 `0`이면
`NOT_ADMITTED`. Context가 종료된 경우 `TERMINATED`.
[errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를 참조.

**참고:** `zlink_send_rid`, `zlink_recv`

---

### 논블로킹 routed send

routed send API를 이용한 논블로킹 지정 송신입니다.

```c
zlink_submit_result_t zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_);
```

논블로킹 routed 전송은 `zlink_send_rid(..., ZLINK_DONTWAIT)` 로 처리합니다.
`ZLINK_SUBMIT_BACKPRESSURED`는 작업이 블로킹되는 경우,
`ZLINK_SUBMIT_NOT_CONNECTED`는 peer에 도달할 수 없는 경우,
`ZLINK_SUBMIT_NOT_ADMITTED`는 대상 RID의 가중치가 `0`인 경우에
반환됩니다. [errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를 참조.

성공하면 모든 파트의 소유권이 라이브러리로 이전됩니다. 실패하면
소유권은 호출자에게 유지됩니다.

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`, 실패 시 실패 이유를 나타내는 `zlink_submit_result_t` 값. [errno-map.ko.md](../errno-map.ko.md) 참조.

**참고:** `zlink_send_rid`

---

### zlink_router_request

특정 피어에게 비동기 요청을 송신하고 응답 핸들러를 등록합니다.

```c
zlink_submit_result_t zlink_router_request (void *router_,
                          const zlink_routing_id_t *peer_rid_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_,
                          zlink_send_flags_t flags_,
                          uint32_t timeout_ms_);
```

ROUTER 소켓에서 `peer_rid_`로 식별되는 피어에게 멀티파트 요청을 송신하고,
응답이 도착하거나 타임아웃이 만료되면 호출될 `handler_`를 등록합니다.
성공 시 모든 파트의 소유권이 라이브러리로 이전됩니다.

**반환값:** request submit이 수락되면 `ZLINK_SUBMIT_OK`를 반환합니다.
실패 시에는 `zlink_submit_result_t` 값을 반환합니다. reply completion은
별도로 `zlink_reply_handler_fn`을 통해 전달됩니다.

**참고:** `zlink_router_reply`, `zlink_reply_handler_fn`

---

### zlink_router_reply

이전에 수신한 요청에 대한 응답을 송신합니다.

```c
zlink_submit_result_t zlink_router_reply (void *router_,
                        const zlink_routing_id_t *peer_rid_,
                        uint64_t request_seq_,
                        zlink_msg_t *parts_,
                        size_t part_count_);
```

`peer_rid_`로 식별되는 피어에게 시퀀스 번호 `request_seq_`의 요청에 대한
멀티파트 응답을 송신합니다. 성공 시 모든 파트의 소유권이 라이브러리로
이전됩니다.

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`. 실패 시에는
`zlink_submit_result_t` 값을 반환합니다. 상세 내부 errno는 진단을 위해
`zlink_errno()`로 유지됩니다.

**참고:** `zlink_router_request`, `zlink_router_recv`

---

### zlink_router_recv

ROUTER 소켓에서 routed 트래픽을 recv 모드로 수신합니다.

```c
zlink_recv_result_t zlink_router_recv (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);
```

ROUTER 소켓의 다음 routed delivery를 받습니다. 이 함수가 ROUTER의 유일한
direct recv 표면입니다. 일반 ROUTER 트래픽과 spot에서 시작한 routed
트래픽을 모두 이 함수 하나로 받습니다. ROUTER의 inbound 수신은 recv 전용이며,
poller의 `ZLINK_POLLIN`과 함께 서버 루프에서 사용하는 것이 기본 경로입니다.
`zlink_router_request()`의 reply completion callback은 여기서 전달되지
않고, 별도 `zlink_reply_handler_fn` 축으로 전달됩니다.

성공 시 `*source_node_rid_out_`는 source node routing id를 가리킵니다.
일반 ROUTER 트래픽이면 `*source_spot_rid_out_`는 `NULL`입니다.
spot에서 온 routed 트래픽이면 `*source_spot_rid_out_`가 source spot
routing id를 가리킵니다.

`*request_seq_out_ == 0`이면 fire-and-forget routed message입니다.
`*request_seq_out_ != 0`이면 request입니다. 일반 ROUTER request는
`zlink_router_reply()`로 응답하고, spot에서 온 request는
`zlink_router_reply_spot()`으로 응답합니다.

반환되는 payload view의 소유권 규칙은 일반 recv와 같습니다. 배열 view는
라이브러리가 소유하고, 호출자는 각 part를 닫아야 합니다.

**반환값:** 성공 시 `ZLINK_RECV_OK`. 실패 시에는
`zlink_recv_result_t` 값을 반환합니다. 상세 내부 errno는 진단을 위해
`zlink_errno()`로 유지됩니다.

**참고:** `zlink_router_reply`, `zlink_router_reply_spot`,
`zlink_router_request`

---

### ROUTER에서 SPOT으로 보내기

`zlink_router_send_spot_part()`와 `zlink_router_request_spot_part()`는
router channel의 `ROUTER`에서 target `Spot`으로 routed 메시지를 보낸다.
target node가 해당 router channel peer로 연결되어 있어야 하며, 이 연결은
`zlink_spot_node_connect_router_channel_peer()` 또는
`zlink_spot_node_attach_router_channel_discovery()`로 만든다.

호출자는 target node routing id와 target spot routing id를 모두 제공해야 한다.
router channel peer가 없거나 아직 route가 준비되지 않은 target으로 보내면 일반
ROUTER not-connected 계열 오류와 같은 방식으로 실패하거나 전송되지 않을 수 있다.
따라서 상위 framework는 channel id를 resolver metadata로만 보관하지 말고 실제
router-capable channel의 `ROUTER` socket을 transport로 선택해야 한다.

---

### zlink_recv

ROUTER가 아닌 소켓에서 멀티파트 메시지를 수신합니다.

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
`zlink_multipart_close()`를 호출하고 배열을 해제해야 합니다. 소켓이 recv
모드여야 합니다 (핸들러 미부착). ROUTER 소켓은 이 표면에서 제외됩니다.
`s_`가 ROUTER 소켓이면 이 호출은 `ZLINK_RECV_NOT_SUPPORTED`로 실패하고,
대신 `zlink_router_recv()`를 사용해야 합니다.

**반환값:** 성공 시 `ZLINK_RECV_OK`. 실패 시에는
`zlink_recv_result_t` 값을 반환합니다. 상세 내부 errno는 진단을 위해
`zlink_errno()`로 유지됩니다.

**참고:** `zlink_send`, `zlink_router_recv`, `zlink_multipart_close`

---

### zlink_set_routing_id

소켓의 라우팅 아이덴티티를 설정합니다.

```c
zlink_config_result_t zlink_set_routing_id (void *handle_,
                           const void *data_,
                           size_t size_);
```

ROUTER 주소 지정을 위한 소켓 아이덴티티를 설정합니다. 최대 255바이트.
바인딩 또는 연결하기 전에 설정해야 합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_get_routing_id`

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
성공한다는 보장은 아닙니다. `MANDATORY=1`이 기본이면 ROUTER의 readiness는
실제로 쓸 수 있는 peer가 있을 때만 surface됩니다. 지원하지 않는 subject는
`ENOTSUP`를 반환합니다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`, 실패 시 `zlink_handler_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_send`
