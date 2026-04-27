# auto HWM / service monitor 정리 실행 계획

> 작성 기준: 2026-04-27
> 입력 초안:
> - `doc/draft/auto-hwm-recalculation-policy.ko.md`
> - `doc/draft/service-monitor-split.ko.md`
>
> 목표:
> 두 초안의 방향을 실제 구현, 공개 헤더, 정식 문서, 바인딩 문서, 바인딩
> 라이브러리, `bindings/c/perf` 스모크 테스트까지 끊기지 않게 끝낼 수 있도록
> 전체 순서를 고정한다.

## TODO

- [x] 단계 0. 기준선 고정과 영향 범위 수집
- [x] 단계 1. core auto HWM 재계산 정책 구현
- [x] 단계 2. core service monitor 계층 제거
- [x] 단계 3. core 테스트와 `bindings/c/perf` 전체 패턴 스모크
- [x] 단계 4. 정식 spec / guide / internals 문서 반영
- [x] 단계 5. `doc/spec/bindings` 문서 반영
- [x] 단계 6. 각 바인딩 라이브러리 반영
- [x] 단계 7. 바인딩 테스트와 최종 재검토

## 실행 로그

### 2026-04-27 단계 0 진행 중

- 수정 파일
  `doc/plan/monitoring/auto-hwm-and-service-monitor-rollout-plan.ko.md`
- 실행 명령
  `git status --short`
- 결과
  작업 트리에 기존 사용자 변경이 매우 많다. 되돌리지 않고 그대로 유지한다.
- 실행 명령
  `rg -n "service monitor|zlink_service_monitor_open|zlink_service_monitor_recv|zlink_service_monitor_handler" core doc bindings -g '!**/build/**'`
- 결과
  service monitor 공개 surface와 문서, 테스트, perf, 각 바인딩 참조가 넓게 남아 있다.
- 실행 명령
  `rg -n "auto HWM|auto_hwm|recalculate|AUTO_HWM_RECALC_DEBOUNCE|AUTO_HWM_STREAM_BOOTSTRAP|AUTO_HWM_SPOT_BOOTSTRAP" core doc bindings -g '!**/build/**'`
- 결과
  auto HWM은 이미 일부 구현돼 있지만 draft의 context-wide debounce / planning count 계약과는 아직 차이가 있다.
- 실행 명령
  `cmake --build core/build -j"$(nproc)"`
- 결과
  기준선 core build 성공.
- 실행 명령
  `cmake --build bindings/c/build -j"$(nproc)"`
- 결과
  기준선 `bindings/c` build 성공.
- 실행 명령
  `ctest --test-dir core/build --output-on-failure -j"$(nproc)"`
- 결과
  기준선 core 테스트 `99/99` 통과.

### 2026-04-27 단계 1 진행 중

- 수정 파일
  `core/include/zlink.h`, `core/include/zlink_enum.h`,
  `core/src/api/context_api.cpp`, `core/src/core/ctx.hpp`,
  `core/src/core/ctx.cpp`, `core/src/core/auto_hwm_policy.hpp`,
  `core/src/core/auto_hwm_policy.cpp`,
  `core/src/sockets/socket_base.cpp`,
  `core/src/sockets/socket_base_api.cpp`,
  `core/src/services/spot/spot_auto_hwm_internal.hpp`,
  `core/src/services/spot/spot_mesh_pub_budget.cpp`,
  `core/src/services/spot/spot_node_handles.cpp`,
  `core/tests/unittest/unittest_spot_data_plane_budget.cpp`
- 실행 명령
  `cmake --build core/build -j"$(nproc)"`
- 결과
  `ctx` auto HWM 공개 옵션 3개와 `zlink_ctx_auto_hwm_recalculate()` 추가 후 core build 성공.
- 실행 명령
  `ctest --test-dir core/build --output-on-failure -R 'test_ctx_options|test_monitor_socket_contract|unittest_spot_data_plane_budget|test_peer_admission' -j1`
- 실패 원인
  debounce 예약을 넣으면서 socket-local 즉시 반영이 필요한 SPOT 내부 경로와
  기존 ctx option 회귀가 깨졌다.
- 해결 내용
  role/scope/setopt 경로는 즉시 `refresh_auto_hwm_policy()`를 유지하고,
  attach/detach 같은 연결 변화만 `ctx` 예약 경로로 모으도록 조정했다.
- 실행 명령
  `ctest --test-dir core/build --output-on-failure -R 'test_ctx_options|test_monitor_socket_contract|unittest_spot_data_plane_budget|test_peer_admission' -j1`
- 결과
  위 4개 회귀 테스트 재통과.

### 2026-04-27 bindings 전 리뷰 게이트 1차

- 실행 명령
  `rg -n "zlink_service_monitor_open|zlink_service_monitor_handler|zlink_service_monitor_recv|zlink_service_event_t|zlink_service_monitor_open_options_t|zlink_service_monitor_handler_fn|zlink_service_monitor_event_t" core/include core/src core/tests -g '!**/build/**'`
- 리뷰 결과
  service monitor 공개 API/타입이 아직 `core/include/zlink.h`, API 구현, 테스트에
  남아 있다.
- 실행 명령
  `rg -n "ZLINK_SERVICE_MONITOR_EVENT_|ZLINK_DISCOVERY_MONITOR_EVENT_|ZLINK_DISCOVERY_SERVICE_|ZLINK_MONITOR_TARGET_DISCOVERY|ZLINK_MONITOR_TARGET_SPOT|ZLINK_MONITOR_TARGET_SPOT_NODE" core/include core/src core/tests -g '!**/build/**'`
- 리뷰 결과
  제거 대상 enum/상수가 아직 `core/include/zlink_enum.h`와 discovery/test 경로에
  남아 있다.
- 실행 명령
  `rg -n "role_budget_bytes|control_budget|routed_budget|fanout_budget|recv_ingress_budget|group_budget_bytes|role_group_budget_bytes|scope_group_budget_bytes" core/src/core core/src/sockets core/src/services/spot core/tests -g '!**/build/**'`
- 리뷰 결과
  auto HWM 계산식은 아직 role-budget 분할 모델을 유지하고 있어 draft의
  context-wide planning count/share 모델이 미완료 상태다.

