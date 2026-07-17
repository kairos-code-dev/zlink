# S5 Core 구현 독립 리뷰 R1 — iteration 4 최종 전체 pass

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 3 수정 4건: **해소 2건, 미해소 2건**
- `F-I1-01 rejected` 재검토: **반박, high finding 유지**
- 신규 finding: **1건** (`low 1`, I3)
- 현재 유효 finding: **4건** (`high 3`, `low 1`)
- I1 계약 구현 일치: **NOT CLEAN**
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **NOT CLEAN**

## 실행 증거

| 항목 | 결과 |
|---|---|
| HEAD | 시작·종료 모두 `59b3ea9400327e74e80b0f8d74763c89ccfdf141` — manifest와 일치 |
| 시작 scope hash | 631 files, `d9621658e16a04c53c646286f3386d99f6ac28faf3196616eec4d71e51a8e413` — manifest와 일치 |
| 종료 scope hash | 631 files, `d9621658e16a04c53c646286f3386d99f6ac28faf3196616eec4d71e51a8e413` — 시작값·manifest와 일치 |
| delta | `25617130e..59b3ea940`, 10 files, +742/−21 — manifest와 일치, `git diff --check` clean |
| 실제 소스 delta | `mesh_actor_api.cpp` +3/−1, `mesh_c_internal.hpp` +0/−1, `mesh_messaging_api.cpp` +28/−19, `mesh_stream_session_api.cpp` +19/−0 |
| 공개 표면 | `check_public_surface.py`: PASS, 196 exports 정확 일치·제거 identifier 없음. `unittest_public_contract_headers`: 1/1 PASS |
| CMake·패키징·workflow | mesh runtime/wire 5 TU와 test 5파일 등록 확인(`core/CMakeLists.txt:886-890`, `core/tests/CMakeLists.txt:93-97`). build/release workflow와 `conandata.yml` YAML parse 성공 |
| mesh 실행 | 현재 regular mesh 바이너리 5개, 36 case 모두 sandbox TCP bind 제한으로 start result 705에서 본문 진입 전 실패. pass·결함 증거로 계산하지 않음 |
| 기존 suite·sanitizer | manifest의 85/85·ASAN clean·TSAN 기존 2계열은 기존 증거로만 기록. 이번 pass에서 재빌드·sanitizer 재실행하지 않음 |

scope hash는 iteration 1 manifest의 다음 정의를 시작과 종료에 그대로 사용했다.

```bash
files=$(git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md | sort -u)
printf '%s\n' "$files" | xargs sha256sum | sha256sum
```

## Iteration 3 수정 4건 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| F-I1-03(재재) | **미해소 (high)** | 새 binding을 넣은 호출만 사후 Actor를 재검증한다(`core/src/api/mesh/mesh_stream_session_api.cpp:685-702`). 그 사이 같은 binding을 본 다른 호출은 `idempotent`로 분류되고(`:634-655`) 재검증을 건너뛴다. 두 호출이 destroy 전 검증을 마친 뒤 destroy의 제거 pass 다음에 A가 삽입하고 B가 이를 idempotent로 성공 처리한 다음 A가 stale rollback하면, destroy 뒤 B는 성공했지만 binding은 없다. |
| N3-I1-01 | **미해소 (high)** | record 생성·payload 복사는 remote commit 전에 옮겼다(`core/src/api/mesh/mesh_messaging_api.cpp:844-870`). 그러나 remote commit 뒤 `deque::push_back`과 `set::insert`를 수행한다(`:872-898`). 실제 container는 `std::deque`와 `std::set`이다(`core/src/runtime/services/mesh/mesh_runtime.hpp:132-145,521-525`). 두 연산은 할당 가능하므로 “commit 뒤 무실패 move만” 조건이 성립하지 않는다. |
| N5 | **해소** | node lock 재획득 직후 `owner_it`을 다시 조회한다(`core/src/api/mesh/mesh_actor_api.cpp:677-686`). deadline 분기의 사용(`:689-696`) 전에 stale iterator가 제거됐다. |
| N6/N3-I3-01 | **해소** | orphan UTF-8 comment가 제거되고 `check_name` 뒤에 실제 Node owner 선언이 바로 이어진다(`core/src/api/mesh/mesh_c_internal.hpp:32-40`). |

### F-I1-03(재재) — 이슈·영향·범위·검증

