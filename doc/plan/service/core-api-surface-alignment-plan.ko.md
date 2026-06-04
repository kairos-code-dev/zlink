# Core API Surface 정리 계획

## 1. 목적

이 문서는 core 공개 API 표면에서 다음 두 가지 정리 작업을 실제 구현으로 옮기기
위한 계획이다.

- Spot Actor create / join 계열 payload를 단일 message에서 multipart payload로 확장한다.
- Registry의 scalar 설정 함수를 registry option API로 정리한다.

두 작업은 서로 다른 기능을 다루지만 문제는 같다. 공개 API가 너무 이른 시점에 좁은
형태로 고정되면 이후 확장 때 새 함수와 wrapper가 늘어난다. 이번 정리는 사용자에게
보이는 계약을 더 일관된 형태로 만들고, 내부 구현 세부 사항은 core 안에 숨기는 것을
목표로 한다.

이 문서는 구현 전 실행 계획이다. 정식 공개 계약은 구현과 테스트가 끝난 뒤
`core/include/zlink.h`, `doc/spec/core/service/*.ko.md`, errno 문서, binding 문서에
나누어 반영한다.

## 2. 구현 전 리뷰 결과

이 절은 구현자가 작업 중 다시 판단하지 않도록 현재 문서 리뷰에서 확정한 결정을
정리한다.

1. Actor create / join C API는 기존 함수 이름을 유지하고 multipart 시그니처로
   바꾼다. C ABI와 source를 모두 바꾸는 작업이므로 core version은 `6.0.0`으로
   올린다. `*_multipart` 별도 symbol은 만들지 않는다.
2. Registry scalar setter는 제거하지 않는다. `zlink_registry_set_id()`,
   `zlink_registry_set_heartbeat()`, `zlink_registry_set_broadcast_interval()`은
   compatibility wrapper로 유지한다. 새 문서와 binding public surface는
   `zlink_registry_set()` / `zlink_registry_get()`을 canonical API로 설명한다.
3. `zlink_registry_add_peer()`는 option API로 옮기지 않는다. peer endpoint 목록을
   바꾸는 command라서 named API를 유지한다.
4. 이 계획의 필수 배포 gate는 local `core/build` 산출물을 언어별 native 경로로
   동기화하는 경로다. 기존 `bindings/update_zlink_libs.sh`는 GitHub release asset
   동기화 전용으로 유지하고, 이 작업에서는 local build 동기화 스크립트를 새로
   만든다.
5. core 구현을 끝낸 뒤 binding 코드를 수정하기 전에 `doc/spec/bindings/` 문서를 먼저
   갱신한다. binding 코드는 이 문서를 기준으로 언어별 typed surface를 구현한다.
6. Codex 에이전트는 이 문서의 단계를 순서대로 수행하고, 실패한 gate는 직접 수정한 뒤
   같은 gate를 다시 실행한다. 사용자에게 설계 선택을 되묻지 않는다.

## 3. 범위

### 3.1 포함 범위

- `zlink_spot_node_create_remote_actor()` payload 인자 변경
- `zlink_actor_admission_handler_fn` payload 인자 변경
- `zlink_spot_node_actor_join_spot()` payload 인자 변경
- `zlink_spot_actor_join_recv()` payload 출력 변경
- `zlink_spot_actor_join_reply()` payload 인자 변경
- `zlink_registry_option_t` 추가
- `zlink_registry_set()` / `zlink_registry_get()` 추가
- registry scalar 설정의 정식 spec, guide, errno 문서 정리
- `doc/internals/`, `doc/spec/`, `doc/guide/`, `doc/spec/bindings/` 반영 순서 정리
- core library를 언어별 binding native 디렉토리에 배포하는 절차 정리
- core 배포 뒤 C/C++/.NET/Go/Java/Node/Python/Rust binding 수정 계획
- C core 테스트 보강
- local core build를 binding native 경로로 동기화하는 자동화 추가

### 3.2 제외 범위

- Actor queue의 part-by-part relay 계약 변경
- raw socket multipart 원자성 모델 변경
- registry peer 목록 API의 option화
- discovery client option surface 정리
- GitHub release tag 생성과 외부 release asset publish

binding 문서와 binding 코드는 core 공개 계약이 안정화된 뒤 이 계획 안에서 갱신한다.

## 4. 기준 원칙

### 4.1 Multipart는 aggregate payload로 다룬다

Actor create / join 요청은 하나의 논리 요청이다. 그래서 public API는 여러 part를
한 번에 넘기고 받는 aggregate multipart 모양을 쓴다.

part-by-part streaming API로 열지 않는다. streaming 형태는 호출자가 중간 상태,
재시도, 실패 복구를 직접 알아야 하므로 create / join 요청 표면에는 맞지 않는다.

### 4.2 payload ownership은 기존 send / recv 계열과 맞춘다