### 2026-04-27 bindings 전 리뷰 게이트 2차

- 수정 파일
  `core/src/services/common/service_monitor_types_internal.hpp`,
  `core/tests/testutil.hpp`,
  `core/tests/testutil_monitoring.hpp`,
  `core/tests/testutil_monitoring.cpp`,
  `core/tests/unittest/unittest_typed_option.cpp`,
  `core/tests/integration/test_peer_admission.cpp`,
  `core/tests/integration/test_thread_safe_contract_policy.cpp`,
  `core/src/api/service_discovery_api.cpp`,
  `core/src/api/service_spot_node_api.cpp`
- 실행 명령
  `cmake --build core/build -j"$(nproc)"`
- 실패 원인
  공개 헤더에서 `zlink_service_event_t`와 service monitor 선언을 제거한 뒤
  test util과 typed option 테스트가 여전히 그 public surface를 직접 참조했다.
- 해결 내용
  내부 전용 service monitor 타입 헤더를 추가해 core 내부 빌드를 먼저 복구하고,
  그 다음 core 테스트를 snapshot/query 중심으로 바꿔 service monitor 공개 의존을
  걷어냈다.
- 실행 명령
  `ctest --test-dir core/build --output-on-failure -R 'unittest_typed_option|test_peer_admission|test_thread_safe_contract_policy|unittest_discovery_service_state' -j1`
- 실패 원인
  `ctest`가 새 `unittest_typed_option` 링크 전에 먼저 실행되어 이전 바이너리의
  service monitor 기대를 잡았다.
- 해결 내용
  빌드 완료 후 `ctest`를 다시 실행했고, `unittest_typed_option`은 generic service
  monitor open 검증 대신 `zlink_spot_node_internal_sockets_snapshot()` 기반으로
  바꿨다.
- 실행 명령
  `ctest --test-dir core/build --output-on-failure -R 'unittest_typed_option|test_peer_admission|test_thread_safe_contract_policy' -j1`
- 결과
  위 3개 타깃 테스트 재통과.
- 실행 명령
  `rg -n "zlink_service_monitor|ZLINK_SERVICE_MONITOR|recv_service_event_from_socket" core/tests -g '!**/build/**'`
- 리뷰 결과
  core 테스트 계층에서는 removed service monitor 공개 API 의존을 제거했다.
- 실행 명령
  `rg -n "zlink_service_monitor_open|zlink_service_monitor_handler|zlink_service_monitor_recv|zlink_service_event_t|ZLINK_SERVICE_MONITOR_EVENT_|ZLINK_DISCOVERY_MONITOR_EVENT_|ZLINK_DISCOVERY_SERVICE_|ZLINK_MONITOR_TARGET_DISCOVERY|ZLINK_MONITOR_TARGET_SPOT|ZLINK_MONITOR_TARGET_SPOT_NODE" core/include core/src -g '!**/build/**'`
- 리뷰 결과
  `core/include` 공개 surface 제거는 반영됐지만, `core/src`에는 service monitor
  내부 허브, open path, event payload가 아직 남아 있어 단계 2가 계속 진행 중이다.

### 2026-04-27 bindings 전 리뷰 게이트 3차

- 수정 파일
  `core/CMakeLists.txt`,
  `core/src/services/discovery/discovery.{hpp,cpp}`,
  `core/src/services/discovery/discovery_access.{hpp,cpp}`,
  `core/src/services/discovery/discovery_bootstrap.cpp`,
  `core/src/services/discovery/discovery_state.cpp`,
  `core/src/services/discovery/discovery_update.cpp`,
  `core/src/services/spot/spot_pub.{hpp,cpp}`,
  `core/src/services/spot/spot_sub.{hpp,cpp}`,
  `core/src/services/spot/spot_sub_option.cpp`,
  `core/src/services/spot/spot_sub_lifecycle.cpp`,
  `core/src/services/spot/spot_sub_subject_state.cpp`,
  `core/src/services/spot/spot_subject_access.hpp`,
  `core/src/services/spot/spot_subject_poller.cpp`,
  `core/src/services/spot/spot_internal_receiver.hpp`,
  `core/src/services/spot/spot_node_access.{hpp,cpp}`
- 삭제 파일
  `core/src/api/monitor_service_open_api.cpp`,
  `core/src/api/monitor_service_api.cpp`,
  `core/src/api/monitor_service_snapshot_api.cpp`,
  `core/src/services/common/service_monitor.cpp`,
  `core/src/services/common/service_monitor.hpp`
- 실행 명령
  `cmake --build core/build -j"$(nproc)"`
- 결과
  discovery/spot 내부의 service monitor hub와 `monitor_open()` 경로를 제거한 뒤에도
  core build 재통과.
- 실행 명령
  `ctest --test-dir core/build --output-on-failure -R 'test_discovery_resolve_spot|test_peer_admission|unittest_typed_option|unittest_spot_subject_access|test_spot_service_introspection' -j1`
- 결과
  discovery/spot 관련 타깃 테스트 12개 재통과.
- 실행 명령
  `rg -n "service_monitor\\.hpp|monitor_service_open_api\\.cpp|monitor_service_api\\.cpp|monitor_service_snapshot_api\\.cpp|service_monitor\\.cpp" core/src core/CMakeLists.txt -g '!**/build/**'`
- 결과
  위 dead source/header 참조는 빌드 경로에서 제거했다.
- 실행 명령
  `rg -n "role_budget_bytes|control_budget|routed_budget|fanout_budget|recv_ingress_budget|group_budget_bytes|role_group_budget_bytes|scope_group_budget_bytes" core/src/core core/src/sockets core/src/services/spot core/tests bindings/c/perf -g '!**/build/**'`
- 리뷰 결과
  auto HWM은 아직 role-budget 분할 모델과 old snapshot/perf 필드에 묶여 있다.
  다음 반복은 단계 1 미완료 항목인 context-wide planning count/share 계산식과
  snapshot/perf 출력 정리로 바로 이어간다.

### 2026-04-27 bindings 전 리뷰 게이트 4차

