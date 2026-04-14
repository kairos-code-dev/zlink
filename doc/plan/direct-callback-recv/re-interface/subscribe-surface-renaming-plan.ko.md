# Subscribe Public C API Surface Renaming Plan

> 상태: 계획 초안.
> 선행 문서: 이 문서는
> [`pubsub-public-surface-renaming-plan.ko.md`](pubsub-public-surface-renaming-plan.ko.md)에서
> 확정한 subscribe 관련 naming을 후속 재편한다.
> 범위: `core/include/zlink.h` public C API 이름 변경을 다룬다.
> 내부 구현 구조 변경은 이 문서 범위가 아니다. 다만 public option 상수 제거에
> 필수적인 내부 상수 치환은 포함한다.
> 수정 범위: `core/`만 포함한다. bindings/외부 래퍼는 이번 범위에서 제외한다.

## 1. 목적

현재 subscribe 관련 public C API는 naming이 역할을 정확히 반영하지 못한다.

현재 문제:

- `zlink_subscribe()`는 구독 필터를 설정하는 제어 함수인데,
  이름만 보면 구독 데이터를 수신하는 함수처럼 보인다.
- 실제 구독 데이터 수신은 `zlink_subscribe_recv()`인데,
  "recv 하는 게 subscribe다"라는 semantic을 `_recv` suffix로만 구분한다.
- `zlink_subscription_event_recv()`는 이름이 길고,
  `_recv` suffix가 general transport `zlink_recv`와 혼동을 유발한다.
- 구독 필터 설정 경로가 `zlink_subscribe()` 전용 함수와
  `zlink_setsockopt(ZLINK_SUBSCRIBE, ...)` 두 갈래로 공존하여
  사용자가 어떤 것이 canonical인지 알기 어렵다.

이 문서는 subscribe 관련 public C API 이름을 역할 기반으로 재편하고,
`setsockopt` 기반 구독 설정 옵션을 public surface에서 제거하는 계획을 정리한다.

핵심 목표:

- 구독 필터 제어 함수를 `set_subscribe`/`unset_subscribe`로 명확히 구분한다.
- 구독 데이터 수신 함수가 `zlink_subscribe`라는 깨끗한 이름을 가져간다.
- XPUB subscription event 수신 함수를 `zlink_subscription_event`로 축약한다.
- `setsockopt`를 통한 구독 설정 경로를 public surface에서 제거한다.

비목표:

- 내부 구현 구조 변경은 이 문서 범위가 아니다.
- handler typedef 이름 변경은 이 문서 범위가 아니다.
- handler registration 함수 이름 변경은 이 문서 범위가 아니다.
- `ZLINK_ONLY_FIRST_SUBSCRIBE` 같은 xsub 전용 behavior 옵션은 이 문서에서 다루지 않는다.

## 2. 이름 변경 명세

### 2.1 함수 이름 변경

| 현재 이름 | 새 이름 | 역할 | 변경 이유 |
| --- | --- | --- | --- |
| `zlink_subscribe` | `zlink_set_subscribe` | 구독 필터 설정 | "설정한다"는 의미를 `set_` prefix로 명시 |
| `zlink_unsubscribe` | `zlink_unset_subscribe` | 구독 필터 해제 | `set_`의 역연산을 `unset_`으로 명시 |
| `zlink_subscribe_recv` | `zlink_subscribe` | 구독 데이터 수신 (direct recv) | 가장 자주 호출되는 subscribe 경로가 canonical 이름을 가져감 |
| `zlink_subscription_event_recv` | `zlink_subscription_event` | XPUB subscription event 수신 | `_recv` suffix 제거로 이름 축약 |

### 2.2 변경하지 않는 항목

| 이름 | 역할 | 유지 이유 |
| --- | --- | --- |
| `zlink_subscribe_handler_fn` | subscribe callback typedef | `zlink_subscribe`(새 이름)의 callback 버전과 자연스럽게 대응 |
| `zlink_subscribe_handler` | subscribe callback 등록 | handler 등록 이름 체계 유지 |
| `zlink_subscription_event_handler_fn` | XPUB event callback typedef | `zlink_subscription_event`(새 이름)의 callback 버전과 자연스럽게 대응 |
| `zlink_subscription_event_handler` | XPUB event callback 등록 | handler 등록 이름 체계 유지 |
| `zlink_publish` | topic-bearing publish | 변경 대상 아님 |

### 2.3 새 이름 관계도

변경 후 subscribe 관련 public C API family:

```
pub/sub data-plane pair:
  zlink_publish(subject, topic, parts, ...)   -- 송신
  zlink_subscribe(subject, ...)               -- 수신 (direct recv)
  zlink_subscribe_handler(subject, ...)       -- 수신 (callback 등록)

구독 필터 제어:
  zlink_set_subscribe(subject, filter)     -- 구독 설정
  zlink_unset_subscribe(subject, filter)   -- 구독 해제

XPUB subscription event (direct / callback):
  zlink_subscription_event(subject, ...)               -- direct recv
  zlink_subscription_event_handler(subject, ...)       -- callback 등록
```

## 3. setsockopt 기반 구독 옵션 제거

### 3.1 public header에서 제거할 옵션 상수

| 상수 | 값 | 제거 이유 |
| --- | --- | --- |
| `ZLINK_SOCKOPT_SUBSCRIBE` | `0x1103` | `zlink_set_subscribe()`가 canonical 경로 |
| `ZLINK_SOCKOPT_UNSUBSCRIBE` | `0x1104` | `zlink_unset_subscribe()`가 canonical 경로 |

### 3.2 internal build alias에서 제거할 항목

| alias | 값 | 비고 |
| --- | --- | --- |
| `ZLINK_SUBSCRIBE` (option alias) | `(zlink_socket_option_t) 6` | `#ifdef ZLINK_INTERNAL_BUILD` 블록 내 |
| `ZLINK_UNSUBSCRIBE` (option alias) | `(zlink_socket_option_t) 7` | `#ifdef ZLINK_INTERNAL_BUILD` 블록 내 |

### 3.3 external build alias에서 제거할 항목

