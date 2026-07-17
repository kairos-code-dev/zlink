# S5 Core 구현 리뷰 — R2 (Claude Fable), iteration 2 (최종 전체 pass, P4)

리뷰어: Claude Fable (general-purpose agent). 수정 권한 없음(read-only).
Codex의 iteration-2 결과 파일은 열람하지 않음. iteration-1의 finding ledger와
codex-review(공유 입력)만 참조.

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Claude Fable (general-purpose agent) |
| Acceptance commit | `a01b537f8ce36d24db44d611b9d9dce4e263306e` |
| 시작 시각 | 2026-07-17 20:07 +0900 |
| 종료 시각 | 2026-07-17 20:53 +0900 |
| Scope hash (시작) | `fa95152dcc7aecf633a79405f35f3613a5cc833824052bde22775aa71ae370c6` (630 files) — manifest §2와 일치 |
| Scope hash (종료) | `fa95152d…` / 630 files — **일치** (core/ 무변경) |

| 실행 명령 | 결과 |
|---|---|
| scope hash 재계산 (iteration-1 manifest §2 명령) | `fa95152d…` / 630 files — manifest 값과 일치 |
| `cmake --build core/build -j20 && ctest --test-dir core/build -j20` | **100% tests passed, 0 failed out of 85** (127.3s) — manifest 기대치와 일치 |
| `./core/build-asan/bin/test_mesh_lifecycle_contracts`, `test_mesh_node_basic` (재빌드 후) | 7/7 OK, 8/8 OK, leak 0 |
| `setarch -R ./core/build-tsan/bin/test_mesh_lifecycle_contracts` (재빌드 후) | 7/7 OK. 경고 10건 전부 기존 2계열로 분류(§5 참조), mesh 신규 코드 기인 race 0 |
| `git diff 8206fd44d..a01b537f8` (32 files, +3436/−1722) | 전량 검토 |
| removed-identifier word-boundary 스캔 (iteration-1 manifest §5) | scope 내 hit 0 (removal manifest JSON + 정당한 absence assert 1건 제외) |
| 신규 결함 재현 하네스 (scratchpad, core 밖) | `zlink_timer_destroy`가 실행 중 handler tick과 겹치면 **영구 데드락 실증**(5초 watchdog으로 확정, §4 N1) |

## 2. Iteration-1 finding 12건 해소 판정

