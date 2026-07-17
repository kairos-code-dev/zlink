# S5 Core 구현 독립 리뷰 R1 — iteration 5 전체 pass

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 4 finding 4건: **해소 3건, 부분 해소·미해소 1건**
- 신규 finding: **3건** (`medium 1`, `low 2`)
- 현재 유효 finding: **4건** (`high 1`, `medium 1`, `low 2`)
- I1 계약 구현 일치: **NOT CLEAN**
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **NOT CLEAN**

## 실행 증거

| 항목 | 결과 |
|---|---|
| HEAD | 시작·종료 모두 `c8d567c644e06de6e4a477fc960dd1c8a9e2a097` — manifest와 일치 |
| 시작 scope hash | 631 files, `53ae8e44f5085109c684e81423c88342456d4b1ded9fc54c234a226d14b4c140` — manifest와 일치 |
| 종료 scope hash | 631 files, `53ae8e44f5085109c684e81423c88342456d4b1ded9fc54c234a226d14b4c140` — 시작값·manifest와 일치 |
| iteration 5 delta | `59b3ea940..c8d567c64`, 18 files, +415/−46, `git diff --check` clean |
| 전체 S5 campaign delta | `8206fd44d..c8d567c64`, scope 안 44 files, +3696/−1815, `git diff --check` clean |
| 공개 surface | `check_public_surface.py`: **PASS**, 196 functions/exports 일치, 제거 identifier 없음, 한·영 C block과 struct field 일치. gate가 수행하는 항목은 `core/tests/contract/check_public_surface.py:4-17`에 고정돼 있다. |
| header contract | 기존 `unittest_public_contract_headers`: 1/1 PASS. 다만 바이너리 timestamp가 이번 source보다 오래돼 신규 필드의 독립 build 증거로 세지 않았다. |
| 정적 hygiene | 14 header·366 source·149 test·23 packaging·50 spec·27 internals·상위 2파일을 전부 목록화했다. 0-byte·merge marker 없음. 제거 pattern은 production/header/spec/internals에서 no-hit이고 `CHANGELOG.md:38-46`의 제거 이력만 의도적으로 남는다. |
| CMake·package | mesh wire 4 TU가 `core/CMakeLists.txt:886-890`, mesh test 5개가 `core/tests/CMakeLists.txt:93-97`에 등록돼 있다. `conandata.yml`은 YAML parse에 성공했다. |
| 동적 실행 | 신규 test 바이너리가 현재 source보다 오래돼 재실행하지 않았다. manifest의 85/85·ASAN·TSAN 결과(`manifest.ko.md:48-58`)는 기존 증거로만 기록했다. sandbox TCP bind 제약은 finding으로 사용하지 않았다. |

scope hash는 지시된 다음 명령을 시작과 종료에 동일하게 사용했다.

```bash
git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md \
  | xargs sha256sum | sha256sum
```

## Iteration 4 finding 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| F-I1-03(재재재) | **해소** | insert와 idempotent를 가르던 gate가 제거돼 두 성공 형태가 모두 Actor 존재·generation·draining을 사후 재검증한다(`core/src/api/mesh/mesh_stream_session_api.cpp:685-704`). stale이면 해당 generation binding을 제거하고 `ESTALE`로 실패한다(`:700-703`). 기존 반례의 idempotent 우회가 닫혔다. |
| N3-I1-01(재) | **핵심 partial-delivery 결함 해소** | remote commit 전에 local record를 만들고(`core/src/api/mesh/mesh_messaging_api.cpp:844-871`), deque placeholder와 ready key를 선예약한다(`:873-905`). remote 실패는 unlock 전에 둘을 rollback한다(`:908-930`). commit 뒤에는 counter와 선예약 slot 대입만 남는다(`:933-940`). 다만 새 보조 vector의 OOM 매핑 누락은 신규 `N5-I1-01`로 분리한다. |
| F-I1-01 | **부분 해소·미해소 (high)** | 공개 detail/event field는 header와 정본에 일치한다(`core/include/zlink/service/mesh_node.h:83-93`, `core/include/zlink/eventing/api.h:263-289`, `core/doc/spec/core/service/01-mesh-node.ko.md:89-99`, `core/doc/spec/core/07-monitoring.ko.md:232-257`). 구현도 true snapshot과 unreachable을 별도 기록한다(`core/src/api/mesh/mesh_messaging_api.cpp:950-974`), wire 주석도 실제 reserve 뒤 commit 전 이탈로 고쳤다(`core/src/runtime/services/mesh/mesh_wire.cpp:339-347`). 그러나 같은 정본 §9의 무조건적인 snapshot 전체 atomic reserve/commit 보장이 남아 아래 coordinator 판정을 수용할 수 없다. |
| N4-I3-01 | **해소** | POSD module map 한·영은 codec/admission/ingress/outbound 4책임을 각각 열거한다(`core/doc/internals/posd-module-structure.ko.md:124-144`, `core/doc/internals/posd-module-structure.md:115-135`). architecture 한·영 source tree도 실제 4 TU와 shared header를 모두 열거한다(`core/doc/internals/architecture.ko.md:1224-1230`, `core/doc/internals/architecture.md:1255-1261`). CMake 목록과 일치한다. |

