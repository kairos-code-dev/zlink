# Asio STREAM Scenario Runner

Binary:

- `core/build/bin/test_scenario_stream_asio`

Batch script:

- `core/tests/scenario/stream/asio/run_stream_scenarios.sh`

Notes:

- Current transport support is `tcp` (non-tcp runs are marked `SKIP`).

Example:

```bash
LD_LIBRARY_PATH=core/build/lib core/build/bin/test_scenario_stream_asio \
  --scenario s2 --transport tcp --ccu 10000 --size 1024 --inflight 30 \
  --warmup 3 --measure 10 --drain-timeout 10 --connect-concurrency 256 \
  --io-threads 8 --send-batch 1 --latency-sample-rate 16
```
