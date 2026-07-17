# S5 Core 구현 리뷰 — R2 (Claude Fable), iteration 4 (최종 전체 pass, P4)

리뷰어: Claude Fable (general-purpose agent). 수정 권한 없음(read-only, core/ 무변경).
Codex의 iteration-4 결과 파일(codex-*)은 열람하지 않음. iteration-3까지의
finding ledger·manifest(공유 입력)만 참조.

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Claude Fable (general-purpose agent) |
| Acceptance commit | `59b3ea9400327e74e80b0f8d74763c89ccfdf141` |
| 시작 시각 | 2026-07-17 21:38 +0900 |
| 종료 시각 | 2026-07-17 21:57 +0900 |
| Scope hash (시작) | `d9621658e16a04c53c646286f3386d99f6ac28faf3196616eec4d71e51a8e413` (631 files) — manifest §2와 일치 |
| Scope hash (종료) | `d9621658…` / 631 files — **일치** (core/ 무변경, `git status core/` clean) |

| 실행 명령 | 결과 |
|---|---|
| scope hash 재계산 (iteration-1 manifest §2 명령, 시작·종료 2회) | 두 번 모두 `d9621658…` / 631 — manifest 값과 일치 |
| `cmake --build core/build -j && ctest --test-dir core/build -j8` (run 1) | 84/85 + `test_monitor_socket_contract` 1건 실패 — **단, 이 run은 리뷰어가 병행 실행한 ASAN 전체 재빌드와 부하가 겹친 오염된 run** (§5.3에 분석) |
| `ctest --test-dir core/build -j8` (run 2, 오염 없음) | exit 0 (전량 통과) |
| `ctest --test-dir core/build -j8` (run 3, 오염 없음, 단독) | **100% tests passed, 0 failed out of 85** — manifest 기대치와 일치 |
| ASAN mesh 5 바이너리 재빌드 후 실행 (`lifecycle`(8) `node_basic`(8) `monitor_matrix`(6) `peer_admission`(11) `stress`(3)) | 전부 OK, `AddressSanitizer` 리포트 0 |
| `setarch -R ./core/build-tsan/bin/test_mesh_lifecycle_contracts` (재빌드 후) | 8/8 OK. 경고 10건 전수 분류: lock-order-inversion 9건 전부 `prepare_auto_hwm_socket_plan`/`refresh_auto_hwm_policy`/`ctx_t::create_socket` 계열, data race 1건 `mailbox_t::recv` command mailbox ypipe 계열 — 기존 2계열뿐, mesh 신규 race 0 |
| `git diff 25617130e..59b3ea940` (10 files, +742/−21; core 4 파일) | 전량 검토. 그 외 627개 scope 파일은 iteration-3 검토 snapshot과 bit 동일 |
| 실패 test 표적 재현 (`test_monitor_socket_contract` 단독 3회 + 6×CPU 포화 부하 아래 15회) | **18/18 전량 통과 — 오염 없는 조건에서 재현 실패** |
| N5-계열 패턴 전수 스캔 (mesh api 전 파일의 `lock.unlock()` 25개소 — relock 후 iterator/pointer 재사용 추적) | 위반 0 (transfer/stream-session의 relock 지점은 모두 `find_*` 재조회, 나머지는 lock scope 종료) |
| `wire_send_mutex` 취득 전수 (락 순서 역전 재확인) | mesh_wire.cpp 3개소뿐, leaf — node→wire_send 단방향 유지 |

## 2. Iteration-3 수정 4건 해소 판정