- 이슈·근거: 삽입 호출의 사후 검증은 `!idempotent`일 때만 실행된다(`core/src/api/mesh/mesh_stream_session_api.cpp:685-703`). 이미 Actor를 검증하고 대기하던 두 번째 호출은 첫 호출의 임시 binding을 idempotent로 보고 이 검증을 생략할 수 있다(`:634-675`). 정식 계약은 bind가 Actor generation과 membership epoch를 검증하고 성공 terminal을 정확히 한 번 전달하도록 한다(`core/doc/spec/core/service/05-stream-session.ko.md:106-112`). destroy는 bound session control을 deadline까지 drain한다(`core/doc/spec/core/service/04-actor.ko.md:177-182`).
- 영향: destroy가 끝난 Actor generation에 대해 bind 성공이 반환되지만, 해당 binding은 이미 제거됐거나 즉시 rollback될 수 있다.
- 수정 범위: `mesh_stream_session_api.cpp`와 `mesh_actor_api.cpp`의 Actor 검증, idempotent/new binding 등록, destroy removal 사이 원자적 순서.
- 검증 방향: 두 bind를 초기 Actor 검증 뒤 정지하고 destroy removal을 완료한 다음 A 삽입 → B idempotent 관측 → A stale 재검증 순서를 결정적으로 만들어, 두 호출 모두 성공하지 않고 binding이 남지 않음을 확인한다.

### N3-I1-01 — 이슈·영향·범위·검증

- 이슈·근거: local record 객체는 선구축됐지만(`core/src/api/mesh/mesh_messaging_api.cpp:844-870`), remote leg를 먼저 commit한 뒤 local `records.push_back`과 `ready.insert`를 실행한다(`:872-898`). 선언된 container가 `std::deque`와 `std::set`이므로(`core/src/runtime/services/mesh/mesh_runtime.hpp:132-145,521-525`) 이 단계는 무실패 move가 아니다. frozen 계약은 snapshot의 모든 target 또는 none과 snapshot 전체의 원자적 reserve/commit을 요구한다(`core/doc/spec/core/service/01-mesh-node.ko.md:367-386,460-462`).
- 영향: post-remote 할당 실패가 C ABI 밖으로 예외를 내보내거나 process를 종료할 수 있고, 오류로 처리하더라도 remote만 수신한 partial delivery가 남는다.
- 수정 범위: `mesh_messaging_api.cpp`의 local mailbox/ready-index commit 준비와 remote/local publish commit 경계, 필요한 내부 container seam.
- 검증 방향: remote target 예약 뒤 첫 local record 삽입과 ready-index 삽입에 각각 할당 실패를 주입해 실패 시 전달 0과 public C API 밖으로 예외가 나오지 않음을 확인한다.

## F-I1-01 rejected 재검토

**rejected 판정을 반박하며 high finding을 유지한다.**

§5는 peer drain이 새 snapshot에서 제외되지만 이미 commit한 message를 취소하지 않는다고 규정한다(`core/doc/spec/core/service/01-mesh-node.ko.md:241-254`). 그러나 §7은 publish가 target을 한 번 snapshot하고 `NODROP=1`에서 그 snapshot의 모든 target이 수락하거나 어느 target에도 전달하지 않는다고 별도로 명시한다(`:367-386`). §9도 Logical Multicast가 snapshot 전체의 원자적 reserve/commit을 보장한다고 다시 고정한다(`:460-462`). 따라서 §5는 이미 commit된 개별 message의 회수를 금지할 뿐, all-or-none을 capacity 실패로 제한하거나 이미 만든 snapshot을 사후 축소하도록 허용하지 않는다.

구현은 node lock 아래 `ADMITTED` peer를 `remote_targets`에 넣고 snapshot count를 고정한다(`core/src/api/mesh/mesh_messaging_api.cpp:760-777`). 이후 commit 실패 target을 `unreachable`로 분류하고(`core/src/runtime/services/mesh/mesh_wire.cpp:339-358`), 이미 만든 snapshot count에서 차감한다(`core/src/api/mesh/mesh_messaging_api.cpp:910-929`). 그러므로 코드 주석의 “snapshot 시점에 이미 떠났다”는 설명은 실제 관찰 순서와 다르다.

- 영향: 성공한 NODROP publish가 앞 target에는 전달하고 뒤 snapshotted target은 누락하면서 dropped=0인 더 작은 snapshot을 보고할 수 있다.
- 수정 범위: `mesh_wire.{hpp,cpp}`, `mesh_messaging_api.cpp`와 raw routed probe seam의 remote NODROP snapshot·reserve/commit·detail/event 회계.
- 검증 방향: admitted target 2개를 snapshot한 뒤 두 번째 pipe를 reserve와 commit 사이에 제거해, public 결과와 회계가 all-target-or-none 조항과 committed-message 취소 불가 조항을 동시에 어떻게 만족하는지 검증한다.

## 전체 campaign scope P4 재검토

현재 snapshot을 기준으로 다음 범위를 처음부터 다시 대조했다.