- 수정 파일
  `core/src/core/auto_hwm_policy.{hpp,cpp}`,
  `core/src/core/ctx.cpp`,
  `core/src/sockets/socket_base.{hpp,cpp}`,
  `core/src/sockets/socket_base_monitor.cpp`,
  `core/include/zlink.h`,
  `core/tests/integration/test_ctx_options.cpp`,
  `core/src/api/monitor_api_internal.hpp`,
  `core/src/api/monitor_api.cpp`,
  `core/src/api/monitor_socket_api.cpp`,
  `core/src/api/monitor_query_api.cpp`,
  `core/src/api/zlink.cpp`,
  `core/src/services/common/service_monitor_types_internal.hpp`,
  `core/src/services/common/monitor_decode.hpp`,
  `core/src/services/spot/spot_data_plane_pending.cpp`
- 실행 명령
  `cmake --build core/build -j"$(nproc)"`
- 실패 원인
  `auto_hwm_policy.cpp`에 `std::max` 인클루드가 빠져 재빌드가 중단됐다.
- 해결 내용
  context-wide planning count/share 계산으로 구조를 바꾸고, old role-group
  snapshot 필드를 제거한 뒤 누락 헤더를 보강했다. raw socket monitor 내부
  dispatcher에서도 service monitor 전용 분기와 recv path를 제거했다.
- 실행 명령
  `ctest --test-dir core/build --output-on-failure -R 'test_ctx_options|test_monitor_socket_contract|test_peer_admission|test_thread_safe_contract_policy|unittest_spot_data_plane_budget|test_discovery_resolve_spot|test_spot_service_introspection|unittest_typed_option' -j1`
- 결과
  위 core 타깃 테스트 재통과.

### 2026-04-27 단계 3 스모크 1차

- 수정 파일
  `bindings/c/perf/multi/common/perf_common.hpp`,
  `bindings/c/perf/multi/common/perf_multi_runtime.hpp`,
  `bindings/c/perf/run_comparison.py`,
  `bindings/c/samples/sample_common.h`,
  `bindings/c/samples/discovery_registry_sample.c`
- 실행 명령
  `cmake --build bindings/c/build -j"$(nproc)"`
- 결과
  `bindings/c` build 성공.
- 실행 명령
  `./bindings/c/perf/run_benchmarks.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1`
- 결과
  single 전체 패턴 smoke 완료.
- 실행 명령
  `./bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2`
- 실패 원인
  `MULTI_SPOT*` 패턴에서 `CLIENT_READY` 단계가 timeout 되었고,
  snapshot상 spot/spotnode socket HWM이 `1`까지 내려갔다.
- 해결 내용
  planning count는 queue share 계산에만 쓰고, transport buffer 비용은
  observed connection 기준으로 다시 분리했다. 이 변경 후 spot 경로 HWM이
  다시 정상 수준으로 회복됐다.
- 실행 명령
  `ctest --test-dir core/build --output-on-failure -R 'test_ctx_options|test_spot_service_introspection|unittest_spot_data_plane_budget' -j1`
- 결과
  spot 회귀 관련 core 테스트 재통과.
- 실행 명령
  `./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT,SPOT_REQREP,SPOT_SENDSEND --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2`
- 결과
  failing 하던 `MULTI_SPOT*` 3개 패턴 smoke 재통과.
- 실행 명령
  `./bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2`
- 결과
  multi 전체 패턴 smoke 완료.

### 2026-04-27 단계 6 진행 중

- 수정 파일
  `bindings/go/monitor.go`, `bindings/go/discovery.go`,
  `bindings/go/callbacks.go`, `bindings/go/monitor_test.go`,
  `bindings/go/spec_alignment_test.go`, `bindings/go/surface_test.go`,
  `bindings/go/samples/internal/samplecommon/samplecommon.go`
- 실행 명령
  `gofmt -w bindings/go/monitor.go bindings/go/callbacks.go bindings/go/discovery.go bindings/go/samples/internal/samplecommon/samplecommon.go bindings/go/monitor_test.go bindings/go/spec_alignment_test.go bindings/go/surface_test.go`
- 실행 명령
  `go test ./...`
- 결과
  Go 바인딩에서 `ServiceMonitor` 공개 surface와 discovery `MonitorOpen()`
  의존을 제거하고, `MonitorSnapshot`을 새 auto HWM 필드로 바꾼 뒤 전체
  테스트 통과.

- 수정 파일
  `bindings/python/src/zlink/_ffi.py`,
  `bindings/python/src/zlink/_monitor.py`,
  `bindings/python/src/zlink/_discovery.py`,
  `bindings/python/src/zlink/_native.py`,
  `bindings/python/src/zlink/_enums.py`,
  `bindings/python/src/zlink/__init__.py`,
  `bindings/python/tests/test_native_contract.py`,
  `bindings/python/tests/test_core_api_alignment.py`,
  `bindings/python/samples/discovery_registry_sample.py`,
  `bindings/python/samples/sample_support.py`
- 실행 명령
  `cp core/build/lib/libzlink.so.5.3.4 bindings/python/src/zlink/native/linux-x86_64/libzlink.so.5.3.4`
- 실패 원인
  Python 표면 테스트가 이전 번들 `libzlink.so`를 읽어 이미 제거된
  `zlink_service_monitor_*` 심볼을 계속 노출했고, snapshot 구조 불일치로
  `pytest` 실행 중 세그폴트가 났다.
- 해결 내용
  service monitor FFI와 공개 export를 제거한 뒤, 번들 native library를
  `core/build` 기준으로 다시 동기화했다.
- 실행 명령
  `python -m pytest bindings/python/tests/test_native_contract.py bindings/python/tests/test_core_api_alignment.py -q`
- 결과
  Python 핵심 계약 테스트 `17 passed`.

- 수정 파일
  `bindings/rust/src/monitor.rs`,
  `bindings/rust/src/ffi.rs`,
  `bindings/rust/src/lib.rs`,
  `bindings/rust/src/service.rs`,
  `bindings/rust/samples/discovery_registry_sample.rs`,
  `bindings/rust/tests/service_surface_tests.rs`
