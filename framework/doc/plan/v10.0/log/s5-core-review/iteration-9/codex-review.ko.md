# S5 Core 독립 리뷰 결과

## Snapshot·scope 검증

- 시작/종료 `HEAD`: `f5000d2fe7d2f9f7bc50eaa9ae23c978d3b54e85`
- worktree: 변경 없음
- 시작/종료 scope: 631개
- 시작/종료 aggregate SHA-256: `5eeb7c9010200c38c933d86ad9fa7a8d99d80e16495a0f3015cc74dcbf516255`
- manifest 값과 모두 일치했다.
- 파일 수정은 수행하지 않았다.

## Iteration 8 finding 재판정

| 항목 | 판정 | 근거 |
|---|---|---|
| CS8-I1-01 MeshNode lifetime pin 일반화 | 해소 | 공개 경로가 `mesh_node_pin_t`를 사용하고([mesh_runtime.hpp](/tmp/zlink-s5-i9-codex/core/src/runtime/services/mesh/mesh_runtime.hpp:612)), unregister 뒤 pin 해제를 기다린다([mesh_runtime.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/services/mesh/mesh_runtime.cpp:498)). 기존 `as_mesh_node()`의 API 사용은 handle 분류 한 곳만 남았다. 동시 submit/destroy 검증도 추가됐다([test_mesh_lifecycle_contracts.cpp](/tmp/zlink-s5-i9-codex/core/tests/integration/test_mesh_lifecycle_contracts.cpp:606)). |
| CS8-I1-02 두 submit 함수의 C ABI OOM 장벽 누락 | 해소 | 공개 `zlink_submit_result_t` 함수 27개와 외곽 `bad_alloc` 변환을 대조했다. 줄바꿈된 request 함수에도 장벽이 있다([mesh_messaging_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_messaging_api.cpp:502), [mesh_stream_session_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_stream_session_api.cpp:1043)). |
| CS8-I1-03 reply tail OOM 뒤 completion 유실 | 미해소 | tail 예외는 internal-error completion으로 전환하지만([mesh_wire_ingress.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/services/mesh/mesh_wire_ingress.cpp:587)), 그 completion 함수 자체가 allocation/admission 실패를 조용히 폐기한다([mesh_runtime.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/services/mesh/mesh_runtime.cpp:984)). 따라서 exactly-once 보장은 아직 닫히지 않았다. |
| lifecycle case 수·CHANGELOG | 해소 | 실제 13개 case와 기록이 일치한다([CHANGELOG.md](/tmp/zlink-s5-i9-codex/CHANGELOG.md:65)). |

## Findings

### HIGH — S5-R1-09-01: request가 성공적으로 전달된 뒤 timeout 자원 확보에 실패하면 실패 반환과 부작용이 동시에 발생한다

- 이슈: request record 또는 wire frame을 먼저 전달한 다음 timeout task를 할당한다. 할당 실패 시 `ZLINK_SUBMIT_INTERNAL_ERROR`를 반환하지만 operation은 deadline 없이 남고 `operation_id_out`도 제공되지 않는다.
- 근거:
  - local request는 mailbox admission 뒤 timeout을 예약한다([mesh_messaging_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_messaging_api.cpp:243), [mesh_messaging_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_messaging_api.cpp:255)).
  - remote request도 wire submit 뒤 예약한다([mesh_messaging_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_messaging_api.cpp:312), [mesh_messaging_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_messaging_api.cpp:329)).
  - 같은 순서가 Actor lookup/destroy/join/message 경로에도 반복된다([mesh_actor_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_actor_api.cpp:562), [mesh_actor_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_actor_api.cpp:621), [mesh_actor_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_actor_api.cpp:1348)).
  - timeout context와 task 할당은 실제로 실패 가능한 단계다([request_reply_runtime_core.hpp](/tmp/zlink-s5-i9-codex/core/src/api/socket/request_reply_runtime_core.hpp:79)).
  - 성공한 request는 nonzero ID와 terminal completion exactly-once를 제공해야 한다([02-dispatch.ko.md](/tmp/zlink-s5-i9-codex/core/doc/spec/core/service/02-dispatch.ko.md:282)).
