# S5 Core 구현 독립 리뷰 R1 — iteration 3

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 2 finding 11건: **해소 9건, 부분 해소·미해소 2건**
- 신규 finding: **2건** (`high 1`, `low 1`)
- 현재 유효 finding: **4건** (`high 3`, `low 1`)
- I1 계약 구현 일치: **NOT CLEAN**
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **NOT CLEAN**

## 실행 증거

| 항목 | 결과 |
|---|---|
| 리뷰어 | R1, Codex (GPT-5) |
| 시작·종료 시각 | `2026-07-17T21:15:27+09:00` · `2026-07-17T21:21:00+09:00` |
| HEAD | 시작·종료 모두 `25617130eeeb1dc464aec6eca1a8378888aee42a` — manifest와 일치 |
| 시작 scope hash | 631 files, `1ca3763f80f568890e2e3e888b4b1dd93e2d15dab9deb9125c12b316fa1f6598` — manifest와 일치 |
| 종료 scope hash | 631 files, `1ca3763f80f568890e2e3e888b4b1dd93e2d15dab9deb9125c12b316fa1f6598` — 시작값·manifest와 일치 |
| delta | `a01b537f8ce..25617130ee`, 30 files, +983/−66 — manifest와 일치, `git diff --check` clean |
| 공개 표면 | `PUBLIC SURFACE CONTRACT: PASS`, 196 exports 일치, 제거 identifier 없음 |
| header contract | `unittest_public_contract_headers`: 1/1 PASS |
| 제거·정적 hygiene | 제거 identifier production no-hit(의도한 absence assert 1건 제외), 0-byte no-hit, CMake에 mesh wire 4 TU·mesh test 5개 등록 |
| 패키징 gate | workflow·conandata YAML parse 성공. 10.0.0은 digest 미기입 상태라 새 sha256 gate에서 의도대로 실패하며 S6-05 전 publish를 차단 |
| mesh 동적 실행 | 5개 바이너리의 36 case 모두 sandbox TCP bind 제한으로 start result 705에서 본문 진입 전 실패. 성공·결함 증거로 계산하지 않음 |
| 기존 suite·sanitizer | manifest의 85/85·ASAN clean·TSAN 기존 2계열은 기존 증거로만 기록. `core/` 쓰기 금지 때문에 재빌드하지 않음 |

## Iteration 2 finding 11건 해소 판정

| ID | 판정 | delta 변경과 근거 |
|---|---|---|
| F-I1-01(재) | **부분 해소·미해소 (high)** | delta는 `unreachable_out_`을 추가해 NODROP commit 실패를 drop에서 제외한다(`mesh_wire.cpp:279-358`). 그러나 호출 초기에 만든 `snapshot_remote`를 commit 실패 뒤 차감한다(`mesh_messaging_api.cpp:901-918`). 정식 계약은 한 번 만든 snapshot의 모든 target 또는 none이다(`service/01-mesh-node.ko.md:367-371,380-386`). post-commit 재분류로 partial delivery가 사라지지 않는다. |
| F-I1-03(재) | **부분 해소·미해소 (high)** | delta는 session pending 조회·binding 제거를 추가하고 destroy에서 호출한다(`mesh_actor_api.cpp:677-737`, `mesh_stream_session_api.cpp:1168-1198`). 하지만 bind는 actor 존재·generation만 확인한 뒤 node lock을 놓고 나중에 binding을 삽입한다(`mesh_stream_session_api.cpp:595-675`). destroy의 제거 pass 뒤 stale binding을 성공 등록할 수 있어 새 admission 차단과 binding 제거가 원자적이지 않다. |
| N1 | **해소** | destroy는 scheduler lock을 먼저 놓고 parked Spot turn을 cancel한 뒤 busy ref를 기다린다(`timer_api.cpp:131-153`); enter-turn은 cancel을 재조회한다(`mesh_api.cpp:296-325`). overlap+claim-held cancel test도 추가됐다(`test_mesh_lifecycle_contracts.cpp:481-541`). |
| N-I1-01/N3 | **해소** | emitter가 node lock 아래 `monitor_emit_refs`를 획득·반납하고(`mesh_runtime.cpp:703-763`), close가 포인터를 제거한 뒤 ref 0을 기다리고서 delete한다(`mesh_monitor_api.cpp:148-168`). |
| N-I1-02 | **해소** | `spot_present`를 `maybe_end_spot_locked` 전에 캡처하며 erase 이후 `spot_it`를 사용하지 않는다(`mesh_actor_api.cpp:709-731`). |
| N2 | **해소** | LEFT record admission 뒤 이전 Spot에 `maybe_end_spot_locked`를 호출한다(`mesh_wire_ingress.cpp:518-550`). |
| N-I2-01 | **해소** | Spot timer가 per-MeshNode scheduler로 생성되고(`mesh_api.cpp:250-276`, `timer_scheduler_backend.cpp:231-250`), node destroy에서 회수된다(`mesh_node_api.cpp:454-457`, `timer_scheduler_backend.cpp:270-284`). 전역·타 node HOL 누출을 막는다. |
| N-I3-01 | **해소** | workflow가 선택 version의 sha256 64자 존재를 필수 검사한다(`.github/workflows/core-conan-release.yml:65-87`). 실제 digest 기입은 예정된 S6-05 전까지 publish를 실패시키며 README가 절차를 명시한다(`core/packaging/conan/README.md:7-18`). |
| N-I3-02 | **해소** | pycache는 tracked 목록에서 제거되고 `.gitignore`로 제외된다. README는 10.0.0·`kairos-code-dev`·`core/v<VERSION>` 경로로 갱신됐다(`README.md:13-18`). |
| N-I3-03 | **해소** | candidate `git diff --check`가 clean이며 `mesh_wire.cpp` EOF extra blank가 없다. |
| N4 | **해소** | `valid_utf8_public` 선언과 사용은 0건이며, `zlink_timer_cleanup_spot`과 timer의 옛 `owner_spot` field도 제거됐다. 선언에 딸린 잔여 comment는 신규 I3 finding으로 분리했다(`mesh_c_internal.hpp:35-40`). |

