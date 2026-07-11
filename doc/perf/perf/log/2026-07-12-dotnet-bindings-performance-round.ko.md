# .NET bindings 성능 개선 라운드

## 실행 조건

- runtime: `core/build/lib/libzlink.so.9.0.0`
- 시작 HEAD: `61cc30d61`
- CPU pin: 사용하지 않음
- 실행 단위: 한 번에 한 perf process
- 순서: 현재 transport의 C 측정 직후 .NET을 측정하고 비교한다.
- 판정: throughput과 평균 latency만 gate에 사용하고 p95와 p99는 진단 자료로만 사용한다.

다른 .NET build와 test가 CPU를 사용하면 해당 프로세스가 끝날 때까지 기다렸다. 각 공식
측정 직전에는 `top`을 반복 실행해 CPU 상태를 확인했다.

## Single PAIR

### tcp 최초 측정

C와 .NET을 CPU pin 없이 각각 5회 측정했다.

- C: `perf_c_single_linux_20260712_071829_core_9_0_dotnet_pair_tcp_nopin_paired_20260712.txt`
- .NET: `perf_dotnet_single_linux_20260712_072101_core_9_0_dotnet_pair_tcp_nopin_paired_20260712.txt`

최초 throughput 비율은 99.1%, 78.6%, 117.9%, 99.7%, 99.9%, 99.9%로 모두
.NET 단순 one-way 최소 목표 70%를 넘었다. 그러나 64B 평균 latency는 C 0.147ms와
.NET 1.032ms로 7.02배였으며 최대 허용치 3.0배를 넘었다.

외부 `dotnet test`가 끝난 뒤 CPU idle 상태에서 64B만 다시 측정했다.

- C: `perf_c_single_linux_20260712_072404_core_9_0_dotnet_pair_tcp64_nopin_recheck_20260712.txt`
- .NET: `perf_dotnet_single_linux_20260712_072525_core_9_0_dotnet_pair_tcp64_nopin_recheck_20260712.txt`

재측정도 C 0.159ms와 .NET 0.894ms로 평균 latency가 5.62배였다. 시스템 부하가
아니라 .NET 경로의 반복 가능한 문제로 판정했다.

### POSD 검토와 병목 진단

확인한 위험 신호는 다음과 같다.

- public send 한 건마다 `Message`와 one-shot send builder가 만들어진다. 호출자가
  소유권 규칙을 지키지 않으면 관리 힙 회수 비용이 hot path에 누적된다.
- `Received`는 caller-provided storage를 재사용하지만 내부 single-part wrapper와
  native handle 교체 비용이 남아 있다.
- perf helper가 성공 submit에서 소비된 `Message` wrapper를 dispose하지 않았다.
  공개 계약은 소비된 wrapper도 dispose해야 pool에 반환된다고 명시한다.

두 가지 방향을 비교했다.

1. 송신 경로에서 public 소유권 계약을 지키고 pool-backed `Message`를 재사용한다.
   payload 모양과 builder API를 바꾸지 않으면서 관리 힙 비용을 줄일 수 있다.
2. 수신 `Received`의 single-part 저장소를 더 직접 재사용한다. 공개 API는 유지할 수
   있지만 송신 측 GC가 주원인이면 효과가 작고, nonblocking 실패 시 기존 결과를
   보존하는 계약을 더 복잡하게 만든다.

`dotnet-counters` 진단에서 변경 전 64B 실행의 managed allocation은 5개 표본 평균
약 351MB/s였고 Gen0 GC 6회와 GC pause 합계 19.772ms가 기록됐다. sampling trace의
상위 경로는 `PerfSocketIo.Send`, `SinglePartSubmit.Submit`,
`SocketKernel.ReceiveBasicParts`였다. 원인과 책임 경계가 직접 맞는 1번을 선택했다.

### 채택한 변경

`PerfSocketIo`의 span 기반 send와 publish가 `Message.Allocate()`로 pool-backed wrapper를
얻고 payload를 복사한 뒤, submit 성공·backpressure·예외와 관계없이 `finally`에서
dispose하도록 바꿨다. 새 public API나 private 우회 경로를 추가하지 않았고 payload와
측정 의미도 바꾸지 않았다. 성공 submit 뒤 wrapper dispose를 요구하는 기존 public
계약을 benchmark가 그대로 따르도록 고친 것이다.

변경 후 진단에서 managed allocation 5개 표본 평균은 약 193MB/s로 45.0% 감소했고,
Gen0 GC는 4회, GC pause 합계는 13.181ms로 감소했다.

### 최종 측정

64B 제한 측정에서 .NET 평균 latency는 0.239ms로 개선 전 0.894ms보다 73.3%
낮아졌고 C 0.159ms의 1.50배로 통과했다.

- .NET 64B 후보: `perf_dotnet_single_linux_20260712_073131_core_9_0_dotnet_pair_tcp64_nopin_after_message_dispose_20260712.txt`

이후 tcp 여섯 size 전체를 .NET 5회로 다시 측정했다. 4회차 1024B와 대형 셀에 외부
부하성 동반 하락이 있었지만 나머지 네 회차와 5회 중앙값은 안정적이었다.

- C 기준: `perf_c_single_linux_20260712_071829_core_9_0_dotnet_pair_tcp_nopin_paired_20260712.txt`
- .NET 최종: `perf_dotnet_single_linux_20260712_073333_core_9_0_dotnet_pair_tcp_nopin_final_after_message_dispose_20260712.txt`

최종 throughput 비율은 98.3%, 86.5%, 99.0%, 99.9%, 100.0%, 99.9%였고 평균
latency 최대 비율은 1.19배였다. 모든 셀이 목표를 만족했다.

- `PAIR / tcp`: 완료
- public API 변경: 없음
- binding runtime 변경: 없음
- perf 변경: 성공 submit 뒤 Message wrapper dispose와 pool-backed 생성 적용
- 다음 transport: ws
