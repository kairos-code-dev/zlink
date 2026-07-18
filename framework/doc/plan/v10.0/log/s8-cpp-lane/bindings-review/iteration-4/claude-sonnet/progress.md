# S8 CPP bindings 리뷰 iteration-4 — R2 (Claude Sonnet) progress

리뷰어 전용. build/실행/파일 수정 없음(정적 소스 대조 + 로컬 grep/read만 사용). 다른 리뷰어(R1)·coordinator
해석 미참조.

## 0. Scope 확인
- 대상 commit: `50faf28fd`. 현재 워킹트리 HEAD(`e86213d3a`)와 scope 4경로(`bindings/cpp/include`,
  `bindings/cpp/src`, `bindings/cpp/samples`, `bindings/cpp/CMakeLists.txt`) 사이 `git diff` 무출력
  → 워킹트리가 대상 commit과 byte-identical.
- 파일 수: `git ls-files ... | grep -v native/` = **120** (기대치 일치, iter-3의 121에서
  `native_send_result.hpp` 삭제로 1 감소 — 기대와 일치).
- Aggregate SHA-256 재현: `git ls-files bindings/cpp/include bindings/cpp/src bindings/cpp/samples
  bindings/cpp/CMakeLists.txt | grep -v native/ | LC_ALL=C sort | xargs sha256sum | sha256sum`
  → `e2190823b037b438a53de285a16be3ce40e92499a7ea06dcc6a73b99291b3bdb` — **기대치와 일치**.
- 종료 시점 재계산도 동일(파일 미수정, 리뷰 전 과정 read-only).

## 1. iter-3 finding 해소 검증 (C3-1 / C3-2 / C3-3)

### C3-1 router_spot 사멸 경로 제거 — RESOLVED
- `received.hpp`(`Contracts/Messaging/received.hpp:63-67`)의 `send_context_kind_t`가
  `{none, socket_rid}` 2값만 보유. `router_spot`, `spot_spot` 둘 다 없음.
- `received_access.hpp`의 `set_router_spot_send_context` 함수 자체가 삭제됨(scope 전체
  0 hit) — `set_socket_rid_send_context`만 잔존.
- `submit_direct_send`/`submit_direct_reply`(`received_access.hpp:50-112`) switch가
  `socket_rid` case만 처리. `has_send_context`/`has_reply_context`가 이미 `kind != none`을
  전제조건으로 걸러내므로, switch 도달 시점엔 `_send_context_kind`가 `socket_rid`
  외의 값을 가질 수 없음(enum에 다른 값이 없음) → **default 분기가 이제 진짜로 도달
  불가능**(이전엔 `router_spot`이 default로 새서 항상 `true` 반환하는 구조적 버그였음).
  즉 iter-3의 I1 finding(단일-파트 send/reply가 router_spot 컨텍스트에서 항상
  `invalid_argument`로 실패) 자체가 근본 해소됨.
- `router.cpp:29-42`의 `recv()`에서 `out_.spot_rid ()`/`set_router_spot_send_context` 분기
  완전 제거, `git diff f9b6ba50c 50faf28fd -- bindings/cpp/src/Runtime/Sockets/router.cpp`로
  diff 직접 확인: 조건 없이 `set_socket_rid_send_context`만 호출.
- `spot_rid` 전체 scope grep(38 hit)을 전수 확인 — 전부 `spot_t::spot_rid()`(spot 자신의
  routing id 접근자, `Contracts/Service/spot.hpp:55`) 또는 `dispatch.hpp`/`actor.hpp`/
  `actor_models.hpp`의 무관한 dispatch/actor 모델 필드(`target_spot_rid`,
  `previous_spot_rid`, `source_spot_rid` 등). `received_t`에 spot_rid 관련 접근자·필드·
  파라미터는 0건 — **router recv가 spot_rid를 surface하지 않는다는 판정 확인**.

### C3-2 spot_spot enum orphan — RESOLVED
- `received.hpp:63-67`에서 `spot_spot` enumerator 자체가 삭제됨(C3-1과 동일 diff).

