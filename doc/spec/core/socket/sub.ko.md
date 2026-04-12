[English](sub.md) | [한국어](sub.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md)

# 소켓 -- SUB

토픽 필터링을 사용하는 구독 소켓. SUB는 데이터 수신 전용이며,
구독 관리가 제어 플레인입니다.

## Sub 옵션 (`zlink_sub_option_t`)

`zlink_set_sub_option()` / `zlink_get_sub_option()`과 함께 사용합니다.

| 상수 | 설명 |
|---|---|
| `ZLINK_SUB_OPT_TOPICS_COUNT` | 구독된 토픽 수 (읽기 전용, `int`) |

## 함수

### zlink_set_sub_option

SUB/XSUB 소켓, spot-sub, spotnode-sub 전용 옵션을 설정합니다.

```c
int zlink_set_sub_option (void *handle_,
                           zlink_sub_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
```

SUB/XSUB 소켓 옵션을 설정합니다. spot-sub과 spotnode-sub 핸들에도
적용됩니다. 모든 소켓 타입에 공유되는 공통 옵션은 `zlink_set_option()`을
사용하세요.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_get_sub_option`, `zlink_set_option`

---

### zlink_get_sub_option

SUB/XSUB 소켓, spot-sub, spotnode-sub 전용 옵션을 조회합니다.

```c
int zlink_get_sub_option (void *handle_,
                           zlink_sub_option_t option_,
                           void *optval_,
                           size_t *optvallen_);
```

SUB/XSUB 소켓 옵션의 현재 값을 가져옵니다.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**참고:** `zlink_set_sub_option`

---

### zlink_set_subscription

토픽 필터를 구독합니다.

```c
int zlink_set_subscription (void *handle_, const char *filter_);
```

`filter_`에 매칭되는 메시지를 구독합니다. 필터 해석: `filter_`가 `*`로
끝나면 prefix-match 패턴이고, 그 외는 exact topic입니다.

적용 대상: raw SUB, raw XSUB.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `handle_`이 NULL이면 `EFAULT`. `filter_`가 NULL이거나 비어있거나
유효하지 않은 패턴 구문(복수 `*`, 중간 `*`)이면 `EINVAL`. handle 타입이
구독을 지원하지 않으면 `ENOTSUP`.

**참고:** `zlink_unset_subscription`, `zlink_subscribe`

---

### zlink_unset_subscription

토픽 필터 구독을 해제합니다.

```c
int zlink_unset_subscription (void *handle_, const char *filter_);
```

이전에 등록된 구독을 제거합니다. `zlink_set_subscription()`과 동일한 문자열
해석 규칙이 적용됩니다: trailing `*`는 패턴 해제, 그 외는 exact topic 해제.

적용 대상: raw SUB, raw XSUB.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `handle_`이 NULL이면 `EFAULT`. `filter_`가 NULL이거나 비어있으면
`EINVAL`. handle 타입이 구독 해제를 지원하지 않으면 `ENOTSUP`.

**참고:** `zlink_set_subscription`

---

### zlink_subscribe

토픽 기반 멀티파트 메시지를 수신합니다.

```c
int zlink_subscribe (void *subject_,
                     zlink_routing_id_t *source_rid_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_out_,
                     zlink_send_flags_t flags_);
```

recv 모드에서 다음 토픽 기반 메시지를 수신합니다. 성공 시
`*source_rid_out_`는 송신자의 routing id (transport가 identity를 전달하지
않으면 zeroed), `*topic_id_out_` / `*topic_id_len_out_`는 토픽 바이트
(binary-safe), `*parts_out_` / `*part_count_out_`는 페이로드 프레임을
받습니다. 파트 배열의 소유권은 호출자에게 이전됩니다.

subject가 recv 모드여야 합니다 (핸들러 미부착). subscribe handler가 부착된
경우 `EBUSY`로 실패합니다.

적용 대상: raw SUB, raw XSUB, `spot`, `spot_node`.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** `subject_`가 NULL이면 `EFAULT`. `ZLINK_DONTWAIT`가 설정되고
메시지가 없으면 `EAGAIN`. subscribe handler가 부착된 경우 `EBUSY`. 토픽
버퍼가 작으면 `EMSGSIZE`. subject 타입이 subscribe recv를 지원하지 않으면
`ENOTSUP`.

**참고:** `zlink_subscribe_handler`, `zlink_set_subscription`

---

### zlink_subscribe_handler

소켓에 토픽 기반 수신 핸들러를 부착합니다.

```c
bool zlink_subscribe_handler (void *s_,
                              zlink_subscribe_handler_fn handler_,
                              void *userdata_);
```

raw `SUB`, raw `XSUB`, `spot`, `spot_node`에 토픽 기반 수신 핸들러를
부착합니다. attach 이후 같은 subject의 `zlink_subscribe()`와 data-plane
poller `ZLINK_POLLIN`은 `errno=EBUSY`로 실패합니다. 동일 subject에 대한
두 번째 attach도 `errno=EBUSY`입니다. 지원하지 않는 subject는 `ENOTSUP`를
반환합니다.

**반환값:** 성공 시 `true`, 실패 시 `false` (errno가 설정됨).

**에러:** 핸들러가 NULL이면 `EINVAL`. handle 타입이 subscribe handler를
허용하지 않으면 `ENOTSUP`. 핸들러가 이미 부착된 경우 `EBUSY`.

**참고:** `zlink_recv_handler`, `zlink_socket`, `zlink_close`

---

### zlink_subscription_at

지정된 인덱스의 구독 필터를 조회한다.

```c
int zlink_subscription_at (void *handle_,
                           size_t index_,
                           char *filter_out_,
                           size_t *filter_len_inout_,
                           int *is_pattern_out_);
```

`index_` (0-기반)에 해당하는 구독 필터 문자열을 반환한다. 진입 시
`*filter_len_inout_`는 버퍼 크기이며, 반환 시 실제 길이로 설정된다.
`*is_pattern_out_`는 필터가 prefix 패턴(후행 `*`)이면 1, exact이면 0이다.

적용 타입: raw SUB, raw XSUB.

**반환값:** 성공 시 0, 실패 시 -1 (errno가 설정됨).

**에러:** 인덱스가 범위를 벗어나면 `EINVAL`. 버퍼가 작으면 `EMSGSIZE`.
handle 타입이 구독 조회를 지원하지 않으면 `ENOTSUP`.

**참고:** `zlink_set_subscription`, `zlink_get_sub_option`
