# `with_zmq single` 회귀 이분 탐색 로그

작업 경로: `/home/hep7/project/kairos/zlink-perf-regression-bisect`

## -1. Current Bisect Summary

- current good:
  `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- current bad:
  `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- current worktree HEAD:
  `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- next exact step:
  `7bea9e3f -> 9b91234c -> 77550a0a` 사이에서
  실제 hot-path를 바꾼 commit만 남긴다.
  현재 정리 대상은
  `ff0140e5` (`pipe::_out_sync`),
  `a819ea3a` (`socket_base_t::send()` public admission/CAS),
  `98e7d324` (public multipart `zlink_send/zlink_recv`),
  `9b91234c` (bench hot-loop activation + `PERF_SINGLE_MAX_INFLIGHT`
  제거)이다.
- do not forget:
  - 측정 surface를 바꾸지 않는다.
  - `PAIR`, `DEALER_DEALER` `64B tcp/inproc`만 먼저 본다.
  - 미세 최적화 코드를 만들지 않는다.

## 0. Scope

- surface: `core/bench/with_zmq/single/run_comparison.py`
- focus: `PAIR`, `DEALER_DEALER` `64B` `tcp,inproc`
- build dir: `core/build/` only
- goal: first bad commit 또는 매우 작은 culprit 구간 특정

## 1. Fixed Command

```bash
python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag <tag>
```

사전 빌드:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=OFF
cmake --build core/build \
  --target comp_std_zmq_pair comp_zlink_pair \
           comp_std_zmq_dealer_dealer comp_zlink_dealer_dealer \
  -j"$(nproc)"
```

## 2. Iteration Log

### 2.1 Baseline Setup

- `good` 후보: `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- `bad` 후보: `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- 판정 규칙:
  - `PAIR tcp 64B`와 `DEALER_DEALER tcp 64B`가 `libzmq` 대비 큰 음수 gap이면 bad
  - `3/05` 수준처럼 `libzmq`와 비슷하거나 우세하면 good
- 현재 good: `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- 현재 bad: `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- 다음 mid: `376347fd32720f0685e0670641469e95c42cf6b0`

### 2.2 Iteration 0: HEAD bad 확인

- commit: `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- build:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=ON \
  -DZLINK_BUILD_WITH_ZMQ_ZLINK_BENCHES=ON
cmake --build core/build \
  --target comp_std_zmq_pair comp_zlink_pair \
           comp_std_zmq_dealer_dealer comp_zlink_dealer_dealer \
  -j"$(nproc)"
```

- run:

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag bisect_head_95d8a3b2
```

- result file:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_141312_bisect_head_95d8a3b2.txt`
- throughput:
  - `PAIR tcp`: `libzmq 3646.94 Kmsg/s`, `zlink 3209.27 Kmsg/s`, `-12.00%`
  - `PAIR inproc`: `libzmq 4227.39 Kmsg/s`, `zlink 3309.67 Kmsg/s`, `-21.71%`
  - `DEALER_DEALER tcp`: `libzmq 3569.88 Kmsg/s`, `zlink 3308.38 Kmsg/s`, `-7.33%`
  - `DEALER_DEALER inproc`: `libzmq 3997.98 Kmsg/s`, `zlink 2716.67 Kmsg/s`, `-32.05%`
- verdict: `bad`

### 2.3 Iteration 1: `3/05` good 후보 확인

- commit: `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- build:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=ON \
  -DZLINK_BUILD_WITH_ZMQ_ZLINK_BENCHES=ON
cmake --build core/build \
  --target comp_std_zmq_pair comp_zlink_pair \
           comp_std_zmq_dealer_dealer comp_zlink_dealer_dealer \
  -j"$(nproc)"
```