| alias | 값 | 비고 |
| --- | --- | --- |
| `ZLINK_SUBSCRIBE` (option alias) | `ZLINK_SOCKOPT_SUBSCRIBE` | `#else` 블록 내 |
| `ZLINK_UNSUBSCRIBE` (option alias) | `ZLINK_SOCKOPT_UNSUBSCRIBE` | `#else` 블록 내 |

### 3.4 내부 구현 대응

`ZLINK_SUBSCRIBE`/`ZLINK_UNSUBSCRIBE` 옵션 상수를 public header에서 제거하면,
내부 구현(`sub.cpp`, `xpub.cpp`, `spot_sub.cpp`)에서 이 상수를
참조하는 부분은 내부 전용 상수로 치환해야 한다.

참고: `xsub.cpp`는 `ZLINK_SUBSCRIBE`/`ZLINK_UNSUBSCRIBE`를 코드에서
참조하지 않는다 (주석만 존재). `xsub.cpp`의 `xsetsockopt`는
`ZLINK_ONLY_FIRST_SUBSCRIBE`만 처리한다.

이 내부 치환은 "내부 구현 변경"이지만, public surface 제거의 필수 선행 작업이므로
이 문서 범위에 포함한다.

방향:

- `core/src/` 내부에서만 유효한 internal option 값을 유지한다.
- public `zlink_socket_option_t` enum에서는 제거한다.
- `zlink_setsockopt(socket, ZLINK_SUBSCRIBE, ...)` 호출은 공개 API에서 불가하게 된다.

## 4. 현재 → 목표 Header 시그니처 비교

### 4.1 현재 public header

```c
/* 구독 필터 제어 */
ZLINK_EXPORT int zlink_subscribe (void *subject_, const char *filter_);
ZLINK_EXPORT int zlink_unsubscribe (void *subject_, const char *filter_);

/* 구독 데이터 수신 */
ZLINK_EXPORT int zlink_subscribe_recv (void *subject_,
                                       zlink_routing_id_t *source_rid_out_,
                                       zlink_msg_t **parts_out_,
                                       size_t *part_count_out_,
                                       char *topic_id_out_,
                                       size_t *topic_id_len_out_,
                                       zlink_send_flags_t flags_);

/* XPUB subscription event 수신 */
ZLINK_EXPORT int zlink_subscription_event_recv (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_send_flags_t flags_);
```

### 4.2 목표 public header

```c
/* 구독 필터 제어 */
ZLINK_EXPORT int zlink_set_subscribe (void *subject_, const char *filter_);
ZLINK_EXPORT int zlink_unset_subscribe (void *subject_, const char *filter_);

/* 구독 데이터 수신 */
ZLINK_EXPORT int zlink_subscribe (void *subject_,
                                   zlink_routing_id_t *source_rid_out_,
                                   zlink_msg_t **parts_out_,
                                   size_t *part_count_out_,
                                   char *topic_id_out_,
                                   size_t *topic_id_len_out_,
                                   zlink_send_flags_t flags_);

/* XPUB subscription event 수신 */
ZLINK_EXPORT int zlink_subscription_event (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_send_flags_t flags_);
```

시그니처(파라미터 목록)는 변경하지 않는다. 이름만 변경한다.

## 5. subject/type별 적용 범위

### 5.1 `zlink_set_subscribe` / `zlink_unset_subscribe`

| subject/type | 지원 | 비고 |
| --- | --- | --- |
| raw `SUB` | 지원 | 기존 `zlink_subscribe` 동일 |
| raw `XSUB` | 지원 | 기존 `zlink_subscribe` 동일 |
| `spot` | 지원 | 기존 `zlink_subscribe` 동일 |
| `spot_node` | 지원 | 기존 `zlink_subscribe` 동일 |
| raw `PUB` | 미지원 | `ENOTSUP` |
| raw `XPUB` | 미지원 | `ENOTSUP` |
| 일반 transport | 미지원 | `ENOTSUP` |

### 5.2 `zlink_subscribe` (새 이름, 구독 데이터 수신)

| subject/type | 지원 | 비고 |
| --- | --- | --- |
| raw `SUB` | 지원 | 기존 `zlink_subscribe_recv` 동일 |
| raw `XSUB` | 지원 | 기존 `zlink_subscribe_recv` 동일 |
| `spot` | 지원 | 기존 `zlink_subscribe_recv` 동일 |
| `spot_node` | 지원 | 기존 `zlink_subscribe_recv` 동일 |

### 5.3 `zlink_subscription_event` (새 이름)

| subject/type | 지원 | 비고 |
| --- | --- | --- |
| raw `XPUB` | 지원 | 기존 `zlink_subscription_event_recv` 동일 |

## 6. 이름 변경의 설계 근거

### 6.1 `set_subscribe` / `unset_subscribe`

현재 `zlink_subscribe()`는 구독 필터를 "설정"하는 제어 함수다.
내부적으로는 `setsockopt(ZLINK_SUBSCRIBE, filter, len)`을 호출한다.

이 함수에 `set_` prefix를 붙이면:

- 구독 제어임이 이름에서 바로 드러난다.
- `setsockopt` 기반 구독 설정이 public에서 제거된 후에도
  "구독 설정 = `zlink_set_subscribe`"라는 유일한 canonical 경로가 남는다.
- `unset_subscribe`는 `set_subscribe`의 역연산으로 읽히므로
  pair 관계가 명확하다.

### 6.2 `zlink_subscribe` = 구독 데이터 수신

`zlink_subscribe_recv`에서 `_recv` suffix를 제거하고 `zlink_subscribe`로 승격한다.

근거:

- `zlink_publish` ↔ `zlink_subscribe`가 pub/sub pair를 이룬다.
  - publish는 topic-bearing 데이터를 보내는 함수다.
  - subscribe는 topic-bearing 데이터를 받는 함수다.
  - 이름만으로 pub/sub data-plane의 송신/수신 pair임이 바로 드러난다.
- canonical recv family 이름 체계에서 `_recv` suffix는 이미 제거 방향이다.
  - 기존: `zlink_recv` (일반 transport direct recv)
  - 기존: `zlink_subscription_event_recv` → 새: `zlink_subscription_event`
