[English](06-dealer.en.md) | 한국어

[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md) · [errno map](../04-errno-map.ko.md)

# 소켓 — DEALER

DEALER는 여러 peer에서 공정 큐잉으로 수신하고, 연결된 peer에 순환 또는 가중치 기반으로 송신하는
비동기 raw socket이다. 일반 raw message와 request/reply record를 같은 socket에서 처리할 수 있다.

## 1. 공개 타입

다음 숫자는 공개 ABI 값이다.

```c
typedef enum zlink_dealer_option_t {
  ZLINK_DEALER_OPT_PROBE              = 0x3201,
  ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS = 0x3202,
  ZLINK_DEALER_OPT_WEIGHT             = 0x3203
} zlink_dealer_option_t;

typedef enum zlink_dealer_message_type_t {
  ZLINK_DEALER_MESSAGE_RAW         = 0,
  ZLINK_DEALER_MESSAGE_REQUEST     = 1,
  ZLINK_DEALER_MESSAGE_REPLY       = 2,
  ZLINK_DEALER_MESSAGE_ERROR_REPLY = 3
} zlink_dealer_message_type_t;

typedef enum zlink_part_flag_t {
  ZLINK_PART_FINAL = 0,
  ZLINK_PART_MORE  = 1
} zlink_part_flag_t;

typedef void (*zlink_reply_handler_fn)(
  zlink_request_result_t result_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);
```

`ZLINK_PART_MORE`는 같은 multipart record에 뒤따르는 part가 있음을 뜻한다.
`ZLINK_PART_FINAL`은 현재 part가 그 record의 마지막임을 뜻한다. receive API의
`has_more_out_`도 같은 두 값을 사용한다.

## 2. DEALER option

```c
ZLINK_EXPORT zlink_config_result_t zlink_set_dealer_option(
  void *handle_,
  zlink_dealer_option_t option_,
  const void *optval_,
  size_t optvallen_);

ZLINK_EXPORT zlink_config_result_t zlink_get_dealer_option(
  void *handle_,
  zlink_dealer_option_t option_,
  void *optval_,
  size_t *optvallen_);
```

| 상수 | 값 형식 | 의미 |
|---|---|---|
| `ZLINK_DEALER_OPT_PROBE` | `int`, `0` 또는 `1` | 연결을 설정할 때 빈 raw message를 보내 peer가 연결과 routing ID를 관찰할 수 있게 한다. 기본값은 `0`이다 |
| `ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS` | 0 이상인 `int`, millisecond | request API에서 `timeout_ms_ == 0`일 때 사용할 기본 timeout을 정한다. 기본값은 `5000`이다 |
| `ZLINK_DEALER_OPT_WEIGHT` | `int`, `0..100` | 연결된 peer에 알리는 이 DEALER의 가중치다. 기본값은 `100`이다 |

`zlink_get_dealer_option()`을 호출할 때 `*optvallen_`은 `optval_`의 입력 용량이다. 성공하면 실제로
쓴 byte 수로 갱신된다. DEALER 전용이 아닌 HWM, reconnect와 timeout option은
`zlink_set_option()`과 `zlink_get_option()`을 사용한다.

양수 가중치가 같은 outbound peer는 순환 방식으로 선택한다. 양수 가중치가 다르면 그 비율을
선택 빈도에 반영하고, 가중치가 `0`인 peer는 후보에서 제외한다. 알려진 peer의 가중치가 모두
`0`이면 submit은 `ZLINK_SUBMIT_NOT_ADMITTED`로 실패할 수 있다.

## 3. Message record 구분

`zlink_dealer_recv_part()`는 payload part와 함께 record 종류와 request sequence를 반환한다.

| `message_type_out_` 값 | `request_seq_out_` | 의미 |
|---|---:|---|
| `ZLINK_DEALER_MESSAGE_RAW` (`0`) | `0` | request/reply envelope가 없는 일반 raw multipart message |
| `ZLINK_DEALER_MESSAGE_REQUEST` (`1`) | 0이 아닌 값 | 이 DEALER가 수신한 request다. 반환된 값은 `zlink_dealer_reply_part()`에 넘길 reply token이다 |
| `ZLINK_DEALER_MESSAGE_REPLY` (`2`) | 0이 아닌 값 | 성공 reply record |
| `ZLINK_DEALER_MESSAGE_ERROR_REPLY` (`3`) | 0이 아닌 값 | 실패 reply record |

한 multipart record의 모든 part에는 같은 message type과 request sequence가 반환된다. request API로
시작한 작업의 reply와 terminal failure는 `zlink_reply_handler_fn` completion으로 전달된다. 일반 raw
message를 보낼 때는 다음 API를 사용하며 request sequence를 만들지 않는다.

```c
ZLINK_EXPORT zlink_submit_result_t zlink_send_part(
  void *s_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);
```

## 4. Part sequence와 소유권

`*_part` send 호출은 `ZLINK_PART_MORE`부터 `ZLINK_PART_FINAL`까지 하나의 multipart sequence를
구성한다. 열린 sequence가 있는 동안 같은 handle에서 다른 send helper family를 섞을 수 없다.

