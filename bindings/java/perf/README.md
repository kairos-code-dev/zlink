# Java Perf Bench

Java perf benchmark suite for `dev.kairoscode.zlink`.

## Build

```bash
cd bindings/java
./gradlew -q :perf-single:classes :perf-multi:classes
```

## Single

```bash
cd bindings/java/perf
./run_benchmarks.sh
```

Direct run:

```bash
java --enable-native-access=ALL-UNNAMED \
  -cp perf/single/Zlink.PerfBench/build/classes/java/main:build/classes/java/main:build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMain PAIR tcp 64
```

## Multi

```bash
cd bindings/java/perf
./run_benchmarks_multi.sh
```

Direct server/client run:

```bash
java --enable-native-access=ALL-UNNAMED \
  -cp perf/multi/Zlink.PerfBench/build/classes/java/main:build/classes/java/main:build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain --multi-server MULTI_DEALER_ROUTER tcp 64

java --enable-native-access=ALL-UNNAMED \
  -cp perf/multi/Zlink.PerfBench/build/classes/java/main:build/classes/java/main:build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain --multi-client MULTI_DEALER_ROUTER tcp 64 --endpoint tcp://127.0.0.1:5555
```
