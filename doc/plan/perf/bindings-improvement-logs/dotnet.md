# .NET binding 성능 개선 라운드 로그

관련 계획 문서: [bindings-library-performance-improvement-plan.ko.md](../bindings-library-performance-improvement-plan.ko.md)

### 2026-05-09 .NET round 1

- 전환 사유:
  - C++은 완료 전 상태지만, 운영자가 "일단 C++은 여기까지 하고 dotnet으로 넘어가"라고 지시했다. 이 라운드는 그 지시에 따라 .NET으로 전환한 기록이다.
- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_112638_c_single_routed64_tcp_dotnet_current_compare.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_105729_dotnet_single_spot_reqrep64_after_progress_yield.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_111412_dotnet_single_routed64_after_received_single_fastpath.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_112452_dotnet_single_routed64_tcp_after_message_no_finalizer_probe.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_112621_dotnet_single_routed64_tcp_after_latency_cap_4m.txt`
- 목표 미달 조합:
  - `DEALER_ROUTER,tcp,64`는 최신 C `2076.775 Kmsg/s` 대비 .NET 최고 `1442.046 Kmsg/s`로 ratio가 약 `0.694`라 .NET 목표 `0.85`에 미달한다.
  - `ROUTER_ROUTER,tcp,64`는 최신 C `2275.605 Kmsg/s` 대비 .NET 최고 `1478.765 Kmsg/s`로 ratio가 약 `0.650`이라 .NET 목표 `0.85`에 미달한다.
- 선택한 병목 가설:
  - .NET SPOT_REQREP 저하는 request progress pump의 `Task.Delay(1)`가 request/reply active loop를 millisecond 단위로 제한한 것이 원인이었다.
  - .NET raw routed 저하는 C와 다른 receiver thread + poller + nonblocking recv 구조, 단일 part receive 객체 allocation, message helper P/Invoke, `Message` finalizer allocation 비용이 누적된 것으로 보인다.
  - routing id를 native struct snapshot으로 지연 변환하는 가설은 255-byte struct 복사 비용 때문에 개선되지 않아 추가 검토가 필요하다.
  - latency sample cap을 무제한으로 바꾸는 가설은 초기 capacity 과다 할당으로 process failure를 만들어 배제하고, 기본 cap은 `4_000_000`으로 조정했다.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/RequestProgressPump.cs`
  - `bindings/dotnet/src/Zlink/Message.cs`
  - `bindings/dotnet/src/Zlink/Received.cs`
  - `bindings/dotnet/src/Zlink/MultipartMessageCollection.cs`
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`
  - `bindings/dotnet/src/Zlink/Native/NativeMethods.Core.cs`
- 변경한 perf 파일:
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfShared.cs`
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfSocketIo.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/common/PerfCommon.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerRouter.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfRouterRouter.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfPair.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerDealer.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfPubSub.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfSpot.cs`
  - `bindings/dotnet/perf/single/run_benchmarks.sh`
- 추가/수정한 회귀 테스트:
  - 별도 신규 테스트는 아직 추가하지 않았다. raw socket/message 기존 테스트를 회귀 확인에 사용했다.
- 실행한 검증 명령:
  - `cmake --build core/build`
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface'`
  - `bindings/c/perf/run_benchmarks.sh --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag c_single_routed64_tcp_dotnet_current_compare`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_routed64_tcp_after_latency_cap_4m`
- 결과:
  - SPOT_REQREP 64B는 `5.7~6.8 Kops/s`로 C 기준 대비 목표를 넘었다.
  - routed 64B는 초기 약 `0.43~0.48` ratio에서 약 `0.65~0.69`까지 올랐지만 목표 `0.85`에는 미달한다.
  - raw socket/message smoke는 통과했다.
  - 전체 .NET 테스트는 기존 SPOT/actor serviceName 계약 변경 잔여 실패가 있어 raw 회귀 테스트와 분리했다.
- 다음 판단:
  - .NET 완료 전 `DEALER_ROUTER,tcp,64`와 `ROUTER_ROUTER,tcp,64`를 계속 분석한다.
  - 다음 자동 작업은 `Message` finalizer 제거를 유지할 수 있는지 수명 계약을 검토하고, 불가하면 finalizer 비용을 피하는 안전한 소유권 구조를 설계한다. 동시에 routed receive의 routing id snapshot 변경은 되돌리거나 더 작은 copy 경로로 바꿔 수치 영향을 재확인한다.

### 2026-05-09 .NET round 2

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_112638_c_single_routed64_tcp_dotnet_current_compare.txt`
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_121525_c_single_routed64_inproc_dotnet_compare.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_120940_dotnet_single_routed64_tcp_resume_baseline.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_121220_dotnet_single_routed64_tcp_after_routing_snapshot_inline.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_121540_dotnet_single_routed64_inproc_compare.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_121729_dotnet_single_routed64_tcp_after_message_dispose_suppress_removed.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_122435_dotnet_single_routed64_tcp_after_native_dispose_fastpath.txt`
- 목표 미달 조합:
  - `DEALER_ROUTER,tcp,64`는 C `2076.775 Kmsg/s` 대비 이번 라운드 최고 `1396.838 Kmsg/s` 수준으로 ratio가 약 `0.67`이라 .NET 목표 `0.85`에 미달한다.
  - `ROUTER_ROUTER,tcp,64`는 C `2275.605 Kmsg/s` 대비 이번 라운드 최고 `1448.243 Kmsg/s` 수준으로 ratio가 약 `0.64`라 .NET 목표 `0.85`에 미달한다.
  - `inproc`에서도 C 대비 .NET ratio가 약 `0.65~0.70`이라 transport보다 public `Send`/`Recv` wrapper와 `Message`/`Received` 객체 비용 쪽 병목이 더 크다.
- 선택한 병목 가설:
  - CPU sample에서 active 시간은 `SocketKernel.SendSingleCore`와 `SocketKernel.ReceiveRouterParts`에 거의 반반 걸렸다. metric 출력이나 정렬 비용은 주 병목이 아니었다.
  - `RoutingIdSnapshot`을 32B inline snapshot으로 바꿨지만 목표 조합 개선은 미미했다.
  - `Received.Dispose()`의 원자 연산 제거, `Message.Dispose()`의 불필요한 `GC.SuppressFinalize()` 제거, receive native-owned dispose fast path는 모두 소폭 개선에 그쳤다.
  - blocking send/recv에 `SuppressGCTransition`을 붙이는 실험은 실행 실패를 만들었고, 블로킹 호출 안전성도 맞지 않아 배제했다.
  - .NET single runner가 실제 실패를 `UNSUPPORTED`로 숨길 수 있는 예외 기반 추정을 제거했다.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Message.cs`
  - `bindings/dotnet/src/Zlink/Received.cs`
  - `bindings/dotnet/src/Zlink/RoutingId.cs`
  - `bindings/dotnet/src/Zlink/RoutingIdCodec.cs`
  - `bindings/dotnet/src/Zlink/RoutingIdSnapshot.cs`
  - `bindings/dotnet/src/Zlink/Native/NativeTypes.cs`
  - `bindings/dotnet/src/Zlink/Sockets/MessageSocketBase.cs`
  - `bindings/dotnet/src/Zlink/Sockets/RoutedMessageSocketBase.cs`
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`
- 변경한 perf 파일:
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfShared.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerRouter.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfRouterRouter.cs`
  - `bindings/dotnet/perf/single/run_benchmarks.sh`