- mesh runtime 8파일: runtime state, wire codec, admission, ingress, outbound
- mesh API 11파일: lifecycle, dispatch, messaging, Actor/transfer, monitor, STREAM session
- timer 2파일: scheduler 수명, destroy, Spot turn과 MeshNode별 scheduler
- `socket_base` routed probe와 ROUTER multipart commit
- `core/include/zlink/` 공개 header closure와 196-export contract gate
- `test_mesh_*` 5파일과 `core/tests/contract/`, CMake 등록
- Conan recipe·`conandata.yml`·release workflow·build workflow
- `core/doc/internals/`와 `CHANGELOG.md`

공개 surface, 제거 identifier, wire/test CMake 연결, package/workflow syntax, 0-byte·tracked artifact에는 신규 결함을 찾지 못했다. 다만 앞의 I1 3건과 아래 internals 정리 결함이 남는다.

## 신규 finding

| ID | 축 | 심각도 | 이슈·근거 | 영향 | 수정 범위 | 검증 방향 |
|---|---|---|---|---|---|---|
| N4-I3-01 | I3 | **low** | POSD module map은 ingress·codec·admission을 여전히 단일 `mesh_wire.cpp/hpp` 책임으로 적는다(`core/doc/internals/posd-module-structure.ko.md:124-140`). architecture source tree도 두 파일만 열거한다(`core/doc/internals/architecture.ko.md:1218-1227`). 실제 구현은 `mesh_wire`, `mesh_wire_codec`, `mesh_wire_admission`, `mesh_wire_ingress` 4모듈이며 최신 설명은 `core/doc/internals/services-internals.ko.md:12-23`, 빌드 목록은 `core/CMakeLists.txt:886-890`이다. 영문 mirror도 같은 stale 상태다(`posd-module-structure.md:115-132`, `architecture.md:1249-1258`). | 유지보수자가 wire decoding, peer admission, ingress lifecycle의 실제 owner를 잘못 찾고 POSD 책임 경계를 단일 파일로 오해한다. | 한·영 POSD module-structure와 architecture internals의 mesh module 행·source tree 항목. | `git ls-files core/src/runtime/services/mesh`와 CMake source list를 기준으로 internals inventory를 대조하고 한·영 설명 동등성을 확인한다. |

## Known risk 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order | **수용·추적 유지, 신규 finding 없음** | `_slot_sync` 아래 socket plan 준비·적용 경로가 유지된다(`core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80-116`). 이번 pass에서 TSAN을 재실행하지 않아 해소로 주장하지 않는다. |
| TSAN raw command mailbox ypipe | **수용·추적 유지, 신규 finding 없음** | cpipe send/recv는 `_sync`로 감싼다(`core/src/runtime/core/mailbox.cpp:39-56,89-97`). 기존 warning을 제거했다는 독립 동적 증거가 없어 추적을 유지한다. |
| ctx_term linger | **기존 raw socket risk로 수용·추적 유지** | blocky context의 기본 linger는 -1이다(`core/src/runtime/sockets/common/socket_base.cpp:129-134`). termination은 attached pipe를 종료한다(`core/src/runtime/sockets/common/socket_base_lifecycle.cpp:137-161`). MIXED test의 1500ms 종료 순서(`core/tests/integration/test_mesh_peer_admission.cpp:2035-2039`)는 bounded 종료 증거가 아니다. |

## 축별 판정

### I1 계약 구현 일치 — NOT CLEAN

- `F-I1-01` high: rejected 근거가 §7·§9의 snapshot 전체 all-or-none/atomic reserve-commit 조항을 이기지 못한다.
- `F-I1-03` high: bind 사후 재검증을 건너뛰는 concurrent idempotent-success race가 남는다.
- `N3-I1-01` high: remote commit 뒤 local container allocation 가능성이 남는다.

### I2 POSD·DDD — CLEAN

- finding 없음.
- runtime/wire/API/timer/socket 책임 분리는 코드에서 유지된다. I1 concurrency 결함은 중복 I2 finding으로 세지 않았고, stale module 문서는 I3에 분류했다.

### I3 정리 완결성 — NOT CLEAN

- `N4-I3-01` low: 두 한·영 internals 쌍의 mesh wire 분할 inventory가 실제 4모듈 구조와 다르다.
- N5 iterator repair와 N6 comment cleanup은 해소됐다. 공개 surface, 제거 identifier, CMake, packaging/workflow, tracked artifact와 diff hygiene에는 추가 finding이 없다.

## 최종 판정

blocker는 없지만 high finding 3건이 남고 I1·I3가 `NOT CLEAN`이다. 따라서 최종 clean gate를 충족하지 못한다.

CORE REVIEW NOT CLEAN