| ID | 심각도 | 판정 | 근거 |
|---|---|---|---|
| F-I1-03(재재) (bind의 stale binding 등록 창) | high | **해소** | 삽입(registry 락으로 one-binding rule 원자화, `mesh_stream_session_api.cpp:621-675`) 후 node lock 재검증(부재/세대 불일치/`draining` → `session_bindings_remove_actor` 롤백+ESTALE, `:685-703`). 원자성 전수: (a) stale 조건은 그 generation에 대해 단조(monotonic) — destroy가 `draining`을 node lock 아래에서 먼저 표시(`mesh_actor_api.cpp:641`)한 뒤 제거 pass(`:739`)를 수행하므로, 재검증이 draining 이전에 통과한 interleaving에서는 이미 삽입된 binding을 destroy의 제거 pass가 반드시 관측·제거하고, 이후라면 재검증이 롤백 — 빠지는 창 없음. (b) 롤백의 광역 제거는 안전: 삽입 시점에 conflict/fenced/idempotent 검사(registry 락 원자)로 그 actor generation의 binding은 자신의 삽입 1개뿐이라 타 세션 binding 제거 불가. (c) 락 순서: node 단독(scoped) → 해제 → registry→service. 기존 registry→node 순서와 역전 없음. (d) spec `service/05-stream-session` §3 "timeout 또는 실패 뒤에는 호출 전 binding 상태를 유지한다"를 롤백이 정확히 이행. idempotent 생략도 안전(기존 binding 존재 = destroy 제거 pass 미도래 → 그 pass가 제거, 합법 직렬화) |
| N3-I1-01 (remote commit 후 local 준비 실패 partial) | high | **해소** | 모든 fallible 준비(record 할당 ENOMEM·metadata 복사·`copy_borrowed_parts`)를 remote commit **이전**의 `prepared` 선구축으로 이동(`mesh_messaging_api.cpp:844-870`), commit 이후는 move·산술·push(`:891-899`)만. goto 경계: `goto retry_reserve`(`:885`)는 `prepared` 선언(`:847`) 이후 지점에서 label(`:798`)로의 **후방 점프로 scope를 빠져나가며** 지역 객체 소멸자가 실행되어 record가 전부 해제됨 — 초기화 건너뛰기 없음(적법), 재시도 round마다 재구축(정확). 실패 경로 전부 commit 이전 → 양 leg all-or-none 성립. counter는 label 직후 재초기화(`:799-803`). iteration-2의 unreachable snapshot 차감(`:912,925`)은 코드 이동 후에도 온전 |
| N5 (drain lock 해제 창 뒤 stale `owner_it` UAF) | medium | **해소(완전)** | 재획득 직후 `owner_it` 재조회(`mesh_actor_api.cpp:685`), deadline 분기는 `end()` 가드(`:691`) 후 사용. **창 이후 다른 stale 참조 재검**: 창 이전 계산물 중 참조형은 `actor_it`(`:658`)와 `owner_it`뿐이고 `actor_it`는 창 이전(`:660-662`)에만 사용, 이후 사용처 없음 — loop 재진입 시 재계산. `held`/`completions_pending`은 값 복사라 무해. 추가로 mesh api 전체의 unlock/relock 25개소 전수 스캔에서 동종 패턴 위반 0 (§1) |
| N6/N3-I3-01 (대상 잃은 주석) | low | **해소** | `mesh_c_internal.hpp:38`의 dead comment 제거 확인. 잔여: 제거 자리에 연속 빈 줄 2개가 남음 — scope에 동종 기존 형상 2건(`mesh_messaging_api.cpp:49`, `mesh_node_api.cpp:121`, 모두 iteration-3 이전부터 존재)이 있는 허용 범위라 editorial(E9)로만 기록 |

## 3. F-I1-01 rejected 재검토 — **수용**

ledger의 기각 근거를 정식 spec 원문과 구현으로 재대조한 결과 수용한다.

1. **계약 원문**: `service/01-mesh-node.ko.md` §5(`:254`) "peer drain은 새
   snapshot에서 제외하지만 **이미 commit한 message를 취소하지 않는다**" — spec이
   commit의 비가역성과 peer 이탈 시 snapshot 제외를 함께 명문화한다. 또한
   §9(`:460-461`) "Logical Multicast는 snapshot 전체의 **원자적 reserve/commit까지
   보장**하며" — 보장 범위가 reserve/commit 원자성에서 끝나고 전달(delivery)은
   범위 밖임을 spec 스스로 한정한다. commit 후 즉시 죽은 peer와 commit 사이에
   pipe를 잃은 peer는 관측 가능한 결과가 동일(미수신)한데, 전자를 spec이 명시적으로
   허용하므로 후자를 "이미 이탈한 peer의 snapshot 제외"로 직렬화하는 회계는 계약
   위반이 아니다.