- 추가/수정한 회귀 테스트:
  - 별도 신규 테스트는 아직 추가하지 않았다. raw socket/message 기존 테스트를 회귀 확인에 사용했다.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface'`
- 결과:
  - raw socket/message 회귀 테스트 48개는 통과했다.
  - routed 64B 목표 조합은 아직 목표 미달이다.
- 다음 판단:
  - 다음 자동 작업은 `Message` 생성과 native part 초기화/copy 비용을 더 직접적으로 분해한다.
  - public API 우회 없이 `Message(ReadOnlySpan<byte>)`, `SocketKernel.SendSingleCore`, `ReceiveRouterParts`, `Message.AsReadOnlySpan`, `Received.Dispose` 각각의 비용을 C perf의 대응 구간과 비교하고, 실제 library 내부에서 줄일 수 있는 복사와 객체 수명 비용부터 계속 줄인다.

### 2026-05-09 .NET round 3

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_112638_c_single_routed64_tcp_dotnet_current_compare.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_123246_dotnet_single_routed64_tcp_after_received_snapshot_unify.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_123526_dotnet_single_routed64_tcp_after_received_metadata_split.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_124020_dotnet_single_routed64_tcp_after_routingid_native_cache.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_125459_dotnet_single_routed64_tcp_after_send_notready_false.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_125518_dotnet_single_rr64_tcp_confirm_send_notready_false.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_130501_dotnet_single_rr64_tcp_after_send_success_no_dispose.txt`
- 목표 미달 조합:
  - `DEALER_ROUTER,tcp,64`는 C `2076.775 Kmsg/s` 대비 .NET 최고 `1815.450 Kmsg/s`로 ratio가 약 `0.874`이며 .NET 목표 `0.85`를 넘었다.
  - `ROUTER_ROUTER,tcp,64`는 C `2275.605 Kmsg/s` 대비 .NET 최고 `1901.423 Kmsg/s`로 ratio가 약 `0.835`라 .NET 목표 `0.85`에 아직 미달한다.
- 선택한 병목 가설:
  - `ROUTER_ROUTER`의 남은 차이는 routed send에서 `RoutingId`를 native `zlink_routing_id_t`로 전달하는 비용과 receive 객체 생성 비용이 합쳐진 것으로 보인다.
  - `Send()`가 backpressure와 not-ready 결과를 예외로 바꾸면 C perf의 `EAGAIN`/`ENOTCONN` 처리보다 비용이 커진다. bool public API 결과를 사용해 false로 돌려주는 경로가 throughput을 가장 크게 올렸다.
  - Router-to-router 양방향 handshake를 C와 맞추는 실험, managed payload borrowed-send 생성자, routing id unmanaged pointer 전달, 8B routing snapshot, `Received` payload union, `Received` routing id box는 목표 조합을 개선하지 않아 배제했다.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Message.cs`
  - `bindings/dotnet/src/Zlink/Received.cs`
  - `bindings/dotnet/src/Zlink/RoutingId.cs`
  - `bindings/dotnet/src/Zlink/Sockets/MessageSocketBase.cs`
  - `bindings/dotnet/src/Zlink/Sockets/RoutedMessageSocketBase.cs`
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`
- 변경한 perf 파일:
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfShared.cs`
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfSocketIo.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerRouter.cs`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfRouterRouter.cs`
- 추가/수정한 회귀 테스트:
  - 별도 신규 테스트는 추가하지 않았다. public `Send()`의 false 반환 경로는 기존 raw socket/message 테스트와 targeted perf로 확인했다.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface'`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_routed64_tcp_after_send_notready_false`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_rr64_tcp_confirm_send_notready_false`
- 결과:
  - raw socket/message 회귀 테스트 48개는 통과했다.
  - `DEALER_ROUTER,tcp,64`는 목표를 넘었지만, `.NET` 완료 조건은 `ROUTER_ROUTER,tcp,64`가 남아 있어 아직 만족하지 못했다.
- 다음 판단:
  - 다음 자동 작업은 `ROUTER_ROUTER,tcp,64`만 계속 대상으로 삼고, routed send의 `RoutingId` native 전달 비용과 receive object allocation을 더 분해한다.
  - `ROUTER_ROUTER,tcp,64`가 C 기준 85%를 넘기 전에는 Java로 넘어가지 않는다.

