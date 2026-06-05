# [Java Perf Bug] `MULTI_SPOT` separate-process path still fails on `core/v4.0.2`

- Date: 2026-03-07
- Repo: `/home/hep7/project/kairos/zlink`
- Release tag: `core/v4.0.2`
- Scope: `bindings/java/perf`
- Affected area: `MULTI_SPOT` Java perf separate-process path

## Summary

`MULTI_SPOT`는 `core/v4.0.2`에서 core-global blocker로 보이지 않습니다.

- `bindings/cpp` `MULTI_SPOT tcp/64`는 통과
- Java `Spot` service-instance poller 최소 경로는 same-context / cross-context
  same-JVM에서 통과
- 하지만 Java perf separate-process 경로는 여전히 `no_active_frames`

즉 기존의 core-side 가설보다, Java perf/binding separate-process 경로를
우선 봐야 하는 상태입니다.

## Evidence

### 1. C++ `MULTI_SPOT` passes on `v4.0.2`

Command:

```bash
python3 bindings/perf/run_policy_bench.py \
  --binding cpp --suite multi --pattern MULTI_SPOT \
  --transports tcp --msg-sizes 64 --runs 1 --result
```

Observed:

```text
530.35 Kmsg/s
latency=3273.6300 ms
```

### 2. Java service-instance poller minimal tests pass

Targeted integration tests:

- [TestSpotServicePollerPortedTest.java](../../bindings/java/src/test/java/dev/kairoscode/zlink/integration/TestSpotServicePollerPortedTest.java)
  - `testSpotSubCanBePolledViaServiceInstance`
  - `testSpotPubCanBePolledViaServiceInstance`
  - `testSpotSubCanBePolledViaServiceInstanceAcrossContexts`

Command:

```bash
cd bindings/java
./gradlew integrationTest \
  --tests dev.kairoscode.zlink.integration.TestSpotServicePollerPortedTest \
  --rerun-tasks
```

Result: pass

### 3. Java perf direct run still fails

Command:

```bash
cd bindings/java
PERF_CLIENTS=1 PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=0 \
PERF_CONNECT_READY_TIMEOUT_MS=5000 PERF_CLIENT_POLL_TIMEOUT_MS=10 \
java --enable-native-access=ALL-UNNAMED \
  -cp perf/multi/Zlink.PerfBench/build/classes/java/main:build/classes/java/main:build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-client MULTI_SPOT tcp 64 --endpoint <READY endpoint>
```

Observed:

```text
ERROR,MULTI_SPOT,client,no_active_frames
```

### 4. C++ parity changes on Java side did not fix it

Re-tested after aligning Java perf with part of the C++ spot setup:

- `XPUB_NODROP` option parity
- post-connect settle `300ms`

Result did not change:

```text
ERROR,MULTI_SPOT,client,no_active_frames
```

## Current Assessment

- This is not a strong core bug candidate on `v4.0.2`, because C++ perf passes.
- It is also not a generic Java `Spot` service-instance poller failure, because
  the targeted integration tests pass.
- The narrower failing surface is:
  - Java perf
  - separate-process
  - multi-spot active-frame observation

## Related Note

Earlier investigation in
[2026-03-07-spot-service-poller-dontwait-v401.md](/home/hep7/project/kairos/zlink/doc/bug/2026-03-07-spot-service-poller-dontwait-v401.md)
already showed that the older "`add_spot_pub` makes `DONTWAIT` unsupported"
explanation was incorrect. `v4.0.2` recheck narrows the remaining issue further
to the Java side.

## Non-Workaround Note

- No raw socket fallback added
- No retry/cap/fallback added to make the benchmark look successful
- Current behavior remains a hard failure with stderr + non-zero exit
