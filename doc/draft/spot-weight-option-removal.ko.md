[스펙 목차](../README.ko.md)

# Draft -- Spot Weight Option Removal

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 남아 있는 API나 상수의
> 최종 유지 여부를 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 이미 코드와 문서 일부에 들어간 Spot/SpotNode weight 설정 표면을 제거하기
위한 계획을 정리한다.

peer weight의 원래 목적은 `DEALER`가 여러 peer 중 다음 메시지를 보낼 대상을 고를
때, 각 peer가 광고한 처리 비율을 라우팅 입력값으로 쓰는 것이다. 이 값은 raw socket
peer 사이에서 전파되는 수신 가중치이며, service/Spot 객체 자체의 설정값이 아니다.

따라서 `SpotNode`와 `Spot`에 별도 weight 설정 옵션을 두면 사용자가 service 객체의
라우팅 정책을 직접 조정할 수 있다고 오해하기 쉽다. 이 초안은 그 혼선을 줄이기 위해
Spot/SpotNode weight 설정 옵션을 공개 표면에서 제거한다.

## 2. 제거 대상

제거 대상은 아래 공개 옵션 상수와 해당 typed option 매핑이다.

| 제거 대상 | enum | 함수 표면 |
|-----------|------|----------|
| `ZLINK_SPOT_NODE_OPT_WEIGHT` | `zlink_spot_node_option_t` | `zlink_set_spot_node_option()`, `zlink_get_spot_node_option()` |
| `ZLINK_SPOT_OPT_WEIGHT` | `zlink_spot_option_t` | `zlink_set_spot_option()`, `zlink_get_spot_option()` |

아래 표면도 함께 제거한다.

- 바인딩의 `SpotNodeOptions.weight`
- 바인딩의 `SpotOptions.weight`
- guide와 site 문서의 Spot/SpotNode weight 설정 예제
- internals 문서에서 Spot/SpotNode option setter로 weight를 조정한다고 설명하는 문장
- 해당 옵션을 검증하는 typed option 테스트

## 3. 남길 표면

Spot/SpotNode weight 설정 옵션을 제거하더라도 peer weight 자체를 모두 제거하는 것은
아니다.

남겨야 하는 표면은 아래와 같다.

- `ZLINK_ROUTER_OPT_WEIGHT`
- `ZLINK_DEALER_OPT_WEIGHT`
- `zlink_spot_node_peer_entry_t.weight`
- `zlink_member_peer_entry_t.weight`
- peer weight monitor event
- discovery, registry, Spot peer cache가 peer의 remote weight를 보여주는 조회 표면

즉 service/Spot 쪽에서는 weight를 **설정**하지 않는다. 다만 raw socket peer가
광고한 weight를 snapshot이나 query 결과에서 **조회**할 수는 있다.

## 4. 동작 기준

삭제 뒤 사용자는 아래 방식으로만 weight를 설정한다.

- `ROUTER`가 자신에게 들어오는 새 메시지 비율을 낮추려면 `ZLINK_ROUTER_OPT_WEIGHT`를
  설정한다.
- worker 역할의 `DEALER`가 자신에게 들어오는 새 메시지 비율을 낮추려면
  `ZLINK_DEALER_OPT_WEIGHT`를 설정한다.
- `SpotNode`와 `Spot`에는 weight 설정 옵션이 없다.

`DEALER` outbound 선택은 peer가 광고한 remote weight를 본다. 이 규칙은
`peer-weight.ko.md`의 라우팅 모델을 따른다.

## 5. 구현 계획

이 변경은 한 번의 작업 단위로 끝낸다. 작업자는 중간에 별도 승인이나 설계 결정을
요청하지 않고, 아래 순서대로 코드, 바인딩, 문서, 테스트를 함께 갱신한다. 이 문서에
명시된 기준과 실제 코드가 충돌하면, 이 문서의 목표 계약을 우선한다.

작업 시작 전에 아래 검색으로 현재 남아 있는 제거 대상을 모두 확인한다.