- handler 이름 `zlink_subscribe_handler`와 자연스럽게 pair를 이룬다:
  - direct recv: `zlink_subscribe()`
  - callback: `zlink_subscribe_handler()`

### 6.3 `zlink_subscription_event`

`_recv` suffix를 제거한다.

근거:

- `zlink_subscription_event` + `zlink_subscription_event_handler` pair가
  일반 transport의 `zlink_recv` + `zlink_recv_handler` pair와 같은 패턴이다.
- XPUB subscription event는 multipart payload가 아니라 metadata event이므로
  `_recv`보다 event 자체를 이름으로 삼는 것이 역할을 더 정확히 표현한다.

## 7. setsockopt 구독 옵션 제거의 설계 근거

현재 구독 필터를 설정하는 경로가 두 개다:

1. `zlink_subscribe(socket, filter)` (현재 이름) → 전용 함수
2. `zlink_setsockopt(socket, ZLINK_SUBSCRIBE, filter, len)` → 범용 옵션

POSD 관점에서 같은 행위에 대한 두 개의 public 경로는 설명 비용을 늘린다.

- 사용자는 "둘 중 어떤 것을 써야 하나"를 배워야 한다.
- 내부 구현은 두 경로를 모두 유지해야 한다.
- 전용 함수가 있으면 범용 옵션의 존재 이유가 없다.

따라서 `ZLINK_SOCKOPT_SUBSCRIBE` / `ZLINK_SOCKOPT_UNSUBSCRIBE`를
public enum에서 제거하고, `zlink_set_subscribe` / `zlink_unset_subscribe`를
유일한 canonical 경로로 남긴다.

## 8. 영향 범위

### 8.1 Public Header

- [`zlink.h`](core/include/zlink.h) — 함수 선언 이름 변경 4건, 옵션 상수 제거 2건, alias 제거 4건

### 8.2 Core API 구현

- [`zlink.cpp`](core/src/api/zlink.cpp) — 함수 정의 이름 변경 4건, 내부 subscribe option 상수 치환

### 8.3 Core Socket 구현

- [`sub.cpp`](core/src/sockets/sub.cpp) — 내부 `ZLINK_SUBSCRIBE`/`ZLINK_UNSUBSCRIBE` 상수 치환
- [`xpub.cpp`](core/src/sockets/xpub.cpp) — 내부 `ZLINK_SUBSCRIBE`/`ZLINK_UNSUBSCRIBE` 상수 치환
- [`xsub.cpp`](core/src/sockets/xsub.cpp) — 주석만 갱신 (코드 상수 참조 없음)

### 8.4 Core Service 구현

- [`spot_sub.cpp`](core/src/services/spot/spot_sub.cpp) — 내부 `setsockopt(ZLINK_SUBSCRIBE, ...)` 호출부 상수 치환

### 8.5 Tests

`zlink_subscribe()`/`zlink_unsubscribe()` 호출부 (구독 필터 설정):

- [`test_pubsub.cpp`](core/tests/integration/test_pubsub.cpp)
- [`test_xpub_verbose.cpp`](core/tests/integration/test_xpub_verbose.cpp)
- [`test_socket_with_handler.cpp`](core/tests/integration/test_socket_with_handler.cpp)
- [`test_asio_tcp.cpp`](core/tests/integration/test_asio_tcp.cpp)
- [`test_proxy.cpp`](core/tests/integration/test_proxy.cpp)
- [`test_disconnect_inproc.cpp`](core/tests/integration/test_disconnect_inproc.cpp)
- `core/tests/e2e/spot/` — 다수 spot 시나리오 파일

`zlink_setsockopt(ZLINK_SUBSCRIBE, ...)` 호출부 (setsockopt 경로):

- [`test_pubsub.cpp`](core/tests/integration/test_pubsub.cpp)
- [`test_xpub_verbose.cpp`](core/tests/integration/test_xpub_verbose.cpp)
- [`test_socket_with_handler.cpp`](core/tests/integration/test_socket_with_handler.cpp)
- [`test_asio_tcp.cpp`](core/tests/integration/test_asio_tcp.cpp)
- [`test_proxy.cpp`](core/tests/integration/test_proxy.cpp)
- 기타 integration/e2e 테스트

`zlink_subscribe_recv()` 호출부:

- [`testutil_unity.hpp`](core/tests/testutil_unity.hpp) — inline wrapper
- [`test_spot_service_introspection.cpp`](core/tests/e2e/spot/test_spot_service_introspection.cpp)
- [`unittest_service_mode_policy.cpp`](core/tests/unittest/unittest_service_mode_policy.cpp)

`zlink_subscription_event_recv()` 호출부:

- [`test_disconnect_inproc.cpp`](core/tests/integration/test_disconnect_inproc.cpp)
- [`test_socket_with_handler.cpp`](core/tests/integration/test_socket_with_handler.cpp)

### 8.6 Performance / Benchmark

`zlink_setsockopt(ZLINK_SUBSCRIBE, ...)` 호출부:

- [`perf_pubsub.cpp`](core/perf/single/src/perf_pubsub.cpp)
- [`perf_multi_client_helpers.hpp`](core/perf/multi/common/perf_multi_client_helpers.hpp)
- [`bench_common.hpp`](core/bench/with_zmq/single/common/bench_common.hpp) — compat shim
- [`bench_zlink_pubsub.cpp`](core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp)
- [`bench_multi_client.hpp`](core/bench/with_zmq/multi/common/bench_multi_client.hpp)

`zlink_subscribe_recv()` 호출부:

- [`perf_spot.cpp`](core/perf/single/src/perf_spot.cpp)
- [`perf_common.hpp`](core/perf/multi/common/perf_common.hpp) — inline wrapper
- [`perf_multi_spot_client.cpp`](core/perf/multi/src/perf_multi_spot_client.cpp)

### 8.7 Documentation