## 신규 테스트 2건 판정

| 테스트 | 판정 | 근거 |
|---|---|---|
| `test_stream_session_bind_destroy_race_leaves_no_binding` | **부분 충족** | 8라운드에서 bind 2개와 destroy를 겹치고(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:652-671`), destroy 뒤 late bind 실패와 binding 잔존 0을 확인한다(`:673-698`). 이는 iteration 4 ledger가 정한 두 관측(`iteration-4/finding-ledger.ko.md:40-44`)을 충족한다. 그러나 동시 bind 두 호출의 반환값을 버려(`test_mesh_lifecycle_contracts.cpp:659-666`) 원 반례의 “idempotent 호출 성공 뒤 binding rollback” 자체는 test가 실패로 잡지 못한다. 이 검증 공백은 `N5-I3-02`다. |
| `test_nodrop_unreachable_target_accounting` | **충족** | fault를 remote commit send에 주입하고(`core/tests/integration/test_mesh_peer_admission.cpp:1186-1205`), true snapshot과 admitted+dropped+unreachable 불변식, dropped 0, local leg 전달을 확인한다(`:1206-1220`). 실제 pipe teardown 수명은 주입하지 않지만, 정적 호출 경로가 writable reserve 뒤 send 실패를 unreachable로 분류한다(`core/src/runtime/services/mesh/mesh_wire.cpp:320-360`). raw teardown 수명은 별도 known risk로 유지한다. |

## F-I1-01 coordinator 판정

**회계 투명화와 §7의 capacity/peer-departure 구분은 수용하지만, “finding 해소” 판정은 반박한다.**

§5는 peer drain을 새 snapshot에서 제외하고 이미 commit한 message를 취소하지 않는다고 규정한다(`core/doc/spec/core/service/01-mesh-node.ko.md:250-255`). §7은 capacity admission의 all-or-none과 reserve~commit 사이 peer 이탈을 구분하고, latter를 unreachable 성공으로 정의한다(`:381-394`). 이 두 문단만 보면 coordinator 해석은 명시적이다.

그러나 같은 정본 §9는 여전히 Logical Multicast가 “snapshot 전체의 원자적 reserve/commit까지 보장”한다고 제한 없이 규정한다(`core/doc/spec/core/service/01-mesh-node.ko.md:467-469`; 영문 `core/doc/spec/core/service/01-mesh-node.md:512-516`). 실제 commit은 target을 순서대로 submit하고 실패 target을 unreachable로 센 뒤 전체 호출을 성공시킨다(`core/src/runtime/services/mesh/mesh_wire.cpp:348-360`). 따라서 한 target이 commit되고 다음 target이 unreachable인 결과는 §7의 새 예외에는 맞지만 §9의 snapshot 전체 atomic commit에는 맞지 않는다. iteration 4가 지적한 정본 내부 긴장이 문서 전체에서는 끝나지 않았다.

- 이슈: §7의 peer-departure 예외와 §9의 무조건적인 snapshot 전체 atomic commit 보장이 동시에 존재한다.
- 영향: 같은 성공 결과를 한 문단에서는 허용하고 다른 문단에서는 금지해 bindings·contract test가 하나의 관찰 가능한 delivery 계약을 선택할 수 없다.
- 수정 범위: `core/doc/spec/core/service/01-mesh-node.{ko.md,md}`의 Logical Multicast 보장 문구와 그 문구를 검증하는 contract test 범위.
- 검증 방향: remote target 2개 중 reserve 뒤 한 pipe가 사라지는 경우를 기준으로 §7·§9에서 동일한 성공/실패·전달·회계 결과를 도출할 수 있는지 한·영 정본과 public detail/event assertion을 함께 대조한다.

## 전체 scope P4 신규 finding

### N5-I1-01 — local slot 선예약 보조 storage의 OOM이 C result로 매핑되지 않음 (medium)

- 이슈·근거: `slot_base`는 크기 지정 vector 생성으로 storage를 확보하지만 `try` 바깥에 있다(`core/src/api/mesh/mesh_messaging_api.cpp:873-882`). `bad_alloc`을 `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`으로 바꾸는 catch는 그 다음 `ready_added.reserve`, deque push와 set insert만 감싼다(`:882-905`). 정본 errno map은 Mesh publisher를 포함한 submit family의 필요한 storage 확보 실패를 `ZLINK_SUBMIT_OUT_OF_MEMORY`, `ENOMEM`으로 규정한다(`core/doc/spec/core/04-errno-map.ko.md:24-45`).
- 영향: 새 선예약 경로 자체의 allocation 실패가 typed result를 반환하지 않고 C ABI 경계를 넘어 예외를 전파하거나 process 종료로 이어질 수 있다. remote commit 전이므로 iteration 4의 partial-delivery 결함과는 별개다.
- 수정 범위: `publish_common`의 local slot 선예약에 필요한 보조 storage 확보와 public submit error mapping.
- 검증 방향: remote commit 전 `slot_base` storage 확보 실패를 주입해 delivery 0, local placeholder/ready 잔존 0, `ZLINK_SUBMIT_OUT_OF_MEMORY`와 `ENOMEM`, C ABI 밖 예외 0을 확인한다.

### N5-I3-01 — CHANGELOG 검증 수치가 현재 snapshot과 불일치 (low)

- 이슈·근거: `CHANGELOG.md`는 10.0.0 검증을 84/84 CTest와 peer admission 10 case로 기록한다(`CHANGELOG.md:56-64`). 현재 configured suite는 85 target이고 manifest도 85/85를 기록한다(`framework/doc/plan/v10.0/log/s5-core-review/iteration-5/manifest.ko.md:48-52`). 실제 peer admission runner에는 12개의 `RUN_TEST`가 있으며 신규 accounting case도 등록돼 있다(`core/tests/integration/test_mesh_peer_admission.cpp:2348-2361`).
- 영향: release candidate의 공개 검증 기록이 실제 acceptance snapshot보다 이전 상태를 설명해, 소비자와 유지보수자가 신규 regression coverage를 확인할 수 없다.
- 수정 범위: `CHANGELOG.md`의 10.0.0 Verification 항목에 있는 target·case inventory.
- 검증 방향: `ctest -N` target 수와 5개 mesh runner의 `RUN_TEST` 수를 다시 집계해 CHANGELOG의 수치·목록과 일치시키고, 신규 두 case가 설명에 포함되는지 확인한다.

### N5-I3-02 — bind/destroy 신규 test가 동시 bind 결과를 검증하지 않음 (low)

- 이슈·근거: test 설명은 destroyed generation에 성공을 보고하지 않는다고 선언한다(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:603-608`). 그러나 두 binder는 반환값과 errno를 모두 버리고(`:659-666`), assertion은 별도의 late bind와 최종 binding count에만 있다(`:673-698`). iteration 4의 원 반례는 idempotent bind가 성공을 반환한 뒤 다른 호출의 stale rollback으로 binding이 사라지는 형태였다(`framework/doc/plan/v10.0/log/s5-core-review/iteration-4/finding-ledger.ko.md:30-38`).
- 영향: 원 regression이 되살아나도 최종 binding이 0이면 hammer가 통과할 수 있어, test 주석이 주장하는 검증 범위보다 실제 회귀 검출 범위가 좁다.
- 수정 범위: `test_stream_session_bind_destroy_race_leaves_no_binding`의 동시 bind 결과 관측과 race 판정 assertion.
- 검증 방향: destroy removal 뒤 임시 binding을 본 idempotent 호출의 반환 결과와 최종 binding 상태를 함께 관측해, 원 반례의 성공-후-rollback 결과가 test 실패가 되는지 확인한다.

