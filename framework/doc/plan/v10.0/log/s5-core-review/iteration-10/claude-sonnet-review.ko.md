# S5 Core 구현 리뷰 — iteration 10, R2 (Claude Sonnet) 독립 리뷰

## Scope 확인

- worktree: `/tmp/claude-1000/zlink-s5-it10-sonnet` (detached HEAD `a4e91c01d`)
- 시작: 631 파일, aggregate `536d62e84fb7f00df811098da619697481ac717e9d626fdb08e27412dc489d3c` — manifest 일치
- 종료: 631 파일, aggregate `536d62e84fb7f00df811098da619697481ac717e9d626fdb08e27412dc489d3c` — 동일 (리뷰 중 어떤 파일도 수정하지 않음, `git status` clean 유지)

## iteration 9 finding 7건 해소 판정

1. **request 전달 뒤 timeout 준비 실패 (S5-R1-09-01, high)** — **해소**. `operation_submission_t`(`core/src/api/mesh/mesh_c_internal.hpp:171-197`, 구현 `mesh_node_api.cpp:91-172`)가 operation 등록·timeout guard·reply route를 소유하고, 소멸자(`mesh_node_api.cpp:121-130`)가 `_committed`가 아닐 때 operation과 reply route를 지우고 timeout guard를 파괴한다. `operation_timeout_guard_t`(`mesh_messaging_api.cpp:89-174`)는 스케줄 task가 `commit()` 또는 소멸(cancel)될 때까지 `operation_timeout_gate_t`로 대기(`on_operation_timeout`, 44-72줄)한다. 27개 submit 진입점 전부 `operation_submission_t` 사용을 grep으로 확인.
2. **reply/completion 선소비 (S5-R1-09-02, high)** — **해소**. `completion_reservation_t`(`mesh_runtime.hpp:184-194`)가 등록 시점에 list node를 미리 할당(`register_operation`, `mesh_node_api.cpp:69-89`)하고, `complete_pending_operation_with_commit`(`mesh_runtime.cpp:1018-1114`)이 하나의 node-mutex critical section 안에서 "레코드 준비→ready-index 삽입→(선택적 commit_locked_ 콜백)→mailbox splice→operation erase"를 원자적으로 수행한다. `mesh_wire_ingress.cpp` diff에서 `handle_reply`가 더 이상 tail 파싱 전에 `operations.erase()`를 선행하지 않고, `complete_pending_operation`의 재조회(`node_->operations.find`)가 exactly-once를 보장함을 확인. STREAM bound-session close는 `prepare_pending_operation_completion`/`commit_prepared_pending_operation`(`mesh_stream_session_api.cpp:1286-1318`)로 raw STREAM observer 호출을 node lock 밖에서 수행하도록 분리되어 있고, 신규 fault-injection 테스트(`test_mesh_lifecycle_contracts.cpp`의 `test_stream_session_binding_atomicity_and_destroy_race`, `test_submit_alloc_failure_maps_to_out_of_memory`)가 이 경로들을 실제로 검증함을 diff에서 확인했다.
3. **package version/ABI 불일치 (medium)** — **해소**. `core/CMakeLists.txt:11`(`VERSION 10.0.0`)·`:1370`(`SOVERSION "10"`), `debian/control`(`Package: libzlink10`/`libzlink10-dev`/`libzlink10-dbg`), `debian/changelog`(`10.0.0-0.1`), `debian/zlink.dsc`, `redhat/zlink.spec`(`Version: 10.0.0`, `lib_name libzlink10`), `nuget/package.config`(`version = "10.0.0" pathversion="10_0_0"`), `nuget/package.nuspec`(`<version>10.0.0</version>`) 전부 일치. `check_public_surface.py`의 `check_packaging_metadata()`가 CMake version/SOVERSION을 읽어 세 recipe를 검사함을 확인.
4. **dead forward declaration 4개 (low)** — **해소**. `spot_node_t`/`spot_pub_t`/`spot_sub_t`/`spot_internal_receiver_t`가 `core/src`, `core/include` 전체에서 0건.
5. **`pending_operation_t::deadline_ms` 제거 (low)** — **해소**. `mesh_runtime.hpp`의 `pending_operation_t`(196-206줄)에 해당 필드 없음. 남은 `deadline_ms`는 `transfer_state_t`(366줄, actor transfer 용도로 무관한 필드)뿐.
6. **REUSED_IDENTIFIER gate (low)** — **해소**. `check_public_surface.py:238-244`가 `removed-identifiers-10.0.0.json`의 `REUSED_IDENTIFIER`(`zlink_actor_join_result_t: ENUM_TYPE`)를 단일 kind로만 검사.
7. **C ABI OOM 정책 중복 27곳 (low)** — **해소**. `submit_out_of_memory_result()`(`mesh_runtime.cpp:980-984`)가 정책 단일 지점이고, `catch (const std::bad_alloc &) { return submit_out_of_memory_result (); }` 패턴이 mesh_dispatch_api.cpp(1)·mesh_messaging_api.cpp(10)·mesh_actor_api.cpp(10)·mesh_stream_session_api.cpp(6) = 27곳 정확히 일치.

