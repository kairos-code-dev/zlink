# Shared multi `PUBSUB` perf high-load case fails to reach active data for large TCP payloads

## Summary

The same failure class is reproducible in both:

- `bindings/cpp/perf` `MULTI_PUBSUB`
- core/perf split multi `PUBSUB`

Current shared pattern:

- transport: `tcp`
- large payloads such as `65536`
- high-load split multi defaults such as `clients=100`, `warmup=3s`, `duration=5s`, `hwm=100000`

This should be classified as a **shared perf benchmark/model issue**.
It is not a core API/runtime release blocker.

## C++ perf repro signal

Observed runner warning:

```text
MULTI_PUBSUB tcp 65536 non-zero exit(1) stderr: \
PUBSUB_CLIENT_FAIL,stage=no_active_data,transport=tcp,size=65536,warmup=384209,drain=63868,active=0
```

Observed direct client stderr:

```text
PUBSUB_CLIENT_FAIL,stage=no_active_data,transport=tcp,size=65536,warmup=445807,drain=74879,active=0
```

## core/perf repro signal

Under the same class of settings, the core split multi `PUBSUB` path also fails to produce normal throughput/latency output for the client.
The failure is therefore not specific to the C++ binding wrapper layer.

## Expected

- The split multi `PUBSUB` benchmark should progress into `phase_active` under the default high-load runner model.
- Large-message warmup backlog should not erase the active measurement window.

## Current conclusion

- Not a core runtime crash
- Not a binding-only issue
- Not a native runtime release blocker
- Likely a shared multi `PUBSUB` readiness / phase progression / backlog handling issue