send 계열 API가 성공하면 `parts` 전체의 소유권은 라이브러리로 넘어간다. validation
실패나 submit 전 실패에서는 호출자에게 남는다.

recv 계열 API가 성공하면 `parts_out`과 `part_count_out`이 가리키는 payload 소유권은
호출자에게 넘어간다. 호출자는 `zlink_multipart_close()`로 닫거나 각 part를 정확히
한 번 소비해야 한다.

### 4.3 Registry scalar 설정은 option으로 모은다

registry의 단일 정수 설정은 `zlink_registry_option_t`와 `zlink_registry_set()` /
`zlink_registry_get()`으로 다룬다.

`zlink_registry_add_peer()`는 option으로 옮기지 않는다. 이 함수는 단일 값을 바꾸는
설정이 아니라 peer 목록에 항목을 추가하는 command이므로 named API가 더 명확하다.

## 5. Public API 변경안

이 절은 구현자가 헤더 변경 범위를 놓치지 않도록 public C API, typedef, enum, option
값을 명시한다. Actor create / join 계열은 기존 symbol의 시그니처를 바꾸고 registry
named setter symbol은 compatibility wrapper로 유지한다.

### 5.1 변경되는 public C API 목록

| 구분 | 항목 | 변경 전 | 변경 후 | 처리 |
|------|------|---------|---------|------|
| 함수 | `zlink_spot_node_create_remote_actor()` | one-part payload pointer | `zlink_msg_t *parts, size_t part_count` | 시그니처 변경 |
| typedef | `zlink_actor_admission_handler_fn` | one-part borrowed payload pointer | `const zlink_msg_t *parts, size_t part_count` | 시그니처 변경 |
| 함수 | `zlink_spot_node_actor_join_spot()` | one-part payload pointer | `zlink_msg_t *parts, size_t part_count` | 시그니처 변경 |
| 함수 | `zlink_spot_actor_join_recv()` | one-part output pointer | `zlink_msg_t **parts_out, size_t *part_count_out` | 시그니처 변경 |
| 함수 | `zlink_spot_actor_join_reply()` | one-part payload pointer | `zlink_msg_t *parts, size_t part_count` | 시그니처 변경 |
| enum type | `zlink_registry_option_t` | 없음 | 새 enum type | 추가 |
| enum value | `ZLINK_REGISTRY_OPT_ID` | 없음 | registry id scalar option | 추가 |
| enum value | `ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS` | 없음 | heartbeat interval option | 추가 |
| enum value | `ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS` | 없음 | heartbeat timeout option | 추가 |
| enum value | `ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS` | 없음 | service list broadcast interval option | 추가 |
| 함수 | `zlink_registry_set()` | 없음 | registry scalar option setter | 추가 |
| 함수 | `zlink_registry_get()` | 없음 | registry scalar option getter | 추가 |
| 함수 | `zlink_registry_set_id()` | named scalar setter | `zlink_registry_set()` wrapper | compatibility 유지 |
| 함수 | `zlink_registry_set_heartbeat()` | named scalar setter | 두 option을 같은 lock 안에서 설정하는 wrapper | compatibility 유지 |
| 함수 | `zlink_registry_set_broadcast_interval()` | named scalar setter | `zlink_registry_set()` wrapper | compatibility 유지 |
| 함수 | `zlink_registry_add_peer()` | peer 추가 command | 변경 없음 | 유지 |

`zlink_reply_handler_fn`은 이미 `zlink_msg_t *parts, size_t part_count`를 받으므로
시그니처를 바꾸지 않는다. Actor join reply payload가 multipart로 바뀌면 기존 reply
handler shape로 그대로 전달한다.

### 5.2 변경되는 소유권 계약

| API | 성공 시 | 실패 시 |
|-----|---------|---------|
| `zlink_spot_node_create_remote_actor()` | `parts[0..part_count)` 소유권이 라이브러리로 이전된다 | validation 실패나 request 제출 전 실패에서는 호출자에게 남는다 |
| `zlink_spot_node_actor_join_spot()` | `parts[0..part_count)` 소유권이 라이브러리로 이전된다 | validation 실패나 submit 전 실패에서는 호출자에게 남는다 |
| `zlink_spot_actor_join_recv()` | `parts_out` payload 소유권이 호출자에게 이전된다 | 출력 payload 없음 |
| `zlink_spot_actor_join_reply()` | reply `parts[0..part_count)` 소유권이 라이브러리로 이전된다 | validation 실패, duplicate reply, stale reply에서는 호출자에게 남는다 |
| `zlink_actor_admission_handler_fn` | handler는 `parts`를 빌려서 읽기만 한다 | handler는 part를 close하거나 move하지 않는다 |

### 5.3 변경되는 validation 계약

