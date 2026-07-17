# S5 Core 구현 독립 리뷰 R1 — iteration 2

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 1의 12건: **resolved 10건, 미해소 2건**
- 신규 finding: **6건** (`high 2`, `medium 2`, `low 2`)
- 전체 유효 finding: `high 4`, `medium 2`, `low 2`
- I1 계약 구현 일치: **NOT CLEAN**
- I2 POSD·DDD: **NOT CLEAN**
- I3 정리 완결성: **NOT CLEAN**

## 실행 증거

| 항목 | 결과 |
|---|---|
| 모델 | Codex (GPT-5) |
| session id | `019f6fde-e3d4-7223-b556-71962ceadc62` |
| companion session id | `8915b088-9549-43cd-a49f-97c1f066b53c` |
| 시작 시각 | `2026-07-17T20:38:26+09:00` |
| 종료 시각 | `2026-07-17T20:44:12+09:00` |
| 시작 HEAD | `a01b537f8ce36d24db44d611b9d9dce4e263306e` — manifest와 일치 |
| 시작 scope hash | 630 files, `fa95152dcc7aecf633a79405f35f3613a5cc833824052bde22775aa71ae370c6` — manifest와 일치 |
| 종료 HEAD/hash | 시작 HEAD와 동일. 630 files, `fa95152dcc7aecf633a79405f35f3613a5cc833824052bde22775aa71ae370c6` — 시작값·manifest와 일치 |
| delta | `8206fd44dcd..a01b537f8ce`, 32 files, +3436/-1722 — manifest와 일치 |
| public surface | `PUBLIC SURFACE CONTRACT: PASS`, 196 exports 일치, 제거 identifier 없음 |
| header contract binary | `unittest_public_contract_headers`: 1/1 PASS |
| lifecycle/peer 동적 test | sandbox TCP bind 제한으로 각각 7/7, 11/11이 start result 705에서 본문 진입 전 실패. 성공·결함 증거로 계산하지 않음 |
| ASAN/TSAN | 같은 환경 제약 때문에 독립 재실행 결과를 만들지 않음. manifest의 결과는 기존 증거로만 기록 |
| 정적 hygiene | 제거 pattern production no-hit, 0-byte no-hit. `git diff --check`는 `mesh_wire.cpp:681` EOF blank 1건 |

