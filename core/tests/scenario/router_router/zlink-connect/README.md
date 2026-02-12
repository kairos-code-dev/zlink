# zlink-connect Router-Router Scenario

- Source: `core/tests/scenario/router_router/zlink-connect/test_scenario_router_router_zlink_connect.cpp`
- Purpose: same flow/logic as `zmq/libzmq_native_router_router_bench.cpp`, but linked to zlink.
- Target: `test_scenario_router_router_zlink_connect`

## Build

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build --target test_scenario_router_router_zlink_connect -j"$(nproc)"
```

## Quick Run

```bash
LD_LIBRARY_PATH=core/build/lib core/build/bin/test_scenario_router_router_zlink_connect \
  --self-connect 1 --size 1024 --ccu 10000 --inflight 10 --duration 10 --senders 1
```

## Batch Run

```bash
core/tests/scenario/router_router/zlink-connect/run_zlink_connect_scenarios.sh
```

Logs are written to:

`core/tests/scenario/router_router/zlink-connect/result/<timestamp>/`
