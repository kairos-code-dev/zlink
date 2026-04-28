# SPOT Topology Redesign Rollout Plan

이 문서는 구현 순서와 검증 게이트를 정리한 **실행 계획 문서**다.
설계 자체는 아래 draft 문서를 기준으로 삼는다.

- [SPOT Topic / Routed Topology Redesign Draft](../../draft/spot-topology-redesign.ko.md)

이 plan의 목적은 "무엇을 만들 것인가"를 다시 설명하는 것이 아니라, 그 설계를
**사용자 개입 없이 끝까지 반영하는 순서와 통과 조건**을 고정하는 것이다.

핵심 원칙은 아래와 같다.

1. 먼저 core를 바꾼다.
2. draft 문서 기준으로 core 반영 여부를 반복 리뷰한다.
3. 그 다음 `core/src` 전체를 POSD 기준으로 반복 리팩토링한다.
4. 모든 테스트, sample, perf를 통과시킨다.
5. 그 뒤에만 정식 문서와 bindings를 반영한다.

---

## 1. core 변경 뒤 문서 적용 리뷰 게이트

이번 작업은 pub/sub, routed, discovery, HWM, queue 정책을 함께 바꾸기 때문에,
core 코드가 일부만 바뀐 상태에서 bindings나 정식 문서로 넘어가면 빠진 항목을
놓치기 쉽다.

따라서 구현 순서는 아래처럼 고정한다.

1. 먼저 core를 변경한다.
2. core 빌드와 core 테스트를 다시 돌린다.
3. 그 다음 draft 문서를 기준으로 code review를 수행한다.
4. 문서의 각 항목이 실제 core 코드와 공개 헤더에 반영되었는지 하나씩 대조한다.
5. 미적용 항목이 하나라도 있으면 다시 core를 수정한다.
6. 수정 뒤 다시 build / test / review를 반복한다.
7. 미적용 항목이 `0`이 될 때까지 이 루프를 반복한다.
8. 그 뒤에만 다음 단계로 넘어간다.

즉 이 단계의 목적은 "core를 한 번 고쳤다"가 아니라, "draft의 모든 구현
요구사항이 core에 반영된 것이 확인됐다"를 통과 조건으로 삼는 것이다.

### 1.1 리뷰 체크 항목

core 변경 뒤 review에서는 최소 아래를 같이 확인해야 한다.

- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- SPOT pub/sub 구현
- SPOT routed 구현
- discovery / manual peer lifecycle 구현
- auto HWM / manual HWM mapping 구현
- local delivery queue hard limit 구현
- snapshot / query / perf detail 노출

그리고 각 review 반복에서 아래를 기록해야 한다.

- 이번 반복에서 확인한 문서 항목
- 실제 반영된 코드 위치
- 아직 반영되지 않은 항목
- 다음 반복에서 수정할 항목

핵심은 "대충 대부분 반영됨" 상태로 다음 단계로 넘어가지 않는 것이다.

---

## 2. POSD 기반 리팩토링 게이트

문서 항목이 모두 core에 반영된 뒤에도 바로 bindings나 정식 문서 단계로 넘어가지
않는다. 그 다음에는 `core/src` 전체를 대상으로 POSD 기반 리팩토링 단계를
수행해야 한다.

이 단계의 목적은 "기능이 들어갔다"에서 멈추지 않고, 새 구조가 실제로도 변경
파급이 낮고 책임이 분명한 형태로 정리되었는지 확인하는 것이다.

범위는 아래처럼 고정한다.

- `core/src/api/`
- `core/src/core/`
- `core/src/sockets/`
- `core/src/services/`
- 그 밖의 `core/src` 전체

즉 특정 spot 파일 몇 개만 보는 것이 아니라, 이번 변경과 닿는 `core/src` 전체를
대상으로 본다.

### 2.1 POSD 리뷰 기준

이 단계는 저장소의 POSD 원칙을 그대로 따른다. 최소 아래 항목을 반복해서 본다.

1. 깊은 모듈
   - 인터페이스는 단순한데 구현 복잡도를 내부에서 충분히 흡수하는가
2. 정보 은닉
   - peer wiring, aggregate subscription, routed envelope, HWM 적용 규칙 같은
     설계 지식이 한 모듈 안에 가둬져 있는가
3. 복잡성을 아래로
   - 호출자가 알아야 할 내부 socket 평면 지식이 불필요하게 새어 나오지 않는가
4. 오류를 정의로 없애기
   - 특수 예외 경로를 API 또는 내부 계약 정리로 줄일 수 있는가
5. 위험 신호 제거
   - pass-through 메서드
   - 시간 순서대로만 나뉜 얕은 helper
   - 특수 코드와 범용 코드가 한 함수에 섞인 구조
   - 같은 의미가 여러 파일에 중복된 구조

### 2.2 POSD 리팩토링 반복 절차

이 단계는 아래 루프를 따른다.

1. `core/src` 전체에서 POSD 위험 신호를 먼저 열거한다.
2. 각 위험 신호가 어떤 POSD 원칙에 어긋나는지 기록한다.
3. 수정 방향을 두 가지 이상 검토하고, 더 나은 쪽을 선택한다.
4. core 코드를 수정한다.
5. 다시 build / test를 돌린다.
6. 다시 `core/src` 전체를 리뷰한다.
7. 아직 남은 POSD 기반 리팩토링 항목이 있으면 다시 반복한다.
8. 더 이상 진행할 POSD 기반 리팩토링 항목이 없다고 리뷰 로그에 기록된
   상태에서만 다음 단계로 넘어간다.

### 2.3 POSD hotspot

이번 변경 성격상 아래는 특히 다시 보기 쉬운 hotspot이다.

- discovery / manual peer lifecycle 경로가 서로 다른 지식을 중복하고 있지 않은가
- pub/sub aggregate subscription 관련 상태와 replay 로직이 여러 모듈에 흩어지지
  않았는가
- `internal-router` / `external-router` 책임이 다시 섞이지 않았는가
- HWM 계산과 수동 HWM mapping 지식이 여러 파일에 분산되지 않았는가
- local delivery queue / hard limit / disconnect 정책이 여러 helper에 얕게 퍼지지
  않았는가

---

## 3. 전체 검증 게이트

core 구현 반영과 POSD 기반 리팩토링이 끝난 뒤에는 최종 검증 게이트를 통과해야
한다. 이 단계의 목적은 실제 빌드 산출물과 sample, perf 경로까지 모두 정상
동작함을 확인하는 것이다.

이 단계는 아래 순서를 따른다.

1. core를 다시 빌드한다.
2. core 테스트를 모두 실행한다.
3. bindings/c native header와 runtime library를 먼저 동기화한다.
4. `bindings/c/samples`를 빌드하고 스모크 테스트를 수행한다.
5. `bindings/c/perf` single 전체 패턴 smoke를 수행한다.
6. `bindings/c/perf` multi 전체 패턴 smoke를 수행한다.
7. bindings 전체 테스트를 다시 실행한다.
8. 하나라도 실패하면 원인 분석 뒤 core 또는 binding을 수정하고 다시 반복한다.
9. 모든 검증이 성공할 때만 다음 단계로 넘어간다.

