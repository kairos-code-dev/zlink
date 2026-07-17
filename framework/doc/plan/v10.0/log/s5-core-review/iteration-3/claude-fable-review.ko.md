# S5 Core 구현 리뷰 — R2 (Claude Fable), iteration 3 (최종 전체 pass, P4)

리뷰어: Claude Fable (general-purpose agent). 수정 권한 없음(read-only).
Codex의 iteration-3 결과 파일은 열람하지 않음. iteration-2까지의 finding
ledger·manifest(공유 입력)만 참조.

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Claude Fable (general-purpose agent) |
| Acceptance commit | `25617130eeeb1dc464aec6eca1a8378888aee42a` |
| 시작 시각 | 2026-07-17 21:15 +0900 |
| 종료 시각 | 2026-07-17 22:05 +0900 |
| Scope hash (시작) | `1ca3763f80f568890e2e3e888b4b1dd93e2d15dab9deb9125c12b316fa1f6598` (631 files) — manifest §2와 일치 |
| Scope hash (종료) | `1ca3763f…` / 631 files — **일치** (core/ 무변경) |

| 실행 명령 | 결과 |
|---|---|
| scope hash 재계산 (iteration-1 manifest §2 명령) | `1ca3763f…` / 631 files — manifest 값과 일치 |
| `cmake --build core/build -j && ctest --test-dir core/build -j` | **100% tests passed, 0 failed out of 85** (125.7s) — manifest 기대치와 일치 |
| ASAN mesh 5 바이너리 재빌드 후 실행 (`lifecycle`(8) `node_basic`(8) `monitor_matrix`(6) `peer_admission`(11) `stress`(3)) | 전부 OK, ASAN 오류 0 |
| `setarch -R ./core/build-tsan/bin/test_mesh_lifecycle_contracts` (재빌드 후) | 8/8 OK. 경고 10건 전부 기존 2계열로 분류(§5), mesh 신규 코드 기인 race 0 |
| `git diff a01b537f8..25617130e` (30 files, +983/−66) | 전량 검토 |
| **N1 재현 하네스 재실행** (scratchpad `timer_destroy_race.c`, 새 라이브러리로 재컴파일, 5초 watchdog, 3회) | **3/3 destroy 정상 반환 — 데드락 소멸 실증** |
| removed-identifier 스캔 (`zlink_timer_cleanup_spot`/`owner_spot` 필드/`valid_utf8_public`) | scope 내 hit 0 (`owner_spot` enum 값은 별개 식별자로 정당) |
| `nm -D libzlink.so` 심볼 표면 | `zlink_timer_new_for_spot_node`/`release_spot_node_scheduler` 미노출(내부 C++ 링키지), 공개 표면 불변(contract gate PASS 동반) |
| release workflow sha256 gate 문법 검증 (heredoc-in-if 패턴 로컬 재현) | 정상 동작(SYNTAX_OK), 빈/짧은 digest 시 실패 경로 확인 |
| `git ls-files core/packaging`에서 pycache | untrack 확인, `.gitignore` 등재 확인 |

## 2. Iteration-2 finding 11건 해소 판정