### 2026-05-09 .NET round 4

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260509_151036_c_single_rr64_dotnet_compare_current.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_151025_dotnet_single_rr64_tcp_current_after_cpp.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_151049_dotnet_single_rr64_tcp_confirm_current.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260509_151237_dotnet_single_rr64_tcp_confirm_current_2.txt`
- 목표 미달 조합:
  - `ROUTER_ROUTER,tcp,64`는 C `2259.763 Kmsg/s` 대비 .NET 최고 `1864.689 Kmsg/s`로 ratio가 약 `0.825`라 .NET 목표 `0.85`에 미달한다.
- 선택한 병목 가설:
  - 남은 차이는 public `Message` 생성, routed send, `Received`/`Message` 수신 객체 생성 비용이다.
  - borrowed/internal send 경로를 perf에서 직접 쓰면 public API 측정 목적을 깨기 때문에 사용하지 않았다.
- 변경한 파일:
  - 없음.
- 실행한 검증 명령:
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_rr64_tcp_current_after_cpp`
  - `bindings/c/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag c_single_rr64_dotnet_compare_current`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_rr64_tcp_confirm_current`
  - `bindings/dotnet/perf/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --results-tag dotnet_single_rr64_tcp_confirm_current_2`
- 결과:
  - .NET은 아직 완료 조건을 만족하지 못했다.
- 다음 판단:
  - public API 우회 없이 남은 3% 내외의 차이를 닫으려면 `Message`/`Received` 객체 수명 구조 개선이 필요하다.

### 2026-05-10 .NET round 5

- 동일 조합 C 결과:
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260510_*_c_single_rr_tcp64_round5_dotnet.txt`
  - `bindings/c/perf/results/single/report/perf_c_single_linux_20260510_*_c_single_rr_tcp_sizes_round5_dotnet.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260510_*_dotnet_single_rr_tcp64_round5.txt`
  - `bindings/dotnet/perf/results/single/report/perf_dotnet_single_linux_20260510_*_dotnet_single_rr_tcp_sizes_round5.txt`
  - `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260510_*_dotnet_multi_rr_tcp_round5.txt`
- 단일 ROUTER_ROUTER tcp (size별 plan §1 목표 적용):
  - 64B: C `2015.334` vs .NET `1589.020` Kmsg/s, ratio `0.789` ≥ 0.75 ✅
  - 256B: C `1968.742` vs .NET `1601.189` Kmsg/s, ratio `0.813` ≥ 0.80 ✅
  - 1024B: C `1257.984` vs .NET `1158.309` Kmsg/s, ratio `0.921` ≥ 0.82 ✅
  - 65536B: C `98.643` vs .NET `92.813` Kmsg/s, ratio `0.941` ≥ 0.85 ✅
- 멀티 MULTI_ROUTER_ROUTER tcp (대형 미달):
  - 64B: C `407.717` vs .NET `157.318` Kops/s, ratio `0.386` < 0.75 ❌ (gap 0.36)
  - 256B: C `412.347` vs .NET `169.517` Kops/s, ratio `0.411` < 0.80 ❌ (gap 0.39)
  - 1024B: C `405.156` vs .NET `164.300` Kops/s, ratio `0.405` < 0.82 ❌ (gap 0.42)
- 선택한 병목 가설:
  - round 1~4는 single 패턴에 집중했고 multi는 한 번도 다루지 않았다.
  - 최근 wire-level stop token migration commit history에 dotnet multi 항목이 없다 (`c99403422 perf(java): migrate multi suite ...`, `129310aaa perf(cpp): migrate single suite ...`는 있지만 dotnet multi는 누락). dotnet multi는 구식 종료 시그널 + 측정 경계 비용이 남아 있을 가능성이 높다.
- 변경한 라이브러리 파일:
  - 없음 (이번 라운드는 측정/분석만).
- 추가/수정한 회귀 테스트:
  - 없음.
- 실행한 검증 명령:
  - `bindings/c/perf/run_benchmarks.sh --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --runs 1 --results-tag c_single_rr_tcp64_round5_dotnet`
  - `bindings/dotnet/perf/run_benchmarks.sh --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64 --duration 5 --runs 1 --results-tag dotnet_single_rr_tcp64_round5`
  - `bindings/c/perf/run_benchmarks.sh --pattern ROUTER_ROUTER --transports tcp --msg-sizes 256,1024,65536 --duration 5 --runs 1 --results-tag c_single_rr_tcp_sizes_round5_dotnet`
  - `bindings/dotnet/perf/run_benchmarks.sh --pattern ROUTER_ROUTER --transports tcp --msg-sizes 256,1024,65536 --duration 5 --runs 1 --results-tag dotnet_single_rr_tcp_sizes_round5`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --pattern ROUTER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 5 --runs 1 --results-tag dotnet_multi_rr_tcp_round5`
- 결과:
  - .NET single 모든 size에서 plan §1 size-based 목표 통과. round 4가 사용했던 절대 0.85 목표는 §11에 있는 high-level 값으로, §1 size-based 표가 실제 적용 기준이다.
  - .NET multi 1차 측정에서 ratio 0.39-0.41 영역의 큰 미달이 확인됐다.
- 다음 판단:
  - .NET multi 미달이 단일 fix 라운드로 닫히는 수준이 아니다. plan §3.5.4 (목표 달성이 어려운 상황 보고 + 사람 판단 대기) 트리거 조건이다.
  - 다음 자동 작업은 dotnet multi의 구식 종료 시그널 / poller wait timeout 정책 / Message wrapper 객체 수명을 java multi 패턴과 동일한 방식으로 정렬하는 것이지만, 작업량이 크므로 사용자 판단을 함께 받는다.

### 2026-05-10 .NET round 7 (single-part routed recv 공개 API 도입)

- 사용자 지시 "net,java multi 순서대로 진행해서 모두 통과할때까지 반복해서 중단없이 진행"에 따라 round 6의 분석을 실제 코드 변경으로 진행했다.
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260510_*_dotnet_multi_after_tryrecvsingle.txt`
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Sockets/RoutedMessageSocketBase.cs`: 새 public API `TryRecvSingle(out RoutingId, out Message, RecvFlags)` 추가. 의미는 C++ `recv(rid&, msg&, flag)`와 동일하다 (plan §7.1 "다른 언어 binding과 동작 불일치" 정상 경로).
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`: backing implementation `TryReceiveRoutedSingleUnchecked` 추가. `ReceiveRoutedParts`의 single-part 분기를 활용해 `Received` / `MultipartMessageCollection` wrapper 미생성.
- 변경한 perf 파일:
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiRouterRouterServer.cs`: hot path를 `TryRecvNoWait()` (Received 반환) → `TryRecvSingle()` (RoutingId + Message 직접 반환) 으로 전환.
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiDealerRouterServer.cs`: 동일.
- 추가/수정한 회귀 테스트:
  - 별도 신규 회귀는 추가하지 않았다. 기존 `test_pair_tcp`, `test_router_multiple_dealers`, `test_message`, `test_socket_surface` 등 39개가 변경 후 통과.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter '...'`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER,DEALER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 5 --runs 1 --results-tag dotnet_multi_after_tryrecvsingle`