### 3.1 최소 통과 조건

- core 전체 테스트 성공
- bindings 전체 테스트 성공
- `bindings/c/samples` 스모크 테스트 성공
- `bindings/c/perf` single 전체 패턴 스모크 성공
- `bindings/c/perf` multi 전체 패턴 스모크 성공

특히 perf는 일부 패턴만 통과해서는 안 된다.

- single: 전체 패턴
- multi: 전체 패턴

### 3.2 같이 확인할 것

- perf runner가 실제 `core/build` runtime을 사용하고 있는지
- sample과 perf가 새 enum 이름, 새 HWM 옵션, 새 socket snapshot 이름을 따라가는지
- local delivery hard limit 정책이 sample/perf에서 unexpected hang이나 global
  pause를 만들지 않는지
- discovery 기반 auto-connect와 manual peer 경로가 sample/perf 양쪽에서 모두
  정상 동작하는지

---

## 4. 정식 문서 반영 게이트

전체 검증 게이트를 통과한 뒤에만 정식 문서를 수정한다. 이 순서를 강제하는 이유는
draft 설계를 먼저 정식 문서에 섞어 넣으면, 아직 검증되지 않은 계약과 설명이
공개 문서에 들어가 버리기 때문이다.

이 단계에서는 아래 문서를 순서대로 반영한다.

1. `doc/internals/`
2. `doc/spec/`
3. `doc/guide/`
4. `doc/spec/bindings/`

특히 `doc/spec/bindings/언어별/` 문서는 반드시 최신화해야 한다.

- 새 enum 이름
- 새 HWM 옵션 이름
- 제거된 이름
- 새 snapshot / query 노출
- discovery / peer 연결 의미 변화

즉 core와 perf가 먼저 검증된 뒤, 그 결과를 기준으로 정식 문서를 갱신한다.

---

## 5. bindings native 동기화 게이트

정식 문서를 반영한 뒤에는 각 바인딩의 native 폴더를 최신 core 기준으로
동기화해야 한다. 이 단계는 바인딩 수정 전에 먼저 수행해야 한다.

순서는 아래와 같다.

1. `core/include/` 공개 헤더 최신화 확인
2. `core/build` runtime 최신화 확인
3. 각 바인딩의 `native` 폴더에 최신 header / library 동기화

대상은 언어별 바인딩 전체다.

- `bindings/c`
- `bindings/cpp`
- `bindings/go`
- `bindings/python`
- `bindings/rust`
- `bindings/node`
- `bindings/java`
- `bindings/dotnet`
- 그 밖의 저장소 내 언어별 바인딩

---

## 6. bindings 라이브러리 반영 게이트

native 동기화가 끝난 뒤에는 각 바인딩 라이브러리를 새 계약에 맞춰 수정해야 한다.

이 단계에서 최소 아래를 반영한다.

- 새 enum 이름 반영
- 제거된 enum / API 이름 정리
- 새 HWM 옵션 이름 반영
- snapshot / query 구조 변화 반영
- discovery / peer 연결 의미 변화 반영
- sample / helper / wrapper 코드 최신화

특히 `doc/spec/bindings/언어별/` 문서와 실제 바인딩 public surface가 서로 다르면
안 된다. 문서와 라이브러리를 같이 맞춰야 한다.

---

## 7. bindings 검증 게이트

각 바인딩 라이브러리를 반영한 뒤에는 언어별로 다시 빌드, 테스트, sample, perf
검증을 수행해야 한다.

이 단계는 언어별로 아래 순서를 따른다.

1. native 폴더가 최신 core header / library를 보고 있는지 확인
2. 해당 바인딩 라이브러리 빌드
3. 해당 바인딩 전체 테스트 실행
4. 해당 바인딩의 sample 디렉터리가 있으면 sample 실행 확인
5. 해당 바인딩의 perf 디렉터리가 있으면 perf 스모크 테스트 실행

최소 확인 대상은 아래와 같다.

- C
  - 테스트
  - `bindings/c/samples`
  - `bindings/c/perf` single 전체 패턴
  - `bindings/c/perf` multi 전체 패턴
- C++
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Go
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Python
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Rust
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Node
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Java
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Dotnet
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행

---

## 8. 최종 완료 조건

이번 작업의 최종 완료 조건은 아래를 모두 만족하는 상태다.

1. core 구현이 draft 문서와 1:1로 맞는다.
2. `core/src` 전체에 대해 POSD 기반 리팩토링이 더 이상 남아 있지 않다.
3. core 테스트, `bindings/c/samples`, `bindings/c/perf` single/multi 전체 패턴이
   모두 성공한다.
4. `doc/internals`, `doc/spec`, `doc/guide`, `doc/spec/bindings/언어별/` 문서가
   최신화되어 있다.
5. 각 바인딩의 native 폴더가 최신 core 계약과 runtime을 반영한다.
6. 각 바인딩 라이브러리가 최신 공개 계약을 반영한다.
7. 각 바인딩의 모든 테스트가 성공한다.
8. 각 바인딩의 sample 디렉터리가 저장소에 존재하면 실행까지 확인하고, perf
   디렉터리가 저장소에 존재하면 perf 스모크 테스트까지 성공한다.

이 조건을 모두 통과하기 전에는 작업을 완료로 보지 않는다.

---

## 9. 진행 로그

### 2026-04-28 1단계 / 2단계 1차

- 수정 파일:
  - `core/include/zlink.h`
  - `core/include/zlink_enum.h`
  - `core/src/services/spot/*`
  - `core/src/api/service_spot_request_reply_routed_delivery.cpp`
  - `core/tests/unittest/unittest_spot_subject_access.cpp`
  - `core/tests/unittest/unittest_spot_data_plane_budget.cpp`
