# S5 Core 구현 리뷰 — iteration 15 — R2 (Claude Sonnet)

## 1. Scope 확인

- 검토 checkout: `/tmp/claude-1000/zlink-s5-it10-sonnet`, detached HEAD `7b580a52062af34fb47dc5e8cae349b936d925ee` (`core(control): resolve S5 iteration-14 finding`). 시작·종료 시점 모두 `git status --short` 출력 없음(clean), `git diff --stat` 출력 없음 — 검토 중 어떤 파일도 수정하지 않았다.
- 파일 수: 시작·종료 모두 **631** (`git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md`) — prompt 명시값과 일치. 시작·종료 두 시점의 파일 목록(`LC_ALL=C sort`)을 `diff`로 대조해 완전히 동일함을 확인했다.
- Aggregate SHA-256: 시작·종료 모두 `cf26306098cb19579b2759b3c82df89809ece3ddb5d4aebac23372da02eefdc5` — prompt 명시값과 정확히 일치. 재현 절차: `git ls-files ...`(`LC_ALL=C sort` 고정)로 정렬한 파일 목록을 그 순서 그대로 각각 `sha256sum`한 뒤, 그 출력 라인들을 (값 기준으로 다시 정렬하지 않고) 그대로 이어 붙인 파일 전체를 다시 `sha256sum`. 개별 해시 라인을 값 기준으로 재정렬하면 다른 값이 나옴을 직접 실험으로 확인했다(파일명 순서 유지가 핵심).

## 2. iteration 14 finding 1건 — 해소 판정

prompt의 우선 검증 대상은 S5-14-01(scheduler allocation 봉인) 1건이다. 수정 commit `7b580a520`이 건드린 파일은 정확히 3개(`git diff --stat 26a4cbb81..7b580a520` 확인): `core/src/api/core/context_api.cpp`, `core/src/runtime/services/control/service_control_runtime.{cpp,hpp}`. 5개 세부 항목을 라인 단위로 대조했다.