## Known risk 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order 계열 | **기존 risk 수용·추적 유지, 신규 S5 finding 없음** | `_slot_sync`를 보유한 채 socket plan을 prepare/apply한다(`core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80-116`). manifest의 단독 TSAN 경고가 유지된다(`manifest.ko.md:53-54`). 반대 lock ordering을 이번 정적 pass에서 확정하지 못했으므로 별도 구현 finding으로 승격하지 않는다. |
| TSAN raw command mailbox ypipe 계열 | **기존 risk 수용·추적 유지, 신규 S5 finding 없음** | command pipe write/flush와 read는 `_sync`로 감싼다(`core/src/runtime/core/mailbox.cpp:39-56`, `:64-97`). manifest에 기존 경고가 남지만(`manifest.ko.md:53-54`) 현재 정적 코드만으로 원인·해소를 확정할 수 없다. |
| raw socket teardown: `detach_peer_backref` / asio `blob_t` | **9.x raw 기계 risk 수용·추적 유지, mesh delta finding 아님** | manifest는 단독 pipe teardown과 부하 시 asio 경로 관찰을 구분한다(`manifest.ko.md:64-66`). pipe ack 종료가 peer backref를 직접 끊는다(`core/src/runtime/core/pipe.cpp:700-730`), asio error 경로는 session의 routing-id `blob_t` view를 얻어 event를 내보낸 뒤 engine error를 진행한다(`core/src/runtime/engine/asio/asio_engine.cpp:1845-1855`). 신규 raw STREAM test가 이 수명을 노출하지만 mesh 계약 수정 4건의 delta는 아니다. 동적 관찰을 정적으로 해소됐다고 주장하지 않는다. |
| ctx_term linger | **기존 risk 수용·추적 유지** | blocky context에서 socket 기본 linger는 -1이다(`core/src/runtime/sockets/common/socket_base.cpp:129-134`), termination은 attached pipe를 종료한다(`core/src/runtime/sockets/common/socket_base_lifecycle.cpp:137-161`). 신규 accounting test도 child-first 종료를 위해 1500ms를 둔다(`core/tests/integration/test_mesh_peer_admission.cpp:1224-1228`). 이는 bounded 종료 증거가 아니라 기존 회피다. |

