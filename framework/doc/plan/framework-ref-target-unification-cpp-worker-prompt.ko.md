# C++ Worker Prompt: actor_ref_t / spot_ref_t 전송 대상 통일

기준 문서: `framework/doc/plan/framework-ref-target-unification-plan.ko.md`

## 목표

C++ framework public contract에서 메시징 대상 개념을 `actor_ref_t` / `spot_ref_t`로 통일한다.
actor id 또는 spot id만 받아서 메시지를 보내는 API는 제거한다. id는 조회 입력이고, ref는 전송
입력이다.

## Naming 규칙

| 현재 이름 | 최종 이름 |
|-----------|-----------|
| `actor_ref_t` | `actor_ref_t` 유지 |
| `actor_ref_snapshot_t` | `actor_ref_snapshot_t` 유지 |
| `spot_address_t` | `spot_ref_t` |
| `spot_address_resolver_t` | `spot_ref_resolver_t` |

C++ public type은 `zlink::framework` namespace 안에서 snake_case + `_t`를 사용한다. 타입명에
`zlink_` prefix를 다시 붙이지 않는다.

## 제거 대상

```text
framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp
  actor_client_t::send_to_actor(std::string actor_id, ...)
  actor_client_t::request_to_actor(std::string actor_id, ...)
  actor_client_t::send_to_actor_erased(std::string actor_id, ...)
  actor_client_t::request_to_actor_erased(std::string actor_id, ...)

framework/languages/cpp/framework/include/zlink/framework/contracts/channels/channel.hpp
  route_client_t::send_to_node(router_channel_id, target_node_rid, target_spot_rid, ...)
  route_client_t::request_to_node(router_channel_id, target_node_rid, target_spot_rid, ...)

framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp
  request_to(node_rid_t node_rid, spot_rid_t spot_rid, ...)
  send_to(node_rid_t node_rid, spot_rid_t spot_rid, ...)
```

## 추가/변경 대상

- `spot_address_t`를 `spot_ref_t`로 변경한다.
- `spot_address_resolver_t`를 `spot_ref_resolver_t`로 변경한다.
- `resolve_spot_address`를 `resolve_spot_ref`로 변경한다.
- `resolve_actor_spot_address`를 `resolve_actor_spot_ref`로 변경한다.
- actor messaging API는 `const actor_ref_t&` 또는 값 이동 가능한 `actor_ref_t`를 받는다.
- spot messaging API는 `const spot_ref_t&` 또는 값 이동 가능한 `spot_ref_t`를 받는다.
- node rid와 spot rid를 낱개로 받는 public messaging API는 `spot.hpp`와 `channel.hpp` 양쪽에서
  제거한다.

## 주요 파일

```text
framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/channels/channel.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/locations/resolvers.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/locations/rows.hpp
framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp
framework/languages/cpp/framework/src/runtime/actors/actor_client.cpp
framework/languages/cpp/framework/src/runtime/actors/actor_gateway_runtime.cpp
framework/languages/cpp/framework/src/runtime/actors/actor_gateway_runtime.hpp
framework/languages/cpp/framework/src/runtime/channels/channel_outbound_exchange.cpp
framework/languages/cpp/framework/src/runtime/channels/route_channel_runtime.cpp
framework/languages/cpp/framework/src/runtime/locations/store_location_resolvers.hpp
framework/languages/cpp/framework/src/runtime/spots/spot_runtime.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_runtime.hpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_internal_dispatcher.cpp
framework/languages/cpp/framework/src/runtime/spots/spot_route_packets.cpp
framework/languages/cpp/extensions/framework-locations-redis/include/zlink/locations/redis.hpp
```

Sample/e2e 영향 범위:

```text
framework/languages/cpp/e2e/RegistrationCodec
framework/languages/cpp/e2e/SpotService
framework/languages/cpp/e2e/YieldDispatch
framework/languages/cpp/e2e/DeliveryDispatch
framework/languages/cpp/e2e/ToActorMessaging
framework/languages/cpp/samples/Bingo
framework/languages/cpp/samples/TicTacToe
framework/languages/cpp/samples/DeliveryDispatch
framework/languages/cpp/samples/SupportChat
framework/languages/cpp/samples/GameQuest
framework/languages/cpp/samples/ShoppingMall
```

