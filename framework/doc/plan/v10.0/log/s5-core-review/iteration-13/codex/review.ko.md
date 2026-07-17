# S5 Core 구현 리뷰 iteration 13 — Codex

## 1. Scope 확인

- 대상 commit: `7c7fb0feb6d042cc36c616e8b659543e41ae3c42`
- 시작: 631 files, aggregate SHA-256 `62999e63af011f587a8a228b6cb9f6ca55c0055bccc058383058c385b591a3b1`
- 종료: 631 files, aggregate SHA-256 `62999e63af011f587a8a228b6cb9f6ca55c0055bccc058383058c385b591a3b1`
- 시작과 종료 시 대상 checkout의 `git status --short`: 출력 없음.
- aggregate는 지정 scope의 `git ls-files` 결과에 대해 각 파일의 `sha256sum`을 파일명 순으로 정렬한 뒤 다시 `sha256sum`한 값이다.
- `core/doc/internals`는 hash 범위에만 포함했고 판정 근거와 수정 대상으로 사용하지 않았다.
- 검토 중 대상 checkout 파일은 수정하지 않았다. 산출물은 지정 review 디렉터리의 `progress.md`와 이 파일뿐이다.
- 절차 규정에 따라 build·테스트·sanitizer·package 생성은 실행하지 않았다. 실행 증거는 iteration 13 manifest §2의 일반 build 오류 0 및 CTest 85/85만 사용했다.

## 2. 직전 finding 해소 판정

### iteration 12 병합 finding 3건

1. **S5-12-01 generation — 해소 판정 수용.** allocator는 epoch microsecond를 앵커로 사용하고 process 안에서는 CAS로 강단조다(`mesh_runtime.cpp:82-107`). 같은 RID의 동일-µs 재시작이나 wall-clock rollback 가능성 자체는 남지만, 정식 spec §4는 generation이 weight revision과 별개로 같은 RID의 새 수명을 구분한다고만 규정하고(`service/01-mesh-node.ko.md:209-212`), §5는 duplicate/stale generation을 conflict로 거부하고 오직 higher generation만 기존 수명을 교체한다고 규정한다(`:242-248`). Core 발급자에게 cross-process durable storage나 모든 재시작의 강단조 값을 보장하라는 계약은 없다. 따라서 충돌을 무단 교체가 아니라 명시된 admission conflict로 표면화한다는 coordinator 해석을 수용한다. microsecond 상향은 충돌 저항 개선일 뿐 판정 근거는 이 계약 경계다.
2. **S5-12-02 monitor 등록 원자성 — 기본 rollback은 해소됐으나 새 구체적 반례로 최종 해소 안 됨.** 이전 handler·userdata·provider·subject를 캡처하고 task 등록 실패 시 복원하며 `bad_alloc`을 `ENOMEM`으로 봉인하는 기본 경로는 맞다(`monitor_api.cpp:344-380`). 그러나 `run_immediately=true` task가 task ID 저장보다 먼저 handler를 실행할 수 있고, 허용된 callback self-close와 교차하면 제거되지 않은 periodic task가 삭제된 state를 다시 사용한다. 상세 반례는 I1/I2 finding에 기록한다.
3. **S5-12-03 detach primitive — 해소됨.** `operations.erase`의 잔여 분모는 primitive 내부 1곳(`mesh_runtime.cpp:1015-1021`)과 pre-commit submission rollback 2곳(`mesh_node_api.cpp:91-130`)뿐이다. runtime terminal 3곳은 primitive를 사용하고 cancel을 node mutex 밖에서 수행한다(`mesh_runtime.cpp:1086-1155`, `:1241-1285`). Actor terminal 3곳도 primitive와 mutex 밖 cancel을 사용한다(`mesh_actor_api.cpp:382-456`, `:1413-1497`). rollback은 timeout guard가 아직 task를 소유하고 destructor에서 cancel하므로 예외로 올바르다(`mesh_messaging_api.cpp:157-201`).

### 출력 계약의 iteration 10 finding 8건