```sh
rg -n \
  "ZLINK_SPOT_NODE_OPT_WEIGHT|ZLINK_SPOT_OPT_WEIGHT|SpotNodeOptions\\.weight|SpotOptions\\.weight|spot_subject_(set|get)_weight|zlink_spot_subject_(set|get)_weight_internal|zlink_service_spot_(set|get)_weight_internal|zlink_service_(set|get)_weight|set_weight \\(|get_weight \\(" \
  core bindings doc/spec doc/guide doc/internals doc/site
```

검색 결과 중 raw socket `ROUTER`/`DEALER` weight, peer entry `weight`, registry
provider `weight`, discovery provider `weight`는 제거 대상이 아니다. 검색 결과는
아래 기준으로 처리한다.

- Spot/SpotNode 객체에서 사용자가 weight를 **설정하거나 조회하는 API**이면 제거한다.
- peer나 provider의 remote weight를 **조회 결과로 보여주는 필드**이면 유지한다.
- raw socket `ROUTER`/`DEALER`의 local peer weight이면 유지한다.

### 5.1 공개 헤더와 옵션 매핑

수정 대상:

- `core/include/zlink_enum.h`
- `core/include/zlink.h`
- `core/src/api/service_surface_internal.hpp`
- `core/src/api/service_option_api.cpp`
- `core/src/api/service_option_spot_api.cpp`
- `core/src/api/zlink_option_specialized_api.cpp`
- `core/src/services/spot/spot_subject_access.hpp`
- `core/src/services/spot/spot_subject_query.cpp`
- `core/src/services/spot/spot_node_handles.cpp`
- `bindings/go/include/zlink_enum.h`

작업 내용:

- `ZLINK_SPOT_NODE_OPT_WEIGHT`를 `zlink_spot_node_option_t`에서 제거한다.
- `ZLINK_SPOT_OPT_WEIGHT`를 `zlink_spot_option_t`에서 제거한다.
- Spot/SpotNode typed option setter/getter에서 weight 분기를 제거한다.
- Spot/SpotNode option 표면 전용으로 쓰이던 `spot_subject_set_weight()`와
  `spot_subject_get_weight()`를 제거하고, 해당 함수를 호출하던 경로도 함께 제거한다.
- `zlink_spot_subject_set_weight_internal()`과
  `zlink_spot_subject_get_weight_internal()`은 Spot/SpotNode weight 설정 표면만을
  위한 내부 API이므로 제거한다.
- `zlink_service_spot_set_weight_internal()`과
  `zlink_service_spot_get_weight_internal()`도 같은 이유로 제거한다.
- `zlink_service_set_weight()`와 `zlink_service_get_weight()`를 제거하고, 해당
  wrapper를 호출하던 Spot/SpotNode option 경로도 함께 제거한다.
- 제거된 옵션을 사용하면 컴파일 단계에서 더 이상 공개 상수로 잡히지 않게 한다.

### 5.2 내부 상태 정리

수정 대상:

- `core/src/services/spot/*`
- `core/src/services/discovery/*`

작업 내용:

- Spot/SpotNode 객체의 로컬 weight 설정 상태를 제거한다.
- SpotNode가 discovery에 자신을 등록할 때 쓰던 로컬 provider weight는 기본값
  `100`으로 고정한다. 이 값은 service 객체의 설정값이 아니라 "별도 감산 신호가
  없다"는 기본 advertised weight다.
- SpotNode의 discovery 등록 weight를 나중에 raw socket weight와 연동하려면 이
  초안과 별도 설계에서 다룬다. 이번 제거 작업에서는 새 연동 규칙을 만들지 않는다.
- SpotNode provider weight를 runtime에 갱신하던 경로가 Spot/SpotNode weight option
  때문에만 존재했다면 함께 제거한다.
- `spot_node_t::set_weight()`, `spot_node_t::get_weight()`, `_weight` 필드는
  제거한다. discovery 등록 경로는 `_weight` 대신 literal 또는 상수화된 기본값
  `100`을 넘긴다.