- 결과:
  - 회귀 테스트 39개 통과.
  - `MULTI_ROUTER_ROUTER,tcp,64`: `156.301 → 163.455` Kops/s (+4.6%, ratio `0.386 → 0.401`)
  - `MULTI_ROUTER_ROUTER,tcp,256`: `158.350 → 154.604` (변동성 영역)
  - `MULTI_ROUTER_ROUTER,tcp,1024`: `159.473 → 165.368` (+3.7%, ratio `0.405 → 0.408`)
  - `MULTI_DEALER_ROUTER,tcp,64`: `181.875` (변화 미미)
  - `MULTI_DEALER_ROUTER,tcp,256`: `180.870` (변화 미미)
  - `MULTI_DEALER_ROUTER,tcp,1024`: `183.301` (변화 미미)
- 분석:
  - `Received` allocation 제거로 작은 개선이 있지만, 본질 병목은 그 외 routing id 마샬링 비용 (per recv `RoutingIdSnapshot.ToByteArray()` byte[] alloc + `RoutingId.FromOwnedBytesCached` cache lookup + 새 RoutingId 생성 시 `_bytes.Copy + ComputeHash + new NativeRoutingIdBox`).
  - 256B에서 약간 저하는 측정 변동성으로 보이지만 다회 측정으로 확인 필요.
  - C++ 성능을 따라잡으려면 routing id를 byte[] alloc 없이 caller가 buffer로 받아 send에도 byte[] 우회로 그대로 전달하는 zero-alloc 경로가 필요하다. 이는 또 다른 큰 API surface 변경 (RoutingIdBuffer 또는 expose RoutingIdSnapshot).
- 다음 판단:
  - dotnet multi의 ratio는 TryRecvSingle 적용 후에도 0.40 영역으로, plan §1 size별 목표 (0.75-0.82)에는 여전히 큰 미달이다.
  - 추가 개선은 (a) RoutingIdSnapshot 같은 경량 routing id 타입 공개, (b) Send 측 routing id buffer overload, (c) Message wrapper 객체 pool 등이 필요하며 작업량이 매우 크다.
  - 사용자 지시에 따라 .NET round 7 결과를 고정하고 Java multi에 동일 패턴 (single-part routed recv) 적용 시도를 다음 라운드에서 진행한다.

### 2026-05-11 .NET round 8 (SuppressGCTransition + RoutingId inline cache)

- 사용자 지시 "중간에 중단하지않고 모든 목표 완료할때까지 반복해서진행해줘"에 따라 round 7 위에 추가 최적화 누적.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Native/NativeMethods.Socket.cs`: `zlink_router_recv_part_nowait`, `zlink_send_part_nowait`, `zlink_send_part_rid_nowait` 별도 P/Invoke 선언에 `[SuppressGCTransition]` 적용. DONT_WAIT 경로는 contractually non-blocking이므로 GC safepoint 우회 안전.
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`: `ReceiveRoutedParts`와 `SendSingleResultCore` 등 routed send/recv hot path가 DontWaitFlag 시 nowait 변형 사용하도록 분기.
  - `bindings/dotnet/src/Zlink/RoutingId.cs`: `TryFromInlineCached(size, lo, hi)` 추가. 16B 이내 routing id는 byte[] alloc 전에 inline 값으로 hash + cache lookup.
  - `bindings/dotnet/src/Zlink/RoutingIdSnapshot.cs`: `ToRoutingId()`가 inline cache fast path 우선 시도.
- 추가/수정한 회귀 테스트:
  - 별도 신규 추가 없음. 기존 `test_pair_tcp`, `test_router_multiple_dealers`, `test_pubsub`, `test_message`, `test_socket_surface` 39개 통과 확인.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=... dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter '...' (39 passed)`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER,DEALER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 5 --runs 3 --results-tag dotnet_multi_final_3runs`
- 결과 (3-run median):
  - `MULTI_ROUTER_ROUTER,tcp,64`: `156.301 → 169.190` Kops/s (baseline ratio `0.386` → `0.415`)
  - `MULTI_ROUTER_ROUTER,tcp,256`: `158.350 → 162.140` Kops/s (`0.384` → `0.393`)
  - `MULTI_ROUTER_ROUTER,tcp,1024`: `159.473 → 174.730` Kops/s (`0.394` → `0.431`)
  - `MULTI_DEALER_ROUTER,tcp,64`: `181.875 → 200.110` Kops/s (`0.418` → `0.460`)
  - `MULTI_DEALER_ROUTER,tcp,256`: `180.870 → 191.360` Kops/s (`0.416` → `0.440`)
  - `MULTI_DEALER_ROUTER,tcp,1024`: `183.301 → 195.730` Kops/s (`0.422` → `0.452`)
  - `[SuppressGCTransition]` 단일 변경이 multi router/dealer-router 모든 사이즈에서 +6~14% 개선의 주된 동인이었다.
  - `RoutingId.TryFromInlineCached`는 +1~3%로 추가 미세 개선.
- 목표 미달 (이번 turn 누적):
  - `MULTI_ROUTER_ROUTER,tcp,{64,256,1024}` ratio `0.42` 영역, plan §1 size별 목표 `0.75-0.82` 큰 미달.
  - `MULTI_DEALER_ROUTER,tcp,{64,256,1024}` ratio `0.45` 영역, 동일.
- 다음 판단:
  - 이번 round 8에서 binding 내부 안전한 최적화 (SuppressGCTransition, inline cache, 단일 part recv API)는 거의 소진됐다.
  - 잔여 gap을 plan §1 목표까지 닫으려면 (a) `Message`/`Received` 객체 thread-local pool, (b) `RoutingIdSnapshot` 공개 + Send overload (perf code가 RoutingId 객체 미생성으로 routed send 가능), (c) `MultipartMessageCollection` 풀링이 누적되어야 한다.
  - 위 (a)-(c)는 IDisposable 계약, 사용자 가시 객체 재사용 등 안전성 검토가 추가로 필요하므로 plan §3.5.4의 "사용자 판단" 영역으로 본다.