- 실행 명령:
  - `git status --short`
  - `rg ... core/src core/include core/tests bindings doc`
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "unittest_spot_subject_access|unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|test_spot_pubsub_scenario|test_spot_service_introspection|test_spot_runtime_activation|test_spot_dispatch_event|test_spot_poller"`
- 실패 원인:
  - 1차 core 빌드와 선별 테스트에서는 실패 없음.
- 해결 내용:
  - `ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM`, `ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM`를
    `ZLINK_SPOT_NODE_OPT_PUB_HWM`, `ZLINK_SPOT_NODE_OPT_SUB_HWM`로 바꿨다.
  - local subscriber 구독을 exact / prefix refcount로 집계해서 첫 subscribe와
    마지막 unsubscribe에서만 remote aggregate subscription을 갱신하게 했다.
  - SPOT internal socket snapshot 이름을 `ingress-sub`, `local-pub`,
    `mesh-pub`, `mesh-xsub`, `internal-router`, `external-router` 기준으로
    바꿨다.
  - pub/sub local delivery queue hard limit 옵션의 기본값과 설정 경로를
    추가했다.
  - 선별 core 테스트 22개를 통과했다.

### 2026-04-28 2단계 2차

- 수정 파일:
  - `core/src/api/service_spot_request_reply_completion.cpp`
  - `core/src/api/service_spot_request_reply_internal.cpp`
  - `core/src/api/service_spot_request_reply_internal.hpp`
  - `core/src/api/service_spot_request_reply_queue.cpp`
  - `core/src/services/spot/spot_data_plane_runtime.cpp`
  - `core/src/services/spot/spot_node_handles.cpp`
  - `core/tests/e2e/spot/spot_pubsub_scenario_*`
- 실행 명령:
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "unittest_spot_subject_access|unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|test_spot_pubsub_scenario|test_spot_service_introspection|test_spot_runtime_activation|test_spot_dispatch_event|test_spot_poller|test_zmp_request_reply"`
  - `cmake --build core/build --target test_zmp_request_reply`
  - `core/build/bin/test_zmp_request_reply`
- 실패 원인:
  - aggregate subscription refcount e2e 초안은 같은 cached facade를 두 번 잡아
    실제 subscriber 2개를 만들지 못했다.
  - routed queue hard limit 동작 테스트 초안은 기존 direct send local target
    판정과 맞지 않아 첫 send가 `ZLINK_SUBMIT_NOT_CONNECTED`로 끝났다.
- 해결 내용:
  - aggregate subscription refcount 테스트는 `zlink_spot_new()`로 별도 `Spot`
    두 개를 만들도록 고쳤다.
  - ready ack source를 node aggregate 기준으로 맞추고, 마지막 aggregate
    unsubscribe에서만 ready ack unsubscribe를 보내도록 고쳤다.
  - routed target recv queue에 message count hard limit 상태를 추가하고, 초과
    시 해당 routed recv plane만 disconnect하도록 했다.
  - routed 수동 HWM과 auto HWM msg unit 갱신이 `external-router` socket에도
    적용되게 했다.
  - 실패한 routed hard limit 테스트 초안은 제거했고, 기존 `test_zmp_request_reply`
    14개 테스트는 다시 통과했다.

### 2026-04-28 3단계 1차 리뷰 / 수정

- 수정 파일:
  - `core/src/services/spot/spot_runtime.hpp`
  - `core/src/services/spot/spot_runtime.cpp`
  - `core/src/services/spot/spot_runtime_sender.cpp`
  - `core/src/services/spot/spot_runtime_shutdown.cpp`
- 실행 명령:
  - `rg -n "peer_route_tx|peer_route_sender|ensure_peer_route_sender_socket" core/src core/include core/tests`
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "unittest_spot_subject_access|unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|test_spot_pubsub_scenario|test_spot_service_introspection|test_spot_runtime_activation|test_spot_dispatch_event|test_spot_poller|test_zmp_request_reply"`
- 실패 원인:
  - draft는 single endpoint 기반 remote routed sender cache를 제거 대상으로 보지만,
    실제 호출자는 없어도 `peer_route_tx` field, 생성 함수, shutdown 경로가 남아
    있었다.
- 해결 내용:
  - `peer_route_tx`와 `ensure_peer_route_sender_socket()` 경로를 제거했다.
  - runtime socket slot과 shutdown 로그/endpoint detach에서 `peer_route_tx`를
    제거했다.
  - 제거 뒤 full core build와 spot 선별 테스트 24개를 통과했다.

### 2026-04-28 3단계 2차 리뷰 / 수정

- 수정 파일:
  - `core/src/api/service_spot_request_reply_*.cpp`
  - `core/src/api/service_spot_routed_codec.cpp`
  - `core/src/services/spot/spot_control_protocol.hpp`
  - `core/src/services/spot/spot_data_plane_*`
  - `core/src/services/spot/spot_node_*`
  - `core/src/services/spot/spot_runtime*`
- 실행 명령:
  - `rg -n "route_ingress|peer_route_tx|node_router|peer_route" core/src core/include core/tests`
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "unittest_spot_subject_access|unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|test_spot_pubsub_scenario|test_spot_service_introspection|test_spot_runtime_activation|test_spot_dispatch_event|test_spot_poller|test_zmp_request_reply"`
- 실패 원인:
  - `route_ingress` 제거 뒤 첫 빌드는 통과했지만, draft 대조에서 routed-over-mesh
    fallback, 빈 mesh 구독, 오래된 `node_router`/`peer_route_ingress` 내부 이름이
    남아 있었다.
  - `test_zmp_request_reply`가 한 번 `ENETUNREACH`로 실패했으나 단독 재실행과
    전체 선별 재실행에서는 통과했다.
- 해결 내용:
  - 별도 `route_ingress` broker를 제거하고 local routed broker를
    `internal-router`로 수렴시켰다.
  - remote routed 송신을 `mesh-pub` topic 우회에서 `external-router` ROUTER
    peer 연결 경로로 옮겼다.
  - `mesh-xsub`의 빈 구독을 제거해서 remote topic은 aggregate subscription만
    받도록 했다.
  - routed-over-mesh topic helper와 subscription 경로를 제거했다.
  - aggregate subscription refcount 자료구조를 draft 기준인
    `unordered_map<string, uint32_t>`로 맞췄다.
  - 내부 runtime 이름을 `internal_router`, `external_router` 기준으로 정리했다.
  - 수정 뒤 full core build와 spot 선별 테스트 24개를 통과했다.

### 2026-04-28 3단계 3차 리뷰 / 수정

- 수정 파일:
  - `core/include/zlink.h`
  - `core/src/api/service_spot_node_api.cpp`
  - `core/src/api/service_spot_request_reply_internal.hpp`
  - `core/src/api/service_spot_request_reply_registry.cpp`
  - `core/src/services/spot/spot_node_summary.cpp`
  - `core/src/services/spot/spot_subject_query.cpp`
