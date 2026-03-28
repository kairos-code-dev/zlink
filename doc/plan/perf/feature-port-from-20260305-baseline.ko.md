# `2026-03-05` 기능 이식 작업 시작점

## 1. 브랜치 / 기준 커밋

- 작업 브랜치:
  `wip/feature-port-from-20260305`
- 기준 커밋:
  `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- 기준 시각:
  `2026-03-05 22:48:49 +0900`
- 기준 이유:
  `with_zmq single`에서
  `PAIR` / `DEALER_DEALER` `64B` `tcp/inproc`가 아직 good band에 있다.

## 2. 고정 측정 Surface

- build dir:
  `core/build/`
- bench surface:
  `core/bench/with_zmq/single/run_comparison.py`
- first guardrail:
  `PAIR`, `DEALER_DEALER`, `64B`, `tcp,inproc`

## 3. 실행 명령

구성:

```bash
cmake -S . -B core/build \
  -DZLINK_BUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=ON \
  -DZLINK_BUILD_WITH_ZMQ_ZLINK_BENCHES=ON
```

빌드:

```bash
cmake --build core/build \
  --target comp_std_zmq_pair comp_zlink_pair \
           comp_std_zmq_dealer_dealer comp_zlink_dealer_dealer \
  -j"$(nproc)"
```

측정:

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag baseline_20260305_feature_port
```

rerun:

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag baseline_20260305_feature_port_rerun
```

## 4. 결과 파일

- run #1:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_174940_baseline_20260305_feature_port.txt`
- run #2:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_175009_baseline_20260305_feature_port_rerun.txt`

## 5. 기준 throughput

run #1

- `PAIR tcp`:
  `libzmq 2691.53 Kmsg/s`, `zlink 3601.67 Kmsg/s`, `+33.81%`
- `PAIR inproc`:
  `libzmq 3221.22 Kmsg/s`, `zlink 3619.88 Kmsg/s`, `+12.38%`
- `DEALER_DEALER tcp`:
  `libzmq 2793.71 Kmsg/s`, `zlink 3784.85 Kmsg/s`, `+35.48%`
- `DEALER_DEALER inproc`:
  `libzmq 3624.60 Kmsg/s`, `zlink 3213.12 Kmsg/s`, `-11.35%`

run #2

- `PAIR tcp`:
  `libzmq 2714.73 Kmsg/s`, `zlink 3836.47 Kmsg/s`, `+41.32%`
- `PAIR inproc`:
  `libzmq 3438.56 Kmsg/s`, `zlink 3557.85 Kmsg/s`, `+3.47%`
- `DEALER_DEALER tcp`:
  `libzmq 2939.23 Kmsg/s`, `zlink 3617.51 Kmsg/s`, `+23.08%`
- `DEALER_DEALER inproc`:
  `libzmq 3857.84 Kmsg/s`, `zlink 3352.91 Kmsg/s`, `-13.09%`

## 6. 앞으로의 운영 규칙

- 기능 이식/변경은 이 브랜치에서만 진행한다.
- 스펙 기준은 main 쪽 문서
  `doc/internals`, `doc/guide`, `doc/api`를 따른다.
- 성능 검증은 이 브랜치의 `with_zmq` surface를 유지한 채 반복한다.
- later main의 public-surface/bench 변화를 무심코 가져오지 않는다.
  특히 `PAIR`/`DEALER_DEALER` one-way hot loop를 바꾸는 변경은
  기능 이식과 분리해서 본다.