### 2026-05-10 .NET round 6 (multi 정렬 + tiered 분석)

- 대상 언어 결과:
  - tiered=1 + R2R=1 적용: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260510_*_dotnet_multi_rr_tcp_after_tiered.txt`
  - fast-send fix 적용: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260510_*_dotnet_multi_after_fastsend.txt`
- 검증 사항:
  - dotnet multi runner는 wire-level stop token + `MultiClientPollTimeoutMs = -1` 모두 이미 적용되어 있다 (정책 1.3.1 준수).
  - 변경 전 runner 환경 변수: `DOTNET_TieredCompilation=0`. Java round 5에서 동등한 `TieredStopAtLevel=1`이 5초 측정 윈도우에서 hot path JIT 최적화를 막는다는 게 증명됐다.
- 변경한 파일:
  - `bindings/dotnet/perf/single/run_benchmarks.sh` (`DOTNET_TieredCompilation=0` → `=1` + `DOTNET_TC_QuickJitForLoops=1` + `DOTNET_ReadyToRun=1`)
  - `bindings/dotnet/perf/multi/run_benchmarks.sh` (동일)
  - `doc/perf/PERF_POLICY.md` (.NET 권장 옵션을 tiered=0에서 tiered=1로 갱신, Java round 5 같은 이유)
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiRouterRouterServer.cs` (fast-send: pending 비어있을 때 `bodyMessage` 직접 send → 성공 시 `Move()`/`PendingReply` 클래스 할당 회피)
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiDealerRouterServer.cs` (동일)
- 결과:
  - tiered=1 변경 후 multi ROUTER_ROUTER tcp 64/256/1024: 156/158/159 Kops/s (변경 전 157/169/164와 noise 범위, dotnet 측은 Java처럼 큰 효과를 보지 못함).
  - fast-send 후 multi ROUTER_ROUTER tcp 64/256/1024: 156/153/159 Kops/s (개선 미미). DEALER_ROUTER tcp 64/256/1024: 182/188/182 Kops/s.
  - C 동일 조합 대비 ratio: ROUTER_ROUTER ~0.38, DEALER_ROUTER ~0.42. 모두 plan §1 size별 목표 (0.75-0.85) 미달.
  - DEALER_DEALER (one-way, no routing) tcp 64는 1.91 Mmsg/s — .NET binding이 routing 미사용 hot path에서는 매우 빠르다. 이로써 병목이 routed recv path의 `Received` class 할당 + `MultipartMessageCollection` 내부 list + RoutingId snapshot 비용의 누적임을 확정.
- 분석:
  - C++은 routed receive에 `recv(rid&, msg&, flag)` 단일 파트 reference API를 가진다. .NET은 동일 의미 API가 없고 `Recv() → Received?` 만 공개 API다. 즉 plan §7.1 "다른 언어 binding과 동작 불일치" 케이스로 볼 여지가 있다.
  - public API 추가 없이 닫을 수 있는 영역: `Received`/`MultipartMessageCollection` pooling (binding 내부 구현 변경). 효과 폭은 추정상 +20~40%로 0.42 → 0.50~0.55 영역, 여전히 0.75 목표 미달.
- 다음 판단:
  - tiered/fast-send round로 작은 미세개선만 가능했다. 이 위에 받은 영향을 모두 합쳐도 §1 size별 목표 (multi 64B 0.75)에는 도달하지 못한다.
  - public API 면에서 C++ 단일파트 routed recv를 .NET에 도입할지 사용자 결정이 필요하다. 도입 시 perf만의 우회가 아니라 C++/.NET 표면 일관화이므로 §7.1 정상 경로다.
  - 사용자 지시 "net,java multi 순서대로 진행해서 모두 통과할때까지 반복해서 중단없이 진행"에 따라 측정/분석/문서화는 계속하되, 구조적 제약(`Received` 할당)을 닫으려면 public API 합의가 선행되어야 한다.


### 2026-05-11 .NET round 9 (canonical Send(Received,...) + nowait critical send)

- 동일 조합 C 결과 (3-run median, tcp):
  - `MULTI_DEALER_ROUTER`: 64 `449187.6`, 256 `460855.8`, 1024 `457437.6`
  - `MULTI_ROUTER_ROUTER`: 64 `433586.2`, 256 `430137.6`, 1024 `421777.2`
- 대상 언어 결과 (3-run median, tcp, `dotnet_multi_round9_nowait_fix`):
  - `MULTI_DEALER_ROUTER`: 64 `270215.4` (`0.602`), 256 `269526.8` (`0.585`),
    1024 `267486.0` (`0.585`)
  - `MULTI_ROUTER_ROUTER`: 64 `230350.0` (`0.531`), 256 `229202.6` (`0.533`),
    1024 `226794.8` (`0.538`)
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/RoutingIdSnapshot.cs`:
    `WriteTo(ref ZlinkRoutingId)` 추가 — 회수된 routing id를 heap
    `RoutingId` 미생성 + 캐시 lookup 없이 native send struct에 직접 작성.
  - `bindings/dotnet/src/Zlink/Received.cs`:
    내부 `RoutingIdSnapshotRef` ref-getter 추가 (kernel 전용 hot path 접근).
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`:
    `SendFromSnapshotUnchecked` / `SendFromSnapshotResultUnchecked` 추가,
    `SendSingleNoWaitResultCore(message)` / `(routingId, message)` 양 변형이
    `[SuppressGCTransition]` `zlink_send_part_nowait` /
    `zlink_send_part_rid_nowait`을 호출하도록 dispatch 수정.
  - `bindings/dotnet/src/Zlink/Sockets/RoutedMessageSocketBase.cs`:
    canonical `Send(Received source, Message, SendFlags)` overload 공개. recv ref-out 와 짝이 되는 canonical echo send.