| ID | 심각도 | 판정 | 근거 |
|---|---|---|---|
| F-I1-01 (NODROP 부분 전달) | high | **해소** | `socket_base`에 비소비 probe `routed_target_writable()` 신설(`socket_base.hpp:139`, `socket_base_msg.cpp:92`, `socket_base_routing.cpp:216` — check_write는 첫 frame만 게이트, multipart 잔여 frame은 항상 수용되므로 1 slot=1 message가 성립). `wire_publish_remote_locked`(`mesh_wire.cpp:279-356`)가 `wire_send_mutex` 아래 전 target reserve→commit 2단계, backpressure 시 zero-commit. `publish_common`(`mesh_messaging_api.cpp:797-859`)이 local mailbox 예약과 결합해 SNDTIMEO까지 전체 reserve 재시도. 락 순서는 node mutex→wire_send_mutex 단방향(역방향 취득 없음, 전수 확인) |
| F-I1-02 (Spot 수명·timer) | high | **해소(부분)** | `maybe_end_spot_locked`(`mesh_runtime.cpp:672`, facade/timer/actor/claim/fence 전부 0일 때만 종료, entry Spot 제외), spot timer immortal registry+seam(`mesh_api.cpp:222-386`), `scheduler_fire_timer`의 enter/leave_turn·tick_allowed hook, `drain_ready`의 timer-turn 배제(`mesh_dispatch_api.cpp:348`). 수명·generation·stale tick 계약은 test로 검증. 단 **감소 경로 7곳 중 1곳(`handle_actor_left`)이 종료 판정을 빠뜨림** → 신규 finding N2 |
| F-I1-03 (actor destroy drain) | high | **해소** | `zlink_mesh_node_actor_destroy`가 draining 마크로 신규 admission 차단(`find_actor_locked`의 ESHUTDOWN, fence는 EAGAIN 경로 보존) 후 held claim·outstanding completion을 deadline까지 대기, 만료 시 revoke 후 제거(`mesh_actor_api.cpp:640-691`). `test_actor_destroy_waits_for_held_claim`이 deadline 대기와 revoke-후-release 안전을 실측 |
| F-I1-04 (shutdown ESHUTDOWN detach) | high | **해소** | deadline 만료 시 모든 outstanding operation(무기한 포함)을 detach해 정확히 한 번의 `TERMINATED`/`ESHUTDOWN` completion 생성(`mesh_node_api.cpp:372-394`), 재진입은 `shutdown_active`로만 `EDEADLK`, TIMED_OUT 뒤 순차 재-shutdown 합법. `test_shutdown_detaches_timeoutless_operations` 검증 |
| F-I1-05 (MIXED 도달 불가) | medium | **해소** | descriptor에 advertised endpoint 추가(`mesh_wire_codec.cpp:61`, `mesh_wire_internal.hpp:97`), inbound 관측 peer가 endpoint 기록(`mesh_wire_admission.cpp:216-217`), manual intent가 endpoint로 병합(`mesh_node_api.cpp:495-505`), source 하나 제거 시 잔여 source 유지. 2-process test `test_inbound_peer_merges_manual_intent_to_mixed`로 실도달 증명 |
| F-I1-06 (DRAINING entry 미유지) | medium | **해소** | 상위 generation 교체가 이전 admitted entry를 `DRAINING`으로 남기고 successor를 새 entry로 시작(`mesh_wire_admission.cpp:186-198`; push_back 후 포인터 재취득으로 재할당 안전). transport 소실 시 DRAINING→CLOSED(`handle_peer_down`), 명시적 disconnect도 DRAINING 수용(`mesh_node_api.cpp:590`), `draining_peer_count` 집계 실동작(`:757-762`). iteration-1의 vestigial 지적 동시 해소 |
| F-I1-07 (query partial output) | medium | **해소** | `zlink_mesh_node_peers`·`zlink_stream_session_bindings` 모두 전 element 선검증 후 출력하는 2-pass(`mesh_node_api.cpp:807-812`, `mesh_stream_session_api.cpp:793-798`). `peer_channels`는 versioned element 출력이 없어 해당 failure mode 자체가 없음(수정 불필요 판단 타당). `test_peers_query_output_is_invariant_on_invalid_element` 검증 |
| F-I1-08 (strict UTF-8 미공유) | medium | **해소** | 단일 strict validator `valid_utf8`(`mesh_runtime.cpp:53` — RFC 3629 DFA: C2/E0-A0/F0-90 하한, ED-9F·F4-8F 상한으로 overlong·surrogate·U+10FFFF 초과 전부 거부)로 통일. `check_name`(모든 이름), topic(`mesh_messaging_api.cpp:745`), filter(`:1025`), metadata key/value(`mesh_runtime.cpp:518,538`) 공유. 구 `valid_utf8_public` 정의 삭제(선언 잔재는 N4). test가 3계열 거부 + 한글 다바이트 통과를 검증 |
| F-I2-01 (mesh_wire 결합) | medium | **해소** | 4모듈 분해: `mesh_wire_codec`(237줄, wire 포맷), `mesh_wire_admission`(315줄, admission 상태 기계), `mesh_wire_ingress`(1074줄, 서비스 라우팅+ingress 스레드), `mesh_wire`(681줄, transport 수명+발신). 공유 선언은 내부 전용 `mesh_wire_internal.hpp`. 각 모듈이 한 결정을 소유하고 공개 표면 불변(`core/include` 무변경, contract_public_surface PASS). internals 문서 양어 동기 |
| F1 (claim serial 충돌) | high | **해소** | serial을 프로세스 전역 원자 카운터로 발급(`mesh_dispatch_api.cpp:94-97`), side table은 immortal 단일 인스턴스, per-node `next_claim_serial` 멤버 제거(사멸 멤버 잔재 없음, 전수 grep). claim 부여 지점은 `drain_ready` 한 곳뿐임을 확인. `test_claims_are_process_unique_across_nodes` 검증 |
| F2 (handler_active 사문) | medium | **해소** | `emit_monitor_event`가 handler 호출 전후로 `handler_active` 증감(`mesh_runtime.cpp:747-757`), `monitor_handler`·`monitor_close`의 EDEADLK 가드 실동작. spec §3의 같은-callback 재진입 계약은 `test_monitor_handler_reentry_is_deadlock_error`로 검증. 단 cross-thread close에 대해서는 가드가 TOCTOU(신규 finding N3, 기존 형상) |
| F3 (무의미 삼항) | low | **해소** | `mesh_messaging_api.cpp:406-408` 내부 삼항 제거, 단일 조건식으로 정리 |