- [`socket.md`](doc/spec/core/socket/README.md) / [`socket.ko.md`](doc/spec/core/socket/README.ko.md)
- [`spot.md`](doc/spec/core/service/spot.md) / [`spot.ko.md`](doc/spec/core/service/spot.ko.md)
- [`README.md`](doc/spec/core/README.md) / [`README.ko.md`](doc/spec/core/README.ko.md)
- [`03-2-pubsub.md`](doc/guide/03-2-pubsub.md) / [`03-2-pubsub.ko.md`](doc/guide/03-2-pubsub.ko.md)
- [`02-core-api.md`](doc/guide/02-core-api.md) / [`02-core-api.ko.md`](doc/guide/02-core-api.ko.md)
- [`07-3-spot.md`](doc/guide/07-3-spot.md) / [`07-3-spot.ko.md`](doc/guide/07-3-spot.ko.md)
- [`11-thread-safety.md`](doc/guide/11-thread-safety.md) / [`11-thread-safety.ko.md`](doc/guide/11-thread-safety.ko.md)

## 9. 실행 단계

### Phase 1. public header + core 구현 이름 변경

목표:

- `zlink.h` 함수 선언 이름을 변경한다.
- 옵션 상수를 제거한다.
- `zlink.cpp` 함수 정의를 새 이름에 맞춘다.
- 내부 subscribe option 상수 참조를 내부 전용 값으로 치환한다.

원자성:

- public header에서 `ZLINK_SOCKOPT_SUBSCRIBE`를 제거하면 내부 구현(`sub.cpp`,
  `xpub.cpp` 등)이 이 상수를 참조하므로 빌드가 깨진다.
- 따라서 header 변경과 내부 상수 치환은 반드시 같은 커밋에서 함께 진행한다.

작업 — header:

- `zlink.h`에서 `zlink_subscribe` 선언을 `zlink_set_subscribe`로 변경
- `zlink.h`에서 `zlink_unsubscribe` 선언을 `zlink_unset_subscribe`로 변경
- `zlink.h`에서 `zlink_subscribe_recv` 선언을 `zlink_subscribe`로 변경
- `zlink.h`에서 `zlink_subscription_event_recv` 선언을 `zlink_subscription_event`로 변경
- `zlink_socket_option_t` enum에서 `ZLINK_SOCKOPT_SUBSCRIBE` 제거
- `zlink_socket_option_t` enum에서 `ZLINK_SOCKOPT_UNSUBSCRIBE` 제거
- `#ifdef ZLINK_INTERNAL_BUILD` 블록에서 `ZLINK_SUBSCRIBE`(option alias) 제거
- `#else` 블록에서 `ZLINK_SUBSCRIBE`(option alias) 제거
- `#ifdef ZLINK_INTERNAL_BUILD` 블록에서 `ZLINK_UNSUBSCRIBE`(option alias) 제거
- `#else` 블록에서 `ZLINK_UNSUBSCRIBE`(option alias) 제거

작업 — 구현:

- `zlink.cpp`에서 `zlink_subscribe()` 정의를 `zlink_set_subscribe()`로 변경
- `zlink.cpp`에서 `zlink_unsubscribe()` 정의를 `zlink_unset_subscribe()`로 변경
- `zlink.cpp`에서 `zlink_subscribe_recv()` 정의를 `zlink_subscribe()`로 변경
- `zlink.cpp`에서 `zlink_subscription_event_recv()` 정의를 `zlink_subscription_event()`로 변경
- `sub.cpp`, `xpub.cpp`, `spot_sub.cpp` 내부에서
  `ZLINK_SUBSCRIBE`/`ZLINK_UNSUBSCRIBE` 옵션 상수를
  internal-only 상수로 치환
- `xsub.cpp` 주석 갱신

완료 기준:

- 공개 헤더에 `zlink_subscribe_recv`, `zlink_subscription_event_recv` 선언이 없다.
- 공개 헤더에 `ZLINK_SOCKOPT_SUBSCRIBE`, `ZLINK_SOCKOPT_UNSUBSCRIBE`가 없다.
- 공개 헤더에 `zlink_subscribe`는 구독 데이터 수신 함수로 존재한다.
- `zlink_set_subscribe` / `zlink_unset_subscribe`가 구독 필터 제어 함수로 존재한다.
- `core/src/` 빌드가 새 이름으로 성공한다.
- 내부 구현이 public enum 값이 아닌 internal 상수를 참조한다.

### Phase 2. test/perf/bench 호출부 이관

목표:

- 모든 test/perf/bench 호출부를 새 이름으로 변환한다.
- `setsockopt(ZLINK_SUBSCRIBE, ...)` 호출부를 `zlink_set_subscribe()`로 변환한다.

작업:

- `zlink_subscribe(subject, filter)` 호출 → `zlink_set_subscribe(subject, filter)`
- `zlink_unsubscribe(subject, filter)` 호출 → `zlink_unset_subscribe(subject, filter)`
- `zlink_subscribe_recv(...)` 호출 → `zlink_subscribe(...)`
- `zlink_subscription_event_recv(...)` 호출 → `zlink_subscription_event(...)`
- `zlink_setsockopt(socket, ZLINK_SUBSCRIBE, filter, len)` 호출 →
  `zlink_set_subscribe(socket, filter)` 로 변환
  (주의: `setsockopt`는 raw `void *` + `size_t`이고,
   `zlink_set_subscribe`는 `const char *filter_`이므로
   호출부마다 filter 문자열 생성이 필요할 수 있다)
- `zlink_setsockopt(socket, ZLINK_UNSUBSCRIBE, filter, len)` 호출 →
  `zlink_unset_subscribe(socket, filter)` 로 변환
- `testutil_unity.hpp`의 inline wrapper 이름 갱신
- `perf_common.hpp`의 inline wrapper 이름 갱신

완료 기준:

- `core/` 전체 빌드 성공
- `ctest --test-dir core/build --output-on-failure` 전체 통과

### Phase 3. 문서 갱신

목표:

- API 문서와 가이드의 함수 이름을 새 이름으로 갱신한다.

작업:

- API reference 문서에서 함수 이름 치환
- 가이드 예제 코드에서 함수 이름 치환
- `setsockopt(ZLINK_SUBSCRIBE, ...)` 기반 예제를
  `zlink_set_subscribe()` 기반으로 변환
- 한국어/영어 문서 모두 동일 적용

완료 기준:

- 문서에 구 이름(`zlink_subscribe_recv`, `zlink_subscription_event_recv`)이 남지 않는다.
- 문서에 `setsockopt(ZLINK_SUBSCRIBE)` 기반 예제가 남지 않는다.

## 10. setsockopt → set_subscribe 호출 변환 상세

### 10.1 변환 패턴

```c
/* 현재: setsockopt 경로 */
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "topic_a", 7);
zlink_setsockopt(sub, ZLINK_UNSUBSCRIBE, "topic_a", 7);

/* 변환 후: 전용 함수 경로 */
zlink_set_subscribe(sub, "");
zlink_set_subscribe(sub, "topic_a");
zlink_unset_subscribe(sub, "topic_a");
```

### 10.2 empty filter 변환

```c
/* 현재: all-topic subscribe */
zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);

/* 변환: empty string = subscribe all */
zlink_set_subscribe(sub, "");
```

### 10.3 bench compat shim 처리

[`bench_common.hpp`](core/bench/with_zmq/single/common/bench_common.hpp)에
libzmq 호환 `#define ZLINK_SUBSCRIBE ZMQ_SUBSCRIBE` shim이 있다.

이 shim은 `with_zmq` bench에서 libzmq와 zlink를 같은 코드로 빌드하기 위한 것이다.
zlink 측 bench 코드는 `zlink_set_subscribe()`로 변환하고,
libzmq 측 bench 코드는 기존 `ZMQ_SUBSCRIBE` + `zmq_setsockopt` 경로를 유지한다.

## 11. 세부 체크리스트

### 11.1 공개 header 체크리스트

- [ ] `zlink.h`에서 `zlink_subscribe` 선언을 `zlink_set_subscribe`로 변경
- [ ] `zlink.h`에서 `zlink_unsubscribe` 선언을 `zlink_unset_subscribe`로 변경
- [ ] `zlink.h`에서 `zlink_subscribe_recv` 선언을 `zlink_subscribe`로 변경
- [ ] `zlink.h`에서 `zlink_subscription_event_recv` 선언을 `zlink_subscription_event`로 변경
- [ ] `zlink_socket_option_t` enum에서 `ZLINK_SOCKOPT_SUBSCRIBE` 제거
- [ ] `zlink_socket_option_t` enum에서 `ZLINK_SOCKOPT_UNSUBSCRIBE` 제거
- [ ] `ZLINK_INTERNAL_BUILD` 블록에서 `ZLINK_SUBSCRIBE` option alias 제거
- [ ] `ZLINK_INTERNAL_BUILD` 블록에서 `ZLINK_UNSUBSCRIBE` option alias 제거
- [ ] `#else` 블록에서 `ZLINK_SUBSCRIBE` option alias 제거
- [ ] `#else` 블록에서 `ZLINK_UNSUBSCRIBE` option alias 제거

### 11.2 구현 체크리스트

- [ ] `zlink.cpp`에서 4개 함수 정의 이름 변경
- [ ] `sub.cpp`에서 `ZLINK_SUBSCRIBE`/`ZLINK_UNSUBSCRIBE` 내부 상수 치환
- [ ] `xpub.cpp`에서 `ZLINK_SUBSCRIBE`/`ZLINK_UNSUBSCRIBE` 내부 상수 치환
- [ ] `xsub.cpp` 주석 갱신
- [ ] `spot_sub.cpp`에서 `setsockopt(ZLINK_SUBSCRIBE, ...)` 호출 내부 상수 치환

### 11.3 호출부 체크리스트

- [ ] core/tests에서 `zlink_subscribe(s, filter)` → `zlink_set_subscribe(s, filter)` 치환
- [ ] core/tests에서 `zlink_unsubscribe(s, filter)` → `zlink_unset_subscribe(s, filter)` 치환
- [ ] core/tests에서 `zlink_subscribe_recv(...)` → `zlink_subscribe(...)` 치환
- [ ] core/tests에서 `zlink_subscription_event_recv(...)` → `zlink_subscription_event(...)` 치환
- [ ] core/tests에서 `zlink_setsockopt(s, ZLINK_SUBSCRIBE, ...)` → `zlink_set_subscribe(s, ...)` 변환
- [ ] core/tests에서 `zlink_setsockopt(s, ZLINK_UNSUBSCRIBE, ...)` → `zlink_unset_subscribe(s, ...)` 변환
- [ ] core/perf에서 동일 치환
- [ ] core/bench에서 zlink 측 동일 치환
- [ ] `testutil_unity.hpp` inline wrapper 갱신
- [ ] `perf_common.hpp` inline wrapper 갱신

### 11.4 문서 체크리스트

- [ ] API reference (영문/한국어) 함수 이름 갱신
- [ ] Guide (영문/한국어) 예제 코드 갱신
- [ ] setsockopt 기반 subscribe 예제 제거

## 12. 검증 계획