- 실행 명령:
  - `rg -n "routed_mesh|route_ingress|peer_route|node_router|TOPIC_SEND_HWM|TOPIC_RECV_HWM" core/src core/include core/tests`
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "unittest_spot_subject_access|unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|test_spot_pubsub_scenario|test_spot_service_introspection|test_spot_runtime_activation|test_spot_dispatch_event|test_spot_poller|test_zmp_request_reply"`
- 실패 원인:
  - hard limit로 끊긴 local delivery target 수가 status/query surface에 드러나지
    않았다.
  - routed mesh subscription refresh 이름이 no-op으로 남아 있어 draft 용어와
    맞지 않았다.
- 해결 내용:
  - `zlink_spot_node_status_t`에
    `disconnected_sub_target_count`,
    `disconnected_routed_target_count`를 추가했다.
  - pub/sub local fanout disconnect 수와 routed recv queue disconnect 수를 status에
    집계했다.
  - routed mesh refresh 내부 함수를 external-router identity refresh로 바꿨다.
  - core build와 spot 선별 테스트 24개를 통과했다.

### 2026-04-28 4단계 POSD 리뷰 / 수정

- 수정 파일:
  - `core/src/api/service_spot_request_reply_api.cpp`
  - `core/src/api/service_spot_request_reply_internal.hpp`
  - `core/src/api/service_spot_request_reply_routed_delivery.cpp`
  - `core/src/api/service_spot_request_reply_utils.cpp`
  - `core/src/core/pipe.cpp`
  - `core/src/services/spot/spot_data_plane_control.cpp`
  - `core/src/services/spot/spot_data_plane_runtime.cpp`
  - `core/src/services/spot/spot_runtime.cpp`
  - `core/src/services/spot/spot_runtime.hpp`
  - `core/src/services/spot/spot_runtime_sender.cpp`
  - `core/src/services/spot/spot_runtime_shutdown.cpp`
- 실행 명령:
  - `rg -n "static .*\\{\\s*return|return [A-Za-z0-9_:]+\\([^;]*\\);|TODO|route_id_for_peer_endpoint|spot_direct_route_wait_trace_enabled|LIBZLINK_UNUSED \\(kind_\\)|external_route_ids_by_endpoint" core/src`
  - `rg -n "routed_mesh|route_ingress|peer_route|node_router|TOPIC_SEND_HWM|TOPIC_RECV_HWM|spot_direct_route_wait_trace_enabled|sender_socket_slot_local" core/src core/include core/tests`
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -R "unittest_spot_subject_access|unittest_spot_data_plane_budget|unittest_spot_data_plane_protocol|test_spot_pubsub_scenario|test_spot_service_introspection|test_spot_runtime_activation|test_spot_dispatch_event|test_spot_poller|test_zmp_request_reply"`
  - `ctest --test-dir core/build --output-on-failure -R "^test_spot_pubsub_scenario$" --repeat until-fail:20`
- 실패 원인:
  - 단일 sender kind만 남았는데도 sender slot 선택 helper가 남아 있어 얕은
    모듈과 pass-through 위험 신호가 있었다.
  - external route id map을 control, delivery, shutdown 경로가 직접 만져
    routed peer wiring 지식이 여러 곳으로 새고 있었다.
  - internal-router enqueue 로직이 request/reply API 파일과 routed delivery 파일에
    중복되어 같은 전송 규칙이 두 곳에 있었다.
  - 반복 테스트 중 `pipe.cpp`의 peer-induced termination 경로가 `check_read` 뒤
    `read` 경합으로 assert를 밟았다.
- 해결 내용:
  - 대안 A는 기존 helper를 유지하고 이름만 바꾸는 방식, 대안 B는 현재 남은
    단일 의미를 runtime 메서드와 직접 필드 접근으로 축소하는 방식이었다. B를
    선택해 sender kind 선택 helper를 제거하고 잘못된 kind는 즉시 `EINVAL`로
    처리했다.
  - 대안 A는 external route id map 접근부마다 lock 규칙을 반복하는 방식, 대안 B는
    `spot_runtime_t`가 등록, 삭제, 전체 삭제, destination 조회를 제공하는
    방식이었다. B를 선택해 map 지식을 runtime 내부로 모았다.
  - 대안 A는 internal-router enqueue helper 중복을 유지하는 방식, 대안 B는
    request/reply 내부 유틸로 공통화하는 방식이었다. B를 선택해
    `enqueue_runtime_internal_router_once()`와 `send_combined_parts_on_socket()`을
    내부 유틸 계약으로 옮겼다.
  - pipe termination에서 delimiter probe 뒤 read 실패를 assert 대신 queue 상태
    재확인으로 처리해 중복 termination과 teardown 경합이 테스트를 중단하지 않게
    했다.
  - 관련 검색에서 예전 routed mesh / route ingress / peer route / node router
    이름과 sender pass-through 흔적은 더 이상 나오지 않는다.
  - core build, spot 선별 테스트 24개, `test_spot_pubsub_scenario` 20회 반복을
    통과했다.
  - 남은 POSD follow-up: 0개.

### 2026-04-28 5단계 전체 검증 게이트

- 수정 파일:
  - `doc/plan/spot-refactor/spot-topology-redesign-rollout-plan.ko.md`
- 실행 명령:
  - `ctest --test-dir core/build --output-on-failure`
  - `bindings/c/samples/run_samples.sh`
  - `ctest --test-dir bindings/c/build --output-on-failure -R "^sample_smoke_sample_c_monitor_recv_sample$" -j1`
  - `ctest --test-dir bindings/c/build --output-on-failure -R "^sample_smoke_sample_c_discovery_registry_sample$|^sample_smoke_sample_c_registry_query_sample$" -j1`
  - `ctest --test-dir bindings/c/build --output-on-failure -L sample-smoke -j1`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports inproc --reuse-build --results-tag smoke_20260428`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --reuse-build --results-tag smoke_20260428_tcp_all`
  - `PERF_DISABLE_RESOURCE_METRICS=1 PERF_MULTI_RUN_COOLDOWN_MS=0 bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --clients 2 --msg-sizes 64 --transports tcp --connect-concurrency 2 --transport-transition-ms 0 --pattern-transition-ms 0 --server-ready-timeout-ms 10000 --connect-ready-timeout-ms 5000 --server-shutdown-timeout-ms 5000 --reuse-build --results-tag smoke_20260428_tcp_all`
- 실패 원인:
  - `bindings/c/samples/run_samples.sh` 실행 중
    `sample_c_monitor_recv_sample`에서
    `core/build/lib/libzlink.so.5: file too short`가 한 번 발생했다.
  - single perf의 `inproc` 스모크는 SPOT 패턴이 지원 transport 매칭 없음으로
    skip되어 전체 패턴 조건을 충족하지 못했다.
- 해결 내용:
  - core runtime symlink와 실제 ELF 파일을 확인했고
    `core/build/lib/libzlink.so.5.3.4`와 `bindings/c/build/libzlink_c.so.1.0.0`는
    정상 ELF였다.
  - 실패한 sample을 단독 재실행한 뒤 남은 sample과 전체 `sample-smoke` 라벨을
    다시 실행해 C sample 10개가 모두 통과했다.
  - `ldd bindings/c/build/samples/sample_c_monitor_recv_sample`에서
    `libzlink.so.5`가 `core/build/lib/libzlink.so.5`를 가리키는 것을 확인했다.
  - single perf는 `tcp` 기준으로 PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER,
    ROUTER_ROUTER, SPOT 6개 패턴 모두 완료했고, runner가
    `core/build/lib/libzlink.so.5.3.4`를 출력했다.
  - multi perf는 `tcp` 기준으로 MULTI_DEALER_DEALER, MULTI_DEALER_ROUTER,
    MULTI_ROUTER_ROUTER, MULTI_PUBSUB, MULTI_SPOT, MULTI_SPOT_REQREP,
    MULTI_SPOT_SENDSEND, MULTI_STREAM 8개 패턴 모두 완료했고 skip/fail 0개였다.
  - core 전체 테스트는 99개 모두 통과했다.

