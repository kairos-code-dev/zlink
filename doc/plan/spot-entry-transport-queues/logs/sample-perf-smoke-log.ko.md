# Sample And Perf Smoke Log

## 5.3.9 bindings smoke 재검증

- 날짜: 2026-05-06
- 대상: core `5.3.9` release 뒤 bindings smoke 재검증
- 수행한 명령:
  - `bindings/c/samples/run_samples.sh`
  - `bindings/c/perf/run_benchmarks.sh --transports tcp --msg-sizes 64`
  - `bindings/c/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64`
  - `bindings/cpp/tests/run_tests.sh`
  - `bindings/cpp/samples/run_samples.sh`
  - `bindings/cpp/perf/run_benchmarks.sh --transports tcp --msg-sizes 64`
  - `bindings/cpp/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64`
  - `bindings/dotnet/tests/run_tests.sh`
  - `bindings/dotnet/samples/run_samples.sh`
  - `bindings/dotnet/perf/run_benchmarks.sh --transports tcp --msg-sizes 64 --clean-build`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --transports tcp --msg-sizes 64 --clean-build`
  - `bindings/go/tests/run_tests.sh`
  - `bindings/go/samples/run_samples.sh`
  - `bindings/go/perf/run_benchmarks.sh --transports tcp --msg-sizes 64 --clean-build`
  - `bindings/go/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --transports tcp --msg-sizes 64 --clean-build`
  - `bindings/go/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64 --clean-build`
- 확인한 draft spec 절: Actor와 Entry Spot 흐름, STREAM session Actor bind, join/leave, dispatch readiness, snapshot
- 발견한 문제:
  - Go `MULTI_SPOT` full suite에서 기존 data-plane warmup delivery gate가 ready timeout을 만들었다.
  - `doc/perf/PERF_MULTI_TEST_POLICY.md`는 multi SPOT에서 delivery-ready gate를 금지하고 control handshake barrier와 settle window를 요구하므로 Go perf ready gate를 정책에 맞췄다.
- 수정한 파일:
  - `bindings/go/perf/internal/perfcommon/runtime.go`
  - `bindings/go/perf/multi/perf_multi_spot.go`
  - `bindings/go/perf/multi/perf_multi_spot_ready.go`
- 검증 결과:
  - C samples 13/13 통과.
  - C single perf complete: `bindings/c/perf/results/single/report/perf_c_single_linux_20260506_130948.txt`
  - C multi perf complete: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260506_131025.txt`
  - C++ contract tests 8/8 통과, samples 14/14 통과.
  - C++ single perf complete: `bindings/cpp/perf/results/single/report/perf_cpp_single_linux_20260506_131213.txt`
  - C++ multi perf complete: `bindings/cpp/perf/results/multi/report/perf_cpp_multi_linux_20260506_131254.txt`
  - .NET tests 141/141 통과, samples 14/14 통과.
  - .NET single perf complete: `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260506_131423.txt`
  - .NET multi perf complete: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260506_131508.txt`
  - Go tests 통과, samples 14/14 통과.
  - Go single perf complete: `bindings/go/perf/results/single/report/perf_go_single_linux_20260506_131606.txt`
  - Go `MULTI_SPOT` 단일 perf complete: `bindings/go/perf/results/multi/report/perf_go_multi_linux_20260506_132238.txt`
  - Go multi perf complete: `bindings/go/perf/results/multi/report/perf_go_multi_linux_20260506_132300.txt`
  - 모든 perf runner는 `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.9` runtime을 출력했다.
- 남은 위험: java, node, python, rust bindings gate 순차 진행 필요
- 다음 확인: java binding spec/code/sample/perf/POSD gate

## Java Binding Smoke

- 날짜: 2026-05-06
- 대상: Java binding tests, samples, perf smoke
- 수행한 명령:
  - `bindings/java/tests/run_tests.sh`
  - `bindings/java/samples/run_samples.sh`
  - `bindings/java/perf/run_benchmarks.sh --transports tcp --msg-sizes 64 --clean-build`
  - `bindings/java/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --transports tcp --msg-sizes 64 --clean-build`
  - `bindings/java/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64 --clean-build`
- 확인한 draft spec 절: Actor와 Entry Spot 흐름, STREAM session Actor bind, join/leave, dispatch readiness, snapshot
- 발견한 문제:
  - Java Actor native downcall과 facade가 제거된 old Actor handle API 일부를 참조했다.
  - Actor join info native layout이 core와 맞지 않아 sample 실행 중 native memory corruption이 발생했다.
  - Actor samples는 user Spot join 전에 bound STREAM session이 있어야 한다는 계약을 명확히 만족하도록 session lifecycle을 유지해야 했다.
  - Java `MULTI_SPOT` perf는 `doc/perf/PERF_MULTI_TEST_POLICY.md`의 logical spot topology와 active window 집계 기준에 맞지 않았다.
- 수정한 파일:
  - `bindings/java/src/main/java/dev/kairoscode/zlink/internal/Native.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/internal/NativeLayouts.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/internal/LibraryLoader.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/Actor.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/ActorJoinInfo.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/Spot.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/SpotNode.java`
  - `bindings/java/src/main/java/dev/kairoscode/zlink/service/spot/SpotRoutedSupport.java`
  - `bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/ActorRoomServerSample.java`
  - `bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/ActorGatewayRelaySample.java`
  - `bindings/java/samples/Zlink.Samples/src/main/java/dev/kairoscode/zlink/samples/ActorSinglePlayerQueueSample.java`
  - `bindings/java/perf/common/src/main/java/dev/kairoscode/zlink/perf/PerfPolicy.java`
  - `bindings/java/perf/common/src/main/java/dev/kairoscode/zlink/perf/PerfReport.java`
  - `bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/PerfMultiSpot.java`
- 검증 결과:
  - Java tests 통과.
  - Java sample runner 14/14 통과.
  - Java single perf complete: `bindings/java/perf/results/single/report/perf_java_single_linux_20260506_135217.txt`
  - Java `MULTI_SPOT` 단일 perf complete: `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260506_140758.txt`
  - Java multi perf complete: `bindings/java/perf/results/multi/report/perf_java_multi_linux_20260506_140818.txt`
  - perf runner는 `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.9` runtime을 출력했다.
- 남은 위험: node, python, rust bindings gate 순차 진행 필요
- 다음 확인: node binding spec/code/sample/perf/POSD gate

## Node Binding Smoke

- 날짜: 2026-05-06
- 대상: Node binding samples와 perf smoke
- 수행한 명령:
  - `npm run build`
  - `npm run typecheck`
  - `bindings/node/tests/run_tests.sh`
  - `bindings/node/samples/run_samples.sh`
  - `bindings/node/perf/run_benchmarks.sh --transports tcp --msg-sizes 64`
  - `bindings/node/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64`
- 확인한 draft spec 절: Actor와 Entry Spot 흐름, STREAM session Actor bind, join/leave, dispatch readiness, snapshot
- 발견한 문제:
  - Node Actor native addon과 facade가 제거된 old Actor handle API 일부를 참조했다.
  - Actor samples는 user Spot join 전에 bound STREAM session이 있어야 한다는 계약을 명확히 만족하도록 session lifecycle을 유지해야 했다.
  - Node single perf worker 감시는 no-error 정상 경로와 active duration 완료 경로를 ready timeout으로 실패 처리했다. 이는 `doc/perf/PERF_SINGLE_TEST_POLICY.md`의 active phase 의미와 맞지 않는 하네스 버그였다.
- 수정한 파일:
  - `bindings/node/native/src/addon.cc`
  - `bindings/node/native/src/addon_api.h`
  - `bindings/node/native/src/addon_spot.cc`
  - `bindings/node/src/canonical.ts`
  - `bindings/node/tests/socket_surface.test.ts`
  - `bindings/node/tests/socket_surface.typecheck.ts`
  - `bindings/node/samples/actor_room_server_sample.ts`
  - `bindings/node/samples/actor_gateway_relay_sample.ts`
  - `bindings/node/samples/actor_single_player_queue_sample.ts`
  - `bindings/node/perf/single/perf_single_common.ts`
  - `bindings/node/perf/single/perf_pair.ts`
  - `bindings/node/perf/single/perf_pubsub.ts`
  - `bindings/node/perf/single/perf_dealer_dealer.ts`
  - `bindings/node/perf/single/perf_dealer_router.ts`
  - `bindings/node/perf/single/perf_router_router.ts`
  - `doc/spec/bindings/node/README.md`
- 검증 결과:
  - Node typecheck 통과.
  - Node tests 통과.
  - Node sample runner 14/14 통과.
  - Node single perf complete: `bindings/node/perf/results/single/report/perf_node_single_linux_20260506_142650.txt`
  - Node multi perf complete: `bindings/node/perf/results/multi/report/perf_node_multi_linux_20260506_142823.txt`
  - perf smoke는 기본 패턴 세트에 `--transports tcp --msg-sizes 64`만 지정해 실행했다.
- 남은 위험: python, rust bindings gate 순차 진행 필요
- 다음 확인: python binding spec/code/sample/perf/POSD gate

## Python Binding Smoke

- 날짜: 2026-05-06
- 대상: Python binding samples와 perf smoke
- 수행한 명령:
  - `bindings/python/tests/run_tests.sh`
  - `bindings/python/samples/run_samples.sh`
  - `bindings/python/perf/run_benchmarks.sh --transports tcp --msg-sizes 64`
  - `bindings/python/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --transports tcp --msg-sizes 64`
  - `bindings/python/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT_REQREP --transports tcp --msg-sizes 64`
  - `bindings/python/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64`
- 확인한 draft spec 절: Actor와 Entry Spot 흐름, STREAM session Actor bind, join/leave, dispatch readiness, snapshot
- 발견한 문제:
  - Python Actor FFI와 facade가 제거된 old Actor handle API 일부를 참조했다.
  - Actor samples는 user Spot join 전에 bound STREAM session이 있어야 한다는 계약을 명확히 만족하도록 session lifecycle을 유지해야 했다.
  - Python `MULTI_SPOT` perf는 `doc/perf/PERF_MULTI_TEST_POLICY.md`의 logical spot topology와 ready gate 기준에 맞지 않았다.
  - Python multi runner control-line timeout과 `MULTI_SPOT_REQREP` per-request timeout은 기본 policy client 수에서 smoke를 완료하지 못하게 했다.
- 수정한 파일:
  - `bindings/python/src/zlink/_ffi.py`
  - `bindings/python/src/zlink/_spot.py`
  - `bindings/python/src/zlink/__init__.py`
  - `bindings/python/tests/test_core_api_alignment.py`
  - `bindings/python/samples/actor_room_server_sample.py`
  - `bindings/python/samples/actor_gateway_relay_sample.py`
  - `bindings/python/samples/actor_single_player_queue_sample.py`
  - `bindings/python/perf/multi/run_benchmarks.py`
  - `bindings/python/perf/multi/perf_multi_spot_client.py`
  - `bindings/python/perf/multi/perf_multi_spot_server.py`
  - `bindings/python/perf/multi/perf_multi_spot_reqrep_client.py`
  - `doc/spec/bindings/python/README.md`
- 검증 결과:
  - Python tests 55 passed, 10 skipped.
  - Python sample runner 14/14 통과.
  - Python single perf complete: `bindings/python/perf/results/single/report/perf_python_single_linux_20260506_143949.txt`
  - Python `MULTI_SPOT` 단일 perf complete: `bindings/python/perf/results/multi/report/perf_python_multi_linux_20260506_145529.txt`
  - Python `MULTI_SPOT_REQREP` 단일 perf complete: `bindings/python/perf/results/multi/report/perf_python_multi_linux_20260506_145759.txt`
  - Python multi perf complete: `bindings/python/perf/results/multi/report/perf_python_multi_linux_20260506_145925.txt`
  - perf smoke는 기본 패턴 세트에 `--transports tcp --msg-sizes 64`만 지정해 실행했다.
- 남은 위험: rust bindings gate 순차 진행 필요
- 다음 확인: rust binding spec/code/sample/perf/POSD gate

## Rust Binding Smoke

- 날짜: 2026-05-06
- 대상: Rust binding samples와 perf smoke
- 수행한 명령:
  - `bindings/rust/tests/run_tests.sh`
  - `bindings/rust/samples/run_samples.sh`
  - `bindings/rust/perf/run_benchmarks.sh --transports tcp --msg-sizes 64`
  - `bindings/rust/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64`
- 확인한 draft spec 절: Actor와 Entry Spot 흐름, STREAM session Actor bind, join/leave, dispatch readiness, snapshot
- 발견한 문제:
  - Rust Actor facade와 samples가 ref 중심 계약과 STREAM bind-before-join 계약을 모두 반영해야 했다.
  - perf는 정책 변경 없이 기본 패턴 세트에 `--transports tcp --msg-sizes 64`만 지정해 실행했다.
- 수정한 파일:
  - `bindings/rust/src/ffi.rs`
  - `bindings/rust/src/service.rs`
  - `bindings/rust/tests/service_surface_tests.rs`
  - `bindings/rust/samples/actor_room_server_sample.rs`
  - `bindings/rust/samples/actor_gateway_relay_sample.rs`
  - `bindings/rust/samples/actor_single_player_queue_sample.rs`
  - `doc/spec/bindings/rust/README.md`
- 검증 결과:
  - Rust tests 10/10 suites 통과.
  - Rust sample runner 14/14 통과.
  - Rust single perf complete: `bindings/rust/perf/results/single/report/perf_rust_single_linux_20260506_150924.txt`
  - Rust multi perf complete: `bindings/rust/perf/results/multi/report/perf_rust_multi_linux_20260506_151010.txt`
  - perf smoke는 기본 패턴 세트에 `--transports tcp --msg-sizes 64`만 지정해 실행했다.
- 남은 위험: bindings 전체 종료 체크 필요
- 다음 확인: 최종 종료 절차

---

## 2026-05-06

- 날짜: 2026-05-06
- 대상: 단계 11 sample과 perf smoke
- 수행한 명령:
  - `cmake --build core/build`
  - `bindings/c/samples/run_samples.sh`
  - `bindings/c/perf/run_benchmarks.sh --transports tcp --msg-sizes 64`
  - `bindings/c/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64`
- 확인한 draft spec 절: Actor와 Entry Spot 흐름, Gateway/session 흐름, Game room 흐름, Single-player 흐름
- 발견한 문제:
  - 처음에는 전체 transport 기본 실행으로 시작했으나 smoke 범위를 `tcp` transport와 64B 메시지 크기로 정정했다.
  - runner 옵션은 `--transports tcp --msg-sizes 64`다.
- 수정한 파일:
  - `bindings/c/samples/actor_sample_common.h`
  - `bindings/c/samples/actor_room_server_sample.c`
  - `bindings/c/samples/actor_gateway_relay_sample.c`
  - `bindings/c/samples/actor_single_player_queue_sample.c`
  - `doc/spec/sample/SAMPLE_POLICY.md`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 검증 결과:
  - core build 성공.
  - C sample runner 13/13 통과.
  - single perf smoke 성공. 결과 파일:
    `bindings/c/perf/results/single/report/perf_c_single_linux_20260506_111213.txt`
  - multi perf smoke 성공. 결과 파일:
    `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260506_111247.txt`
  - perf runner는 `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.8`
    runtime을 출력했다.
  - stale runtime guard는 runner 시작 시 실행됐다.
- 남은 위험: bindings native library는 core release 뒤 `bindings/update_zlink_libs.sh`로 최신화해야 한다.
- 다음 확인: 구현 후 문서-코드 반복 리뷰와 POSD gate

## .NET Binding Smoke

- 날짜: 2026-05-06
- 대상: .NET binding samples와 perf smoke
- 수행한 명령:
  - `bindings/dotnet/tests/run_tests.sh`
  - `bindings/dotnet/samples/run_samples.sh`
  - `bindings/dotnet/perf/run_benchmarks.sh --transports tcp --msg-sizes 64 --clean-build`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --transports tcp --msg-sizes 64 --clean-build`
- 확인한 draft spec 절: Actor와 Entry Spot 흐름, STREAM session Actor bind, join/leave, dispatch readiness, snapshot
- 발견한 문제:
  - SPOT perf 수신 모델이 정책의 `dispatch_event` drain과 달라 dotnet binding callback delivery를 함께 수정했다.
  - dotnet perf runner는 `core/build` runtime을 명시하도록 정렬했다.
- 수정한 파일:
  - `bindings/dotnet/src/Zlink/Service/Spot.cs`
  - `bindings/dotnet/src/Zlink/Monitor.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfSpot.cs`
  - `bindings/dotnet/perf/single/run_benchmarks.sh`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh`
  - `bindings/dotnet/tests/Zlink.Tests/test_callback_delivery.cs`
  - `doc/spec/bindings/dotnet/README.md`
- 검증 결과:
  - .NET tests 141/141 통과.
  - .NET sample runner 14/14 통과.
  - single perf smoke 성공. 결과 파일:
    `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260506_115005.txt`
  - multi perf smoke 성공. 결과 파일:
    `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260506_115052.txt`
  - perf runner는 `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.8`
    runtime을 출력했다.
- 남은 위험: go, java, node, python, rust binding smoke 순차 진행 필요
- 다음 확인: go binding smoke
