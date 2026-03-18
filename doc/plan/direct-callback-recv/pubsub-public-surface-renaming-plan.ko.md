# Pub/Sub Public Surface Renaming Plan

> 상태: `Msg-Only Data Plane Realignment` 이후에 이어서 검토할 후속 작업 초안.
> 현재 구현 기준 source of truth가 아니라 다음 단계 public naming 재편 제안이다.
> 현재 구현/회귀 수정은 먼저
> `msg-only-data-plane-realignment-plan.ko.md`를 기준으로 끝내고,
> 그 다음 단계에서 이 문서를 별도 승인 후 적용한다.

## 1. 목적

현재 public data-plane은 일반 transport 계열에 대해 `send` / `recv` 중심으로
정리되어 있다. 그러나 pub/sub 계열은 의미상 `publish` / `subscribe`가 더
직접적이며, 특히 `spot`, `spot_node`, `XPUB` 예외 family까지 포함하면
현재 naming은 일관성이 약하다.

이 문서는 pub/sub 계열 public C API를 `publish` / `subscribe` /
`subscription event` 축으로 재편하는 초안을 정리한다.

핵심 목표:

- 일반 transport 계열의 canonical `send` / `recv`는 유지한다.
- pub/sub 계열만 별도 public family로 분리한다.
- `spot`, `spot_node`, raw `PUB/SUB/XSUB/XPUB`를 같은 pub/sub naming 축으로 묶는다.
- `XPUB`는 publish data-plane이 아니라 subscription event-plane으로 정리한다.

비목표:

- 일반 transport family의 `zlink_send` / `zlink_send_rid` / `zlink_recv`를 없애지 않는다.
- 이번 초안만으로 현재 구현 기준 문서를 대체하지 않는다.
- `XPUB`를 일반 multipart data-plane으로 흡수하지 않는다.
- pattern 문법을 현재 `spot`이 쓰는 suffix `*` prefix-match 규칙 이상으로 확장하지 않는다.


## 2. 범위와 원칙

### 2.1 유지되는 canonical family

다음 public API는 일반 multipart transport family로 유지한다.

- `zlink_send`
- `zlink_send_rid`
- `zlink_recv`
- `zlink_recv_handler`

적용 대상:

- `PAIR`
- `DEALER`
- `ROUTER`
- `STREAM`
- `gateway`

### 2.2 pub/sub family로 재편되는 대상

다음 subject/type은 pub/sub family로 재편한다.

- raw `PUB`
- raw `SUB`
- raw `XSUB`
- raw `XPUB`
- `spot`
- `spot_node`

### 2.3 의미 모델

- 일반 peer/multipart transport는 `send` / `recv`
- topic-bearing fanout은 `publish` / `subscribe`
- `XPUB`는 data-plane이 아니라 subscription event-plane

### 2.4 적용 순서

적용 순서는 아래와 같다.

1. `Msg-Only Data Plane Realignment` 문서 기준 구현/회귀를 완료한다.
2. public header/테스트/guide에서 현재 `spot` / `xpub` 예외 family 사용처를 정리한다.
3. 그 다음 pub/sub naming 재편을 별도 작업으로 진행한다.

즉 이 문서는 현재 구현 단계의 추가 지시가 아니라 다음 단계 설계 초안이다.

### 2.5 Migration 원칙

- 기존 `spot` / `spot_node` / `XPUB` public 이름은 바로 compatibility wrapper로
  남기지 않고, 새 family로 일괄 치환하는 방향을 기본으로 한다.
- 다만 실제 적용 시점에는 guide, 테스트, bench/perf 호출부를 같은 단계에서
  함께 정리해야 한다.
- 이번 문서는 migration 순서를 설명하는 초안이며, 즉시 적용용 체크리스트는 아니다.


## 3. 목표 Public API

### 3.1 Publish

```c
ZLINK_EXPORT int zlink_publish (void *subject_,
                                const char *topic_id_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_);
```

의미:

- `topic_id_ != NULL`
  - topic-bearing publish
  - 적용 대상: `spot`, `spot_node`
- `topic_id_ == NULL`
  - raw pub publish
  - 적용 대상: raw `PUB`, raw `XPUB`

