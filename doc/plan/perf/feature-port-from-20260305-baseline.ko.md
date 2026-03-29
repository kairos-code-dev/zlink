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

`2026-03-05` single compare anchor의 raw result txt 두 개는 현재 이
worktree에 남아 있지 않다. 따라서 이 anchor는 아래 기록된 기준 수치와 실행
명령을 authority로 유지한다.

- run #1:
  raw result file missing in current worktree
- run #2:
  raw result file missing in current worktree

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

## 7. 추가 Guardrail: `with_zmq single` compare baseline (`2026-03-29`)

single compare 전체 패턴 기준선도 별도로 고정한다.
이 결과는 rebuilding 작업의 현재 single compare baseline으로 사용한다.

- bench surface:
  `core/bench/with_zmq/run_benchmarks.sh`
- 대상 pattern:
  `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`
- 고정 transport:
  `tcp,ipc,inproc`

실행 명령:

```bash
./core/bench/with_zmq/run_benchmarks.sh \
  --reuse-build \
  --runs 1 \
  --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER \
  --transports tcp,ipc,inproc
```

결과 파일:

- `/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/rebuilding/baseline/perf_linux_20260329_200606.txt`

기준 사용 규칙:

- rebuilding loop의 single compare 기준은 위 결과 파일을 사용한다.
- 허용오차 정책은 동일하게
  `-5% 이내 허용 / -5~-10% warning / -10% 이하 block`으로 본다.

## 8. 추가 Guardrail: `with_zmq single` `ws/wss/tls` zlink-only baseline (`2026-03-29`)

`ws/wss/tls`는 libzmq compare 대상이 아니므로
single 전 패턴에 대해 zlink-only baseline을 별도로 고정한다.

- bench surface:
  `core/bench/with_zmq/run_benchmarks.sh`
- 대상 pattern:
  `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`
- 고정 transport:
  `tcp,ws,wss,tls`
  단, `tcp` 값은 참고용이고 compare baseline을 대체하지 않는다.

실행 명령:

```bash
./core/bench/with_zmq/run_benchmarks.sh \
  --reuse-build \
  --runs 1 \
  --zlink-only \
  --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER \
  --transports tcp,ws,wss,tls
```

결과 파일:

- `/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/rebuilding/baseline/perf_linux_20260329_155434.txt`

기준 사용 규칙:

- single `ws/wss/tls`는 위 결과 파일을 기준선으로 사용한다.
- 이 범위는 `libzmq` diff가 아니라 zlink baseline 대비 회귀만 본다.
- 허용오차 정책은 single compare와 동일하게
  `-5% 이내 허용 / -5~-10% warning / -10% 이하 block`으로 본다.

## 9. 추가 Guardrail: `with_zmq multi` compare baseline (`2026-03-29`)

이 섹션은 `2026-03-05` single anchor를 대체하지 않는다.
single anchor는 그대로 유지하고,
multi regression 감시용 compare baseline을 별도로 추가 확보한 것이다.

- bench surface:
  `core/bench/with_zmq/run_benchmarks_multi.sh`
- 목적:
  `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `STREAM`
  의 multi compare 기준선을 고정한다.
- 고정 transport:
  `tcp,ipc`
  단, `STREAM` compare는 runner 정책상 `tcp`만 측정된다.

실행 명령:

```bash
./core/bench/with_zmq/run_benchmarks_multi.sh \
  --reuse-build \
  --runs 1 \
  --results-tag multi_baseline_20260329
```

결과 파일:

- `/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/rebuilding/baseline/perf_linux_20260329_162318_multi_baseline_20260329.txt`

기준 사용 규칙:

- multi compare는 위 결과 파일을 기준선으로 사용한다.
- `tcp,ipc` 셀은 이후 동일 surface/동일 조건으로 재측정해서 비교한다.
- 허용오차 정책은 single과 동일하게
  `-5% 이내 허용 / -5~-10% warning / -10% 이하 block`으로 본다.

## 10. 추가 Guardrail: `MULTI_STREAM` `ws/wss/tls` zlink-only baseline (`2026-03-29`)

`ws/wss/tls`는 libzmq compare 대상이 아니므로
`MULTI_STREAM`에 대해 zlink-only baseline을 별도로 고정한다.

- bench surface:
  `core/bench/with_zmq/run_benchmarks_multi.sh`
- 대상 pattern:
  `STREAM`
- 고정 transport:
  `ws,wss,tls`

실행 명령:

```bash
./core/bench/with_zmq/run_benchmarks_multi.sh \
  --reuse-build \
  --runs 1 \
  --zlink-only \
  --pattern stream \
  --transports ws,wss,tls \
  --results-tag multi_stream_ws_wss_tls_baseline_20260329_rerun
```

결과 파일:

- `/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/rebuilding/baseline/perf_linux_20260329_163829_multi_stream_ws_wss_tls_baseline_20260329_rerun.txt`

기준 사용 규칙:

- `MULTI_STREAM ws/wss/tls`는 위 결과 파일을 기준선으로 사용한다.
- 이 범위는 `libzmq` diff가 아니라 zlink baseline 대비 회귀만 본다.
- 허용오차 정책은 single과 동일하게
  `-5% 이내 허용 / -5~-10% warning / -10% 이하 block`으로 본다.