- `parts == NULL && part_count == 0`은 payload 없음이다.
- `parts != NULL && part_count > 0`은 multipart payload다.
- `parts == NULL && part_count > 0`은 invalid argument다.
- `parts != NULL && part_count == 0`은 invalid argument다.
- 빈 message 하나를 명시적으로 보내고 싶으면 size 0인 `zlink_msg_t` 하나를
  `part_count == 1`로 넘긴다.
- `zlink_registry_set()`에서 `value == 0`은 invalid argument다.
- `zlink_registry_get()`은 성공하면 option 값을 반환하고 `error_out`에
  `ZLINK_CONFIG_OK`를 기록한다.
- `zlink_registry_get()` 실패 시 반환값은 0이며, `error_out`이 NULL이 아니면 실패
  result를 기록한다.
- 알 수 없는 registry option은 `ZLINK_CONFIG_NOT_SUPPORTED`를 반환하고 `errno`를
  `ENOTSUP`로 설정한다.
- `zlink_registry_set()`에서 `registry == NULL`이면 `ZLINK_CONFIG_INVALID_HANDLE`을
  반환하고 `errno`를 `EFAULT`로 설정한다.
- `zlink_registry_set()`에서 `value == 0`이면 `ZLINK_CONFIG_INVALID_ARGUMENT`를
  반환하고 `errno`를 `EINVAL`로 설정한다.

### 5.4 Spot Actor multipart payload

권장 public shape는 아래와 같다.

```c
ZLINK_EXPORT zlink_request_result_t zlink_spot_node_create_remote_actor(
  void *node,
  const zlink_routing_id_t *target_node_rid,
  const char *actor_id,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_actor_create_result_t *out,
  uint32_t timeout_ms);

typedef zlink_actor_admission_result_t (*zlink_actor_admission_handler_fn)(
  void *node,
  const char *actor_id,
  const zlink_msg_t *parts,
  size_t part_count,
  void *userdata);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_join_spot(
  void *node,
  const zlink_actor_ref_t *actor,
  const zlink_routing_id_t *dest_node_rid,
  const zlink_routing_id_t *dest_spot_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_actor_join_recv(
  void *spot,
  zlink_actor_join_info_t *info_out,
  zlink_msg_t **parts_out,
  size_t *part_count_out,
  zlink_recv_flags_t flags);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_actor_join_reply(
  void *spot,
  const zlink_actor_join_info_t *info,
  uint32_t accepted,
  zlink_msg_t *parts,
  size_t part_count);
```

### 5.5 Registry option

권장 public shape는 아래와 같다.

```c
typedef enum zlink_registry_option_t {
  ZLINK_REGISTRY_OPT_ID = 0x3801,
  ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS = 0x3802,
  ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS = 0x3803,
  ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS = 0x3804
} zlink_registry_option_t;

ZLINK_EXPORT zlink_config_result_t zlink_registry_set(
  void *registry,
  zlink_registry_option_t option,
  uint32_t value);

ZLINK_EXPORT uint32_t zlink_registry_get(
  void *registry,
  zlink_registry_option_t option,
  zlink_config_result_t *error_out);
```

기존 named scalar setter는 compatibility wrapper로 유지한다. 새 문서와 binding
surface는 option API를 canonical API로 설명한다.

| 기존 API | 구현 방식 | 문서 처리 |
|----------|-----------|-----------|
| `zlink_registry_set_id()` | `ZLINK_REGISTRY_OPT_ID` wrapper | compatibility API로 남김 |
| `zlink_registry_set_heartbeat()` | interval / timeout option을 같은 lock 안에서 설정 | compatibility API로 남김 |
| `zlink_registry_set_broadcast_interval()` | `ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS` wrapper | compatibility API로 남김 |
| `zlink_registry_add_peer()` | 기존 command 유지 | canonical API로 유지 |

## 6. 문서 반영 계획

### 6.1 문서 반영 순서

문서 반영은 구현 상태와 독자에 맞춰 순서대로 진행한다.

1. 구현 전 설계는 `doc/spec/draft/core-api-surface-alignment.ko.md`에 둔다.
2. core 구현과 C 테스트가 끝난 뒤 정식 spec을 갱신한다.
3. 정식 spec과 header가 맞는지 확인한 뒤 guide를 갱신한다.
4. 내부 구조가 바뀐 부분만 internals에 반영한다.
5. binding public surface 문서는 core native 배포와 binding 코드 수정 전에 갱신한다.

### 6.2 문서별 반영 항목

