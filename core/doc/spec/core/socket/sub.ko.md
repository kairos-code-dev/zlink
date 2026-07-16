[English](sub.md) | [한국어](sub.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md)

# 소켓 -- SUB

토픽 필터링을 사용하는 구독 소켓. SUB는 데이터 수신 전용이며,
구독 관리가 제어 플레인입니다.

## 자동 HWM 기본값

SUB는 context auto HWM 정책에서 `recv_ingress` policy class로 분류됩니다.
활성 auto-HWM profile이 단위 예산과 메시지 크기 cap을 고르며, 기본 profile은
`balanced`입니다. 사용자가 `RCVHWM`이나 `RCVBUF`를 직접 설정하면 자동값보다
그 값이 우선합니다.

## Sub 옵션 (`zlink_sub_option_t`)

`zlink_set_sub_option()` / `zlink_get_sub_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_SUB_OPT_TOPICS_COUNT` | 구독된 토픽 수 (읽기 전용, `int`) |

## 함수

### zlink_set_sub_option

SUB/XSUB 소켓 전용 옵션을 설정합니다.

```c
zlink_config_result_t zlink_set_sub_option (void *handle_,
                           zlink_sub_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
```

SUB/XSUB 소켓 옵션을 설정합니다. 모든 소켓 타입에 공유되는 공통 옵션은
`zlink_set_option()`을 사용합니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_get_sub_option`, `zlink_set_option`

---

### zlink_get_sub_option

SUB/XSUB 소켓 전용 옵션을 조회합니다.

```c
zlink_config_result_t zlink_get_sub_option (void *handle_,
                           zlink_sub_option_t option_,
                           void *optval_,
                           size_t *optvallen_);
```

SUB/XSUB 소켓 옵션의 현재 값을 가져옵니다.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_set_sub_option`

---

### zlink_set_subscription

토픽 필터를 구독합니다.

```c
zlink_config_result_t zlink_set_subscription (void *handle_, const char *filter_);
```

`filter_`에 매칭되는 메시지를 구독합니다. 구독은 byte-prefix 필터입니다:
메시지의 토픽이 `filter_` 바이트로 시작하면 매칭됩니다. 빈 `filter_`는 모든
메시지를 구독합니다. 필터 바이트는 binary-safe이며 wildcard 구문은 없습니다
(후행 `*`는 리터럴 바이트로 매칭됩니다).

적용 대상: raw SUB, raw XSUB.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** `handle_`이 NULL이면 `EFAULT`. `filter_`가 NULL이거나 handle 타입이
구독을 지원하지 않으면 `EINVAL`.

**참고:** `zlink_unset_subscription`, `zlink_subscribe`

---

### zlink_unset_subscription

토픽 필터 구독을 해제합니다.

```c
zlink_config_result_t zlink_unset_subscription (void *handle_, const char *filter_);
```

이전에 등록된 구독을 제거합니다. `zlink_set_subscription()`과 동일한
byte-prefix 해석이 적용되며, `filter_` 바이트가 이전에 등록한 prefix와
일치해야 합니다.

적용 대상: raw SUB, raw XSUB.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** `handle_`이 NULL이면 `EFAULT`. `filter_`가 NULL이거나 handle 타입이
구독 해제를 지원하지 않으면 `EINVAL`.

**참고:** `zlink_set_subscription`

---

### zlink_subscribe

토픽 기반 멀티파트 메시지를 수신합니다.

```c
zlink_recv_result_t zlink_subscribe (void *subject_,
                     zlink_routing_id_t *source_rid_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_out_,
                     zlink_recv_flags_t flags_);
```

recv 모드에서 다음 토픽 기반 메시지를 수신합니다. 성공 시
`*source_rid_out_`는 송신자의 routing id (transport가 identity를 전달하지
않으면 zeroed), `*topic_id_out_` / `*topic_id_len_out_`는 토픽 바이트
(binary-safe), `*parts_out_` / `*part_count_out_`는 payload 프레임을
받습니다. 파트 배열의 소유권은 호출자에게 이전됩니다.