지원하지 않는 조합은 `ENOTSUP`다.

추가 규칙:

- `topic_id_ == NULL`은 raw pub publish에서만 허용한다.
- `spot` / `spot_node`에서 `topic_id_ == NULL`은 `EINVAL`이다.

### 3.2 Subscribe Control

```c
ZLINK_EXPORT int zlink_subscribe (void *subject_, const char *filter_);
ZLINK_EXPORT int zlink_unsubscribe (void *subject_, const char *filter_);
```

적용 대상:

- raw `SUB`
- raw `XSUB`
- `spot`
- `spot_node`

필터 해석 규칙:

- `filter_`가 `*`로 끝나면 pattern
- 그 외는 exact topic

정확한 topic 규칙:

- exact topic은 non-empty
- 길이 제한은 기존 `spot` 계약을 따른다
- exact topic은 trailing `*`를 허용하지 않는다
- pattern은 마지막 글자 하나만 `*`를 허용한다
- 중간 `*` 또는 복수 `*`는 `EINVAL`

`unsubscribe()`는 exact/pattern 구분 인자를 따로 받지 않는다.
동일한 문자열 해석 규칙으로 제거 대상을 판별한다.

추가 규칙:

- `unsubscribe("abc*")`는 exact topic이 아니라 pattern unsubscribe로 해석한다.
- exact topic 이름에 trailing `*`를 허용하지 않으므로 이 해석은 모호하지 않다.

### 3.3 Topic-bearing Subscribe Receive

```c
ZLINK_EXPORT int zlink_subscribe_recv (void *subject_,
                                       zlink_routing_id_t *source_rid_out_,
                                       zlink_msg_t **parts_out_,
                                       size_t *part_count_out_,
                                       char *topic_id_out_,
                                       size_t *topic_id_len_out_,
                                       zlink_send_flags_t flags_);
```

반환 shape:

- `source_rid + topic + multipart payload`

적용 대상:

- raw `SUB`
- raw `XSUB`
- `spot`
- `spot_node`

계약:

- raw `SUB` / `XSUB`에서 sender identity가 없으면 zeroed `source_rid_out_`
- `topic_id_out_` / `topic_id_len_out_`는 binary-safe
- 버퍼가 작으면 `EMSGSIZE`
- raw `SUB` / `XSUB`의 topic은 wire first-part에서 해석하며,
  public payload에는 topic frame을 다시 포함하지 않는다.

### 3.4 Topic-bearing Subscribe Callback

```c
typedef void (*zlink_subscribe_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);

ZLINK_EXPORT int zlink_subscribe_handler (void *subject_,
                                          zlink_subscribe_handler_fn handler_,
                                          void *userdata_);
```

적용 대상:

- raw `SUB`
- raw `XSUB`
- `spot`
- `spot_node`

의미:

- topic-aware recv callback registration
- direct recv와 동일하게 `source_rid + topic + multipart payload`를 전달한다
- raw `SUB` / `XSUB`도 같은 callback shape를 사용한다.

### 3.5 XPUB Subscription Event Receive

```c
ZLINK_EXPORT int zlink_subscription_event_recv (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_send_flags_t flags_);
```

반환 shape:

- `source_rid + subscribed + topic`

적용 대상:

- raw `XPUB`

계약:

- `subscribed_out_ == 1`이면 subscribe
- `subscribed_out_ == 0`이면 unsubscribe
- `topic_id_out_` / `topic_id_len_out_`는 `zlink_subscribe_recv()`와 동일한
  binary-safe buffer contract를 사용한다

### 3.6 XPUB Subscription Event Callback

```c
typedef void (*zlink_subscription_event_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  int subscribed_,
  const char *topic_,
  size_t topic_len_,
  void *userdata_);

ZLINK_EXPORT int zlink_subscription_event_handler (
  void *subject_,
  zlink_subscription_event_handler_fn handler_,
  void *userdata_);
```

적용 대상:

- raw `XPUB`

의미:

- `XPUB` subscription event callback registration
- 일반 multipart recv callback과 분리된 event-plane registration이다


## 4. 대체 및 제거 대상

다음 API는 새 pub/sub family로 대체한다.