- run #1:

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag bisect_good_7bea9e3f
```

- result file #1:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_141453_bisect_good_7bea9e3f.txt`
- throughput #1:
  - `PAIR tcp`: `libzmq 850.33 Kmsg/s`, `zlink 3572.10 Kmsg/s`, `+320.08%`
  - `PAIR inproc`: `libzmq 3434.06 Kmsg/s`, `zlink 3336.56 Kmsg/s`, `-2.84%`
  - `DEALER_DEALER tcp`: `libzmq 2787.20 Kmsg/s`, `zlink 3550.44 Kmsg/s`, `+27.38%`
  - `DEALER_DEALER inproc`: `libzmq 3947.44 Kmsg/s`, `zlink 3358.15 Kmsg/s`, `-14.93%`

- run #2:

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag bisect_good_7bea9e3f_rerun
```

- result file #2:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_141555_bisect_good_7bea9e3f_rerun.txt`
- throughput #2:
  - `PAIR tcp`: `libzmq 2666.72 Kmsg/s`, `zlink 3678.93 Kmsg/s`, `+37.96%`
  - `PAIR inproc`: `libzmq 3285.80 Kmsg/s`, `zlink 3428.38 Kmsg/s`, `+4.34%`
  - `DEALER_DEALER tcp`: `libzmq 2792.41 Kmsg/s`, `zlink 3622.52 Kmsg/s`, `+29.73%`
  - `DEALER_DEALER inproc`: `libzmq 3854.89 Kmsg/s`, `zlink 3582.15 Kmsg/s`, `-7.08%`

- verdict: `good`
- note:
  - `tcp`는 두 패턴 모두 명확히 good
  - `inproc`는 pair는 동등권, dealer는 소폭 열세지만 current HEAD처럼 본격 붕괴 상태는 아님
  - `3/05` good anchor로 사용 가능

### 2.4 Current Bisect State

- 현재 good: `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- 현재 bad: `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- 다음 mid: `376347fd32720f0685e0670641469e95c42cf6b0`
- next mid reason:
  - `git rev-list --bisect` 기준 midpoint
  - 시점상 `thread-safe socket` 계약 완료 뒤, `3/18` public-surface 전환 전

### 2.5 Iteration 2: midpoint `376347fd` buildability check

- commit: `376347fd32720f0685e0670641469e95c42cf6b0`
- intended role: first bisect midpoint
- build command:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=ON \
  -DZLINK_BUILD_WITH_ZMQ_ZLINK_BENCHES=ON
cmake --build core/build \
  --target comp_std_zmq_pair comp_zlink_pair \
           comp_std_zmq_dealer_dealer comp_zlink_dealer_dealer \
  -j"$(nproc)"
```

- result: `skip (unbuildable)`
- failure summary:
  - `bench_common_zlink.hpp`가 `zlink_recv`, `zlink_msg_recv`,
    `zlink_socket_peers`, `zlink_peer_info_t`를 사용
  - same-commit `core/include/zlink.h`에는 위 선언이 없음
  - 따라서 fixed `with_zmq zlink` surface가 direct-callback 전환 중간 상태에서
    깨져 있어 성능 측정 불가
- implication:
  - `3/12` 이후 한 구간은 `single with_zmq` zlink bench가 skip 구간이다
  - 이 구간은 성능 good/bad가 아니라 surface buildability break로 기록
  - 다음 buildable boundary를 찾아 bisection을 이어간다

### 2.6 Updated Bisect State

- 현재 good: `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- 현재 bad: `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- skip: `376347fd32720f0685e0670641469e95c42cf6b0`
- 다음 buildable boundary candidate: `9b91234c1cd1f8fc8b2766e584e61eec35fd3adb`
- next candidate reason:
  - `3/18` public API realignment commit
  - `bench_zlink_pair/dealer_dealer`가 `zlink_send/zlink_recv` surface로 이동한 시작점

### 2.7 Iteration 3: candidate `9b91234c` buildability check

- commit: `9b91234c1cd1f8fc8b2766e584e61eec35fd3adb`
- result: `skip (unbuildable)`
- failure summary:
  - `comp_std_zmq_pair` build에서 `bench_zmq_pair.cpp`가 `send_exact`를 호출
  - same-commit `single/common/bench_common.hpp`에는 `send_exact` helper가 없음
  - 즉 `3/18` public-surface realignment 직후에도 fixed `with_zmq single`
    comparison surface 전체는 아직 복구되지 않음
- implication:
  - `9b91234c`는 first measurable bad가 아니라 still-broken surface
  - `3/18 -> 3/27` 초반까지 buildability boundary를 더 좁혀야 함

### 2.8 Iteration 4: first restored surface candidate `77550a0a`

- commit: `77550a0aa1ce05d8f9f7f38c2b026338b3925e45`
- build:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=ON \
  -DZLINK_BUILD_WITH_ZMQ_ZLINK_BENCHES=ON
cmake --build core/build \
  --target comp_std_zmq_pair comp_zlink_pair \
           comp_std_zmq_dealer_dealer comp_zlink_dealer_dealer \
  -j"$(nproc)"
```

