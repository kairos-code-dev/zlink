# S5 Core 구현 독립 리뷰 R1 — iteration 7 전체 pass

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 6 병합 finding 4건: **해소 2건, 부분 해소 2건**
- 전체 scope finding: **3건** (`high 1`, `medium 1`, `low 1`)
- I1 계약 구현 일치: **NOT CLEAN**
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **NOT CLEAN**

## 실행 증거

| 항목 | 결과 |
|---|---|
| snapshot | `HEAD == f8c35e6fecbe0c8ec13a7cac0e7fffbd24218f0d`, manifest candidate와 일치 |
| scope | 631 files, manifest와 일치 |
| 시작 scope hash | `cdbc1b1053c4931d0610968d640c133b5ea9b07964a2a57cd6d379c6f2478af6` — manifest와 일치 |
| 종료 scope hash | `cdbc1b1053c4931d0610968d640c133b5ea9b07964a2a57cd6d379c6f2478af6` — 시작값·manifest와 일치 |
| iteration 7 delta | `b1e6c81fb..f8c35e6fe`, scope 안 11 files, +250/−98, `git diff --check` clean |
| 전체 S5 campaign delta | `8206fd44d..f8c35e6fe`, scope 안 46 files, +3961/−1899, `git diff --check` clean |
| 공개 surface | `check_public_surface.py`: **PASS**, formal function 196개와 export 일치, 제거 identifier 없음 |
| configured suite | `ctest --test-dir core/build -N`: **85 targets**. 동적 실행은 하지 않았고 manifest의 기존 85/85·ASAN·TSAN 결과를 재사용했다. |
| runner inventory | peer admission 12, monitor matrix 6, stress 3, lifecycle contracts 10 case |
| 정적 hygiene | 지정 scope 631 files를 목록화했다. 0-byte·merge marker·금지 문구 no-hit, CMake의 mesh wire 4 TU와 mesh test 5개 연결, 한·영 formal C block/public export gate PASS다. |
| 동적 제약 | 이번 pass의 주 증거는 정적 대조다. TCP bind가 필요한 test는 재실행하지 않았고 sandbox 705를 finding 근거로 사용하지 않았다. |

scope hash는 시작과 종료에 지시된 다음 명령을 그대로 사용했다.

```bash
git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md \
  | xargs sha256sum | sha256sum
```

## Iteration 6 병합 finding 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| N6-I1-02 (high) | **부분 해소** | shutdown은 drain wait부터 unlocked emit/completion 또는 `wire_stop`·state event 꼬리까지 `shutdown_active`를 유지하고 마지막에 해제한다(`core/src/api/mesh/mesh_node_api.cpp:328-411`). destroy도 플래그를 검사해 `ZLINK_CLOSE_BUSY`/`EDEADLK`를 반환하며 child 검사와 강제 종료를 한 lock 구간에서 수행하고 unregister를 wire teardown보다 앞에 둔다(`:427-476`). errno map 한·영 close 표도 `EDEADLK`를 포함한다(`core/doc/spec/core/04-errno-map.ko.md:98`, `core/doc/spec/core/04-errno-map.md:93`). 신규 test는 active claim으로 shutdown이 대기한 뒤 destroy 거부·node 생존·shutdown/destroy 성공을 확인한다(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:566-602`). 그러나 shutdown이 registry 검증을 통과하고 node mutex를 얻기 전 destroy가 mutex 구간·unregister·delete를 먼저 끝내는 역순 창은 남는다. `N7-I1-01`로 계속한다. |
| N6-I1-01 + CS6-I1-01 (medium) | **부분 해소** | `publish_common`의 이름·snapshot·record·slot·remote envelope 구간, local/actor/session record 구축, copy helper 2종과 `admit_record`·monitor queue에 `bad_alloc`/ENOMEM 처리가 추가됐다(`core/src/api/mesh/mesh_messaging_api.cpp:52-60`, `:741-798`, `:867-955`; `core/src/api/mesh/mesh_actor_api.cpp:1321-1350`; `core/src/api/mesh/mesh_stream_session_api.cpp:229-249`, `:917-943`; `core/src/runtime/services/mesh/mesh_runtime.cpp:665-680`, `:744-768`). 그러나 public submit의 validation, target preparation, operation/reply map, remote wire, 일부 caller result 변환에는 미매핑 allocation이 남는다. `N7-I1-02`로 계속한다. |
| N6-I1-03 (medium) | **해소** | active monitor callback close는 `ZLINK_CLOSE_BUSY`와 `errno == EBUSY`를 반환한다(`core/src/api/mesh/mesh_monitor_api.cpp:136-158`). close 정식 표의 active callback 매핑과 일치한다(`core/doc/spec/core/04-errno-map.ko.md:98`). |
| N6-I3-01 (low) | **해소** | 한·영 internals가 `mesh_api.cpp` seam이 Spot timer registry·cancellation과 `timer_turn_active`·`timer_count` 상태를 직접 소유·변경한다고 명시한다(`core/doc/internals/services-internals.ko.md:23-33`, `core/doc/internals/services-internals.md:24-34`). |

