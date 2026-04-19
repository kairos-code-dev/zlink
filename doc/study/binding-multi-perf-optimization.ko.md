# binding multi perf 최적화 기록

이 문서는 `netzlink`와 `jvmzlink`의 multi perf 64B 결과를 core perf와
비교하면서, 실제로 효과가 있었던 바인딩 라이브러리 최적화를 정리한
작업 기록이다.

목표는 perf 전용 우회가 아니라, 공개 바인딩 API를 유지한 상태에서
일반적인 nonblocking send/recv 경로를 가볍게 만드는 것이다.

## 기준 수치

core 기준 수치는 아래 파일을 사용했다.

- [RR 64 core](/home/hep7/project/kairos/zlink/core/perf/results/multi/report/perf_core_multi_linux_20260418_232652.txt)
- [DR 64 core](/home/hep7/project/kairos/zlink/core/perf/results/multi/report/perf_core_multi_linux_20260418_232827.txt)

핵심 throughput:

- `MULTI_ROUTER_ROUTER 64B`: `371.62 Kops/s`
- `MULTI_DEALER_ROUTER 64B`: `350.09 Kops/s`

## .NET 쪽에서 실제로 효과가 있었던 변경

### 1. `Recv(RecvFlags.DontWait)` 내부 no-throw fast path

가장 먼저 효과가 있었던 건 no-data를 예외로 만들기 전에,
native recv 자체는 no-throw로 처리하는 경로를 넣은 것이다.

변경 대상:

- [SocketKernel.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/src/Zlink/Sockets/Internal/SocketKernel.cs)

핵심은 아래 두 helper다.

- `TryReceiveMessageCore(int flags)`
- `TryReceiveRoutedCore(int flags)`

이 helper는 `EAGAIN`일 때 `null`을 돌려주고,
실제 public surface에서만 `ZlinkRecvException`을 만든다.

이 변경만으로 `netzlink` multi 수치는 대략 `60 Kops/s`대에서
`100 Kops/s`대로 올라갔다.

### 2. documented public no-wait API 승격

그 다음 실제로 큰 폭으로 올라간 건, perf가 예외 기반 `Recv(DontWait)`와
`Send(..., DontWait)`를 반복 호출하지 않도록, 이미 코드에 있던 no-wait
결과형 API를 공개 표면으로 승격한 것이다.

변경 대상:

- [MessageSocketBase.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/src/Zlink/Sockets/MessageSocketBase.cs)
- [RoutedMessageSocketBase.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/src/Zlink/Sockets/RoutedMessageSocketBase.cs)
- [dotnet binding spec](/home/hep7/project/kairos/zlink/doc/spec/bindings/dotnet/README.md)

추가로 public으로 취급한 메서드:

- `RecvNoWait()`
- `SendNoWaitResult(...)`

그리고 perf는 이 공개 API만 쓰도록 바꿨다.

변경 대상:

- [PerfRouterRouterServer.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfRouterRouterServer.cs)
- [PerfRouterRouterClient.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfRouterRouterClient.cs)
- [PerfDealerRouterServer.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfDealerRouterServer.cs)
- [PerfDealerRouterClient.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfDealerRouterClient.cs)

이 변경 후 최신 수치는 아래와 같다.

- [netzlink RR 64 latest](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260419_025116.txt)
- [netzlink DR 64 latest](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260419_025116.txt)

핵심 throughput:

- `MULTI_ROUTER_ROUTER 64B`: `220.68 Kops/s`
- `MULTI_DEALER_ROUTER 64B`: `278.93 Kops/s`

core 대비 비율:

- `RR`: 약 `59.4%`
- `DR`: 약 `79.7%`

즉 `.NET`에서 제일 큰 병목은 native bridge 자체보다,
public surface가 no-data와 backpressure를 예외로 다루는 방식이었다.

## Java 쪽에서 실제로 효과가 있었던 변경

### 1. recv single-part fast path

Java는 `Received`와 `Message[]`를 항상 만드는 generic recv 경로가
무거웠다. 그래서 single-part일 때는 `Message` 하나로 바로
materialize하는 fast path를 넣었다.

관련 파일:

- [MessagePlane.java](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/MessagePlane.java)
- [Received.java](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/Received.java)
- [Message.java](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/Message.java)

### 2. `recvNoWait()` public API 사용

Java는 이미 `DealerSocket`, `RouterSocket`, `PairSocket`, `StreamSocket`에
`recvNoWait()`가 있었는데, spec에 안 적혀 있어서 perf가
`recv(DONT_WAIT)` 예외 경로를 타고 있었다.

이건 정책과 성능 둘 다 안 좋은 상태였다.

그래서 아래를 같이 맞췄다.

- [DealerSocket.java](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/DealerSocket.java)
- [RouterSocket.java](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/RouterSocket.java)
- [PairSocket.java](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/PairSocket.java)
- [StreamSocket.java](/home/hep7/project/kairos/zlink/bindings/java/src/main/java/dev/kairoscode/zlink/StreamSocket.java)
- [java binding spec](/home/hep7/project/kairos/zlink/doc/spec/bindings/java/README.md)

그리고 perf는 documented public API인 `recvNoWait()`를 쓰도록 바꿨다.

관련 파일:

- [PerfMultiRouterRouter.java](/home/hep7/project/kairos/zlink/bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/PerfMultiRouterRouter.java)
- [PerfMultiDealerRouter.java](/home/hep7/project/kairos/zlink/bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/PerfMultiDealerRouter.java)

### 3. `sendNoWaitResult()` 공개도 시도했지만 perf 기준 이득은 없었다

`sendNoWaitResult()`도 public으로 올려서 perf에 적용해 봤지만,
이번 `router-router`와 `dealer-router` 64B 패턴에서는 오히려 수치가
내려갔다.

그래서 perf는 다시 `send(..., SendFlags.DONT_WAIT)`로 유지했다.
즉 이 변경은 API 정합성에는 의미가 있어도, 이번 benchmark 기준으로는
핫패스 개선으로 이어지지 않았다.

최신 Java 수치:

- [jvmzlink RR 64 latest](/home/hep7/project/kairos/zlink/bindings/java/perf/results/multi/report/perf_java_multi_linux_20260419_025217_rrpublic6b.txt)
- [jvmzlink DR 64 latest](/home/hep7/project/kairos/zlink/bindings/java/perf/results/multi/report/perf_java_multi_linux_20260419_025207_drpublic6.txt)

핵심 throughput:

- `MULTI_ROUTER_ROUTER 64B`: `151.16 Kops/s`
- `MULTI_DEALER_ROUTER 64B`: `148.24 Kops/s`

core 대비 비율:

- `RR`: 약 `40.7%`
- `DR`: 약 `42.3%`

## 현재 판단

정리하면 지금까지 실제로 효과가 있었던 것은 아래 두 가지다.

1. `.NET`
   - no-data/backpressure 예외를 public 바깥쪽으로 밀어낸 no-throw 내부 경로
   - documented public `RecvNoWait()` / `SendNoWaitResult(...)` 사용
2. `Java`
   - single-part recv fast path
   - documented public `recvNoWait()` 사용

반대로 아래는 이번 패턴에서 큰 효과가 없었다.

- Java `sendNoWaitResult()` 기반 perf loop
- 작은 `Message` 메모리 풀링
- FFM 메모리 복사 자체를 줄이는 미세 최적화

즉 남은 큰 차이는 이제 단순 recv/send 예외 경로보다는,

- `.NET`: routed `Received` / `Message` materialization
- `Java`: `Message` 객체화와 generic send/recv 경계

쪽에 더 많이 남아 있다.

## 다시 확인한 점

이후에 global binding spec을 다시 기준으로 맞췄다.

- `doc/spec/bindings/README.md`는 exception 언어에서
  `sendNoWait`, `recvNoWait` 같은 별도 이름을 공개 surface에 두지
  않는다고 적고 있다.
- 그래서 perf도 다시 `send(..., DontWait)` / `recv(DontWait)`와
  public `Poller` 조합만 쓰는 방향으로 되돌려서 측정했다.

이 상태의 최신 수치는 아래와 같다.

- [netzlink strict public RR/DR 64](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260419_063026.txt)
- [jvmzlink strict public RR/DR 64](/home/hep7/project/kairos/zlink/bindings/java/perf/results/multi/report/perf_java_multi_linux_20260419_063226.txt)

핵심 throughput:

- `.NET`
  - `MULTI_ROUTER_ROUTER 64B`: `109.55 Kops/s`
  - `MULTI_DEALER_ROUTER 64B`: `123.77 Kops/s`
- `Java`
  - `MULTI_ROUTER_ROUTER 64B`: `117.50 Kops/s`
  - `MULTI_DEALER_ROUTER 64B`: `143.28 Kops/s`

즉 이전에 잘 나오던 수치의 큰 부분은 별도 no-wait helper 이름에
기대고 있었고, spec을 엄격히 맞추면 다시 내려간다.

이건 곧, 지금 남은 핵심 병목이 단순 native bridge가 아니라
`EAGAIN` / backpressure를 public surface에서 예외로 처리하는 계약과
그 위의 `Message` objectization 비용이라는 뜻이다.

## 최근에 실제로 유지할 가치가 있었던 변경

### .NET `Message` wrapper 재사용

`.NET`에서는 `Message.FromBytes(...)`, `Message.MoveFromNativeSingle(...)`,
`Message.FromNativeVector(...)`에서 wrapper 객체를 계속 새로 만들고
있었다.

그래서 [Message.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/src/Zlink/Message.cs)
에 작은 wrapper pool을 넣었다.

이건 public contract를 바꾸지 않으면서 managed allocation을 줄이는
방향이다.

strict public perf 기준으로도 이 변경은 의미가 있었다.

- [netzlink RR after message wrapper pooling](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260419_062920.txt)
- [netzlink DR after message wrapper pooling](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/results/multi/report/perf_dotnet_multi_linux_20260419_062933.txt)

핵심 throughput:

- `RR 64`: `257.83 Kops/s`
- `DR 64`: `279.35 Kops/s`

다만 이 수치는 perf가 다시 helper 이름을 타던 시점의 측정이다.
즉 wrapper pooling 자체는 실제 효과가 있었지만, 최종적인 정책 준수
측정에서는 아직 exception 경계 비용이 더 크게 남아 있다.
