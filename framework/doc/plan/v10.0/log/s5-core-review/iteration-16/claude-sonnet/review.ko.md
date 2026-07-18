# S5 Core 구현 리뷰 — iteration 16 — R2 Claude Sonnet

## 1. Scope 확인

- 검토 checkout: `/tmp/claude-1000/zlink-s5-it10-sonnet`, detached HEAD `1f247af7ae3946b74945ff96aa1462d1a984a3ac`
  (`core(control): resolve S5 iteration-15 finding`). 시작·종료 모두 `git status --short --untracked-files=all`
  clean, HEAD 불변 확인 — 리뷰 중 파일 수정 없음.
- Scope 파일 수 실측(시작·종료 동일, `git ls-files`와 `git ls-tree -r HEAD` 교차 확인 일치): **632**.
- Scope aggregate SHA-256(`LC_ALL=C sort` 고정, 시작·종료 동일):
  `952284938c531e471e34df46709388623778892da4ccc10367af8ba997d62911`.
- **Process note (finding 아님)**: prompt.md/manifest.ko.md에 기재된 값(631 파일,
  `398ee290bd39d1fb2070b9585cfb86bf63646f2407600c68ca26a4070e7fa993`)과 불일치한다. 원인 추적: 리뷰 대상
  commit `1f247af7a`가 scope 내 신규 파일 `core/tests/unittest/unittest_service_control_runtime.cpp`를
  추가했다(`git show --stat HEAD` 확인, 10 files changed 중 scope 내 신규 3건: 소스 1·테스트 1·CMakeLists 수정 1,
  로그 문서 6건은 scope 밖). 직전 commit `7b580a520`은 `git ls-tree -r 7b580a520`으로 631 파일임을
  재확인했다 — manifest 통계가 이번 commit 반영 전 값을 그대로 옮겨 적은 것으로 보인다. checkout 자체는
  clean·정확한 detached HEAD로 변조 근거는 없으므로 코드 finding으로는 등록하지 않고 여기 기록만 남긴다.
  scope에 새로 들어온 파일(`unittest_service_control_runtime.cpp`)은 아래 §2에서 직접 검토했다.

## 2. 우선 검증 — iteration 15 병합 finding 2건

### S5-15-01 (worker lifecycle) — **해소 확인**

`core/src/runtime/services/control/service_control_runtime.cpp`:
- `loop()`의 `call.fn (call.arg)` 호출이 `catch (const std::bad_alloc &)`로 개별 봉인됐다(L270-279).
  주석대로 task는 호출 전에 이미 재스케줄됐으므로(`schedule_task_locked` L240, `call.fn` 호출 L271 이전)
  실패한 tick만 버려지고 task는 다음 주기에 재시도한다.
- epilogue(`_active_task_id` 해제 + `_cv.broadcast()`, L281-287)는 try/catch 블록 **밖**에 있어 callback
  성공/bad_alloc 실패 양쪽 경로 모두 무조건 실행된다. `remove_task`의 대기 루프(`while (_active_task_id ==
  task_id_) _cv.wait(...)`, L133-134)와 `stop()`이 이 broadcast에 의존하는데, 봉인 전이라면 예외가
  epilogue를 건너뛰어 영구 대기가 발생했을 결함이 정확히 이 지점에서 막힌다.
- `run()`의 최후 `catch (const std::bad_alloc &)`(L171-176)는 `loop()` 자체(주로 steady-state에서는
  할당이 없다고 문서화됐지만 예외적 실패 시)에서 예외가 올라온 경우를 대비해 `_stopping=true`·
  `_active_task_id=0`·broadcast를 한 critical section에서 commit한다 — 죽은 worker가 새 task를 받지
  않도록 `add_periodic_task`가 `_stopping`을 확인하는 계약(L78)과 정합한다.
- `~service_control_runtime_t()` → `stop()` → `ctx_runtime_resources_t::teardown()` 경로를 추적한 결과
  (`ctx_runtime_resources.cpp` L52-56), worker가 이미 죽은 뒤에도 `stop()`이 이미 설정된 `_stopping`을
  재설정하고 `_thread.stop()`(이미 종료된 스레드에 대한 join)이 정상 반환해 `zlink_ctx_term`이 걸리지
  않음을 확인했다.