## 재현 실패 2건 원인 분석

### 1) `unittest_request_timeout_scheduler` — 진짜 race, coordinator 재분류를 기각한다

`core/src/api/socket/request_timeout_scheduler_internal.cpp`의 `schedule()`(163-190줄)은 두 개의 별도 critical section으로 나뉜다.

- `ensure_started()`(151-160줄): `state.mutex`를 잡고 `state.started`를 확인, true면 즉시 반환(새 스레드 미생성).
- 삽입+notify(178-186줄): `state.mutex`를 **다시** 잡고 `state.schedule.insert()` 후 `notify_all()`.

`run_timeout_loop`의 idle-exit 분기(88-100줄)는 `state.schedule`이 비고 100ms(`idle_exit_wait_ns`) 동안 아무 일도 없으면 `state.started = false`를 설정하고 스레드를 종료한다(같은 `state.mutex` 아래 원자적).

TOCTOU 창: `ensure_started()`가 `state.started==true`를 보고 반환한 **직후**, 삽입 critical section을 잡기 **전** 사이에 기존 스케줄러 스레드가 idle-exit로 종료(`started=false` 설정 후 반환)하면, 뒤이은 삽입은 소비자가 없는 `state.schedule`에 들어가고 `notify_all()`은 아무도 깨우지 못한다. 이 task는 이후 어떤 다른 `schedule()` 호출이 `ensure_started()`를 다시 실행할 때까지 고아 상태로 남는다 — idle 시스템에서는 두 critical section 사이 간격이 수 나노초라 이 창을 실제 스레드 선점이 때리기 거의 불가능하지만, sanitizer `-j20`(load ~43) 같은 고경합 환경에서는 OS 스케줄링 지연이 이 간격을 밀리초~초 단위로 늘려 창을 실질적으로 키운다. 이는 정확히 관찰된 패턴과 일치한다: idle 재실행 10/10 통과, 고부하 3/3 재현 실패(1ms deadline인데 handler가 6초 내 미진입, 관측값 7820ms — orphan된 task는 이후 다른 `schedule()` 호출이 우연히 스케줄러를 재기동시킬 때까지 부정형 지연을 갖는다는 것과 정확히 부합).