- 삭제 파일
  `bindings/rust/tests/service_monitor_tests.rs`
- 실행 명령
  `cp core/build/lib/libzlink.so.5.3.4 bindings/rust/native/linux-x86_64/libzlink.so.5.3.4`
- 실행 명령
  `cargo fmt`
- 실패 원인
  `Discovery::monitor_open()` 제거 후 Rust 샘플과 service surface 테스트가
  이전 API를 계속 호출했다.
- 해결 내용
  discovery 샘플을 `member_peers()` polling으로 바꾸고, service surface
  테스트를 snapshot/query 중심으로 정리했다.
- 실행 명령
  `cargo test`
- 결과
  Rust 전체 테스트 통과.

- 수정 파일
  `bindings/cpp/include/zlink/types.hpp`,
  `bindings/cpp/include/zlink/services/discovery.hpp`,
  `bindings/cpp/tests/contract/test_cpp_contract_monitor.cpp`,
  `bindings/cpp/tests/contract/test_cpp_contract_service.cpp`,
  `bindings/cpp/samples/discovery_registry_sample.cpp`
- 실행 명령
  `cmake --build bindings/cpp/build -j"$(nproc)"`
- 실행 명령
  `ctest --test-dir bindings/cpp/build --output-on-failure -j1`
- 결과
  C++ 바인딩에서 `service_monitor_handle_t`와 `Discovery::monitor_open()` 공개
  surface를 제거하고 snapshot/query 중심으로 정리한 뒤 테스트 통과.

- 수정 파일
  `bindings/node/src/canonical.ts`,
  `bindings/node/native/src/addon_api.h`,
  `bindings/node/native/src/addon_core.cc`,
  `bindings/node/native/src/addon_discovery.cc`,
  `bindings/node/tests/api.test.ts`,
  `bindings/node/tests/monitor.test.ts`,
  `bindings/node/tests/socket_surface.typecheck.ts`
- 실행 명령
  `npm run typecheck`
- 실행 명령
  `npm test`
- 결과
  Node 바인딩에서 `ServiceMonitor`, `Discovery.monitorOpen()`과 old auto HWM
  snapshot 필드를 제거하고 테스트 통과.

- 수정 파일
  `bindings/java/src/main/java/dev/kairoscode/zlink/MonitorSnapshot.java`,
  `bindings/java/src/main/java/dev/kairoscode/zlink/service/discovery/Discovery.java`,
  `bindings/java/src/main/java/dev/kairoscode/zlink/internal/Native.java`,
  `bindings/java/src/main/java/dev/kairoscode/zlink/internal/NativeLayouts.java`,
  `bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/DiscoveryRegistrySample.java`
- 삭제 파일
  `bindings/java/src/main/java/dev/kairoscode/zlink/ServiceMonitor.java`,
  `bindings/java/src/main/java/dev/kairoscode/zlink/ServiceMonitorEventMask.java`,
  `bindings/java/src/main/java/dev/kairoscode/zlink/ServiceMonitorHandler.java`,
  `bindings/java/src/main/java/dev/kairoscode/zlink/service/registry/ServiceEvent.java`,
  `bindings/java/src/main/java/dev/kairoscode/zlink/service/registry/ServiceEventType.java`,
  `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/SpotServiceMonitorEvent.java`
- 실행 명령
  `./gradlew test --no-daemon`
- 결과
  Java 바인딩에서 `ServiceMonitor`와 관련 native downcall 잔재까지 제거한 뒤
  전체 테스트 통과.

- 수정 파일
  `bindings/dotnet/src/Zlink/Monitor.cs`,
  `bindings/dotnet/src/Zlink/Service/Discovery.cs`,
  `bindings/dotnet/src/Zlink/Enums.cs`,
  `bindings/dotnet/tests/Zlink.Tests/test_socket_surface.cs`,
  `bindings/dotnet/tests/Zlink.Tests/test_callback_delivery.cs`
- 삭제 파일
  `bindings/dotnet/src/Zlink/Service/ServiceMonitor.cs`,
  `bindings/dotnet/tests/Zlink.Tests/test_service_monitor_contract.cs`
- 실행 명령
  `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj`
- 실행 명령
  `dotnet test bindings/dotnet/Zlink.sln`
- 결과
  Dotnet 바인딩에서 `ServiceMonitor`, `Discovery.MonitorOpen()`, old snapshot
  필드를 제거한 뒤 `135 passed`.

### 2026-04-27 단계 4 / 5 / 7 완료

- 수정 파일
  `doc/spec/core/context.{md,ko.md}`,
  `doc/spec/core/monitoring.{md,ko.md}`,
  `doc/spec/core/events.{md,ko.md}`,
  `doc/spec/core/service/{discovery,registry,spot}.{md,ko.md}`,
  `doc/spec/core/README.md`,
  `doc/spec/core/README.ko.md`,
  `doc/spec/core/errno-map.md`,
  `doc/spec/core/errno-map.ko.md`,
  `doc/spec/README.md`,
  `doc/spec/bindings/README.md`,
  `doc/spec/bindings/{cpp,python,go,rust,node,java,dotnet}/README.md`,
  `doc/guide/06-monitoring.{md,ko.md}`,
  `doc/guide/07-0-services.{md,ko.md}`,
  `doc/guide/07-1-discovery.{md,ko.md}`,
  `doc/guide/07-3-spot.{md,ko.md}`,
  `doc/guide/07-4-registry.md`,
  `doc/guide/11-thread-safety.{md,ko.md}`,
  `doc/internals/{architecture,architecture.ko,posd-module-structure,posd-module-structure.ko,services-internals,spot-internals,spot-internals.ko}.md`
- 실행 명령
  `rg -n "zlink_service_monitor|ServiceMonitor|service_monitor_handle_t|auto_hwm_planning_transport_connections|auto_hwm_group_budget|auto_hwm_role_group_budget|auto_hwm_scope_group_budget" doc/spec doc/guide doc/internals`
- 결과
  정식 spec/guide/internals와 `doc/spec/bindings`에서 old service monitor
  공개 계약과 old auto HWM snapshot 필드를 제거했다.
- 실행 명령
  `cargo test`
- 실행 명령
  `./gradlew test --no-daemon`