### 12.1 Build

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build -j$(nproc)
```

### 12.2 Test Lanes

```bash
./core/tests/run_test_lanes.sh --include-e2e
```

### 12.3 Focused Tests

- `test_pubsub` — raw SUB subscribe/recv 경로
- `test_xpub_verbose` — XPUB subscription event 경로
- `test_socket_with_handler` — handler callback 경로
- `test_disconnect_inproc` — inproc subscribe 경로
- `test_proxy` — proxy subscribe 경로
- `unittest_service_mode_policy` — subscribe handler/recv mode 정책
- `test_spot_service_introspection` — spot subscribe_recv 경로
- spot e2e 시나리오 — spot/spot_node subscribe/unsubscribe 전체 흐름

### 12.4 Symbol Verification

```bash
nm -D core/build/lib/libzlink.so | grep -w -E 'zlink_set_subscribe|zlink_unset_subscribe|zlink_subscribe$|zlink_subscription_event$|zlink_subscribe_recv|zlink_unsubscribe$|zlink_subscription_event_recv'
```

`-w` (word match) 또는 `$` anchor를 사용하여 substring 매칭을 방지한다.
예를 들어 `zlink_unsubscribe`가 `zlink_unset_subscribe`에 매칭되지 않게 한다.

기대 결과:

- `zlink_set_subscribe` 존재
- `zlink_unset_subscribe` 존재
- `zlink_subscribe` 존재 (새 시그니처 — 구독 데이터 수신)
- `zlink_subscription_event` 존재
- `zlink_subscribe_recv` 부재
- `zlink_unsubscribe` 부재
- `zlink_subscription_event_recv` 부재

## 13. 완료 정의

다음 조건을 모두 만족하면 이 문서 범위의 작업은 완료다.

- 공개 `zlink.h`에 `zlink_subscribe_recv` 선언이 없다.
- 공개 `zlink.h`에 `zlink_unsubscribe` 선언이 없다.
- 공개 `zlink.h`에 `zlink_subscription_event_recv` 선언이 없다.
- 공개 `zlink.h`에 `ZLINK_SOCKOPT_SUBSCRIBE` / `ZLINK_SOCKOPT_UNSUBSCRIBE`가 없다.
- `zlink_set_subscribe` / `zlink_unset_subscribe`가 구독 필터 제어의 유일한 public 경로다.
- `zlink_subscribe`가 구독 데이터 수신 함수로 존재한다.
- `zlink_subscription_event`가 XPUB event 수신 함수로 존재한다.
- handler typedef/registration 이름은 변경 없이 유지된다.
- `core/` 전체 빌드와 테스트가 통과한다.
- 문서가 새 이름과 일치한다.
- `core/` 외부(bindings)는 이번 범위에서 변경하지 않는다.

## 14. 실행 참조: 변경 대상 정확한 위치

이 섹션은 새 컨텍스트에서 작업을 시작할 때 코드를 탐색하지 않고도
바로 변경할 수 있도록 모든 변경 대상의 정확한 파일과 라인 번호를 기록한다.

### 14.1 core/include/zlink.h — header 변경

함수 선언 이름 변경:

| 라인 | 현재 | 변경 후 |
| --- | --- | --- |
| 815 | `zlink_subscribe (void *subject_, const char *filter_)` | `zlink_set_subscribe` |
| 816 | `zlink_unsubscribe (void *subject_, const char *filter_)` | `zlink_unset_subscribe` |
| 818 | `zlink_subscribe_recv (void *subject_, ...)` | `zlink_subscribe` |
| 826 | `zlink_subscription_event_recv (...)` | `zlink_subscription_event` |

옵션 상수 제거:

| 라인 | 제거 대상 |
| --- | --- |
| 355 | `ZLINK_SOCKOPT_SUBSCRIBE = 0x1103,` (enum 멤버) |
| 356 | `ZLINK_SOCKOPT_UNSUBSCRIBE = 0x1104,` (enum 멤버) |
| 430 | `#define ZLINK_SUBSCRIBE ((zlink_socket_option_t) 6)` (`ZLINK_INTERNAL_BUILD` 블록) |
| 431 | `#define ZLINK_UNSUBSCRIBE ((zlink_socket_option_t) 7)` (`ZLINK_INTERNAL_BUILD` 블록) |
| 499 | `#define ZLINK_SUBSCRIBE ZLINK_SOCKOPT_SUBSCRIBE` (`#else` 블록) |
| 500 | `#define ZLINK_UNSUBSCRIBE ZLINK_SOCKOPT_UNSUBSCRIBE` (`#else` 블록) |

변경하지 않는 항목 (확인용):

| 라인 | 이름 | 유지 |
| --- | --- | --- |
| 662-668 | `zlink_subscribe_handler_fn` typedef | 유지 |
| 670-675 | `zlink_subscription_event_handler_fn` typedef | 유지 |
| 697-698 | `zlink_subscribe_handler` 선언 | 유지 |
| 700-701 | `zlink_subscription_event_handler` 선언 | 유지 |

### 14.2 core/src/api/zlink.cpp — 함수 정의 이름 변경

| 라인 | 현재 | 변경 후 |
| --- | --- | --- |
| 5473 | `int zlink_subscribe (void *subject_, const char *filter_)` | `int zlink_set_subscribe` |
| 5505 | `int zlink_unsubscribe (void *subject_, const char *filter_)` | `int zlink_unset_subscribe` |
| 5604 | `int zlink_subscribe_recv (void *subject_, ...)` | `int zlink_subscribe` |
| 5636 | `int zlink_subscription_event_recv (void *subject_, ...)` | `int zlink_subscription_event` |

내부 옵션 상수 참조 (internal 상수로 치환):

| 라인 | 현재 코드 |
| --- | --- |
| 5502 | `subscribe_socket_filter (handle, ZLINK_SUBSCRIBE, filter_)` |
| 5530 | `subscribe_socket_filter (handle, ZLINK_UNSUBSCRIBE, filter_)` |

### 14.3 core/src/sockets/ — 내부 옵션 상수 치환

**sub.cpp:**

| 라인 | 현재 코드 |
| --- | --- |
| 25 | `if (option_ != ZLINK_SUBSCRIBE && option_ != ZLINK_UNSUBSCRIBE)` |
| 34 | `if (option_ == ZLINK_SUBSCRIBE)` |

**xpub.cpp:**

| 라인 | 현재 코드 |
| --- | --- |
| 231 | `else if (option_ == ZLINK_SUBSCRIBE && _manual)` |
| 238 | `else if (option_ == ZLINK_UNSUBSCRIBE && _manual)` |

**xsub.cpp:**

| 라인 | 내용 |
| --- | --- |
| 573 | 주석만: `//  zlink_setsockopt(ZLINK_SUBSCRIBE, ...), which also drops subscriptions` |

코드 참조 없음. 주석만 갱신.

### 14.4 core/src/services/spot/spot_sub.cpp — 내부 setsockopt 호출

| 라인 | 현재 코드 |
| --- | --- |
| 355 | `socket->setsockopt (ZLINK_SUBSCRIBE, topic.data (), topic.size ())` |
| 399 | `socket->setsockopt (ZLINK_SUBSCRIBE, prefix.data (), prefix.size ())` |
| 454 | `socket->setsockopt (ZLINK_UNSUBSCRIBE, filter.data (), filter.size ())` |
| 1524 | `socket->setsockopt (ZLINK_UNSUBSCRIBE, it->c_str (), ...)` |
| 1529 | `socket->setsockopt (ZLINK_UNSUBSCRIBE, it->c_str (), ...)` |