- `discovery_owned_service::update_attributes(..., weight)` 호출이 SpotNode local
  weight 변경을 전파하기 위한 용도로만 쓰였다면 해당 호출 경로를 제거한다. registry
  provider 조회와 remote weight 저장을 위한 discovery 내부 weight 필드는 유지한다.
- peer snapshot, registry member query, Spot peer cache의 remote weight 저장은
  유지한다.
- admission state를 대체하는 조회 필드는 `weight`로 유지한다.
- 제거 뒤 SpotNode가 discovery에 등록하는 provider weight는 항상 `100`이어야 한다.
  registry query와 Spot peer cache에서 보이는 기본 provider weight도 이 값을
  기준으로 검증한다.

### 5.3 테스트 정리

수정 대상:

- `core/tests/unittest/unittest_typed_option.cpp`
- `core/tests/unittest/*`
- `core/tests/integration/*`
- `core/tests/e2e/spot/*`

작업 내용:

- `ZLINK_SPOT_NODE_OPT_WEIGHT` setter/getter 테스트를 제거한다.
- `ZLINK_SPOT_OPT_WEIGHT` setter/getter 테스트를 제거한다.
- `ZLINK_ROUTER_OPT_WEIGHT`와 `ZLINK_DEALER_OPT_WEIGHT` 테스트는 유지한다.
- Spot/registry query 결과에 peer weight가 채워지는 테스트는 유지한다.
- SpotNode discovery provider weight가 기본 `100`으로 유지되는 테스트가 없으면
  추가한다. 기존 테스트가 같은 결과를 이미 검증하면 새 테스트를 만들지 않는다.
- 제거된 상수를 참조하는 테스트는 삭제하거나, 해당 상수가 공개 표면에 없다는
  contract 성격의 컴파일 테스트로 바꾼다.

### 5.4 바인딩 코드 반영

수정 대상:

- `bindings/cpp/include/zlink/types.hpp`
- `bindings/cpp/include/zlink/services/*`
- `bindings/cpp/tests/contract/*`
- `bindings/go/include/zlink_enum.h`
- `bindings/go/*`
- `bindings/dotnet/src/Zlink/**/*`
- `bindings/java/src/main/java/dev/kairoscode/zlink/**/*`
- `bindings/node/native/src/**/*`
- `bindings/python/src/zlink/**/*`
- `bindings/python/tests/**/*`
- `bindings/rust/src/**/*`
- `bindings/rust/include/zlink.h`
- `bindings/rust/tests/**/*`

작업 내용:

- 각 바인딩에서 `SpotNodeOptions.weight`, `SpotOptions.weight` 또는 같은 의미의
  typed option facade를 제거한다.
- 각 바인딩의 generated 또는 vendored C header 복사본에서
  `ZLINK_SPOT_NODE_OPT_WEIGHT`와 `ZLINK_SPOT_OPT_WEIGHT`를 제거한다.
- Spot/SpotNode weight setter/getter wrapper와 native binding entry point를
  제거한다.
- Spot peer entry, registry member entry, discovery provider entry의 `weight`
  조회 필드는 유지한다. 이 필드는 설정 옵션이 아니라 remote peer 상태 조회값이다.
- `RouterSocketOptions.weight`와 `DealerSocketOptions.weight`는 유지한다.
- 삭제된 Spot/SpotNode weight API를 확인하던 contract, alignment, surface 테스트는
  제거하거나 "존재하지 않아야 한다"는 테스트로 바꾼다.
- 바인딩별 public enum 또는 option facade에서 Spot/SpotNode weight 이름이 자동 생성
  파일에 다시 생기지 않도록 generator 입력과 생성 결과를 함께 수정한다.
- 바인딩이 core header 복사본을 들고 있으면 `core/include/zlink.h`와
  `core/include/zlink_enum.h`의 제거 결과를 같은 작업에서 반영한다.

### 5.5 문서와 사이트 반영

문서 반영은 코드 제거와 같은 작업 단위에서 끝낸다. 정식 spec, guide, internals,
site 문서에 Spot/SpotNode weight 설정 설명이 남아 있으면 구현과 공개 계약이
서로 충돌하기 때문이다.

