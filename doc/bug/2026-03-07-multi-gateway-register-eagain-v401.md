# [Bug Report] MULTI_GATEWAY fails on core/v4.0.1 with receiver register EAGAIN / no data

- Date: 2026-03-07
- Repo: `/home/hep7/project/kairos/zlink`
- Release tag: `core/v4.0.1`
- Runtime status: native libraries already refreshed to `4.0.1`
- Affected area: `Gateway` / `Receiver` service registration and readiness path

## Summary

`MULTI_GATEWAY` does not produce usable traffic on `core/v4.0.1`.

Two independent reproductions show the same failure class:

1. `bindings/java` direct run:
   - client fails during `Receiver.register(...)`
   - native error: `Resource temporarily unavailable (errno=11)`
2. `bindings/cpp` perf run:
   - benchmark completes with `no_data`
   - no successful request/reply traffic is observed

This does not look like a Java-only issue because the failure reproduces through
another binding on the same native runtime.

## Expected

- each client `Receiver.register(...)` succeeds once the local receiver has been
  bound and connected to the registry
- server discovery sees all client services become ready
- benchmark exchanges request/reply traffic and prints non-zero `RESULT` metrics

## Actual

- Java client fails immediately:

```text
multi_gateway_client_error:zlink_receiver_register failed: Resource temporarily unavailable (errno=11)
```

- Java server later fails waiting for discovered client services:

```text
multi_gateway_server_error:gateway_client_services_not_ready
```

- C++ binding perf run on the same `core/v4.0.1` runtime also fails:

```text
## Failures
- MULTI_GATEWAY current tcp 64B: no_data
```

## Reproduction

### 1. Java integration tests

```bash
cd bindings/java
./gradlew test integrationTest
```

Result:

- passes
- this confirms the runtime is loadable and the Java binding itself is not
  globally broken on `4.0.1`

### 2. Java direct MULTI_GATEWAY reproduction

Server:

```bash
cd bindings/java
java -cp perf/multi/Zlink.PerfBench/build/classes/java/main:perf/multi/Zlink.PerfBench/build/resources/main:build/classes/java/main:build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-server MULTI_GATEWAY tcp 64
```

Observed server ready line:

```text
READY,tcp://127.0.0.1:9017|tcp://127.0.0.1:27505|tcp://127.0.0.1:19555
```

Client:

```bash
cd bindings/java
env PERF_MULTI_CLIENTS=4 PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 \
  java -cp perf/multi/Zlink.PerfBench/build/classes/java/main:perf/multi/Zlink.PerfBench/build/resources/main:build/classes/java/main:build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-client MULTI_GATEWAY tcp 64 \
  --endpoint 'tcp://127.0.0.1:9017|tcp://127.0.0.1:27505|tcp://127.0.0.1:19555'
```

Observed client stderr:

```text
multi_gateway_client_error:zlink_receiver_register failed: Resource temporarily unavailable (errno=11)
```

Observed server stderr after client failure:

```text
multi_gateway_server_error:gateway_client_services_not_ready
```

### 3. C++ binding perf reproduction

```bash
cd /home/hep7/project/kairos/zlink
env PERF_MULTI_CLIENTS=4 \
  python3 bindings/perf/run_policy_bench.py \
    --binding cpp \
    --suite multi \
    --pattern MULTI_GATEWAY \
    --msg-sizes 64 \
    --transports tcp \
    --runs 1 \
    --reuse-build \
    --multi-warmup-seconds 0 \
    --multi-duration-seconds 1 \
    --result
```

Observed result:

```text
## Failures
- MULTI_GATEWAY current tcp 64B: no_data
```

Saved runner file:

- `bindings/cpp/perf/results/multi/tmp/perf_linux_20260307_155817.txt`

## Comparison signal

- `MULTI_SPOT` on the same `core/v4.0.1` runtime succeeds in `bindings/cpp`
- `MULTI_GATEWAY` fails in both `bindings/java` and `bindings/cpp`

This makes `MULTI_GATEWAY` much more likely to be a core/runtime issue than a
Java perf implementation issue.

## Likely core-side problem area

The strongest signal is that a normal client startup path hits:

```text
zlink_receiver_register failed: Resource temporarily unavailable (errno=11)
```

That suggests a problem in one of these areas:

1. receiver registration state machine returns `EAGAIN` during a startup path
   that previously should have been accepted
2. registry/router readiness changed but `register` no longer blocks or becomes
   reliably successful after bind/connect ordering used by bindings
3. discovery visibility for freshly registered client services is delayed or
   dropped, causing the server to stay in `gateway_client_services_not_ready`
4. service registration succeeds internally in some paths but readiness is not
   observable to gateway discovery, producing the C++ `no_data` symptom

## Non-workaround policy used here

- no retry budget was added
- no sleep-based masking was added around failed send/register paths
- no fallback path was added to make the benchmark appear successful

The failure above is the raw behavior observed on `core/v4.0.1`.

## Suggested next checks for core owner

1. reproduce `Receiver.register(...)` immediately after `bind + connectRegistry`
   with 4 parallel client receivers
2. trace whether `register` returning `EAGAIN` is intentional in current core
3. verify registry publication and gateway discovery visibility for newly
   registered client services
4. compare `core/v4.0.0` vs `core/v4.0.1` around receiver registration and
   gateway discovery readiness