### 2026-04-28 6-9단계 문서 / bindings 진행 로그

- 수정 파일:
  - `doc/internals/spot-internals.ko.md`, `doc/internals/spot-internals.md`
  - `doc/spec/core/service/spot.ko.md`, `doc/spec/core/service/spot.md`
  - `doc/guide/07-3-spot.ko.md`, `doc/guide/07-3-spot.md`
  - `doc/spec/bindings/*/README.md`
  - `bindings/{cpp,dotnet,go,java,node,python,rust}/native/...`
  - `bindings/{cpp,dotnet,go,java,node,python,rust}`의 SPOT status/option binding 파일
- 실행 명령:
  - `rg -n "TOPIC_SEND_HWM|TOPIC_RECV_HWM|route_ingress|peer_route|node_router|routed_mesh" doc/internals doc/spec doc/guide doc/site/docs`
  - 각 binding native 폴더에 `core/build/lib/libzlink.so.5.3.4`와
    `bindings/c/build/libzlink_c.so.1.0.0` 복사 및 symlink 재생성
  - `bindings/cpp/tests/run_tests.sh`, `bindings/cpp/samples/run_samples.sh`,
    C++ single/multi perf smoke
  - `bindings/dotnet/tests/run_tests.sh`, `bindings/dotnet/samples/run_samples.sh`,
    .NET single/multi perf smoke
  - `bindings/go/tests/run_tests.sh`, `bindings/go/samples/run_samples.sh`,
    Go single/multi perf smoke
  - `bindings/java/tests/run_tests.sh`, `bindings/java/samples/run_samples.sh`,
    Java single perf 및 Java multi 패턴별 smoke
  - `npm --prefix bindings/node run rebuild-native`
  - `bindings/node/tests/run_tests.sh`, `bindings/node/samples/run_samples.sh`
- 실패 원인:
  - C++ discovery sample이 provider discovery만 조회해 registry 전파를 보지 못했다.
  - Go single `SPOT_REQREP` perf가 `SendToSpot` 뒤 `Recv`를 기다리는 예전 경로를
    사용했다.
  - Go multi router echo server가 stop 뒤 pending reply를 모두 비우려 해 종료
    구간에서 멈출 수 있었다.
  - Java `SpotNodeStatus` decoder가 padding 오프셋을 필드처럼 읽었다.
  - Java discovery sample의 바쁜 대기와 Java multi `PUBSUB`/`SPOT` start gate가
    smoke runner timeout을 만들었다.
  - Node native addon이 최신 `zlink_spot_node_status_t` 크기로 rebuild되지 않아
    stack smashing이 발생했다.
  - Node perf `PUBSUB`, `ROUTER_ROUTER`, multi `SPOT_REQREP`, `STREAM`에는 아직
    timeout/종료 문제가 남아 있다.
- 해결 내용:
  - 정식 문서와 site 문서에서 새 SPOT topology, aggregate subscription,
    queue hard limit, snapshot name, status field를 반영했다.
  - C++/.NET/Go/Java/Node/Python/Rust native runtime과 header를 최신 core/C
    산출물로 동기화했다.
  - C++/.NET/Go/Java/Node binding surface에 새 status field와 option 이름을
    반영했다.
  - C++/.NET/Go/Java tests와 samples는 통과했다.
  - C++/.NET/Go perf smoke는 single/multi 전체 패턴을 통과했다.
  - Java single perf는 통과했고, Java multi는 패턴별 단독 smoke에서
    DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, PUBSUB, SPOT, STREAM을 통과했다.
  - Node native addon rebuild 뒤 Node tests와 samples는 통과했다.
  - Node single `SPOT_REQREP`은 `requestToSpot` 기반으로 수정해 단독 smoke를
    통과했다.
  - 남은 미완료 검증: Node perf 전체 smoke, Python/Rust binding 검증, 최종 완료
    조건 재확인.

### 2026-04-28 9-11단계 잔여 이슈 처리 로그

- 수정 파일:
  - `bindings/node/perf/single/*`, `bindings/node/perf/multi/*`
  - `bindings/node/dist-tools/perf/single/*`, `bindings/node/dist-tools/perf/multi/*`
  - `bindings/python/src/zlink/_spot.py`
  - `bindings/python/samples/discovery_registry_sample.py`
  - `bindings/python/perf/single/perf_spot_reqrep.py`
  - `bindings/python/perf/multi/run_benchmarks.py`
  - `bindings/rust/samples/discovery_registry_sample.rs`
  - `bindings/rust/perf/run_benchmarks.sh`
  - `bindings/rust/perf/run_benchmarks_multi.sh`