### 14.5 internal 상수 전략

public `ZLINK_SOCKOPT_SUBSCRIBE` / `ZLINK_SOCKOPT_UNSUBSCRIBE`를 enum에서
제거한 뒤, 내부 코드가 참조할 상수가 필요하다.

방법: `ZLINK_INTERNAL_BUILD` 블록에 internal-only define을 유지한다.

```c
/* internal-only subscribe option values — not part of public API */
#define ZLINK_INTERNAL_OPT_SUBSCRIBE   ((zlink_socket_option_t) 6)
#define ZLINK_INTERNAL_OPT_UNSUBSCRIBE ((zlink_socket_option_t) 7)
```

이 define은 기존 `ZLINK_SUBSCRIBE` / `ZLINK_UNSUBSCRIBE`의 값(6, 7)을
그대로 사용한다. wire protocol이나 내부 option dispatch 로직에 영향이 없다.

치환 규칙:

- `sub.cpp`, `xpub.cpp` 내부: `ZLINK_SUBSCRIBE` → `ZLINK_INTERNAL_OPT_SUBSCRIBE`
- `sub.cpp`, `xpub.cpp` 내부: `ZLINK_UNSUBSCRIBE` → `ZLINK_INTERNAL_OPT_UNSUBSCRIBE`
- `spot_sub.cpp` 내부: `ZLINK_SUBSCRIBE` → `ZLINK_INTERNAL_OPT_SUBSCRIBE`
- `spot_sub.cpp` 내부: `ZLINK_UNSUBSCRIBE` → `ZLINK_INTERNAL_OPT_UNSUBSCRIBE`
- `zlink.cpp` 내부: `ZLINK_SUBSCRIBE` → `ZLINK_INTERNAL_OPT_SUBSCRIBE`
- `zlink.cpp` 내부: `ZLINK_UNSUBSCRIBE` → `ZLINK_INTERNAL_OPT_UNSUBSCRIBE`

### 14.6 core/tests/ — 호출부 변경 목록

#### 14.6.1 `zlink_setsockopt(s, ZLINK_SUBSCRIBE, ...)` → `zlink_set_subscribe(s, ...)`

| 파일 | 라인 |
| --- | --- |
| `test_pubsub.cpp` | 23 |
| `test_xpub_verbose.cpp` | 25, 31, 37, 47, 80, 87, 101, 112, 144, 150, 156, 163, 168, 178, 188, 200, 207, 226, 233, 247, 251, 267, 274, 281 |
| `test_socket_with_handler.cpp` | 606, 648, 728, 782, 814, 854 |
| `test_asio_tcp.cpp` | 156 |
| `test_proxy.cpp` | 67, 232 |
| `test_disconnect_inproc.cpp` | 24 |
| `test_connect_null_fuzzer.cpp` | 24 |
| `test_bind_null_fuzzer.cpp` | 27 |
| `test_pub_invert_matching.cpp` | 29, 31 |
| `test_xpub_welcome_msg.cpp` | 22 |
| `test_pubsub_inproc_alternating.cpp` | 57 |
| `test_sub_forward.cpp` | 29 |
| `test_asio_ws.cpp` | 609 |
| `test_asio_poller.cpp` | 117 |
| `test_pubsub_filter_xpub.cpp` | 57 |
| `test_xpub_topic.cpp` | 34, 48 |
| `test_xpub_manual.cpp` | 29, 71, 77, 87, 108, 144, 153, 160, 168, 208, 214, 271, 280, 286, 294, 360, 383, 395 |
| `test_hwm_pubsub.cpp` | 49, 123, 198 |
| `test_xpub_nodrop.cpp` | 27 |
| `test_transport_matrix.cpp` | 159 |
| `monitoring/test_monitor_enhanced.cpp` | 196 |
| `pgm/test_pgm_smoke.cpp` | 95 |

변환 예시:

```c
/* 현재 */
zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "", 0);
/* 변환 */
zlink_set_subscribe (sub, "");
```

#### 14.6.2 `zlink_subscribe(s, filter)` → `zlink_set_subscribe(s, filter)`

| 파일 | 라인 |
| --- | --- |
| `test_socket_with_handler.cpp` | 681, 723, 725 |
| `monitoring/test_monitor_service_contract.cpp` | 1142 |
| `e2e/spot/spot_pubsub_scenario_callback_cases.cpp` | 142, 213, 229, 283, 391, 407 |
| `e2e/spot/spot_pubsub_scenario_node_cases.cpp` | 44, 45, 46, 47, 64, 66, 90, 92, 166, 167, 235 |
| `e2e/spot/spot_pubsub_scenario_peer_cases.cpp` | 196, 273 |
| `e2e/spot/spot_pubsub_scenario_discovery_cases.cpp` | 72, 100 |
| `e2e/spot/test_spot_service_introspection.cpp` | 1065, 1270, 1302, 1383, 1384, 1425, 1426, 1627, 1845, 1905, 1964, 2017, 2085, 2144, 2198, 2277, 2390, 2434, 2547, 2552 |

#### 14.6.3 `zlink_unsubscribe(s, filter)` → `zlink_unset_subscribe(s, filter)`

| 파일 | 라인 |
| --- | --- |
| `e2e/spot/spot_pubsub_scenario_callback_cases.cpp` | 310, 430, 431 |
| `e2e/spot/spot_pubsub_scenario_node_cases.cpp` | 78, 80, 103, 105, 107, 108, 109, 111 |
| `e2e/spot/spot_pubsub_scenario_shared.cpp` | 943 |
| `e2e/spot/spot_pubsub_scenario_peer_cases.cpp` | 244 |
| `e2e/spot/spot_pubsub_scenario_discovery_cases.cpp` | 115, 117 |
| `e2e/spot/test_spot_service_introspection.cpp` | 1142 |

#### 14.6.4 `zlink_subscribe_recv(...)` → `zlink_subscribe(...)`

