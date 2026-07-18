# S8 CPP bindings 전환 리뷰 — iteration 4 — R2 (Claude Sonnet)

리뷰어 전용(정적 소스 대조). build/실행/파일 수정 없음. 다른 리뷰어 결과·coordinator 해석 미참조.

## 1. Scope 확인
- 대상 commit: `50faf28fd`. 워킹트리 HEAD(`e86213d3a`)와 scope 4경로 `git diff` 무출력 →
  byte-identical 스냅샷으로 취급.
- 파일 수: **120**(기대치 일치, iter-3의 121에서 `native_send_result.hpp` 삭제로 -1, 기대와 부합).
- Aggregate SHA-256 재현 커맨드: `git ls-files bindings/cpp/include bindings/cpp/src
  bindings/cpp/samples bindings/cpp/CMakeLists.txt | grep -v native/ | LC_ALL=C sort | xargs
  sha256sum | sha256sum`
  → `e2190823b037b438a53de285a16be3ce40e92499a7ea06dcc6a73b99291b3bdb` — **기대치와 일치**.
  종료 시점 재계산도 동일(리뷰 전체가 read-only).

## 2. iter-3 finding 해소 판정

### C3-1. router_spot 사멸 경로 제거 — **RESOLVED**
- `Contracts/Messaging/received.hpp:63-67`의 `send_context_kind_t`가 `{none, socket_rid}` 2값만
  보유. `router_spot`은 물론 setter `set_router_spot_send_context`도 scope 전체 0 hit(정의째
  삭제).
- `Runtime/Sockets/router.cpp:29-42`: `git diff f9b6ba50c 50faf28fd`로 직접 확인 —
  `out_.spot_rid ()` 분기·`set_router_spot_send_context` 호출이 사라지고 조건 없이
  `set_socket_rid_send_context`만 호출. `received_t`에 spot_rid 접근자·필드·파라미터가 남아
  있는지 scope 전체(`spot_rid` 38 hit)를 전수 확인 — 전부 `spot_t::spot_rid()`(spot 자신의
  routing id) 또는 dispatch/actor 모델의 무관 필드(`target_spot_rid` 등)이고,
  `received_t` 관련 흔적은 0. **router recv가 spot_rid를 더 이상 surface하지 않는다는 iter-3
  전제가 정확히 반영됨.**
- 부수 확인: `submit_direct_send`/`submit_direct_reply`(`received_access.hpp:50-112`)의 switch가
  이제 `socket_rid` 단일 case만 다루고, `has_send_context`/`has_reply_context`가 `kind != none`을
  이미 걸러내므로 default 분기는 **구조적으로 도달 불가능**해짐 — iter-3에서 R2가 발견했던
  "router_spot이 default로 새서 항상 true 반환"하는 I1 버그가 원인 소거로 근본 해소.

### C3-2. spot_spot enum orphan — **RESOLVED**
- `send_context_kind_t`에 `spot_spot` enumerator 자체가 없음(C3-1과 동일 diff에서 함께 제거).

### C3-3. 전이적(transitive) orphan 연쇄 — **RESOLVED**
- `resolve_timeout` — 0 hit.
- `native_send.hpp` dead trio(`send_single_no_wait_result`/`submit_single_message_part_restore`/
  `submit_single_message_part_no_wait_result`) + `to_send_result` — 0 hit.
- `native_send_result.hpp` — 파일 삭제 확인(find 무결과), `#include` 잔존 참조 0.
- `get_string_option`(+`detail.hpp` dead `using`) — 0 hit.
- `submit_message_array`(+`detail.hpp` dead `using`) — 0 hit.
- **fixpoint 검증**: `git diff f9b6ba50c 50faf28fd -- native_message_parts.hpp`를 보면 원 ledger가
  명시하지 않았던 `submit_message_parts_no_wait`도 함께 제거됨 — `native_send.hpp`의
  `send_parts_no_wait_result`(이것도 원 trio에 없었음)의 유일 호출자였기 때문. "제거 후
  whole-scope grep 재탐색"이 실제로 한 단계 더 전이되어 적용된 흔적이며, 이로 인한 새 orphan은
  발견되지 않음.

**iter-3 finding 전량 소스 대조로 RESOLVED 재확인. 새 반례 없음(재개 없음).**

## 3. 전체 scope 3축 재검토 (fresh)

### I1 계약 일치 — **CLEAN**
- vendored C 헤더 3종(`service/{mesh_node,dispatch,stream_session}.h`)이 `core/include/zlink/service/`
  대응 파일과 `diff -q` 무출력, byte-identical.
- `ABI_VERSION` 17 occurrence, `struct_size` 25 occurrence — 이전 iteration 대비 급변 없음(대량
  계약 변경 없음, 순수 dead-code 삭제만 있었던 diff와 부합).