- run:

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag bisect_77550a0a
```

- result file:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_142205_bisect_77550a0a.txt`
- throughput:
  - `PAIR tcp`: `libzmq 3536.27 Kmsg/s`, `zlink 616.77 Kmsg/s`, `-82.56%`
  - `PAIR inproc`: `libzmq 4009.99 Kmsg/s`, `zlink 1183.88 Kmsg/s`, `-70.48%`
  - `DEALER_DEALER tcp`: `libzmq 3740.92 Kmsg/s`, `zlink 611.50 Kmsg/s`, `-83.65%`
  - `DEALER_DEALER inproc`: `libzmq 4019.53 Kmsg/s`, `zlink 974.59 Kmsg/s`, `-75.75%`
- verdict: `bad`
- note:
  - fixed comparison surface가 다시 buildable해진 뒤의 첫 strong bad 후보
  - later HEAD보다도 훨씬 더 나쁘므로, 이후 commit들은 mostly partial recovery로 해석 가능

### 2.9 Iteration 5: `77550a0a^` buildability check

- commit: `b7a68af72af3d26844750881f93be7aed94151e2`
- build:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=ON \
  -DZLINK_BUILD_WITH_ZMQ_ZLINK_BENCHES=ON
cmake --build core/build \
  --target comp_std_zmq_pair comp_zlink_pair \
           comp_std_zmq_dealer_dealer comp_zlink_dealer_dealer \
  -j"$(nproc)"