| 문서 위치 | 반영 항목 | 기준 |
|-----------|-----------|------|
| `doc/spec/draft/core-api-surface-alignment.ko.md` | 이 계획의 public C API, ownership, registry option 계약을 구현 전 초안으로 작성 | 구현 전 초안 |
| `doc/spec/draft/spot-actor-dispatch.ko.md` | create / join 단일 message 계약을 multipart payload 계약으로 변경 | 구현 전 초안 |
| `doc/spec/core/service/spot.ko.md` | 구현 완료 뒤 changed API, ownership, validation, recv/reply 계약 반영 | `core/include/zlink.h` |
| `doc/spec/core/service/registry.ko.md` | `zlink_registry_option_t`, `zlink_registry_set()`, `zlink_registry_get()`, wrapper 계약 반영 | `core/include/zlink.h` |
| `doc/spec/core/errno-map.md` | 새 registry option API, 변경된 Actor create / join 실패 경로 반영 | result / errno 구현 |
| `doc/internals/spot-internals.ko.md` | join request payload 저장소가 단일 message에서 owned multipart로 바뀐 내부 흐름 반영 | core 구현 |
| `doc/internals/discovery-internals.ko.md` | registry heartbeat / broadcast option 적용 흐름 반영 | core 구현 |
| `doc/guide/07-4-registry.ko.md` | named setter 예제를 registry option 예제로 변경 | 정식 spec |
| `doc/guide/07-1-discovery.ko.md` | registry 구성 예제에서 heartbeat / broadcast interval 설정 방식 변경 | 정식 spec |
| `doc/guide/11-thread-safety.ko.md` | registry 설정 API 이름 목록 갱신 | 정식 spec |
| `doc/internals/thread-safety.ko.md` | registry 설정 API 이름 목록 갱신 | 정식 spec |
| `doc/spec/bindings/README.md` | binding 공통 Actor join payload와 registry option 매핑 원칙 반영 | binding 계약 |
| `doc/spec/bindings/c/README.md` | C binding은 core C 계약을 그대로 노출한다는 변경점 반영 | core header |
| `doc/spec/bindings/cpp/README.md` | C++ Actor create / join multipart, Registry typed option surface 반영 | C++ binding 설계 |
| `doc/spec/bindings/dotnet/README.md` | .NET Actor create / join multipart, Registry typed option surface 반영 | .NET binding 설계 |
| `doc/spec/bindings/go/README.md` | Go Actor create / join multipart, Registry typed option surface 반영 | Go binding 설계 |
| `doc/spec/bindings/java/README.md` | Java Actor create / join multipart, Registry typed option surface 반영 | Java binding 설계 |
| `doc/spec/bindings/node/README.md` | Node Actor create / join multipart, Registry typed option surface 반영 | Node binding 설계 |
| `doc/spec/bindings/python/README.md` | Python Actor create / join multipart, Registry typed option surface 반영 | Python binding 설계 |
| `doc/spec/bindings/rust/README.md` | Rust Actor create / join multipart, Registry typed option surface 반영 | Rust binding 설계 |

guide에는 사용자가 어떤 상황에서 multipart payload와 registry option을 쓰는지 설명한다.
내부 socket, inproc endpoint, request queue 구현은 guide에 넣지 않는다.

internals에는 단일 message 저장소가 multipart 저장소로 바뀐 구조와 registry option이
runtime tick에서 반영되는 내부 흐름만 적는다. 사용법 예제는 internals에 넣지 않는다.

## 7. Core 구현 단계

### 7.1 구현 파일 매트릭스

| 영역 | 파일 | 작업 |
|------|------|------|
| public enum | `core/include/zlink_enum.h` | `zlink_registry_option_t`와 option 값을 추가한다 |
| public C API | `core/include/zlink.h` | Actor create / join 시그니처 변경, registry option API 선언 추가, registry wrapper 선언 유지 |
| version | `core/include/zlink.h`, binding package version files | core와 binding package version을 `6.0.0`으로 맞춘다 |
| registry C facade | `core/src/api/service_registry_api.cpp` | `zlink_registry_set()` / `zlink_registry_get()` 구현, 기존 setter wrapper 재배선 |
| registry access | `core/src/services/discovery/registry_access.hpp`, `core/src/services/discovery/registry_access.cpp` | option set/get access helper 추가 |
| registry core | `core/src/services/discovery/registry.hpp`, `core/src/services/discovery/registry_config.cpp` | `set_option`, `get_option`, heartbeat pair wrapper 구현 |
| Actor C facade | `core/src/api/service_spot_actor_api.cpp` | admission handler, create remote actor, join submit/recv/reply multipart 전환 |
| local native sync | `bindings/sync_local_zlink_libs.sh` | `core/build/lib` 산출물을 언어별 native 경로에 복사하는 local 동기화 스크립트 추가 |
| core tests | `core/tests/integration/test_spot_actor_dispatch.cpp` | Actor multipart create / join / reply / ownership 테스트 추가 |
| registry tests | `core/tests/unittest/unittest_registry_access.cpp` | registry option set/get 단위 테스트 추가 |
| registry integration tests | `core/tests/integration/test_discovery_resolve_spot.cpp`, `core/tests/integration/test_spot_multi_service_discovery.cpp`, `core/tests/integration/test_discovery_socket_auto_connect_policy.cpp`, 관련 discovery test | 새 option API와 wrapper 계약에 맞춰 호출 갱신 |
| version gate | `core/include/zlink.h`, build metadata | version `6.0.0` 적용 여부를 검증한다 |