- 회귀 unittest `unittest_service_control_runtime.cpp`: `throwing_task`가 첫 tick(`run_immediately=true`)
  에서 `std::bad_alloc`을 던지고, `wait_for_ticks(3)`으로 worker 생존(누적 3틱)을 관찰하며,
  `remove_task()`가 즉시 반환(`TEST_ASSERT_EQUAL_INT(0, ...)`)하고 `zlink_ctx_term`이
  `ZLINK_CLOSE_OK`로 종료함을 검증한다 — S5-15-01의 세 계약(생존·즉시 반환·정상 종료)을 정확히 포괄한다.
  `core/tests/unittest/CMakeLists.txt` L21에 무조건(플랫폼 게이트 없이) 등록됐다.

### S5-15-02 (schedule_task_locked fallback) — **해소 확인**

`schedule_task_locked`(L179-193)의 도달 불가 분기가 `zlink_assert (!task_->cached_node.empty ())`로
교체됐다. 불변식을 코드 경로로 추적: `cached_node`는 `deschedule_task_locked`가 `_schedule.extract`로
채우거나(L195-203) `loop()`가 due task를 직접 extract할 때(L236) 채워진다. `schedule_task_locked`는
항상 자신의 `deschedule_task_locked` 호출을 먼저 거치는데(L184), 그 호출이 no-op이 되는(= `scheduled`가
이미 false인) 시점은 두 caller(`wakeup_task` L149, `loop()` L236-237) 모두 그 직전에 이미 extract를
수행해 `cached_node`를 채운 뒤였다. `add_periodic_task`(최초 삽입, L91-103)는 `schedule_task_locked`를
거치지 않고 `_schedule.insert`를 직접 호출하므로 이 assert 경로에 들어오지 않는다. 즉 assert는 실제로
도달 불가능하며, "모든 caller가 deschedule 선행 — 무할당 계약 명시"라는 커밋 설명과 일치한다.

## 3. iteration 10 finding 8건 회귀 판정

15회의 반복 검토를 거쳐 CLEAN이 유지돼 온 항목들이다. 이번 commit(`1f247af7a`)의 diff는
`service_control_runtime.cpp`·해당 테스트·`CMakeLists.txt`·로그 문서로 국한돼(`git show --stat HEAD`
확인) 아래 8건의 수정 지점을 직접 건드리지 않는다. 각 수정 메커니즘이 현재 소스에 그대로 남아있음을
재확인했다 — 새 반례 없음, 전부 해소 유지.

| ID | 요약 | 재확인 근거 |
|---|---|---|
| S5-10-01 | `request_timeout_scheduler` lost-wakeup | `request_timeout_scheduler_internal.cpp::schedule()` L171-193 — liveness 판정(`starting`)·스레드 기동·`_schedule.insert`가 한 `lock_guard` critical section 안에서 수행됨 |
| S5-10-02 | MeshNode lifecycle generation 고정 1 | `mesh_runtime.cpp:82` `allocate_lifecycle_generation()`, `mesh_runtime.cpp:354`에서 node 생성자가 이를 호출 |
| S5-10-03 | stale raw timeout pointer ABA | `mesh_runtime.hpp:207` `pending_operation_t::timeout_task`가 `shared_ptr<task_t>`를 소유 |
| S5-10-04 | monitor registry UAF | `monitor_api_internal.hpp:71-99` `pin/unpin_monitor_handler_state` + `monitor_state_pin_t` RAII, reader 5개 진입점(`monitor_query_api.cpp:149`, `monitor_socket_api.cpp:31`, `monitor_api.cpp:194,416`, `zlink.cpp:136`) 전수 전환 확인 |
| S5-10-05 | join reply flags 무시 | `mesh_wire.cpp:476-505` `wire_submit_join_reply`가 `flags_`를 `send_data_message`에 그대로 관통 |
| S5-10-06 | acceptor 오류 EADDRINUSE 오분류 | `asio_tcp_acceptor_config.hpp:29-41` `acceptor_error_to_errno()` — `address_in_use`만 EADDRINUSE, open/reuse/bind/listen 4곳(L57,70,100,108) 전수 사용 |
| S5-10-07 | 테스트 대기 상한 단위 오류 | `unittest_request_timeout_scheduler.cpp:40-43` `timeout_us = SETTLE_TIME*20*1000`, `zlink_stopwatch_intermediate`(µs 반환)와 직접 비교 |
| S5-10-08 | stale internals 문서 | `core/doc/internals/multipart-atomicity.ko.md` — internals는 이번 iteration 판정·수정 대상에서 제외(prompt.md §2 규칙), S5-11 이관 방침대로 미착수 상태 유지. 리뷰 대상 아님 |