```

- result: `skip (unbuildable)`
- failure summary:
  - `single/common/bench_common.hpp`가
    `zlink_set_option`, `zlink_get_option`,
    `ZLINK_OPT_TLS_CERT`, `ZLINK_OPT_TLS_KEY`, `ZLINK_OPT_TLS_CA`,
    `ZLINK_OPT_TLS_HOSTNAME`, `ZLINK_OPT_TLS_TRUST_SYSTEM`,
    `ZLINK_OPT_LAST_ENDPOINT`를 전제로 하지만 same-commit public surface에는
    대응 alias가 없다
  - `single/zmq/bench_zmq_pair.cpp`는 여전히 `send_exact()`를 호출하지만
    same-commit `bench_common.hpp`에는 helper가 없다
  - 따라서 `77550a0a` 직전까지 fixed `with_zmq single` comparison surface는
    zlink public API renaming + std benchmark helper 누락 때문에 여전히 깨져 있다
- implication:
  - `77550a0a`는 단순 성능 회귀 commit이라기보다,
    깨져 있던 comparison surface를 다시 buildable하게 만드는
    `first buildable bad` 경계다
  - `77550a0a` diff에서 `with_zmq` 관련 실질 변경은
    `core/bench/with_zmq/single/common/bench_common.hpp`,
    `core/bench/with_zmq/std_compat/zlink.h`,
    `core/bench/with_zmq/CMakeLists.txt`에 집중된다
  - 즉 `77550a0a`는 already-bad structural state를 드러낸 복구 경계로 보는 것이
    더 정확하다

### 2.10 Structural Boundary Mapping

- first structural bad candidate: `9b91234c1cd1f8fc8b2766e584e61eec35fd3adb`
- evidence:
  - `git log -S'zlink_send (' -- core/bench/with_zmq/single/zlink/...`의
    첫 hit가 `9b91234c`
  - `7bea9e3f -> 9b91234c` diff에서
    zlink `PAIR`/`DEALER_DEALER` bench가
    `send_exact + zlink_msg_recv`에서
    `zlink_send + zlink_recv + multipart close/free`로 이동
  - 반면 std `libzmq` bench는 thin `send_exact + zlink_msg_recv` 경로를 유지
- first pipe/publication hot-path change:
  `ff0140e50a8174f045f86b92aa055727734f35c4`
  - `pipe_t::check_write/write/flush`를 포함한 hot path에 `_out_sync`
    fast mutex가 들어감
- first send-side public admission change:
  `a819ea3a`
  - `socket_base_t::send()`가
    `xsend(msg_)` 직호출에서
    `enter_public_api()` +
    `lock_public_api_sync(); xsend(msg_); unlock_public_api_sync();`
    구조로 바뀜
- first public multipart API change:
  `98e7d324`
  - `zlink_send(void*, const void*, size_t, flags)` /
    `zlink_recv(void*, void*, size_t, flags)`가
    multipart `zlink_send(void*, zlink_msg_t*, size_t, flags)` /
    `zlink_recv(void*, rid*, zlink_msg_t**, size_t*, flags)`로 교체됨
- buildability restore boundary: `77550a0aa1ce05d8f9f7f38c2b026338b3925e45`
  - `bench_common.hpp`에 option/helper alias 추가
  - `std_compat/zlink.h` 추가
  - fixed comparison surface가 다시 측정 가능해짐

### 2.10.1 Actual Code-Change Map

- `7bea9e3f` good:
  - zlink bench inner loop는
    `send_exact(payload)` +
    caller-owned `zlink_msg_recv(&msg)` 경로

- `ff0140e5` (`2026-03-12`)
  - `pipe.cpp`에 `_out_sync`가 들어가며
    `check_read/read/check_write/write/rollback/flush`가 lock 아래로 이동

- `a819ea3a` (`2026-03-15`)
  - `socket_base_t::send()`에
    `enter_public_api()`와 `lock_public_api_sync()`가 추가됨
  - send 1회당 inflight atomic + CAS lock 비용이 새로 붙음

- `98e7d324` (`2026-03-18 12:57`)
  - public `zlink_send/zlink_recv`가 multipart contract로 바뀜
  - `send_socket_parts()` / `recv_socket_parts()`가 생김
  - `zlink_msg_send/zlink_msg_recv`는 compat 역할로 밀림

- `9b91234c` (`2026-03-18 18:13`)
  - `bench_zlink_pair.cpp`, `bench_zlink_dealer_dealer.cpp`가
    실제로 위 public multipart 경로를 타기 시작
  - send:
    `send_exact()` ->
    `zlink_msg_init_size()+memcpy()+zlink_send()`
  - recv:
    `zlink_msg_recv()` ->
    `zlink_recv()+zlink_multipart_close()+free()`
  - `PERF_SINGLE_MAX_INFLIGHT` limiter도 제거됨

- 해석:
  - first structural bad는 `9b91234c`
  - 하지만 그 직전까지 이미 `ff0140e5`와 `a819ea3a`, `98e7d324`로
    hot-path 비용이 들어와 있었고,
    `9b91234c`가 그 경로를 실제 측정 loop에서 밟게 만든 activation commit이다

### 2.11 Current Bisect State

- 현재 good: `7bea9e3f6af1542d0f216397e729b8d9f529372d`
- skip surface-broken interval: `376347fd` 포함, 최소 `9b91234c`까지 확인
- first structural bad candidate: `9b91234c1cd1f8fc8b2766e584e61eec35fd3adb`
- first buildable bad candidate: `77550a0aa1ce05d8f9f7f38c2b026338b3925e45`
- 현재 bad anchor: `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- 다음 exact step:
  - report에
    `ff0140e5 -> a819ea3a -> 98e7d324 -> 9b91234c`
    실제 코드 변화 지도를 반영하고,
    multipart 배열 할당 수정과 겹치지 않는 원인을
    `a819ea3a` + `ff0140e5`로 분리
    정리
    로 최종 정리

### 2.12 Supplementary Diagnostic: current HEAD raw-msg toggle

