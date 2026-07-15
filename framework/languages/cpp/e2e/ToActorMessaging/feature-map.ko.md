# ToActorMessaging feature-map

이 스위트는 공통 config-9 `to-actor messaging`의 C++ 구현 위치다.

- public 표면: `zlink::framework::actor_client_t`
- send 터미널: `send_to_actor(...).async()`
- request 터미널: `request_to_actor(...).async<TReply>()`
- 실패 분류: `actor_route_not_found`, `actor_location_stale`, `route_not_connected`

최신 proof는 `E2E_START_ORDER=reverse ZLINK_CPP_E2E_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-redis-vcpkg CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 timeout 240s framework/languages/cpp/e2e/ToActorMessaging/run_e2e.sh`이며, 로그는 `logs/20260707-182812-3053142`이다. 이 실행은 actor owner 서버, caller 서버, Redis location store, client runner를 띄우고 `to-actor-messaging e2e result=passed`를 출력했다.

| 공통 항목 | 상태 | C++ 구현 |
|-----------|------|----------|
| TA-A1 bind된 actor send/request | 구현 | `Client/Scenarios/ta_a1_scenario.hpp`가 actor를 준비한 뒤 caller 서버의 `/send`, `/request`를 호출하고 actor owner evidence의 handler marker를 확인한다. |
| TA-A2 bind 안 된 actor send/request | 구현 | `Client/Scenarios/ta_a2_scenario.hpp`가 session binding 없이 서버 측 caller에서 actor mailbox 전달과 reply를 확인한다. |
| TA-A3 no-bind 전달 뒤 이후 bind | 구현 | `TA-A3-before-bind`가 생성 전 fail-fast를 확인하고, 생성 뒤 `TA-A3-after-bind-*`가 성공을 확인한다. |
| TA-A4 unbind/disconnect 후 | 구현 | `TA-A4-disconnected-*`가 session-bound 상태 없이 actor row가 유지되는 상태에서 호출하고, destroy 뒤 `actor_route_not_found`를 확인한다. |
| TA-B1 row 없음 | 구현 | `TA-B1-missing*`가 send/request 양쪽에서 `actor_route_not_found`를 검증한다. |
| TA-B2 stale location | 구현 | `TA-B2-prepare`가 actor row는 있지만 owner SPOT에 actor가 없는 stale row를 만들고, `TA-B2-stale-location`이 caller 서버의 public `request_to_actor` 결과가 `actor_location_stale`인지 검증한다. |
| TA-B3 route not connected | 구현 | `TA-B3-prepare`가 연결되지 않은 ghost node/spot row를 만들고, `TA-B3-route-not-connected`가 caller 서버의 public `request_to_actor` 결과가 `route_not_connected`인지 검증한다. 이후 `TA-B3-route-restored`가 정상 actor follow-up request를 확인한다. |

`run_e2e.sh`는 Redis를 준비한 뒤 actor owner 서버와 caller 서버를 모두 시작하고, 두 서버 health를
기다린 다음 client runner를 실행한다. `E2E_START_ORDER=reverse`에서도 같은 순서 독립성을 검증한다.
현재 C++ runner는 전용 Docker Redis를 직접 띄우며, 사용자 환경에서 넘긴 외부 Redis endpoint를
공유 Redis로 재사용하지 않는다.