### 7.2 단계 1. Draft / plan 기준 확정

- `doc/spec/draft/spot-actor-dispatch.ko.md`에서 단일 message로 적힌 create / join
  계약을 multipart payload 기준으로 고친다.
- `doc/spec/draft/core-api-surface-alignment.ko.md`를 추가하고, 이 문서의 public C API,
  ownership, registry option 계약을 구현 전 초안으로 정리한다.
- `doc/spec/core/service/spot.ko.md`와 `doc/spec/core/service/registry.ko.md`는 구현
  전에는 현재 구현 기준을 유지한다.

### 7.3 단계 2. Public header 변경

- `core/include/zlink_enum.h`에 `zlink_registry_option_t`를 추가한다.
- `core/include/zlink.h`에서 Actor create / join 함수 시그니처를 multipart 기준으로
  바꾼다.
- `zlink_actor_admission_handler_fn`에 `parts`와 `part_count`를 추가한다.
- `zlink_registry_set()` / `zlink_registry_get()` 선언을 추가한다.
- 기존 registry scalar setter 선언은 wrapper로 유지한다.
- Actor C API 시그니처 변경은 ABI break다. 이 단계에서 `ZLINK_VERSION_MAJOR`를 `6`,
  `ZLINK_VERSION_MINOR`를 `0`, `ZLINK_VERSION_PATCH`를 `0`으로 바꾼다.

### 7.4 단계 3. Actor create / join 내부 저장 모델 변경

- `queued_join_request_t`의 단일 `zlink_msg_t message` / `reply`를
  `spot_owned_msg_parts_t message_parts`와 `spot_owned_msg_parts_t reply_parts`로
  바꾼다.
- owned container가 소유권을 갖게 한다. 기존 `owns_message` / `owns_reply` boolean은
  제거한다.
- create admission 경로는 request payload를 `const zlink_msg_t *parts, size_t part_count`
  view로 handler에 넘긴다.
- handler는 payload를 닫지 않는다. admission payload의 lifetime은 handler 호출 동안만
  유효하다고 문서화한다.
- `complete_join_request()`, timeout cleanup, `retire_join_request_locked()`,
  `release_join_request_after_completion()`에서 모든 owned part가 한 번만 닫히는지
  확인한다.

### 7.5 단계 4. Actor join recv / reply 변경

- `zlink_spot_actor_join_recv()`는 TLS multipart view를 써서 `parts_out` /
  `part_count_out`을 채운다.
- `zlink_spot_actor_join_reply()`는 reply multipart를 request에 move/adopt한다.
- accept / reject completion callback은 기존 `zlink_reply_handler_fn` shape에 맞춰
  reply parts와 count를 그대로 전달한다.
- timeout, duplicate reply, stale reply 경로에서 owned multipart가 누수 없이 닫히는지
  확인한다.
- `zlink_multipart_close()`가 TLS view payload를 닫는 기존 규칙과 충돌하지 않도록
  recv output 방식은 기존 service routed recv와 같은 TLS multipart view로 고정한다.

### 7.6 단계 5. Registry option 구현

- registry config 내부에 `set_option` / `get_option` helper를 둔다.
- `ZLINK_REGISTRY_OPT_ID`는 기존 registry id 설정과 같은 상태를 갱신한다.
- `ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS`와
  `ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS`는 각각 기존 heartbeat interval / timeout
  상태를 갱신한다.
- `ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS`는 기존 broadcast interval 상태를 갱신한다.
- `value == 0`은 invalid argument로 처리한다.
- 알 수 없는 option은 `ZLINK_CONFIG_NOT_SUPPORTED`와 `ENOTSUP`로 처리한다.
- `zlink_registry_set_heartbeat()` wrapper는 interval과 timeout을 한 lock 안에서
  함께 갱신한다. 외부 tick이 한 값만 바뀐 중간 상태를 관찰하지 않도록 이 wrapper는
  기존 `registry_t::set_heartbeat()` 경로를 호출한다.
- `zlink_registry_get()`은 `error_out == NULL`도 허용한다. 실패 시 반환값은 0이다.

### 7.7 단계 6. 테스트 보강

Actor create / join 테스트는 최소한 아래 항목을 추가한다.

| ID | 확인 내용 |
|----|-----------|
| ACT-MPART-01 | remote create admission handler가 여러 part를 순서대로 읽는다 |
| ACT-MPART-02 | remote create 성공 시 input parts 소유권이 라이브러리로 이전된다 |
| ACT-MPART-03 | remote create validation 실패 시 input parts 소유권이 호출자에게 남는다 |
| ACT-MPART-04 | join recv가 여러 part를 순서대로 반환한다 |
| ACT-MPART-05 | join reply가 여러 part를 completion handler에 전달한다 |
| ACT-MPART-06 | duplicate reply 실패 시 reply parts 소유권이 호출자에게 남는다 |
| ACT-MPART-07 | join timeout cleanup이 unread request parts를 닫는다 |