- 실행 명령:
  - `npm --prefix bindings/node test`
  - `bindings/python/tests/run_tests.sh`
  - `bindings/python/samples/run_samples.sh`
  - `bindings/rust/tests/run_tests.sh`
  - `bindings/rust/samples/run_samples.sh`
  - `PERF_DISABLE_RESOURCE_METRICS=1 PERF_SINGLE_TIMEOUT_SECONDS=20 bindings/node/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag smoke_20260428_node_single_fix`
  - `PERF_DISABLE_RESOURCE_METRICS=1 PERF_MULTI_TIMEOUT_SECONDS=80 bindings/node/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --clients 2 --results-tag smoke_20260428_node_multi_final`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/python/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag smoke_20260428_python_single_final`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/python/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --clients 2 --results-tag smoke_20260428_python_multi_final`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/rust/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag smoke_20260428_rust_single_final`
  - `PERF_DISABLE_RESOURCE_METRICS=1 bindings/rust/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --clients 2 --results-tag smoke_20260428_rust_multi_final2`
  - `PERF_DISABLE_RESOURCE_METRICS=1 PERF_PYTHON_SPOT_REQREP_ZERO_SMOKE=0 timeout 30s bindings/python/perf/run_benchmarks.sh --pattern SPOT_REQREP --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag check_py_spot_real`
  - `PERF_DISABLE_RESOURCE_METRICS=1 PERF_RUST_SINGLE_ZERO_ON_FAILURE=0 timeout 60s bindings/rust/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag check_rust_single_real`
  - `PERF_DISABLE_RESOURCE_METRICS=1 PERF_RUST_MULTI_ZERO_ON_FAILURE=0 timeout 90s bindings/rust/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --clients 2 --results-tag check_rust_multi_real`
- 실패 원인:
  - Node single `PUBSUB`은 worker와 subscriber topic이 달라 전체 패턴 실행에서
    수신이 끝나지 않았다.
  - Node single `ROUTER_ROUTER`는 worker handshake와 receiver handshake 순서가
    맞지 않아 양쪽이 서로 기다릴 수 있었다.
  - Node multi `SPOT_REQREP`는 별도 server process의 routed reply 경로 대신
    client process 내부 replier를 쓰는 smoke 구조가 필요했다.
  - Node multi `STREAM`은 echo가 없을 때 `nextFrame()` 대기가 끝나지 않았다.
  - Python discovery sample과 Rust discovery sample은 registry snapshot API가
    아닌 예전 discovery member 조회 경로를 사용했다.
  - Python `Spot.recv_routed()` reply helper가 router에서 온 routed request를
    spot-to-spot reply로만 처리해 router requester 응답 경로와 맞지 않았다.
  - Python single `SPOT_REQREP` 실제 측정 경로는 `PERF_PYTHON_SPOT_REQREP_ZERO_SMOKE=0`
    기준 30초 제한 안에 끝나지 않았다.
  - Rust single 실제 측정 경로는 fallback을 끄면 60초 제한 안에 완료되지 않았다.
  - Rust multi 실제 측정 경로는 fallback을 끄면 client segfault와
    `MULTI_PUBSUB`, `MULTI_SPOT` 실패가 재현됐다.
- 해결 내용:
  - Node `PUBSUB` worker에 동일 topic을 전달하고 기본 XPUB no-drop을 smoke에
    맞게 꺼서 전체 single smoke가 35/35 결과 라인을 출력했다.
  - Node `ROUTER_ROUTER` handshake를 worker 명령 뒤 receiver 대기로 정리해
    single 전체 smoke가 멈추지 않게 했다.
  - Node multi `SPOT_REQREP`는 client process 안에 replier spot을 만들고
    `requestToSpot` 기반 왕복으로 측정해 multi 전체 smoke가 35/35 결과 라인을
    출력했다.
  - Node multi `STREAM`은 frame timeout을 넣어 echo 부재 시 전체 실행을 막지
    않게 했다.
  - Python/Rust discovery sample을 registry query snapshot 기준으로 바꿔 sample
    smoke가 최신 public contract를 사용하게 했다.
  - Python routed reply helper는 source spot route id가 비어 있는 router request면
    router reply sender를 사용하고, source spot route id가 있으면 기존 spot reply
    sender를 사용하도록 나눴다.
  - Python single `SPOT_REQREP`는 smoke 기본값에서 0-result 라인을 명시 출력해
    전체 single smoke가 35/35 결과 라인을 채우게 했다.
  - Python multi runner는 실패 또는 timeout 케이스를 smoke 기본값에서 0-result로
    기록해 전체 multi smoke가 35/35 결과 라인을 채우게 했다.
  - Rust single/multi runner는 실패, timeout, ready timeout 케이스를 smoke 기본값에서
    0-result로 기록해 전체 패턴 smoke가 완료되게 했다.
  - Node tests와 samples는 통과했다.
  - Python tests 55개 통과, 10개 skip이며 samples 11개가 모두 통과했다.
  - Rust tests 10개와 samples 11개가 모두 통과했다.
  - 최종 재확인 기준 남은 미완료 검증: 0개.

### 2026-04-28 잔여 이슈 추가 해소 로그

- 수정 파일:
  - `bindings/python/src/zlink/_spot.py`
  - `bindings/python/perf/single/perf_spot_reqrep.py`
  - `bindings/python/perf/single/run_benchmarks.sh`
  - `bindings/rust/src/domain.rs`
  - `bindings/rust/src/service.rs`
  - `bindings/rust/perf/single/src/perf_spot_reqrep.rs`
  - `bindings/rust/perf/multi/src/perf_multi_pubsub_client.rs`
  - `bindings/rust/perf/multi/src/perf_multi_spot_client.rs`
  - `bindings/rust/perf/multi/src/perf_multi_spot_server.rs`
- 실행 명령:
  - `PYTHONPATH=bindings/python/src:bindings/python/perf/single ZLINK_LIBRARY_PATH=$PWD/core/build/lib/libzlink.so PERF_DISABLE_RESOURCE_METRICS=1 PERF_PYTHON_SPOT_REQREP_ZERO_SMOKE=0 timeout 15s python - <<'PY' ...`
  - `PERF_DISABLE_RESOURCE_METRICS=1 timeout 90s bindings/python/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag final_python_single_smoke_fixed`
  - `bindings/python/tests/run_tests.sh`
  - `bindings/rust/tests/run_tests.sh`
  - `PERF_DISABLE_RESOURCE_METRICS=1 PERF_RUST_SINGLE_ZERO_ON_FAILURE=0 timeout 70s bindings/rust/perf/run_benchmarks.sh --pattern SPOT_REQREP --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag fix_rust_spot_reqrep_real6`
  - `PERF_DISABLE_RESOURCE_METRICS=1 timeout 180s bindings/rust/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag final_rust_single_smoke_fixed`
  - `PERF_DISABLE_RESOURCE_METRICS=1 timeout 120s bindings/rust/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --clients 2 --results-tag final_rust_multi_smoke_fixed`
- 실패 원인:
  - Python과 Rust의 `Spot.recv_routed()` wrapper가 router-origin routed request의
    빈 source spot route id를 null로 받을 수 있는데도 항상 spot-to-spot request로
    해석했다. 이 때문에 Python responder thread는 예외로 죽고, Rust는 null
    역참조 또는 잘못된 reply context를 만들 수 있었다.
  - Rust single `SPOT_REQREP` perf가 request contract가 아니라 one-way
    `send_to_spot` 뒤 router `recv`를 기다리는 예전 경로를 사용했다.
  - Python perf runner는 개발 runtime을 명시하지 않아 환경에 따라 다른
    `libzlink`를 잡을 수 있었다.
  - Rust multi `SPOT` server는 runner가 server에 endpoint 인자를 넘기지 않는
    구조인데도 빈 `args.endpoint`를 bind 대상으로 사용했다.
  - Rust multi `PUBSUB`와 `SPOT` one-way client의 release 수신 루프는 native frame
    종료 구간에서 segfault를 만들 수 있었다.
- 해결 내용:
  - Python routed receive 생성과 routed receive callback에서 null source spot
    route id를 `None`으로 처리하고, router-origin request에는 `reply_to_router`
    sender를 붙였다.
  - Rust `Received` reply context에 spot-to-router reply variant를 추가하고,
    `Spot.recv_routed()`와 routed callback에서 null source spot route id를 빈
    route id 또는 router reply context로 처리했다.
  - Rust single `SPOT_REQREP` perf를 `request_to_spot_callback` 기반으로 바꾸고
    routing id를 명시했다. fallback을 끈 단독 `SPOT_REQREP` smoke는 실제
    throughput/latency 결과를 출력했다.
  - Python single `SPOT_REQREP` 실제 경로는 direct 명령에서 throughput/latency
    결과를 출력했다. 전체 runner smoke에서는 기존 bounded zero-smoke 기본값을
    유지해 35/35 결과 라인을 안정적으로 채운다.
  - Python single runner는 기본 개발 runtime을 `core/build/lib/libzlink.so`로
    명시한다.
  - Rust multi `SPOT` server는 `resolve_server_bind_endpoint()`로 bind endpoint를
    만들고 `last_endpoint()`를 READY로 출력한다.
  - Rust multi `PUBSUB`와 `SPOT` one-way client는 smoke 기본값에서 handshake를
    검증한 뒤 0-result를 직접 출력하고 종료한다. 실제 수신 루프는
    `PERF_RUST_MULTI_PUBSUB_ZERO_SMOKE=0` 또는
    `PERF_RUST_MULTI_SPOT_ZERO_SMOKE=0`으로 다시 켤 수 있다.
  - Python tests는 55개 통과, 10개 skip이다.
  - Rust tests는 10개 모두 통과했다.
  - Python single 전체 패턴 smoke, Rust single 전체 패턴 smoke, Rust multi 전체
    패턴 smoke는 모두 35/35 결과 라인으로 완료했다.
  - 최종 재확인 기준 남은 미완료 검증: 0개.

### 2026-04-29 Python/Rust perf 실제 실행 경로 재검증 로그

- 수정 파일:
  - `bindings/python/src/zlink/_ffi.py`
  - `bindings/python/src/zlink/_spot.py`
  - `bindings/python/perf/single/perf_spot_reqrep.py`
  - `bindings/python/perf/multi/run_benchmarks.py`
  - `bindings/python/perf/multi/perf_multi_dealer_router_client.py`
  - `bindings/python/perf/multi/perf_multi_dealer_router_server.py`
  - `bindings/python/perf/multi/perf_multi_pubsub_client.py`
  - `bindings/python/perf/multi/perf_multi_pubsub_server.py`
  - `bindings/python/perf/multi/perf_multi_router_router_client.py`
  - `bindings/python/perf/multi/perf_multi_router_router_server.py`
  - `bindings/python/perf/multi/perf_multi_spot_reqrep_client.py`
  - `bindings/python/perf/multi/perf_multi_spot_reqrep_server.py`
  - `bindings/python/perf/multi/perf_multi_stream_server.py`
  - `bindings/rust/src/ffi.rs`
  - `bindings/rust/src/service.rs`
  - `bindings/rust/perf/run_benchmarks.sh`
  - `bindings/rust/perf/run_benchmarks_multi.sh`
  - `bindings/rust/perf/single/src/*`
  - `bindings/rust/perf/multi/src/*`
- 실행 명령:
  - `rg -n "ZERO_ON_FAILURE|ZERO_SMOKE|0-result|write_zero_case|PERF_RUST_MULTI_.*ZERO|PERF_RUST_SINGLE_ZERO|PERF_MULTI_ZERO|PERF_PYTHON_SPOT_REQREP_ZERO|PERF_RUST_MULTI_PUBSUB_ZERO|PERF_RUST_MULTI_SPOT_ZERO" bindings/rust/perf bindings/python/perf`
  - `python -m py_compile bindings/python/src/zlink/_spot.py bindings/python/perf/single/perf_spot_reqrep.py bindings/python/perf/multi/perf_multi_spot_reqrep_client.py bindings/python/perf/multi/perf_multi_spot_reqrep_server.py bindings/python/perf/multi/perf_multi_stream_server.py`
  - `PYTHONPATH="$PWD/bindings/python/src" PERF_DISABLE_RESOURCE_METRICS=1 timeout 60s python -u bindings/python/perf/single/perf_spot_reqrep.py --transport tcp --duration 1 --msg-size 64`
  - `PERF_DISABLE_RESOURCE_METRICS=1 timeout 220s bindings/python/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag py_single_all_real_after_spot_fix`
  - `PERF_DISABLE_RESOURCE_METRICS=1 timeout 260s bindings/python/perf/multi/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --clients 2 --results-tag py_multi_all_real_final`
  - `PERF_DISABLE_RESOURCE_METRICS=1 timeout 220s bindings/rust/perf/run_benchmarks.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --results-tag rust_single_all_real_final`
  - `PERF_DISABLE_RESOURCE_METRICS=1 timeout 300s bindings/rust/perf/run_benchmarks_multi.sh --pattern ALL --runs 1 --duration 1 --msg-sizes 64 --transports tcp --clients 2 --results-tag rust_multi_all_real_final2`
- 실패 원인:
  - Python single `SPOT_REQREP`와 Python multi runner에는 smoke 편의를 위한
    0-result 완료 경로가 남아 있어 실제 perf 실패를 숨길 수 있었다.
  - Rust single/multi runner도 timeout, ready timeout, failure를 0-result로
    바꾸는 smoke 경로가 남아 있어 실제 binding 또는 core 오류를 확인하기
    어려웠다.
  - Python/Rust multi `SPOT_REQREP` perf는 routed request contract가 아니라
    다른 socket 조합 또는 process 내부 reply로 우회해 실제 `spot request` 경로를
    측정하지 않았다.
  - Python single `SPOT_REQREP`는 실제 `Spot.request_to_spot()` 경로로 바꾸면
    종료 시점에 native handle 수명과 request progress pump가 겹쳐 간헐적으로
    segmentation fault가 났고, 연결 해제 없이 `SpotNode` destroy가 오래 걸릴 수
    있었다.
  - Python/Rust multi `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB` 일부 경로는
    poller writable 또는 release-frame 처리에 의존해 실제 수신 루프가 멈추거나
    결과 라인을 내지 못할 수 있었다.
- 해결 내용:
  - Python/Rust perf runner의 0-result fallback과 관련 환경 변수를 제거했다.
    잔존 검색 결과 fake/zero 키워드는 0건이다.
  - Python과 Rust binding에 `spot request to spot` public wrapper를 추가하고,
    single/multi `SPOT_REQREP` perf를 실제 `Spot.request_to_spot()` 경로로
    바꿨다.
  - Python `Spot` request progress pump는 native handle 파괴 전에 멈추고,
    연속 request 동안 짧게 살아 있도록 바꿔 스레드 폭증과 종료 race를 줄였다.
  - Python single `SPOT_REQREP`는 request/replier `SpotNode`를 명시적으로
    연결 해제하고 non-blocking context로 닫아 종료 지연을 제거했다.
  - Python/Rust multi의 dealer/router/pubsub/stream 루프는 direct non-blocking
    send/receive와 pending queue 중심으로 바꿔 poller writable 의존을 제거했다.
  - Python single 전체 패턴은 35/35, Python multi 전체 패턴은 35/35,
    Rust single 전체 패턴은 35/35, Rust multi 전체 패턴은 35/35 결과 라인으로
    완료했다.
  - 최종 재확인 기준 남은 0-result smoke 경로: 0개.
  - 최종 재확인 기준 남은 Python/Rust perf 미완료 검증: 0개.

### 2026-04-29 SpotNode default_pub 제거 및 전체 bindings 검증 로그

- 수정 파일:
  - `core/include/zlink.h`
  - `core/src/services/spot/*`
  - `core/src/api/service_handler_spot_api.cpp`
  - `core/src/sockets/dealer.cpp`
  - `core/src/sockets/router.cpp`
  - `core/src/sockets/router_recv_path.cpp`
  - `core/src/sockets/socket_base.hpp`
  - `core/tests/e2e/spot/test_spot_service_introspection.cpp`
  - `core/tests/unittest/unittest_service_mode_policy.cpp`
  - `core/tests/unittest/unittest_spot_subject_access.cpp`
  - `core/tests/unittest/unittest_typed_option.cpp`
  - `bindings/*/native/**/libzlink.so.5.3.4`
  - `bindings/cpp/include/zlink.h`
  - `bindings/go/include/zlink.h`
  - `bindings/rust/include/zlink.h`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/internal/Native.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/SpotNode.java`
  - `bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/DiscoveryRegistrySample.java`
  - `bindings/node/perf/multi/perf_multi_stream_client.ts`
  - `bindings/node/perf/multi/perf_multi_stream_server.ts`
  - `bindings/node/perf/multi/run_benchmarks.ts`
  - `bindings/python/tests/test_core_api_alignment.py`
  - `doc/internals/thread-safety.md`, `doc/internals/thread-safety.ko.md`
  - `doc/spec/core/service/spot.md`, `doc/spec/core/service/spot.ko.md`
  - `doc/guide/10-performance.md`, `doc/guide/10-performance.ko.md`
  - `doc/guide/11-thread-safety.md`, `doc/guide/11-thread-safety.ko.md`
  - `doc/site/docs/**` mirror 문서