## Iteration 1 finding 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| F-I1-01 | **미해소 (high)** | probe 자체는 `socket_base_routing.cpp:216-225`에 추가됐지만, reserve 뒤 peer 소실 시 `mesh_wire.cpp:337-355`는 일부 target을 commit하고 `admitted>0`이면 `OK`를 반환한다. 이는 NODROP 전 target all-or-none 및 성공 dropped=0 계약(`core/doc/spec/core/service/01-mesh-node.ko.md:380-387`)과 충돌한다. |
| F-I1-02 | **resolved** | `mesh_runtime.cpp:672-698`의 `maybe_end_spot_locked`, `mesh_api.cpp:246-385`의 timer generation registry/close, `mesh_dispatch_api.cpp:346-350`의 timer turn 배제로 기존 수명·generation·상호배제 누락을 실제 수정했다. scheduler 구조 문제는 신규 finding으로 분리했다. |
| F-I1-03 | **미해소 (high)** | destroy는 claim과 actor requester operation만 기다린다(`mesh_actor_api.cpp:647-689`). bound control은 별도 session binding/pending 구조(`mesh_stream_session_api.cpp:25-47,76-84`)에 있으나 destroy에 조회·drain hook이 없다. test도 held claim만 검증한다(`test_mesh_lifecycle_contracts.cpp:278-324`). spec 요구는 세 조건 모두다(`04-actor.ko.md:181-182`). |
| F-I1-04 | **resolved** | `mesh_node_api.cpp:317-397`이 concurrent re-entry만 차단하고 deadline에 operation 전체를 detach/clear한 뒤 각각 `TERMINATED/ESHUTDOWN`으로 완료한다. `test_mesh_lifecycle_contracts.cpp:327-366`도 추가됐다. |
| F-I1-05 | **resolved** | descriptor endpoint(`mesh_wire_internal.hpp:88-99`), inbound 기록(`mesh_wire_admission.cpp:199-218`), 동일 endpoint MIXED 병합(`mesh_node_api.cpp:495-503`)과 2-process test(`test_mesh_peer_admission.cpp:1989-2039`)를 확인했다. |
| F-I1-06 | **resolved** | 상위 generation에서 old entry를 DRAINING으로 두고 successor를 생성한다(`mesh_wire_admission.cpp:182-198`); status가 이를 센다(`mesh_node_api.cpp:750-762`). |
| F-I1-07 | **resolved** | peers와 versioned bindings는 전체 element 선검증 후 출력한다(`mesh_node_api.cpp:807-826`, `mesh_stream_session_api.cpp:793-806`). versioned element가 없는 peer channels는 handle/pointer/generation/capacity 검증을 출력 loop 전에 모두 끝낸다(`mesh_node_api.cpp:840-878`). |
| F-I1-08 | **resolved** | strict scalar validator가 overlong/surrogate/U+10FFFF 초과를 거부한다(`mesh_runtime.cpp:50-99`). name은 `mesh_node_api.cpp:22-36`, topic/filter는 `mesh_messaging_api.cpp:739-746,1012-1029`에서 재사용한다. |
| F-I2-01 | **resolved** | codec/admission/ingress/transport 경계가 `mesh_wire_internal.hpp:101-179`로 분리되고 CMake가 네 모듈을 빌드한다(`core/CMakeLists.txt:887-890`). internals도 같은 책임을 설명한다(`services-internals.ko.md:12-29`). |
| F1 | **resolved** | process-global atomic serial과 immortal table을 실제 사용한다(`mesh_dispatch_api.cpp:75-123,353-354`). |
| F2 | **resolved** | 원 finding인 dead guard는 callback 주위 depth 증가/감소로 연결됐다(`mesh_runtime.cpp:747-756`). 다만 close와의 새 수명 경합은 아래 신규 finding이다. |
| F3 | **resolved** | 중복 삼항이 직접 ternary로 정리됐다(`mesh_messaging_api.cpp:406-408`). |

## 신규 finding

| ID | 축 | 심각도 | finding과 근거 |
|---|---|---|---|
| N-I1-01 | I1 | **high** | monitor callback과 close 사이 UAF 경합. emitter는 node에서 monitor를 얻고 락을 놓은 뒤 handler를 캡처하고 다시 락을 놓으며(`mesh_runtime.cpp:700-746`), 그 다음에야 `handler_active`를 올린다(`:747-750`). 그 사이 close는 node+monitor 락 아래 포인터를 제거한 뒤 object를 delete할 수 있다(`mesh_monitor_api.cpp:136-162`). emitter가 `:749`에서 해제된 mutex를 잠글 수 있다. handler 캡처와 active reference 획득을 한 임계구역으로 만들어야 한다. |
| N-I1-02 | I1 | **high** | Actor destroy의 invalid iterator/UAF 가능성. `mesh_actor_api.cpp:695-717`은 `spot_it`를 보존한 채 `maybe_end_spot_locked`를 호출하고 unlock한 뒤 그 iterator를 다시 비교한다. helper는 마지막 non-entry Spot을 실제 erase한다(`mesh_runtime.cpp:672-698`). 마지막 actor인 경우 `:713`이 무효 iterator를 사용한다. erase 전에 control 목적지를 복사하거나 unlock 뒤 key/generation으로 다시 찾아야 한다. |
| N-I2-01 | I2 | **medium** | Spot timer claim 대기가 프로세스 전역 timer scheduler를 head-of-line으로 막는다. Spot 생성은 `mesh_api.cpp:246-256`에서 generic `zlink_timer_new`를 호출하고, 이는 global scheduler를 고정한다(`timer_api.cpp:67-90`). scheduler는 한 timer를 동기 실행한다(`timer_scheduler_backend.cpp:180-215`), `spot_timer_enter_turn`은 claim이 풀릴 때까지 기다린다(`mesh_api.cpp:291-312`). 코드가 이미 선언한 per-Spot backend(`timer_api_internal.hpp:20-36`, `timer_scheduler_backend.cpp:44-50`)를 사용하지 않는다. |
| N-I3-01 | I3 | **medium** | Conan 10.0.0 source가 digest로 고정되지 않았다. recipe는 conandata를 그대로 `get`에 넘기지만(`conanfile.py:31-33`), 10.0.0/rc.1에는 URL만 있고 sha256이 없다(`conandata.yml:2-5`). 같은 디렉터리 README는 URL+sha256을 요구한다(`README.md:7-11`). 최종 package source 무결성 gate가 비어 있다. |
| N-I3-02 | I3 | **low** | `core/packaging/conan/__pycache__/conanfile.cpython-312.pyc`가 tracked file이다. 또한 packaging README의 예시 version과 URL은 아직 0.6.0/`ulala-x`다(`README.md:13-15`), 현재 10.0.0/`kairos-code-dev` entry(`conandata.yml:2-5`)와 어긋난다. |
| N-I3-03 | I3 | **low** | `git diff --check 8206fd44dcd..a01b537f8ce -- core`가 `core/src/runtime/services/mesh/mesh_wire.cpp:681: new blank line at EOF`를 보고한다. |

