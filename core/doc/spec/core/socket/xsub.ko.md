[English](xsub.md) | [한국어](xsub.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md)

# 소켓 -- XSUB

구독 전달을 지원하는 확장 구독자. XSUB는 SUB와 동일한
subscribe/unsubscribe 및 토픽 수신 API를 지원하며, 구독 메시지가 업스트림으로
전달됩니다.

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