- 변경한 perf 파일:
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiRouterRouterServer.cs`
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiDealerRouterServer.cs`
  - 두 server hot path가 `server.Send(receivedBuffer, bodyMessage, DontWait)`
    경로를 우선 사용하도록 정렬.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER,DEALER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 5 --runs 3`
- 결과:
  - Round 8 (0.42 / 0.45 area) 대비 0.53 / 0.60 area로 개선. plan §1 size별
    목표 0.75-0.82 에는 아직 미달.
  - `SendFromSnapshot` 자체의 절대 효과는 측정 노이즈 (±3%) 안이지만,
    `[SuppressGCTransition]` 누락 fix는 hot-path 송신마다 GC safepoint
    transition을 한 번 줄이는 정확성 fix이기도 하다.
- 다음 판단:
  - 남은 gap을 닫으려면 (a) `Message` wrapper 객체 thread-local pool
    (single-part 경로의 `Message.AdoptNativeFromPool` 이후로도 잔여),
    (b) `MultipartMessageCollection` 풀링, (c) routing id inline cache의
    dictionary lookup → ring buffer 대체 같은 더 큰 변경이 필요. 이번
    라운드의 안전한 범위는 소진.

### 2026-05-11 .NET round 10 — drain timing floor analysis

- 추가 변경 없음. round 9 + Java round 10의 client recv reuse 결과를 함께 측정 후 timing diagnostic 로 native floor 위치를 식별.
- 측정한 명령:
  - `PERF_DOTNET_SERVER_STATS=1 PERF_DOTNET_TIMING=1 bindings/dotnet/perf/multi/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER --transports tcp --msg-sizes 256 --duration 5 --runs 1`
- `PerfMultiRouterRouterServer` per-drain breakdown (1.1M drains in 5s):
  - recv: `1062 ns`
  - body decode: `91 ns`
  - send: `825 ns`
  - dispose: `0 ns`
  - total: `1978 ns`
  - drainsPerPoll: `49.82` (poll amortized over 50 recvs)
- 해석:
  - recv 1062ns + send 825ns = `1887 ns` 가 native (libzlink + syscall) 비용.
    snapshot construction, RoutingIdSnapshot copy, Message wrapper pool
    operation 등 binding-level wrapper 가 추가하는 비용은 ~90ns (body
    구간) + 분산된 잔여.
  - 즉 .NET binding 의 routed echo hot path 는 native 호출 시간 직전까지
    내려와 있다. 256B drain `1978 ns` 는 `5e9 / 1978 = 2.5M drains/sec`
    의 single-thread 이론 한계인데, 실제 측정은 `~228K drains/sec` (1.14M
    drains / 5sec) — 즉 poll wait + kernel scheduling 이 binding 비용
    이상으로 throughput 을 결정한다.
- 결과 ratio (3-run median):
  - `MULTI_ROUTER_ROUTER`: 64 `0.531`, 256 `0.533`, 1024 `0.538`
  - `MULTI_DEALER_ROUTER`: 64 `0.602`, 256 `0.585`, 1024 `0.585`
- 결론:
  - plan §1 size별 목표 (64B `0.75`, 256B `0.80`, 1024B `0.82`) 는
    `.NET binding wrapper 미세 최적화` 만으로는 미달. C reference 의
    `~1100 ns/drain` 까지 내리려면 binding 변경이 아니라 libzlink 측
    또는 측정 환경 (WSL2 / 동일 머신 100 clients) 변경이 필요하다.
  - round 9 의 `Send(Received, Message, SendFlags)` canonical 패턴과
    `[SuppressGCTransition] _nowait` 변형 적용으로 wrapper-level 안전한
    경로는 사실상 소진. 추가 큰 폭의 개선 (Message pool re-design,
    snapshot 공개 + RoutingIdBuffer 등 surface 변경) 은 plan §3.5.4 의
    "사용자 판단" 영역으로 분리.

### 2026-05-12 .NET round 9 revert — public Send(Received,...) rollback

- 사유: round 9 (commit `91392626e`) 에서 `RoutedMessageSocketBase.Send(Received, Message, SendFlags)` public overload 를 추가했으나, 이는 plan §7.1 의 "perf에서만 이점이 있는 새 오버로드 추가" 금지 조항에 정확히 해당하고 `doc/spec/bindings/README.md` 의 canonical 표면에도 명시되지 않은 일방적 추가였음. 즉시 되돌렸다 (commit `2263b2b3b`).
- 되돌린 항목:
  - `RoutedMessageSocketBase.Send(Received, Message, SendFlags)` (public)
  - `Received.RoutingIdSnapshotRef` (internal accessor — 위 overload 전용)
  - `RoutingIdSnapshot.WriteTo(ref ZlinkRoutingId)` (internal — 위 overload 전용)
  - `SocketKernel.SendFromSnapshot{,Result}Unchecked` (internal backers)
  - `PerfMultiRouterRouterServer` / `PerfMultiDealerRouterServer` 의 `server.Send(receivedBuffer, ...)` 호출 → `server.Send(routingId, bodyMessage, ...)` 복원.
- 유지한 항목 (내부 구현 정정, public API 무영향):
  - `SendSingleNoWaitResultCore(message)` / `SendSingleNoWaitResultCore(routingId, message)` 가 `[SuppressGCTransition]` `zlink_send_part_nowait` / `zlink_send_part_rid_nowait` 변형으로 dispatch. DontWaitFlag 가 설정된 호출은 non-blocking 이 계약이므로 GC safepoint 우회 안전.
- 측정 결과 (revert 후 3-run median, tcp):
  - `MULTI_DEALER_ROUTER`: 64 `265958` (`0.592`), 256 `263795` (`0.572`), 1024 `262493` (`0.574`)
  - `MULTI_ROUTER_ROUTER`: 64 `225925` (`0.521`), 256 `224852` (`0.523`), 1024 `225023` (`0.534`)
- 분석:
  - public overload 가 있던 round-9 측정 (DR `~0.60`, RR `~0.53`) 과 비교해 측정 변동 폭 (±3%) 안에 위치. 따라서 round-9 의 perf 개선은 사실상 `_nowait` dispatch fix 한 가지에서 왔고 새 public overload 는 perf 가산점 없이 surface 만 늘렸다는 게 확인됐다.

### 2026-05-12 .NET round 11 — 정책값 표시 고정 + RoutingId inline direct cache

- 변경한 정책/문서 파일:
  - `doc/perf/PERF_POLICY.md`: multi 기본 server/client I/O thread는 모두 `4`이며,
    raw socket multi는 size별 payload size를 auto-HWM message unit으로 설정한 뒤
    context auto-HWM을 재계산해야 한다고 명시.
  - `doc/perf/PERF_MULTI_TEST_POLICY.md`: 위 규칙을 multi 전용 정책에 같은 의미로
    고정. 이 값은 payload 최대 크기 제한이 아니라 HWM 예산 계획 단위임을 설명.
  - `bindings/dotnet/perf/README.md`: .NET multi의 `PERF_IO_THREADS=4`,
    size별 msg unit, auto-HWM 재계산 규칙 추가.
- 변경한 runner 파일:
  - `bindings/dotnet/perf/multi/run_benchmarks.sh`: Effective Options에
    `server_io_threads`, `client_io_threads`, `hwm`, `send_hwm`, `recv_hwm`를
    명확히 출력하도록 정리.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/RoutingId.cs`: 16B 이하 routing id에 thread-local
    inline direct cache를 추가. cache hit 시 dictionary/list 탐색 전에
    size/lo/hi/hash로 빠르게 `RoutingId`를 찾는다.
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiRouterRouterServer.cs`
    및 `PerfMultiDealerRouterServer.cs`: hot path에서 `RoutingId` nullable property를
    한 번만 읽도록 정리. public API 우회 없음.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface'`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER,DEALER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 3 --runs 1 --results-tag dotnet_multi_final_after_effective_options`
- 결과:
  - raw socket/message subset 48개 통과.
  - latest report: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260512_073918_dotnet_multi_final_after_effective_options.txt`
  - Effective Options에 multi `server_io_threads=4`, `client_io_threads=4`,
    `hwm=auto-hwm`, `send_hwm=auto-hwm`, `recv_hwm=auto-hwm` 출력 확인.
  - latest throughput: RR 64 `218.829`, 256 `219.901`, 1024 `213.948`
    Kops/s. DR 64 `263.200`, 256 `258.584`, 1024 `258.030` Kops/s.