작업자는 아래 검색 결과를 모두 처리한다.

```sh
rg -n \
  "ZLINK_SPOT_NODE_OPT_WEIGHT|ZLINK_SPOT_OPT_WEIGHT|SpotNodeOptions\\.weight|SpotOptions\\.weight|SpotNode.*weight 설정|Spot.*weight 설정|set_spot_node_option\\([^\\n]*WEIGHT|set_spot_option\\([^\\n]*WEIGHT" \
  doc/spec doc/guide doc/internals doc/site
```

처리 기준은 아래와 같다.

- Spot/SpotNode option 표, 예제, guide 문장에서는 해당 설정 표면을 삭제한다.
- peer entry나 registry query의 `weight` 필드는 remote peer 상태 조회값으로
  설명한다.
- service/Spot에서 부하 감소나 drain을 설명해야 하면 Spot/SpotNode option 대신 raw
  socket `ROUTER` 또는 worker `DEALER`의 weight 설정으로 안내한다.
- site 문서가 정식 문서의 복사본이면 같은 의미로 함께 수정한다.

## 6. 문서 반영 계획

### 6.1 정식 spec

수정 대상:

- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/bindings/README.md`
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`
- `doc/site/docs/api/spot.ko.md`
- `doc/site/docs/api/spot.md`
- `doc/site/docs/api/bindings.md`

작업 내용:

- Spot/SpotNode option 표에서 `ZLINK_SPOT_NODE_OPT_WEIGHT`와
  `ZLINK_SPOT_OPT_WEIGHT`를 제거한다.
- service/Spot에서 weight를 설정할 수 있다고 설명하는 문장을 제거한다.
- peer entry의 `weight` 필드는 remote peer 상태 조회값이라고 설명한다.

### 6.2 guide

수정 대상:

- `doc/guide/07-0-services.ko.md`
- `doc/guide/07-0-services.md`
- `doc/guide/07-3-spot.ko.md`
- `doc/guide/07-3-spot.md`
- `doc/site/docs/guide/07-0-services.ko.md`
- `doc/site/docs/guide/07-0-services.md`
- `doc/site/docs/guide/07-3-spot.ko.md`
- `doc/site/docs/guide/07-3-spot.md`

작업 내용:

- Spot/SpotNode weight 설정 예제를 제거한다.
- service drain이나 부하 감소 예시에서 Spot/SpotNode option을 쓰지 않는다.
- 필요하면 raw socket `ROUTER` 또는 worker `DEALER`의 weight 설정으로 안내한다.

### 6.3 internals

수정 대상:

- `doc/internals/services-internals.ko.md`
- `doc/internals/services-internals.md`
- `doc/internals/spot-internals.ko.md`
- `doc/internals/spot-internals.md`
- `doc/site/docs/internals/services-internals.md`
- `doc/site/docs/internals/services-internals.ko.md`
- `doc/site/docs/internals/spot-internals.ko.md`
- `doc/site/docs/internals/spot-internals.md`

작업 내용:

- `zlink_set_spot_node_option(..., ZLINK_SPOT_NODE_OPT_WEIGHT, ...)`로 값을 바꾼다는
  설명을 제거한다.
- Spot/registry/cache가 remote weight를 저장하고 보여주는 흐름만 남긴다.
- 라우팅 비율 결정은 `DEALER` load balancer의 책임이라고 설명한다.

## 7. 호환성 기준

이번 변경은 호환성 유지 단계를 두지 않는다. 제거 뒤에는 기존 상수를 사용하는 C
코드가 컴파일되지 않아야 한다.

바인딩도 같은 기준을 따른다. `SpotNodeOptions.weight`와 `SpotOptions.weight`는
deprecated 단계 없이 제거한다. 이미 배포된 외부 버전을 고려해야 하는 경우에는 이
초안을 구현하기 전에 별도 호환성 정책을 먼저 정해야 한다.

## 8. 자동 검증 절차

