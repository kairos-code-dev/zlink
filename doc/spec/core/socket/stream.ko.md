[English](stream.md) | [한국어](stream.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md)

# 소켓 -- STREAM

피어 routing id 주소 지정을 사용하는 raw TCP/WS 통신. STREAM은 bind 전용이며
`zlink_connect`를 지원하지 않습니다.

## 수신 모델

STREAM은 다른 소켓 타입과 달리 세 가지 수신 모델을 지원합니다. 하나의
handle에서 이 세 모델 중 정확히 하나만 활성화할 수 있습니다.

- raw recv: `zlink_recv()`로 transport 조각을 직접 가져옵니다.
- raw callback: `zlink_recv_handler()`로 raw 조각을 콜백으로 받습니다.
- packet callback: `zlink_stream_packet_handler()`로 고정 framing 규약에
  따라 조립된 packet을 header/body로 받습니다.

한 handle에서 두 번째 모드로 전환하려 하면 `EBUSY`로 실패합니다. 즉 모드
전환은 한 방향으로만 일어나며, 세 모델은 상호 배타입니다.

## 자동 HWM 기본값

STREAM은 context auto HWM(고수위 표시, High-Water Mark) 정책에서 `stream` policy class로 분류됩니다. 기본
context에서는 auto-HWM이 켜져 있으며, 활성 profile은 `balanced`입니다.
애플리케이션이 `SNDBUF` / `RCVBUF`를 직접 주지 않으면 STREAM은 호환 기본값
`262144`를 사용합니다.

## Stream 옵션 (`zlink_stream_option_t`)

`zlink_set_stream_option()` / `zlink_get_stream_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_STREAM_OPT_NOTIFY` | STREAM 연결/해제 알림 (`int`; 0 또는 1) |

## 함수

### zlink_set_stream_option

STREAM 소켓 전용 옵션을 설정합니다.

```c
zlink_config_result_t zlink_set_stream_option (void *handle_,
                              zlink_stream_option_t option_,
                              const void *optval_,
                              size_t optvallen_);
```

STREAM 소켓 옵션을 설정합니다. 모든 소켓 타입에 공유되는 공통 옵션은
`zlink_set_option()`을 사용하세요.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_get_stream_option`, `zlink_set_option`

---

### zlink_get_stream_option

STREAM 소켓 전용 옵션을 조회합니다.

```c
zlink_config_result_t zlink_get_stream_option (void *handle_,
                              zlink_stream_option_t option_,
                              void *optval_,
                              size_t *optvallen_);
