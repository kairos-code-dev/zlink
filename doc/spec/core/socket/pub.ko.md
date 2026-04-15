[English](pub.md) | [한국어](pub.ko.md)


[스펙 목차](../../README.ko.md) · [코어 목차](../README.ko.md) · [소켓 공통](README.ko.md)

# 소켓 -- PUB

발행 전용 소켓, 토픽 기반 fan-out. PUB는 송신 전용이며 수신 함수는
적용되지 않습니다.

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

`ZLINK_PUB_OPT_NODROP`의 기본값은 `1`입니다. HWM이 찼을 때 조용히 drop하는
대신 `zlink_publish()`가 `ZLINK_SUBMIT_BACKPRESSURED`를 반환합니다. 호출자가
조용한 drop 동작을 원하면 명시적으로 `0`으로 설정해야 합니다.

## 함수

### zlink_set_pub_option

PUB/XPUB 소켓, spot-pub, spotnode-pub 전용 옵션을 설정합니다.

```c
zlink_config_result_t zlink_set_pub_option (void *handle_,
                           zlink_pub_option_t option_,
                           const void *optval_,
                           size_t optvallen_);
```

PUB/XPUB 소켓 옵션을 설정합니다. spot-pub과 spotnode-pub 핸들에도
적용됩니다. 모든 소켓 타입에 공유되는 공통 옵션은 `zlink_set_option()`을
사용하세요.

**반환값:** 성공 시 `ZLINK_CONFIG_OK`, 실패 시 `zlink_config_result_t` 값. `zlink_errno()`는 진단용 내부 errno를 그대로 유지합니다.

**참고:** `zlink_get_pub_option`, `zlink_set_option`

---

### zlink_get_pub_option

PUB/XPUB 소켓, spot-pub, spotnode-pub 전용 옵션을 조회합니다.

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

- raw `PUB` / `XPUB`: `topic_id_`는 NULL이어야 합니다 (raw pub publish).
  토픽 매칭은 wire first-frame prefix 규칙을 따릅니다.

**반환값:** 성공 시 `ZLINK_SUBMIT_OK`. 실패 시에는
`zlink_submit_result_t` 값을 반환합니다. 상세 내부 errno는 진단을 위해
`zlink_errno()`로 유지됩니다.

**에러:** `subject_`가 NULL이면 `EFAULT`. `topic_id_`가 spot/spot_node에서
NULL이거나 지원하지 않는 타입이면 `EINVAL`. subject 타입이 publish를
지원하지 않으면 `ENOTSUP`.

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