- 목적:
  - 이미 적용된 `recv_tls_view` / single-part fast path 이후에도 남는 gap이
    old `multipart array alloc` 문제인지, 아니면 그 아래 core send path인지 분리
- commit: `95d8a3b2673734dcecb7fc66155cb72c75c5386d`
- same build command:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=OFF \
  -DBUILD_BENCHMARKS=ON \
  -DZLINK_BUILD_WITH_ZMQ_ZLINK_BENCHES=ON
cmake --build core/build \
  --target comp_std_zmq_pair comp_zlink_pair \
           comp_std_zmq_dealer_dealer comp_zlink_dealer_dealer \
  -j"$(nproc)"
```

- public default rerun:

```bash
BENCH_NO_AUTOBUILD=1 python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag bisect_head_95d8a3b2_rerun_public
```

- public rerun result file:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_144711_bisect_head_95d8a3b2_rerun_public.txt`
- public rerun throughput:
  - `PAIR tcp`: `libzmq 3566.35 Kmsg/s`, `zlink 3279.12 Kmsg/s`, `-8.05%`
  - `PAIR inproc`: `libzmq 4288.19 Kmsg/s`, `zlink 3411.24 Kmsg/s`, `-20.45%`
  - `DEALER_DEALER tcp`: `libzmq 3791.43 Kmsg/s`, `zlink 3161.76 Kmsg/s`, `-16.61%`
  - `DEALER_DEALER inproc`: `libzmq 3974.89 Kmsg/s`, `zlink 3222.43 Kmsg/s`, `-18.93%`

- raw msg API diagnostic:

```bash
PERF_SINGLE_ZLINK_RAW_MSG_API=1 BENCH_NO_AUTOBUILD=1 \
python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,DEALER_DEALER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag bisect_head_95d8a3b2_rawmsg
```

- raw result file #1:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_144625_bisect_head_95d8a3b2_rawmsg.txt`
- raw throughput #1:
  - `PAIR tcp`: `libzmq 3711.93 Kmsg/s`, `zlink 2905.64 Kmsg/s`, `-21.72%`
  - `PAIR inproc`: `libzmq 3950.40 Kmsg/s`, `zlink 3302.37 Kmsg/s`, `-16.40%`
  - `DEALER_DEALER tcp`: `libzmq 4115.75 Kmsg/s`, `zlink 3238.81 Kmsg/s`, `-21.31%`
  - `DEALER_DEALER inproc`: `libzmq 4093.44 Kmsg/s`, `zlink 3332.82 Kmsg/s`, `-18.58%`

- raw result file #2:
  `/home/hep7/project/kairos/zlink-perf-regression-bisect/core/bench/with_zmq/results/single/report/perf_linux_20260328_144757_bisect_head_95d8a3b2_rawmsg_rerun.txt`
- raw throughput #2:
  - `PAIR tcp`: `libzmq 4347.26 Kmsg/s`, `zlink 3254.37 Kmsg/s`, `-25.14%`
  - `PAIR inproc`: `libzmq 4173.59 Kmsg/s`, `zlink 3147.81 Kmsg/s`, `-24.58%`
  - `DEALER_DEALER tcp`: `libzmq 3540.12 Kmsg/s`, `zlink 2902.77 Kmsg/s`, `-18.00%`
  - `DEALER_DEALER inproc`: `libzmq 3947.35 Kmsg/s`, `zlink 3132.98 Kmsg/s`, `-20.63%`

- interpretation:
  - current HEAD에서 `PERF_SINGLE_ZLINK_RAW_MSG_API=1`는 gap을 일관되게 줄이지 못했다
  - current code는 이미
    `b812ab49` 이후 `recv_tls_view`와
    `send_socket_singlepart_fast`를 갖고 있으므로,
    old `multipart array alloc` / `public recv export` 문제는 residual gap의
    주원인으로 보기 어렵다
  - 따라서 historical code-change map과 별도로 봐도,
    multipart 배열 export와 겹치지 않는 send-side 원인은
    `socket_base_t::send()` public admission/lock과
    `pipe::_out_sync` serialization으로 남는다