## 3. 전체 재검토 범위 (P4)

mesh 4 wire 모듈 전체 + `mesh_runtime.{hpp,cpp}`, `core/src/api/mesh/` 8파일
전체, `timer_api.cpp`·`timer_scheduler_backend.cpp`, socket_base 신규 probe 3파일,
`core/include/zlink/` 폐쇄(무변경 확인), `test_mesh_*` 5파일,
`check_public_surface.py`(무변경, ctest gate PASS), CMake 2파일(3 신규 TU + 1
신규 test 등록, orphan 없음), `services-internals.{ko.md,md}`(구현과 축조 대조
— 4모듈 표, 전역 serial, shutdown detach, DRAINING·MIXED 서술 모두 소스와
일치). 특히 수정이 만든 회귀(락 순서, race, 수명, dead code)를 적대적으로
탐색했고 그 결과가 §4다.

## 4. 신규 finding

| ID | 심각도 | 축 | Finding | 근거 (file:line) | 수정 방향 |
|---|---|---|---|---|---|
| N1 | **High** | I1 | **`zlink_timer_destroy`가 진행 중인 fire와 겹치면 영구 데드락 + 전역 timer scheduler 정지.** destroy는 scheduler mutex와 timer mutex를 잡고 `scheduler_busy_refs`가 0이 될 때까지 `timer->cv.wait(timer_lock)`으로 대기하는데, 이 대기는 **scheduler mutex를 쥔 채** 이루어진다. 한편 `scheduler_fire_timer`의 종료 구간은 busy_refs를 감소시키기 위해 scheduler mutex를 먼저 획득해야 한다 → 순환 대기. 재현 하네스로 실증: 500ms handler tick 중 destroy 호출 → 5초 내 미복귀(영구 정지). 결과적으로 destroy 스레드와 scheduler 스레드가 함께 멎어 **프로세스의 모든 timer가 정지**한다. 이 결함 자체는 delta 이전 구조(delta는 hook 4줄만 추가)이나, S5의 `spot_timer_enter_turn`이 fire 구간을 "spot application claim 해제까지"로 **무한정 연장**해 노출 확률을 실사용 수준으로 끌어올렸다(claim을 쥔 스레드가 같은 spot timer를 destroy하면 100% 데드락). spec상 destroy는 실행 중 타이머 정지·해제를 지원해야 하고(08-utilities), spot spec 9장은 "성공한 destroy 반환 후 새 tick 미전달"로 destroy-중-tick 공존을 전제한다 | `core/src/api/monitoring/timer_api.cpp:110-123` (scheduler_lock 보유 채 busy_refs 대기), `core/src/api/monitoring/timer_scheduler_backend.cpp:153-164` (감소 경로가 scheduler mutex 요구), `:147-150` (enter_turn로 fire 구간 무한 연장). 재현: scratchpad `timer_destroy_race.c` | destroy의 busy_refs 대기 전에 scheduler_lock을 해제(등록 제거는 이미 완료 상태)하거나, fire 종료 구간의 busy_refs 감소·notify를 timer mutex만으로 수행하도록 분리. spot claim 보유 중 같은 spot timer destroy는 EDEADLK 분류 검토 |
| N2 | Medium | I1 | **`handle_actor_left`가 Spot 종료 판정을 누락 — zombie Spot.** `active_actor_count` 감소 지점 7곳 중 이곳만 `maybe_end_spot_locked` 미호출. 도달 가능: facade 전부 해제(facade_count=0, actor가 수명 유지)된 Spot의 마지막 actor가 **다른 node의 Spot으로 join** → 이전 Spot node에 wire LEFT 도착 → 감소 후 참조 0인데 Spot이 영구 잔존. 이후 `spot_get_or_new`가 `created=0`으로 **묵은 generation에 재부착**되어 "마지막 참조 해제 시 종료·재생성 시 generation 증가" 계약(spot spec §1)이 이 경로에서 깨지고 spot/owner entry가 누수된다. F-I1-02 수정의 전파 누락 | `core/src/runtime/services/mesh/mesh_wire_ingress.cpp:518-519` (vs 동형 6곳: `mesh_actor_api.cpp:167,338,703,1020,1142`, `mesh_transfer_api.cpp:1074`) | 감소 직후(LEFT control record admit 뒤가 안전 — facade>0이면 어차피 종료 안 됨) `maybe_end_spot_locked` 호출 + 원격 leave-last-actor 시나리오 test 추가 |
| N3 | Low | I1 | **monitor close vs emit의 cross-thread teardown race (TOCTOU).** `emit_monitor_event`는 node mutex로 `node->monitor`를 스냅샷한 뒤 monitor mutex를 두 번에 나눠 재획득한다(큐/handler 판정 후 unlock → handler_active 증가를 위해 재lock). 그 사이 다른 스레드의 `monitor_close`가 `handler_active==0`을 관측하고 close→`delete monitor`까지 완주할 수 있고, emit은 파괴된 mutex를 잠그거나 close가 OK를 반환한 뒤 handler를 호출한다. emit은 ingress 스레드 등 내부 스레드에서 발생하므로 앱이 외부 동기화로 완전히 회피하기 어렵다. 같은-callback 재진입 계약(F2의 요구)은 정상 동작하며, 이 형상 자체는 iteration-1 snapshot에도 동일(기존 결함, 최종 pass에서 기록) | `core/src/runtime/services/mesh/mesh_runtime.cpp:700-757` (스냅샷·분할 lock), `core/src/api/mesh/mesh_monitor_api.cpp:147-162` (close가 관측 창 사이에 delete) | emit의 handler 스냅샷과 handler_active 증가를 한 critical section으로 묶고, close는 handler_active>0이면 대기 또는 emit 경로를 registry 기반 liveness로 보호 |
| N4 | Low | I3 | **사멸 선언 잔재**: `valid_utf8_public` 선언이 남았는데 정의는 이번 delta에서 삭제됨(호출자 0). F-I1-08 수정이 만든 새 dead code | `core/src/api/mesh/mesh_c_internal.hpp:39` | 선언 삭제 |