2. **capacity 차원 all-or-none은 구현이 보장**: reserve 단계가 `wire_send_mutex`
   아래에서 전 target의 `routed_target_writable`을 선검사하고 하나라도 실패하면
   아무것도 commit하지 않는다(`mesh_wire.cpp:319-337`). probe 1 slot이 전체
   multipart admission을 허용하므로 commit 단계의 실패는 용량이 아니라 동시
   peer disconnect에 의한 pipe 소실뿐(`:339-357`). 따라서 §7(`:380`)의
   all-or-none은 admission 차원에서 성립하고, unreachable은 dropped와 분리되어
   "NODROP=1 성공은 두 dropped count가 모두 0"(`:386`)과 정합한다.
3. **회계 정합**: `snapshot − unreachable` 차감이 detail(`mesh_messaging_api.cpp:912`)과
   monitor event(`:925`)에 일관 적용되고 `unreachable ≤ snapshot_remote`라
   underflow 불가. 반례가 될 대안(전체 실패 처리)은 이미 다른 pipe에 넘긴 frame을
   회수해야 하는데 TCP 위에서 불가능하고 §5의 취소-불가 조항과 정면 충돌한다.

## 4. 전체 재검토 범위 (P4)

delta 4 파일 외 627개 scope 파일은 iteration-3에서 P4 전체 재검토를 마친
snapshot과 bit 동일함을 diff로 확정한 뒤, delta와 그 상호작용 면을 적대
재검토했다.

- **bind 경로 재검증 롤백의 원자성·락 순서**: §2 F-I1-03 판정에 상술 — 창 없음,
  락 순서 역전 없음, one-binding rule의 registry 원자성이 롤백의 부수 피해를
  구조적으로 차단.
- **prepared record 이동의 goto 경계**: §2 N3-I1-01 판정에 상술 — 후방 점프의
  소멸자 실행으로 누수·이중 해제 없음.
- **이전 라운드 수정의 회귀**: delta 4 파일 내 기존 수정(unreachable 회계,
  destroy drain·session binding drain, iterator 선캡처, session binding helper)
  전부 형상 유지 확인. 나머지 모듈(timer, monitor 핀, per-node scheduler,
  wire ingress, packaging/workflow)은 무변경이라 iteration-3 판정 유지.
- **N5-계열 전수 스캔**: mesh api 전 파일 unlock 25개소에서 relock 후 무효
  참조 재사용 0 (transfer의 seal/대기 루프와 stream-session close 복원 경로는
  모두 재조회 패턴).
- **락 순서 재확인**: node→wire_send(leaf), node 단독→registry→service,
  registry→node — 순환 없음.
- **공개 표면**: delta에 header 변경 없음, contract gate(ctest 포함) 3 run 전부
  PASS — 신규 공개 API 추가 없음.

## 5. 명시 판정 (known risk)

1. **TSAN 기존 2계열** — 재빌드 후 재실행(lifecycle 8/8) 경고 10건 전수 분류:
   auto-HWM lock-order 9건 + command mailbox ypipe data race 1건. 두 계열 모두
   raw socket 9.x 기계 소속, mesh 신규 race 0 — **수용(추적 유지)**.
2. **ctx_term linger** — mesh 신규 계약이 아닌 raw 소켓 기존 특성. 오염 없는
   전체 suite 2회 연속 85/85 green — **수용(기존 특성, 추적 유지)**.
