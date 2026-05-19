# Draft -- Spot GetOrNew C API

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와
> `bindings/c/include/zlink.h`에 없는 API를 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 `SpotNode` 안에서 특정 `Spot` routing id를 기준으로 `Spot` handle을
원자적으로 확보하는 C API를 정의한다.

게임 방, 대기열, 작업 그룹처럼 이름이나 코드로 식별되는 논리 공간은 "없으면
만들고, 이미 있으면 기존 공간에 붙는다"는 동작이 필요하다. 현재 C API에는
`zlink_spot_new()`와 `zlink_spot_node_spot_lookup()`이 따로 있지만, 두 API를
사용자 코드에서 조합하면 lookup과 new 사이에 경합이 생길 수 있다.

따라서 core가 아래 동작을 하나의 API 계약으로 제공해야 한다.

- 같은 `SpotNode` 안에서 같은 `Spot` routing id는 하나의 logical spot만 가진다.
- 처음 호출한 쪽은 logical spot을 만들고 `created_out = 1`을 받는다.
- 이후 호출한 쪽은 기존 logical spot에 대한 새 facade handle을 받고
  `created_out = 0`을 받는다.
- 이 판단은 `SpotNode` 내부 lock 아래에서 원자적으로 수행한다.

## 2. 현재 공개 API 상태

현재 공개 C API에는 아래 함수가 있다.

```c
ZLINK_EXPORT void *zlink_spot_new (void *node_);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_lookup (
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_);
```

`zlink_spot_new()`는 새 logical spot을 만들고 routing id를 내부에서 생성한다.
`zlink_spot_node_spot_lookup()`은 이미 존재하는 logical spot을 routing id로 찾는다.

이 조합만으로는 "특정 routing id의 spot을 확보한다"는 계약을 표현하기 어렵다.
사용자가 먼저 lookup하고, 없으면 새 spot을 만들고, 다시 routing id를 바꾸는
흐름을 직접 작성해야 한다. 이 방식은 호출자가 내부 수명 규칙과 경합 처리를
알아야 하므로 API가 너무 얕아진다.

## 3. 추가 API 초안

### 3.1 C API 수정 목록

이번 초안에서 수정되는 C API는 아래 하나를 추가하는 것이다.

- 추가:
  - `zlink_spot_node_spot_get_or_new(...)`

아래 기존 API의 의미는 바꾸지 않는다.

- `zlink_spot_new(...)`
  - 기존처럼 local `SpotNode` 위에 새 logical spot을 만들고 routing id는 core가
    생성한다.
- `zlink_spot_node_spot_lookup(...)`
  - 기존처럼 이미 존재하는 logical spot을 routing id로 조회한다.
- `zlink_spot_node_actor_join_spot(...)`
  - 기존처럼 actor join 요청만 처리한다. spot 생성은 이 API에 섞지 않는다.

즉 이번 변경은 "기존 생성 함수의 의미 변경"이 아니라 "명시적 routing id를 가진
logical spot을 원자적으로 확보하는 새 함수 추가"다.

수정 대상 파일은 구현 시점에 아래를 기본 범위로 본다.

- `core/include/zlink/spot.h`
- `bindings/c/include/zlink.h`
- `core/src/api/spot/core/service_spot_node_api.cpp`
- `core/src/runtime/services/spot/node/spot_node.hpp`
- `core/src/runtime/services/spot/node/spot_node_access.hpp`
- `core/src/runtime/services/spot/node/spot_node_access.cpp`
- `core/src/runtime/services/spot/node/spot_node_lifecycle.cpp`
- `core/src/libzlink.vers`

`core/include/zlink.h`는 `zlink/spot.h`를 포함하는 aggregate header이므로 직접
선언을 추가하는 파일이 아니다. 구현 단계에서는 새 함수가 이 aggregate header를
통해 노출되는지만 확인한다.

`core/src/libzlink.vers`는 플랫폼별 symbol export 정책을 유지하기 위해 함께
확인한다. 빌드 설정상 별도 export list가 필요 없더라도, 구현 단계에서 누락
여부를 반드시 확인한다.

### 3.2 `zlink_spot_node_spot_get_or_new`

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_spot_get_or_new (
  void *node_,
  const zlink_routing_id_t *spot_rid_,
  void **spot_out_,
  uint32_t *created_out_);