## 5. 명시 판정 (manifest §5)

1. **NODROP commit 중 peer 사망** — commit 단계 실패는 pipe 소실(peer 사망)
   뿐이고, 이때 해당 target만 dropped로 집계하고 admitted>0이면 OK를 반환한다
   (`mesh_wire.cpp:337-355`). 사망은 backpressure가 아니라 target 소멸이며
   snapshot 사후 소멸과 동치. `MULTICAST_DROPPED` event와 detail의
   dropped_remote로 관측 가능 — **수용**. 부기: admitted==0이면 BACKPRESSURED로
   분류되어 blocking 호출이 SNDTIMEO까지 10ms poll로 재시도한 뒤 ETIMEDOUT —
   영구 소멸 target에 대한 fail-fast는 아니지만 all-or-none 의미론과 정합
   (editorial 부기 E1).
2. **Spot timer handler의 scheduler head-of-line** — `spot_timer_enter_turn`이
   claim 해제까지 scheduler 스레드에서 `node->cv`를 대기하므로, 한 Spot의 긴
   application claim turn이 같은 scheduler의 다른 모든 timer tick을 지연시킨다.
   상호배제 계약(spot spec §9)의 구현 방식으로서 tick 지연 자체는 계약 위반이
   아니고 지연 상한은 앱 claim turn 길이 — **조건부 수용**. 단 이 특성은 N1과
   결합하면 "지연"이 아니라 "영구 정지"가 된다(enter_turn 대기 중 아무 timer의
   destroy가 겹치면 scheduler 전체 잠김). N1 해소를 전제로만 수용.