### 미해소 기존 finding 상세

| ID | 이슈·근거 | 영향 | 수정 범위 | 검증 방향 |
|---|---|---|---|---|
| F-I1-01(재) | NODROP commit 실패를 `unreachable_out_`으로 세어 OK를 반환하고(`mesh_wire.cpp:346-358`), 호출 초기에 만든 remote snapshot을 사후 차감한다(`mesh_messaging_api.cpp:901-918`). 이는 한 번 만든 snapshot의 모든 target 또는 none 계약과 충돌한다(`service/01-mesh-node.ko.md:367-371,380-386`). | 성공한 NODROP에서 원 snapshot의 일부 remote target만 수신하고, detail·event가 이미 일어난 snapshot을 축소해 보고할 수 있다. | `mesh_wire.{hpp,cpp}`의 remote NODROP reserve/commit과 `mesh_messaging_api.cpp`의 detail 회계. | reserve와 commit 사이 peer pipe 소실을 결정적으로 주입해 원 snapshot 전체 전달 또는 zero-delivery backpressure만 나오는지, snapshot count가 사후 변경되지 않는지 확인한다. |
| F-I1-03(재) | destroy는 draining을 설정하고(`mesh_actor_api.cpp:635-642`) binding을 한 번 제거하지만(`mesh_actor_api.cpp:736-737`), bind는 actor 검증 뒤 node lock을 놓고 별도 registry/service lock 아래 나중에 삽입한다(`mesh_stream_session_api.cpp:595-675`). | destroy 성공 뒤 stale Actor generation binding이 남아 성공한 bound send에 사용되거나, 이미 사라진 authority에 대한 bind가 성공할 수 있다(`mesh_stream_session_api.cpp:1018-1068`). | 내부 Actor draining admission check와 bind 등록·destroy binding 제거 사이의 원자적 순서. | bind를 Actor 검증 직후 정지시키고 destroy를 완료한 뒤 재개해 draining/stale 결과, binding 0개, destroyed generation의 bound send 실패를 확인한다. |

## 신규 finding