### C3-3 orphan 연쇄(전이적 제거) — RESOLVED, 그리고 fixpoint가 실제로 한 단계 더 나갔음을 확인
- `resolve_timeout` — 0 hit (정의 자체 삭제).
- `native_send.hpp` dead trio(`send_single_no_wait_result`/`submit_single_message_part_restore`/
  `submit_single_message_part_no_wait_result`) + `to_send_result` — 0 hit.
- `native_send_result.hpp` — 파일 자체 삭제 확인(`find` 무결과), `#include` 참조도 0건.
- `get_string_option`(native_options.hpp) — 0 hit. `detail.hpp`의 dead `using` 재-export도
  0 hit(같이 제거됨).
- `submit_message_array`(native_message_parts.hpp) — 0 hit. 마찬가지로 dead `using`
  재-export 0 hit.
- **추가 확인**: `git diff f9b6ba50c 50faf28fd -- .../native_message_parts.hpp`를 보면 원 finding
  ledger가 명시하지 않은 `submit_message_parts_no_wait`도 함께 삭제됨 — `send_parts_no_wait_result`
  (native_send.hpp, 이것도 원 trio엔 없었음)의 유일 호출자였기 때문. Ledger의 "fixpoint까지 반복"
  지시가 실제로 한 단계 더 전이되어 적용됐음을 diff로 확인 — 새 orphan 없이 깨끗하게 마감.

## 2. 전체 scope 재검토 (I1/I2/I3, fresh)

### 방법
- I3 dead-code: 정규식 기반 후보 함수/템플릿 정의 추출(스크립트, `Runtime/**/*.hpp` 113개 +
  `Runtime/**/*.cpp` 213개 후보) → 후보별 `git grep -w -c`로 scope 전체 occurrence 집계,
  결과 ≤1(정의만 있고 참조 0)인 것만 수작업 재확인.
- I1: 핵심 계약 지점(ABI 상수, vendored C 헤더, `received_access.hpp`의 switch 완전성)
  직접 diff/read. 라이브러리 헤더가 core/include를 우선(-I) 하므로 시그니처 drift는
  구조적으로 컴파일 실패로 드러남(coordinator가 이미 green 확인, 여기선 재실행 안 함).
- I2: 파일 크기 분포(god-file 징후) + iter-2/iter-3에서 이미 확립된 RAII/책임 분리
  논거가 이번 diff(순수 dead-code 제거)로 훼손됐는지 확인.

### I1 계약 일치
- `received_access.hpp` switch 완전성: 위 C3-1 분석대로 이제 구조적으로 건전(도달 불가능한
  default가 실제로 도달 불가능해짐).
- vendored C 헤더 3종(`service/{mesh_node,dispatch,stream_session}.h`)이 `core/include/zlink/service/`
  대응 파일과 `diff -q` 무출력 — byte-identical 유지.
- `ABI_VERSION` 17곳, `struct_size` 25곳 — iter-1/2/3 대비 카운트 급변 없음(대량 계약 변경 없음).
- CMakeLists `project(zlink_cpp VERSION 10.0.0 ...)` 유지.
- **Finding 없음. I1 = CLEAN.**

### I2 POSD/DDD
- 최대 파일 `mesh_node.cpp` 750줄 — god-file 임계 아님. 이번 커밋은 순수 삭제(dead trio/orphan
  helper 제거)뿐이라 책임 분리·응집도에 부정적 영향 없음. 오히려 `native_send.hpp`/
  `native_message_parts.hpp`에서 미사용 오버로드 제거로 파일당 책임이 더 명확해짐.
- **Finding 없음. I2 = CLEAN.**