- 결과
  문서 반영 중 정리한 Rust/Java 잔재 제거까지 포함해 바인딩 테스트 재통과.

## 1. 작업 원칙

### 1.1 사람 개입 없이 진행

실행자는 아래 규칙을 따른다.

- 두 draft에 적힌 결정은 다시 묻지 않고 그대로 구현한다.
- 정식 spec, guide, internals 문서는 **구현이 끝난 뒤** 반영한다.
- 구현 중 충돌이 나면 `core/include/zlink.h`에 들어갈 최종 공개 계약을 먼저
  고정하고, 테스트와 문서를 그 계약에 맞춘다.
- 기존 사용자 변경은 되돌리지 않는다.
- 단계별 검증이 끝나기 전에는 다음 단계로 넘어가지 않는다.

### 1.2 문서 분리 원칙

`AGENTS.md` 기준으로 아래를 지킨다.

- `doc/spec/`는 공개 계약만 적는다.
- `doc/guide/`는 사용 목적과 사용법만 적는다.
- `doc/internals/`는 내부 구조와 계산 흐름만 적는다.
- 구현 전 내용은 draft에만 남기고, 구현 후 정식 문서에 반영한다.

### 1.3 빌드 기준

`bindings/c/perf`는 반드시 `core/build` runtime 기준으로 검증한다.

```bash
cmake --build core/build -j"$(nproc)"
cmake --build bindings/c/build -j"$(nproc)"
```

`build_cpp_release` 같은 임시 빌드 결과로 perf를 판단하지 않는다.

## 2. 구현 범위 요약

이번 작업의 실제 범위는 아래 두 축이다.

### 2.1 auto HWM

- 연결 변화 기준 자동 재계산
- context 단위 debounce
- 기본 debounce `3000ms`
- 즉시 적용 함수 `zlink_ctx_auto_hwm_recalculate(ctx)`
- `stream bootstrap=5000`
- `spot bootstrap=500`
- `spotnode bootstrap=500`
- perf 출력은 `observed count`와 `planning count`를 함께 보여줌

### 2.2 service monitor

- service monitor 계층 전체 제거
- `Discovery`, `SpotNode`, `Registry`, `Spot`은 monitor 없이
  snapshot/query만 사용
- raw socket monitor만 유지

## 3. 단계 0. 기준선 고정과 영향 범위 수집

### 3.1 작업 전 저장

```bash
git status --short
```

결과는 작업 로그에 남긴다.

### 3.2 영향 파일 자동 수집

아래 검색 결과를 시작점으로 쓴다.

```bash
rg -n "service monitor|zlink_service_monitor_open|zlink_service_monitor_recv|zlink_service_monitor_handler" \
  core doc bindings -g '!**/build/**'

rg -n "auto HWM|auto_hwm|recalculate|AUTO_HWM_RECALC_DEBOUNCE|AUTO_HWM_STREAM_BOOTSTRAP|AUTO_HWM_SPOT_BOOTSTRAP" \
  core doc bindings -g '!**/build/**'
```

실행자는 검색 결과를 파일 목록으로 정리하고, 각 단계에서 실제 수정 대상을
 체크한다.

### 3.3 기준선 빌드

```bash
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -j"$(nproc)"
cmake --build bindings/c/build -j"$(nproc)"
```

실패하면 그 상태를 기준선 로그에 남기고 원인을 먼저 정리한다.

## 4. 단계 1. core auto HWM 재계산 정책 구현

### 4.1 공개 계약 반영

아래 항목을 `core/include/zlink.h`, `core/include/zlink_enum.h`에 반영한다.

- context option 추가
  - `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS`
  - `ZLINK_CTX_OPT_AUTO_HWM_STREAM_BOOTSTRAP`
  - `ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP`
- 함수 추가
  - `int zlink_ctx_auto_hwm_recalculate(void *ctx);`

`doc/draft/auto-hwm-recalculation-policy.ko.md`의 계약이 그대로 드러나게 맞춘다.

### 4.2 구현 원칙

- 자동 재계산 단위는 socket이 아니라 context다.
- 연결 생성, 종료, `spot` attach/detach, 관련 option 변경은 재계산 예약 사유다.
- timer는 context당 하나만 둔다.
- debounce 만료 전 새 변화가 오면 deadline을 다시 민다.
- manual `SNDHWM` / `RCVHWM` / `SNDBUF` / `RCVBUF`는 자동 계산이 덮어쓰지 않는다.
- 내부 monitor/helper socket은 auto HWM 계산 대상에서 제외한다.

### 4.3 구현 파일 시작점

최소 시작점은 아래 파일로 본다.

- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- `core/src/core/ctx.hpp`
- `core/src/core/ctx.cpp`
- `core/src/core/auto_hwm_policy.hpp`
- `core/src/core/auto_hwm_policy.cpp`
- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/socket_base.cpp`
- `core/src/sockets/socket_base_monitor.cpp`
- `core/src/api/monitor_socket_api.cpp`
- `core/src/services/common/service_monitor.cpp`
- `core/src/services/common/socket_monitor_bridge.hpp`
- `core/src/services/spot/spot_auto_hwm_internal.hpp`

실제 파일 범위는 검색 결과에 따라 늘어날 수 있다.

### 4.4 필수 테스트

- debounce 0, 양수 동작
- connect/detach 모두 같은 debounce 규칙
- manual override 우선순위
- `stream`, `spot`, `spotnode` bootstrap 반영
- `zlink_ctx_auto_hwm_recalculate()` 즉시 적용
- monitor/helper socket 제외
- auto HWM detail에서 `observed`, `planning` count 노출

## 5. 단계 2. core service monitor 계층 제거

### 5.1 공개 계약 반영

`doc/draft/service-monitor-split.ko.md` 기준으로 아래를 제거한다.

#### API

- `zlink_service_monitor_open`
- `zlink_service_monitor_handler`
- `zlink_service_monitor_recv`

#### 타입

- `zlink_service_event_t`
- `zlink_service_monitor_handler_fn`
- `zlink_service_monitor_event_t`
- `zlink_service_monitor_event_detail_mask_t`
- `zlink_service_monitor_open_options_t`

#### enum / 상수

- `ZLINK_SERVICE_MONITOR_EVENT_*`
- `ZLINK_DISCOVERY_MONITOR_EVENT_*`
- `ZLINK_DISCOVERY_SERVICE_*`
- `ZLINK_MONITOR_TARGET_DISCOVERY`
- `ZLINK_MONITOR_TARGET_SPOT`
- `ZLINK_MONITOR_TARGET_SPOT_NODE`

아래는 유지한다.

- `zlink_socket_monitor_open()`
- `ZLINK_MONITOR_TARGET_SOCKET`
- `ZLINK_MONITOR_EVENT_ERROR`
- `ZLINK_MONITOR_EVENT_CLOSED`

### 5.2 구현 원칙

- service monitor open path를 삭제한다.
- discovery/spotnode/spot 쪽 상태 관찰은 snapshot/query만 남긴다.
- service monitor가 쓰던 내부 bridge/helper가 공개 surface를 위해서만 존재했다면
  같이 제거한다.
- raw socket monitor는 동작을 바꾸지 않는다.

### 5.3 구현 파일 시작점

- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- `core/src/api/monitor_service_open_api.cpp`
- `core/src/api/service_api.cpp`
- `core/src/api/zlink.cpp`
- `core/src/services/common/service_monitor.cpp`
- `core/src/services/common/service_monitor.hpp`
- `core/src/services/spot/*`
- `core/tests/**` 중 service monitor 참조 테스트
- `bindings/c/perf/**` 중 service monitor 참조 경로
- `bindings/c/samples/sample_common.h`

### 5.4 공개 타입 분리 원칙

`zlink_service_event_t`를 공개 헤더에서 제거하는 경우, 구현자는 아래 둘 중
하나를 먼저 고정해야 한다.

1. 내부 전용 event struct를 별도 internal header로 옮긴다.
2. service monitor 전용 event payload 자체를 internal type으로 다시 정의한다.

즉 "공개 타입 삭제"와 "내부 구현 삭제"를 같은 의미로 취급하지 않는다.
공개 surface에서 제거하더라도, snapshot/query 전환 작업이 끝날 때까지 내부에서는
필요한 형태로 잠시 유지할 수 있다.

단계 2 구현은 반드시 아래 순서를 따른다.

1. 공개 헤더에서 service monitor surface 제거
2. `bindings/c/perf`, sample, core test의 service monitor 의존 제거
3. 내부에서 더 이상 필요 없는 service monitor helper와 payload 정리

## 6. 단계 3. core 테스트와 `bindings/c/perf` 전체 패턴 스모크

### 6.1 core 테스트

```bash
cmake --build core/build -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L regression -j1
```

label 구성이 다르면 `ctest --test-dir core/build -N`으로 lane을 다시 맞춘다.

### 6.2 C binding 빌드

```bash
cmake --build bindings/c/build -j"$(nproc)"
```

### 6.3 core 결과를 바인딩 native 디렉토리에 먼저 동기화

모든 core 테스트와 `bindings/c/perf` 스모크가 끝난 뒤, 바인딩 라이브러리 수정에
들어가기 전에 먼저 native 기준 파일을 각 바인딩 디렉토리에 복사한다.

최소 대상은 아래와 같다.

- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- 바인딩이 별도 복제본으로 들고 있는 native header

예:

- `bindings/go/include/zlink.h`
- `bindings/rust/include/zlink.h`
- `bindings/cpp/include/zlink.h`
- `bindings/c/bench/with_zmq/std_compat/zlink.h`

필요하면 해당 바인딩의 native enum/header 생성 스크립트도 함께 실행한다.
이 단계를 먼저 끝내야 이후 바인딩 래퍼 수정이 모두 같은 native 계약을 기준으로
진행된다.

### 6.4 `bindings/c/perf` single 전체 패턴 스모크

아래 명령은 single의 모든 기본 패턴을 한 번씩 돈다.

```bash
./bindings/c/perf/run_benchmarks.sh \
  --build-dir /home/hep7/project/kairos/zlink/bindings/c/build \
  --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,SPOT \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --duration 1
```

승인 기준:

- report 파일이 `status=complete`로 끝난다.
- 각 패턴의 `64B` 결과가 모두 출력된다.
- auto HWM detail이 남아야 하는 패턴은 표가 깨지지 않고 출력된다.

### 6.5 `bindings/c/perf` multi 전체 패턴 스모크

아래 명령은 multi의 모든 기본 패턴을 한 번씩 돈다.

```bash
./bindings/c/perf/run_benchmarks_multi.sh \
  --build-dir /home/hep7/project/kairos/zlink/bindings/c/build \
  --pattern DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,SPOT,SPOT_REQREP,SPOT_SENDSEND,STREAM \
  --transports tcp \
  --msg-sizes 64 \
  --runs 1 \
  --clients 100 \
  --duration 1
```

승인 기준:

- report 파일이 `status=complete`로 끝난다.
- 첫 패턴 `64B`에서 멈추지 않는다.
- 모든 패턴이 최소 한 줄 이상의 정상 result를 남긴다.
- non-spot 패턴 auto HWM detail 표가 깨지지 않는다.
- spot/spotnode 계열은 snapshot 기반 표가 깨지지 않는다.

### 6.6 perf 실패 시 처리 규칙

- runner가 멈추면 report 파일 마지막 위치를 먼저 기록한다.
- 남아 있는 perf 프로세스를 확인한다.

```bash
pgrep -af "run_benchmarks|run_comparison.py|comp_src_"
```

- stale runtime, hang, timeout, report parser 오류를 구분해서 수정한다.
- 같은 단계 안에서 고치고 다시 스모크를 반복한다.

## 7. 단계 4. 정식 spec / guide / internals 문서 반영

이 단계는 **코드와 테스트가 모두 끝난 뒤** 진행한다.

### 7.1 spec 반영 대상

#### service monitor 제거 관련

- `doc/spec/README.md`
- `doc/spec/core/README.md`
- `doc/spec/core/monitoring.ko.md`
- `doc/spec/core/monitoring.md`
- `doc/spec/core/events.ko.md`
- `doc/spec/core/events.md`
- `doc/spec/core/service/discovery.ko.md`
- `doc/spec/core/service/discovery.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/core/service/registry.md`

#### auto HWM 재계산 관련

- `doc/spec/core/monitoring.ko.md`
- `doc/spec/core/socket/dealer.ko.md`
- `doc/spec/core/socket/dealer.md`
- `doc/spec/core/socket/router.ko.md`
- `doc/spec/core/socket/router.md`
- `doc/spec/core/socket/pub.ko.md`
- `doc/spec/core/socket/pub.md`
- `doc/spec/core/socket/sub.ko.md`
- `doc/spec/core/socket/sub.md`
- `doc/spec/core/socket/stream.ko.md`
- `doc/spec/core/socket/stream.md`
- 필요 시 context option spec 문서

### 7.2 guide 반영 대상

- `doc/guide/02-core-api.ko.md`
- `doc/guide/02-core-api.md`
- `doc/guide/06-monitoring.ko.md`
- `doc/guide/06-monitoring.md`
- `doc/guide/07-0-services.ko.md`
- `doc/guide/07-0-services.md`
- `doc/guide/07-1-discovery.ko.md`
- `doc/guide/07-1-discovery.md`
- `doc/guide/07-3-spot.ko.md`
- `doc/guide/07-3-spot.md`
- `doc/guide/07-4-registry.ko.md`
- `doc/guide/07-4-registry.md`
- `doc/guide/10-performance.ko.md`
- `doc/guide/10-performance.md`
- `doc/guide/11-thread-safety.ko.md`
- `doc/guide/11-thread-safety.md`
- `doc/guide/12-socket-options.ko.md`
- `doc/guide/12-socket-options.md`

### 7.3 internals 반영 대상

- `doc/internals/services-internals.ko.md`
- `doc/internals/services-internals.md`
- `doc/internals/discovery-internals.ko.md`
- `doc/internals/discovery-internals.md`
- `doc/internals/spot-internals.ko.md`
- `doc/internals/spot-internals.md`
- `doc/internals/socket-option-defaults.ko.md`
- `doc/internals/socket-option-defaults.md`
- `doc/internals/stream-socket.ko.md`
- `doc/internals/stream-socket.md`
- `doc/internals/posd-module-structure.ko.md`
- `doc/internals/posd-module-structure.md`

### 7.4 문서 반영 기준

- service monitor 관련 예제와 문구는 snapshot/query 설명으로 바꾼다.
- auto HWM은 새 계산식, debounce, bootstrap, 즉시 재계산 함수를 기준으로 다시 쓴다.
- guide에는 내부 timer/state 세부 구현을 넣지 않는다.
- internals에는 debounce state, context-wide recalc 흐름, bootstrap 적용 지점을 적는다.

## 8. 단계 5. `doc/spec/bindings` 문서 반영

### 8.1 공통 bindings spec

- `doc/spec/bindings/README.md`

아래 항목을 반영한다.

- service monitor capability 제거
- discovery/spotnode/spot 관찰 모델을 snapshot/query 중심으로 재정리
- auto HWM 재계산 option과 context recalc 함수 추가
- perf 정책 문구 갱신

### 8.2 언어별 bindings spec

- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`

각 문서에서 아래를 확인한다.

- service monitor API 소개 제거
- snapshot/query 관찰 모델 설명 추가
- auto HWM context option과 recalc 함수 노출 여부 반영
- 예제 코드에서 삭제된 API 사용 여부 제거

## 9. 단계 6. 각 바인딩 라이브러리 반영

### 9.1 공통 원칙

각 바인딩은 아래를 같은 순서로 반영한다.

1. native header / generated binding surface 갱신
2. 삭제 API 제거
3. 새 auto HWM option / 함수 노출
4. 문서와 샘플 갱신
5. 바인딩 테스트 실행

### 9.2 C / 공용 헤더 복제본

대상:

- `bindings/c/**`
- `bindings/go/include/zlink.h`
- `bindings/rust/include/zlink.h`
- `bindings/cpp/include/zlink.h`
- `bindings/c/bench/with_zmq/std_compat/zlink.h`

작업:

- service monitor 선언 제거
- 새 auto HWM context option / 함수 반영

### 9.3 Go

대상 시작점:

- `bindings/go/discovery.go`
- `bindings/go/monitor_test.go`
- `bindings/go/doc.go`
- `bindings/go/README.godoc.md`
- `bindings/go/samples/**`

작업:

- discovery service monitor wrapper 제거
- service monitor 테스트 제거 또는 snapshot/query 테스트로 교체
- auto HWM context option과 recalc 함수 wrapper 추가

### 9.4 Rust

대상 시작점:

- `bindings/rust/src/monitor.rs`
- `bindings/rust/src/service.rs`
- `bindings/rust/src/ffi.rs`
- `bindings/rust/tests/service_monitor_tests.rs`
- `bindings/rust/samples/**`
- `bindings/rust/README.rustdoc.md`

작업:

- service monitor wrapper와 typed event 제거
- snapshot/query 예제로 대체
- auto HWM context option과 recalc 함수 추가

### 9.5 Java

대상 시작점:

- `bindings/java/src/main/java/dev/kairoscode/zlink/ServiceMonitor.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/ServiceMonitorEventMask.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/service/discovery/Discovery.java`
- `bindings/java/src/main/java/dev/kairoscode/zlink/internal/Native.java`
- `bindings/java/src/test/java/dev/kairoscode/zlink/integration/contract/ServiceContractsIntegrationTest.java`

작업:

- service monitor class 제거
- discovery monitor open wrapper 제거
- auto HWM context option과 recalc method 추가

### 9.6 .NET

대상 시작점:

- `bindings/dotnet/src/Zlink/Native/NativeMethods.Monitor.cs`
- `bindings/dotnet/src/Zlink/Service/Discovery.cs`
- `bindings/dotnet/src/Zlink/EnumValidation.cs`

작업:

- service monitor P/Invoke 제거
- 관련 validation 제거
- auto HWM context option과 recalc wrapper 추가

### 9.7 Node

대상 시작점:

- `bindings/node/tests/monitor.test.ts`
- 검색으로 잡히는 native bridge 파일

작업:

- discovery service monitor 테스트 제거
- snapshot/query 테스트로 대체
- auto HWM context option과 recalc surface 추가

### 9.8 C++

`doc/spec/bindings/cpp/README.md`에 contract를 반영하고, 실제 wrapper가
존재하면 같은 원칙으로 service monitor surface 제거와 auto HWM recalc surface를
추가한다.

### 9.9 Python

`doc/spec/bindings/python/README.md`에 contract를 반영하고, 실제 binding source에
service monitor 노출이 있으면 같은 원칙으로 제거한다.

## 10. 단계 7. 바인딩 테스트와 최종 재검토

### 10.1 바인딩별 테스트

실행 가능한 테스트 entry를 각 binding README와 build script에서 찾아 실행한다.
최소 기준은 아래와 같다.

- Go: `go test ./...`
- Rust: `cargo test`
- Java: `./gradlew test`
- .NET: `dotnet test`
- Node: repository 기준 test command

테스트 entry가 다르면 해당 binding의 README 기준 명령을 우선한다.

### 10.2 남은 참조 검색

최종적으로 아래 검색이 깨끗해야 한다.

```bash
rg -n "zlink_service_monitor_open|zlink_service_monitor_recv|zlink_service_monitor_handler|service monitor" \
  core doc bindings -g '!**/build/**'
```

허용 예외는 아래뿐이다.

- 과거 plan/log 문서
- draft/history 성격의 문서
- 바인딩 정렬 이력 문서

제품 문서, 공개 헤더, 바인딩 코드, 샘플, 테스트에 남아 있으면 실패로 본다.

### 10.3 auto HWM 검색

아래 검색으로 새 계약이 빠진 곳이 없는지 확인한다.

```bash
rg -n "AUTO_HWM_RECALC_DEBOUNCE|AUTO_HWM_STREAM_BOOTSTRAP|AUTO_HWM_SPOT_BOOTSTRAP|zlink_ctx_auto_hwm_recalculate" \
  core doc bindings -g '!**/build/**'
```

spec, guide, internals, binding spec, binding code에서 각각 최소 한 번 이상
반영되어 있어야 한다.

## 11. 최종 승인 기준

아래를 모두 만족해야 작업 완료로 본다.

1. `core/include/zlink.h`, `core/include/zlink_enum.h`가 두 draft 결정과 일치한다.
2. core test lane이 통과한다.
3. `bindings/c/perf` single 전체 패턴 스모크가 통과한다.
4. `bindings/c/perf` multi 전체 패턴 스모크가 통과한다.
5. 정식 spec / guide / internals 문서가 구현과 맞는다.
6. `doc/spec/bindings`가 구현과 맞는다.
7. 실제 바인딩 라이브러리에서 삭제 API가 제거되고 새 auto HWM surface가 반영된다.
8. 남은 service monitor 참조가 제품 surface에 없다.
9. 남은 auto HWM 문서가 옛 role 고정 비율 정책을 현재 계약처럼 설명하지 않는다.

## 12. 작업 로그 권장 형식

실행자는 각 단계마다 아래를 남긴다.

- 시작 시각
- 기준 commit
- 읽은 draft 절 목록
- 수정 파일 목록
- 실행한 build / test / perf 명령
- 실패와 수정 내용
- 남은 위험

로그는 `doc/plan/monitoring/logs/` 아래에 단계별 파일로 남기는 것을 권장한다.

## 13. 실행 로그

### 2026-04-27 최종 리뷰 루프와 검증

- 수정 파일
  - `core/include/zlink.h`
  - `bindings/cpp/include/zlink.h`
  - `bindings/go/include/zlink.h`
  - `bindings/rust/include/zlink.h`
  - `bindings/go/doc.go`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/ZlinkException.java`
  - `bindings/rust/src/ffi.rs`
  - `bindings/rust/tests/run_tests.sh`
  - `bindings/c/bench/with_zmq/std_compat/zlink.h`
  - `doc/spec/core/events*`, `monitoring*`
  - `doc/guide/06-monitoring*`
  - `doc/spec/bindings/cpp/README.md`
  - `doc/site/docs/api/*`, `doc/site/docs/guide/*`, `doc/site/docs/internals/*` 대응 문서
  - `core/tools/bindings-perf/bindings-perf-execution-guide.ko.md`
- 실행 명령
  - `cmake --build core/build -j"$(nproc)"`
  - `ctest --test-dir core/build --output-on-failure -j1`
  - `./bindings/c/perf/run_benchmarks.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1`
  - `./bindings/c/perf/run_benchmarks_multi.sh --pattern ALL --transports tcp --msg-sizes 64 --duration 1 --runs 1 --clients 2`
  - `ctest --test-dir bindings/cpp/build --output-on-failure -j1`
  - `go test ./...`
  - `PYTHONPATH=src:codecs/zlink_codec_json/src:codecs/zlink_codec_messagepack/src:codecs/zlink_codec_protobuf/src python -m pytest -q`
  - `cargo test`
  - `npm test`
  - `./gradlew test --no-daemon`
  - `dotnet test`
- 실패 원인
  - 처음 `ctest --test-dir core/build`를 `cmake --build core/build`와 병렬로 실행해 `libzlink.so.5: file too short`, unittest `permission denied`, `test_helper_request_sequence_failure` 오검출이 함께 발생했다.
  - `bindings/python` 전체 `pytest`는 codec 패키지 import 경로가 빠져 collection 단계에서 `ModuleNotFoundError`가 발생했다.
- 해결
  - `core` 빌드 완료 후 `ctest`를 단독 재실행해 99/99 통과를 확인했다.
  - Python은 codec `src` 디렉터리를 `PYTHONPATH`에 추가해 `54 passed, 10 skipped`로 재검증했다.
- 결과
  - 공개 헤더, 정식 spec/guide/internals, site 복제 문서, 각 바인딩 공개면에서 service monitor 공개 surface 제거 완료.
  - auto HWM 재계산 공개 계약과 새 snapshot 필드가 core/bindings/docs/site에 반영된 상태로 최종 검증 통과.
