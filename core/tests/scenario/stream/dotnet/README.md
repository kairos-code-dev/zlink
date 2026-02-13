# .NET STREAM Scenario Runner

Binary:

- `core/tests/scenario/stream/dotnet/bin/Release/net8.0/StreamSocketScenario.dll`

Batch script:

- `core/tests/scenario/stream/dotnet/run_stream_scenarios.sh`

Notes:

- Current transport support is `tcp` (non-tcp runs are marked `SKIP`).

Example:

```bash
dotnet core/tests/scenario/stream/dotnet/bin/Release/net8.0/StreamSocketScenario.dll \
  --scenario s2 --transport tcp --ccu 10000 --size 1024 --inflight 30 \
  --warmup 3 --measure 10 --drain-timeout 10 --connect-concurrency 256 \
  --io-threads 4 --latency-sample-rate 16
```
