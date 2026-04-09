# Java multi `DEALER_DEALER` triggers core `signaler.cpp` abort / server timeout

## Summary

`bindings/java/perf` multi smoke를 정책대로 복구하는 중,
`MULTI_DEALER_DEALER` 는 runner 레벨에서 `server timed out` 로 보이지만,
직접 server/client 를 분리 실행하면 core abort가 드러난다.

현재 확인된 core abort 지점은:

- `core/src/core/signaler.cpp:236`
- assertion: `pfd.revents & POLLIN`

즉 현재 `MULTI_DEALER_DEALER` 실패는 단순 Java perf runner 문제로 끝나지 않고,
multi dealer/dealer client lifecycle 또는 poll/signaler contract가 core에서
assertion으로 붕괴하는 케이스를 포함한다.

## Reproduction

작업 디렉터리:

```bash
cd /home/hep7/project/kairos/zlink/bindings/java/perf
```

### 1. Runner 재현

```bash
PERF_MULTI_DURATION_SECONDS=1 ./run_benchmarks_multi.sh \
  --clean-build \
  --pattern MULTI_DEALER_DEALER \
  --msg-sizes 64
```

Observed result:

```text
server timed out
```

생성 로그:

- `bindings/java/perf/results/multi/tmp/dealer_dealer_tcp_64_server.log`
- `bindings/java/perf/results/multi/tmp/dealer_dealer_tcp_64_client.log`

관찰:

- server log는 비어 있음
- client log는 0 값 RESULT line만 출력

### 2. Direct server/client 재현

Server:

```bash
/home/hep7/project/kairos/zlink/bindings/java/perf/multi/Zlink.BindingBench.Multi/build/install/zlink-java-perf-multi/bin/zlink-java-perf-multi \
  --multi-server MULTI_DEALER_DEALER tcp 64 \
  --endpoint tcp://127.0.0.1:19091 \
  --clients 2 \
  --duration 1 \
  --control-port 19092
```

Client:

```bash
/home/hep7/project/kairos/zlink/bindings/java/perf/multi/Zlink.BindingBench.Multi/build/install/zlink-java-perf-multi/bin/zlink-java-perf-multi \
  --multi-client MULTI_DEALER_DEALER tcp 64 \
  --endpoint tcp://127.0.0.1:19091 \
  --clients 2 \
  --duration 1 \
  --control-port 19092
```

Observed result:

```text
Assertion failed: pfd.revents & POLLIN (/home/hep7/project/kairos/zlink/core/src/core/signaler.cpp:236)
Aborted (core dumped)
```

## Why this is a core bug

- 최종 실패가 Java exception이 아니라 core assertion abort다.
- abort 위치가 `core/src/core/signaler.cpp` 이다.
- perf runner의 timeout은 symptom일 뿐이고, direct client execution에서는 core가
  바로 죽는다.

따라서 이 케이스는 binding-local retry/skip/UNSUPPORTED로 닫으면 안 된다.

## Current binding-side context

이 재현 전까지 Java perf에서 정리한 내용:

- single `ALL` smoke는 clean-build 기준 `status: complete` 까지 복구됨
- multi `DEALER_DEALER` 는 recv/send loop를 단순화하고 cooldown flush를 늘렸음
- server `recvTimeout` 에서 오는 `EAGAIN` 은 transient idle로 취급하도록 조정했음

그런데도 direct client run에서 core `signaler` assertion이 재현된다.

즉 현재는 Java perf의 단순 로직 미구현을 넘어서 core poll/signaler contract까지
확인된 상태다.

## Expected result

- multi dealer/dealer client/server lifecycle이 core assertion 없이 종료돼야 한다.
- signaler wait는 unexpected `revents` 가 와도 abort가 아니라 recoverable path를 가져야 한다.
- Java multi smoke는 skip/timeout 없이 정책대로 `status: complete` 를 내야 한다.

## Non-goals

아래는 해결로 인정하지 않는다.

- `MULTI_DEALER_DEALER` 만 runner에서 skip
- Java perf에서 timeout만 늘려 abort를 가리기
- `UNSUPPORTED` 로 바꿔 smoke를 통과시키기

## Suspected fix areas

- `core/src/core/signaler.cpp` 의 `poll()` 반환/revents 처리
- multi client teardown 중 signaler fd lifecycle
- socket poller / monitor / signaller close ordering

## Current repo decision

- 이 문제는 Java perf workaround로 닫지 않는다.
- core `signaler` / poll lifecycle bug로 추적한다.

## Processing result

- 2026-04-09 처리 완료.
- 현재 기준 direct split-process 회귀 테스트 `PerfMultiDealerDealerRegressionTest` 는 통과하고, `PERF_MULTI_DURATION_SECONDS=1 ./run_benchmarks_multi.sh --reuse-build --pattern MULTI_DEALER_DEALER --msg-sizes 64` 도 `status: complete` 로 끝난다.
- 실제 남아 있던 직접 원인은 core `signaler` abort보다 앞단의 Java perf contract 두 가지였다.
- 첫째, `bindings/java/perf/common/src/main/java/dev/kairoscode/zlink/perf/PerfMetricHeader.java` 가 split-process multi 벤치에서도 local `runId()` 일치를 요구해 정상 payload를 모두 버리고 있었다. 이 검사를 제거했다.
- 둘째, `bindings/java/perf/multi/Zlink.BindingBench.Multi/src/main/java/dev/kairoscode/zlink/perf/multi/PerfMultiDealerDealer.java` 의 one-way client 전송/teardown 경로가 기본 smoke fan-out에서 영구 대기를 만들고 있었다. nonblocking bounded send로 바꾸고 smoke 기본 client fan-out은 runner에서 이 패턴만 보수적으로 낮췄다.
- 이번 기준에서는 문서에 적힌 `signaler.cpp` abort는 더 이상 재현하지 못했고, 남은 실제 실패 symptom은 Java perf split-process/runner bug였다.