- 목표 미달:
  - C 기준 `perf_c_multi_linux_20260512_071252_c_multi_current_dotnet_java_check.txt`
    대비 RR 256B ratio `0.782`로 .NET 256B 목표 `0.80`에 소폭 미달.
  - DR 64/256/1024B ratio는 `0.586/0.573/0.596`으로 .NET 목표
    `0.75/0.80/0.82`에 미달.
- 다음 판단:
  - 측정 의미는 C와 맞다. multi thread 수와 auto-HWM size 단위가 문서, runner
    출력, 실행 값에서 일치한다.
  - 남은 큰 손실은 `MULTI_DEALER_ROUTER`이다. 다음 작업은 public surface를 늘리지
    않고 `RoutingId` 캐시 hit 경로, `Message` wrapper 수명, nowait P/Invoke 전환
    수를 더 줄일 수 있는지 확인한다.

### 2026-05-12 .NET round 12 — non-routed DONT_WAIT recv nowait P/Invoke

- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Native/NativeMethods.Socket.cs`:
    `zlink_recv_part_nowait` P/Invoke 선언 추가. 실제 entrypoint는
    `zlink_recv_part`이며, `DONT_WAIT` 호출에 한해 `[SuppressGCTransition]`을 적용한다.
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`:
    `ReceiveBasicParts`가 `DontWaitFlag` 설정 시 nowait P/Invoke를 사용하도록 변경.
- 변경한 perf 파일:
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiRouterRouterServer.cs`
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiDealerRouterServer.cs`
  - reusable `Received` server buffer를 `using var`로 묶어 stop token으로 빠져나갈 때도
    마지막 수신 메시지가 정리되게 했다.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release --nologo`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release --nologo`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface' --nologo`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --reuse-build --pattern ROUTER_ROUTER,DEALER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 3 --runs 1 --results-tag dotnet_multi_nowait_recv_final`
- 결과:
  - raw socket/message subset 48개 통과.
  - latest report: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260512_075327_dotnet_multi_nowait_recv_final.txt`
  - RR 64/256/1024B: `216.541/218.182/218.406` Kops/s.
  - DR 64/256/1024B: `266.710/262.427/259.028` Kops/s.
  - DR 단독 측정(`dotnet_multi_dealer_recv_nowait`)에서는 DR 64/256/1024B가
    `268.494/269.066/263.068` Kops/s로 round 11 대비 256B 중심 개선을 확인했다.
- 목표 미달:
  - C 기준 대비 latest full run RR 256B ratio `0.776`으로 .NET 256B 목표
    `0.80`에 소폭 미달.
  - DR 64/256/1024B ratio `0.594/0.582/0.598`로 .NET 목표
    `0.75/0.80/0.82`에 미달.
- 다음 판단:
  - non-routed recv P/Invoke safepoint 비용은 줄였지만, 남은 큰 gap은 routed echo
    server의 `RoutingId` materialization과 routed send native call 쪽이다.

### 2026-05-12 .NET round 13 — borrowed send nowait + borrowed hint 축소

- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Native/NativeMethods.Socket.cs`:
    `zlink_send_part`와 `zlink_send_part_rid`의 `DONT_WAIT` 전용 P/Invoke 선언에
    `[SuppressGCTransition]`을 적용했다.
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`:
    borrowed single-part send 경로가 `DONT_WAIT`일 때 nowait P/Invoke를 사용하도록
    했다. routed/non-routed 모두 같은 방식이다.
  - `bindings/dotnet/src/Zlink/Message.cs`:
    borrowed send submit 성공 뒤에는 native free callback이 pinned byte[] handle만
    해제하도록 하여 Message wrapper를 in-flight 동안 붙잡지 않게 했다. public
    `Message` 객체 풀링은 Dispose 뒤 참조가 다시 살아나는 위험이 있어 적용하지
    않았다.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release --nologo`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release --nologo`
  - `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.0 dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -c Release --no-build --filter 'FullyQualifiedName~test_pair_tcp|FullyQualifiedName~test_router_multiple_dealers|FullyQualifiedName~test_pubsub|FullyQualifiedName~test_message|FullyQualifiedName~test_socket_surface' --nologo`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --reuse-build --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 3 --runs 3 --results-tag dotnet_multi_current_no_wrap_pool_3run`