## Manifest §5 명시 판정과 TSAN

| 항목 | 판정 | 근거 |
|---|---|---|
| ① NODROP commit 중 peer 사망 | **수용 불가 — high, F-I1-01 미해소** | `mesh_wire.cpp:337-355`의 partial success가 spec `01-mesh-node.ko.md:380-387`의 all-or-none 및 성공 dropped=0을 깨뜨린다. |
| ② Spot timer scheduler head-of-line | **수용 불가 — medium, N-I2-01** | global scheduler의 단일 worker가 `spot_timer_enter_turn`의 claim 대기를 직접 수행한다. 이미 존재하는 per-Spot scheduler backend는 연결되지 않았다. |
| ③ ctx_term linger | **기존 raw socket 특성으로 수용·추적** | blocky context 기본 linger는 -1(`socket_base.cpp:133`)이고 termination이 이를 pipe에 적용한다(`socket_base_lifecycle.cpp:137-161`). MIXED test는 상대보다 늦게 종료해 회피한다(`test_mesh_peer_admission.cpp:2035-2039`). bounded 종료 증거는 아니지만 신규 S5 회귀 근거도 없다. |
| TSAN auto-HWM lock-order | **판정 불완전·추적 유지, 신규 finding 없음** | `_slot_sync`를 잡은 채 socket plan을 준비·적용하는 경로가 유지된다(`ctx_auto_hwm_recalc.cpp:80-116`). 이 환경에서는 독립 TSAN 본문 실행이 불가능해 resolved로 판정하지 않았다. |
| TSAN raw socket mailbox ypipe | **판정 불완전·추적 유지, 신규 finding 없음** | cpipe send/recv는 `_sync`로 보호된다(`mailbox.cpp:39-56,89-97`). 기존 기계 warning의 동적 소거 증거는 없으므로 추적을 유지한다. |

## 축별 판정

### I1 계약 구현 일치 — NOT CLEAN

- 기존 high 2건(F-I1-01, F-I1-03)이 미해소다.
- 신규 high 2건(N-I1-01, N-I1-02)이 있다.

### I2 POSD·DDD — NOT CLEAN

- `N-I2-01` 1건. per-Spot scheduler 결정이 준비돼 있는데도 Spot timer가 generic 전역 scheduler에 연결되어 한 Spot의 claim 상태가 모든 timer의 진행 조건으로 누출된다.

### I3 정리 완결성 — NOT CLEAN

- `N-I3-01` medium 1건, `N-I3-02`·`N-I3-03` low 2건.
- 제거 public identifier와 공개 surface는 clean이지만 package integrity·generated artifact·delta hygiene가 남아 있다.

## 종료 검증

종료 시 manifest §2 명령을 다시 실행해 아래 두 값이 시작값과 같은지 확인한다.

- file count: `630`
- aggregate SHA-256: `fa95152dcc7aecf633a79405f35f3613a5cc833824052bde22775aa71ae370c6`

최종 판정: **NOT CLEAN**