3. **(신규 관찰, finding 아님)** ctest run 1에서
   `test_monitor_socket_contract`의 `test_pubsub_ready_with_monitor_recv_and_socket_callback`
   1회 실패(subscribe recv EAGAIN 후 teardown 강제 close가 `mutex.hpp:108`
   abort). 단 이 run은 리뷰어가 병행시킨 ASAN 전체 재빌드 부하와 겹친 오염
   조건이었고, 오염 없는 전체 suite 2회 + 단독 3회 + 6×CPU 포화 부하 15회
   전량 green으로 재현 실패. 실패 기전은 raw PUB slow-joiner 창(CONNECTION_READY가
   구독 전파까지 보장하지 않음)으로 raw PUB 계약(§9 "classic PUB의 subscriber별
   전달 범위는 raw PUB 계약을 따른다") **안의 동작**이며 delta(mesh 4 TU)와 무관한
   9.x 유지 기계 test의 시계열 민감성이다. editorial E12에 test-only 보강 여지 기록.

## 6. 축별 판정

### I1 — 계약 구현 일치
- Finding: **없음**. iteration-3 수정 4건 전부 해소(N5는 완전성 재검 포함),
  F-I1-01 rejected 수용(§3). bind 롤백 원자성·goto 경계·이전 수정 회귀 전부 clean.
- 판정: **CLEAN**

### I2 — POSD·DDD
- 없음. bind 재검증은 one-binding rule이 사는 API 계층에 위치하고 기존 helper
  (`session_bindings_remove_actor`)를 재사용해 중복을 만들지 않는다. prepared
  선구축은 commit 정책을 한 함수 안에서 주석과 함께 단일화했다. 경계 훼손·
  패스스루·정책 다중화 없음.
- 판정: **CLEAN**

### I3 — 정리 완결성
- 없음. dead comment 제거 완결(빈 줄 잔재는 기존 허용 형상과 동종 — E9),
  delta에 신규 식별자·공개 표면 변화 없음, orphan 없음.
- 판정: **CLEAN**

## 7. 결론

- iteration-3 수정 4건: 전부 해소 판정 (N5는 창 이후 stale 참조 전수 재검으로
  완전성 확인, 동종 패턴 mesh api 전수 스캔 위반 0).
- F-I1-01 rejected: 계약 근거(spec §5 취소-불가 + §9 보장 범위 한정 + reserve의
  capacity all-or-none 구현 확인)로 **수용**.
- 신규 finding: 없음 (editorial 4건만).
- 검증: scope hash 시작·종료 일치, 오염 없는 ctest 85/85 × 2, ASAN 5 바이너리
  clean, TSAN 기존 2계열뿐.
- 축 판정: I1 = CLEAN, I2 = CLEAN, I3 = CLEAN. blocker·high 없음.

## 8. Editorial note (finding 아님)

- E9: `mesh_c_internal.hpp:37-38` 연속 빈 줄 2개(N6 수정 잔재). 동종 기존 형상
  2건(`mesh_messaging_api.cpp:49`, `mesh_node_api.cpp:121`)과 같은 허용 범위.
- E10: multicast local commit의 "무실패 move"는 엄밀히는 `std::deque::push_back`
  (`mesh_runtime.hpp:136`)과 `ready` set insert의 할당을 포함 — 진성 OOM에서는
  예외 전파(코드베이스 공통 std 컨테이너 관례)로 종결되며 조용한 partial은 아님.
- E11: NODROP blocking 재시도가 round마다 `prepared` 전체(전 target×전 part
  복사)를 재구축 — backpressure 지속 시 ~10ms 주기 CPU 소모. 정확성 무관.
- E12: `test_pubsub_ready_with_monitor_recv_and_socket_callback`은
  CONNECTION_READY 이후 즉시 publish하는데 raw PUB slow-joiner 창(구독 전파
  이전 drop)이 계약상 열려 있어 극한 부하에서 3s 수신창을 놓칠 수 있다(§5.3).
  test-only 보강 여지: ready gate 강화 또는 수신 재시도 창. 실패 시 teardown
  강제 close의 `mutex.hpp:108` abort는 2차 증상.
- iteration-3 E5~E8은 형상 불변으로 유지.

CORE REVIEW CLEAN