- 영향: 호출자는 실패를 관측해 재시도할 수 있지만 첫 request도 이미 처리될 수 있다. 중복 업무 동작, 식별할 수 없는 completion, timeout 없는 operation 잔류가 발생한다.
- 수정 범위: request bookkeeping, timeout task 준비/arming, local mailbox admission 및 모든 remote wire request 호출군. 현재 10개 scheduling 호출을 하나의 transaction 책임으로 통합해야 한다.
- 검증 방향: timeout task allocation fault를 admission 직전에 주입하고, 실패 시 target record/wire 전송·operation·reply route가 모두 0이며 output ID가 0인지 검증한다. Node/Channel/Spot/Actor/STREAM request 전부 포함해야 한다.

### HIGH — S5-R1-09-02: reply token과 operation을 fallible reply commit 전에 소비해 exactly-once와 실패 원자성을 위반한다

- 이슈: generic reply와 Actor join reply가 token·operation·membership 상태를 먼저 소비한 후 payload 복사 또는 wire submit을 수행한다.
- 근거:
  - 계약은 성공한 reply 한 번만 token을 소비하며, 실패한 join reply는 retry할 수 있어야 한다([02-dispatch.ko.md](/tmp/zlink-s5-i9-codex/core/doc/spec/core/service/02-dispatch.ko.md:286), [04-actor.ko.md](/tmp/zlink-s5-i9-codex/core/doc/spec/core/service/04-actor.ko.md:227)).
  - generic reply는 route를 consumed로 바꾸고 operation을 지운 뒤([mesh_dispatch_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_dispatch_api.cpp:840)), reply vector를 할당·복사한다([mesh_dispatch_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_dispatch_api.cpp:880)).
  - Actor join reply는 target actor count/membership과 token을 먼저 commit한다([mesh_actor_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_actor_api.cpp:1169), [mesh_actor_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_actor_api.cpp:1200)). 이후 wire submit 또는 vector/message 복사가 실패할 수 있다([mesh_actor_api.cpp](/tmp/zlink-s5-i9-codex/core/src/api/mesh/mesh_actor_api.cpp:1238)).
  - terminal completion 저장도 allocation 실패 시 폐기한다([mesh_runtime.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/services/mesh/mesh_runtime.cpp:984)).
  - 현재 one-shot test는 첫 reply 성공 후 두 번째 호출만 검사한다([test_mesh_node_basic.cpp](/tmp/zlink-s5-i9-codex/core/tests/integration/test_mesh_node_basic.cpp:222)). 실패한 첫 reply의 retry와 completion allocation 실패 test는 scoped 검색 결과가 없다.
- 영향: reply API가 실패했는데 retry는 `EALREADY`로 거절된다. requester completion이 영구 유실될 수 있고, accepted join은 source commit 없이 target의 actor count만 증가할 수 있다.
- 수정 범위: generic/local/remote reply, Actor join reply, `complete_operation`, reply-route commit과 message cleanup.
- 검증 방향: payload 복사, wire backpressure, terminal record 생성, infrastructure admission 각각에 fault를 주입한다. 실패 뒤 token retry 가능, membership 불변, 정확히 하나의 terminal completion, message reference 누수 0을 검사해야 한다.

### MEDIUM — S5-R1-09-03: 10.0.0 ABI와 package metadata가 일치하지 않는다

- 이슈: CMake와 정식 계약은 10.0.0/SOVERSION 10이지만 Debian·RPM·NuGet recipes는 6.0.3 또는 4.2.3 metadata를 유지한다.
- 근거:
  - 정식 계약은 설치 package도 spec/header와 일치하도록 요구한다([00-public-contract-governance.ko.md](/tmp/zlink-s5-i9-codex/core/doc/spec/core/00-public-contract-governance.ko.md:13)); SOVERSION은 10이다([03-errors.ko.md](/tmp/zlink-s5-i9-codex/core/doc/spec/core/03-errors.ko.md:202)).
  - CMake는 10.0.0과 SOVERSION 10을 생성한다([CMakeLists.txt](/tmp/zlink-s5-i9-codex/core/CMakeLists.txt:11), [CMakeLists.txt](/tmp/zlink-s5-i9-codex/core/CMakeLists.txt:1368)).
  - Debian은 `libzlink6` 및 `6.0.3-0.1`이다([control](/tmp/zlink-s5-i9-codex/core/packaging/debian/control:18), [changelog](/tmp/zlink-s5-i9-codex/core/packaging/debian/changelog:1)).
  - RPM도 `libzlink6`, `Version: 6.0.3`이다([zlink.spec](/tmp/zlink-s5-i9-codex/core/packaging/redhat/zlink.spec:11)).
  - NuGet은 package version과 binary target 이름을 `4.2.3.0`으로 고정한다([package.config](/tmp/zlink-s5-i9-codex/core/packaging/nuget/package.config:4), [package.nuspec](/tmp/zlink-s5-i9-codex/core/packaging/nuget/package.nuspec:9), [package.targets](/tmp/zlink-s5-i9-codex/core/packaging/nuget/package.targets:29)).
  - Core test/CMake/workflow 범위에는 이 세 recipe를 검사하는 gate가 없다.
