# `bindings/dotnet/perf` POSD 후속 리팩토링 계획

> 후속 검토 결론: follow-up 리팩토링은 가치가 있다. `PerfCommonMulti.cs` 쪽의 pattern preset 재조회와 `PollManager` 의 broad polling 책임은 유지보수성을 개선할 여지가 분명하다.
> 전제: perf 목적 범위 내, PERF 정책 준수, `core/perf` 와 동일한 측정 의미 유지.
> 대상: `bindings/dotnet/perf/`

## TODO

- [ ] `PerfCommonMulti.cs` preset 재조회 지점 목록화
- [ ] `PerfOptions` preset snapshot 전달 방식 설계
- [ ] `PollManager` 책임 분리안 정리
- [ ] preset 재조회 제거 후 call site 변경 범위 점검
- [ ] build 검증 완료
- [ ] single/multi smoke 검증 완료
- [ ] grep 검증 완료
- [ ] 완료 정의 충족 여부 최종 리뷰

## 1. 목표

- `PerfCommonMulti.cs` 에 흩어진 `PerfOptions.FromMultiPattern("PUBSUB")` / `("STREAM")` 재조회 경계를 한 번만 계산하는 형태로 줄인다.
- `PollManager` 의 monitor/socket polling 책임을 더 좁혀, ready gate 와 event pump 의 정책 차이를 드러낸다.
- 변경이 생기더라도 single/multi 측정 의미와 RESULT 포맷은 그대로 유지한다.

## 2. 현재 구조 요약

- 환경 변수 해석은 [PerfEnv.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfEnv.cs) 에 모았다.
- 옵션과 검증은 [PerfOptions.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfOptions.cs), 공통 결과와 계산은 [PerfShared.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfShared.cs) 에 둔다.
- [PerfOptions.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/common/Zlink.BindingBench.Common/PerfOptions.cs) 는 272라인이고, [PerfCommonMulti.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/common/PerfCommonMulti.cs) 는 341라인이다.
- 다만 [PerfCommonMulti.cs](/home/hep7/project/kairos/zlink/bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/common/PerfCommonMulti.cs) 에서는 `PerfOptions.FromMultiPattern("PUBSUB")` 와 `PerfOptions.FromMultiPattern("STREAM")` 재조회가 반복되고, [PollManager.cs](../../../../bindings/dotnet/perf/common/Zlink.BindingBench.Common/PollManager.cs) 는 monitor/socket polling 을 한 클래스로 넓게 감싼다.
- [PollManager.cs](../../../../bindings/dotnet/perf/common/Zlink.BindingBench.Common/PollManager.cs) 는 123라인인데도 `PollMonitors`, `PollSockets`, `IsSocketReadReady`, `IsSocketWriteReady` 를 한 ownership 안에 묶는다.

## 3. 남은 POSD 문제

- `PerfCommonMulti.cs` 의 preset 조회가 pattern 별로 중복되어, PUBSUB/STREAM 기본값과 multi 옵션 해석이 넓게 퍼졌다.
- `PollManager` 는 `PollMonitors`, `PollSockets`, `IsSocketReadReady`, `IsSocketWriteReady` 를 모두 품어 ready gate, socket polling, 상태 확인을 한 타입에 묶는다.
- `PerfOptions.cs` 는 env 해석을 한곳에 모았지만, 그 위에 얹힌 preset 재조회와 polling 책임은 아직 더 깊게 분리할 여지가 있다.

## 4. 우선순위

- P1: `PerfCommonMulti.cs` 에서 PUBSUB/STREAM preset 재조회 지점을 제거하고, pattern preset 을 한 번만 해석하는 helper 로 묶는다.
- P2: `PollManager` 를 monitor polling 과 socket polling 정책으로 나눠 wait strategy 와 state query 를 분리한다.
- P3: `PerfOptions` 와 `PerfShared` 는 현재 계약을 유지하되, entrypoint 가 preset snapshot 을 한 번만 넘기도록 만들어 call site 의 변경 증폭을 줄인다.

## 5. 단계별 작업

1. preset 해석 정리
- `PerfCommonMulti.cs` 에서 PUBSUB/STREAM 별 preset 재조회 지점을 나열하고 공통 default resolver 로 모은다.

2. polling 경계 축소
- `PollManager` 안의 monitor wait 와 socket wait 를 분리하고, 각 경로의 timeout/ready 의미를 이름으로 드러낸다.

3. runner 계약 유지
- single/multi 측정 의미, RESULT 포맷, policy 제약은 그대로 유지한다.

## 6. 검증 방법

- `dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release`
- `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release`
- `cd bindings/dotnet/perf && ./run_benchmarks.sh --pattern SPOT --msg-sizes 64 --warmup 1 --duration 1`
- `cd bindings/dotnet/perf && ./run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --msg-sizes 64 --clients 4 --warmup 1 --duration 1`
- `rg -n "FromMultiPattern\\(\"PUBSUB\"\\)|FromMultiPattern\\(\"STREAM\"\\)|new PollManager|PollManager" bindings/dotnet/perf -g '*.cs'`

## 7. 완료 정의

- `PerfCommonMulti.cs` 의 pattern preset 재조회가 한 번만 일어나도록 정리된다.
- `PollManager` 의 책임이 monitor ready / socket polling / state query 로 더 명확히 갈린다.
- `core/perf` 와 동일한 측정 의미와 RESULT 포맷이 유지된다.

## 8. 비범위

- 새로운 공통 프레임워크를 추가로 만드는 일
- hot path를 감추는 오케스트레이터 도입
- `core/perf` 와 다른 측정 의미 도입
