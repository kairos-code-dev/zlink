# zlink STREAM Scenario Runner

Binary:

- `core/build/bin/test_scenario_stream_zlink`

Batch script:

- `core/tests/scenario/stream/zlink/run_stream_scenarios.sh`

Example:

```bash
LD_LIBRARY_PATH=core/build/lib core/build/bin/test_scenario_stream_zlink \
  --scenario s2 --transport tcp --ccu 10000 --size 1024 --inflight 30 \
  --warmup 3 --measure 10 --drain-timeout 10 --connect-concurrency 256 \
  --io-threads 4 --latency-sample-rate 16
```