**이 scheduler는 `core/src/api/mesh/mesh_messaging_api.cpp`의 `operation_timeout_guard_t`(89-138줄)가 모든 Mesh request timeout에 사용**하며(`request_timeout::schedule`, 123줄), `core/doc/spec/core/service/01-mesh-node.md:332-333`("A terminal reply, timeout, shutdown, or route failure arrives exactly once as a completion record")가 약속하는 시한 내 completion 전달을 이 race가 깨뜨릴 수 있다. coordinator의 "idle 재실행 10/10 통과이므로 flake"라는 재분류는 근본원인을 제거하지 못한다 — race는 발현 확률이 부하에 비례할 뿐 실재하며, iteration-9가 새로 강화한 operation transaction의 원자성 보장이 그 아래 계층인 이 scheduler의 결함으로 무력화된다. **독립적으로 I1 finding으로 등록한다** (아래 참조).

### 2) `test_monitor_socket_contract` — `mutex.hpp:108` abort는 신규 결함이 아니다

`core/src/runtime/utils/mutex.hpp:108`은 `pthread_mutex_lock()`의 `posix_assert(rc)`이며, `EINVAL`은 전형적으로 이미 파괴되었거나 초기화되지 않은 mutex에 대한 lock 호출을 뜻한다. 이 정확한 실패 시그니처(`test_monitor_socket_contract` teardown, `mutex.hpp:108` Invalid argument)는 **iteration-4에서 이미 근본원인이 규명된 사례**(`framework/doc/plan/v10.0/log/s5-core-review/iteration-4/claude-fable-review.ko.md:95-103,144-148`)와 일치한다: `test_pubsub_ready_with_monitor_recv_and_socket_callback`의 raw PUB slow-joiner 창(CONNECTION_READY가 구독 전파를 보장하지 않는 §9 raw PUB 계약 안의 동작) 때문에 EAGAIN 후 테스트의 강제 teardown close가 겹치면서 발생하는 2차 증상이며, 오염 없는 조건에서는 전체 suite 2회+단독 3회+CPU 포화 15회 전량 green으로 재확인됐다. `f5000d2fe..a4e91c01d` diff는 `core/src/runtime/utils/mutex.hpp`, `socket_runtime.hpp`, `pipe.cpp` 등 이 경로에 관련된 어떤 파일도 건드리지 않으므로, iteration 9/10 변경이 이를 유발했다는 근거가 없다. **신규 finding 아님** — 기존 9.x 유지 기계의 시계열 민감성으로 재확인, editorial 수준(test-only 강화 여지)만 유효.

## I1 — 계약 구현 일치

**Finding:**
`[I1][high] core/src/api/socket/request_timeout_scheduler_internal.cpp:88-100,151-160,169-186` — scheduler의 idle-exit(88-100줄)와 `ensure_started()`의 "이미 시작됨" 판정(151-160줄)이 서로 다른 `state.mutex` critical section이라, `schedule()`(163-190줄)이 `ensure_started()`를 호출한 직후·작업을 실제로 큐에 넣기(178-186줄) 전 사이에 기존 스케줄러 스레드가 idle-exit하면 방금 등록한 task가 소비자 없이 고아가 된다 — 근거: 위 재현 실패 분석. 이 scheduler는 `mesh_messaging_api.cpp:89-138`의 `operation_timeout_guard_t`를 통해 모든 Mesh request timeout에 쓰이고, `core/doc/spec/core/service/01-mesh-node.md:332-333`이 약속하는 "timeout이 정확히 한 번 completion으로 도착한다"는 시한 보장을 이 race가 무너뜨릴 수 있다. coordinator의 "idle 10/10 통과이므로 flake"라는 재분류는 근본원인을 반박하지 못한다(경합이 큰 환경일수록 창이 넓어지는 전형적 TOCTOU 패턴과 정확히 일치) — 수정 제안: `ensure_started()`의 상태 판정과 task 삽입을 하나의 `state.mutex` critical section으로 병합하거나, 삽입을 먼저 수행한 뒤(스케줄러가 이를 관찰할 수 있게) `ensure_started()`를 재확인하는 순서로 바꿔 idle-exit 결정과 신규 작업 발행이 항상 같은 lock 아래 직렬화되게 한다.