## 축별 판정

### I1 계약 구현 일치 — NOT CLEAN

- `F-I1-01` high: §7의 명시적 peer-departure 예외와 §9의 snapshot 전체 atomic commit 보장이 아직 충돌한다.
- `N5-I1-01` medium: 새 local slot 보조 vector의 allocation 실패가 public `OUT_OF_MEMORY/ENOMEM`으로 매핑되지 않는다.
- `F-I1-03`의 두 성공 형태 재검증과 `N3-I1-01`의 post-remote fallible container commit은 코드상 해소됐다.

### I2 POSD·DDD — CLEAN

- finding 없음.
- mesh object model, outbound transport, codec, admission과 ingress 책임은 실제 4모듈과 shared internal contract로 분리돼 있고(`core/doc/internals/posd-module-structure.ko.md:124-144`, `core/CMakeLists.txt:886-890`), public API를 늘리지 않고 기존 raw routed probe를 재사용한다.
- I1의 계약 문구와 OOM mapping 결함은 독립 I2 finding으로 중복 계산하지 않았다.

### I3 정리 완결성 — NOT CLEAN

- `N5-I3-01` low: CHANGELOG의 suite·case 수치가 현재 snapshot과 다르다.
- `N5-I3-02` low: 신규 bind/destroy test가 test 설명의 핵심 동시 bind 결과를 assertion하지 않는다.
- N4 module inventory, 공개 surface, 제거 identifier, CMake 연결, package YAML, 0-byte/merge marker와 diff hygiene에는 추가 finding이 없다.

## 최종 판정

blocker는 없지만 high 1건과 medium 1건이 남고 I1·I3가 `NOT CLEAN`이다. 따라서 clean gate를 충족하지 못한다.

CORE REVIEW NOT CLEAN