1. **S5-10-01 scheduler lost-wakeup — 해소됨.** liveness 판정·thread 기동·task 삽입과 idle-exit commit이 같은 scheduler mutex로 직렬화되고 self-cancel은 firing thread를 기다리지 않는다(`request_timeout_scheduler_internal.cpp:95-104`, `:171-193`, `:197-221`). idle-exit sweep은 CTest 대상에 등록돼 있고 manifest §2의 85/85에 포함됐다.
2. **S5-10-02 lifecycle generation — 해소됨.** boot-relative anchor는 epoch microseconds로 교체됐고, durable cross-process 발급은 정식 Core 계약이 요구하지 않는다는 위 S5-12-01 해석을 적용한다.
3. **S5-10-03 timeout task 소유 — 해소됨.** full operation ID 검증, guard 인계, detach primitive, 모든 terminal/destroy 경로의 mutex 밖 cancel을 확인했다.
4. **S5-10-04 monitor registry pin — 원래 finding은 해소됨.** reader pin, unregister drain과 expected-state identity 갱신은 유지된다. 이번 finding은 registry replacement가 아니라 task ID publication과 immediate callback self-close의 새 interleaving이다.
5. **S5-10-05 join reply flags — 해소됨.** local completion은 선예약 storage를 사용하고 remote path는 public flags를 `wire_submit_join_reply()`와 최종 send까지 전달하며 실패한 token을 retry 가능 상태로 되돌린다(`mesh_actor_api.cpp:1461-1513`, `mesh_wire.cpp:476-504`).
6. **S5-10-06 acceptor errno — 해소됨.** 실제 address collision만 `EADDRINUSE`로 분류하고 Windows system error는 `wsa_error_to_errno()`로 변환한다(`asio_tcp_acceptor_config.hpp:24-40`, `err.cpp:201-314`).
7. **S5-10-07 테스트 단위 — 해소됨.** stopwatch microsecond 단위에 맞춘 상한을 유지한다(`unittest_request_timeout_scheduler.cpp:38-50`).
8. **S5-10-08 — 이번 판정 대상 아님.** 지시대로 internals를 근거 또는 수정 대상으로 사용하지 않았다.

## 3. I1 — 계약 구현 일치

### Finding

- `[I1][high] core/src/api/monitoring/monitor_api.cpp:367 — S5-12-02를 수정 commit 7c7fb0feb에서 재지적: immediate dispatch task와 handler callback self-close가 교차하면 periodic task가 삭제된 monitor state를 재사용함 — handler를 먼저 publish한 뒤 run_immediately task를 등록하고(:344-367), task ID는 add_periodic_task가 반환한 뒤에만 state에 저장한다(:382). scheduler는 task를 호출 전에 다음 주기로 재등록한다(service_control_runtime.cpp:183-205, :228-237). scheduler가 ID 저장보다 먼저 queued event의 handler를 실행하고 handler가 self-close하면, finalizer는 dispatch_task_id==0을 먼저 snapshot한 뒤(:108-129) 등록 caller의 registry pin drain을 기다린다(:89-105). caller가 ID를 저장하고 pin을 해제하면 finalizer는 이미 snapshot한 0 때문에 task를 remove하지 않고 state를 delete하며, 다음 tick이 dangling arg를 `monitor_handler_task()`에 전달한다. monitor callback self-close는 명시적으로 허용된 계약이다(socket/README.ko.md:619-626) — handler publication부터 dispatch_task_id 저장까지 `dispatch_sync` 또는 scheduler의 비활성 task reservation으로 묶어 callback이 완성된 task identity보다 먼저 진입하지 못하게 하고, immediate queued-event self-close 회귀 테스트를 추가한다. 또한 `_tasks` insert 뒤 `_schedule` insert가 bad_alloc으로 실패하는 경우(service_control_runtime.cpp:92-95, :152-159) partial task entry를 제거해 task 등록 자체에 strong rollback을 제공한다.`

### Evidence

- 위 항목은 S5-12-02와 같은 error-atomicity root-cause family다. 이전 iteration에는 없던 구체적 반례로, `7c7fb0feb`이 새로 추가한 publish→task 확보→ID 저장 순서와 기존 self-close finalizer를 함께 추적해 재지적했다.
- 등록 caller는 `monitor_state_pin_t`를 set 함수 반환까지 보유한다(`monitor_socket_api.cpp:31-45`). 이 pin은 setter의 ID 저장 전 state 삭제는 막지만, finalizer가 pin wait 전에 ID 0을 snapshot하는 것을 막지 않으므로 stale ID에 의한 task 미제거 반례는 그대로 성립한다.
- `zlink_close()`는 현재 monitor callback의 state를 인식하면 `close_requested`를 설정하고 성공을 반환하며(`zlink.cpp:136-143`), callback epilogue가 finalizer를 실행한다(`monitor_api.cpp:167-179`). 따라서 반례는 금지된 API 사용에 의존하지 않는다.
- 공통 spec 8개, service spec 5개, socket spec 9개와 영문 대응본, modular public header, errno mapper와 구현 entry point를 정적으로 대조했다. 그 밖의 blocker/high/medium 계약 불일치는 찾지 못했다.
- public surface contract target은 formal spec/header/removed identifier/package metadata/export를 대조하도록 등록돼 있다(`tests/CMakeLists.txt:910-923`, `tests/contract/check_public_surface.py:215-276`). 실행은 하지 않았고 manifest §2의 CTest 85/85 기록만 사용했다.
- package metadata는 CMake 10.0.0/SOVERSION 10(`core/CMakeLists.txt:11`, `:1368-1370`), Debian 10.0.0 및 `libzlink10`(`packaging/debian/changelog:1-5`, `zlink.dsc:1-13`, `control:18-57`), RPM 10.0.0/`libzlink10`(`packaging/redhat/zlink.spec:11-14`), NuGet 10.0.0과 `10_0_0` artifact 이름(`packaging/nuget/package.config:4`, `package.nuspec:10,46-91`, `package.targets:29-119`)으로 일치했다.