초기화된 유효한 `part_`를 send API에 넘기면 함수는 성공과 실패 모두에서 그 message 내용을
소비한다. 따라서 호출 결과와 관계없이 호출자가 전송 전 payload를 다시 읽거나 같은 내용을 다시
보낼 수 없다. 다시 보내야 하는 payload는 호출 전에 별도 message로 보관해야 한다.

각 send helper family는 성공한 중간 파트를 `ZLINK_PART_FINAL`이 성공할 때까지 하나의 record로
staging한다. 열린 sequence의 중간 또는 마지막 submit이 실패하면 Core는 이전에 staging한 파트와
실패한 파트를 원자적으로 폐기하고 sequence를 닫는다. peer에는 그 record의 어떤 파트도 보이지
않는다. 실패한 호출의 `part_`도 소비되며 다음 submit은 새 record의 첫 파트로 시작한다. 실패한
request submit은 request sequence를 만들지 않고 handler도 호출하지 않는다. reply sequence가 실패하면
reply token은 성공한 `ZLINK_PART_FINAL`이나 request lifecycle 종료 전까지 유효하므로 caller가 보관한 전체 reply를
첫 파트부터 다시 제출할 수 있다.

receive API의 `part_out_`은 호출 전에 초기화된 `zlink_msg_t`여야 한다. 성공하면 수신 part의
소유권이 호출자에게 이동하며 호출자는 `zlink_msg_close()`로 정확히 한 번 해제한다. 실패하면
수신 part 소유권은 이동하지 않는다.

## 5. Raw request submit

```c
ZLINK_EXPORT zlink_submit_result_t zlink_dealer_request_part(
  void *dealer_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_);
```

하나의 비동기 request payload를 part 단위로 제출한다. 중간 part는 `part_flag_`를
`ZLINK_PART_MORE`로 두고 `timeout_ms_ == 0`, `handler_ == NULL`, `userdata_ == NULL`로 호출한다.
마지막 part는 `ZLINK_PART_FINAL`과 0이 아닌 `handler_`를 사용한다. 마지막 호출의
`timeout_ms_ == 0`은 `ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS` 기본값을 사용한다. `flags_`는
`ZLINK_SEND_FLAGS_NONE` 또는 `ZLINK_SEND_FLAGS_DONTWAIT`다.

마지막 submit이 `ZLINK_SUBMIT_OK`이면 completion은 정확히 한 번 `handler_`로 전달된다. submit이
실패하면 handler를 호출하지 않는다. callback의 `parts_`와 각 message의 소유권은 callback으로
이동하며 callback은 이를 정확히 한 번 해제한다. timeout과 다른 terminal result에서는
`zlink_request_result_t`가 결과를 나타낸다.

## 6. Raw record receive

```c
ZLINK_EXPORT zlink_recv_result_t zlink_dealer_recv_part(
  void *dealer_,
  uint8_t *message_type_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_);
```

완전한 record에서 part 하나를 반환한다. 모든 output pointer는 필수다. `message_type_out_`의 C 타입은
`uint8_t`이며 값은 `zlink_dealer_message_type_t`에 정의된 숫자 중 하나다. `flags_`는
`ZLINK_RECV_FLAGS_NONE` 또는 `ZLINK_RECV_FLAGS_DONTWAIT`다. non-blocking 호출에 받을 record가 없으면
`ZLINK_RECV_NO_DATA`와 `EAGAIN`을 반환한다.

`has_more_out_ == ZLINK_PART_MORE`이면 다음 호출로 같은 record의 다음 part를 받아야 한다.
`ZLINK_PART_FINAL`이면 record 수신이 끝난다.

## 7. Raw reply submit

```c
ZLINK_EXPORT zlink_submit_result_t zlink_dealer_reply_part(
  void *dealer_,
  uint64_t request_seq_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_);
```

`ZLINK_DEALER_MESSAGE_REQUEST` record에 reply part를 보낸다. `request_seq_`는 같은 socket의
`zlink_dealer_recv_part()`가 그 request에 대해 반환한 0이 아닌 reply token이어야 한다. 여러 part로
reply할 때 모든 호출에서 같은 token을 사용한다. `ZLINK_PART_FINAL`이 성공하면 그 token의 reply가
완료되며 다시 사용할 수 없다.

## 8. Result와 readiness

submit은 `zlink_submit_result_t`, receive는 `zlink_recv_result_t`, option은
`zlink_config_result_t`를 반환한다. 각 result와 `zlink_errno()`의 대응은
[errno map](../04-errno-map.ko.md)을 따른다.

DEALER의 `ZLINK_POLLIN`은 raw 또는 request/reply record를 수신할 수 있음을 뜻한다.
`ZLINK_POLLOUT`과 `zlink_send_ready_handler()`는 backpressure 뒤 submit을 다시 시도할 가치가 있음을
나타내지만 다음 submit 성공을 보장하지 않는다.