Registry option 테스트는 최소한 아래 항목을 추가한다.

| ID | 확인 내용 |
|----|-----------|
| REG-OPT-01 | registry id를 option으로 설정하고 snapshot에서 확인한다 |
| REG-OPT-02 | heartbeat interval / timeout을 option으로 설정한다 |
| REG-OPT-03 | broadcast interval을 option으로 설정한다 |
| REG-OPT-04 | `value == 0`은 invalid argument로 실패한다 |
| REG-OPT-05 | 알 수 없는 option은 unsupported 계열로 실패한다 |
| REG-OPT-06 | 기존 registry scalar setter wrapper가 같은 상태를 갱신한다 |

필수 core 검증 명령:

```sh
cmake --build core/build
ctest --test-dir core/build --output-on-failure -R "test_spot_actor_dispatch|unittest_registry_access"
ctest --test-dir core/build --output-on-failure -R "test_discovery_resolve_spot|test_spot_multi_service_discovery|test_discovery_socket_auto_connect_policy"
```

targeted test를 통과한 뒤 전체 core test를 실행한다.

```sh
ctest --test-dir core/build --output-on-failure
```

### 7.8 단계 7. 문서 반영

구현과 테스트가 끝난 뒤 아래 문서를 갱신한다.

- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/registry.ko.md`
- `doc/spec/core/errno-map.md`
- `doc/guide/07-1-discovery.ko.md`
- `doc/guide/07-4-registry.ko.md`
- `doc/spec/bindings/README.md`
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`

`/home/hep7/project/kairos/zlink/doc/spec/bindings` 아래 문서는 binding 코드 수정 전에
반드시 먼저 갱신한다. 이 문서는 언어별 binding public surface의 기준이므로, core
C API만 바꾸고 binding spec 문서를 나중으로 미루지 않는다.

guide에는 사용자가 어떤 상황에서 multipart payload를 쓰는지 설명한다. 내부 socket,
inproc endpoint, request queue 구현은 guide에 넣지 않는다.

## 8. Core library 배포 단계

core C API를 바꾼 뒤에는 언어별 binding이 로드하는 native library가 같은 ABI를 보도록
local native 동기화 단계를 별도 gate로 둔다. 이 gate는 GitHub release 없이 로컬에서
끝나야 한다.

### 8.1 local core 검증

- `cmake --build core/build`로 `core/build` runtime을 먼저 다시 만든다.
- `bindings/c/perf` 기준 규칙과 같이 다른 임시 build directory 결과를 binding 배포
  기준으로 쓰지 않는다.
- `core/build/lib/libzlink.so`와 versioned soname 파일이 새 header와 같은 version인지
  확인한다.
- binding 코드 수정 전 local smoke는 `bindings/sync_local_zlink_libs.sh --expect-version 6.0.0`
  실행 뒤 진행한다. `core/build` 산출물을 임시 복사하는 test script를 모두 찾아
  새 soname `libzlink.so.6`을 쓰도록 고친다.

### 8.2 local native 동기화

`bindings/sync_local_zlink_libs.sh`를 추가한다. 이 스크립트는 `core/build/lib`의
현재 platform 산출물을 현재 platform의 binding native 경로에 복사한다.

실행 예:

```sh
bindings/sync_local_zlink_libs.sh --expect-version 6.0.0
```

이 스크립트는 다음을 수행해야 한다.

- 현재 OS/architecture를 repo 표준 platform key로 변환한다. Linux x86_64는
  `linux-x86_64`와 `linux-x64` 양쪽 naming을 필요한 binding 경로에 맞춰 쓴다.
- `core/build/lib/libzlink.so`, `libzlink.so.6`, versioned soname 파일을 현재 platform
  native 경로에 복사한다.
- `bindings/cpp/include/zlink.h`, `bindings/cpp/include/zlink_enum.h`,
  `bindings/go/include/zlink.h`, `bindings/go/include/zlink_enum.h`,
  `bindings/rust/include/zlink.h`, `bindings/rust/include/zlink_enum.h`를
  `core/include` 기준으로 갱신한다.
- binding package/test version marker를 `6.0.0`으로 맞춘다.
- linux-x64 native library의 `zlink_version()`이 `6.0.0`을 반환하는지 확인한다.

### 8.3 배포 대상 경로