| 파일 | 라인 |
| --- | --- |
| `testutil_unity.hpp` | 134, 141 (inline wrapper — 함수 이름과 내부 호출 모두) |
| `unittest_service_mode_policy.cpp` | 142, 188 |
| `test_disconnect_inproc.cpp` | 58 |
| `test_socket_with_handler.cpp` | 697 |
| `e2e/spot/test_spot_service_introspection.cpp` | 878, 920, 1930, 2043, 2111, 2224 |

#### 14.6.5 `zlink_subscription_event_recv(...)` → `zlink_subscription_event(...)`

| 파일 | 라인 |
| --- | --- |
| `test_disconnect_inproc.cpp` | 40 |
| `test_socket_with_handler.cpp` | 823, 863 |

### 14.7 core/perf/ — 호출부 변경 목록

| 파일 | 라인 | 변경 내용 |
| --- | --- | --- |
| `perf_common.hpp` | 448, 455 | inline wrapper `zlink_subscribe_recv` → `zlink_subscribe` |
| `perf_pubsub.cpp` | 353 | `zlink_setsockopt(sub, ZLINK_SUBSCRIBE, ...)` → `zlink_set_subscribe(sub, ...)` |
| `perf_spot.cpp` | 485 | `zlink_subscribe_recv(...)` → `zlink_subscribe(...)` |
| `perf_spot.cpp` | 854 | `zlink_subscribe(sub, k_topic)` → `zlink_set_subscribe(sub, k_topic)` |
| `perf_multi_spot_client.cpp` | 560 | `zlink_subscribe_recv(...)` → `zlink_subscribe(...)` |
| `perf_multi_spot_client.cpp` | 690 | `zlink_subscribe(slot->sub, k_topic)` → `zlink_set_subscribe(slot->sub, k_topic)` |

### 14.8 core/bench/ — 호출부 변경 목록

| 파일 | 라인 | 변경 내용 |
| --- | --- | --- |
| `bench_zlink_pubsub.cpp` | 333 | `zlink_setsockopt(sub, ZLINK_SUBSCRIBE, ...)` → `zlink_set_subscribe(sub, ...)` |
| `bench_multi_client.hpp` | 550 | `zlink_setsockopt(sock, ZLINK_SUBSCRIBE, ...)` → `zlink_set_subscribe(sock, ...)` |
| `bench_zlink_multi_pubsub.cpp` | 297, 436 | `zlink_setsockopt(sub, ZLINK_SUBSCRIBE, ...)` → `zlink_set_subscribe(sub, ...)` |

libzmq 측 bench (변경하지 않음):

| 파일 | 라인 | 비고 |
| --- | --- | --- |
| `bench_zmq_pubsub.cpp` | 293 | `zmq_setsockopt(sub, ZMQ_SUBSCRIBE, ...)` — libzmq API, 변경 대상 아님 |

bench compat shim:

| 파일 | 라인 | 현재 | 처리 |
| --- | --- | --- | --- |
| `bench_common.hpp` | (ZLINK_SUBSCRIBE define) | `#define ZLINK_SUBSCRIBE ZMQ_SUBSCRIBE` fallback | zlink 측 bench가 `zlink_set_subscribe`로 변환되면 이 shim은 불필요 — 제거 가능 |

### 14.9 doc/ — 문서 변경 목록

다음 문서에서 `zlink_subscribe(`, `zlink_unsubscribe(`, `zlink_subscribe_recv`,
`zlink_subscription_event_recv`, `setsockopt(ZLINK_SUBSCRIBE` 등의 이름을 새 이름으로 갱신:

| 파일 |
| --- |
| `doc/spec/core/socket/README.md` |
| `doc/spec/core/socket/README.ko.md` |
| `doc/spec/core/service/spot.md` |
| `doc/spec/core/service/spot.ko.md` |
| `doc/guide/03-2-pubsub.md` |
| `doc/guide/03-2-pubsub.ko.md` |
| `doc/guide/07-3-spot.md` |
| `doc/guide/07-3-spot.ko.md` |
| `doc/guide/11-thread-safety.md` |
| `doc/guide/11-thread-safety.ko.md` |

선행 계획 문서 (`pubsub-public-surface-renaming-plan.ko.md`)는 이 문서가
후속 재편임을 상단에 기술했으므로, 선행 문서 본문은 수정하지 않는다.

### 14.10 변경하지 않는 호출부 (확인용)

`zlink_subscribe_handler()` 호출은 이름 변경 대상이 아니다.
다음 파일에서 사용 중이며, 그대로 유지한다:

| 파일 | 라인 |
| --- | --- |
| `zlink.cpp` (정의) | 2383 |
| `testutil_unity.cpp` | 55 |
| `unittest_service_mode_policy.cpp` | 129, 135, 175, 181 |
| `test_socket_with_handler.cpp` | 337, 343, 597, 642, 774 |
| `test_monitor_with_handler.cpp` | 572, 574 |
| `test_thread_safe_scaling_contract.cpp` | 492, 494, 501, 503 |
| `monitoring/test_monitor_service_contract.cpp` | 1104, 1106, 1113, 1115, 1231, 1236, 1259, 1264 |
| `e2e/spot/spot_pubsub_scenario_peer_cases.cpp` | 81, 83 |
| `e2e/spot/spot_pubsub_scenario_shared.cpp` | 98, 152 |
| `e2e/spot/test_spot_service_introspection.cpp` | 354, 376, 1893, 2069 |
| `perf_pubsub.cpp` | 344 |
| `perf_spot.cpp` | 790 |
| `perf_multi_spot_client.cpp` | 681 |
| `perf_multi_spot_server.cpp` | 644 |

## 15. 현재 판단 요약

- 이번 작업의 본질은 public C API 이름 변경이다.
- 내부 구현 구조 변경은 포함하지 않는다.
- `setsockopt` 기반 구독 설정 옵션 제거는 public surface 단순화의 일환이다.
- 이름 변경 이후 subscribe 관련 public C API는
  "제어는 `set_subscribe`/`unset_subscribe`, 수신은 `subscribe`/`subscribe_handler`"로
  한 문장으로 설명 가능해진다.
- XPUB event 축은 `subscription_event`/`subscription_event_handler`로
  subscribe 축과 분리 유지된다.
