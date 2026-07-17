파일 쓰기가 차단되었다 — 리뷰 지시가 "어떤 파일도 수정하지 마라"는 read-only 리뷰이므로 결과를 응답으로 직접 제시한다.

# S5 Core 구현 리뷰 — iteration 9 (Claude Sonnet, R2-CODE)

## 0. Snapshot 검증

| 항목 | manifest 값 | 실측 값 | 일치 |
|---|---|---|---|
| HEAD | `f5000d2fe7d2f9f7bc50eaa9ae23c978d3b54e85` | `f5000d2fe7d2f9f7bc50eaa9ae23c978d3b54e85` | 일치 |
| Scope 파일 수 | 631 | 631 | 일치 |
| Scope aggregate SHA-256 | `5eeb7c9010200c38c933d86ad9fa7a8d99d80e16495a0f3015cc74dcbf516255` | 동일 | 일치 |
| 종료 시점 재확인 | - | 동일 3값 재계산, drift 없음 | 일치 |

작업 공간은 리뷰 시작부터 종료까지 clean(`git status --short` 무출력)했다. 파일 수정 없이 read-only로 진행했다. AGENTS.md 전문, execution ledger §0~§3, 고정 manifest, iteration 8 finding ledger를 모두 읽었다. 이전 리뷰(iteration 1~8)의 clean 판정은 근거로 재사용하지 않고, iteration 8 finding 해소 여부와 최신 Core 전체 scope를 처음부터 다시 검토했다.

## 1. iteration 8 finding 해소 여부

### 1.1 CS8-I1-01 (high) — registry pin 일반화

**해소 확인.** `pin_node_data_path`(`core/src/runtime/services/mesh/mesh_runtime.cpp:451-463`)는 `registry().mutex` 아래 membership 확인(`live_nodes.count`)·tag 검증(`check_tag()`)·`lifecycle_pins` 증가를 단일 lock scope 안에서 원자적으로 수행한다. RAII 래퍼 `mesh_node_pin_t`(`mesh_runtime.hpp:616-631`)는 소멸 시 `unpin_node_lifecycle`을 호출해 같은 mutex 아래 감소시키고 `lifecycle_cv.notify_all()`로 대기자를 깨운다.

`core/src/api/mesh/*.cpp` 8개 파일 전역에서 `mesh_node_pin_t` 사용 57곳을 확인했다(공개 진입점 전수, timer 콜백 `on_operation_timeout`(`mesh_messaging_api.cpp:27-48`, `node_pin`이 33행에서 확보), claim 관련 `mesh_dispatch_api.cpp` 다수 지점 포함). `mesh_api.cpp:21`의 `classify_handle`만 `as_mesh_node`를 직접 호출하지만 membership probe만 하고 node 상태를 역참조하지 않아 안전하다.