| ID | 심각도 | 판정 | 근거 |
|---|---|---|---|
| F-I1-01(재) (NODROP commit 중 peer 소실) | high | **해소** | commit 단계 실패를 nodrop에서는 `unreachable_out_`으로 분리 집계하고 dropped에서 제외(`mesh_wire.cpp:339-357`), admitted==0 BACKPRESSURED 분기 제거. snapshot에서 unreachable 차감(`mesh_messaging_api.cpp:903,916`) — NODROP 성공 시 dropped==0 계약 성립(admitted == snapshot−unreachable). unreachable ≤ `remote_targets.size()` == snapshot이라 underflow 불가(`:776`). 비-nodrop 경로는 기존 dropped 집계 유지, monitor counter도 unreachable 미포함으로 정합 |
| F-I1-03(재) (destroy가 bound session control 미drain) | high | **해소** | drain 루프가 held·completions 소진 후 node lock을 풀고 `session_bindings_pending`(registry+service lock만, `mesh_stream_session_api.cpp:1168-1177`) 검사, deadline까지 대기(`mesh_actor_api.cpp:677-686`). commit 후 `session_bindings_remove_actor`(`:737`, `mesh_stream_session_api.cpp:1180-1202`)로 파괴된 generation을 세션이 계속 지칭하지 못함. 락 순서는 node 미보유 상태에서 registry→service 단방향. 단 **이 수정이 도입한 lock 해제 창에서 iterator 무효화 회귀 1건** → 신규 finding N5 |
| N1 (timer destroy 데드락) | high | **해소(실증)** | destroy가 busy 대기 전에 scheduler lock과 timer lock을 모두 해제(`timer_api.cpp:142-152`), fire 종료부의 busy_refs 감소는 scheduler→timer lock으로 정상 진행(`timer_scheduler_backend.cpp:152-163`). claim 보유 채 destroy 시 `spot_timer_cancel`이 parked turn을 취소(`mesh_api.cpp:329-347`)하고 enter_turn이 매 round registry를 재조회+50ms bounded wait(`:296-326`). **재현 하네스 3/3 데드락 소멸**, 회귀 test `test_timer_destroy_overlapping_fire_completes`(plain overlap + claim-held cancel) 실측 green. UAF 재검: destroy의 delete는 busy_refs==0 관측 후이고, fire는 timer 참조를 timer_lock 해제 이전에만 사용 — 안전 |
| N-I1-01/N3 (monitor emit vs close UAF) | high/low | **해소** | emit이 node mutex 아래에서 monitor 포인터 핀(`monitor_emit_refs`, `mesh_runtime.cpp:704-712`), 핀 이후 조기 return 경로 없음(닫힘/필터는 fall-through 후 ref 해제 `:722-764`). close는 `handler_active` EDEADLK 가드 후 `node->monitor` 해제, emit refs 0까지 node cv 대기 후 delete(`mesh_monitor_api.cpp:148-166`). 대기 중 node mutex는 cv가 해제하므로 emit의 감소 경로와 데드락 없음. handler 실행 중 cross-thread close는 BUSY 반환(spec 07-monitoring §3은 같은-callback 재진입만 규정 — 위반 아님, editorial E6) |
| N-I1-02 (destroy 무효 iterator) | high | **해소** | `spot_present` 선캡처 후 erase 이후 `spot_it` 미사용(`mesh_actor_api.cpp:711-732`), Spot이 끝난 경우 DESTROYED record는 owner 부재로 자연 drop(관찰자 없음 — 주석 근거 타당) |
| N2 (`handle_actor_left` maybe_end 누락) | medium | **해소** | LEFT record admit 후 node lock에서 `maybe_end_spot_locked` 호출(`mesh_wire_ingress.cpp:543-551`). 감소~판정 사이 재참조/선종료 경합은 maybe_end의 현재-상태 판정으로 안전(재참조 시 no-op, 신규 generation은 facade ref 보유) |
| N-I2-01 (Spot timer 전역 head-of-line) | medium | **해소** | per-MeshNode scheduler(`zlink_timer_new_for_spot_node`, `resolve_spot_scheduler` map, `timer_scheduler_backend.cpp:231-241`) — Spot turn 대기가 전역 scheduler·타 node timer를 막지 않음. node destroy가 EBUSY gate(live_timers, `mesh_node_api.cpp:422-432`) 후 `zlink_timer_release_spot_node_scheduler`(`:457`)로 회수. 수명 재검: scheduler state는 map+worker thread의 shared_ptr로 유지, timer의 raw 포인터 접근은 모두 timer_count 감소(=gate 통과 가능 시점) 이전에 종료 — UAF 없음. 설계 대안 선택(per-Spot 대신 per-node)은 manifest 계약대로 coordinator 권한 |
| N-I3-01 (conandata sha256 미고정) | medium | **해소** | release workflow에 sha256 필수 gate(`core-conan-release.yml:73-87`) — 64자 미만/부재 시 실패. bash heredoc-in-if 문법 로컬 검증 통과. digest 기입은 S6-05(tag 직후)로 README에 절차 명시. 잔여 위험: gate가 runner의 PyYAML 존재를 전제(ubuntu 이미지 기본 포함) — 수용 |
| N-I3-02 (pycache/README 잔재) | low | **해소** | pycache untrack+`.gitignore`, README가 10.0.0·kairos-code-dev 경로·sha256 필수를 명시 |
| N-I3-03 (EOF blank) | low | **해소** | `mesh_wire.cpp` 말미 blank 제거 확인 |
| N4 (`valid_utf8_public` 사멸 선언) | low | **해소(잔재 1)** | 선언 제거, 9.x 잔재(`zlink_timer_cleanup_spot`/`owner_spot` 필드) 제거 — 전수 스캔 hit 0. 단 선언 위 주석 한 줄이 대상 없이 잔존 → 신규 finding N6 |

