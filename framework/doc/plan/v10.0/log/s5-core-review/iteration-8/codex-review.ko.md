# S5 Core 구현 독립 리뷰 R1 — iteration 8 전체 pass

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 7 수정 4건: **해소 3건, 부분 해소 1건**
- 전체 scope finding: **1건** (`medium 1`)
- I1 계약 구현 일치: **NOT CLEAN**
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **CLEAN**

## 실행 증거

| 항목 | 결과 |
|---|---|
| snapshot | `HEAD == ee8036a09e951e89db5730426d6a91a44afdac85`, manifest candidate와 일치 |
| scope | 631 files, manifest와 일치 |
| 시작 scope hash | `269b6c1b17aab31c4b74979d2eb8c61482ce102d49da63de3f06c2cba7c632ff` — manifest와 일치 |
| 종료 scope hash | `269b6c1b17aab31c4b74979d2eb8c61482ce102d49da63de3f06c2cba7c632ff` — 시작값·manifest와 일치 |
| iteration 8 delta | `f8c35e6fe..ee8036a09`, scope 안 10 files, +548/−62, `git diff --check` clean |
| 공개 surface | `contract_public_surface`: **PASS**, formal function 196개와 export 일치, 제거 identifier 없음 |
| configured suite | `ctest --test-dir core/build -N`: **85 targets** |
| runner inventory | peer admission 12, monitor matrix 6, stress 3, lifecycle contracts 12 case. CHANGELOG의 12 case와 일치(`CHANGELOG.md:56-68`, `core/tests/integration/test_mesh_lifecycle_contracts.cpp:865-881`). |
| 정적 hygiene | 지정 scope 631 files에서 0-byte·merge marker·금지 문구 no-hit. Mesh API/runtime 13 TU와 mesh test 5개가 CMake에 연결되어 있다(`core/CMakeLists.txt:878-890`, `core/tests/CMakeLists.txt:93-97`). |
| 집중 동적 실행 | `contract_public_surface`는 1/1 통과. `test_mesh_lifecycle_contracts`는 12 case 모두 bind 단계의 `705`로 실행되지 못했다. sandbox TCP bind 제약에 따른 증거 부재로만 기록하며 finding 근거로 사용하지 않았다. manifest의 기존 85/85·ASAN·TSAN 결과는 보조 증거로만 사용했다. |

scope hash는 시작과 종료에 지시된 다음 명령을 그대로 사용했다.

```bash
git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md \
  | xargs sha256sum | sha256sum
```

