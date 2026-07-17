# S5 Core 구현 독립 리뷰 R2 (Claude Sonnet) — iteration 7 (연장 3회차, 전체 pass)

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 6 병합 finding 4건: **해소 3건, 부분 해소 1건**(N6-I1-02가
  명시적으로 겨냥한 4개 interleaving은 전부 막혔지만, 같은 결함 계열의
  다섯 번째 interleaving이 handle 검증 자체에 남아 있다 — §3)
- 신규 finding: **3건**(`high 2`, `low 1`)
- I1 계약 구현 일치: **NOT CLEAN**(high 2)
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **NOT CLEAN**(low 1)

전체 scope를 다시 훑는 중 두 갈래의 잔여 결함을 발견했다. 첫째,
`as_mesh_node()`의 handle 검증(registry membership 확인)과 shutdown이
`node->mutex`를 실제로 잡는 시점 사이에는 어떤 lock도 없는 창이 있고, 그
창 안에서 concurrent destroy가 node를 완전히 삭제할 수 있다 — 이는
N6-I1-02가 막은 4개 interleaving(모두 `shutdown_active`가 설정된
**이후**의 창)과 다른, 플래그 설정 **이전**의 다섯 번째 interleaving이라
그 fix의 보호 밖에 있다(§4.1 CS7-I1-02). 둘째, N6-I1-01+CS6-I1-01이 명시적
으로 열거한 submit(제출) 경로 7곳과 **같은 결함 계열**이 completion·reply
(완료·응답) 경로에 손대지 않은 채 남아 있다(§4.1 CS7-I1-01). 이 중 다수는
wire ingress·timer 스레드처럼 C++ 예외 경계가 전혀 없는 지점이어서, 이전에
medium으로 다뤄진 같은 계열보다 결과가 더 확정적이다(`std::thread` 진입
함수를 예외가 벗어나면 표준이 `std::terminate()`를 보장한다). 이 두 신규
finding 때문에 I1이 다시 NOT CLEAN이다. CHANGELOG 수치가 이번 delta로
갱신되지 않아 I3도 NOT CLEAN이다.

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Claude Sonnet |
| Candidate commit / HEAD | `f8c35e6fecbe0c8ec13a7cac0e7fffbd24218f0d` — 시작·종료 모두 동일 |
| Scope hash (시작) | `cdbc1b1053c4931d0610968d640c133b5ea9b07964a2a57cd6d379c6f2478af6` (631 files) — manifest §2와 일치 |
| Scope hash (종료) | 동일 값 — 일치, `git status core/` clean |
| iteration 6→7 delta | `b1e6c81fb..f8c35e6fe`, 11 files (+250/−98): `mesh_node_api.cpp`(shutdown/destroy), `mesh_messaging_api.cpp`(publish_common/submit_local_record), `mesh_actor_api.cpp`(actor send/request), `mesh_stream_session_api.cpp`(session submit), `mesh_runtime.cpp`(admit_record/emit_monitor_event), `mesh_monitor_api.cpp`(close EBUSY), errno-map ko/en, services-internals ko/en, 신규 test 1개. `CHANGELOG.md`는 delta에 포함되지 않음(§4.4) |
| `check_public_surface.py . core/build/lib/libzlink.so.10.0.0` | **PASS**, 196 exports 정확 일치, 제거 identifier 없음 |
| `unittest_public_contract_headers` | 1/1 PASS |
| `cmake --build core/build -j$(nproc)` | 이미 최신, 성공 |
| `ctest --test-dir core/build -j8` | **100% tests passed, 0 failed out of 85** |
| ASAN 5 mesh 바이너리(재빌드·재실행) | lifecycle 10·peer_admission 12·stress 3·monitor_matrix 6·node_basic 8 = 39 case, 전부 리포트 0 |
| TSAN lifecycle(`setarch $(uname -m) -R`) | 10/10 PASS, 경고 13건 전수 stack frame 분류: lock-order-inversion 11건 = `prepare_auto_hwm_socket_plan`/`refresh_auto_hwm_policy`/`ctx_t::create_socket` 계열(known risk #1), data race 1건 = `mailbox_t::recv`(`mailbox.cpp:66`, known risk #2), data race 1건 = `pipe_t::detach_peer_backref`(`pipe.cpp:202`, known risk #3). 신규 mesh 코드 경로 프레임 0 — 신규 `test_destroy_during_shutdown_wait_is_deadlock_error` 포함 전 케이스 PASS, 새 race 없음 |
| TSAN stress(3회 재실행) | 1회차 `test_ready_handler_churn_under_load` FAIL(`Expected 0 Was 304`, `test_mesh_stress.cpp:371`), lock-order-inversion 3건(전부 known risk #1); 2·3회차 3/3 PASS. §4.5에서 delta-무관 판정 |
| `git diff --check b1e6c81fb f8c35e6fe` | clean |
| 정적 hygiene | delta 11개 파일 전수 재확인: TODO/FIXME/디버그 출력 없음 |
| 0-byte·merge marker | scope 631개 파일 전수 재확인: 0건 |
| CMake·package | mesh wire 4 TU(`core/CMakeLists.txt:887-890`), mesh test 5개(`core/tests/CMakeLists.txt:93-97`) 등록 유지. `conandata.yml`은 이번 delta에 없음(전 iteration에 확인됨) |
| known risk 관련 raw 파일 diff | `socket_base.cpp`·`pipe.cpp`·`mailbox.cpp`·`ctx_auto_hwm_recalc.cpp`를 `b1e6c81fb..f8c35e6fe`로 비교 — diff 없음(known risk 4건 모두 이번 delta와 무관) |

TSAN 2-process admission 3건(round-robin·MIXED·reconnect)은 과제 지시와
iteration-5 manifest §4가 이미 baseline `472f66a32` 재현으로 delta-무관을
확정한 사항이며, 이번 delta도 그 판단을 뒤집을 코드 변경이 없어(§1 표의
11개 파일에 wire admission 로직 변경 없음) 재실행하지 않았다.

```bash
git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md \
  | xargs sha256sum | sha256sum
```

## 2. iteration 6 병합 finding 4건 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| N6-I1-02(high) — shutdown/destroy 수명 | **부분 해소** | §3에서 이 finding이 겨냥한 4개 interleaving(전부 `shutdown_active` 설정 이후의 창)은 코드 추적과 신규 test로 확인했다. 그러나 handle 검증~lock 획득 사이의 다섯 번째 interleaving이 별도로 남아 있어 CS7-I1-02로 계속한다(§4.1) |
| N6-I1-01+CS6-I1-01(medium) — submit-family OOM 매핑 | **해소**(열거된 7개 지점 한정) | `publish_common`(`mesh_messaging_api.cpp:877-933`, record 구축+슬롯 예약+remote leg 롤백 통합), `submit_local_record`(`:193-210`), `copy_borrowed_parts`(`:54-60`, ENOMEM 반환), `mesh_actor_api.cpp:1321-1351`(actor send/request), `mesh_stream_session_api.cpp:924-943`(session submit) + `copy_session_record_parts`(`:233-239`, ENOMEM 반환), `admit_record`(`mesh_runtime.cpp:669-681`, ready-index+deque를 counter 갱신보다 앞서 try/catch), `emit_monitor_event`(`:760-768`, overflow drop과 동일 행동) — 7곳 전부 코드로 직접 확인. 다만 같은 계열이 completion·reply 경로에 남아 있어 신규 finding CS7-I1-01로 계속한다(§4.1) |
| N6-I1-03(medium) — monitor close errno | **해소** | `mesh_monitor_api.cpp:152-157`이 active handler close를 `EBUSY`로 반환(주석: "Formal close mapping: an active callback is EBUSY (EDEADLK belongs to handler registration and node ...)"). errno map ko `04-errno-map.ko.md:98`·en `04-errno-map.md:93`이 `ZLINK_CLOSE_BUSY`에 `EBUSY`(active child/callback/API)와 `EDEADLK`(같은 handle lifecycle 재진입, §11 참조)를 모두 명시해 close 표와 코드가 일치 |
| N6-I3-01(low) — internals timer 경계 | **해소** | `services-internals.ko.md:25-33`·en `:26-33`이 계층 규칙의 예외로 `mesh_api.cpp` seam이 Spot timer registry(취소 포함)와 turn admission 상태(`timer_turn_active`·`timer_count`)를 직접 소유·변경한다고 명시. 실제 `mesh_api.cpp:268,322,372,416-417`이 이 두 필드를 직접 갱신해 서술과 일치 |

## 3. N6-I1-02 shutdown/destroy interleaving 검증(겨냥한 4종 + 잔여 1종)

`mesh_node_api.cpp:310-479`를 직접 추적했다. 아래 4개는 N6-I1-02가
명시적으로 겨냥한 interleaving이다.

1. **파킹된 wait 중 destroy**: shutdown이 `shutdown_active=true`(:328) 후
   `cv.wait_for`(:353)로 lock을 놓는다. destroy는 lock을 잡자마자
   `shutdown_active`를 검사해(:433) true면 node를 전혀 건드리지 않고
   `EDEADLK`+`ZLINK_CLOSE_BUSY`로 즉시 반환한다(:434-435) — node 생존,
   shutdown은 계속 대기.
2. **drained 꼬리 중 destroy**: drained 경로는 lock을 쥔 채로 나온 뒤
   `state=STOPPED`, `lock.unlock()`, `wire_stop`, `emit_state_changed`,
   `lock.lock()`, `shutdown_active=false`, `unlock`까지(:404-411) 전부
   `shutdown_active=true`를 유지한다. 이 unlock 구간에서 destroy가 lock을
   잡아도 `shutdown_active`가 여전히 true이므로 위 1번과 동일하게
   `EDEADLK`로 반환한다.
3. **!drained 꼬리 중 destroy**: revoke·detach 계산은 lock 보유 중
   끝내고(:356-386), `shutdown_active=true`를 유지한 채 unlock 후
   `emit_monitor_event`·`complete_operation` 루프를 돈다(:391-397). 이
   구간도 2번과 동일하게 destroy를 `EDEADLK`로 막는다.
4. **destroy 진행 중 shutdown 시작**: destroy는 `shutdown_active` 검사,
   child 검사, 강제 종료(state→STOPPED, operations clear)를 한
   `lock_guard` 보유 구간(:427-466)으로 병합했다 — 검사와 커밋 사이에
   틈이 없다. 이 블록이 끝난 뒤에만 `unregister_node`(:471)가 호출된다.
   블록 종료~`unregister_node` 사이의 극히 짧은 창에서 새 shutdown이
   `node->mutex`를 잡으면 `state==STOPPED`를 보고 즉시 `ZLINK_REQUEST_OK`로
   반환하며(:325-326) node를 더 이상 건드리지 않는다 — 안전하다.
   `unregister_node` 이후에는 `as_mesh_node`가 `live_nodes` registry에서
   해당 포인터를 찾지 못해(`mesh_runtime.cpp:395-404`) `EFAULT`로 실패한다
   — "공개 진입문을 먼저 닫는다"는 주석(:468-470)이 정확히 이 순서를
   보장한다. 두 경우 모두 UAF가 없다.

신규 `test_destroy_during_shutdown_wait_is_deadlock_error`
(`test_mesh_lifecycle_contracts.cpp:569-604`)는 claim을 보유시켜 shutdown을
drain wait에 실제로 파킹시킨 뒤(스레드 분리, 300ms 대기로 파킹 확인) 동시
destroy를 호출해 `ZLINK_CLOSE_BUSY`+`EDEADLK`+node 생존을 단정하고, claim
해제 후 shutdown이 `ZLINK_REQUEST_OK`로 끝나며 뒤이은 destroy가 성공함을
검증한다 — interleaving 1번을 실제 동시성으로, 나머지 3개는 코드 구조
논증으로 커버한다. TSAN lifecycle 10/10에서 이 test가 통과하고 신규 race가
없음을 §1에서 확인했다.

위 4개는 전부 shutdown이 `node->mutex`를 잡고 `shutdown_active=true`를
설정한 **이후**에 열리는 창이다. 그러나 `:312`의 `as_mesh_node
(mesh_node_)` 호출과 `:318`의 `std::unique_lock<std::mutex> lock
(node->mutex)` 사이에는 **아직 어떤 lock도 없는 다섯 번째 창**이 있다 —
`shutdown_active`는 이 시점에 아직 false이므로 destroy의 admission 검사가
차단하지 않는다. 이 창에서 concurrent destroy가 완주하면(admission
통과·teardown 커밋·`unregister_node`·`delete node`) shutdown이 재개될 때
`:318`이 이미 해제된 메모리 위의 mutex를 잠그려 시도한다. 상세는
§4.1 CS7-I1-02.

## 4. 축별 finding

### I1 계약 구현 일치 — NOT CLEAN (high 2)

#### CS7-I1-02 (high) — shutdown의 handle 검증과 mutex 획득 사이의 창에서 concurrent destroy가 node를 삭제할 수 있음

- 이슈·근거: `zlink_mesh_node_shutdown`(`mesh_node_api.cpp:310-328`)은
  `as_mesh_node (mesh_node_)`(:312)로 handle을 검증한 뒤, **아직 어떤
  lock도 잡지 않은 채** null 검사를 거쳐 `:318`에서야
  `std::unique_lock<std::mutex> lock (node->mutex)`를 실행한다.
  `as_mesh_node`(`mesh_runtime.cpp:395-406`) 자체도 이 창을 만든다: registry
  mutex는 `live_nodes.count (handle_)` 검사(:401) 동안만 잡히고, 그 블록이
  끝난 뒤(:403) `node->check_tag ()`(:405)가 registry lock 밖에서
  node 메모리를 직접 역참조한다. `zlink_mesh_node_destroy`
  (`mesh_node_api.cpp:415-479`)도 동일하게 `as_mesh_node`로 handle을 얻은
  뒤(:421) 별도로 `node->mutex`를 잡는다(:428). 두 함수 모두 이 창 안에서는
  `shutdown_active`가 아직 `false`이므로(shutdown이 `:328`에서야 그 값을
  설정), destroy의 admission 검사(:433)가 이를 막지 못한다.
  interleaving: T1(shutdown)이 `:312`를 통과한 직후, `:318`을 실행하기
  전에 선점된다. T2(destroy)가 같은 handle로 `as_mesh_node`를 통과하고
  (registry에 아직 등록돼 있으므로 성공), `:428`의 lock을 잡아 admission
  검사를 통과하고(자식·timer 0, `shutdown_active` false), 강제 종료를
  커밋하고 lock을 놓은 뒤 `unregister_node`(:471)·`wire_stop`(:472)·
  `delete node`(:476)까지 완주한다. T1이 재개되어 `:318`을 실행하면 이미
  해제된 메모리 위의 `std::mutex`를 잠그려 시도한다 — use-after-free다.
  이는 N6-I1-02가 겨냥한 4개 interleaving(§3, 전부 `shutdown_active=true`
  설정 **이후**의 창)과 다른 창이므로 그 fix로 막히지 않는다. 신규 test
  `test_destroy_during_shutdown_wait_is_deadlock_error`
  (`test_mesh_lifecycle_contracts.cpp:569-604`)는 claim을 미리 보유시켜
  shutdown이 이미 `:318`을 지나 drain wait에 파킹된 상태만 만든다
  (claim 획득이 `:577-582`에서 `zlink_mesh_node_shutdown` 호출보다 먼저
  끝남) — 이 다섯 번째 interleaving은 test가 만드는 순서에 없다.
- 영향: 정본은 같은 handle의 shutdown/destroy 재진입을 `EDEADLK`로 끝내야
  한다고 규정한다(`01-mesh-node.ko.md:508-509`). 이 창에서는 그 보장이
  깨지고, use-after-free·해제된 mutex 잠금·hang 또는 process corruption이
  가능하다 — N6-I1-02가 닫으려던 것과 동일한 결과 클래스(node 수명
  경쟁)이지만 다른 진입 경로다.
- 수정 범위: `as_mesh_node`가 반환하는 raw pointer에 registry lock 검사와
  실제 사용(첫 `node->mutex` 획득 또는 필드 접근) 사이의 수명 보장이
  없다는 근본 문제. shutdown·destroy 양쪽의 handle 검증부터 lock 획득까지
  구간을 단일 원자적 admission으로 병합하거나(예: registry lock을 쥔 채로
  node mutex까지 획득), node에 registry-lock 보호 참조 카운트를 추가해
  `as_mesh_node`가 반환하기 전에 pin하는 방식이 필요 — 설계는
  coordinator 책임.
- 검증 방향: shutdown이 `as_mesh_node` 통과 직후·`node->mutex` 획득 직전에
  정지하도록 제어 가능한 seam(테스트 전용 hook 또는 결정적 스레드 pause)을
  주입한 뒤 destroy를 완주시키는 역순 interleaving을 반복 실행해,
  정본의 `EDEADLK` 결과·node 미삭제·단일 최종 destroy 성공을 확인한다.
  ASAN(use-after-free 직접 탐지)과 TSAN(mutex-vs-delete race) 양쪽에서
  반복한다. active claim 경로와 timeoutless operation 경로 양쪽의 대기
  원인을 포함한다.
- severity 근거: N6-I1-02와 동일한 결과 클래스(use-after-free를 포함한
  node 수명 붕괴)이며 그 finding이 high였던 이유가 그대로 적용된다 —
  진입 경로만 다를 뿐 이 창을 트리거하는 데 특별한 조건이나 오용이
  필요 없다(정상적인 shutdown·destroy 호출의 통상적인 스레드 스케줄링
  변주).

#### CS7-I1-01 (high) — completion·reply record 구성이 submit-family bad_alloc 장벽 밖에 있고, 일부는 예외 경계가 전혀 없는 IO/timer 스레드에서 실행됨

- 이슈·근거: N6-I1-01+CS6-I1-01은 **submit(제출)** 경로 7곳을 닫았지만, 모든
  submit-family 함수가 공유하는 **completion(완료)** 기록자
  `complete_operation`(`core/src/runtime/services/mesh/mesh_runtime.cpp:849-888`)
  자체는 손대지 않았다. 그 안의
  `std::unique_ptr<queued_record_t> record (new queued_record_t ());`
  (`mesh_runtime.cpp:856`)는 `(std::nothrow)`가 아닌 일반 `new`다 — mesh
  하위 시스템 전체에서 `queued_record_t`를 할당하는 14곳 중 이 한 곳만
  예외뿐이다(나머지 13곳은 전부 `new (std::nothrow) queued_record_t ()` +
  null 검사, `grep -n "new queued_record_t\|new (std::nothrow)
  queued_record_t" core/src/api/mesh/*.cpp
  core/src/runtime/services/mesh/*.cpp`로 확인).
  `complete_operation`은 성공·실패·timeout·shutdown 종료·원격 wire 응답 등
  **모든** operation 완료 경로가 거치는 단일 지점이다(15개 호출부:
  `mesh_messaging_api.cpp:46,670,672`, `mesh_stream_session_api.cpp:297,531`,
  `mesh_node_api.cpp:395`(N6-I1-02가 방금 강화한 shutdown 꼬리),
  `mesh_actor_api.cpp:368,379,381,751,1069,1215,1217`,
  `mesh_dispatch_api.cpp:777,879`, `mesh_wire_ingress.cpp:589,596,615,621,625`).
  같은 계열의 caller-side 컨테이너 구성도 여전히 무방비다:
  - `core/src/api/mesh/mesh_dispatch_api.cpp:869` — 공개 `zlink_mesh_reply()`
    (`ZLINK_EXPORT`, `dispatch.h:193`)의 로컬 reply 경로
    `std::vector<zlink_msg_t> reply_parts (part_count_);`, try/catch 없음.
    :879에서 `complete_operation` 호출.
  - `core/src/api/mesh/mesh_actor_api.cpp:1200-1205` — 공개
    `zlink_actor_join_reply()`(`ZLINK_EXPORT`, `actor.h:172`)의
    `std::vector<unsigned char> kind_data (...)`와
    `std::vector<zlink_msg_t> reply_parts; reply_parts.resize
    (part_count_);`, try/catch 없음.
  - `core/src/api/mesh/mesh_actor_api.cpp:375-377` — 내부
    `actor_apply_remote_join_reply()`의 `kind_data` 벡터. 이 함수는
    **오직** `mesh_wire_ingress.cpp:592`(ingress 스레드의 `handle_reply`)
    에서만 호출된다 — 공개 API 경유가 아니라 원격 peer의 정상적인
    actor-join 응답을 처리하는 통상 경로다.
  - `core/src/runtime/services/mesh/mesh_wire_ingress.cpp:618-621` —
    `handle_reply`의 `ZLINK_MESH_OPERATION_ACTOR_LOOKUP` 성공 분기가
    `location` 구조체를 담는 `kind_data` 벡터를 구성, 역시 try/catch 없음.
  이 마지막 두 지점의 유일한 호출 경로인 `run_ingress_loop`
  (`mesh_wire_ingress.cpp:1054-1081`, `mesh_wire.cpp:165`에서
  raw `std::thread`로 기동)와, `on_operation_timeout`
  (`mesh_messaging_api.cpp:27-47`)을 부르는 timer 스레드
  (`request_timeout_scheduler_internal.cpp:82`의 `run_timeout_loop`,
  `:157`에서 `detach()`된 `std::thread`)에는 **어떤 catch도 없다**
  (`grep -n "catch" core/src/runtime/services/mesh/mesh_wire_ingress.cpp`
  0건, 같은 파일 `dispatch_wire_message`도 0건;
  `request_timeout_scheduler_internal.cpp`도 0건).
- 영향: 이 지점들 중 하나에서 실제 OOM 아래 `bad_alloc`이 발생하면 두
  갈래로 갈린다. (a) `zlink_mesh_reply`/`zlink_actor_join_reply`의
  동시적 로컬 경로에서 던지면 예외가 `extern "C"` 경계를 넘는
  undefined behavior다 — N6-I1-01/CS6-I1-01이 다른 7곳에서 막으려던 것과
  동일한 위반이며, 그 fix가 "submit family 전반"이라 서술한 범위가
  completion 절반을 빠뜨렸음을 보여준다. (b) ingress·timer 스레드 쪽
  (`mesh_wire_ingress.cpp:618`·`mesh_actor_api.cpp:375`·`complete_operation`
  자신)에서 던지면 표준이 보장하는 결과는 훨씬 더 확정적이다:
  `std::thread`(또는 detach된 thread)의 진입 함수를 예외가 벗어나면
  `std::terminate()`가 호출된다([thread.thread.constr]) — 이는 "정의되지
  않은 동작"이 아니라 **표준이 보장하는 전체 프로세스 abort**다. 게다가
  이 경로들은 공격적이거나 드문 입력이 아니라 통상적인 원격 actor-join
  응답·actor-lookup 응답·operation timeout 처리이므로, 시스템 메모리
  압박이라는 흔한 조건 하나로 살아있는 모든 원격 연결을 가진 node가
  네트워크 스레드에서 전체 프로세스째 죽을 수 있다. 이는 N6-I1-02가
  방금 봉인한 shutdown 꼬리(:395)에도 적용된다 — lifecycle 재진입은
  막혔지만 그 꼬리의 `complete_operation` 호출 자체는 여전히 무방비다.
- 수정 범위: `complete_operation`의 `new queued_record_t ()`를
  `new (std::nothrow)` + null 검사로 정렬하고, 호출자에게 실패를 알리는
  경로(현재 `void` 반환이므로 반환형 변경 또는 out-param 필요)를 추가.
  `zlink_mesh_reply`·`zlink_actor_join_reply`·
  `actor_apply_remote_join_reply`·`handle_reply`(ACTOR_LOOKUP 분기)의
  `reply_parts`/`kind_data` 구성을 기존 7곳과 동일한
  `try`/`catch (const std::bad_alloc &)` 장벽으로 감싸고, 공개 두 함수는
  `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`으로 매핑. ingress·timer 스레드
  경로는 반환 매핑이 없으므로 실패 시 안전한 관측 가능 대안(예: 해당
  operation을 조용히 미완료 상태로 남겨 재시도·timeout에 맡기거나, 프로세스
  차원의 정책 결정)이 필요 — 이는 coordinator 설계 판단.
- 검증 방향: `complete_operation`과 4개 caller-side 컨테이너 각각에
  할당 실패를 주입해(테스트 전용 allocator 훅) (1) 동기 공개 API 두 곳은
  `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM` 반환과 C ABI 밖 예외 0을, (2)
  ingress·timer 스레드 경로는 프로세스 abort 없이 안전하게 실패가
  흡수됨을, (3) 모든 경로에서 operation 상태(`node->operations`,
  `reply_routes`)에 고아 항목이 남지 않음을 확인한다.
- severity 근거: N6-I1-01/CS6-I1-01과 같은 결함 계열이지만, (a) 그
  fix가 "닫았다"고 서술한 대상(submit family 전반)의 상당 부분(모든
  completion·reply 경로)이 실제로는 손대지 않았고, (b) 그중 다수가
  UB가 아니라 표준이 보장하는 전체 프로세스 종료로 이어지며, (c) 그
  트리거가 임의 API 오용이 아니라 통상적인 원격 응답 처리이므로 iteration
  5·6이 이 계열에 매긴 medium보다 상향한다.

### I2 POSD·DDD — CLEAN

- finding 없음. `core/doc/internals/posd-module-structure.ko.md:124-144`가
  열거하는 mesh 4모듈이 `git ls-files
  core/src/runtime/services/mesh/`의 실제 8개 파일과 여전히 일치하고,
  `core/CMakeLists.txt:887-890` 빌드 목록도 동일하다. iteration 6→7
  delta는 기존 함수 본문만 수정했을 뿐 새 파일·새 모듈 경계를 만들지
  않았다.
- CS7-I1-01·CS7-I1-02 둘 다 계약(errno map)·동시성 수명 문제이지 모듈
  경계 침범이 아니다 — CS7-I1-01은 각 함수가 자신이 속한 API 계층의 기존
  helper(`complete_operation`)를 그대로 재사용했을 뿐 새 우회 경로를
  만들지 않았고, CS7-I1-02는 `mesh_node_api.cpp`와
  `mesh_runtime.cpp`의 기존 handle-검증/lock 계층 안에서 발생하는 순서
  문제일 뿐 두 파일의 책임 분담 자체는 바뀌지 않는다. 둘 다 I1에만
  계상하고 I2에 중복 계상하지 않는다(iteration 6의 CS6-I1-01과 동일한
  분류 원칙).

### I3 정리 완결성 — NOT CLEAN (low 1)

#### CS7-I3-01 (low) — CHANGELOG 검증 절이 iteration 6 fix commit(N6-I1-02 등)을 반영하지 않아 수치가 낡음

- 이슈·근거: `CHANGELOG.md:56-70`의 "Verification (release-candidate
  scope)" 절은 `test_mesh_lifecycle_contracts`를 "9 cases including the
  bind/destroy race hammer"라고 서술한다(`:65-67`). 그러나
  `grep -c "RUN_TEST (" core/tests/integration/test_mesh_lifecycle_contracts.cpp`는
  10을 반환하고 `ctest --test-dir core/build -N`의 Total Tests는 여전히
  85다 — iteration 6이 추가한 `test_destroy_during_shutdown_wait_is_deadlock_error`
  (§3)가 lifecycle 케이스 수를 9→10으로 늘렸지만 CHANGELOG는 갱신되지
  않았다. `git diff --stat b1e6c81fb f8c35e6fe`가 보여주듯 `CHANGELOG.md`
  자체가 이번 delta의 변경 파일 목록에 없다. 같은 절은 N6-I1-02(shutdown/
  destroy EDEADLK 재진입)·errno map `EDEADLK` 추가·N6-I1-01/CS6-I1-01 OOM
  장벽 어느 것도 언급하지 않는다.
- 영향: 이 finding은 실행 코드 결함이 아니라 문서 수치·서술의 최신성
  문제다 — CHANGELOG를 근거로 검증 범위를 판단하는 하류 소비자(릴리스
  노트, 다음 iteration 리뷰어)가 실제보다 좁은 검증 범위를 믿게 된다.
  iteration 5의 N5-I3-01/CS-I3-01(동일 계열, 그때는 해소됨)과 같은
  성격이다.
- 수정 범위: `CHANGELOG.md`의 lifecycle 케이스 수를 10으로 갱신하고,
  shutdown/destroy 재진입 EDEADLK 규칙과 submit-family OOM 매핑 확장을
  Verification 또는 Added/Changed 절에 반영.
- 검증 방향: `RUN_TEST (` 카운트와 `ctest -N`을 CHANGELOG 수치와
  재대조하고, iteration 6 커밋(`f8c35e6fe`)의 diff가 언급하는 모든 공개
  계약 변경(errno map, 신규 test)이 CHANGELOG에 나타나는지 확인.

그 밖의 정리 gate는 clean이다: 0-byte·merge marker 631개 파일 전수 0건,
`git diff --check` clean, CMake mesh wire 4 TU·mesh test 5개 등록 유지,
known-risk 관련 raw 파일(`socket_base.cpp`·`pipe.cpp`·`mailbox.cpp`·
`ctx_auto_hwm_recalc.cpp`) 이번 delta 무변경.

## 5. Known risk 4건 명시 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order 계열 | **수용·추적 유지, 신규 finding 없음** | lifecycle 재실행 경고 11건 전부 top frame이 `prepare_auto_hwm_socket_plan`/`refresh_auto_hwm_policy`/`ctx_t::create_socket`(`socket_base.cpp:225,372`). `git diff b1e6c81fb f8c35e6fe -- core/src/runtime/sockets/common/socket_base.cpp core/src/runtime/core/ctx_auto_hwm_recalc.cpp` diff 없음 — 이번 delta와 무관 |
| TSAN raw command mailbox ypipe 계열 | **수용·추적 유지, 신규 finding 없음** | lifecycle 재실행 data race 1건이 `mailbox_t::recv`(`mailbox.cpp:66`)로 귀결. `mailbox.cpp` 이번 delta 무변경 |
| raw socket teardown(`pipe_t::detach_peer_backref`·asio `blob_t`) | **9.x raw 기계 risk로 수용·추적 유지** | lifecycle 재실행에서 `pipe.cpp:202`(`detach_peer_backref`) 1건 관측, asio `blob_t` 경로는 이번 단독 실행에서 미발현(부하 의존적, 기존 관찰과 동일). `pipe.cpp` 이번 delta 무변경 |
| ctx_term linger | **수용·추적 유지** | `socket_base.cpp:129-134`의 blocking 기본 linger `-1`은 이번 delta로 미변경. 이번 delta는 `mesh_node_api.cpp`·`mesh_messaging_api.cpp` 등 mesh API 계층만 건드렸을 뿐 raw socket termination 정책을 손대지 않았다 |

TSAN 2-process admission 3건 실패(round-robin·MIXED·reconnect)는 과제
지시와 iteration-5 manifest §4가 이미 baseline `472f66a32`(수정 미포함)
재현으로 delta-무관을 확정한 사항이며, 이번 delta도 wire admission
로직(`mesh_wire_admission.cpp`)을 건드리지 않아 재현 조건이 달라질 이유가
없어 재실행하지 않았다.

TSAN stress 재실행 1회차의 `test_ready_handler_churn_under_load` FAIL
(`Expected 0 Was 304`, `test_mesh_stress.cpp:371`)은 iteration 5에서 내가
직접 문서화한 CS-I3-03(low/editorial, `iteration-5/claude-sonnet-raw-output.txt:189-202`,
`iteration-5/claude-sonnet-review.ko.md:35,190`)과 **동일한 실패
시그니처**다 — 그때도 "3회 실행 중 2회 FAIL, 1회 PASS, 정확히 같은 메시지"였고
근본원인은 이 test 자체의 TSAN-slowdown 대비 고정 4초 drain 예산 부족으로
판정했다(`test_mesh_stress.cpp`는 이번 delta에 포함되지 않은 파일). 이번
재실행도 1/3 FAIL·2/3 PASS로 동일 비율이라 새로운 관찰이 아니라 기존
관찰의 재확인이며, 신규 finding으로 계상하지 않는다.

## 6. 최종 판정

blocker 0, high 2(CS7-I1-02, CS7-I1-01), medium 0, low 1(CS7-I3-01) — I1과
I3가 `NOT CLEAN`이다. I2는 CLEAN이다. clean gate는 blocker·high·medium 0과
세 축 CLEAN을 동시에 요구하므로 충족하지 못한다.

iteration 6의 4개 병합 finding 중 3건(N6-I1-03·N6-I1-01+CS6-I1-01·
N6-I3-01)은 인용된 범위 안에서 전부 실측으로 해소를 확인했다. N6-I1-02
(shutdown/destroy 수명, high)는 그 finding이 명시적으로 겨냥한 4개
interleaving을 정확히 막았지만, 같은 결함 클래스의 다섯 번째
interleaving — handle 검증(`as_mesh_node`)과 실제 `node->mutex` 획득
사이의 lock 없는 창 — 이 fix 범위 밖에 그대로 남아 있어 use-after-free가
여전히 가능하다(CS7-I1-02). 별도로, N6-I1-01/CS6-I1-01이 "submit-family
전반"이라 서술한 bad_alloc 장벽은 제출(submit) 절반만 닫았고, 모든
submit-family 함수가 공유하는 완료(completion) 기록자
`complete_operation`과 그 호출자들의 reply 컨테이너 구성은 손대지 않은
채로 남아 있었다(CS7-I1-01). 그중 다수가 예외 경계가 전혀 없는 IO·timer
스레드에서 실행되어, 이전 iteration들이 이 결함 계열에 부여한 medium보다
결과가 확정적이고 심각하다(std::thread 경계를 벗어난 예외는 표준이
`std::terminate()`를 보장한다). 두 finding 모두 iteration 6이 닫았다고
서술한 범위가 실제로 충분했는지에 대한 문제이지, iteration 6이 고친 지점
자체가 잘못됐다는 뜻은 아니다.

CORE REVIEW NOT CLEAN