## 3. 전체 재검토 범위 (P4)

mesh wire 4모듈 + `mesh_runtime.{hpp,cpp}` + `core/src/api/mesh/` 8파일 +
`timer_api.cpp`·`timer_scheduler_backend.cpp`·`timer_api_internal.hpp`(이번
delta 핵심, 전문) + socket_base probe 3파일(무변경 확인) +
`core/include`(무변경) + mesh test 5파일 + contract gate(ctest PASS) +
CMake(무변경) + packaging/conan + release workflow + internals 문서 대조.

delta가 만들 수 있는 회귀를 적대적으로 추적한 결과:

- **락 순서 전수 재검**: 신규 순서는 node→reg(enter_turn), timer→reg·
  timer→node(tick_allowed), scheduler→timer(기존). 역방향(reg→node 중첩,
  node→timer, node→scheduler) 취득 지점 없음 — 순환 없음. `spot_timer_cancel`/
  `leave_turn`/`closed`는 reg 해제 후 node 취득(순차). destroy의 cancel·busy
  대기는 락 비보유 상태에서 수행.
- **per-node scheduler 수명**: 생성(lazy)–회수(destroy) 경로, stale generation
  방어(등록 시 generation 불일치면 timer_count 미증가, tick/turn 전부 거부),
  facade_count가 generation 종료를 막아 불일치 자체가 실도달 불가임을 확인.
- **monitor 핀 누락 경로**: 핀 이후 return 없는 fall-through 구조 확인, 핀·
  해제 대칭 1:1.
- **unreachable 회계 경계**: underflow 불가, retry 시 재초기화, 전량 unreachable
  시 OK+snapshot 0(전송 대상 없음 의미론) — NODROP all-or-none과 정합.
- **actor destroy drain 창**: 신규 lock 해제 창에서 iterator 무효화 1건 발견
  (§4 N5).

## 4. 신규 finding

| ID | 심각도 | 축 | Finding | 근거 (file:line) | 수정 범위·검증 방향 |
|---|---|---|---|---|---|
| N5 | **Medium** | I1 | **actor destroy drain 루프의 lock 해제 창에서 `owner_it` 무효화 — freed map node에 대한 UAF read/write.** F-I1-03 수정이 도입한 창(`lock.unlock(); session_bindings_pending(); lock.lock()`)이 열려 있는 동안, ingress 스레드의 원격 destroy(`handle_actor_destroy`→`actor_destroy_local`)가 같은 actor의 owner entry를 erase할 수 있다(`actor_destroy_local`은 draining 검사 없이 id+generation 일치만으로 즉시 erase). 같은 iteration에서 deadline 분기에 들어가면 창 이전에 계산한 `owner_it`을 비교·역참조해 해제된 `std::map` 노드를 읽고 `revoked = true`를 **freed 메모리에 쓴다**. 도달 조건: 세션 pending 잔존(=이 코드가 방금 지원한 시나리오) + deadline 도달(또는 `timeout_ms_==0`이면 항상) + 창 내 원격 destroy 경합 — 창은 좁으나 분산 환경에서 실도달 가능하고, 결과는 heap corruption 계열 | 창: `core/src/api/mesh/mesh_actor_api.cpp:681-683`, stale 사용: `:688-695`(owner_it 계산은 `:664-665`), 경합 eraser: `core/src/api/mesh/mesh_actor_api.cpp:171`(`actor_destroy_local`, draining 무검사), 호출자 `core/src/runtime/services/mesh/mesh_wire_ingress.cpp:454` | 재획득 후 deadline 분기 진입 전 `owner_it` 재조회(1곳). 검증: 원격 destroy와 로컬 destroy(timeout 0, 세션 pending 상태) 경합 test 또는 TSAN/ASAN 하 반복 실행 |
| N6 | Low | I3 | **대상 잃은 주석 잔존**: N4 수정이 `valid_utf8_public` 선언을 지우면서 그 위 설명 주석("UTF-8 validity for public names and topics.")을 남김 — 아무 선언도 설명하지 않는 dead comment | `core/src/api/mesh/mesh_c_internal.hpp:38` | 주석 삭제(1줄) |

## 5. 명시 판정 (known risk)

