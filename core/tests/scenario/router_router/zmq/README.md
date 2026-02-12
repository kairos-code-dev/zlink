# libzmq Router-Router Scenario

- Source: `core/tests/scenario/router_router/zmq/libzmq_native_router_router_bench.cpp`
- Purpose: run the same router-router flow against libzmq-native binary for comparison.

## Build

Example using local libzmq-native release files:

```bash
g++ -O2 -std=c++11 \
  core/tests/scenario/router_router/zmq/libzmq_native_router_router_bench.cpp \
  -I/tmp/libzmq-native-v435/include \
  -L/tmp/libzmq-native-v435/libzmq-linux-x64 \
  -Wl,-rpath,/tmp/libzmq-native-v435/libzmq-linux-x64 \
  -lzmq -lpthread \
  -o core/tests/scenario/router_router/zmq/bin/libzmq_native_router_router_bench
```

## Quick Run

```bash
LD_LIBRARY_PATH=/tmp/libzmq-native-v435/libzmq-linux-x64:${LD_LIBRARY_PATH:-} \
core/tests/scenario/router_router/zmq/bin/libzmq_native_router_router_bench \
  --self-connect 1 --size 1024 --ccu 10000 --inflight 10 --duration 10 --senders 1
```

Warmup mode options:

- default: stable warmup
- `--warmup-legacy`: legacy warmup loop
- `--warmup-none`: start benchmark traffic immediately without ready gate

## Output Metrics

The runner prints the same `METRIC ...` line format as `zlink-connect`.

Mapping note:

- `connection_ready` in this runner is counted from `ZMQ_EVENT_HANDSHAKE_SUCCEEDED`.
