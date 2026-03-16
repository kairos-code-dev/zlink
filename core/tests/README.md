# zlink Test Suite

`core/tests` is the single test root for zlink.

## Directory Layout

| Path | Purpose |
| --- | --- |
| `core/tests/unittest/` | small internal tests labeled `unittest` |
| `core/tests/integration/` | focused functional tests labeled `integration` |
| `core/tests/e2e/` | umbrella and smoke-style tests labeled `e2e` |
| `core/tests/testutil*` | shared test helpers |
| `core/tests/run_test_lanes.sh` | sequential lane runner |

## Lane Policy

Tests are classified along two axes:

- category: `unittest`, `integration`, `e2e`
- execution mode: `parallel-safe`, `serial`

Current policy is intentionally conservative:

- `unittest` => `parallel-safe`
- `integration` => `serial`
- `e2e` => `serial`

`RESOURCE_LOCK` only coordinates tests inside one `ctest` process. Do not run
multiple `ctest` commands concurrently for serial lanes.

## Commands

Quick start:

```bash
./core/build.sh
```

Manual configure/build:

```bash
cmake -S . -B core/build -DBUILD_TESTS=ON -DBUILD_STATIC=ON -DBUILD_SHARED=ON
cmake --build core/build -j"$(nproc)"
```

Lane execution:

```bash
ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
ctest --test-dir core/build --output-on-failure -L e2e -j1
```

Sequential lane runner:

```bash
./core/tests/run_test_lanes.sh
./core/tests/run_test_lanes.sh --include-e2e
```

Thread-safe contract stress runner:

```bash
./core/tests/run_thread_safe_contract_stress.sh
./core/tests/run_thread_safe_contract_stress.sh --count 100
```

Thread-safe contract perf runner:

```bash
./core/tests/run_thread_safe_contract_perf.sh
./core/tests/run_thread_safe_contract_perf.sh --min-ratio 0.85
```

Thread-safe contract TSan runner:

```bash
./core/tests/run_thread_safe_contract_tsan.sh
./core/tests/run_thread_safe_contract_tsan.sh --build-dir core/build-tsan-clang
```

Default runner behavior:

- `unittest` in parallel
- `integration` serially
- `e2e` only when `--include-e2e` is specified

SPOT WSS first-delivery note:

- `test_spot_pubsub_scenario_unified_wss_ready_delivery` defaults to a single
  iteration in normal lanes.
- To rerun the historical flake regression with repeated attempts, execute the
  single-iteration test repeatedly via CTest.

```bash
ctest --test-dir core/build \
  --output-on-failure \
  --repeat until-fail:16 \
  -R '^test_spot_pubsub_scenario_unified_wss_ready_delivery$'
```

## Writing Tests

- Add new internal logic tests under `core/tests/unittest/`.
- Add new focused behavior tests under `core/tests/integration/`.
- Add new umbrella or executable-level smoke tests under `core/tests/e2e/`.
- Start new tests in the safest lane first. Only promote to `parallel-safe`
  after confirming the test does not depend on live socket timing, discovery
  state, global env mutation, or teardown ordering.
- Do not add retry logic or sleep-based retry loops. Use deterministic events
  and hard timeouts.

## Notes

- Some scenario-style e2e tests still live outside `core/tests/` because they
  are tied to benchmark/source-stack fixtures under `core/bench/`.
- `test_spot_node_discovery_direct_and_child_interop` and
  `test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery` remain in
  `core/tests/e2e/spot/test_spot_pubsub_scenario.cpp`, but they are not part
  of the default split integration lane. Both still expose flaky
  discovery-driven SPOT behavior and should be run only while working on those
  core bugs.
- `run_thread_safe_contract_stress.sh` repeats the selected thread-safe
  contract cases at the CTest layer. It does not add retry logic inside the
  tests themselves.
- The stress lane currently covers discovery control-path teardown, gateway
  runtime read and attach/query ordering, gateway send-ready self-close,
  spot runtime read, spot monitor-child destroy, and spot send-ready
  self-close lifecycle cases.
- `run_thread_safe_contract_perf.sh` executes the raw/gateway/spot 1/4/16/64
  handle scaling contract cases with a configurable acceptance ratio.
- `run_thread_safe_contract_tsan.sh` configures a dedicated TSan build and
  runs the thread-safe regression lane against that build tree, including the
  discovery control-path, gateway attach/query, gateway self-close, and spot
  monitor-child/self-close regressions.
- CURVE/libsodium and GSSAPI are not supported in zlink.