## I1 계약 구현 일치 — NOT CLEAN

### N7-I1-01 — shutdown의 handle 검증과 mutex 획득 사이에 destroy가 node를 삭제할 수 있음 (high)

- 이슈·근거: shutdown은 `as_mesh_node()`로 live registry를 검사한 뒤 별도 단계에서 node mutex를 획득한다(`core/src/api/mesh/mesh_node_api.cpp:310-328`). destroy도 같은 방식으로 pointer를 얻고 mutex 구간을 마친 뒤 unregister, wire teardown과 `delete`를 수행한다(`:415-476`). `as_mesh_node()`는 registry mutex 아래 membership만 검사하고 raw pointer를 반환하며 호출 수명 pin을 만들지 않는다(`core/src/runtime/services/mesh/mesh_runtime.cpp:395-405`). 따라서 shutdown이 `:312`를 통과한 뒤 `:318` 전에 정지하고 destroy가 `:421-476`을 완료하면, shutdown은 해제된 mutex/node에 접근한다. 이 순서에서는 destroy가 lock을 잡을 때 `shutdown_active`가 아직 false이므로 `:433-435` guard도 동작하지 않는다. 정본은 같은 handle의 shutdown/destroy 재진입을 `EDEADLK`로 종료해야 한다(`core/doc/spec/core/service/01-mesh-node.ko.md:506-513`; 영문 `core/doc/spec/core/service/01-mesh-node.md:556-564`). 신규 test는 shutdown이 이미 대기해 플래그가 설정된 순서만 만든다(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:577-595`).
- 영향: 반대 admission 순서의 concurrent shutdown/destroy에서 use-after-free, 해제된 mutex 접근, hang 또는 process corruption이 가능하며 정식 `EDEADLK` lifecycle 결과를 보장하지 못한다.
- 수정 범위: MeshNode live-handle 검증부터 lifecycle mutex admission·destroy teardown 완료까지의 수명 소유와 shutdown/destroy public result/errno 경계.
- 검증 방향: shutdown이 live-handle 검증을 통과한 직후 mutex 획득 전에 정지하도록 제어한 뒤 destroy를 완료시키는 역순 interleaving을 반복해 `EDEADLK`, node 미삭제와 단일 최종 destroy를 확인한다. active claim과 timeoutless operation 두 대기 원인 및 ASAN·TSAN 반복을 포함한다.

### N7-I1-02 — submit-family의 필요한 storage 실패가 아직 OUT_OF_MEMORY로 전수 매핑되지 않음 (medium)

- 이슈·근거: 정본은 필요한 storage 확보 실패를 `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`으로 정하고 MeshNode·Spot·Actor·STREAM submit family에 적용한다(`core/doc/spec/core/04-errno-map.ko.md:24-45`; 영문 `core/doc/spec/core/04-errno-map.md:22-41`). 그러나 공통 metadata 검증은 `std::set<std::string>`과 string 삽입을 수행하면서 allocation 장벽이 없고(`core/src/runtime/services/mesh/mesh_runtime.cpp:490-549`), node/channel target 선택의 candidate vector·round-robin map과 name storage도 장벽 밖이다(`core/src/api/mesh/mesh_messaging_api.cpp:101-133`, `:395-408`). `register_operation`의 unordered-map 삽입도 장벽 없이 모든 request submit에서 사용된다(`core/src/api/mesh/mesh_node_api.cpp:66-80`). remote node/Spot/Actor wire submit은 envelope·metadata vector를 구성하지만 public caller 또는 wire 함수에 장벽이 없다(`core/src/runtime/services/mesh/mesh_wire.cpp:215-276`, `:363-395`; 호출 `core/src/api/mesh/mesh_messaging_api.cpp:273-325`, `:628-648`, `core/src/api/mesh/mesh_actor_api.cpp:1292-1310`). local actor submit은 `admit_record()`가 반환한 `ENOMEM`을 default `INTERNAL_ERROR`로 바꾼다(`core/src/api/mesh/mesh_actor_api.cpp:1377-1395`). bound-session complete coalescing의 message allocation 실패도 errno를 ENOMEM으로 둔 채 `ZLINK_SUBMIT_INTERNAL_ERROR`를 반환한다(`core/src/api/mesh/mesh_stream_session_api.cpp:190-210`, 공개 호출 `:1037-1103`). public OOM 결과를 직접 검증하는 mesh fault-injection test는 없고 enum 변환 unit assertion만 존재한다(`core/tests/unittest/unittest_result_enum_mapping.cpp:158-159`).
- 영향: allocation 지점에 따라 C ABI 밖으로 `std::bad_alloc`이 전파되거나 process가 종료되고, 또는 같은 ENOMEM이 `INTERNAL_ERROR`로 관측된다. request bookkeeping allocation이 실패하면 operation serial 등 mutation이 일부 진행된 상태에서 공개 결과를 반환하지 못할 수 있다.
- 수정 범위: MeshNode·Spot·Actor·STREAM public submit의 validation, target snapshot, operation/reply bookkeeping, local admission caller mapping과 remote wire envelope를 포함한 전체 allocation 경계 및 OOM contract test.
- 검증 방향: 각 allocation class에 deterministic failure를 주입해 `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`, C ABI 밖 예외 0, delivery 0, operation·reply route·mailbox·binding pending mutation 0을 확인한다. local/remote, send/request, publisher, Actor와 STREAM session 경로를 각각 포함한다.

## I2 POSD·DDD — CLEAN

- finding 없음.
- public C contract는 transport·wire storage를 노출하지 않고, API mapping, Mesh aggregate state, wire codec/admission/ingress/outbound 책임이 서로 다른 추상화로 유지된다(`core/doc/internals/services-internals.ko.md:13-33`, `core/src/runtime/services/mesh/mesh_wire_internal.hpp:12-18`, `core/CMakeLists.txt:878-890`). iteration 7은 public function을 늘리지 않았고 formal function 196개와 export가 계속 일치한다.
- I1의 lifetime/OOM 계약 결함과 반복된 예외 장벽은 독립 I2 finding으로 중복 계산하지 않았다.

## I3 정리 완결성 — NOT CLEAN

### N7-I3-01 — CHANGELOG lifecycle contract case 수가 runner와 불일치 (low)

- 이슈·근거: release-candidate verification은 `test_mesh_lifecycle_contracts`를 9 cases로 기록한다(`CHANGELOG.md:56-67`). 실제 main은 신규 `test_destroy_during_shutdown_wait_is_deadlock_error`를 포함해 `RUN_TEST` 10개를 등록한다(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:777-786`).
- 영향: release 검증 inventory가 candidate `f8c35e6fe`의 실제 lifecycle 회귀 범위를 한 건 적게 보고해 이후 reviewer와 release 소비자가 신규 shutdown/destroy 검증 포함 여부를 잘못 판단할 수 있다.
- 수정 범위: `CHANGELOG.md`의 release-candidate lifecycle test inventory와 신규 case 설명.
- 검증 방향: configured target 85개와 네 runner의 실제 `RUN_TEST` 수(12·6·3·10)를 다시 산출해 CHANGELOG의 수치·설명이 정확히 일치하는지 확인한다.