**Evidence:** 위 코드 정적 분석 + coordinator의 고부하 재현(3/3 실패, "Expected 7820 to be less than or equal to 6000") + idle 재현(10/10 통과)이 race 가설과 정확히 부합. `mesh_messaging_api.cpp`에서 이 scheduler가 Mesh request timeout 경로에 쓰임을 grep으로 확인. iteration 9 finding 7건은 모두 소스 대조로 해소를 확인(위 §2). `test_monitor_socket_contract`/`mutex.hpp:108`은 iteration-4에서 이미 규명된 pre-existing 이슈로 재확인, diff 미겹침 확인.

**Verdict: NOT CLEAN** (high finding 1건)

## I2 — POSD·DDD 리팩터링

**Finding:** 없음.

**Evidence:** `operation_submission_t`/`completion_reservation_t`/`prepare_pending_operation_completion`/`commit_prepared_pending_operation`은 각 트랜잭션 경계(request 준비, terminal 준비/commit)를 한 곳에 캡슐화하고 있고, 호출부(mesh_actor_api.cpp, mesh_stream_session_api.cpp, mesh_dispatch_api.cpp)는 rollback 순서를 알 필요 없이 `submission.commit()`/`prepare_...`/`commit_...`만 호출한다. `complete_pending_operation_with_commit`과 `prepare/commit_prepared_pending_operation` 사이에 구조적 유사성이 있으나(둘 다 splice+erase), 이는 STREAM close가 raw socket 호출을 node lock 밖에서 해야 하는 서로 다른 동시성 요구(문서화됨, `services-internals.md` §4)에서 기인하는 정당한 분기이며 불필요한 추상화 통합이 아니다. 공개 API·호출자 설정 추가 없음(ledger §3 주장과 diff 일치).

**Verdict: CLEAN**

## I3 — 정리 완결성

**Finding:** 없음.

**Evidence:** dead forward declaration 4개(`spot_node_t` 등) 전체 저장소에서 0건, `pending_operation_t::deadline_ms` 제거 확인, `register_operation`이 private helper로 정확히 국한, `REUSED_IDENTIFIER` gate 정상 동작, `submit_out_of_memory_result()` 27곳 정확히 일치하고 그 외 `ZLINK_SUBMIT_OUT_OF_MEMORY` 직접 반환은 모두 별도 fallible 실패(예: `operation_submission_t::valid()==false`, `zlink_msg_init_size` 실패)를 자체 errno로 이미 세팅한 경로로 정책 중복이 아님을 확인. `CHANGELOG.md`/`services-internals.md`/`threading-model.md` 갱신 내용이 실제 코드 동작과 일치.

**Verdict: CLEAN**

## Known risk 4건

1. **TSAN auto-HWM lock-order** — `core/src/runtime/core/ctx_auto_hwm_recalc.cpp:79-116`에서 `_slot_sync`를 쥔 채 `prepare_auto_hwm_socket_plan`/`apply_auto_hwm_socket_plan`을 호출하는 패턴 그대로 확인. `f5000d2fe..a4e91c01d` diff에 이 파일·`socket_base.cpp` 없음. **수용·추적 유지, 신규 아님.**
2. **TSAN raw command mailbox ypipe** — `mailbox.cpp`가 diff에 없음, 기존 관찰과 무관한 신규 변경 없음. **수용·추적 유지, 신규 아님.**
3. **raw socket teardown 관찰(`pipe_t::detach_peer_backref`, Asio `blob_t`)** — `core/src/runtime/core/pipe.cpp`가 diff에 없음. **수용·추적 유지, 신규 아님.**
4. **`ctx_term` linger** — `ctx_termination.cpp` 등 관련 파일이 diff에 없음. **수용·추적 유지, 신규 아님.**

네 항목 모두 이번 mesh 전용 delta와 소스 수준에서 완전히 분리되어 있음을 diff stat과 파일별 대조로 확인했다.

CORE REVIEW NOT CLEAN