## C++ E2E/sample gap 제거 연계

이 작업은 `framework/doc/plan/framework-cpp-e2e-sample-gap-closure-plan.ko.md`의 C++ 담당 작업과
같이 진행한다. ref 기반 전송 표면을 바꾸면 공통 E2E와 sample의 public API 사용 예시가 함께 바뀌므로,
아래 항목을 같은 작업 범위 안에서 확인한다.

- `framework/doc/framework/common/e2e`의 config-1~9 scenario가 C++에서 `implemented` 상태로 남아
  있는지 확인한다.
- `framework/doc/framework/common/sample`의 sample 요구사항이 C++ sample inventory와 runner evidence에
  반영되어 있는지 확인한다.
- sample 동작은 `.NET` 구현(`framework/languages/dotnet/samples`)을 포팅 기준으로 삼는다.
- `partial` 또는 `gap` 상태는 완료로 보지 않는다. ref target 통일 때문에 새 public API 공백이 드러나면
  private helper, raw frame, 테스트 전용 adapter로 우회하지 않고 spec/guide/draft 검토 항목으로 분리한다.
- `core/`는 수정하지 않는다. core 버그가 의심되면 C++에서 우회하지 말고 다른 언어 재현 여부를 확인한 뒤
  별도 버그 리포트로 분리한다.

## 테스트

추가/수정해야 할 테스트:

```text
framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp
framework/languages/cpp/tests/Zlink.Framework.ContractTests/test_cpp_framework_layout_contract.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_actor_gateway.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_channel_messaging.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_locations_redis.cpp
framework/languages/cpp/tests/Zlink.Framework.UnitTests/test_cpp_framework_spot_runtime.cpp
```

필수 검증:

- `send_to_actor(actor_ref_t, ...)` / `request_to_actor(actor_ref_t, ...)`가 동작한다.
- `send_to(spot_ref_t, ...)` / `request_to(spot_ref_t, ...)`가 동작한다.
- id-only messaging API가 public headers에 없다.
- ref 기반 전송 중 location resolver/store가 호출되지 않는다.
- stale `spot_ref_t` 실패 분류가 기존 계약과 맞다.

## 문서 변경 대상

코드와 테스트를 바꾸는 같은 작업 안에서 C++ 문서와 관련 공통 문서를 함께 수정한다. 문서 수정은
별도 worker로 넘기지 않는다.

```text
framework/doc/contract-inventory/framework-public-contract-inventory.json
framework/doc/framework/common
framework/doc/framework/cpp
```

사용자-facing 문서에는 `actor_ref_t` / `spot_ref_t` 기반 전송만 남긴다. 내부 row나 store 설명에서
저장된 위치 정보를 말할 때는 "location row"라고 풀어 쓴다.

## 완료 게이트

```bash
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure

rg -n "spot_address_t|resolve_spot_address|resolve_actor_spot_address|send_to_actor\\s*\\([^)]*actor_id|request_to_actor\\s*\\([^)]*actor_id|send_to\\s*\\([^)]*spot_rid|request_to\\s*\\([^)]*spot_rid" \
  framework/languages/cpp \
  -S -g '!**/build/**'

rg -n "spot_address_t|SpotAddress|spot address|SpotRemoteAddress|spot remote address|send_to_actor\\s*\\([^)]*actor_id|request_to_actor\\s*\\([^)]*actor_id|send_to\\s*\\([^)]*spot_rid|request_to\\s*\\([^)]*spot_rid" \
  framework/doc/contract-inventory framework/doc/framework/common framework/doc/framework/cpp \
  -S -g '!framework/doc/plan/**' -g '!framework/doc/**/draft/**'

rg -n "\\b(partial|gap)\\b|public contract gap" \
  framework/languages/cpp/e2e framework/languages/cpp/samples \
  -g 'feature-map.ko.md' -g 'porting-inventory.ko.md' -g 'sample-porting-inventory.ko.md'
```

마지막 `rg`는 완료 전 누락 확인용이다. 결과가 나오면 해당 항목을 구현하거나, public contract 설계가
필요한 별도 항목으로 분리한 뒤 이 worker prompt를 완료 처리하지 않는다.
