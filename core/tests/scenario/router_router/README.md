# Router-Router Scenario Runner

`test_scenario_router_router` reproduces LZ-01..LZ-05 style checks directly on C `zlink`.

## Build

```bash
cmake -S core -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build --target test_scenario_router_router -j"$(nproc)"
```

## Quick Run

```bash
core/tests/scenario/router_router/run_lz_scenarios.sh
```

Logs are written under
`core/tests/scenario/router_router/result/<timestamp>/`.

## Direct Commands

```bash
core/build/bin/test_scenario_router_router --scenario lz-02 --self-connect 0 --size 64 --ccu 50 --inflight 10 --duration 10
core/build/bin/test_scenario_router_router --scenario lz-03 --self-connect 1 --size 64 --ccu 50 --inflight 10 --duration 10
```