raw SUB/XSUB는 recv-only 타입입니다. poller의 `ZLINK_POLLIN`과 함께 사용해
서버 루프에서 readable을 관찰한 뒤 이 함수로 토픽 메시지를 가져오는 방식을
기본 경로로 합니다.

적용 대상: raw SUB, raw XSUB.

**반환값:** 성공 시 `ZLINK_RECV_OK`, 실패 시 `zlink_recv_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** `subject_`가 NULL이면 `EFAULT`. `ZLINK_DONTWAIT`가 설정되고
메시지가 없으면 `EAGAIN`. 토픽 버퍼가 작으면 `EMSGSIZE`. subject 타입이
subscribe recv를 지원하지 않으면 `ENOTSUP`.

**참고:** `zlink_set_subscription`

---

### zlink_subscribe_part

raw `SUB` 또는 `XSUB` 소켓에서 토픽 메시지의 payload 파트 하나를
수신합니다.

```c
ZLINK_EXPORT zlink_recv_result_t zlink_subscribe_part (void *sub_,
                                                       const zlink_routing_id_t **source_rid_out_,
                                                       char *topic_id_buf_,
                                                       size_t topic_id_capacity_,
                                                       size_t *topic_id_len_out_,
                                                       zlink_msg_t *part_out_,
                                                       zlink_part_flag_t *has_more_out_,
                                                       zlink_recv_flags_t flags_);
```

`topic_id_len_out_`, 초기화된 `part_out_`, `has_more_out_`은 필수입니다.
`source_rid_out_`은 선택 사항이며 raw `SUB`와 `XSUB`에서는 항상 `NULL`을
받습니다. 성공하면 토픽 바이트를 호출자 버퍼에 복사하고 payload 파트의
소유권을 호출자에게 이전합니다. 토픽 바이트는 binary-safe이며 NUL 문자를
덧붙이지 않습니다. 호출자는 받은 파트를 `zlink_msg_close(part_out_)`로
정확히 한 번 닫아야 합니다.

`topic_id_capacity_ == 0`이면 `topic_id_buf_`는 NULL이어도 되며, 필요한 토픽
길이와 payload 파트를 정상적으로 반환합니다. 용량이 0보다 큰데 버퍼가
NULL이면 `errno`는 `EFAULT`, 버퍼가 작으면 `errno`는 `EMSGSIZE`입니다. 이 두
오류는 payload 파트를 이미 수신한 뒤 발생하므로 `topic_id_len_out_`,
`part_out_`, `has_more_out_`은 유효하고 파트 소유권도 호출자에게 이전됩니다.
작은 버퍼는 변경하지 않습니다. payload를 받기 전에 발생한 다른 실패에서는
파트 소유권이 이전되지 않습니다.

한 멀티파트 메시지의 첫 payload 파트부터 마지막 파트까지 같은 스레드에서 이
함수로 계속 수신해야 합니다. `*has_more_out_`은 다음 payload 파트가 있으면
`ZLINK_PART_MORE`, 마지막이면 `ZLINK_PART_FINAL`입니다. 적용 타입은 raw
`SUB`, raw `XSUB`입니다.

---

### zlink_subscription_at

지정된 인덱스의 구독 필터를 조회한다.

```c
zlink_config_result_t zlink_subscription_at (void *handle_,
                           size_t index_,
                           char *filter_out_,
                           size_t *filter_len_inout_,
                           int *is_pattern_out_);
```

`index_` (0-기반)에 해당하는 구독 필터 문자열을 반환합니다. 진입 시
`*filter_len_inout_`는 버퍼 크기이며, 반환 시 실제 길이로 설정됩니다.
`*is_pattern_out_`는 필터가 패턴 구독인지 보고하며, 모든 raw 구독이 byte-prefix
필터이므로 10.0.0에서는 항상 `0`을 반환합니다.

적용 타입: raw SUB, raw XSUB.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**에러:** 인덱스가 범위를 벗어나면 `ENOENT`. 버퍼가 작으면 `EINVAL`.
handle 타입이 구독 조회를 지원하지 않으면 `ENOTSUP`.

**참고:** `zlink_set_subscription`, `zlink_get_sub_option`
