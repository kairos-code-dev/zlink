# Implementation Review Log

## 단계 0

- 날짜: 2026-05-04
- 단계: 기준 상태 고정
- 확인한 draft spec 절: 첫 구현 범위, 구현 순서, 기존 공개 계약과의 관계
- draft spec에서 구현해야 할 계약 요약: Actor lifecycle, queue/dispatch, join/leave, STREAM session Actor list, relay, remote create-or-get/destroy, Discovery active route, snapshot, generic route 제거, 테스트와 문서 반영을 첫 구현에서 닫는다.
- 이번 단계에서 구현하지 않는 계약: 기능 구현은 matrix gate와 baseline 확인 뒤 단계 1부터 진행한다.
- 관련 회귀 테스트 ID: 전체 `ACT-*`
- 수행한 명령: `git status --short`, `git branch --show-current`, `git rev-parse --short HEAD`, draft/plan/POSD 문서 확인, draft symbol 추출
- 발견한 문제: 없음
- 수정한 파일: `doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md`
- 남은 위험: baseline build/test 결과 미기록
- 다음 확인: matrix `comm` 검증과 baseline build/test

## 단계 0 baseline 결과

- 날짜: 2026-05-04
- 대상: core baseline build/test
- 수행한 명령: `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -L integration -j1`
- 발견한 문제: 없음
- 수정한 파일: 없음
- 남은 위험: e2e/regression lane은 baseline 단계에서 실행하지 않았고, 기능 구현 뒤 milestone 검증에서 다시 판단한다.
- 다음 확인: 단계 1 public C surface 추가

## 단계 1-8 core 구현 결과

- 날짜: 2026-05-04
- 대상: core public surface, Actor lifecycle, join/leave, STREAM Actor list, relay, remote create-or-get/destroy, Discovery active route, snapshot, generic route 공개 표면 제거
- 확인한 draft spec 절: C API 변경 목록, 상수와 구조체, Dispatch enum 확장, Actor 생성과 종료, Actor ref 조회, Remote Actor ref, Actor active route 조회, Generic discovery route 제거 계획, Remote Actor create-or-get, Remote Actor 종료, Actor와 Spot join request, Actor와 Spot leave, STREAM session Actor list bind, STREAM에서 Actor로 relay, Actor queue 수신, 모니터링과 snapshot, 오류 의미, 소유권 규칙
- 구현 요약:
  - `core/include/zlink.h`, `zlink_enum.h`, `zlink_errno.h`에 Actor ref, join/recv/create/route/snapshot 타입과 request result, dispatch enum, Actor route sync option을 추가했다.
  - `core/src/api/service_spot_actor_api.cpp`를 추가해 local Actor table, checked/unchecked ref, join request queue, Actor readable dispatch, session Actor list, active route map, snapshot API를 구현했다.
  - remote create-or-get은 target Actor가 없을 때만 admission handler를 호출하고, existing 경로에서는 호출하지 않도록 테스트했다.
  - `generation == 0` unchecked ref는 유효하지 않은 ref로 처리하지 않고 현재 live Actor lookup으로 해석한다.
  - generic discovery route API는 `core/include/zlink.h` 공개 표면에서 제거했고, core 테스트 호출부도 제거했다.
  - Discovery formal spec과 Registry guide에서 generic route 사용 설명을 제거하고 `zlink_discovery_resolve_actor()` 계약과 사용 방향을 반영했다.
- 검증:
  - `cmake --build core/build -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -R test_spot_actor_dispatch -j1`
  - `ctest --test-dir core/build --output-on-failure -R unittest_typed_option -j1`
  - `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -L integration -j1`
  - `nm -D core/build/lib/libzlink.so | rg "zlink_discovery_(bind_route|unbind_route|resolve_route)"`
- 검증 결과: 모두 통과
- 제거 API 검증 결과: 공개 header, core 테스트, core 정식 문서/guide/internals, shared library dynamic symbol에서 generic route API가 검색되지 않는다.
- 남은 위험:
  - Actor control plane은 현재 같은 process 안의 registered SpotNode를 기준으로 동작한다. Registry/mesh wire protocol까지 닫힌 구현은 아니다.
  - `zlink_actor_send_bound_session_msg()`와 `zlink_actor_send_bound_session_packet()`은 bound session 검증과 소유권 이전만 수행하고 실제 STREAM callback 송신까지 연결하지 못했다.
  - join timeout과 request cleanup은 아직 timeout scheduler와 통합되지 않았다.
  - bindings는 아직 새 Actor API를 노출하지 않았고 제거된 generic route wrapper가 남아 있다.
- 다음 확인: bindings native library 동기화 가능 여부와 언어별 binding 표면 정리

## 2026-05-04 추가 core/public route 검증