| 세부 항목 | 판정 | 근거 |
|---|---|---|
| ① `cached_node` 무할당 rekey·재삽입 | **해소** | `service_control_runtime.hpp:57` `task_entry_t::cached_node`(`multimap<uint64_t,uint64_t>::node_type`) 도입. `deschedule_task_locked`(`.cpp:192-200`)는 `_schedule.extract(task_->schedule_it)`로 node를 `cached_node`에 보존(`extract`는 표준상 할당 없음·no-throw). `schedule_task_locked`(`.cpp:174-190`)는 `cached_node`가 비어있지 않으면 `key()`를 재대입 후 `_schedule.insert(std::move(task_->cached_node))`로 재삽입 — node handle 삽입은 컨테이너 할당을 하지 않는다(`std::less<uint64_t>`는 noexcept). `loop()`의 due-task 처리(`.cpp:233`)도 `_schedule.extract(next)`로 동일 패턴을 직접 사용한다. wakeup(`wakeup_task`, `.cpp:138-154`)과 periodic reschedule(`loop`, `.cpp:236-237`) 양쪽 모두 이 무할당 경로만 지난다. |
| ② `add_periodic_task` sealed transaction | **해소** | `.cpp:91-109`: `_tasks.emplace`부터 `task.schedule_it = _schedule.insert(...)`까지 하나의 `try` 블록. `catch (const std::bad_alloc &)`에서 `task_it != _tasks.end ()`이면 `_tasks.erase(task_it)`로 완전 rollback, `errno = ENOMEM`, `return 0`. 부분 성공(스케줄 슬롯 없이 `_tasks`에만 남는 반쪽 entry) 경로 없음. |
| ③ pass당 단일 due call + `_active_task_id` 계약 | **해소** | `loop()`(`.cpp:202-281`)이 `std::vector<due_call_t> due_calls`(이전 버전)를 제거하고 스택 지역 변수 `due_call_t call` 하나만 사용, 매 pass 하나의 due task만 발견해 `break`(`.cpp:242`)한다. `_active_task_id`는 due task 발견과 같은 잠금 구간에서 설정(`.cpp:264`)된 뒤 `call.fn` 호출 전 unlock, 콜백 종료 후 재잠금해 자신이 설정한 값일 때만 0으로 되돌리고 `broadcast`(`.cpp:269-275`). `remove_task`의 자기-스레드 즉시삭제 분기(`.cpp:119-127`, `_active_task_id == task_id_` && 현재 스레드가 서비스 스레드)와 타 스레드의 `while (_active_task_id == task_id_) _cv.wait`(`.cpp:133-134`) 대기 계약은 이전 버전과 동일하게 유지됨을 확인했다. 이전 버전은 "발견 시점 존재 확인" 뒤 "호출 직전 재확인"의 2단계 잠금이었으나(잠금 해제 구간이 각 due call 사이에 있었음), 새 버전은 발견부터 `_active_task_id` 설정까지 단일 잠금 구간이라 그 사이 concurrent 제거가 끼어들 틈이 없어 구버전의 존재 재확인 로직 제거가 안전하다 — `git diff`로 구버전 대조해 직접 확인. |
| ④ `run()` 최후 방어 seal + C ABI barrier | **해소** | `.cpp:161-172` `run()`이 `self->loop ()`를 `try`로 감싸 `catch (const std::bad_alloc &)`에서 조용히 반환(워커 스레드만 종료, 프로세스 `std::terminate` 없음). `context_api.cpp:149-165` `zlink_ctx_set_ext`와 `context_api.cpp:192-207` `zlink_ctx_auto_hwm_recalculate` 둘 다 `try`/`catch (const std::bad_alloc &)`로 감싸 `errno = ENOMEM` 후 오류 반환(`-1` / `ZLINK_CONFIG_INTERNAL_ERROR`)한다. 이 seal이 실질적인지 확인하기 위해 호출 그래프를 추적했다: `zlink_ctx_set_ext` → `ctx_t::set` → (`ctx_options.cpp:106`) `schedule_auto_hwm_recalculate` → 디바운스 없으면(`ctx_auto_hwm_recalc.cpp:63-64`) `auto_hwm_recalculate_now()` 직접 호출 → `std::vector` 두 개(`sockets`, `plans`, `.cpp:86,94-95`) 할당 — 이 경로는 `add_periodic_task` 내부 catch로 막히지 않고 `zlink_ctx_set_ext`까지 예외가 전파될 수 있는 실제 경로이므로 seal이 사문화된 방어코드가 아님을 확인했다. `zlink_ctx_auto_hwm_recalculate`도 동일 함수를 직접 호출하므로 마찬가지. |
| ⑤ node handle 소유권·해제 | **해소** | `deschedule_task_locked`의 `extract`는 반환된 node의 소유권을 `cached_node`로 이전(반복자/소유권 규칙 위반 없음 — 표준 `extract(const_iterator)`가 보장하는 동작). `schedule_task_locked`의 `insert(std::move(...))` 성공 후 `cached_node`는 move-from 빈 상태가 되고, 다음 `deschedule_task_locked`가 다시 채운다 — add 시점 1회 할당된 단일 node가 task 수명 동안 `_schedule`과 `cached_node` 사이를 왕복하며 재할당되지 않는다. `remove_task`(`.cpp:114-136`)는 `_tasks.erase(it)`로 `task_entry_t`를 파괴하는데, 이때 `cached_node`가 비어있지 않은 상태로 소멸하면(`node_type` 소멸자가 보유 노드를 해제) 누수 없이 반환된다 — scheduled 상태(entry가 `_schedule`에 있고 `cached_node`는 비어있음)에서 제거될 경우 `deschedule_task_locked`가 먼저 extract해 `cached_node`를 채운 뒤 erase하므로 두 경우 모두 정확히 해제됨을 확인했다. |

`schedule_task_locked`의 `cached_node.empty ()` false 분기(`.cpp:185-188`, `make_pair` 할당 경로)는 위 호출 그래프상 add 이후 도달 불가능한 방어적 fallback으로 보인다 — add가 `cached_node`를 항상 비어있는 상태로 시작하지만 add 자체는 `schedule_task_locked`를 호출하지 않고 직접 삽입하며, 이후 모든 `schedule_task_locked` 호출은 직전 `deschedule_task_locked`(또는 `loop`의 직접 `extract`)로 `cached_node`가 채워진 뒤에만 일어난다. blocker/high/medium 아님(low, §4 기록) — 실제 실행되지 않는 방어 코드이며 봉인 보장을 해치지 않는다.

**S5-14-01 — 해소.** 새로 여는 항목 없음, 반례 없음.

## 3. I1 / I2 / I3 — 전체 scope 재검토

`git log 26a4cbb81..7b580a520 --oneline`은 커밋 1개(`7b580a520`)뿐이고, `git diff --stat 26a4cbb81..7b580a520`은 scope 내 소스 변경을 §2의 3개 파일로 한정한다(그 외 변경은 scope 밖 iteration-14 로그/문서뿐). `git log -1 --oneline -- <파일>`로 known-risk 파일(`ctx_auto_hwm_recalc.cpp`, `socket_base.cpp`, `mailbox.{cpp,hpp}`, `ypipe*.hpp`, `own.cpp`, `session_base.{cpp,hpp}`)과 packaging/spec 파일을 각각 대조해 전부 iteration-14 기준선보다 이전 커밋(S5 캠페인 이전)에 머물러 있음을 확인했다 — 이번 회차 diff 범위 밖.