```

의도는 `node_` 안에서 `spot_rid_`로 식별되는 logical spot을 확보하는 것이다.

- `node_`는 local `SpotNode` handle이다.
- `spot_rid_`는 확보하려는 logical spot의 routing id다.
- `spot_out_`은 호출자가 소유하는 새 `Spot` facade handle을 받는다.
- `created_out_`은 이번 호출이 logical spot을 새로 만들었는지 알려 준다.

`created_out_`은 선택 인자다. `NULL`이면 생성 여부를 돌려주지 않는다. 다만
게임 방 생성 같은 사용 사례에서는 최초 생성자만 초기 상태를 세팅해야 하므로,
사용자는 가능하면 이 값을 확인하는 편이 좋다.

## 4. 동작 계약

### 4.1 새 logical spot 생성

`spot_rid_`에 해당하는 logical spot이 없으면 core는 새 logical spot을 만들고,
그 logical spot의 routing id를 `spot_rid_`로 고정한다.

성공 시:

- `*spot_out_`은 새 facade handle을 가리킨다.
- `created_out_`이 `NULL`이 아니면 `*created_out_ = 1`이다.
- 반환값은 `ZLINK_CONFIG_OK`다.

새 logical spot이 생성되면 discovery summary를 사용할 수 있는 상태에서는 기존
spot 생성 경로와 같은 방식으로 READY summary를 발행한다.

### 4.2 기존 logical spot 반환

`spot_rid_`에 해당하는 logical spot이 이미 있으면 core는 새 facade handle만
만들어 반환한다. logical spot 자체는 새로 만들지 않는다.

성공 시:

- `*spot_out_`은 기존 logical spot을 가리키는 새 facade handle이다.
- `created_out_`이 `NULL`이 아니면 `*created_out_ = 0`이다.
- 반환값은 `ZLINK_CONFIG_OK`다.

반환된 facade handle은 다른 `Spot` handle과 같은 방식으로
`zlink_spot_destroy()`로 해제한다.

### 4.3 원자성

`zlink_spot_node_spot_get_or_new()`는 lookup과 new 판단을 `SpotNode` 내부
per-spot creation queue에서 수행해야 한다.

같은 `node_`와 같은 `spot_rid_`로 여러 thread가 동시에 호출하더라도 결과는
아래처럼 고정된다.

- 정확히 하나의 logical spot만 만들어진다.
- 생성에 성공한 호출은 최대 하나만 `created_out = 1`을 받는다.
- 나머지 성공 호출은 같은 logical spot에 대한 facade handle을 받고
  `created_out = 0`을 받는다.
- 아직 facade 생성과 routed state 초기화가 끝나지 않은 logical spot은
  `spots_by_rid`에서 관찰되면 안 된다.
- 같은 `spot_rid_`의 동시 호출은 같은 per-spot creation queue에서 직렬 처리된다.
- 다른 `spot_rid_`의 호출은 서로 다른 queue를 사용하므로 서로 막지 않는다.

이 규칙이 이 API의 핵심이다. 사용자가 `lookup -> new -> update routing id` 순서를
직접 작성하지 않게 하려는 이유도 이 원자성 때문이다.

### 4.4 실패 시 rollback

새 logical spot을 만든 뒤 facade handle 생성이나 routed state 초기화가 실패하면
core는 방금 만든 logical spot을 외부에서 관찰할 수 없게 해야 한다. 이 처리는
per-spot creation queue의 head에서만 수행한다.

구체적인 규칙은 아래와 같다.

- 같은 `spot_rid_`의 호출은 per-spot creation queue에 들어간다.
- queue head만 새 logical spot 후보를 만들 수 있다.
- queue head는 facade handle과 routed state가 준비된 뒤에만 `spots_by_rid`에
  publish한다.
- queue head가 실패하면 그 후보는 publish하지 않는다. queue의 다음 caller는
  실패를 그대로 받을지, 새 head로 재시도할지 구현 정책에 따라 처리할 수 있지만,
  실패한 후보를 existing spot으로 반환하면 안 된다.
- 새 logical spot을 publish한 호출에서만 rollback을 수행한다.
- 기존 logical spot을 반환하려던 호출은 facade 생성 실패가 나더라도 logical
  spot을 제거하면 안 된다.
- rollback 후 `spot_out_`은 `NULL`이어야 한다.
- `created_out_`은 실패한 경우 의미가 없지만, 구현은 가능하면 `0`으로 초기화한다.
- READY summary를 이미 발행했다면 STOPPED summary도 발행해야 한다.
- READY summary를 facade 생성 성공 이후에만 발행하도록 구현하면 STOPPED 보정이
  필요하지 않다. 이 초안은 이 방식을 구현 기준으로 고정한다.

이 rollback 규칙이 없으면 첫 호출이 실패했는데 logical spot만 남고, 다음 호출이
`created_out = 0`을 받는 잘못된 상태가 생길 수 있다.

특히 아래 상태는 허용하지 않는다.

- caller A가 새 logical spot을 map에 넣었지만 facade 생성에는 실패한다.
- caller B가 그 사이에 같은 `spot_rid_`를 조회해서 `created_out = 0`으로 성공한다.
- 결과적으로 아무 성공 caller도 `created_out = 1`을 받지 않았는데 logical spot이
  남는다.

이 상태를 피하려면 같은 `spot_rid_`의 호출을 per-spot creation queue로 직렬화하고,
새 state를 완전히 준비한 뒤 publish해야 한다.

## 5. 에러 계약 초안

정확한 반환값은 기존 `zlink_config_result_t` 매핑을 따른다.

| 조건 | 반환 방향 | errno 방향 |
|------|-----------|------------|
| `node_ == NULL` 또는 유효하지 않은 handle | `ZLINK_CONFIG_INVALID_HANDLE` | `EFAULT` |
| `spot_rid_ == NULL` | `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL` |
| `spot_out_ == NULL` | `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL` |
| `spot_rid_` 형식이 유효하지 않음 | `ZLINK_CONFIG_INVALID_ARGUMENT` | `EINVAL` |
| logical spot 또는 facade 할당 실패 | 기존 config error 매핑 | `ENOMEM` |
| facade 등록 실패 | 기존 config error 매핑 | 원인 errno 유지 |

성공하지 못한 경우 `spot_out_`은 `NULL`로 유지되어야 한다. `created_out_`은
성공한 경우에만 의미가 있다.

## 6. `JoinSpot`과의 관계

이 API는 `CreateOrJoin` API가 아니다.

`GetOrNew`는 local `SpotNode` 안에서 logical spot을 확보하는 기능이다.
actor가 그 spot에 참가하는 것은 여전히 별도 단계다.

예상 사용 흐름은 아래와 같다.

1. 서버 코드가 `zlink_spot_node_spot_get_or_new()`로 방 spot을 확보한다.
2. `created_out = 1`이면 방의 초기 상태를 구성한다.
3. actor는 기존 join API로 해당 spot에 join 요청을 보낸다.
4. spot handler는 방이 가득 찼는지, 이미 시작했는지 같은 게임 규칙으로 join을
   수락하거나 거절한다.

이 경계를 유지해야 spot 확보 실패와 join 거절이 섞이지 않는다. 사용자는 "방을
확보하지 못한 것"과 "방에는 도달했지만 참가가 거절된 것"을 다르게 처리할 수
있어야 한다.

## 7. Remote 생성 범위

이 초안은 local `SpotNode` 안의 원자적 확보만 다룬다.

remote node에 있는 spot을 만들거나 가져오는 기능은 이 API에 포함하지 않는다.
remote 생성은 discovery, routing, 권한, 소유권 정책이 함께 필요하므로 별도
request/reply 프로토콜 또는 framework 계층에서 다루는 편이 맞다.

즉 이 API의 범위는 아래처럼 제한한다.

- 포함:
  - local node 안에서 특정 `spot_rid_`의 logical spot을 원자적으로 확보
  - 같은 logical spot에 대한 여러 facade handle 반환
  - 최초 생성 여부 반환
- 제외:
  - remote node에 spot 생성 명령 보내기
  - actor join 요청을 함께 수행하기
  - game room capacity, password, matchmaking 같은 애플리케이션 규칙 처리

## 8. 내부 구현 방향

구현은 기존 `spot_node_t`의 logical spot map을 기준으로 한다.

필요한 내부 helper는 per-spot creation queue와 logical spot state, facade handle을
함께 다루는 형태여야 한다. state만 반환하면 facade 생성 실패 rollback과 publish
시점을 한 곳에서 닫기 어렵다.

```c++
struct spot_get_or_new_result_t
{
    std::shared_ptr<spot_logical_state_t> state;
    void *spot;
    bool created;
};