## 4. I1 — 계약 구현 일치

**Finding**: 없음.

**Evidence**: §2·§3의 재확인에 더해, 이번 commit이 실제로 변경한 유일한 공개 표면 인접 지점은
`service_control_runtime_t`(내부 worker, 공개 API 아님)이며 이를 사용하는 공개 함수
`zlink_ctx_auto_hwm_recalculate`(`01-context.md` L264-289, "Thread safety: Safe to call from any thread")의
계약(오류 `EFAULT`만, 나머지는 무오류)은 이번 변경으로 오히려 강화됐다 — worker가 bad_alloc으로
죽어도 이후 `zlink_ctx_term`이 걸리지 않는다. `ctx_auto_hwm_recalc.cpp`의 debounce 폴링(10ms 주기 검사,
기본 debounce 3000ms)도 spec L61/L90의 옵션 정의와 일치한다. package metadata
(`core/CMakeLists.txt:11` VERSION 10.0.0, `:1370` SOVERSION 10; `debian/control`·`debian/changelog`
libzlink10; `redhat/zlink.spec:13` Version 10.0.0, `lib_name libzlink10`; `nuget/package.nuspec:10`
10.0.0)도 전부 정합.

**Verdict**: CLEAN.

## 5. I2 — POSD·DDD

**Finding**: 없음.

**Evidence**: `service_control_runtime_t`는 여전히 단일 책임(주기 task worker)을 유지하며, 이번 수정도
그 경계를 넘지 않았다 — bad_alloc 봉인 정책과 lifecycle commit이 클래스 내부에 캡슐화됐고, `ctx_t`
쪽 호출자(`ctx_auto_hwm_recalc.cpp`, `ctx_runtime_resources.cpp`)는 여전히 `add_periodic_task` /
`remove_task` / `wakeup_task` 표면만 사용한다. `pending_operation_t`가 `timeout_task`를 소유하는
구조(S5-10-03), monitor registry pin RAII(S5-10-04) 등 이전 iteration에서 정리된 깊은 모듈 경계도
이번 diff로 훼손되지 않았다.

**Verdict**: CLEAN.

## 6. I3 — 정리 완결성

**Finding**: 없음.

**Evidence**: TODO/FIXME/XXX 텍스트 검색(`core/src`, `core/include`) 결과 실질 항목 0건(유일한 매치는
`ip.cpp:449`의 `mktemp` 템플릿 문자열 `"tmpXXXXXX"`로 오탐). S5-10-04에서 제거됐다고 기록된
`spot_provider` 관련 잔재를 재검색했으나 소스 전역에 0건 — 삭제가 이후 커밋들에서도 재유입되지 않았다.
신규 테스트 `unittest_service_control_runtime`은 `CMakeLists.txt`에 무조건 등록되어 고아 target이
아니다.

**Verdict**: CLEAN.

## 7. Low finding 목록

없음.

## 8. Known risk 4건 판정

1. **TSAN auto-HWM lock-order** — 미해소·추적 유지. `ctx_auto_hwm_recalc.cpp::auto_hwm_recalculate_now()`가
   `_slot_sync`를 잡은 채 각 소켓의 `prepare_auto_hwm_socket_plan`/`apply_auto_hwm_socket_plan`을
   호출하는 구조(L80-117)가 그대로 남아있다. 이번 commit은 이 경로를 건드리지 않았다(`git show --stat`
   확인) — 새 반례 없음.
2. **raw command mailbox ypipe** — 미해소·추적 유지. `mailbox.hpp`(`ypipe_t<command_t, ...>`)는 이번
   commit이 건드리지 않은 파일이다 — 새 반례 없음.
3. **raw socket teardown 관찰** — 미해소·추적 유지. 관련 파일 변경 없음 — 새 반례 없음.
4. **ctx_term linger** — 계약 일치, finding 아님. 오히려 이번 S5-15-01 수정으로 worker가 bad_alloc으로
   죽은 뒤에도 `stop()`이 걸리지 않음을 §2/§4에서 직접 추적 확인했다 — 판정 유지·근거 보강.

## 9. 결론

I1·I2·I3 모두 CLEAN(blocker·high·medium finding 0건), iteration 15 병합 finding 2건과 iteration 10
병합 finding 8건 모두 소스 대조로 해소 확인, 새 반례 없음. known risk 4건은 이번 commit이 해당 코드를
건드리지 않아 판정 불변.

CORE REVIEW CLEAN