### 4.1 Spot / SpotNode

- `zlink_spot_publish`
- `zlink_spot_node_publish`
- `zlink_spot_subscribe`
- `zlink_spot_subscribe_pattern`
- `zlink_spot_unsubscribe`
- `zlink_spot_node_subscribe`
- `zlink_spot_node_subscribe_pattern`
- `zlink_spot_node_unsubscribe`
- `zlink_spot_sub_recv`
- `zlink_spot_node_recv`
- `zlink_recv_spot_handler`

### 4.2 XPUB

- `zlink_xpub_recv`
- `zlink_recv_xpub_handler`

이 변경 이후:

- topic-bearing data-plane은 `zlink_publish` / `zlink_subscribe` /
  `zlink_unsubscribe` / `zlink_subscribe_recv` / `zlink_subscribe_handler`
  로 수렴한다.
- `XPUB`는 `zlink_subscription_event_recv` /
  `zlink_subscription_event_handler`로 분리된다.


## 5. subject/type별 허용 인터페이스

| subject/type | publish | subscribe control | subscribe recv/callback | subscription event |
| --- | --- | --- | --- | --- |
| raw `PUB` | `zlink_publish(topic=NULL)` | 미지원 | 미지원 | 미지원 |
| raw `SUB` | 미지원 | 지원 | 지원 | 미지원 |
| raw `XSUB` | 미지원 | 지원 | 지원 | 미지원 |
| raw `XPUB` | `zlink_publish(topic=NULL)` | 미지원 | 미지원 | 지원 |
| `spot` | `zlink_publish(topic!=NULL)` | 지원 | 지원 | 미지원 |
| `spot_node` | `zlink_publish(topic!=NULL)` | 지원 | 지원 | 미지원 |

오류 정책:

- 지원하지 않는 subject/type 조합: `ENOTSUP`
- null/invalid subject, null required pointer: `EFAULT`
- invalid topic/filter/pattern: `EINVAL`


## 6. 내부 구현 방향

public 표면 변경과 별개로 내부는 아래 3개 core로 재구성한다.

### 6.1 canonical multipart transport core

대상:

- `PAIR`
- `DEALER`
- `ROUTER`
- `STREAM`
- `gateway`

### 6.2 topic-bearing pub/sub core

대상:

- raw `SUB`
- raw `XSUB`
- `spot`
- `spot_node`

공통 shape:

- `source_rid + topic + multipart payload`

### 6.3 subscription event core

대상:

- raw `XPUB`

공통 shape:

- `source_rid + subscribed + topic`


## 7. 테스트 기준

### 7.1 parity

- raw `SUB` / `XSUB`와 `spot` / `spot_node`가 동일한
  `subscribe_recv` 계약을 따르는지
- `XPUB` direct recv와 callback이 동일한 `source_rid + subscribed + topic`
  계약을 따르는지

### 7.2 filter contract

- exact topic subscribe
- pattern subscribe (`*` suffix)
- invalid pattern (`*` 중복, 중간 `*`)
- trailing `*` exact topic 금지
- unsubscribe가 exact/pattern 모두 동일 규칙으로 동작하는지

### 7.3 error regression

- unsupported subject/type mismatch
- null/invalid pointer
- topic buffer truncation / `EMSGSIZE`
- `topic_id_out_` / `topic_id_len_out_` binary-safe contract


## 8. 결정 사항 요약

- pub/sub 계열은 `send/recv`가 아니라 `publish/subscribe` naming으로 재편한다.
- 일반 transport family의 canonical `send/recv`는 유지한다.
- `publish`는 단일 함수 `zlink_publish()`만 둔다.
- `subscribe`는 단일 함수 `zlink_subscribe()`만 둔다.
- `unsubscribe`도 단일 함수 `zlink_unsubscribe()`만 둔다.
- pattern 구분은 enum 없이 `*` suffix 유무로 판별한다.
- `XPUB`는 pub/sub umbrella 아래 포함되더라도 data-plane이 아니라
  subscription event-plane으로 유지한다.
- `unsubscribe()`는 `kind` 인자 없이 같은 문자열 규칙으로 exact/pattern을 판별한다.