```

STREAM 소켓 옵션의 현재 값을 가져옵니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_stream_option`

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
(`ROUTER_MANDATORY` 활성 시) `NOT_CONNECTED`. Context가 종료된 경우 `TERMINATED`.
[errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를 참조.

**참고:** `zlink_send_rid`, `zlink_send`, `zlink_recv`

---

### 논블로킹 routed send

routed send API를 이용한 논블로킹 전송입니다.

```c
zlink_submit_result_t zlink_send_rid (void *s_,
                    const zlink_routing_id_t *target_rid_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    zlink_send_flags_t flags_);
```

논블로킹 routed 전송은 `zlink_send_rid(..., ZLINK_DONTWAIT)` 로 처리합니다.
`ZLINK_SUBMIT_BACKPRESSURED`는 작업이 블로킹되는 경우,
`ZLINK_SUBMIT_NOT_CONNECTED`는 peer에 도달할 수 없는 경우에 반환됩니다.
[errno-map.ko.md](../errno-map.ko.md)에서 전체 결과 매트릭스를 참조.
성공하면 모든 파트의 소유권이 라이브러리로 넘어갑니다. 실패하면
소유권은 호출자에게 남습니다.

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`, 실패 시 실패 이유를 나타내는 `zlink_submit_result_t` 값. [errno-map.ko.md](../errno-map.ko.md) 참조.

**참고:** `zlink_send_rid`

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
`zlink_multipart_close()`를 호출하고 배열을 해제해야 합니다. STREAM의 세 수신
모드 중 raw recv 모드일 때만 사용할 수 있습니다. raw callback 모드
(`zlink_recv_handler()` 부착) 또는 packet callback 모드
(`zlink_stream_packet_handler()` 부착)에서는 `errno=EBUSY`로 실패합니다.
메시지가 없을 때 즉시 반환하려면 `ZLINK_DONTWAIT`를 전달하세요.

**반환값:** 성공 시 `ZLINK_RECV_OK`, 실패 시 `zlink_recv_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** 작업이 블로킹되고 `ZLINK_DONTWAIT`가 설정된 경우, 또는
`ZLINK_OPT_RCVTIMEO`가 만료된 경우 `EAGAIN`. raw callback이나 packet
callback이 부착된 경우 `EBUSY`. Context가 종료된 경우 `ETERM`.

**참고:** `zlink_send`, `zlink_recv_handler`, `zlink_stream_packet_handler`,
`zlink_multipart_close`

---

### STREAM session Actor list

STREAM session Actor list는 STREAM client session routing id와 Actor ref를 연결하는
per-session 매핑이다. 이 매핑은 STREAM socket의 public lookup 대상이 아니다. 한
session은 여러 Actor를 bind할 수 있고, 한 Actor는 동시에 하나의 STREAM session에만
bind될 수 있다.

```c
zlink_request_result_t zlink_stream_bind_actor(
  void *node,
  void *stream,
  const zlink_routing_id_t *session_rid,
  const zlink_actor_ref_t *actor,
  uint32_t timeout_ms);

zlink_request_result_t zlink_stream_unbind_actor(
  void *node,
  void *stream,
  const zlink_routing_id_t *session_rid,
  const char *actor_id,
  uint32_t timeout_ms);

zlink_submit_result_t zlink_stream_send_bound_actor_part(
  void *node,
  void *stream,
  const zlink_routing_id_t *session_rid,
  const char *actor_id,
  zlink_msg_t *part,
  zlink_send_flags_t flags,
  zlink_part_flag_t part_flag);
```

- `node`는 STREAM session owner `SpotNode`다. session owner node 없이 bind, unbind,
  relay는 수행하지 않는다.
- `stream`은 session routing id가 속한 raw STREAM socket이다.
- `session_rid`는 STREAM client session routing id다.
- 같은 session에 서로 다른 Actor id를 여러 개 bind할 수 있다.
- 같은 session의 같은 Actor id를 다시 바인딩하면 그 Actor id 항목만 새 Actor ref로
  교체한다.
- 같은 session에 같은 Actor ref를 다시 바인딩하면 중복 항목을 만들지 않고 성공한다.
- 이미 다른 session에 바인딩된 Actor를 바인딩하면 `ZLINK_REQUEST_BUSY` 계열 실패다.
- `zlink_actor_ref_t.generation == 0`인 unchecked ref로 바인딩하면 target node의 현재
  같은 Actor id Actor를 연결하고, session Actor list에는 concrete generation을 가진
  ref가 저장된다.
- checked ref의 generation이 target Actor와 다르면 conflict 또는 invalid-state 계열
  실패다.
- 바인딩 성공 시 Actor owner node에서 actor route sync가 켜져 있으면 active route가
  publish된다. Actor 생성만으로는 active route가 publish되지 않는다.
- 바인딩 해제는 없는 Actor id에 대해서도 성공으로 끝나는 idempotent 작업이다.
- 여러 Actor가 바인딩된 session에서 한 Actor id의 바인딩을 해제해도 다른 Actor 항목은
  유지된다.
- remote Actor owner node와 연결이 없으면 explicit unbind는
  `ZLINK_REQUEST_NOT_CONNECTED`로 실패하고 기존 Actor id 항목을 유지한다.
- Actor owner provider 종료가 확인된 뒤의 explicit unbind는 detach 확인 없이 session
  Actor list 항목을 제거하고 성공할 수 있다.
- bind/unbind timeout 실패 뒤 session Actor list와 Actor bound session ref는 호출 전
  상태로 유지된다.
- unbind와 session disconnect cleanup은 active route를 제거하지 않는다.

`zlink_stream_send_bound_actor_part()`는 `actor_id` selector가 가리키는 Actor unread
state로 STREAM session 메시지 part를 relay한다.

- session Actor list에 `actor_id` 항목이 없으면 `ZLINK_SUBMIT_NOT_FOUND` 계열 실패다.
- `actor_id`가 잘못됐거나 NULL이면 `ZLINK_SUBMIT_INVALID_ARGUMENT` 계열 실패다.
- 성공 시 `part` 소유권은 라이브러리로 이전된다. 실패 시 호출자에게 남는다.
- multipart 중 `ZLINK_PART_MORE`가 성공하면 같은 session의 다음 part는 같은
  `actor_id`로만 보낼 수 있다. 다른 Actor id를 쓰면 `ZLINK_SUBMIT_INVALID_STATE`다.
- final part submit이 실패하면 이미 성공한 part는 라이브러리가 소유하고, caller는
  같은 Actor id로 final part를 다시 시도할 수 있다.
- target Actor가 remote node에서 이미 사라진 경우 target node에서 메시지를 버릴 수
  있으며, sender의 완료된 send 결과는 바뀌지 않는다.
- target Actor owner node와 연결이 없으면 `ZLINK_SUBMIT_NOT_CONNECTED` 계열 실패다.
- relay 경로의 내부 자원 부족이나 HWM 초과는 `ZLINK_SUBMIT_BACKPRESSURED` 계열
  실패다.

---

### zlink_recv_handler

raw `STREAM` 소켓에 raw 수신 콜백을 부착합니다.

```c
zlink_handler_result_t zlink_recv_handler (
  void *s_, zlink_socket_msg_handler_fn handler_, void *userdata_);
```

멀티파트 수신 subject에 raw 메시지 수신 핸들러를 부착합니다. 지원 대상은
raw `STREAM` 뿐입니다. 지원하지 않는 subject(PAIR, DEALER 등)는 `ENOTSUP`로
실패합니다. attach 이후 같은 handle의 `zlink_recv()`, `zlink_stream_packet_handler()`,
data-plane poller `ZLINK_POLLIN`은 `errno=EBUSY`로 실패합니다. 동일 handle에
대한 두 번째 attach도 `errno=EBUSY`입니다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`. 실패 시에는 `zlink_handler_result_t`
값을 반환합니다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지됩니다.

**참고:** `zlink_recv`, `zlink_stream_packet_handler`

---

### zlink_stream_packet_handler

raw `STREAM` 소켓에 packet 단위 수신 콜백을 부착합니다.

```c
typedef void (*zlink_stream_packet_handler_fn) (
  void *stream_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *header_,
  zlink_msg_t *body_,
  void *userdata_);

zlink_handler_result_t zlink_stream_packet_handler (
  void *stream_,
  zlink_stream_packet_handler_fn handler_,
  void *userdata_);
```

이 함수는 raw `STREAM` 전용입니다. 다른 소켓 타입에서는 `ENOTSUP`로
실패합니다.

attach 이후에는 구현이 연결별 수신 바이트를 내부에 누적해 packet 하나가
완성될 때마다 콜백을 호출합니다. framing 규약은 고정이며 아래 순서로 읽어
해석합니다.

1. `header_size`: 2바이트 big-endian `uint16_t`
2. `body_size`: 4바이트 big-endian `uint32_t`
3. header payload (`header_size` 바이트)
4. body payload (`body_size` 바이트)

`header_size == 0` 또는 `body_size == 0`인 packet도 허용됩니다. 둘 다 0인
packet도 허용됩니다. 이 경우에도 `header_`, `body_`는 길이가 0인 유효한
`zlink_msg_t`로 전달되며, `NULL`이 넘어오지 않습니다.

소유권 규칙은 아래와 같습니다.

- `source_rid_`는 packet을 보낸 client 연결의 routing id를 가리키는
  borrowed view입니다. 콜백 실행 중에만 유효하며, 이후에도 유지하려면
  호출자가 값을 복사해야 합니다.
- `header_`와 `body_`의 소유권은 콜백으로 이전됩니다. 콜백은 두 `msg_t`를
  각각 정확히 한 번 닫거나 소비해야 합니다.

같은 handle에 이미 raw callback 모드(`zlink_recv_handler()`)가 붙어 있으면
이 함수는 `EBUSY`로 실패합니다. 반대로 packet callback이 이미 붙어 있는
handle에서 `zlink_recv()`, `zlink_recv_handler()`, data-plane
`ZLINK_POLLIN`은 모두 `EBUSY`로 실패합니다. 같은 handle에 대한 두 번째
`zlink_stream_packet_handler()` attach도 `EBUSY`입니다.

조립 중 malformed packet(불완전 상태로 연결 종료, 선언 길이가 구현 제한을
초과, 내부 조립 실패 등)이 감지되면 해당 연결은 packet mode 기준으로
invalid stream으로 처리되며 기본 동작은 연결 종료입니다. 부분 packet이
콜백으로 전달되지는 않습니다. malformed 이벤트는 socket monitor 경로에서
관찰합니다.

**반환값:** 성공 시 `ZLINK_HANDLER_OK`. 실패 시에는 `zlink_handler_result_t`
값을 반환합니다. 상세 내부 errno는 진단을 위해 `zlink_errno()`로 유지됩니다.

**에러:** 핸들러가 NULL이면 `INVALID_ARGUMENT`. handle이 raw `STREAM`이
아니면 `NOT_SUPPORTED`. 이미 다른 수신 모드가 활성화된 경우 `BUSY`.

**참고:** `zlink_recv`, `zlink_recv_handler`

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