| Binding | native library 대상 |
|---------|---------------------|
| C | `bindings/c`는 core header와 build link 기준을 갱신한다. 별도 언어별 native 복사 대상은 아니다 |
| C++ | `bindings/cpp/native/*` |
| .NET | `bindings/dotnet/native/*`, `bindings/dotnet/runtimes/*/native` |
| Go | `bindings/go/native/*` |
| Java | `bindings/java/native/*`, `bindings/java/src/main/resources/native/*`, `bindings/java/build/resources/main/native/*` |
| Node | `bindings/node/native/*`, `bindings/node/prebuilds/*` |
| Python | `bindings/python/src/zlink/native/*` |
| Rust | `bindings/rust/native/*` |

수동 복사는 쓰지 않는다. 동기화 경로가 빠지면 `bindings/sync_local_zlink_libs.sh`를
고치고 다시 실행한다.

### 8.4 배포 후 확인

- 각 native directory의 `libzlink.so`, `libzlink.so.6`, versioned soname symlink가
  깨지지 않았는지 확인한다.
- local gate는 현재 platform native 경로를 검증한다. 다른 platform 산출물 생성은
  GitHub release tag 생성 범위에 속하므로 이 계획의 필수 gate에 넣지 않는다.
- language binding test가 로드하는 library 경로가 배포된 binding native 경로인지
  확인한다.

## 9. Binding 수정 계획

binding 수정은 core native 배포 뒤 진행한다. 각 binding은 native C 함수를 그대로
노출하지 않고 언어별 typed API로 깊은 표면을 제공해야 한다.

### 9.1 공통 binding 정책

- Actor create / join payload는 언어별 message collection으로 받는다.
- 단일 message convenience overload는 유지하고 내부에서 multipart collection 경로를
  호출한다.
- empty payload와 empty single message를 구분한다.
- admission handler는 borrowed payload view를 받으며, callback 밖으로 native part를
  보관하지 않는다.
- join recv 결과는 `ActorJoinRequest` 같은 wrapper로 묶고, native reply context는
  binding 내부에 숨긴다.
- registry option은 언어별 typed enum setter/getter로 감싼다.
- 기존 named setter를 제공하는 언어 binding은 해당 setter를 compatibility alias로
  문서화한다. runtime deprecation warning은 추가하지 않는다.

### 9.2 언어별 작업 항목

| Binding | 수정 항목 | 검증 |
|---------|-----------|------|
| C | `bindings/c/include`, `bindings/c/src`가 새 core header와 빌드되도록 갱신한다. C binding 문서는 core C 계약과 동일하게 맞춘다 | `cmake --build bindings/c/build`, `ctest --test-dir bindings/c/build --output-on-failure` |
| C++ | `bindings/cpp/include/zlink.h`, `bindings/cpp/include/zlink_enum.h`, `bindings/cpp/include/zlink/services/actor.hpp`, `bindings/cpp/include/zlink/services/registry.hpp`를 갱신한다. `message_t` collection 기반 Actor create / join API로 기존 Actor create / join API를 변경한다. Registry option은 typed enum과 setter/getter로 감싼다 | `cmake --build bindings/cpp/build`, `ctest --test-dir bindings/cpp/build --output-on-failure` |
| .NET | `bindings/dotnet/src/Zlink/Native/NativeMethods.Service.cs`, `NativeServiceModels.cs`, `Enums.cs`, `Service/Registry.cs`, `Service/Spot.cs`의 P/Invoke와 wrapper를 갱신한다 | `bindings/dotnet/tests/run_tests.sh` |
| Go | `bindings/go/include/zlink.h`, `bindings/go/include/zlink_enum.h`, `bindings/go/actor.go`, `bindings/go/spot.go`의 cgo 선언과 wrapper를 갱신한다 | `(cd bindings/go && go test ./...)` |
| Java | `bindings/java/src`의 native downcall 선언, `List<Message>` 기반 Actor payload, registry option enum, Gradle native resource를 갱신한다 | `bindings/java/tests/run_tests.sh` |
| Node | `bindings/node/src/native.ts`, native addon source, TypeScript `Message[]` API, registry option enum, prebuild packaging metadata를 갱신한다 | `(cd bindings/node && npm test)` |
| Python | `bindings/python/src/zlink/_ffi.py`, `_enums.py`, `_spot.py`, `_discovery.py`의 native declaration과 wrapper를 갱신한다 | `(cd bindings/python && pytest tests)` |
| Rust | `bindings/rust/include`, `bindings/rust/src/service.rs`, 관련 Actor wrapper의 FFI 선언과 typed API를 갱신한다 | `(cd bindings/rust && cargo test)` |

### 9.3 binding 문서와 코드 순서

각 언어는 아래 순서로 진행한다.

1. `doc/spec/bindings/<language>/README.md`에서 public binding surface를 먼저 정리한다.
2. native interop 선언을 새 C API에 맞춘다.
3. public wrapper를 언어 관례에 맞게 갱신한다.
4. samples에서 registry named setter 예제를 option API로 바꾼다.
5. Actor create / join multipart 테스트와 registry option 테스트를 추가한다.
6. 해당 언어 test를 통과시킨다.

