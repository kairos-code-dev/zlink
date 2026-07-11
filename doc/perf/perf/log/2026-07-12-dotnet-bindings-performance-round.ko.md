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

### PAIR active send 정책 정합화

`PERF_SINGLE_TEST_POLICY.md`는 raw one-way sender가 blocking send를 연속 수행하고 HWM에서
자연 backpressure를 받도록 정한다. 그러나 C `perf_pair.cpp`와 .NET `PerfPair`는 모두
`DONTWAIT`를 사용하고 있었다. C와 binding 사이만 같고 확정 정책의 측정 의미와는 달랐으므로,
현재 pattern인 PAIR의 두 perf를 함께 blocking send로 수정했다. 다른 pattern의 perf는
현재 작업 단위가 아니므로 미리 바꾸지 않았다.

이 정합화로 기존 tcp 완료 report는 같은 측정 의미의 근거가 아니게 됐다. tcp의 모든
transport size를 blocking 의미로 다시 paired 측정한 뒤 완료 상태를 복구한다.

### ws 256B blocking paired 측정

CPU idle 상태에서 C와 .NET을 CPU pin 없이 각각 5회 측정했다.

- C: `perf_c_single_linux_20260712_080902_core_9_0_dotnet_pair_ws256_blocking_policy_paired_20260712.txt`
- .NET before: `perf_dotnet_single_linux_20260712_080939_core_9_0_dotnet_pair_ws256_blocking_policy_paired_20260712.txt`

C 중앙값은 1,648,611.8 msg/s와 평균 latency 41.411ms였다. .NET before는
1,015,541.6 msg/s와 67.322ms로, 처리량 비율 61.60%는 최소 목표 70%에 미달했고
평균 latency 비율 1.63배는 3.0배 상한을 통과했다.

### blocking 병목 POSD 검토

sampling trace에서 `PerfSocketIo.Send` 48.21%, `SinglePartSubmit.Submit` 23.69%,
`Message.MoveTo` 10.10%, `SocketKernel.ReceiveBasicParts` 39.90%,
`Received.ResetForReuse` 4.99%가 관측됐다. `dotnet-counters`에서는 초당 약 67MB의
지속 할당이 있었지만 5초 동안 GC가 발생하지 않아 GC pause는 이번 blocking 처리량
미달의 원인이 아니었다.

검토한 위험 신호와 대안은 다음과 같다.

1. one-shot builder를 풀링하면 과거 builder 참조와 다음 operation이 결합된다. 실제로
   builder 할당을 제거한 진단 후보도 처리량이 약 2%만 올라 목표에 도달하지 못해 제거했다.
2. opaque `ZlinkMsg`를 C#에서 직접 복사하면 core message layout 지식이 binding으로
   누출된다. 기존 `zlink_msg_adopt`를 사용한 후보는 수치 변화가 없어 제거했다.
3. assembly 전체 local 초기화를 생략하면 unrelated interop local까지 안전 가정이 퍼진다.
   바로 다음 native init이 64바이트 전체를 초기화하는 송신·basic 수신 local 두 곳만
   `Unsafe.SkipInit`으로 좁히는 방안을 선택했다.

수신 wrapper 직접 교체, compact builder buffer, GC transition 생략, tiered JIT 강제 최적화,
중복 guard 제거, 예외 slow-path 분리 후보도 기능 검증 뒤 제한 측정했지만 처리량 개선이
없거나 latency가 악화돼 모두 제거했다.

### 채택한 `ZlinkMsg` 중복 초기화 제거

`SinglePartSubmit`과 `SocketKernel.ReceiveBasicParts`는 stack의 64바이트 `ZlinkMsg`를
0으로 채운 직후 `zlink_msg_init`으로 다시 전부 초기화했다. 두 local에만
`Unsafe.SkipInit`을 적용하고, native init 성공 전에는 값을 읽거나 닫지 않는 기존 순서를
유지했다. 확정 hot path와 안전 조건은 코드 주석으로 남겼다.

공식 5회 후보 report는 다음과 같다.

- .NET after: `perf_dotnet_single_linux_20260712_082632_core_9_0_dotnet_pair_ws256_blocking_skipinit_candidate_20260712.txt`