| ID | 축 | 심각도 | 이슈·근거 | 영향 | 수정 범위 | 검증 방향 |
|---|---|---|---|---|---|---|
| N3-I1-01 | I1 | **high** | remote leg를 먼저 commit한다(`mesh_messaging_api.cpp:844-861`). 이후 local target별 record allocation·copy가 실패하면 즉시 오류를 반환한다(`mesh_messaging_api.cpp:863-882`). 이는 NODROP all-or-none 계약(`service/01-mesh-node.ko.md:380-386`)과 충돌한다. | remote와 앞선 local target은 수신했는데 호출은 실패하고 뒤 local target은 누락될 수 있다. retry하면 중복 전달도 가능하다. | Logical Multicast의 fallible 준비와 commit 순서(`mesh_messaging_api.cpp`; 필요할 때만 내부 wire seam). | remote 1개·local 2개 이상에서 local record allocation/copy 실패를 결정적으로 주입해 실패 시 전달 0을 확인하거나, 첫 commit 전에 모든 fallible 준비가 끝났음을 검증한다. |
| N3-I3-01 | I3 | **low** | `valid_utf8_public` 삭제 뒤 `mesh_c_internal.hpp:35-40`에 “UTF-8 validity for public names and topics” comment만 남고 설명 대상 선언이 없다. | 내부 header가 존재하지 않는 helper를 암시해 N4 정리가 불완전하고 유지보수자가 잘못된 표면을 찾게 한다. | `mesh_c_internal.hpp`의 해당 comment 한 곳. 동작·공개 API 변경 없음. | comment가 실제 인접 선언을 설명하는지 scoped review하고 `valid_utf8_public` no-hit를 유지한다. |

## 전체 campaign scope P4 재검토

현재 snapshot을 기준으로 다음을 처음부터 다시 대조했다.

- mesh runtime 8파일과 mesh API 11파일의 lifecycle, lock, mailbox, wire, reply, transfer, monitor, STREAM session 책임
- timer 3파일의 scheduler 수명·destroy·Spot turn 상호배제
- `socket_base*`의 routed probe, pipe commit과 linger 종료 경로
- `core/include/zlink/` 공개 header 폐쇄와 contract export 196개
- `test_mesh_*` 5파일, `core/tests/contract/`, CMake target 연결
- Conan recipe·workflow·README, internals 한영 문서의 구현 일치

Iteration 2에서 해소된 monitor pin, iterator 선캡처, remote LEFT 종료, scheduler 분리,
package gate에는 추가 결함을 찾지 못했다. 다만 NODROP의 snapshot/commit 원자성과
Actor destroy–binding admission 경합은 정식 계약을 계속 위반한다.

## Known risk 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order | **수용·추적 유지, 신규 finding 없음** | `_slot_sync` 아래 socket plan 준비·적용 경로가 유지된다(`core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80-116`). 이번 환경에서 독립 TSAN 본문 실행은 불가능했으므로 해소로 주장하지 않는다. |
| TSAN raw command mailbox ypipe | **수용·추적 유지, 신규 finding 없음** | cpipe send/recv는 `_sync`로 감싼다(`core/src/runtime/core/mailbox.cpp:39-56,89-97`). 기존 기계 경고의 동적 소거 증거가 없어 추적을 유지한다. |
| ctx_term linger | **기존 raw socket risk로 수용·추적 유지** | blocky context의 기본 linger는 -1이다(`socket_base.cpp:129-134`), termination은 attached pipe를 종료한다(`socket_base_lifecycle.cpp:137-161`). MIXED test의 1500ms 종료 순서(`test_mesh_peer_admission.cpp:2035-2039`)는 bounded 종료 증거가 아니라 회피다. 신규 S5 회귀 근거는 없다. |

## 축별 판정

### I1 계약 구현 일치 — NOT CLEAN

- `F-I1-01` high: commit-time peer 소실을 post-commit snapshot 축소로 처리해 all-or-none 위반이 남는다.
- `F-I1-03` high: Actor destroy와 concurrent session binding 사이에 stale binding 등록 창이 남는다.
- `N3-I1-01` high: remote commit 뒤 local allocation/copy 실패가 partial delivery를 만든다.

### I2 POSD·DDD — CLEAN

- finding 없음.
- per-MeshNode scheduler 선택은 generic timer와 타 node의 진행 조건을 격리하며 새 public surface를 만들지 않는다.
- wire codec·admission·ingress·transport 경계와 Actor/STREAM 책임 분리는 유지된다.

### I3 정리 완결성 — NOT CLEAN

- `N3-I3-01` low 1건: 삭제한 내부 선언의 orphan comment.
- package integrity gate, tracked artifact, 제거 identifier, CMake 연결과 candidate diff hygiene는 clean이다.

## 최종 판정

Iteration 2의 11건 중 9건은 해소됐고 2건은 부분 해소에 그쳤다. 신규 finding은
2건이며 현재 high 3건이 존재한다. 따라서 clean gate를 충족하지 못한다.

CORE REVIEW NOT CLEAN