1. **TSAN 기존 2계열** — TSAN 재실행(lifecycle, 신규 timer case 포함 8/8)
   경고 10건 전수 분류: lock-order-inversion 9건 전부
   `prepare_auto_hwm_socket_plan`/`refresh_auto_hwm_policy`/`ctx_t::create_socket`
   계열(auto-HWM lock-order), data race 1건은 `mailbox_t::recv`의 command
   mailbox ypipe 계열. 두 계열 모두 raw socket 9.x 기계 소속으로 mesh 코드는
   호출자로만 등장 — **수용(추적 유지)**, mesh 신규 race 0. (iteration-2의
   8+2 대비 9+1은 실행별 관측 변동, 계열 동일.)
2. **ctx_term linger** — mesh 신규 계약이 아닌 raw 소켓 기존 특성. 2-process
   test는 종료 순서로 회피, 85/85 재현 green — **수용(기존 특성, 추적 유지)**.

## 6. 축별 판정

### I1 — 계약 구현 일치
- Finding: N5 (Medium).
- iteration-2 finding 11건 중 11건 해소(단 F-I1-03 수정이 N5 회귀를 동반).
  N1은 재현 하네스 3/3으로 데드락 소멸 실증.
- 그 외 재검토: timer destroy/fire의 UAF·순환 대기 전수 추적 clean, per-node
  scheduler 수명 clean, monitor 핀 대칭 clean, unreachable 회계 경계 clean,
  `handle_actor_left` 경합 안전.
- 판정: **NOT CLEAN**

### I2 — POSD·DDD
- 없음. per-MeshNode scheduler는 "두 scheduler family 분리 유지" 기존 결정과
  정합하고 회수 시점(모든 timer가 destroy를 EBUSY로 gate한 후)이 주석으로
  근거를 갖는다. session binding helper 2개는 순회 방식이 달라(콜백 vs erase)
  분리가 정당하며 내부 header에 계약 주석(node mutex 비보유) 명시.
  `services-internals` 양어 갱신(monitor 핀, Spot timer scheduler 행)이 구현과
  정확히 일치. 경계 훼손·패스스루 없음.
- 판정: **CLEAN**

### I3 — 정리 완결성
- N6 (Low): 대상 잃은 주석 1건.
- 그 외: 9.x 잔재(`zlink_timer_cleanup_spot`/`owner_spot` 필드) 제거 완결,
  removed-identifier 스캔 clean, pycache untrack·ignore 완결, README 갱신 정확,
  내부 심볼 미노출, orphan build target 없음.
- 판정: **NOT CLEAN** (low 1건, 기능 영향 없음)

## 7. 결론

- iteration-2 finding: 11건 전부 해소 판정(핵심 N1은 하네스 재실행으로 실증).
- 신규 finding: 2건 (Medium 1 — F-I1-03 수정이 도입한 lock 해제 창의 iterator
  UAF, Low 1 — dead comment).
- 축 판정: I1 = NOT CLEAN, I2 = CLEAN, I3 = NOT CLEAN.
- blocker·high 없음이나 세 축 CLEAN 미충족.

## 8. Editorial note (finding 아님)

- E5: actor destroy drain이 세션 pending 소진 통지를 받지 못한다 — 세션
  서비스는 `service->cv`만 notify하고 destroy는 `node->cv`를 deadline 잔여
  전체시간으로 대기(`mesh_actor_api.cpp:697`)하므로, 세션 control이 즉시
  드레인돼도 destroy는 deadline까지 잠든다(정확성 영향 없음, 지연만).
  bounded poll(예: 50ms) 또는 드레인 시 node cv notify 여지.
- E6: handler 실행 중 cross-thread `monitor_close`는 `ZLINK_CLOSE_BUSY`+
  `EDEADLK`를 반환한다. spec은 같은-callback 재진입만 규정하므로 위반은
  아니나, cross-thread 상황에 EDEADLK errno는 명칭 부정합(EBUSY 여지).
- E7: `emit_monitor_event`가 event마다 `node->cv.notify_all()`을 수행 —
  monitor 부착 시 claim/destroy 대기자들의 스퓨리어스 웨이크업이 event 빈도에
  비례(핀 해제 전용 cv 분리 여지).
- E8: 자기 timer handler 안에서 `zlink_timer_destroy` 호출은 self-deadlock
  (busy_refs에 자기 fire 포함). spec 08-utilities는 "다른 스레드 사용 중 금지"
  만 규정 — 계약 밖(기존 형상). 문서화 또는 EDEADLK 감지 여지.
- iteration-2 E1~E4는 형상 불변으로 유지(E1의 local/remote 소멸 처리 비대칭은
  unreachable 회계 도입으로 더 가시화됨 — remote는 snapshot 제외, local은
  ETIMEDOUT).

NOT CLEAN