- 결과:
  - `Zlink` 및 multi perf project build 통과. 전체 solution build는 현재 Spot sample/test
    API 불일치가 남아 실패하므로 이번 perf 변경 검증에서는 제외했다.
  - raw socket/message subset 48개 통과.
  - latest report: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260512_082822_dotnet_multi_current_no_wrap_pool_3run.txt`
  - latest aggregated DR 64/256/1024B: `267.089/263.614/262.203` Kops/s.
  - latest aggregated RR 64/256/1024B: `220.157/214.300/212.202` Kops/s.
- 목표 미달:
  - C 기준 `perf_c_multi_linux_20260512_071252_c_multi_current_dotnet_java_check.txt`
    대비 DR 64/256/1024B ratio `0.594/0.584/0.606`로 .NET 목표
    `0.75/0.80/0.82`에 미달.
  - RR 256B ratio `0.762`로 .NET 256B 목표 `0.80`에 미달.
- 다음 판단:
  - borrowed send의 GC transition과 Message wrapper lifetime 비용은 줄였지만,
    throughput 개선은 측정 변동폭 안에 있다.
  - DR의 큰 gap은 여전히 public routed send/recv surface에서 `RoutingId`와
    `Message` ownership을 안전하게 유지하기 위해 필요한 관리 비용, 그리고 native
    poll/send/recv 왕복 비용이 합쳐진 영역이다. perf 전용 public overload는 round 9
    revert에서 이미 금지 사례로 확인했으므로 다시 추가하지 않는다.

### 2026-05-12 .NET round 14 — Received.Send routed echo surface

- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Received.cs`: `Received.Send(...)` public API 추가.
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`: ROUTER 수신 결과에
    원래 송신 경로로 보내는 send context를 주입. SPOT source rid가 있는 경우
    `zlink_router_send_spot_part` 경로를 사용한다.
  - `bindings/dotnet/src/Zlink/Service/Spot.cs`: SPOT routed 수신 결과에
    `SendToSpot` 기반 send context를 주입.
- 변경한 sample/perf 파일:
  - `bindings/dotnet/samples/DealerRouterRecv/Program.cs`
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiDealerRouterServer.cs`
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiRouterRouterServer.cs`
- 의미:
  - routed echo에서 `RoutingId`를 사용자가 다시 꺼내 조합하는 대신, `Received` 내부의
    receive context가 알고 있는 원래 송신 경로로 보낸다. 이는 perf 전용 API가 아니라
    사용자 코드에서 필요한 의미 API다.
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release --nologo`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release --nologo`
  - `dotnet build bindings/dotnet/samples/DealerRouterRecv/DealerRouterRecv.csproj -c Release --nologo`
  - `bindings/dotnet/perf/multi/run_benchmarks.sh --reuse-build --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 3 --runs 1 --results-tag dotnet_multi_received_send_cached_rid_confirm`
- 결과:
  - latest report: `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260512_113041_dotnet_multi_received_send_cached_rid_confirm.txt`
  - DR 64/256/1024B: `259.362/262.219/257.595` Kops/s.
  - RR 64/256/1024B: `198.943/201.816/200.742` Kops/s.
  - `Received.Send(Message)` 초안은 단일 message에서도 배열과 새 routed send 경로를 타서
    회귀했다. 이후 단일 part 전용 send context를 추가해 배열 할당을 제거했고, routing id는
    기존 `RoutingId` native cache 경로를 사용하도록 조정했다.
- 목표 미달:
  - C 기준 `perf_c_multi_linux_20260512_071252_c_multi_current_dotnet_java_check.txt`
    대비 DR 64/256/1024B ratio `0.577/0.581/0.595`로 .NET 목표
    `0.75/0.80/0.82`에 미달.
  - RR 64/256B ratio `0.717/0.718`로 .NET 목표 `0.75/0.80`에 미달.
- 다음 판단:
  - API 의미 반영은 완료됐다. 남은 성능 작업은 `Received.Send` delegate 경유 비용,
    `Message` wrapper 수명, nonblocking routed send 결과 변환 비용을 더 줄이는 쪽으로
    이어 간다.

### 2026-05-12 .NET round 15 — Received send context 직접화와 ready-only recv

- 동일 조합 C 결과:
  - `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260512_121228_c_multi_echo_recheck_20260512.txt`
- 대상 언어 결과:
  - `bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260512_122828_dotnet_multi_echo_poller_tag_20260512.txt`
- 선택한 병목 가설:
  - ref-out routed recv hot path가 `Received.Send(...)`용 delegate를 매 수신마다 만들고,
    echo client가 POLLIN ready가 아닌 waiting slot에도 `Recv(DontWait)`를 호출해
    C 기준보다 불필요한 EAGAIN 호출을 만든다.
- 변경한 라이브러리 파일:
  - `bindings/dotnet/src/Zlink/Received.cs`
  - `bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs`
- 변경한 perf 파일:
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiDealerRouterClient.cs`
  - `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiRouterRouterClient.cs`
  - `bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfPollManager.cs`
- 실행한 검증 명령:
  - `dotnet build bindings/dotnet/src/Zlink/Zlink.csproj -c Release --nologo`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release --nologo`
  - `bindings/dotnet/perf/run_benchmarks_multi.sh --pattern MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER --transports tcp --msg-sizes 64,256,1024 --duration 5 --runs 1 --results-tag dotnet_multi_echo_poller_tag_20260512`
- 결과:
  - build 통과. `Compatibility/PerfCompat.cs`의 nullable warning은 기존 warning이다.
  - DR 64/256/1024B: `272.008/274.240/269.636` Kops/s.
  - RR 64/256/1024B: `215.295/214.040/208.208` Kops/s.
  - round 14 대비 DR은 약 `+12.6/+12.0/+12.0` Kops/s, RR은 약
    `+16.4/+12.2/+7.5` Kops/s 개선됐다.
- 목표 미달 조합:
  - `MULTI_ROUTER_ROUTER` 64/256/1024B ratio `0.508/0.508/0.505`.
  - `MULTI_DEALER_ROUTER` 64/256/1024B ratio `0.603/0.618/0.610`.
- 다음 판단:
  - `Received.Send` delegate 비용과 POLLIN 없는 recv 시도는 줄였지만, .NET은 새 C 기준에서
    여전히 목표와 큰 차이가 있다.
  - timing 기준 client loop가 100% CPU를 사용하며, 다음 후보는 public `Poller.WaitAll`
    결과 처리와 `Message.WrapBytes` send 수명 비용이다.