## 10. POSD 점검

### 10.1 기존 위험 신호

| 위험 신호 | 위치 | 문제 |
|-----------|------|------|
| 좁은 API | Actor create / join 단일 message payload | 사용자가 payload framing을 직접 만들어야 한다 |
| 얕은 wrapper 증가 가능성 | registry scalar setter | 설정이 늘 때마다 새 함수가 추가된다 |
| 정보 누출 가능성 | part-by-part join API 대안 | 호출자가 join 요청의 중간 상태를 알아야 한다 |

### 10.2 선택한 방향

Actor payload는 aggregate multipart API를 선택한다. 이 방식은 단일 요청이라는
인터페이스 의미를 유지하면서 payload 표현력만 넓힌다.

Registry 설정은 option API를 선택한다. scalar 설정을 하나의 set / get 표면으로 모아
새 설정을 추가할 때 public 함수 수가 계속 늘어나는 문제를 줄인다.

### 10.3 대안과 배제 이유

| 대안 | 배제 이유 |
|------|-----------|
| 단일 `zlink_msg_t` 유지 | 사용자 payload 안에 별도 framing을 만들게 한다 |
| Actor join part-by-part API 추가 | create / join 요청의 원자성을 호출자에게 노출한다 |
| registry named setter 계속 추가 | 설정 수가 늘수록 shallow API가 늘어난다 |
| `zlink_registry_add_peer()`를 option으로 흡수 | peer 추가는 scalar 설정이 아니라 command다 |

## 11. 완료 기준

- core public header가 이 문서의 API 방향과 일치한다.
- Actor C API signature break에 맞춰 core와 binding package version이 `6.0.0`이다.
- Actor create / join multipart ownership 테스트가 통과한다.
- Registry option set / get 테스트가 통과한다.
- 정식 spec은 구현된 공개 계약만 설명한다.
- guide는 사용법과 의도를 설명하고 내부 구현 설명을 포함하지 않는다.
- errno 문서와 `/home/hep7/project/kairos/zlink/doc/spec/bindings` 아래 binding 문서가
  새 public surface를 반영한다.
- `bindings/sync_local_zlink_libs.sh --expect-version 6.0.0`이 새 core library를 현재
  platform의 언어별 native 경로에 배포한다.
- C/C++/.NET/Go/Java/Node/Python/Rust binding의 native interop 선언과 public wrapper가
  새 core API를 쓴다.
- 각 binding의 Actor multipart와 Registry option 테스트가 통과한다.

## 12. Codex 무인 실행 루프

Codex 에이전트는 각 큰 단계마다 아래 루프를 반복한다.

1. 해당 단계의 파일 매트릭스와 계약을 다시 읽는다.
2. 코드를 수정한다.
3. 해당 단계의 gate 명령을 실행한다.
4. 실패한 gate는 원인을 찾아 수정하고 같은 gate를 다시 실행한다.
5. gate 통과 뒤 stale 계약 검색을 실행한다.
6. stale 계약이 남아 있으면 문서, 코드, 테스트 중 기준과 다른 쪽을 고치고 같은 검색을
   다시 실행한다.
7. 같은 단계의 gate와 stale 계약 검색이 모두 통과한 뒤 다음 단계로 이동한다.

필수 stale 계약 검색:

```sh
rg -n "zlink_spot_node_create_remote_actor\\s*\\([^\\n]*zlink_msg_t \\*message|zlink_spot_node_actor_join_spot\\s*\\([^\\n]*zlink_msg_t \\*message|zlink_spot_actor_join_recv\\s*\\([^\\n]*zlink_msg_t \\*message_out|zlink_spot_actor_join_reply\\s*\\([^\\n]*zlink_msg_t \\*message" core bindings doc
rg -n "단일 `zlink_msg_t`|단일 message|single `zlink_msg_t`|single message" doc/spec doc/guide doc/internals doc/spec/bindings bindings
rg -n "zlink_registry_set_heartbeat\\(|zlink_registry_set_broadcast_interval\\(|zlink_registry_set_id\\(" doc/guide doc/spec/bindings bindings
rg -n "libzlink\\.so\\.5|5\\.3\\.10|5\\.3\\." bindings core/include doc/spec/bindings
```

허용되는 stale 검색 결과:

- `zlink_registry_set_id()`, `zlink_registry_set_heartbeat()`,
  `zlink_registry_set_broadcast_interval()`이 compatibility wrapper 자체나 wrapper 테스트에서
  등장하는 경우
- `doc/spec/draft/`에서 변경 전 계약을 설명하고 바로 아래에 변경 후 계약을 대비하는 경우
- migration 설명에서 과거 API 이름을 "이전 계약"으로 명시하는 경우

그 외 결과는 현재 단계가 완료되지 않은 것으로 처리한다.