### I1 — 계약 구현 일치

Finding: 없음.

Evidence: §2의 diff 3파일을 라인 단위로 재검증했다. `core/doc/spec/core/01-context.md`의 `zlink_ctx_set`/`zlink_ctx_set_data`(§"Errors": `EINVAL`, `EFAULT`)와 `zlink_ctx_auto_hwm_recalculate`(§"Errors": `EFAULT`)는 함수별 Errors 목록에 `ENOMEM`을 별도로 나열하지 않는다. 이것이 이번 수정이 만든 신규 계약 이탈인지 확인하기 위해 `04-errno-map.md` §7(Configuration result)을 대조했다: `ZLINK_CONFIG_INTERNAL_ERROR` 행이 이미 "preserved errno / Internal failure without a finer public category"로 전역 정의되어 있고, 이는 개별 함수 문서가 열거하지 않는 나머지 내부 오류(ENOMEM 포함)를 포괄하는 기존 컨벤션이다 — 각 함수 스펙이 "zlink_errno()는 진단용 상세 errno를 보존한다"는 문구를 이미 갖고 있다(`01-context.md:195,224,280-281`). 같은 `ZLINK_CONFIG_INTERNAL_ERROR` 하드코딩 패턴은 `mesh_dispatch_api.cpp:726`, `mesh_node_api.cpp:1142,1182`에도 이미 존재해 이번 수정이 만든 특이 패턴이 아님을 확인했다. 따라서 신규 계약 이탈이 아니다. `service_control_runtime_t::add_periodic_task`의 반환 계약(성공 시 0 아닌 ID, 실패 시 0 + `ENOMEM`)도 유지된다(공개 API는 아니고 monitor/auto-HWM 내부 소비자 계약).

Verdict: **CLEAN**

### I2 — POSD·DDD

Finding: 없음(blocker/high/medium).

Evidence: `cached_node` 도입은 스케줄 슬롯 수명 관리를 `service_control_runtime_t` 자신의 책임 범위 안에서만 완결시킨다 — 호출자(`ctx_auto_hwm_recalc.cpp`, `socket_base_monitor.cpp`, `monitor_api.cpp`)는 여전히 "0 반환 시 아무 것도 등록되지 않았다"는 단순 계약만 관찰하며, node 소유권 왕복이라는 내부 최적화가 어떤 외부 인터페이스도 넓히지 않았다. C ABI seal(`context_api.cpp`)의 `catch (const std::bad_alloc &)` 패턴은 `mesh_actor_api.cpp`/`mesh_dispatch_api.cpp`/`mesh_messaging_api.cpp`/`mesh_node_api.cpp`/`mesh_stream_session_api.cpp`/`monitor_api.cpp`에 이미 존재하는 확립된 경계-봉인 관용구이며(`grep -rl "catch (const std::bad_alloc" core/src/api` 6개 파일 확인), 이번 두 함수 추가는 그 관용구를 빠져있던 두 지점에 정합적으로 채운 것이다. 새 락이나 공유 상태 추가 없음, 책임 경계 이동 없음.

Verdict: **CLEAN**

### I3 — 정리 완결성

Finding: 없음.

Evidence: diff 3파일에 TODO/FIXME/XXX/HACK 없음. scope 전체(631개 파일, `core/doc/internals` 제외 대상이지만 마커 검색 자체는 전체에 대해 실행)에서 `TODO|FIXME|XXX|HACK` 재검색 결과 0건(`mktemp`/`mkstemp` 템플릿 문자열도 이번 회차엔 매칭되지 않음). `core/tests/CMakeLists.txt`의 `zlink_warn_unregistered_tests` 미등록 테스트 자동 경고 메커니즘은 diff 밖이라 미변경. 이번 diff는 기존 파일 내부 로직 재구성뿐이라 신규/고아 build target, 신규 테스트 파일, 호환 잔재가 없다. `git status`/`git diff`가 scope 전체에 대해 clean함을 시작·종료 두 시점에 확인(§1).

Verdict: **CLEAN**

## 4. low finding 목록