destroy 경로(`unregister_node_and_wait_lifecycle_quiesced`, `mesh_runtime.cpp:503-515`)는 `live_nodes`/`nodes_by_name`에서 먼저 제거한 뒤 `while (node_->lifecycle_pins != 0) registry().lifecycle_cv.wait(lock);`로 실제 대기한다. `test_destroy_waits_for_concurrent_submits`(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:609-651`)가 4-thread submit hammer 중 destroy를 수행하고 post-destroy 호출이 `EFAULT`(`ZLINK_SUBMIT_INVALID_HANDLE`)로 실패함을 확인한다.

### 1.2 CS8-I1-02 = N8-I1-01 (medium) — 외곽 장벽 2개 누락

**해소 확인.** `zlink_mesh_node_request_to_channel`(`mesh_messaging_api.cpp:502-526`)과 `zlink_stream_session_request_to_actor`(`mesh_stream_session_api.cpp:1043-1067`) 모두 `try`/`catch (const std::bad_alloc&)` → `ENOMEM`/`ZLINK_SUBMIT_OUT_OF_MEMORY` 패턴을 갖췄다. `core/src/api/mesh/*.cpp` 전역에서 `zlink_submit_result_t`를 반환하는 공개 진입점을 전수 조사해 정확히 27개를 확인했고, 27개 전부가 동일한 outer catch를 갖는다(누락 0).

### 1.3 CS8-I1-03 (medium) — handle_reply의 bookkeeping 선소거

**부분 해소 — 잔여 결함 발견 (아래 §2 Finding 1).** wire-ingress 경로(`mesh_wire_ingress.cpp:562-599`, `handle_reply`)는 correlation 항목을 지운(584행) 뒤 `handle_reply_tail`을 `try`로 감싸고 `catch (bad_alloc)`에서 `complete_operation(..., ZLINK_REQUEST_INTERNAL_ERROR, ENOMEM, ...)`으로 강등해 completion 유실을 막는다 — 이 경로 자체는 의도대로 고쳐졌다.

그러나 같은 "bookkeeping 선소거 후 construction에서 throw 가능" 패턴이 로컬 reply 제출 경로인 `zlink_mesh_reply`와 `zlink_actor_join_reply`에도 존재하며, 이 두 곳은 고쳐지지 않았다. 원래 요청자의 `pending_operation_t`가 이미 지워진 뒤 payload 구성이 실패하면, catch가 **reply를 보내는 쪽 호출자**에게만 OOM을 반환하고 원래 요청자에게는 어떤 completion도 전달되지 않는다. 근거는 아래 Finding 1.

## 2. 신규/잔여 finding

### Finding 1 (high) — `zlink_mesh_reply`/`zlink_actor_join_reply`가 소거된 operation의 completion을 유실시켜 요청자가 영구 대기한다

**이슈.** `zlink_mesh_reply`(`core/src/api/mesh/mesh_dispatch_api.cpp:795-899`)는 요청자의 `pending_operation_t`를 찾아 867행에서 `node->operations.erase(op_it)`로 소거하고 `route.consumed = true`로 표시한 뒤 lock을 벗어난다. 이후 881행 `std::vector<zlink_msg_t> reply_parts (part_count_);`는 힙 할당이 필요해 `std::bad_alloc`을 던질 수 있다. 이 예외가 발생하면 894-899행 outer catch로 바로 뛰어 `errno = ENOMEM; return ZLINK_SUBMIT_OUT_OF_MEMORY;`만 수행하고, 891행의 `complete_operation(node, op, ZLINK_REQUEST_OK, 0, NULL, &reply_parts)`는 결코 실행되지 않는다.

`zlink_actor_join_reply`(`mesh_actor_api.cpp:1115-1269`)도 동일한 구조다. 1234행 `node->operations.erase(op_it)`로 요청자 operation을 소거한 뒤 1243-1248행에서 `kind_data`/`reply_parts` 벡터를 구성하며, 이 구성이 실패하면 1263-1269행 outer catch가 reply 제출자에게만 OOM을 돌려주고 1258/1260행의 `complete_operation` 호출에는 도달하지 않는다.

두 함수 모두 요청자 쪽 timeout 백업도 없다: `register_operation`이 성공한 요청마다 별도로 등록한 `on_operation_timeout`(`mesh_messaging_api.cpp:27-48`)은 `node->operations.find(ctx->operation_low)`가 실패하면(이미 위에서 erase됐으므로 실패) 아무 것도 하지 않고 반환한다(43행). 즉 completion도, timeout도 전달되지 않는다.

**근거(file:line).**
- `core/src/api/mesh/mesh_dispatch_api.cpp:857-868` — reply_routes 조회 후 `node->operations.erase(op_it)` (867), `route.consumed = true` (868)
- `core/src/api/mesh/mesh_dispatch_api.cpp:880-892` — erase 이후 `std::vector<zlink_msg_t> reply_parts (part_count_)` 구성(881), 성공 시에만 891행 `complete_operation` 도달
- `core/src/api/mesh/mesh_dispatch_api.cpp:894-899` — outer catch가 `complete_operation`을 호출하지 않고 리턴만
- `core/src/api/mesh/mesh_actor_api.cpp:1229-1235` — `node->operations.erase(op_it)` (1234)
- `core/src/api/mesh/mesh_actor_api.cpp:1243-1256` — erase 이후 payload 구성, 성공 시에만 1258/1260행 `complete_operation` 도달
- `core/src/api/mesh/mesh_actor_api.cpp:1263-1269` — outer catch가 completion을 대체하지 않음
- `core/src/api/mesh/mesh_messaging_api.cpp:27-48` — timeout 콜백이 이미 소거된 operation을 발견하면 43행에서 그냥 반환
- 대조군(안전): `deliver_reply_via_route`(`mesh_dispatch_api.cpp:742-791`)는 `parts_`를 호출 전에 이미 구성된 상태로 받으므로 erase(777)와 `complete_operation`(788) 사이에 할당 가능 코드가 없다
- `core/doc/spec/core/service/02-dispatch.ko.md:283` — "claim은 operation마다 terminal completion을 정확히 한 번 반환한다" 계약 위반
- 테스트 공백: `test_submit_alloc_failure_maps_to_out_of_memory`(`test_mesh_lifecycle_contracts.cpp:656-703`)는 submit-측 OOM만 검증한다. `test_maybe_throw_alloc()` 계측도 `mesh_node_api.cpp:76`, `mesh_messaging_api.cpp:196,940`에만 있고 두 reply 함수에는 없다 — 이 결함은 기존 OOM fault-injection 테스트로 도달 불가능했다.

**영향.** 메모리 압박으로 이 벡터 할당이 실패하면(드물지만 배제 불가), 요청 스레드는 이미 지워진 operation의 completion을 영원히 받지 못하고 무기한 대기한다. review manifest가 명시적으로 지목한 "reply token·operation exactly-once"·"OOM과 실패 원자성" 영역의 핵심 위반이며, CS8-I1-03가 고쳤다고 주장한 결함 클래스가 두 개의 공개 API(`zlink_mesh_reply`, `zlink_actor_join_reply`)에서 재현된다.

**수정 범위(제안, 구현 안 함).** `handle_reply`와 같은 패턴 — payload 구성을 op 소거 이전으로 옮기거나, 실패 시 `complete_operation(node, op, ZLINK_REQUEST_INTERNAL_ERROR, ENOMEM, NULL, NULL)`로 강등 — 을 적용. `zlink_actor_join_reply`는 membership commit(1200-1215행) 이후 completion 구성 실패 시 상태 일관성도 함께 검토 필요.

**검증 방향.** 두 함수의 payload 구성 지점에 `test_maybe_throw_alloc()` 계측을 추가하고, 유도된 실패에서 (a) 요청자가 유한 시간 안에 completion을 받는지 (b) 이중 completion이 없는지(exactly-once 유지)를 확인하는 회귀 테스트.

---

### Finding 2 (low) — `pending_operation_t::deadline_ms`가 기록만 되고 전혀 읽히지 않는 죽은 필드

`mesh_runtime.hpp:185`의 필드가 `mesh_node_api.cpp:84`에서 한 번 대입되지만 저장소 전체에서 읽는 코드가 없다. 실제 timeout은 별도 타이머(`schedule_operation_timeout`/`on_operation_timeout`)가 전담한다. 기능 결함은 아니나 두 메커니즘이 공존하는 것처럼 오독될 수 있는 vestigial state.

### Finding 3 (low) — `zlink_actor_join_result_t` 이름 재사용이 제거-식별자 게이트의 맹점을 만든다

`removed-identifiers-10.0.0.json`은 옛 struct `zlink_actor_join_result_t`(TYPE 버킷)를 제거 대상으로 기록하지만, 현재 헤더(`actor.h:29-32`)는 같은 이름을 다른 모양(enum)으로 재사용한다. `check_public_surface.py`는 선언 형태로 버킷을 나눠(`ENUM_TYPE` vs `TYPE`) 이 재사용을 감지하지 못한다. 현재는 spec 승인된 의도된 설계이나, 향후 실수로 옛 struct 모양이 같은 이름으로 되살아나도 게이트가 못 잡는 구조적 맹점.

### Finding 4 (low) — OOM outer barrier 27곳이 동일 주석·동일 로직으로 손코딩 중복

`core/src/api/mesh/{mesh_actor_api,mesh_dispatch_api,mesh_messaging_api,mesh_stream_session_api}.cpp`의 27개 진입점에 동일한 catch 블록(주석 포함)이 반복되고 공유 헬퍼가 없다. 기능 결함은 아니지만 "bad_alloc → ENOMEM+OUT_OF_MEMORY" 정책이 27곳에 반영되어 정보 은닉 원칙 위반.

### Finding 5 (low, informational) — `core/packaging`의 redhat/debian/nuget 매니페스트 버전이 10.0.0으로 갱신되지 않음

`zlink.spec:13`(6.0.3), `debian/changelog:1`(6.0.3-0.1), `nuget/package.config:4`(4.2.3.0)가 CMakeLists.txt/zlink.h/CHANGELOG.md/conandata.yml의 10.0.0과 어긋난다. `git log` 확인 결과 RouteMesh 10.0.0 S4/S5 작업 이전부터 정체된 값으로, 이번 iteration의 신규 회귀는 아니다. 활성 배포 채널(Conan)은 정확하다.

## 3. Known risk 4건 판정

| # | 항목 | 판정 |
|---|---|---|
| 1 | TSAN auto-HWM lock-order | 기존 계열 확인. mesh 통합 이전 커밋만 존재, mesh는 표준 소켓 경로로만 간접 도달. 신규 위험 없음 |
| 2 | TSAN raw command mailbox ypipe | 기존 계열 확인. mesh 자체 `mailbox_t`(이름만 동일)는 무관한 별개 구조체. 신규 위험 없음 |
| 3 | raw socket teardown(`pipe_t::detach_peer_backref`, Asio `blob_t`) | 기존 계열 확인. mesh 이전 소켓/엔진 계층, mesh가 직접 수정 안 함. 신규 위험 없음 |
| 4 | `ctx_term` linger | 기존 계열 확인. mesh 소스에서 호출되지 않음. 완화책(2-process 종료 순서)이 정식 문서 밖에 있다는 점은 후속 문서화 과제 |

4건 모두 mesh 통합 이전부터 존재하는 legacy 코드이며 신규 finding으로 승격하지 않는다.

## 4. I1·I2·I3 판정

**I1 (계약-구현 일치) — NOT CLEAN.** Finding 1(high)이 dispatch spec §283의 exactly-once terminal completion 계약을 위반한다. Finding 5(low)는 패키지 메타데이터 버전 불일치. 그 외 표본 검사(공개 헤더-spec 8개 함수 시그니처, errno 매핑, `contract_public_surface` gate 동작)는 모두 일치.

**I2 (POSD·DDD) — NOT CLEAN.** Finding 4(low)의 27곳 손코딩 중복이 정보 은닉 위반. `pin_node_data_path`(깊은 모듈), destroy/pin-wait 경로(일원화된 처리), pass-through 부재는 양호. 최대 파일(`mesh_actor_api.cpp` 1544줄)도 엔티티별 1:1 구조로 god-file 징후 없음.

**I3 (정리 완결성) — NOT CLEAN.** Finding 2(low, 죽은 필드)와 Finding 3(low, 이름 재사용 게이트 맹점). governance 문서가 지목한 ~50개 제거 식별자·enum·타입 전수 grep은 이 두 건 제외 전부 0-hit. dead-code 스멜, orphan 테스트, dual-mode 스캐폴딩은 발견되지 않음.

## 5. 결론

Finding 1은 high severity로 blocker·high·medium finding 0건 조건을 충족하지 못한다. I1·I2·I3 모두 완전한 CLEAN이 아니다.

CORE REVIEW NOT CLEAN