- 영향: package manager dependency 이름과 실제 `libzlink.so.10`이 어긋나며, 10.0.0 binary를 이전 버전으로 게시하거나 NuGet consumer가 존재하지 않는 옛 binary 이름을 링크할 수 있다.
- 수정 범위: Debian 파일명·control·dsc·rules, RPM spec, NuGet config/nuspec/targets/generated binary names, package verification.
- 검증 방향: 각 package를 실제 생성한 뒤 metadata와 payload SONAME/version을 검사하고 clean consumer link/run test를 추가한다.

### LOW — S5-R1-09-04: 제거된 SpotNode/PUB-SUB 구현 타입의 dead declaration이 남아 있다

- 이슈: 사용처가 없는 `spot_node_t`, `spot_pub_t`, `spot_sub_t`, `spot_internal_receiver_t` forward declaration이 남아 있다.
- 근거: [monitor_api_internal.hpp](/tmp/zlink-s5-i9-codex/core/src/api/monitoring/monitor_api_internal.hpp:13). 전체 고정 scope 검색에서 선언 외 사용처가 없다.
- 영향: runtime 영향은 없지만 SpotNode와 service PUB/SUB 제거 범위 및 current-state 정리가 완결되지 않았다.
- 수정 범위: 해당 dead declaration 제거.
- 검증 방향: 네 type의 전체 scope no-hit와 monitoring build를 확인한다.

## Known risk 4건

| Risk | 현재 판정 |
|---|---|
| TSAN auto-HWM lock-order | **미해소·추적 유지.** `_slot_sync`를 잡은 상태에서 socket plan이 monitor lock을 취한다([ctx_auto_hwm_recalc.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80), [socket_base.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/sockets/common/socket_base.cpp:214)). Manifest의 현재 warning 14건을 해소됐다고 볼 source 변화가 없다. 반대 lock 순서의 확정 source 경로는 이번 정적 검토에서 입증하지 못해 별도 finding으로 승격하지 않았다. |
| raw command mailbox ypipe | **미해소·추적 유지.** send/read는 `_sync`로 감싸지만([mailbox.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/core/mailbox.cpp:39), [mailbox.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/core/mailbox.cpp:89)), 현재 TSAN 관찰을 무효화할 독립 증거가 없다. |
| raw socket teardown | **미해소·추적 유지.** peer back-reference detach와 sink 종료가 이어지고([pipe.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/core/pipe.cpp:704)), Asio는 session의 `blob_t` view를 읽은 뒤 teardown을 진행한다([asio_engine.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/engine/asio/asio_engine.cpp:1845)). Manifest의 data-race 관찰이 현재도 남아 있다. |
| `ctx_term` linger | **계약 일치, finding 아님.** 기본 blocky context는 linger `-1`을 적용하며([socket_base.cpp](/tmp/zlink-s5-i9-codex/core/src/runtime/sockets/common/socket_base.cpp:129)), 정식 계약도 모든 socket이 닫힐 때까지 block될 수 있다고 명시한다([01-context.ko.md](/tmp/zlink-s5-i9-codex/core/doc/spec/core/01-context.ko.md:131)). 운영 risk이지만 현재 계약 위반은 아니다. |

## 독립 축 판정

- **I1 계약 구현 일치: NOT CLEAN**
  - request 실패 원자성과 reply/completion exactly-once 위반이 남았다.
- **I2 POSD·DDD: NOT CLEAN**
  - operation 등록, reply route, timeout 확보와 admission commit 지식이 여러 호출부에 반복되어 있다. 실제로 10개 scheduling 지점이 동일한 partial-commit 결함을 갖는다. request transaction/lifecycle owner가 충분히 깊은 모듈로 닫히지 않았다.
- **I3 정리 완결성: NOT CLEAN**
  - Debian/RPM/NuGet version·ABI metadata가 stale하며 dead SpotNode 계열 선언도 남았다.
  - 반면 formal export 196개, 제거 identifier, merge marker, 0-byte file, 금지 문구와 scoped `git diff --check`에서는 추가 문제를 찾지 못했다.

CORE REVIEW NOT CLEAN