처리량 중앙값은 1,050,306.2 msg/s로 before보다 3.42% 높아졌고 평균 latency는
64.065ms로 4.84% 낮아졌다. C 대비 처리량은 63.71%라 아직 미달이며 평균 latency는
1.55배로 통과한다. 이 개선은 유지하되 ws 완료로 기록하지 않는다.

검증 결과:

- .NET single Release build: 통과, warning 0
- .NET multi Release build: 통과, warning 0
- `Zlink.Tests` Release 전체: 177개 통과
- C `perf_pair` build와 C/.NET blocking smoke: 통과

검증된 코드와 perf 정합화는 `f1440eb18` (`perf(dotnet): align pair send and skip native clears`)
커밋으로 분리했고, 측정 근거 커밋 `867aa137b`와 함께 원격 `main`에 푸시했다.

다음 작업은 blocking 의미로 PAIR tcp 전체 size를 C와 .NET 순서로 다시 paired 측정하고,
tcp 완료 상태를 복구한 뒤 ws 256B의 남은 처리량 미달을 계속 개선하는 것이다.

### POSD 개선 단독 채택 기준

성능 수치가 좋아지지 않더라도 기존 위험 신호를 실제로 제거하고 정보 은닉이나 책임 경계를
분명하게 개선하며, 처리량·평균 latency·기능 회귀가 없으면 채택할 수 있도록 판정 기준을
보완했다. 다만 POSD 개선만으로 목표 미달 셀을 통과로 바꾸지는 않는다. 성능과 POSD 어느
쪽에서도 분명한 이득이 없거나 성능 회귀가 생긴 후보는 계속 제거한다.

### PAIR tcp blocking paired 재측정

CPU 고부하 프로세스가 없을 때 C와 .NET을 CPU pin 없이 차례로 5회 측정했다.

- C: `perf_c_single_linux_20260712_083121_core_9_0_dotnet_pair_tcp_blocking_nopin_paired_20260712.txt`
- .NET: `perf_dotnet_single_linux_20260712_083602_core_9_0_dotnet_pair_tcp_blocking_nopin_paired_20260712.txt`

64, 256, 1024, 65536, 131072, 262144B 처리량 비율은 각각 87.5%, 64.4%,
76.5%, 85.9%, 90.6%, 87.0%였다. 평균 latency 비율은 모두 3배 이내였다. 256B만
최소 처리량 목표에 미달했다. 변동 여부를 확인하기 위해 같은 크기만 다시 C 직후 .NET으로
5회 측정했고 C 1,854,838.8msg/s, .NET 1,180,672.2msg/s로 63.65%가 재현됐다.

- C 256B 재확인: `perf_c_single_linux_20260712_084049_core_9_0_dotnet_pair_tcp256_blocking_nopin_recheck_20260712.txt`
- .NET 256B 재확인: `perf_dotnet_single_linux_20260712_084120_core_9_0_dotnet_pair_tcp256_blocking_nopin_recheck_20260712.txt`

### tcp 256B 추가 병목 진단과 기각 후보

sampling trace에서 송신 스레드의 5초 중 대부분은 native blocking 구간에 있었고,
`SinglePartSubmit`과 `Message.MoveTo`가 남은 관리 경계로 확인됐다. 두 방향을 추가로
검증했다.

1. `Message`가 가진 native handle을 임시 handle로 옮기지 않고 직접 submit하는 후보는
   처리량이 1,189,576.0msg/s로 0.75%만 높아졌고 평균 latency가 0.185ms에서
   0.235ms로 27.0% 높아졌다. 성능 회귀가 있어 제거했다.
2. 임시 friend 접근으로 public fluent builder 비용을 완전히 뺀 진단 상한도
   1,209,149.4msg/s로 2.4%만 높아졌고 C 대비 65.2%였다. public API나 builder
   수명 모델을 바꿀 근거가 없으므로 임시 코드를 모두 제거했고 이 결과는 공식 판정에
   사용하지 않는다.

`/usr/bin/time -v`로 같은 5초 실행을 비교하면 C는 user 7.07초, system 2.59초,
최대 RSS 138,336KB였고 .NET은 user 8.43초, system 2.57초, 최대 RSS 489,368KB였다.
system 비용보다 관리 힙 누적과 관리 경계의 user CPU 차이가 크지만 builder 하나만으로는
목표 차이를 설명하지 못한다. tcp 256B는 계속 `미달`로 유지하고 다음 binding 내부 후보를
조사한다.
