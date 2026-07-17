# S5 Core 구현 독립 리뷰 R2 (Claude Sonnet) — iteration 6 (연장 2회차, 전체 pass)

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 5 병합 finding 4건: **해소 4건**(전부 확인)
- 신규 finding: **1건**(`medium 1`)
- 현재 유효 finding: **1건**(`medium 1`)
- I1 계약 구현 일치: **NOT CLEAN**(medium 1)
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **CLEAN**

manifest는 iteration 5의 4개 병합 지점 해소를 전제로 "전체 pass"를 지시했다.
그 4건은 실측으로 전부 해소를 확인했다. 그러나 전체 scope 재검토 중, 이번
iteration이 고친 `slot_base`(CS-I1-01/N5-I1-01)와 **동일한 결함 계열**이
바로 인접한 코드와 다른 3개 파일에 남아 있음을 발견했다(§4.1). 이 신규
finding 때문에 I1이 다시 NOT CLEAN이 되어 clean gate를 충족하지 못한다.

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Claude Sonnet |
| Candidate commit / HEAD | `b1e6c81fb5de1d28000bbb9ffaf6750169393067` — 시작·종료 모두 동일 |
| Scope hash (시작) | `6fa7e8c66ef878cff76356c9208dbf194a3dffc7f494948d91b87f51cd7656d8` (631 files) — manifest §2와 일치 |
| Scope hash (종료) | 동일 값 — 일치, `git status core/` clean |
| iteration 6 delta | 없음(리뷰 전용 iteration, core/ 무수정) |
| 공개 표면 | `check_public_surface.py . core/build/lib/libzlink.so.10.0.0`: **PASS**, 196 exports 정확 일치, 제거 identifier 없음 |
| header contract | `unittest_public_contract_headers` 재실행: 1/1 PASS |
| `cmake --build core/build -j$(nproc)` | 이미 최신, 성공 |
| `ctest --test-dir core/build -j8` | **100% tests passed, 0 failed out of 85** |
| `test_mesh_lifecycle_contracts` 단독 3회 | 매회 9/9 PASS(신규 bind/destroy 반환값 단정 포함, 안정) |
| ASAN 5 mesh 바이너리(재빌드·재실행) | lifecycle 9·peer_admission 12·stress 3·monitor_matrix 6·node_basic 8 — 전부 리포트 0 |
| TSAN lifecycle(`setarch $(uname -m) -R`) | 9/9 PASS, 경고 12건 전수 stack frame 분류: lock-order-inversion 10건 = `prepare_auto_hwm_socket_plan` 계열(known risk #1), data race 2건 = `mailbox_t::recv` command mailbox ypipe 1(known risk #2) + `pipe_t::detach_peer_backref` 1(known risk #3). mesh 신규 race 0 |
| `git diff --check c8d567c64..b1e6c81fb` | clean |
| 정적 hygiene | delta 5개 파일(CHANGELOG/spec 2/cpp 1/test 1) 전수 재확인: TODO/FIXME/디버그 출력 없음 |
| CHANGELOG 수치 재계산 | `ctest -N` 85, `RUN_TEST (` count: peer_admission 12·lifecycle_contracts 9·monitor_matrix 6·stress 3 — CHANGELOG 서술과 전부 일치 |
| 0-byte·merge marker | scope 631개 파일 전수 재확인: 0건 |
| CMake·package | mesh wire 4 TU(`core/CMakeLists.txt:886-890`), mesh test 5개(`core/tests/CMakeLists.txt:93-97`) 등록 확인. `conandata.yml` YAML parse 성공 |

TSAN 2-process peer_admission(baseline `472f66a32` 재현, delta-무관)과 TSAN
stress 3회 매트릭스는 iteration 5에서 이미 독립 재실행으로 확정된 사항이라
이번 iteration에서 재실행하지 않았다(과제 지시 "TSAN 2-process admission
3건 실패는 baseline 재현으로 delta-무관 확정된 사항"을 그대로 수용) — 대신
이번 iteration이 실제로 건드린 코드 경로(publish 선예약 try 범위, race test
반환값 단정)를 직접 커버하는 lifecycle TSAN·ASAN 5종을 재실행해 delta 자체의
안전성을 확인했다.

```bash
git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md \
  | xargs sha256sum | sha256sum
```

## 2. iteration 5 병합 finding 4건 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| CS-I1-02/F-I1-01 잔여 | **해소** | `01-mesh-node.ko.md:467-470`(영문 `md:512-516`)이 "이 원자성은 §7의 capacity admission 보장이며, reserve와 commit 사이의 peer 이탈은 §7의 unreachable 규칙을 따른다"를 명시해 §9를 §7에 종속시켰다. §5(`:254-255` peer drain은 새 snapshot에서 제외하되 이미 commit한 message는 취소하지 않음)·§7(`:388-390` reserve~commit 사이 이탈은 backpressure가 아니라 §5 peer 이탈이며 drop이 아닌 unreachable로 보고)·§9(신규 문구)가 이제 동일한 결론(성공+unreachable 분리 보고+나머지 target 전달)으로 수렴한다. `grep`으로 다른 spec 파일(07-monitoring 등)에 잔여 무조건적 원자성 문구가 없음을 확인했다 |
| CS-I1-01/N5-I1-01 | **해소** | `mesh_messaging_api.cpp:878-882`에서 `slot_base`가 빈 vector로 선언된 뒤 `try` 안에서 `resize`된다. `resize`가 던지면 `slots_taken == 0`이므로 롤백 루프(`for (t < slots_taken)`)가 no-op이 되어 상태 훼손이 없다(§4.1에서 코드를 직접 추적해 확인) — slots_taken 카운터 기반 롤백은 resize 실패 시에도 정합 |
| CS-I3-01/N5-I3-01 | **해소** | `ctest -N` 실측 85, `test_mesh_peer_admission` 12·`test_mesh_lifecycle_contracts` 9·`test_mesh_monitor_matrix` 6·`test_mesh_stress` 3(전부 `RUN_TEST (` 카운트로 직접 재집계) — `CHANGELOG.md:56-66`의 수치·케이스 서술과 전부 일치 |
| CS-I3-02/N5-I3-02 | **해소, coordinator 판단 수용** | `test_mesh_lifecycle_contracts.cpp:659-666`에서 두 binder의 반환값을 `std::atomic<int>`로 캡처하고, `:679-691`에서 legal set `{OK, INVALID_STATE, NOT_FOUND, BACKPRESSURED}` 단정을 라운드마다 수행한다. 주석(`:603-611`)도 실제 단정 범위(합법 집합·post-destroy 단조 실패·잔존 0)와 성공-후-rollback이 외부 관측점 없이 설계 논증으로 닫힘을 정확히 서술한다. coordinator 판단(seam 추가 비용이 낮은 severity 대비 부적절)을 **수용**한다 — 이유는 §3 |

## 3. 성공-후-rollback interleaving 미관측에 대한 coordinator 판단 평가

iteration 5의 coordinator는 원 반례(idempotent 성공 후 다른 호출의 stale
rollback으로 binding 소멸)를 test가 직접 잡지 못하는 문제를, 프로덕션
bind 경로에 결정적 pause seam을 추가하지 않고 **형식적 happens-before
논증**으로 닫기로 했다(low severity, seam 비용이 그보다 큼).

`mesh_stream_session_api.cpp:685-705`를 다시 추적했다: 두 성공 형태(초기
삽입·idempotent 관측) 모두 `:692-698`에서 동일한 재검증(actor 존재·generation
일치·미draining)을 node mutex 임계구역 안에서 거친다. registry_lock과
node->mutex 임계구역이 이루는 전순서(total order)와, generation이 node
전역에서 단조 증가하며 재사용되지 않는다는 사실(`mesh_actor_api.cpp:436,476`)을
결합하면, 재검증에서 not-stale을 관측한 시점 이후에 실행되는 어떤 destroy도
반드시 그 binding을 회수한다 — 이는 결정적 interleaving 주입 없이 성립하는
논증이며, 재검증 코드 자체가 두 성공 형태를 구분하지 않고 동일하게 처리하므로
회귀가 재도입되면 코드 구조 자체가 달라져야 한다(즉 조용히 되살아나기 어렵다).

**수용**: seam 추가는 공개 경로(모든 bind 호출)에 test 전용 분기를 상시
심는 비용이며, 이 low-severity 검증 공백 하나를 위해 정당화되지 않는다.
논증이 코드의 실제 구조(재검증 게이트가 두 경로에 대해 문자 그대로 동일)에
근거하므로 형식적 대체물로 충분하다고 판단한다.

## 4. 축별 finding

### I1 계약 구현 일치 — NOT CLEAN (medium 1)

#### CS6-I1-01 (medium) — mesh submit record 구성 중 다수의 fallible 할당이 ENOMEM 매핑 밖에 있음(slot_base와 동일 결함 계열, 인접·자매 지점)

- 이슈·근거: 이번 iteration의 CS-I1-01/N5-I1-01 수정은 `publish_common`의
  `slot_base` **한 줄**만 `try` 안으로 옮겼다. 그러나 바로 그 함수 안, 같은
  `try` 블록 **바로 앞**의 "prepared record" 구축 loop
  (`core/src/api/mesh/mesh_messaging_api.cpp:848-869`)에 구조적으로 동일한
  결함이 남아 있다: `prepared.reserve (accepting.size ())`(:849),
  `record->channel_name = channel;` / `record->topic = topic;`(:860-861,
  `std::string` 대입 — SSO 상한을 넘는 channel/topic 이름에서 힙 할당),
  `record->application_metadata.assign (...)`(:864-865), `copy_borrowed_parts`
  내부의 `record_->parts.resize (part_count_)`(mesh_messaging_api.cpp:54, 호출은
  :868) 전부 `std::bad_alloc`을 던질 수 있는 `std::string`/`std::vector`
  연산이며, 이 loop를 감싸는 try/catch가 전혀 없다. 이 함수 안에서 유일하게
  null-guard가 있는 지점은 `new (std::nothrow) queued_record_t ()`(:851)
  자체뿐이고, 그 뒤에 이어지는 멤버 대입은 모두 무방비다.
  같은 결함 계열이 이 함수에 국한되지 않는다:
  - `core/src/api/mesh/mesh_messaging_api.cpp:190,193,196` —
    `submit_local_record`(Node/Spot 직접 send·request의 공유 본체). `record`
    null-guard(:183-185, ENOMEM 매핑)는 있으나 `record->channel_name =
    channel_name_;`(:190), `application_metadata.assign`(:193),
    `copy_borrowed_parts`(:196)는 무방비.
  - `core/src/api/mesh/mesh_actor_api.cpp:1319-1338` — 로컬 Actor
    send·request submit 경로. `new (std::nothrow)` null-guard가
    `ZLINK_SUBMIT_OUT_OF_MEMORY`로 명시 매핑되지만(:1320-1323), 바로 다음
    `application_metadata.assign`(:1333)과 `record->parts.resize`(:1336)는
    무방비.
  - `core/src/api/mesh/mesh_stream_session_api.cpp:908-925` — transfer-fence가
    걸린 STREAM session Actor send·request 경로. 동일 패턴: null-guard는
    ENOMEM 매핑(:910-913)이지만 `application_metadata.assign`(:923)과
    `copy_session_record_parts`(:925, 그 내부 `record_->parts.resize`는
    `mesh_stream_session_api.cpp:233`)가 무방비.
  네 곳 모두 반환 타입이 `zlink_submit_result_t`이고 같은 함수 안에서 이미
  `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`을 명시적으로 매핑하는 코드
  (record-null 검사)와 나란히 있어, submit family의 errno 계약
  (`core/doc/spec/core/04-errno-map.ko.md:24-45`: "`ZLINK_SUBMIT_OUT_OF_MEMORY`
  — `ENOMEM` — 필요한 storage 확보 실패")가 이 함수들에 적용됨이 코드
  자체로 명백하다. (참고: `mesh_actor_api.cpp:88,176`의 `control_record`/
  `destroyed`와 `mesh_transfer_api.cpp:75`의 `emit_transfer_control`은
  `void`/`unique_ptr` 반환의 내부 best-effort 이벤트 레코드 빌더로, 공개
  submit 계약에 묶이지 않아 이 finding에서 제외했다.)
- 영향: CS-I1-01/N5-I1-01이 고친 것과 완전히 동일한 위반 — 실제 OOM에서
  이 지점들의 할당 실패가 `extern "C"` 경계를 넘어 전파되어 typed
  ENOMEM 반환 대신 `std::terminate` 계열 process abort로 이어질 수 있다.
  `submit_local_record`와 `publish_common`의 prepared loop는 각각 Node/Spot
  직접 메시징과 Logical Multicast의 **모든** 성공 호출이 지나가는 핫패스이므로,
  실제 시스템 OOM 상황에서 이 무방비 지점 중 하나에 도달할 가능성은 이번에
  고친 `slot_base` 한 줄과 본질적으로 같은 수준이다(빈도가 아니라 종류가
  같은 위험). 데이터 훼손은 아니며, `core/src/runtime/transports/*`·
  `core/src/runtime/core/{socket_poller,ctx_socket_registry}.cpp`의 14개
  기존 `catch (const std::bad_alloc &)` 사이트가 보여주듯 이 프로젝트는
  이 경계에서 예외 안전성을 실제로 지키는 관행이 있어, mesh API 계층만
  일관성이 깨져 있다.
- 수정 범위: 위 4개 함수의 record 구축 loop(문자열 대입·vector assign/resize
  전부)를 기존 `slot_base` 자매 `try`/`catch (const std::bad_alloc &)` 경계
  안으로 이동하거나, 각 함수에 동등한 새 경계를 추가.
- 검증 방향: 각 지점에서 할당 실패를 주입해(예: 매우 긴 channel/topic 이름
  경계값, 또는 테스트 전용 allocator 훅) delivery 0, 부분 상태 잔존 0,
  `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM` 반환, C ABI 밖 예외 0을 확인.
- severity 근거: iteration 5 양 리뷰어가 이 정확히 같은 결함 계열(단일
  지점)에 medium을 매긴 선례를 따른다. 지점 수가 늘었다고 해서 실패 조건
  (시스템 OOM)의 발생 가능성 자체가 커지지는 않으므로 상향하지 않았다 —
  다만 이번에 닫힌 지점 바로 옆에 구조적으로 동일한 지점들이 그대로
  남아 있다는 점에서, 이번 수정의 "완결성"은 coordinator가 재평가할
  필요가 있다.

### I2 POSD·DDD — CLEAN

- finding 없음. `core/doc/internals/posd-module-structure.ko.md:124-144`·
  `architecture.ko.md:1224-1230`(영문 대응 동일)이 열거하는 mesh 4모듈
  (`mesh_wire`/`_codec`/`_admission`/`_ingress`, 공유 계약
  `mesh_wire_internal.hpp`)이 `git ls-files
  core/src/runtime/services/mesh/`의 실제 8개 파일과 정확히 일치하고,
  `core/CMakeLists.txt:886-890` 빌드 목록도 동일하다.
- CS6-I1-01은 계약(errno map)과 구현의 정합성 문제이지 모듈 경계 침범이
  아니다 — 각 함수는 자신이 속한 API 계층의 기존 helper
  (`copy_borrowed_parts`/`copy_session_record_parts`)를 그대로 재사용했을
  뿐 새 우회 경로를 만들지 않았으므로 I1에만 계상하고 I2에 중복 계상하지
  않았다.

### I3 정리 완결성 — CLEAN

- finding 없음. CHANGELOG 수치는 실측과 전부 일치(§2). scope 631개 파일
  전수에서 0-byte·merge marker 0건. mesh wire 4 TU·mesh test 5개가
  CMakeLists에 등록. `conandata.yml` parse 성공. `git diff --check`
  clean. delta 5개 파일에 TODO/FIXME/디버그 출력 없음.
- race test의 반환값 관측·legal-set 단정·주석 정합화가 완료되어 iteration 5
  low finding 2건(CS-I3-01, CS-I3-02) 모두 재발 없음을 확인했다.

## 5. Known risk 4건 명시 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order 계열 | **수용·추적 유지, 신규 finding 없음** | lifecycle 재실행 경고 10건 전부 top frame이 `prepare_auto_hwm_socket_plan`/`refresh_auto_hwm_policy`/`ctx_t::create_socket` 계열(`socket_base.cpp:225,372`)로 귀결. 이번 iteration은 이 경로를 건드리지 않았다(delta 4파일 중 코드 변경은 `mesh_messaging_api.cpp` 1줄 이동뿐) |
| TSAN raw command mailbox ypipe 계열 | **수용·추적 유지, 신규 finding 없음** | lifecycle 재실행 data race 1건이 `mailbox_t::recv`(`mailbox.cpp:66`)의 command mailbox 읽기/쓰기 경합으로 귀결. mesh 응용 계층 mutex는 스택에 없음 |
| raw socket teardown(`pipe_t::detach_peer_backref`·asio `blob_t`) | **9.x raw 기계 risk로 수용·추적 유지, mesh delta finding 아님** | lifecycle 재실행에서 `pipe.cpp:202`(`detach_peer_backref`) 1건 관측. asio `blob_t` 경로는 이번 단독 lifecycle 실행에서는 발현하지 않았으나(부하 의존적 관측으로 iteration 5도 동일하게 보고) 이번 delta(mesh 6개 파일 중 실질 코드 변경은 1파일 1줄)와 무관한 raw socket 계층 코드이므로 관측 유무와 무관하게 known risk 분류를 유지한다 |
| ctx_term linger | **수용·추적 유지** | `socket_base.cpp:129-134`의 blocking 기본 linger `-1`은 이번 delta로 변경되지 않았다. `test_mesh_peer_admission.cpp`의 1500ms 회피 패턴도 그대로다(코드 미변경 확인) |

TSAN 2-process admission 3건 실패(round-robin·MIXED·reconnect)는 과제 지시와
manifest §4·iteration-5 manifest §4가 이미 baseline `472f66a32`(수정 미포함)
재현으로 delta-무관을 확정한 사항이며, 이번 iteration은 delta가 코드 변경
1줄(spec 문구 제외)뿐이라 재현 조건이 달라질 이유가 없어 재실행하지
않았다. 이 결정을 명시적으로 밝힌다.

## 6. 최종 판정

blocker 0, high 0, medium 1(CS6-I1-01) — I1이 `NOT CLEAN`이다. I2·I3는
CLEAN이다. clean gate는 blocker·high·medium 0과 세 축 CLEAN을 동시에
요구하므로 충족하지 못한다.

iteration 5의 4개 병합 finding은 전부 실측으로 해소를 확인했다 — coordinator의
수정은 정확했다. 그러나 `slot_base`를 고친 바로 그 자리에서 한 걸음만
옆으로 가면 동일한 결함 계열이 자매 함수 4곳(같은 파일 안 1곳 포함)에
남아 있었다. 이는 이번 iteration의 4개 지점 수정 자체를 반박하는 것이
아니라, 그 수정이 닫아야 했던 계약 위반의 실제 범위가 한 줄보다 넓었다는
뜻이다.

CORE REVIEW NOT CLEAN