그 밖의 정리 gate는 clean이다. 지정 scope 631 files에서 0-byte·merge marker·금지 문구가 없고, 공개 C block/header/export 196개가 일치하며 제거 identifier도 없다. mesh wire 4 TU와 mesh test 5개는 CMake에 연결되어 있다(`core/CMakeLists.txt:878-890`, `core/tests/CMakeLists.txt:93-97`).

## Known risk 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order | **기존 risk 수용·추적 유지, 신규 S5 finding 없음** | `_slot_sync`를 잡은 상태에서 socket plan prepare/apply를 호출한다(`core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80-116`). 현재 정적 대조로 반대 lock order의 실재나 iteration 7 delta 연관성을 확정하지 못했다. |
| TSAN raw command mailbox ypipe | **기존 risk 수용·추적 유지, 신규 S5 finding 없음** | command pipe write/flush와 read는 `_sync`로 직렬화된다(`core/src/runtime/core/mailbox.cpp:39-56`, `:64-97`). 기존 TSAN 관찰의 해소를 정적 코드만으로 확정하지 않아 추적을 유지한다. |
| raw socket teardown (`pipe_t::detach_peer_backref`·asio `blob_t`) | **9.x raw 기계 risk 수용·추적 유지, mesh finding 아님** | pipe term ack는 peer backref를 끊은 뒤 sink 종료를 알린다(`core/src/runtime/core/pipe.cpp:704-730`). asio error 경로는 session routing-id `blob_t` view로 disconnect event를 낸 뒤 engine error·unplug·deferred destroy를 진행한다(`core/src/runtime/engine/asio/asio_engine.cpp:1845-1862`). 동적 수명 관찰을 정적으로 해소됐다고 판정하지 않는다. |
| ctx_term linger | **기존 risk 수용·추적 유지** | blocky context의 socket 기본 linger는 `-1`이다(`core/src/runtime/sockets/common/socket_base.cpp:129-134`). termination은 attached pipe를 종료하고 ack를 기다린다(`core/src/runtime/sockets/common/socket_base_lifecycle.cpp:137-161`). bounded 종료 증거가 없어 기존 risk로 유지한다. |

## 최종 판정

blocker는 없지만 high 1건, medium 1건, low 1건이 남았다. I1과 I3가 `NOT CLEAN`이므로 clean gate를 충족하지 못한다.

CORE REVIEW NOT CLEAN