- `received_access.hpp` switch 완전성이 C3-1로 구조적으로 건전해짐(§2 참조).
- CMakeLists `project(zlink_cpp VERSION 10.0.0 ...)` 유지.
- Finding 없음.

### I2 POSD/DDD — **CLEAN**
- 파일 크기 분포 재확인: 최대 `mesh_node.cpp` 750줄 — god-file 징후 없음(scope 전체 15,927줄,
  15개 파일이 300줄 이상, 상위권도 완만).
- 이번 대상 diff는 미사용 오버로드/헬퍼 삭제뿐이라 책임 분리·응집도에 부정적 영향 없음. 오히려
  `native_send.hpp`/`native_message_parts.hpp`가 미사용 다중 오버로드 제거로 책임이 더 명확해짐.
- Finding 없음.

### I3 정리(폐기 no-hit + dead code + 전이적 신규 orphan) — **CLEAN**(low 1건 별도)
- 독립 dead-code 스윕: `Runtime/**/*.hpp`(113개 정의 후보) + `Runtime/**/*.cpp`(213개 정의 후보)를
  정규식으로 추출, 각 심볼 scope 전체 occurrence를 `git grep -w -c`로 집계 → occurrence ≤1(정의만,
  참조 0)인 것 5개 발견.
  - 4개는 **public Contracts 헤더의 공개 API**로 오탐 처리: `poll_item_t::from_fd`/`from_socket`
    (`Contracts/Eventing/poller.hpp`), `routing_id_t::from_hex`(`Contracts/Core/routing_id.hpp`),
    `submit_result_from_errno`(`Contracts/Errors/errors.hpp`) — 리포 내부 미호출이지만 라이브러리
    소비자용 공개 팩토리/유틸리티이므로 dead code 판정 대상 아님(iter-3 R1의 동일 항목
    "관찰·비확정" 판단과 동일 결론 유지).
  - **[I3-1, low]** `reset_handle`(`Runtime/Native/socket_handle.hpp:77`, `socket_handle_t`의
    `protected` 멤버) — `socket_handle_t`를 상속하는 클래스가 scope 전체에 0개(합성 멤버로만
    쓰임, `Contracts/Sockets/socket_contracts.hpp:147`), `protected`라 외부 호출 경로도 없음 →
    구조적으로 도달 불가능한 고아 메서드. `git log -S"reset_handle"` 추적 결과 이번 iteration
    커밋(`f9b6ba50c`→`50faf28fd`)과 무관한 **pre-existing** dead code(수년 전부터 존재,
    C3 전이적 제거 사슬이 만든 것 아님). 기능/빌드/링크 영향 없음.
- no-hit 게이트 10개(SpotNode/spot_node/route_bridge/subjects/internal_sockets/
  set_pub_routing_id/set_sub_routing_id/dispatch_workers/recv_actor_part/msg_gets) 전량 0 hit.
- `TODO|FIXME|XXX|HACK|legacy|deprecated`(대소문자 무시) scope 전체 0 hit.
- `git status --porcelain` scope 무수정(시작·종료 동일).
- **blocker/high/medium 0 → I3 CLEAN**(iteration-4 규칙). low 1건(I3-1)은 아래 §4에 별도 기록,
  CLEAN 판정 저해 안 함.

## 4. Low finding 목록
- **I3-1 [low]**: `bindings/cpp/src/Runtime/Native/socket_handle.hpp:77` `socket_handle_t::reset_handle`
  — protected 멤버지만 상속 클래스 0개로 호출 경로 없음. pre-existing(이번 전이적 제거와 무관),
  기능 영향 없음. 후속 정리 후보로만 기록(즉시 조치 불필요).

## 5. 폐기 no-hit 판정
- 10-키워드 게이트 + TODO/FIXME/XXX/HACK/legacy/deprecated 전량 0 hit — **ZERO 확인**.

## 6. 15-sample 확인
- `bindings/cpp/samples` `.cpp` 15개(기대치 일치). `router_socket_t` 사용 샘플은
  `dealer_router_recv_sample.cpp` 1개뿐, spot_rid 미관여 순수 dealer/router 시나리오 — C3-1
  수정과 충돌 없음.

## 7. 실행 증거
- 빌드/실행 직접 수행하지 않음. Coordinator manifest(`iteration-4/manifest.ko.md` §2)의
  "라이브러리+15 samples green, no-hit ZERO" 증거를 재실행 없이 그대로 인용.

## 8. 최종 판정
- iter-3 C3-1/C3-2/C3-3 전량 소스 대조로 RESOLVED, 새 반례 없음.
- I1 CLEAN(0 finding). I2 CLEAN(0 finding). I3 CLEAN(blocker/high/medium 0, low 1건 I3-1 별도
  기록·CLEAN 비저해).

BINDINGS REVIEW CLEAN