- [I3][low] `core/src/runtime/services/control/service_control_runtime.cpp:185-188` — `schedule_task_locked`의 `cached_node.empty ()` false 분기(할당 가능한 `make_pair` 삽입 fallback)는 현재 호출 그래프상 add 이후 도달 불가능하다(§2 세부 항목 정리 참고). 실행되지 않는 방어 코드라 봉인 보장을 해치지 않으며 CLEAN 판정을 막지 않는다. 수정 제안: 유지해도 무방하나, 만약 정리하려면 `zlink_assert (!task_->cached_node.empty ())`로 불변식을 명시하고 fallback 분기를 제거하는 편이 "무할당 보장"이라는 주석의 의도를 코드로도 강제할 수 있다.

## 5. Known risk 4건 판정

diff가 scope 내 3개 파일(§2, §3)에만 있고 아래 4개 영역의 소스는 이번 커밋에서 전혀 변경되지 않았음을 `git log -1 --oneline -- <파일>`로 확인했다. 각 risk를 처음부터 독립적으로 소스 추적해 재확인했다.

1. **TSAN auto-HWM lock-order** — `ctx_auto_hwm_recalc.cpp:80` `auto_hwm_recalculate_now ()`가 `_slot_sync`(ctx 레벨, `runtime_lock`)를 쥔 채 `socket->prepare_auto_hwm_socket_plan ()`(`.cpp:103`)을 호출하고, 그 함수(`socket_base.cpp:224`)가 내부에서 `monitor_runtime ().sync`(소켓 레벨)를 추가로 취한다 — `_slot_sync → monitor sync` 중첩을 직접 재확인했다. 역방향(소켓 레벨 monitor sync를 쥔 채 `_slot_sync`를 기다리는) 경로를 찾기 위해 `monitor_runtime ().sync`를 취하는 모든 지점(`socket_base_lifecycle.cpp:126,181`, `socket_base_monitor.cpp:40,87,103,120,281,336,391`, `socket_base_api.cpp:78,336`, `socket_base.cpp:225,388`, 총 13곳)을 grep했고, `_slot_sync`는 `core/src/runtime/core/`의 `ctx.cpp`/`ctx_bootstrap.cpp`/`ctx_auto_hwm_recalc.cpp`/`ctx_termination.cpp` 밖에서는 획득되지 않음을 확인했다 — `core/src/runtime/sockets/` 어디에도 `_slot_sync` 직접 획득이 없다. 직접 호출 그래프상 역방향 경로는 없다. 다만 가상 호출·콜백 경유 간접 경로까지는 정적 검토만으로 완전히 배제할 수 없으므로, 이전 iteration들과 동일하게 "정적 검토로 완전 배제 불가, 신규 반례 없음, 추적 유지"로 판정한다.
2. **raw command mailbox ypipe** — `mailbox.{cpp,hpp}`, `ypipe.hpp`, `ypipe_base.hpp`, `ypipe_conflate.hpp`의 마지막 변경 커밋은 `b64eb3a24`(spot actor transfer 안정화, S5 캠페인 이전)이다. 단일 writer/단일 reader 락-프리 큐 설계이며 이번 S5 control-runtime 수정과 무관. 추적 유지, 신규 반례 없음.
3. **raw socket teardown 관찰** — `core/src/runtime/sockets/` 전체가 이번 diff에 포함되지 않는다(§3). 추적 유지, 신규 반례 없음.
4. **`ctx_term` linger** — `own.cpp`의 마지막 변경은 `d31508b81`(clang-format width 스타일 변경뿐), `session_base.{cpp,hpp}`도 동일 커밋 — 둘 다 이번 diff와 무관. 계약 일치, finding 아님(이전 판정 유지).

## 6. Package metadata 정적 대조

- `core/CMakeLists.txt`: `project(zlink VERSION 10.0.0 ...)`(11행), `SOVERSION "10"`(1370행).
- `core/packaging/debian/control`: `libzlink10`/`libzlink10-dev`/`libzlink10-dbg`(SOVERSION 10과 일치).
- `core/packaging/debian/changelog`: `zlink (10.0.0-0.1) UNRELEASED`.
- `core/packaging/redhat/zlink.spec`: `Version: 10.0.0`(13행).
- `core/packaging/nuget/package.nuspec`: `<version>10.0.0</version>`(10행).
- `CHANGELOG.md`: 이번 diff에 포함되지 않음(`## 10.0.0 (release candidate)` 헤더 유지).
- 위 모든 파일이 이번 커밋 diff 밖(§3)이라 이전 판정에서 변경 없음. 전부 상호 일치, 불일치 없음.

## 7. 실행 증거 참조

coordinator 실행 결과(재실행하지 않고 manifest 인용): `cmake --build core/build -j16` 오류 0, `ctest --test-dir core/build -j4` 100% 통과(85/85). ([iteration-15 manifest](../manifest.ko.md) §2)

## 8. 최종 판정

CORE REVIEW CLEAN
