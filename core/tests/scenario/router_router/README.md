# Router-Router Scenarios

Folder split:

- `core/tests/scenario/router_router/zlink/`: C zlink scenario runner (LZ-01..LZ-05).
- `core/tests/scenario/router_router/zmq/`: libzmq-native comparison runner.

## zlink Build

```bash
cmake -S core -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build --target test_scenario_router_router -j"$(nproc)"
```

## zlink Quick Run

```bash
core/tests/scenario/router_router/zlink/run_lz_scenarios.sh
```

Logs are written under:

`core/tests/scenario/router_router/zlink/result/<timestamp>/`