## Iteration 7 수정 4건 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| N7-I1-01 (high) | **해소** | shutdown은 registry mutex 아래 live membership·tag·destroy claim을 검사하고 lifecycle pin을 올린다(`core/src/runtime/services/mesh/mesh_runtime.cpp:425-448`). 모든 shutdown 반환은 RAII guard로 unpin되며(`core/src/api/mesh/mesh_node_api.cpp:330-354`), destroy는 registry 아래 배타 claim을 잡고(`core/src/runtime/services/mesh/mesh_runtime.cpp:459-480`) commit 뒤 live registry에서 제거한 다음 pin 0까지 기다린 후에만 wire teardown·delete한다(`core/src/runtime/services/mesh/mesh_runtime.cpp:489-500`, `core/src/api/mesh/mesh_node_api.cpp:516-525`). 따라서 shutdown pin 선행, destroy claim 선행, shutdown_active 선행과 destroy 완료 뒤 호출의 네 순서에서 node storage를 pin 없이 다시 만지는 창이 없다. test-only pause는 pin과 node mutex 사이를 연다(`core/src/api/mesh/mesh_node_api.cpp:316-360`), 신규 case는 그 동안 destroy가 진입해 pin 해제를 기다리고 양 호출 완료와 이후 EFAULT를 확인한다(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:574-603`). 이번 sandbox에서는 705로 동적 재확인하지 못했으나 정적 interleaving 대조로 기존 high 창은 닫혔다. |
| N7-I1-02 (medium) | **부분 해소** | `complete_operation`은 nothrow 생성과 `bad_alloc` 장벽을 갖고(`core/src/runtime/services/mesh/mesh_runtime.cpp:960-1010`), ingress loop도 메시지 단위 장벽을 둔다(`core/src/runtime/services/mesh/mesh_wire_ingress.cpp:1054-1088`). actor admission ENOMEM은 `OUT_OF_MEMORY`로 매핑되고(`core/src/api/mesh/mesh_actor_api.cpp:1419-1438`), STREAM complete 할당 실패도 같은 결과다(`core/src/api/mesh/mesh_stream_session_api.cpp:190-210`). 그러나 공개 mesh submit 반환 함수는 27개인데 function-try-block은 25개뿐이며 두 request 진입점이 빠졌다. 아래 `N8-I1-01`로 계속한다. |
| N7-C1 | **해소** | 정본은 성공한 해제가 이미 시작한 callback 반환 뒤 완료되고 handler 내부 해제만 EDEADLK라고 규정한다(`core/doc/spec/core/service/02-dispatch.ko.md:172-177`). 구현은 callback thread id를 기록하고(`core/src/runtime/services/mesh/mesh_runtime.cpp:684-693`), 같은 thread 재진입만 `ZLINK_HANDLER_DEADLOCK`으로 반환하며 다른 thread는 depth 0까지 조건변수로 기다린다(`core/src/api/mesh/mesh_dispatch_api.cpp:177-198`). |
| N7-I3-01 (low) | **해소** | 실제 lifecycle runner는 12개 case를 등록한다(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:865-881`). CHANGELOG도 12 case와 신규 lifecycle 양 순서·OOM mapping을 기록한다(`CHANGELOG.md:56-68`). |

## I1 계약 구현 일치 — NOT CLEAN

### N8-I1-01 — 공개 mesh submit OOM 장벽과 실패 원자성이 전수 적용되지 않음 (medium)

- **이슈·근거:** 정본은 필요한 storage 실패를 `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM`으로 규정하고 MeshNode·Spot·Actor·STREAM submit에 적용한다(`core/doc/spec/core/04-errno-map.ko.md:24-45`). 실제 네 Mesh API 파일의 공개 `zlink_submit_result_t zlink_*` 정의는 27개지만 `try {` function body는 25개다. 줄바꿈된 두 선언 `zlink_mesh_node_request_to_channel`은 평범한 body로 공통 channel 경로를 호출하고(`core/src/api/mesh/mesh_messaging_api.cpp:500-517`), `zlink_stream_session_request_to_actor`도 같은 형태로 session 경로를 호출한다(`core/src/api/mesh/mesh_stream_session_api.cpp:1039-1056`). 첫 경로는 string·candidate vector·round-robin map과 operation map을 사용할 수 있고(`core/src/api/mesh/mesh_messaging_api.cpp:398-425`, `core/src/api/mesh/mesh_node_api.cpp:69-85`), 둘째 경로도 metadata validation, session key string과 request operation/reply map을 사용한다(`core/src/api/mesh/mesh_stream_session_api.cpp:855-897`, `:959-983`). 이 할당의 `std::bad_alloc`은 두 공개 C 함수 밖으로 나간다. 장벽이 있는 local request도 operation을 먼저 등록한 뒤 reply route를 삽입하므로 reply route 할당이 실패하면 outer catch 전에 만든 operation을 지우는 경로가 없다(`core/src/api/mesh/mesh_messaging_api.cpp:215-236`). generic reply도 route를 소비하고 local operation을 제거한 뒤 reply vector를 할당한다(`core/src/api/mesh/mesh_dispatch_api.cpp:842-875`); 정본이 허용하는 것은 한 번의 **성공한** reply이며(`core/doc/spec/core/service/02-dispatch.ko.md:282-297`), 이 순서에서는 OOM 실패가 retry 가능한 token과 requester operation을 이미 잃는다. 신규 fault test는 send record 준비와 publish 준비 두 번만 fault를 arm한다(`core/tests/integration/test_mesh_lifecycle_contracts.cpp:609-648`). `register_operation`의 별도 fault point(`core/src/api/mesh/mesh_node_api.cpp:69-85`)와 두 누락 request 진입점, reply-route 실패는 검증하지 않는다.
- **영향:** 두 request API에서는 storage 부족이 정식 result/errno로 봉인되지 않고 C ABI 밖 C++ 예외 또는 process 종료로 관측될 수 있다. 나머지 request/reply 경로도 할당 실패 시 caller에게 operation ID나 성공을 반환하지 않은 채 orphan operation, 소비된 reply token 또는 누락 completion을 남길 수 있다.
- **수정 범위:** 공개 mesh submit/reply 진입점 전체 inventory, channel·STREAM request의 C ABI OOM 경계, operation/reply-route 등록과 reply 소비의 실패 원자성, OOM contract test 범위.
- **검증 방향:** 27개 공개 `zlink_submit_result_t` 진입점을 선언 형태와 무관하게 전수 열거한다. validation·target selection·operation 등록·reply-route 등록·remote wire envelope·reply payload 준비 각각에 결정적 allocation failure를 주입해 `OUT_OF_MEMORY`/`ENOMEM`, C ABI 밖 예외 0, 실패 뒤 operation·reply route·mailbox mutation 0, token retry 가능성과 다음 정상 호출 성공을 확인한다.

## I2 POSD·DDD — CLEAN

- finding 없음.
- public C contract는 storage·wire·registry 결정을 노출하지 않고 API mapping, Mesh aggregate state, wire codec/admission/ingress/outbound 책임을 분리한다(`core/doc/internals/services-internals.ko.md:10-33`, `core/CMakeLists.txt:878-890`). lifecycle pin은 registry가 handle 수명을 흡수하고 ready handler thread 판정은 dispatch/runtime 내부 상태로 유지되어 caller 복잡성을 늘리지 않는다(`core/src/runtime/services/mesh/mesh_runtime.hpp:521-554`).
- I1의 누락된 OOM 장벽과 실패 원자성은 계약 구현 finding으로 계산했다. 별도의 public abstraction 추가, transport 정보 누출, 병렬 상태 owner 또는 새 POSD·DDD finding은 확인하지 못했다.

## I3 정리 완결성 — CLEAN

- finding 없음.
- CHANGELOG의 lifecycle 12 case가 실제 runner와 일치하고(`CHANGELOG.md:56-68`, `core/tests/integration/test_mesh_lifecycle_contracts.cpp:865-881`), scope 631 files의 0-byte·merge marker·금지 문구 no-hit, `git diff --check` clean, CMake source/test 연결과 formal public surface 196개 일치를 확인했다.
- OOM test coverage 부족은 `N8-I1-01`의 계약 검증 범위로 포함했으며 독립 I3 finding으로 중복 계산하지 않았다.

## Known risk 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order | **기존 risk 수용·추적 유지, 신규 S5 finding 없음** | `_slot_sync`를 잡은 채 socket plan prepare/apply를 호출한다(`core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80-116`). 현재 정적 대조만으로 반대 lock order의 실재 또는 iteration 8 delta 연관성을 확정할 수 없어 기존 동적 관찰을 해소하지 않는다. |
| TSAN raw command mailbox ypipe | **기존 risk 수용·추적 유지, 신규 S5 finding 없음** | command pipe write/flush와 read는 `_sync`로 직렬화된다(`core/src/runtime/core/mailbox.cpp:39-56`, `:64-97`). 기존 TSAN 관찰의 원인을 정적으로 제거됐다고 확정할 근거가 없어 추적을 유지한다. |
| raw socket teardown (`pipe_t::detach_peer_backref`·asio `blob_t`) | **9.x raw 기계 risk 수용·추적 유지, mesh finding 아님** | pipe term ack는 peer backref를 끊은 뒤 sink 종료를 알린다(`core/src/runtime/core/pipe.cpp:704-730`). asio error 경로는 session routing-id `blob_t` view로 disconnect event를 낸 뒤 engine error·unplug·deferred destroy를 진행한다(`core/src/runtime/engine/asio/asio_engine.cpp:1845-1866`). 동적 수명 관찰을 정적으로 해소됐다고 판정하지 않는다. |
| ctx_term linger | **기존 risk 수용·추적 유지** | blocky context의 socket 기본 linger는 `-1`이다(`core/src/runtime/sockets/common/socket_base.cpp:129-134`). termination은 attached pipe를 종료하고 ack를 등록한 뒤 종료 기계를 진행한다(`core/src/runtime/sockets/common/socket_base_lifecycle.cpp:137-161`). bounded 종료 증거가 없어 기존 risk로 유지한다. |

## 최종 판정

blocker와 high는 없지만 medium 1건이 남아 I1이 `NOT CLEAN`이다. I2와 I3는 `CLEAN`이지만 clean gate를 충족하지 못한다.

CORE REVIEW NOT CLEAN