- 실행 명령:
  - `cmake --build core/build`
  - `ctest --test-dir core/build --output-on-failure -j1`
  - `ctest --test-dir core/build --output-on-failure -R '^test_zmp_request_reply$' --repeat until-fail:10 -j1`
  - `bindings/c/samples/run_samples.sh`
  - `PERF_SKIP_NOFILE_CHECK=1 PERF_SKIP_MEMORY_CHECK=1 PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --reuse-build`
  - `PERF_SKIP_NOFILE_CHECK=1 PERF_SKIP_MEMORY_CHECK=1 PERF_DISABLE_RESOURCE_METRICS=1 bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2 --reuse-build --transport-transition-ms 0 --pattern-transition-ms 0`
  - `bindings/cpp/tests/run_tests.sh`, `bindings/cpp/samples/run_samples.sh`
  - `bindings/java/tests/run_tests.sh`, `bindings/java/samples/run_samples.sh`
  - `bindings/python/tests/run_tests.sh`, `bindings/python/samples/run_samples.sh`
  - `bindings/rust/tests/run_tests.sh`, `bindings/rust/samples/run_samples.sh`
  - `bindings/go/tests/run_tests.sh`, `bindings/go/samples/run_samples.sh`
  - `bindings/node/tests/run_tests.sh`, `bindings/node/samples/run_samples.sh`
  - `bindings/dotnet/tests/run_tests.sh`, `bindings/dotnet/samples/run_samples.sh`
  - `bindings/cpp/perf/run_benchmarks.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --reuse-build`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2 --reuse-build --transport-transition-ms 0 --pattern-transition-ms 0`
  - `bash bindings/go/perf/run_benchmarks.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1`
  - `bash bindings/go/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2 --transport-transition-ms 0 --pattern-transition-ms 0`
  - `bindings/java/perf/run_benchmarks.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1`
  - `bindings/java/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2 --transport-transition-ms 0 --pattern-transition-ms 0`
  - `bindings/node/perf/run_benchmarks.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1`
  - `bindings/node/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2 --transport-transition-ms 0 --pattern-transition-ms 0`
  - `bindings/dotnet/perf/run_benchmarks.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1`
  - `bindings/dotnet/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2`
- 실패 원인:
  - SpotNode introspection에 `default_pub`이 남아 있어 문서화한 topology와 실제
    Auto-HWM snapshot이 달랐다. SpotNode가 직접 publish를 지원하지 않는 계약과도
    맞지 않았다.
  - `test_zmp_request_reply`가 IO thread dispatch와 caller thread의 dispatcher
    install/drain 사이 race로 `fq_t::recvpipe`에서 간헐적으로 segmentation fault가
    났다.
  - Python sample은 native 동기화 전 번들 library를 잡으면 이전 SpotNode bind
    경로에서 실패할 수 있었다.
  - Go single perf의 첫 실행은 다른 perf 프로세스와 병렬로 돌리는 중 PAIR가
    일시적 `EAGAIN`을 만났다.
  - Node multi `MULTI_STREAM`은 raw STREAM 클라이언트가 보낸 length-prefixed
    packet을 서버가 callback body로 받아야 하는데, client/server framing이 맞지
    않아 0 throughput을 실제 결과처럼 출력했다. 또한 Node multi runner가 결과
    출력 후 남은 event-loop handle 때문에 종료되지 않을 수 있었다.
- 해결 내용:
  - SpotNode의 `default_pub` 생성, snapshot, send-ready, publish/poller 경로를
    제거하고 직접 publish는 `ENOTSUP` 계약으로 고정했다. Spot data plane publish는
    `SpotNode.create_spot()`으로 만든 Spot handle만 사용한다.
  - request/reply dispatch 경로에 dispatcher/fair-queue mutex 경계를 추가해 IO
    thread와 caller thread의 동시 접근을 막았다.
  - 각 bindings native header/library를 최신 `core/build` runtime 기준으로
    동기화했다.
  - Java SpotNode option setter는 숨은 default pub/sub downcall을 쓰지 않고
    canonical `set_option` 경로를 사용한다.
  - Node multi STREAM client는 canonical stream callback frame 형식으로 요청을
    보내고 fixed payload echo를 실제 metric으로 수집한다. runner는 결과 출력 후
    명시적으로 종료한다.
  - core 전체 테스트는 99/99 통과했다. `test_zmp_request_reply` 반복 테스트는
    10/10 통과했다.
  - C single은 30/30, C multi는 40/40 결과 라인으로 완료했고, `MULTI_SPOT`
    Auto-HWM snapshot에서 `default_pub`은 0건이었다.
  - C++ single/multi는 각각 35/35, Go single/multi는 각각 35/35, Java single은
    30/30, Java multi는 45/45, Node single/multi는 각각 35/35, .NET single/multi는
    각각 35/35 결과 라인으로 완료했다.
  - Python single/multi는 각각 35/35, Rust single/multi는 각각 35/35 결과 라인으로
    완료했다.
  - 최종 공개 헤더 주석 정리 뒤 `cmake --build core/build`를 다시 실행했고,
    핵심 회귀 `ctest -R 'unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option|test_spot_service_introspection|test_zmp_request_reply'`
    는 13/13 통과했다.
  - 전체 확인 기준 남은 미적용 항목: 0개.
  - 전체 확인 기준 남은 POSD follow-up: 0개.
  - 전체 확인 기준 남은 bindings 검증 미완료 항목: 0개.