작업자는 구현을 마친 뒤 아래 검증을 순서대로 실행한다. 특정 언어 도구가 로컬에
없으면 그 항목만 실패 사유와 함께 기록하고, 나머지는 계속 실행한다.

### 8.1 필수 검색 검증

아래 검색은 제거 대상이 남아 있으면 실패로 본다.

```sh
rg -n \
  "ZLINK_SPOT_NODE_OPT_WEIGHT|ZLINK_SPOT_OPT_WEIGHT|SpotNodeOptions\\.weight|SpotOptions\\.weight" \
  core bindings doc/spec doc/guide doc/internals doc/site
```

`doc/plan`과 그 아래 `logs`는 과거 실행 기록이므로 필수 제거 검색 대상에서 제외한다.
정식 spec, guide, internals, site 문서에는 제거 대상 이름이 남아 있으면 안 된다.

아래 검색은 결과를 확인한다. Spot/SpotNode 설정 API 경로가 남아 있으면 실패이고,
remote weight 조회 필드나 raw socket weight 경로만 남아 있으면 통과다.

```sh
rg -n "spot_subject_(set|get)_weight|zlink_spot_subject_(set|get)_weight_internal|zlink_service_spot_(set|get)_weight_internal|zlink_service_(set|get)_weight|set_weight \\(|get_weight \\(" core/src/api core/src/services/spot
rg -n "weight" core/src/services/discovery core/src/services/spot
```

### 8.2 core 빌드와 테스트

기존 build directory가 있으면 그것을 사용한다. 없으면 `core/build`를 만든다.

```sh
cmake -S core -B core/build -DBUILD_TESTS=ON
cmake --build core/build -j$(nproc)
ctest --test-dir core/build --output-on-failure
```

`ctest --test-dir core/build`가 테스트를 찾지 못하면 아래도 시도한다.

```sh
ctest --test-dir core/build/core --output-on-failure
```

### 8.3 바인딩 검증

가능한 바인딩 검증을 모두 실행한다.

```sh
ctest --test-dir core/build -L contract --output-on-failure
ctest --test-dir core/build -L sample-smoke --output-on-failure -j1
(cd bindings/python && python -m pytest -q)
(cd bindings/rust && cargo test)
(cd bindings/go && go test ./...)
(cd bindings/dotnet && dotnet test)
(cd bindings/node && npm test)
```

Java 바인딩은 저장소에 있는 빌드 도구를 따른다. `gradlew`가 있으면 아래를 실행한다.

```sh
(cd bindings/java && ./gradlew test)
```

`mvnw`가 있으면 아래를 실행한다.

```sh
(cd bindings/java && ./mvnw test)
```

### 8.4 완료 판정

아래 조건을 모두 만족해야 작업을 완료로 본다.

- `core/include/zlink_enum.h`와 바인딩 header 복사본에
  `ZLINK_SPOT_NODE_OPT_WEIGHT`, `ZLINK_SPOT_OPT_WEIGHT`가 없다.
- Spot/SpotNode typed option setter/getter에서 weight option 분기가 없다.
- `zlink_spot_subject_set_weight_internal()`,
  `zlink_spot_subject_get_weight_internal()`,
  `zlink_service_spot_set_weight_internal()`,
  `zlink_service_spot_get_weight_internal()`, `zlink_service_set_weight()`,
  `zlink_service_get_weight()`가 남아 있지 않다.
- Spot/SpotNode public binding surface에 weight 설정 API가 없다.
- `ZLINK_ROUTER_OPT_WEIGHT`, `ZLINK_DEALER_OPT_WEIGHT`는 그대로 남아 있다.
- peer entry, registry member entry, discovery provider entry의 `weight` 조회
  필드는 그대로 남아 있다.
- SpotNode discovery provider weight는 제거 뒤 기본 `100`으로 등록된다.
- 정식 문서, guide, internals, site 문서에 Spot/SpotNode weight 설정 예제가 없다.
- core build와 가능한 테스트가 통과한다. 실행하지 못한 테스트가 있으면 도구 부재나
  환경 제약을 명확히 기록한다.