3. **ctx_term linger** — 신규 MIXED 2-process test가 종료 순서(자식 선종료 후
   부모 1.5s 대기)로 회피했고, 이는 mesh 신규 계약이 아닌 raw 소켓의 기존
   특성. 85/85 재현 green — **수용(기존 특성, 추적 유지)**.

**TSAN 기존 2계열** — TSAN 재실행(lifecycle) 경고 10건을 전수 분류:
lock-order-inversion 8건은 전부 `prepare_auto_hwm_socket_plan`/`ctx_t::create_socket`
계열(auto-HWM lock-order), data race 2건은 `mailbox_t::recv`의 command mailbox
ypipe 계열(main 스레드 connect의 process_commands vs ingress 스레드 poll의
get_events). 두 계열 모두 9.x 기계 소속으로 mesh 코드는 호출자로만 등장 —
**수용(추적 유지)**, mesh 신규 race 0.

## 6. 축별 판정

### I1 — 계약 구현 일치
- Finding: N1 (High), N2 (Medium), N3 (Low).
- iteration-1 12건 중 11건 완전 해소, F-I1-02는 한 경로 잔여(N2).
- 그 외 재검토(delta 전 경로 + 전체 scope): NODROP 원자성 락 순서, 신규 probe의
  send-scope 안전성, admission successor 재할당 안전, shutdown detach의
  exactly-once, drain 대기와 cv 신호 경로, reply exactly-once, ingress frame
  소유권(전 경로 close/move 대칭), codec 경계 검사(u8/u16 길이라 overflow 불가)
  모두 문제 없음.
- 판정: **NOT CLEAN**

### I2 — POSD·DDD
- 없음. 4모듈 분해는 각각 한 결정(포맷/admission/라우팅/transport)을 소유하는
  깊은 모듈로 정렬되고 공유 표면은 내부 header 하나로 최소화됐다. spot timer
  registry·claim side table의 immortal 패턴은 기존 handle registry와 일관되고
  주석이 근거를 설명한다. 수정으로 인한 경계 훼손·패스스루·책임 혼합 없음.
- 판정: **CLEAN**

### I3 — 정리 완결성
- N4 (Low): `valid_utf8_public` 사멸 선언 1건.
- 그 외: per-node `next_claim_serial` 제거 완결, iteration-1의 DRAINING vestigial
  해소(집계·분기 실동작), removed-identifier 스캔 clean, orphan build target
  없음, internals 문서 양어 동기 정확.
- 판정: **NOT CLEAN** (low 1건, 기능 영향 없음)

## 7. 결론

- iteration-1 finding: 12건 중 11건 해소, 1건(F-I1-02) 부분 해소(잔여 경로는 N2).
- 신규 finding: 4건 (High 1, Medium 1, Low 2). N1은 재현 하네스로 실증된
  데드락으로, 구조는 기존이나 S5 수정(enter_turn)이 노출 창을 무한정 넓혔다.
- 축 판정: I1 = NOT CLEAN, I2 = CLEAN, I3 = NOT CLEAN.
- High finding(N1)이 존재하므로 clean 기준 미충족.

## 8. Editorial note (finding 아님)

- E1: NODROP blocking publish의 재시도가 영구 소멸 local target(owner 소멸)에도
  SNDTIMEO까지 10ms poll을 계속한다 — 의미론상 유효하나 fail-fast 여지.
- E2: mesh SNDTIMEO에 음수(-1) 값 검증이 없고, 음수는 "무한 대기"가 아니라
  "즉시 timeout"으로 동작한다. spec이 음수를 정의하지 않으므로 위반은 아니나
  raw 소켓 관례(-1=무한)와 상충 — 값 검증 또는 spec 명문화 권장.
- E3: `connect_peer`의 endpoint 병합 loop가 DRAINING 이전 세대 entry에 먼저
  매칭될 수 있다(같은 endpoint의 successor보다 앞 index). 반환 intent_id가
  draining 수명을 가리키는 blemish — ADMITTED 우선 매칭 권장.
- E4: ingress multicast의 channel/topic은 UTF-8 재검증 없이 로컬 전달된다
  (metadata는 검증). admitted peer 신뢰 모델에서 수용 가능하나 발신측 검증과
  비대칭.

NOT CLEAN