- 대상: generic route 공개 API 제거와 내부 route 한계값 이동
- 확인한 draft spec 절: Generic discovery route 제거 계획, Actor active route 조회
- 구현 요약:
  - `core/include/zlink.h`에서 `zlink_route_kind_t`, `ZLINK_ROUTE_KIND_INVALID`, `ZLINK_ROUTE_KEY_MAX`, `ZLINK_ROUTE_VALUE_MAX` 공개 선언을 제거했다.
  - Registry 내부 구현이 쓰는 route kind와 크기 제한은 `core/src/services/discovery/route_limits_internal.hpp`로 옮겼다.
  - `zlink_discovery_bind_route()`, `zlink_discovery_unbind_route()`, `zlink_discovery_resolve_route()` 공개 선언과 dynamic symbol 부재를 다시 확인했다.
- 검증:
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "test_spot_actor_dispatch|test_discovery_socket_auto_connect_policy|test_spot_service_introspection|unittest_typed_option" -j1`
  - `ctest --test-dir core/build --output-on-failure`
  - `ctest --test-dir core/build --output-on-failure -R '^test_reconnect_ivl$'`
  - `rg -n "zlink_discovery_(bind|unbind|resolve)_route|zlink_route_kind_t|ZLINK_ROUTE_KIND_INVALID|ZLINK_ROUTE_KEY_MAX|ZLINK_ROUTE_VALUE_MAX" core/include core/src/api core/tests`
- 검증 결과:
  - core build와 Actor/Discovery/Spot/introspection/typed option 대상 테스트는 통과했다.
  - 전체 core CTest는 102개 중 `test_reconnect_ivl` 1개가 한 번 실패했고, 같은 테스트 단독 재실행은 통과했다.
  - 제거 API는 공개 header, public api source, core 테스트에서 검색되지 않았다.
- 남은 위험:
  - Registry 내부 route protocol과 내부 helper 이름은 Actor active route 구현 기반으로 남아 있다. 공개 API 제거와 구분해서 관리해야 한다.
  - full mesh remote Actor control plane, 실제 STREAM callback 송신, timeout scheduler 통합은 아직 남은 구현 위험이다.

## 2026-05-05 core 보강 진행

- 대상: 단계 5-13 중 join timeout, STREAM Actor list, Actor-to-session send, remote create-or-get 소유권과 직렬화, Actor snapshot route sync
- 확인한 draft spec 절: Actor와 Spot join request, STREAM session Actor list bind, STREAM에서 Actor로 relay, Actor에서 bound session으로 전송, Remote Actor create-or-get, Remote Actor 종료, Actor active route 조회, 모니터링과 snapshot, 소유권 규칙
- 구현 요약:
  - join request timeout scheduler를 붙이고 timeout 뒤 pending join request가 queue에 남지 않도록 정리했다.
  - `zlink_actor_send_bound_session_msg()`와 `zlink_actor_send_bound_session_packet()`이 실제 STREAM session으로 전송하고, 실패 시 원본 message/header/body 소유권을 유지하도록 바꿨다.
  - STREAM Actor API가 fake pointer가 아니라 실제 raw STREAM socket handle만 받도록 검증을 추가했다.
  - session binding에 session owner node를 저장하고, 같은 session의 같은 `actor_id` rebind 시 이전 Actor의 stale bound session ref를 정리했다.
  - STREAM close cleanup에서 session Actor list와 Actor bound session ref를 제거하도록 `zlink_close()`에 cleanup hook을 연결했다.
  - remote create-or-get은 target Actor가 없을 때만 admission handler를 호출하고, accept 뒤 Actor slot 생성까지 같은 Actor lock 구간에서 처리하도록 보강했다.
  - remote create submit 뒤 reject/default reject도 message 소유권을 소비하도록 보강했다.
  - admission handler 안에서 Actor 생성 API를 재진입 호출하면 `EFSM`으로 실패하도록 막았다.
  - Actor snapshot의 `route_synced`가 active route와 현재 Actor ref 일치 여부를 반영하도록 보강했다.
  - Spot snapshot의 `route_synced`가 Discovery SPOT owner sync와 node registration 상태를 반영하도록 보강했다.
  - Actor active route 이동, joined 후 bind publish, 이전 Actor destroy 후 새 route 유지, provider cleanup을 회귀 테스트로 추가했다.
  - plan 문서의 단계 0, 단계 1, 단계 8 일부, 단계 9 일부, 단계 10, 단계 12 일부 체크리스트를 완료 상태로 갱신했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - remote Actor control plane은 아직 같은 process 안 registered SpotNode 조회 기반이다. mesh wire routing까지 닫혔는지 별도 검토가 필요하다.
  - timeout 원자성은 join timeout 중심으로 보강됐고 destroy/bind/unbind 원자성은 draft 기준 추가 검토가 필요하다.

## 2026-05-05 stale API와 route 제거 검증

- 대상: 단계 11, 단계 16 stale symbol 검증
- 수행한 명령:
  - `rg -n "zlink_discovery_(bind_route|unbind_route|resolve_route)|zlink_route_kind_t|ZLINK_ROUTE_KIND_INVALID|ZLINK_ROUTE_KEY_MAX|ZLINK_ROUTE_VALUE_MAX" core/include core/src/api core/tests bindings samples doc/guide doc/spec/core doc/internals || true`
  - `nm -D core/build/lib/libzlink.so | rg "zlink_discovery_(bind_route|unbind_route|resolve_route)" || true`
  - 제거된 Actor helper, Actor별 queue limit option 오해 표현, 금지 표현을 검색했다.
- 검증 결과:
  - generic discovery route 공개 API와 관련 public type/limit 이름은 지정 범위에서 검색되지 않았다.
  - `libzlink.so` dynamic symbol에도 제거된 generic route API가 없다.
  - stale Actor API 이름은 plan 체크 항목 자체를 제외하면 검색되지 않았다.
- 수정한 파일: `doc/plan/spot-actor-dispatch-implementation-plan.ko.md`

## 2026-05-05 core 전체 build

- 대상: 단계 16 core build
- 수행한 명령: `cmake --build core/build -j"$(nproc)"`
- 검증 결과: 통과
- 수정한 파일: `doc/plan/spot-actor-dispatch-implementation-plan.ko.md`

## 2026-05-05 문서-코드 반복 리뷰

- 대상: 구현 후 문서-코드 반복 리뷰 루프
- 확인한 draft spec 절: C API 변경 목록, Dispatch enum 확장, Actor 생성과 종료,
  Actor ref 조회, Remote Actor create-or-get, Actor join/leave, STREAM session
  Actor list bind, STREAM relay, Actor queue 수신, Actor active route 조회,
  모니터링과 snapshot, 오류 의미, 소유권 규칙, 비목표
- 1차 mismatch:
  - `doc/site/docs`에 제거된 generic discovery route API 설명과 오래된 draft 링크가
    남아 있었다.
  - contract matrix 일부 `Implementation Owner`가 최종 구현 파일
    `core/src/api/service_spot_actor_api.cpp`와 달랐다.
- 수정한 파일:
  - `doc/site/docs/api/bindings.ko.md`
  - `doc/site/docs/api/bindings.md`
  - `doc/site/docs/api/spot.ko.md`
  - `doc/site/docs/api/spot.md`
  - `doc/site/docs/api/discovery.ko.md`
  - `doc/site/docs/api/discovery.md`
  - `doc/site/docs/api/registry.ko.md`
  - `doc/site/docs/api/registry.md`
  - `doc/site/docs/api/monitoring.ko.md`
  - `doc/site/docs/api/monitoring.md`
  - `doc/site/docs/guide/07-4-registry.ko.md`
  - `doc/site/docs/guide/07-4-registry.md`
  - `doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md`
- 2차 검증 명령:
  - `comm -23 <(rg --no-filename -o 'ACT-[A-Z]+-[0-9]+' doc/spec/draft/spot-actor-dispatch.ko.md | sort -u) <(rg --no-filename -o 'ACT-[A-Z]+-[0-9]+' doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md core/tests/integration/test_spot_actor_dispatch.cpp bindings/c/samples/*.c | sort -u)`
  - `comm -23 <(printf '%s\n' ...actor public API list... | sort -u) <(rg --no-filename -o 'zlink_[A-Za-z0-9_]+' doc/spec/core/service/spot.ko.md doc/spec/core/socket/stream.ko.md doc/spec/core/errno-map.ko.md doc/spec/core/service/discovery.ko.md | sort -u)`
  - 제거된 generic route API, 제거된 Actor helper, Actor별 queue limit option 오해 표현,
    unchecked generation 오해 표현을 검색했다.
  - `rg -n "\| reviewed \|" doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md | wc -l`
  - `rg -n "\| (planned|implemented|tested|documented) \|" doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md`
- 2차 검증 결과:
  - draft `ACT-*`는 matrix와 테스트/샘플 추적 범위에 모두 포함된다.
  - Actor public API 목록은 public header와 정식 spec에 모두 있다.
  - 정식 spec, guide, internals, site docs에서 제거된 generic route 공개 API와
    stale Actor API 이름이 검색되지 않는다.
  - contract matrix의 33개 행이 모두 `reviewed`이고 미검토 행은 없다.
- 남은 위험:
  - binding 언어별 full 구현 여부는 core release와 native library 최신화 뒤
    bindings 순차 단계에서 닫는다.
- 다음 확인: core unit/integration 전체 테스트

## 2026-05-05 core unit/integration 전체 테스트

- 대상: 단계 16 core unit/integration, 기존 Discovery/SPOT/STREAM tests
- 수행한 명령:
  - `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -L integration -j1`
- 검증 결과:
  - unittest 21개 통과
  - integration 63개 통과
- 수정한 파일: `doc/plan/spot-actor-dispatch-implementation-plan.ko.md`
- 다음 확인: public header compile check, 남은 Actor contract 테스트와 문서 반영

## 2026-05-05 public header compile check

- 대상: 단계 16 public header compile check
- 수행한 명령: 임시 C 파일에서 `#include "zlink.h"`와 Actor public 타입 선언 후 `cc -Icore/include -c`
- 검증 결과: 통과
- 수정한 파일: `doc/plan/spot-actor-dispatch-implementation-plan.ko.md`

## 2026-05-05 단계 4 Actor unread dispatch 보강

- 대상: 단계 4 Actor별 unread 상태, dispatch event, recv 제한, backpressure
- 확인한 draft spec 절: Actor queue 수신, 동시성과 callback 제한, Backpressure, Actor queue와 relay 회귀 테스트
- 구현 요약:
  - Actor queue가 독립 queue/socket이 아니라 Actor별 unread 상태라는 draft 설명을 plan 단계 4에 반영했다.
  - Actor별 unread part hard limit을 내부 dispatch 자원 한계로 두고 초과 시 `ZLINK_SUBMIT_BACKPRESSURED`를 반환하도록 했다. Actor별 public queue limit option은 추가하지 않았다.
  - `SpotNode` shutdown 경로에서 joined Actor도 강제로 정리되도록 node cleanup 전용 Actor 제거 경로를 분리했다.
  - dispatch callback 테스트가 `ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR`, callback 안 nonblocking drain, `ZLINK_RECV_NO_DATA`, incomplete multipart recv, cleanup, backpressure ownership을 확인하도록 보강했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - `ACT-QUEUE-02`와 `ACT-QUEUE-07`은 true remote mesh forwarding과 remote target drop 검증이 필요해서 아직 미완료다.

## 2026-05-05 단계 5 Actor join/leave 보강

- 대상: 단계 5 ref 기반 join, reject reply, leave 뒤 Entry Spot 체류 중 queue 보존
- 확인한 draft spec 절: Actor와 Spot join request, Actor와 Spot leave, 회귀 테스트 항목 `ACT-JOIN-*`
- 구현 요약:
  - ref 기반 join에서 target node가 없으면 submit 단계에서 `ZLINK_SUBMIT_NOT_CONNECTED`를 반환하도록 했다.
  - target Actor 없음, target Spot 없음, checked generation mismatch는 submit 성공 뒤 request completion으로 `ZLINK_REQUEST_NOT_FOUND` 또는 `ZLINK_REQUEST_CONFLICT`를 전달하도록 맞췄다.
  - join accept 시 Actor unread 상태가 이미 있으면 새 Spot dispatch context로 readable event를 발행하도록 했다.
  - join reject reply, unchecked ref join, ref join target node 선택, leave 뒤 Entry Spot 체류 중 unread 보존과 FIFO drain을 회귀 테스트로 추가했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - true remote mesh 연결 단절과 timeout scheduler 기반 leave timeout은 remote control plane 단계와 함께 다시 검토해야 한다.

## 2026-05-05 단계 12 Spot snapshot 보강

- 대상: 단계 12 Spot snapshot destroy 반영, joined Actor count, pending join count
- 확인한 draft spec 절: 모니터링과 snapshot, SpotNode snapshot, `ACT-SNAPSHOT-03..05`
- 구현 요약:
  - public snapshot API만 사용해 Spot rid를 얻는 테스트 helper를 추가했다.
  - joined Actor count가 join/leave에 따라 1에서 0으로 바뀌는지 검증했다.
  - dispatch handler가 없는 pending join request가 Spot snapshot의 `pending_actor_join_count`에 반영되고, Spot destroy 뒤 row가 사라지고 pending join이 terminated로 완료되는지 검증했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과

## 2026-05-05 단계 13 소유권과 실패 경로 보강

- 대상: 단계 13 message ownership, request opaque handle, callback destroy 금지
- 확인한 draft spec 절: 소유권 규칙, Actor queue 수신, Actor와 Spot join request, Actor에서 bound session으로 전송, 소유권과 실패 경로
- 구현 요약:
  - join submit 성공과 immediate request completion 실패에서 source message가 라이브러리로 이전되는지 확인했다.
  - join submit not-connected 실패에서는 source message가 caller에게 남는지 확인했다.
  - actor packet send 실패 시 header/body가 모두 caller에게 남는지 확인했다.
  - remote create validation 실패 시 create message가 caller에게 남는지 확인했다.
  - dispatch callback 안 같은 Actor destroy가 busy로 거부되고 handle이 유지되는지 확인했다.
  - partial packet send는 공개 계약으로 제공하지 않고 `zlink_actor_send_bound_session_packet()` 한 번의 header/body packet send만 검증했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과

## 2026-05-05 단계 9 remote create/destroy 보강

- 대상: 단계 9 remote create-or-get 직렬화, retry 수렴, remote destroy 상태
- 확인한 draft spec 절: Remote Actor create-or-get, Admission handler, Remote Actor 종료, `ACT-REMOTE-*`
- 구현 요약:
  - 같은 actor id의 동시 create-or-get 두 건이 `CREATED` 하나와 `EXISTING` 하나로 수렴하는지 검증했다.
  - timeout 값이 0인 create-or-get 재시도도 `CREATED` 또는 `EXISTING`으로 수렴하고 중복 Actor slot을 만들지 않는지 검증했다.
  - join 상태 remote destroy가 busy로 실패하고 Actor slot을 유지하는지 검증했다.
  - leave 뒤 remote destroy 성공과 destroy 뒤 lookup `ENOENT`를 검증했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - true mesh request routing, remote destroy detach failure, remote destroy timeout 원자성은 아직 미완료다.

## 2026-05-05 core 전체 재검증

- 대상: 단계 16 core build, core unit/integration tests
- 수행한 명령:
  - `cmake --build core/build -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -L integration -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_reconnect_ivl$' -j1`
- 검증 결과:
  - core build 통과
  - unittest 21개 통과
  - integration 전체 실행은 63개 중 `test_reconnect_ivl` 1개가 `test_reconnect_ivl_tcp_ipv6`에서 한 번 실패했다.
  - `test_reconnect_ivl` 단독 재실행은 통과했다.
- 남은 위험:
  - integration 전체 실행에서 동일 flake가 반복 관측된다. Actor dispatch 변경과 직접 관련된 `test_spot_actor_dispatch`는 전체 integration 실행과 단독 실행 모두 통과했다.

## 2026-05-05 단계 14 sample과 binding 영향 목록

- 대상: core C API Actor sample, generic route 제거 영향, binding 영향 정리
- 확인한 draft spec 절: Local Actor 사용 흐름, Remote Actor 사용 흐름, STREAM packet handler 사용 예, Actor client send 사용 예
- 구현 요약:
  - Local Actor room server sample을 추가했다.
  - gateway session에서 remote play server Actor로 relay하는 sample을 추가했다.
  - single-player queue serialization sample을 추가했다.
  - sample runner에 새 Actor sample 3개를 포함했다.
  - generic route 제거는 sample에서 `zlink_discovery_resolve_actor()`와
    `zlink_discovery_resolve_spot()` 전용 경로를 쓰는 방향으로 유지한다.
  - binding full 구현은 이 단계에서 수행하지 않고, 언어별 typed Actor object나
    codec/runtime wrapper도 만들지 않았다.
- 검증:
  - `bindings/c/samples/run_samples.sh`
- 검증 결과:
  - C sample build 성공
  - sample smoke 13개 통과

## 2026-05-05 core remote relay와 not-connected 보강

- 대상: 단계 7 remote bound relay, 단계 8 Actor-to-session not-connected,
  단계 9 remote destroy target node 없음
- 확인한 draft spec 절: STREAM에서 Actor로 relay, Actor에서 bound session으로 전송,
  Remote Actor 종료
- 구현 요약:
  - session Actor list entry가 target Actor 포인터뿐 아니라 concrete
    `zlink_actor_ref_t`를 함께 저장하도록 바꿨다.
  - remote Actor owner node가 종료되어 session owner에 stale binding만 남은 경우
    STREAM에서 Actor로 보내는 relay가 `ZLINK_SUBMIT_NOT_CONNECTED`를 반환하고
    message ownership을 caller에게 유지하는지 검증했다.
  - explicit unbind는 Actor owner provider 종료 cleanup으로 stale binding을 제거하고
    성공하는지 검증했다.
  - remote Actor에 multipart relay를 보낼 때 remote Actor unread part 순서와 part 수가
    보존되는지 검증했다.
  - Actor에서 bound session으로 보내는 경로에서 session owner node가 사라진 경우
    `ZLINK_SUBMIT_NOT_CONNECTED`를 반환하고 message ownership을 caller에게 유지하도록
    보강했다.
  - remote destroy에서 target node routing id가 등록되어 있지 않으면
    `ZLINK_REQUEST_NOT_CONNECTED`를 반환하도록 보강했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - explicit unbind의 not-connected 실패와 기존 항목 유지, bind/unbind/destroy timeout
    원자성, remote target Actor 없음 drop은 아직 미완료다.

## 2026-05-05 request timeout 원자성 보강

- 대상: 단계 3 destroy timeout, 단계 6 bind/unbind timeout, 단계 9 remote destroy timeout
- 확인한 draft spec 절: Actor 생성과 종료, STREAM session Actor list bind,
  Remote Actor 종료
- 구현 요약:
  - Actor 전역 lock을 timed mutex로 바꾸고, request API가 `timeout_ms_ > 0`일 때
    lock 획득 timeout을 `ZLINK_REQUEST_TIMED_OUT`으로 반환하도록 보강했다.
  - timeout 실패는 lock 획득 전 실패이므로 Actor slot, session Actor list, Actor bound
    session ref를 변경하지 않는다.
  - `SpotNode` 제거 시 admission handler 전역 등록도 제거해, 파괴된 node pointer가
    다음 테스트나 node 재사용에 남지 않도록 정리했다.
  - admission handler가 control lock을 잡고 있는 상황을 만들어 local destroy, stream
    bind, stream unbind, remote destroy가 timeout으로 실패해도 이후 정상 호출과 relay가
    원상태 기준으로 성공하는지 검증했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - detach 자체가 실패하는 경로와 remote target Actor 없음 drop은 아직 별도로 닫지 않았다.

## 2026-05-05 destroy detach 실패 보강

- 대상: 단계 3 `ACT-LIFE-09`, 단계 9 `ACT-REMOTE-14`
- 확인한 draft spec 절: Actor 생성과 종료, Remote Actor 종료, STREAM에서 Actor로 relay
- 구현 요약:
  - bound session에 해당 Actor로 향하는 stream-to-actor multipart relay가 진행 중이면
    Actor destroy가 session detach를 완료할 수 없는 상태로 보고 `ZLINK_REQUEST_BUSY`를
    반환하도록 보강했다.
  - local destroy 실패 시 Actor handle과 Actor slot이 유지되는지 검증했다.
  - remote destroy 실패 시 target Actor slot이 유지되고 lookup이 같은 generation을
    반환하는지 검증했다.
  - in-progress relay를 final part로 완료하고 unbind한 뒤 destroy가 성공하는지
    검증했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - remote target Actor 없음 drop과 explicit unbind not-connected 실패는 아직 미완료다.

## 2026-05-05 remote create-or-get target routing 보강

- 대상: 단계 9 create-or-get request routing
- 확인한 draft spec 절: Remote Actor create-or-get, Admission handler
- 구현 요약:
  - caller node에 같은 actor id의 local Actor가 있어도 `target_node_rid_`가 가리키는
    target node의 admission handler와 Actor table을 사용해 create-or-get을 처리하는지
    검증했다.
  - 반환된 Actor ref의 node rid가 target node rid와 같고, caller node의 같은 actor id와
    혼동되지 않는지 검증했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - process 밖 mesh transport를 통과하는 create-or-get은 기존 SPOT routed request 경로
    연결 단계에서 추가 검증이 필요하다.

## 2026-05-05 explicit unbind not-connected 보강

- 대상: 단계 6 `ACT-STREAM-13`
- 확인한 draft spec 절: STREAM session Actor list bind
- 구현 요약:
  - `zlink_spot_node_disconnect_peer_rid()` 호출을 Actor relay/control route state에도
    반영하도록 내부 hook을 추가했다.
  - explicit unbind에서 Actor owner route가 disconnected 상태면
    `ZLINK_REQUEST_NOT_CONNECTED`를 반환하고 session Actor list 항목을 유지하도록
    보강했다.
  - unbind 실패 뒤 같은 actor id로 relay를 시도하면 binding이 남아 있기 때문에
    `ZLINK_SUBMIT_NOT_CONNECTED`가 반환되는지 검증했다. binding이 제거되었다면
    `ZLINK_SUBMIT_NOT_FOUND`가 되었을 경로다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - remote target Actor 없음 drop은 아직 미완료다.

## 2026-05-05 remote target Actor 없음 drop 보강

- 대상: 단계 7 `ACT-QUEUE-07`
- 확인한 draft spec 절: STREAM에서 Actor로 relay
- 구현 요약:
  - stale session binding이 target node rid를 가리키고, 같은 node rid를 가진 replacement
    node에는 target actor id가 없는 상황을 public API만으로 구성했다.
  - sender가 relay를 submit하면 target node까지 도착한 one-way relay로 보고
    `ZLINK_SUBMIT_OK`를 반환하며 message ownership이 라이브러리로 이전되는지 검증했다.
  - replacement node의 다른 Actor queue에는 메시지가 들어가지 않는지 snapshot으로
    검증했다.
- 검증:
  - `cmake --build core/build --target test_spot_actor_dispatch`
  - `ctest --test-dir core/build --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과: 통과
- 남은 위험:
  - core Actor 단계 2-13의 plan 체크박스는 닫혔다. 이후 matrix review와 정식 문서 반영
    단계에서 draft/spec/code mismatch를 다시 점검한다.

## 2026-05-05 Actor socket 없음 결정 반영

- 대상: draft spec과 plan의 Actor queue/relay 표현
- 확인한 draft spec 절: 전체 모델, 설계 원칙, STREAM에서 Actor로 relay, Actor queue
  수신, Backpressure
- 반영 요약:
  - Actor가 socket, inproc endpoint, transport endpoint를 소유하지 않는다고 명시했다.
  - local/remote relay 모두 target `SpotNode`의 Actor별 unread 상태에 enqueue하는
    구조라고 명시했다.
  - `zlink_actor_recv_part()`는 Actor socket recv가 아니라 dispatch callback 안에서
    내부 unread part를 꺼내는 API라고 명시했다.
  - Actor별 socket queue limit과 Actor socket high-water mark `0` 계약이 없다고 명시했다.
- 검증:
  - `rg -n "language[-]exchange|문서[ ]?작성" doc/spec/draft/spot-actor-dispatch.ko.md`
- 검증 결과:
  - 금지 표현 없음

## 2026-05-05 정식 문서 1차 반영

- 대상: 단계 15
- 확인한 draft spec 절: C API 변경 목록, Dispatch enum 확장, Actor 생성과 종료,
  Remote Actor create-or-get, Actor와 Spot join request, STREAM session Actor list
  bind, STREAM에서 Actor로 relay, Actor queue 수신, Actor active route 조회,
  모니터링과 snapshot, 오류 의미, 소유권 규칙, 첫 구현 제외 항목
- 반영 요약:
  - `doc/spec/core/service/spot.ko.md`에 Actor public contract, dispatch subject,
    join/leave, remote create-or-get/destroy, Actor recv/send, snapshot 계약을 추가했다.
  - `doc/spec/core/socket/stream.ko.md`에 STREAM session Actor list bind/unbind와
    Actor selector relay 계약을 추가했다.
  - `doc/spec/core/errno-map.ko.md`에 확장된 request result enum과 Actor 적용 함수를
    반영했다.
  - `doc/guide/07-3-spot.ko.md`에는 Actor 사용 흐름과 C sample 위치를, 
    `doc/internals/spot-internals.ko.md`에는 Actor가 socket을 소유하지 않는 내부 모델을
    분리해서 반영했다.
  - `doc/spec/sample/SAMPLE_POLICY.md`에 Actor sample 규칙을 추가했다.
  - `doc/spec/bindings/README.md`에는 binding 정식 계약을 core release 뒤 언어별로
    반영한다는 추적 링크만 남겼다.
- 검증:
  - `rg -n "language[-]exchange|문서[ ]?작성" doc --glob '!doc/site/**'`
  - 제거된 Actor helper와 Actor별 queue limit option 오해 표현을 검색했다.
  - unchecked generation 오해 표현을 검색했다.
  - `rg -n "doc/draft|spot-socket-backed-runtime|spot-routed-request-api" doc/spec/core doc/spec/bindings doc/guide doc/internals --glob '!doc/site/**'`
- 검증 결과:
  - 모두 결과 없음

## 2026-05-05 ASAN 누수 정리와 전체 검증

- 대상: 단계 16
- 확인한 draft spec 절: Actor와 Spot join request, timeout 규칙, SpotNode shutdown,
  소유권 규칙
- 구현 요약:
  - timeout 또는 reply로 완료된 Actor join request가 retired set에 남아 누수되지
    않도록 request 수명 정리를 보강했다.
  - Spot data plane 정상 teardown에서 `socket_poller_t`를 삭제하도록 정리했다.
- 검증:
  - `cmake --build core/build -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -L integration -j1`
  - `cmake -S core -B core/build-asan -DENABLE_ASAN=ON -DBUILD_TESTS=ON -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build-asan --target test_spot_actor_dispatch -j"$(nproc)"`
  - `ASAN_OPTIONS=detect_odr_violation=0 ctest --test-dir core/build-asan --output-on-failure -R '^test_spot_actor_dispatch$' -j1`
- 검증 결과:
  - normal core build 통과
  - unit 21개 통과
  - integration 63개 통과
  - ASAN Actor targeted test 통과
  - ASAN 기본 ODR 감지는 Boost.Asio header static `id` 중복 진단으로 먼저 실패하므로,
    `detect_odr_violation=0`을 설정해 Actor 경로 누수와 메모리 오류를 확인했다.

## 2026-05-06 문서 논리 리뷰

- 대상: draft spec, Entry Spot/transport queue draft, 실행 plan, contract matrix
- 확인한 draft spec 절: Actor와 Spot join request, Actor와 Spot leave, STREAM session
  Actor list bind, Actor active route 조회, 회귀 테스트 항목
- 발견 및 반영:
  - `leave`가 일부 문단과 테스트에서는 "join 관계 해제"로 남아 있고 다른 절에서는
    "Entry Spot 이동"으로 정의되어 있었다. leave 계약, plan 체크 항목, matrix 행,
    회귀 테스트 기대값을 Entry Spot 이동 모델로 통일했다.
  - user Spot destroy가 active route를 unjoined처럼 바꾼다는 문장이 Entry Spot
    불변식과 충돌했다. user Spot destroy는 Actor를 Entry Spot으로 이동시키고
    `joined = 1`, `joined_spot_rid = Entry Spot rid`를 유지하도록 수정했다.
  - destroy 계약이 "Spot에 join된 Actor destroy 실패"로 남아 있어, Actor가 항상
    Entry Spot을 가진다는 규칙과 충돌했다. destroy 실패 조건을 "Entry Spot이 아닌
    user Spot에 있는 Actor"로 좁혔다.
  - matrix test manifest에 `ACT-JOIN-29..38`, `ACT-STREAM-21..22`가 빠져 있었다.
    manifest를 draft 회귀 테스트 목록과 맞췄다.
  - 상단 remote join 개요 sequence가 target accept 직후 source retire처럼 보였다.
    session Actor list compare-and-swap, visible commit, source retire 순서가 드러나도록
    개요 sequence를 수정했다.
- 검증:
  - 오래된 leave 의미와 route 전환 표현 검색
  - 금지 표현 검색
  - `git diff --check -- doc/spec/draft/spot-actor-dispatch.ko.md doc/spec/draft/spot-entry-transport-queues.ko.md doc/plan/spot-actor-dispatch-implementation-plan.ko.md doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md doc/plan/spot-actor-dispatch/logs/implementation-review-log.ko.md`
- 검증 결과:
  - 남은 hit는 `joined = 0`으로 바꾸지 않는다는 부정 문장뿐이다.
  - 금지 표현 없음
  - diff whitespace 오류 없음
- 남은 위험:
  - 정식 spec과 site API 문서에는 현재 header 기준의 이전 join/leave 표면이 남아 있다.
    draft의 단일 `zlink_spot_node_actor_join_spot(node, actor, dest_node_rid,
    dest_spot_rid, ...)` 계약과 `current_spot_rid_` 기반 leave 계약은 core header와
    구현이 갱신된 뒤 정식 spec과 site 문서에 다시 반영해야 한다.
  - 위 mismatch 때문에 실행 plan의 최종 문서 업데이트 종료 조건 일부는 완료 상태에서
    미완료 상태로 되돌렸다.

## 2026-05-06 join 승인 handler와 remote join pending Actor 정리

- 대상: draft spec, Entry Spot/transport queue draft, 실행 plan, contract matrix
- 확인한 draft spec 절: Actor와 Spot join request, Remote Actor create-or-get,
  Admission handler, remote join process
- 발견 및 반영:
  - join 승인은 새 handler 등록 API가 아니라 기존 `zlink_spot_dispatch_event_handler()`
    등록으로 처리한다는 점을 join API 설명에 명시했다.
  - target Spot handler가 `ACTOR_JOIN_READABLE` event를 받고
    `zlink_spot_actor_join_recv()` / `zlink_spot_actor_join_reply()`로 accept 또는 reject를
    결정한다고 명시했다.
  - remote join caller가 target node에 remote Actor를 미리 만들 필요가 없다고
    명시했다. target node는 prepare 단계에서 pending Actor state를 내부 생성한다.
  - remote join prepare는 explicit remote create-or-get이 아니므로
    `zlink_spot_node_actor_admission_handler()`를 호출하지 않고, target Spot join
    handler가 pending Actor 생성과 Spot 입장을 함께 승인한다고 명시했다.
  - 같은 `actor_id`의 live Actor나 다른 pending Actor가 target node에 이미 있으면
    conflict 또는 busy 계열 실패가 되며 target Spot handler는 호출되지 않는다고 명시했다.
  - 회귀 테스트 항목 `ACT-JOIN-39..41`, `ENTRY-ACTOR-30..32`를 추가했고 plan에는
    아직 구현 검증 전 항목으로 남겼다.
- 검증:
  - 금지 표현 검색
  - 오래된 remote join/create admission 표현 검색
  - diff whitespace 검사
- 검증 결과:
  - 금지 표현 없음
  - diff whitespace 오류 없음

## 2026-05-06 활성 draft spec 기준 문서 전환

- 대상: `doc/spec/draft/spot-entry-transport-queues.ko.md`, 실행 plan, contract matrix
- 확인한 draft spec 절: 목적, Actor join, Remote join process, Public API 변경,
  회귀 테스트
- 반영:
  - `doc/spec/draft/spot-entry-transport-queues.ko.md`를 Entry Spot, Actor 이동,
    remote join, Spot socket 제거 규칙의 활성 draft spec 단일 기준으로 명시했다.
  - `doc/spec/draft/spot-actor-dispatch.ko.md`에 추가했던 join handler와 remote pending
    Actor 관련 중복 내용을 제거했다. 이 내용은 활성 draft spec에만 남긴다.
  - 실행 plan의 기준 문서와 traceability 표를 활성 draft spec으로 전환했다.
  - contract matrix의 기준 문서 설명과 Draft Link 경로를 활성 draft spec으로 전환했다.
  - 새 회귀 항목은 `ACT-JOIN-*`가 아니라 활성 draft spec의 `ENTRY-ACTOR-30..32`로
    추적하도록 plan과 matrix manifest를 맞췄다.
- 검증:
  - 활성 draft spec과 matrix의 `ENTRY-*` / `ACT-*` ID 대조
  - stale `ACT-JOIN-39..41` 검색
  - 금지 표현 검색
  - diff whitespace 검사