### Verdict

**NOT CLEAN** — blocker 0, high 1, medium 0.

## 4. I2 — POSD·DDD

### Finding

- `[I2][high] core/src/api/monitoring/monitor_api.cpp:344 — S5-12-02와 같은 family: monitor handler의 registry publication, scheduler task 생성, task identity publication과 self-close finalization 책임이 하나의 원자적 lifecycle로 캡슐화되지 않음 — caller pin은 storage 수명만 보호하고 scheduler가 state를 호출할 자격과 finalizer가 제거할 task ID의 일치를 보호하지 않는다. 이 시간적 분해 때문에 finalizer가 stale task ID를 snapshot하고 scheduler에는 dangling state가 남는다 — monitor registration owner가 handler state와 task identity를 함께 commit하는 깊은 primitive를 소유하게 하고, scheduler에는 등록 완료 전 실행 불가능한 reserve/activate 인터페이스 또는 동등한 synchronization을 제공한다.`

### Evidence

- S5-12-03은 operation identity 검증·task 인계·erase를 `detach_pending_operation_locked()`에 모아 여섯 terminal caller에서 lifecycle 지식을 제거했다. submission rollback 두 곳은 pre-commit guard 소유라는 경계가 명확하므로 추가 I2 finding이 아니다.
- Monitor expected-state 검사는 registry identity를 올바르게 숨긴다. 남은 문제는 registry pin, handler atomics와 service-control task identity가 서로 다른 synchronization domain에서 순서로만 결합된 점이다.
- MeshNode, Spot, Actor와 STREAM session의 handle·strong child·claim·transfer 책임 경계에서는 위 monitor family 외의 blocker/high/medium POSD·DDD finding을 찾지 못했다.

### Verdict

**NOT CLEAN** — blocker 0, high 1, medium 0.

## 5. I3 — 정리 완결성

### Finding

없음.

### Evidence

- `core/src`의 `.cpp` 174개와 `core/CMakeLists.txt`의 literal source 174개가 일치하며 누락 source가 없다.
- unit/integration CMake는 `zlink_warn_unregistered_tests()`를 유지하고 scheduler regression target과 public surface contract target을 등록한다(`tests/unittest/CMakeLists.txt:4-39`, `:77`; `tests/CMakeLists.txt:903-923`).
- 새 `detach_pending_operation_locked()` 선언은 terminal 6곳에서 사용되며 dead declaration이 아니다. scope의 removed identifier, public header, build target와 compatibility 잔재 정적 검색에서 blocker/high/medium 정리 finding을 찾지 못했다.
- raw monitor immediate-task self-close 회귀가 없는 점은 I1/I2의 동일 root-cause 수정 제안에 포함했고 별도 I3 ID로 분할하지 않았다.
- coordinator manifest §2의 일반 build 오류 0과 CTest 85/85만 실행 증거로 사용했다.

### Verdict

**CLEAN** — blocker 0, high 0, medium 0.

## 6. Low finding 목록

없음.

## 7. Known risk 4건 판정

1. **TSAN auto-HWM lock-order — 추적 유지, 새 확정 static finding 없음.** context recalc는 `_slot_sync`를 보유한 채 socket plan을 준비하고(`ctx_auto_hwm_recalc.cpp:80-116`), plan 준비는 socket monitor sync를 취한다(`socket_base.cpp:214-238`). 반대 순서의 확정 경로는 찾지 못했다. sanitizer는 이번 리뷰에서 실행하지 않았다.
2. **raw command mailbox ypipe — source상 직렬화 확인, sanitizer 전 추적 유지.** `_cpipe.write/flush`, read와 `check_read`는 `_sync` 아래 있다(`mailbox.cpp:39-57`, `:64-98`, `:138-167`). 새 정적 반례는 없다.
3. **raw socket teardown 관찰 — 추적 유지, 새 확정 static finding 없음.** public API inflight/closing과 callback deferred-close gate는 `socket_lifecycle_runtime.cpp:20-100`, async mailbox quiesce와 mailbox refcount는 `:210-315`, `:351-373`에 있다. Context는 reaper 완료와 빈 socket registry를 확인한다(`ctx.cpp:147-170`). 동적 검증은 실행하지 않았다.
4. **`ctx_term` linger — 계약 일치, finding 아님.** spec은 모든 socket이 닫힐 때까지 term이 block될 수 있다고 명시하고(`01-context.ko.md:123-144`), 구현은 shutdown 뒤 reaper 완료와 빈 registry를 기다린다(`ctx.cpp:147-170`).

CORE REVIEW NOT CLEAN