### I3 정리 완결성
- iter-3 지정 5개 orphan(C3-1~C3-3) 전량 확인 해소(위 §1).
- 독립 fresh dead-code 스윕(정규식 후보 320여 개, `.hpp` 113 + `.cpp` 213)에서 occurrence≤1인
  것은 5개만 발견, 그중 4개는 **public Contracts 헤더의 공개 API 표면**이라 오탐 처리:
  - `poll_item_t::from_fd`/`from_socket`(`Contracts/Eventing/poller.hpp:75,83`) — 공개 정적
    팩토리, 리포 내부에서 호출 안 해도 라이브러리 소비자 API로서 정상(zmq류 팩토리 패턴).
  - `routing_id_t::from_hex`(`Contracts/Core/routing_id.hpp:298`) — 공개 유틸리티, 동일 근거.
  - `submit_result_from_errno`(`Contracts/Errors/errors.hpp:262`) — 공개 inline 유틸리티.
    iter-3 R1이 이미 "관찰(비확정)"으로 짚고 finding 미승격했던 항목과 동일 — 이번에도
    공개 계약 표면이라는 판단 유지, 재개 안 함.
  - 위 4개는 공개 API이므로 "미사용 = dead code"가 아니라 "리포 내부에서 exercise 안 됨"일
    뿐 — 별도 finding 미등록.
  - **`reset_handle`(`Runtime/Native/socket_handle.hpp:77`)** — `protected` 멤버 함수. `socket_handle_t`를
    상속하는 클래스가 scope 전체에 0개(`socket_handle_t`는 오직
    `Contracts/Sockets/socket_contracts.hpp:147`의 `std::unique_ptr<detail::socket_handle_t> _socket`
    합성(composition) 멤버로만 쓰임, 상속 0건 확인). `protected`라 외부 호출도 불가 → 호출
    경로가 구조적으로 전무한 고아 메서드. `git log -S"reset_handle"`로 확인 시 이번
    iteration 커밋(`f9b6ba50c`→`50faf28fd`)과 무관한 **기존(pre-existing) dead code**(수년 전
    커밋부터 존재, `0ed863342` 이전). 이번 전이적 제거 사슬이 만든 신규 orphan은 아니지만,
    prompt가 요구하는 "전체 scope 재검토"(diff 한정이 아님) 범위에 해당해 fresh finding으로
    기록. 기능 영향 없음(빌드/링크/동작 무관, 클래스 크기에 영향 없는 순수 코드 라인).
    → **I3-1 [low]**.
- no-hit 게이트(SpotNode/spot_node/route_bridge/subjects/internal_sockets/
  set_pub_routing_id/set_sub_routing_id/dispatch_workers/recv_actor_part/msg_gets) 10개 전량
  재확인 0 hit.
- `TODO|FIXME|XXX|HACK|legacy|deprecated` 대소문자 무시 검색 scope 전체 0 hit.
- `git status --porcelain`으로 scope 4경로 무수정 확인(리뷰 시작·종료 시점 둘 다).
- **I3 = CLEAN**(blocker/high/medium 0). low finding 1건(I3-1) 별도 기록, CLEAN 저해 안 함
  (iteration-4 규칙).

## 3. 15-sample scope 확인
- `bindings/cpp/samples`에서 `.cpp` 15개 확인(기대치 일치). `router_socket_t` 사용 샘플은
  `dealer_router_recv_sample.cpp` 1개뿐 — spot_rid 없는 순수 dealer/router 시나리오로,
  이번 C3-1 수정과 충돌 없음(오히려 수정으로 구조가 단순해져 회귀 위험 감소).

## 4. 실행 증거
- 빌드/실행 직접 수행 안 함. Coordinator manifest(iteration-4/manifest.ko.md §2) 명시:
  라이브러리+15 samples `cmake --build` rc=0 green, no-hit ZERO — 재실행 없이 그대로 인용.

## 5. 최종 판정
- I1 CLEAN(0 finding). I2 CLEAN(0 finding). I3 CLEAN(blocker/high/medium 0, low 1건: I3-1
  `reset_handle` 고아 protected 메서드, pre-existing, 기능 무영향).
- iter-3 C3-1/C3-2/C3-3 전량 소스 대조로 해소 재확인, 신규 반례 없음.

BINDINGS REVIEW CLEAN
