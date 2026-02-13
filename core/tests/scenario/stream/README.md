# STREAM Scenario Bench (Client -> Server)

This folder contains reproducible STREAM benchmarks aligned to the
`zlink-stream-10k-repro-and-tuning-scenarios.md` plan.

Layout:

- `core/tests/scenario/stream/zlink/`: zlink STREAM runner and batch script
- `core/tests/scenario/stream/asio/`: Boost.Asio TCP runner and batch script
- `core/tests/scenario/stream/dotnet/`: .NET TCP socket runner and batch script
- `core/tests/scenario/stream/cppserver/`: CppServer-backed runner and helper scripts
- `core/tests/scenario/stream/net-zlink/`: .NET Zlink STREAM runner and batch script
- `core/tests/scenario/stream/run_stream_compare.sh`: runs all runners and writes result artifacts

## Build

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build --target test_scenario_stream_zlink test_scenario_stream_asio -j"$(nproc)"
```

## Quick run (S0~S2, tcp, 5 stacks)

```bash
RUN_DOTNET=1 RUN_CPPSERVER=1 RUN_NET_ZLINK=1 \
core/tests/scenario/stream/run_stream_compare.sh
```

Default profile:

- `ccu=10000`
- `size=1024`
- `inflight=30` (per connection)
- `warmup=3s`
- `measure=10s`
- `drain-timeout=10s`
- `connect-concurrency=256`
- `run_dotnet=1`
- `run_cppserver=1`
- `run_net_zlink=1`

Outputs are written to:

`../playhouse/doc/plan/zlink-migration/results/<timestamp>/libzlink-stream-10k/`

Files:

- `summary.json`
- `scenario.log`
- `metrics.csv`
- `kernel.log`

## Optional sweeps

Run S3/S4 extensions:

```bash
RUN_S3=1 RUN_S4=1 core/tests/scenario/stream/run_stream_compare.sh
```

You can override profile variables:

```bash
CCU=2000 SIZE=1024 INFLIGHT=20 WARMUP=2 MEASURE=5 \
CONNECT_CONCURRENCY=128 IO_THREADS=8 SEND_BATCH=1 LATENCY_SAMPLE_RATE=16 \
core/tests/scenario/stream/run_stream_compare.sh
```