int get_or_new_spot_facade (
  spot_node_t *node_,
  const zlink_routing_id_t *spot_rid_,
  spot_get_or_new_result_t *out_);
```

구현 규칙은 아래와 같다.

- `spot_rid_` 검증은 lookup과 같은 기준을 사용한다.
- 같은 `spot_rid_`의 호출은 per-spot creation queue에서 직렬 처리한다.
- `_handle_state.spots_by_rid` 조회와 publish는 같은 `_sync` lock 안에서 수행한다.
- 새 logical spot 후보는 처음부터 `spot_rid_`를 가진 상태로 준비한다.
- 새 logical spot 후보의 facade handle과 routed state를 먼저 준비한 뒤,
  `_sync` lock 안에서 같은 `spot_rid_`가 여전히 비어 있으면 publish한다.
- 같은 `spot_rid_`가 이미 publish되어 있으면 새 후보를 폐기하고 기존 logical
  spot에 대한 facade handle을 만든다.
- 기존 logical spot에 대해서는 `zlink_spot_node_spot_lookup()`과 같은
  `create_spot_facade()` 경로로 facade handle을 만든다.
- logical spot을 새로 만들고 facade handle 생성까지 성공했을 때만 spot owner
  READY summary를 발행한다.
- 새 logical spot 후보의 facade handle 생성이나 routed state 초기화가 실패하면
  후보를 publish하지 않고 실패한다.

기존 `zlink_spot_new()`처럼 임의 routing id로 만든 뒤
`update_spot_routing_id()`를 호출하는 구현은 피해야 한다. 그 방식은 중간 상태를
만들고, 같은 routing id를 두 caller가 동시에 확보하려는 경우를 API 내부에서
깔끔하게 닫기 어렵다.

## 9. C 바인딩 및 상위 바인딩 영향

`bindings/c/include/zlink.h`는 core 공개 헤더와 같은 함수 선언을 노출해야 한다.

core C API 이름은 기존 `zlink_spot_new()`와 맞추기 위해 `new`를 사용한다. 이
API는 "있으면 가져오고, 없으면 새로 만든다"는 의미이므로 C 함수 이름은
`zlink_spot_node_spot_get_or_new()`로 둔다.

상위 바인딩은 이 C API를 기준으로 자기 언어의 관용에 맞는 이름을 정한다.

- C++: `spot_node.get_or_create_spot(routing_id)` 계열
- .NET binding: `SpotNode.GetOrCreateSpot(RoutingId id)` 계열
- .NET framework: `IZLinkSpotManager.GetOrCreateAsync(...)` 계열

상위 바인딩에서 `Create`라는 단어를 쓰는 것은 언어 관용에 따른 선택이다. 다만
C API의 직접 대응 함수나 C 스타일 wrapper에서는 기존 `zlink_spot_new()`와
맞춰 `get_or_new`를 우선 사용한다.

상위 바인딩은 lookup과 new를 따로 조합해서 같은 의미를 흉내 내면 안 된다.
그렇게 하면 core가 제공하는 원자성 계약을 잃는다.

## 10. 회귀 테스트 계획

구현 시 아래 테스트를 함께 추가해야 한다.

### 10.1 Core 회귀 테스트

core 테스트는 `core/tests/integration/test_spot_actor_dispatch.cpp` 또는 별도
SpotNode C API 테스트 파일에 추가한다. 테스트가 actor join과 섞이면 목적이
흐려지므로, 기본 검증은 actor 없이 `SpotNode`와 `Spot` handle만 사용한다.

필수 테스트는 아래와 같다.

1. 새 routing id로 호출하면 `created_out = 1`이고 snapshot에 해당 spot이 보인다.
2. 같은 routing id로 다시 호출하면 `created_out = 0`이고 snapshot row는 하나만
   유지된다.
3. 두 facade handle은 같은 `spot_rid`를 가진다.
4. 여러 thread가 같은 routing id로 동시에 호출해도 생성 결과는 하나만
   `created_out = 1`이다.
5. 잘못된 인자는 기존 config error 정책에 맞게 실패하고 `spot_out_`은 `NULL`로
   유지된다.
6. 반환된 facade handle을 모두 destroy하면 기존 non-entry spot 수명 규칙에 따라
   logical spot이 제거된다.
7. 새 logical spot 생성 후 facade 생성에 실패하면 logical spot map에 해당
   `spot_rid`가 남지 않는다. 이 실패는 테스트 전용 failure injection이나 같은
   수준의 deterministic hook으로 검증한다.
8. 새 logical spot 후보가 facade 생성에 실패하는 동안 동시에 들어온 같은
   `spot_rid` 호출이 `created_out = 0`으로 성공하지 않는다.
9. 같은 `spot_rid`의 동시 호출은 per-spot creation queue에서 직렬 처리되고, 다른
   `spot_rid`의 호출은 서로 막지 않는다.

4번 테스트는 이 API의 존재 이유를 검증하는 핵심 테스트다. 인위적인 지연을 넣어
성공하게 만드는 테스트가 아니라, 동시에 호출해도 map 삽입이 하나로 닫히는지
확인해야 한다.

실행 명령은 아래 순서를 기본으로 한다.

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

전체 core 테스트가 너무 오래 걸리는 경우에도, 구현 직후에는 최소한 새 테스트가
들어간 test binary를 직접 실행하고, 마지막 검증 단계에서 `ctest`를 실행한다.

### 10.2 C 바인딩 회귀 테스트

C 바인딩은 public header와 link 계약을 확인해야 한다.

필수 테스트는 아래와 같다.

1. `bindings/c/include/zlink.h`만 include한 C 테스트가
   `zlink_spot_node_spot_get_or_new(...)`를 컴파일하고 링크한다.
2. 새 routing id 호출은 `created_out = 1`을 반환한다.
3. 같은 routing id 재호출은 `created_out = 0`을 반환한다.
4. `zlink_spot_node_spots_snapshot(...)` 기준으로 해당 routing id row가 하나만
   남는다.

### 10.3 상위 바인딩 회귀 테스트

상위 바인딩은 lookup과 new를 조합하지 않고 새 C API를 직접 호출하는지 확인한다.

필수 테스트는 아래 범위를 포함한다.

- C++:
  - `spot_node.get_or_create_spot(...)` wrapper가 새 C API를 호출한다.
  - 같은 routing id 재호출 시 같은 logical spot을 가리키고 최초 생성 여부를
    구분한다.
- .NET binding:
  - `ISpotNode.GetOrCreateSpot(...)` wrapper를 추가한다.
  - `NativeMethods.RequiredExports`에 새 export를 추가한다.
  - `SpotNode.GetOrCreateSpot(...)`이 새 C API를 직접 호출한다.
- Java, Node, Go, Rust, Python:
  - 각 바인딩의 SpotNode wrapper가 새 C API를 직접 호출한다.
  - 최소 smoke test로 새 호출과 기존 호출 경로가 같은 native library에서
    동작하는지 확인한다.

### 10.4 Framework 회귀 테스트

.NET framework는 core 원자성 위에 typed spot lifecycle을 추가하므로 별도 테스트가
필요하다.

필수 테스트는 아래와 같다.

1. 같은 `spotId`로 동시에 `GetOrCreateAsync(...)`를 호출해도
   `OnCreateAsync(...)`는 한 번만 호출된다.
2. `OnCreateAsync(...)`가 진행 중일 때 들어온 두 번째 caller는 같은 initializing
   entry를 기다린 뒤 같은 framework spot instance를 받는다.
3. `OnCreateAsync(...)` 또는 `OnInitializeAsync(...)`가 실패하면 같은 `spotId`로
   대기하던 caller 모두 같은 `SpotCreateFailed`를 관찰한다.
4. `OnCreateAsync(...)` 또는 `OnInitializeAsync(...)` 실패 후 다음 호출은 이전
   실패 entry를 재사용하지 않고 새로 생성을 시도한다.
5. core get-or-new가 `Created = false`를 반환했지만 framework registry에 기존
   entry가 없으면 framework는 충돌로 실패하고, 반환된 facade handle을 닫는다.
6. framework registry가 보관한 facade handle 때문에 caller가 받은 임시 handle을
   닫아도 core logical spot이 유지된다.
7. 최초 생성 요청이 넘긴 multipart create payload가 part 경계를 유지한 채
   `OnCreateAsync(...)`로 한 번만 전달된다.
8. 같은 `spotId`로 동시에 들어온 두 번째 요청의 create payload는 다시 전달되지
   않고, 두 번째 caller는 첫 번째 초기화 결과만 관찰한다.
9. `OnCreateAsync(...)` 또는 `OnInitializeAsync(...)` 실패 후 다음 호출이 다른
   create payload로 다시 시도하면
   새 payload가 새 `OnCreateAsync(...)` 호출에 전달된다.
10. 같은 `spotId`에 대해 서로 다른 `spotName`으로 생성 또는 확보를 시도하면
    `SpotTypeMismatch`로 실패하고 새 `OnCreateAsync(...)`를 호출하지 않는다.
11. `CreateAsync(spotName)`처럼 create payload가 없는 생성도 빈 multipart payload로
    `OnCreateAsync(...)`를 한 번 호출한다.

## 11. Core 빌드와 binding native library 배포 계획

core 변경 후 바인딩 테스트를 실행하기 전에 local core library를 바인딩 native
디렉토리로 배포해야 한다.

기본 순서는 아래와 같다.

```bash
cmake --build core/build
bindings/dev_sync_local_core_libs.sh
```

`bindings/dev_sync_local_core_libs.sh`는 `core/build/lib`의 `libzlink.so*`를 각
바인딩의 local native directory로 복사한다. 이 파일들은 local 개발 산출물이므로
커밋 대상이 아니다.

검증 후 staging 전에 아래를 확인한다.

```bash
git status --short bindings
```

`bindings/*/native/libzlink.so*`, `bindings/*/prebuilds/*/libzlink.so*`,
`bindings/*/runtimes/*/native/libzlink.so*` 같은 sync 산출물이 보이면 커밋에서
제외한다. 필요하면 script 출력에 적힌 restore 명령으로 되돌린다.

## 12. Binding 적용 계획

바인딩 적용은 core API가 빌드되고 C 테스트가 통과한 뒤 진행한다.

### 12.1 C

- `bindings/c/include/zlink.h`에
  `zlink_spot_node_spot_get_or_new(...)` 선언을 추가한다.
- C sample 또는 contract test에서 새 함수의 compile/link/runtime 경로를 확인한다.

### 12.2 C++

- `spot_node` wrapper에 `get_or_create_spot(routing_id)`를 추가한다.
- wrapper 내부는 반드시 새 C API를 직접 호출한다.

### 12.3 .NET binding

- `ISpotNode.GetOrCreateSpot(...)`에 명시적 routing id 기반 wrapper를 추가한다.
- `NativeMethods.Service.cs`에 P/Invoke 선언을 추가한다.
- `NativeMethods.Core.cs`의 required export 목록에
  `zlink_spot_node_spot_get_or_new`를 추가한다.
- 기존 `CreateSpot()`은 기존 의미를 유지한다.
- 새 wrapper는 lookup 후 new를 조합하지 않고 새 C API를 직접 호출한다.

### 12.4 Java, Node, Go, Rust, Python

- 각 언어의 SpotNode wrapper에 같은 의미의 API를 추가한다.
- 이름은 아래처럼 맞춘다.
  - Java: `getOrCreateSpot`
  - Node: `getOrCreateSpot`
  - Go: `GetOrCreateSpot`
  - Rust: `get_or_create_spot`
  - Python: `get_or_create_spot`
- 각 언어 문서에는 core C API와의 대응 관계를 명시한다.
- 최소 테스트는 "첫 호출 created, 두 번째 호출 existing"을 확인한다.

## 13. Framework 적용 계획

framework는 core C API 이름을 그대로 노출하지 않아도 된다. 사용자 관점에서는
`GetOrCreate`가 더 자연스럽다.

.NET framework 적용 방향은 아래와 같다.

- `IZLinkSpotManager`에 명시적 id 기반 `GetOrCreateAsync(...)`를 추가한다.
- 기존 `CreateAsync(string spotName)`은 "새 spot 생성" 의미로 유지한다.
- 기존 `CreateAsync(string spotName, RoutingId spotRid)` 같은 explicit routing id
  생성 API는 public surface에서 제거하고 `GetOrCreateAsync(...)`로 대체한다.
  explicit routing id 경로는 get-or-new 의미를 가지므로 `CreateAsync` overload로
  남기지 않는다.
- framework 구현은 .NET binding의 새 wrapper를 사용한다.
- `GetOrCreateAsync(...)`가 반환하는 `Created` 값은 core의 `created_out_`에서
  직접 온 값이어야 한다.

Spot registration과 `OnCreate` payload 처리는 framework 영역으로 둔다.
core C API에는 registration, DI, typed payload 해석을 넣지 않는다.

framework는 별도 생성 요청 queue를 두지 않는다. core가 per-spot creation queue로
logical spot 생성을 직렬화하므로, framework는 typed spot 초기화 상태만 관리한다.
이를 위해 framework registry는 `spotId -> initialization state`를 가진다.

### 13.1 Framework create payload 계약

framework의 spot 생성 요청은 multipart create payload를 받을 수 있어야 한다.
이 payload는 방 설정, 초기 seed, 접근 정책처럼 spot이 처음 만들어질 때 한 번만
해석해야 하는 값을 담는다.

생성 요청에는 framework가 spot factory를 고를 수 있는 type discriminator도 함께
있어야 한다. 이 초안에서는 그 discriminator를 `spotName`으로 본다. `spotName`은
등록된 spot factory key이며, wire 요청에 CLR class name이나 assembly-qualified type
name을 싣는다는 뜻이 아니다. 실제 .NET type은 수신 framework node의 registration
table이 `spotName`으로 결정한다.

framework public surface는 아래 의미를 제공한다.

```csharp
public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        string spotName,
        ZLinkSpotId spotId,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpot
{
    ValueTask OnCreateAsync(
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken = default);
}
```

정확한 overload 구성은 .NET framework spec에서 확정한다. 다만 계약은 아래처럼
고정한다.

- `CreateAsync(spotName, ...)`처럼 create payload를 받지 않는 편의 overload는
  빈 multipart payload를 넘긴 것과 같다. 새 spot이 만들어지면
  `OnCreateAsync(Array.Empty<Message>(), ...)`를 한 번 호출한다.
- `CreateAsync(spotName, createParts, ...)`는 framework가 새 `spotId`를 발급하고,
  `spotName`으로 factory를 선택한 뒤, 생성에 성공한 spot의
  `OnCreateAsync(createParts, ...)`를 호출한다.
- `GetOrCreateAsync(spotName, spotId, createParts, ...)`는 명시적 `spotId`를
  확보한다. 이번 요청이 framework spot을 실제로 만든 경우에만 `spotName`으로
  factory를 선택하고 `OnCreateAsync(createParts, ...)`를 호출한다.
- 이미 ready 상태인 framework spot이 있으면 `Created = false`를 반환하고
  새 요청의 `createParts`는 전달하지 않는다. ready spot의 `spotName`이 요청의
  `spotName`과 다르면 `SpotTypeMismatch`로 실패한다.
- initializing 상태인 framework spot이 있으면 새 요청의 `createParts`는 보류하지
  않는다. caller는 첫 생성 요청의 `OnCreateAsync(...)` 완료를 기다린다. 이때도
  initializing entry의 `spotName`과 요청의 `spotName`이 다르면 `SpotTypeMismatch`로
  실패한다.
- `OnCreateAsync(...)`가 받는 part 수와 순서는 caller가 넘긴 multipart create
  payload와 같아야 한다.
- framework는 multipart part를 하나의 serialized envelope로 합치지 않는다.
- `createParts` 자체는 `null`이면 안 된다. payload가 없는 생성은 빈 list로
  표현한다.
- `OnCreateAsync(...)`가 실패하면 해당 생성 시도는 실패한다. 다음 생성 시도는
  자기 요청의 create payload로 다시 `OnCreateAsync(...)`를 호출할 수 있다.

`OnCreateAsync(...)`는 생성 payload를 받아 spot 내부 상태로 해석하는 callback이다.
payload를 장기 보관해야 하면 spot은 callback 안에서 필요한 값으로 복사하거나
decode해야 한다.

### 13.2 Framework create request 전송 계약

framework 서버끼리 생성 요청을 보낼 때는 type discriminator와 multipart payload를
분리해서 보낸다.

wire 요청의 framework metadata에는 최소한 아래 값이 들어간다.

- operation: `spot.get-or-create` 또는 같은 의미의 내부 operation id
- `spotName`: 수신 node에서 factory를 고르는 framework type discriminator
- `spotId`: 확보하려는 logical spot id. 새 id를 수신 node가 발급하는 create 요청이면
  비울 수 있다.

metadata 뒤의 message part들은 create payload다. framework는 이 part들을 순서대로
`OnCreateAsync(createParts, ...)`에 전달한다. `spotName`을 create payload 안에
넣으면 안 된다. payload codec이 바뀌면 factory 선택 자체를 읽을 수 없게 되기
때문이다.

CLR type name, assembly-qualified type name, source node의 local class name은 wire
요청에 싣지 않는다. 수신 node의 registration table이 `spotName`을 실제 .NET type으로
매핑한다.

### 13.3 Framework 오류 계약

Spot 생성/확보 실패는 actor 생성 오류와 같은 `ZLinkFrameworkException` 계열로
분류한다.

- `SpotCreateFailed`
  - factory resolve, spot activation, `OnCreateAsync(...)`,
    `OnInitializeAsync(...)` 중 실패한 경우 사용한다.
  - retry 가능 여부는 원인에 따라 `IsRetriable`로 표현한다.
- `SpotTypeMismatch`
  - 같은 `spotId`에 대해 기존 framework entry의 `spotName`과 요청의 `spotName`이
    다른 경우 사용한다.
  - 이 경우 새 `OnCreateAsync(...)`는 호출하지 않는다.

framework의 예상 흐름은 아래와 같다.

1. 등록된 spot factory를 찾는다.
2. framework registry에서 `spotId`를 조회한다.
3. 완료된 entry가 있으면 그 framework spot instance를 즉시 반환한다.
   단, entry의 `spotName`이 요청의 `spotName`과 다르면 `SpotTypeMismatch`로
   실패한다.
4. initializing entry가 있으면 그 initialization이 끝날 때까지 기다린 뒤 같은
   framework spot instance를 반환한다. 이때도 entry의 `spotName`이 요청의
   `spotName`과 다르면 기다리지 않고 `SpotTypeMismatch`로 실패한다.
5. entry가 없으면 initializing entry를 먼저 등록한다.
6. initializing entry를 새로 등록한 caller만 core get-or-new를 호출하고, 반환된 facade
   handle을 entry가 보관한다.
7. core get-or-new 결과가 `Created = true`이면 framework가 spot instance를 만든다.
   lifecycle 순서는 `Configure()`, descriptor bind,
   `OnCreateAsync(createParts, cancellationToken)`, `OnInitializeAsync(...)`다.
   첫 생성 요청의 multipart create payload는 `OnCreateAsync(...)`로 한 번만
   전달한다.
8. core get-or-new 결과가 `Created = false`이면 충돌로 실패한다. 이 경우 framework는
   반환된 facade handle을 닫고 initializing entry를 제거한다. framework registry에는
   해당 `spotId`의 entry가 없었는데 core logical spot만 이미 있다는 뜻이므로,
   외부에서 만들어진 logical spot을 typed framework spot으로 임의 attach하지 않는다.
   core에는 `spotName`이나 factory type 정보가 없고, framework registry에도 typed
   entry가 없으므로 안전하게 초기화된 framework spot이라고 볼 수 없기 때문이다.
9. initialization이 성공하면 entry를 ready 상태로 전환하고, 대기 caller는
   같은 framework spot instance를 받는다.
10. actor join은 별도 `JoinSpot(...)` 흐름으로 처리한다.

framework registry entry는 core facade handle에 대한 strong reference를 유지한다.
그렇지 않으면 caller가 받은 facade handle을 닫는 순간 non-entry logical spot의
마지막 facade가 사라져 core logical spot이 제거될 수 있다.

`OnCreateAsync(...)` 또는 `OnInitializeAsync(...)` 실패 시 rollback 규칙은 아래와
같다.

- initializing entry를 failed 상태로 전환한다.
- entry가 보관한 facade handle을 닫는다.
- core logical spot이 더 이상 facade를 가지지 않으면 기존 non-entry 수명 규칙에
  따라 제거된다.
- 같은 `spotId`의 대기 caller에게는 동일한 `SpotCreateFailed`를 전파한다.
- 실패한 entry는 registry에서 제거해서 다음 호출이 다시 생성을 시도할 수 있게
  한다.

`OnCreate` callback과 join callback은 섞지 않는다. 생성 multipart payload는 방
초기 설정에 쓰이고, join payload는 참가 요청 검증에 쓰인다.

## 14. Sample 적용 계획

샘플은 framework API가 정리된 뒤 반영한다.

### 14.1 Bingo sample

4인 Bingo sample은 get-or-new/get-or-create 사용 사례를 보여 주는 대표 샘플로
사용한다.

- API 또는 Play server가 room id를 결정한다.
- room spot은 `IZLinkSpotManager.GetOrCreateAsync(..., createParts, ...)`로
  확보한다.
- 첫 생성 요청은 Bingo room 설정을 multipart create payload로 넘기고,
  `BingoRoomSpot.OnCreateAsync(...)`는 그 payload를 해석해 room state를 만든다.
- player actor는 이후 `JoinSpot(...)`으로 room에 참가한다.
- 방이 가득 찼거나 이미 시작된 경우는 spot join handler가 거절한다.

### 14.2 TicTacToe sample

TicTacToe sample은 기존 흐름을 깨지 않는 선에서만 반영한다.

- 방 id가 명시적으로 필요한 경로가 있으면 `GetOrCreateAsync(...)`를 사용한다.
- 단순 single-room 또는 미리 생성된 spot을 쓰는 경로는 불필요하게 바꾸지 않는다.
- sample 문서는 "생성/확보"와 "join"이 별도 단계임을 설명한다.

## 15. 문서 적용 계획

구현 전에는 이 draft 문서만 공개되지 않은 설계 근거로 유지한다.

구현 후에는 아래 문서에 나누어 반영한다.

- core C API contract:
  - `doc/spec/`의 Spot C API 계약 문서에 함수 시그니처, 반환값, errno, 수명
    규칙을 반영한다.
- core guide:
  - `doc/guide/07-3-spot.ko.md`에 "명시적 room id를 가진 spot 확보" 사용법을
    추가한다.
- internals:
  - `doc/internals/services-internals.ko.md` 또는 SpotNode 내부 구조 문서에
    logical spot map의 원자적 get-or-new 규칙을 설명한다.
- binding docs:
  - 각 바인딩 문서에 C API 대응 이름과 언어별 wrapper 이름을 적는다.
- framework docs:
  - `framework/doc/spec/draft/framework-adapter/bindings/dotnet/aspnet-core-spot.ko.md`
    또는 관련 spot 문서에 `IZLinkSpotManager.GetOrCreateAsync(...)`를 반영한다.
  - Bingo sample draft에는 room 생성/확보와 join 단계 분리를 반영한다.

정식 spec에는 사용 예시보다 계약을 우선 둔다. 게임 방 예시는 guide나 framework
sample 문서에 둔다.

## 16. 구현 순서

구현은 아래 순서로 진행한다.

1. core C API와 내부 helper를 구현한다.
2. 새 logical spot 생성 후 facade 생성 실패 rollback 테스트를 먼저 추가한다.
3. core 회귀 테스트를 추가하고 `cmake --build core/build`, `ctest`로 검증한다.
4. `bindings/dev_sync_local_core_libs.sh`로 local core library를 바인딩 native
   디렉토리에 배포한다.
5. C 바인딩 header/test를 맞춘다.
6. .NET binding을 먼저 맞춘다. framework와 sample이 이 경로에 의존하기 때문이다.
7. 나머지 바인딩을 같은 계약으로 맞춘다.
8. .NET framework `IZLinkSpotManager`와 runtime 구현을 맞춘다.
9. framework error kind에 `SpotCreateFailed`, `SpotTypeMismatch`를 추가한다.
10. framework initialization registry, multipart create payload 전달,
    create lifecycle 실패 전파 테스트를 추가한다.
11. Bingo sample draft와 실제 sample 구현을 맞춘다.
12. 문서를 정식 위치에 나누어 반영한다.
13. 전체 관련 테스트를 다시 실행하고, sync된 native library 산출물은 커밋에서
    제외한다.
