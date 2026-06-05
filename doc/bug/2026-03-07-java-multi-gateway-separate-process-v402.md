# [Java Perf Bug] `MULTI_GATEWAY` separate-process path still fails on `core/v4.0.2`

- Date: 2026-03-07
- Repo: `/home/hep7/project/kairos/zlink`
- Release tag: `core/v4.0.2`
- Scope: `bindings/java/perf`
- Affected area: `MULTI_GATEWAY` multi-process perf startup/state path

## Summary

`core/v4.0.2`에서 `MULTI_GATEWAY`는 더 이상 core-global blocker로 보이지 않습니다.

- `bindings/cpp` `MULTI_GATEWAY tcp/64`는 통과
- Java same-JVM service-instance poller 최소 경로는 통과
- 하지만 Java perf separate-process 경로는 여전히 실패

즉 현 상태는 core/runtime 전반의 문제라기보다
Java multi perf의 separate-process readiness/state 문제로 보는 편이 맞습니다.

## Evidence

### 1. C++ `MULTI_GATEWAY` passes on `v4.0.2`

Command:

```bash
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite multi --pattern MULTI_GATEWAY \
  --transports tcp --msg-sizes 64 --runs 1 --result
```

Observed:

```text
251.50 Kops/s
latency=0.3600 ms
```

### 2. Java service-instance poller minimal paths pass

Added integration tests:

- [TestGatewayServicePollerPortedTest.java](../../bindings/java/src/test/java/dev/kairoscode/zlink/integration/TestGatewayServicePollerPortedTest.java)
  - `testGatewayReceiverRoundTripViaServicePoller`
  - `testGatewayReceiverRoundTripViaServicePollerAcrossContexts`

Both pass with:

```bash
cd bindings/java
./gradlew integrationTest \
  --tests dev.kairoscode.zlink.integration.TestGatewayServicePollerPortedTest \
  --rerun-tasks
```

즉 `Poller.addReceiver(...)`, `Poller.addGateway(...)`, `Gateway.sendTo(...)`,
`Receiver.routerSocket()` 기본 경로는 same-process / cross-context same-JVM에서
동작합니다.

### 3. Java perf direct run still fails

Command:

```bash
cd bindings/java
PERF_CLIENTS=1 PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=0 \
PERF_CONNECT_READY_TIMEOUT_MS=5000 PERF_CLIENT_POLL_TIMEOUT_MS=10 \
java --enable-native-access=ALL-UNNAMED \
  -cp perf/multi/Zlink.PerfBench/build/classes/java/main:build/classes/java/main:build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-server MULTI_GATEWAY tcp 64
```

Client stderr:

```text
ERROR,MULTI_GATEWAY,client,prime_failed,send_immediate=1,send_flushed=0,recv=0,matched=0
```

Server exits cleanly and does not report a matching receive-side failure.

### 4. Java runner still fails

Command:

```bash
python3 bindings/perf/run_policy_bench.py \
  --binding java --suite multi --pattern MULTI_GATEWAY \
  --transports tcp --msg-sizes 64 --runs 1 --result
```

Observed warning:

```text
ERROR,MULTI_GATEWAY,client,prime_failed,send_immediate=100,send_flushed=0,recv=0,matched=0
```

Client reports that requests were accepted on the immediate send path, but no
reply was ever observed on the per-client receiver side.

## Current Assessment

- This is not a good core bug candidate anymore.
- It is also not a generic Java service-instance poller failure, because the
  minimal integration tests pass.
- The failing surface is narrower:
  - Java perf
  - separate-process
  - multi-gateway startup / readiness / state progression

## Non-Workaround Note

- No retry budget/cap/fallback added to hide the failure
- No raw socket rewrite used as a workaround
- Current behavior remains a hard failure with stderr + non-zero exit
