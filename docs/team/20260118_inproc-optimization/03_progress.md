# Progress Log

## Phase 1: mailbox_t refactor (signaler-based)

### Goal

- Reduce recv-side locking and wakeup overhead in inproc command path.

### Actions

1. Replaced mailbox_t condition_variable-based wakeup with signaler.
2. Restored _active flag to avoid redundant waits.
3. Removed recv-side _sync lock (single-reader assumption).
4. Restored valid()/forked() semantics based on signaler.
5. Simplified schedule_if_needed() (no sender-side cpipe check).

### Build

```
cmake --build build
```

### Bench Commands

```
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_pair zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_pubsub zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_dealer_dealer zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_dealer_router zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_router_router zlink inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_router_router_poll zlink inproc 64 | rg throughput

BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_pair libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_pubsub libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_dealer_dealer libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_dealer_router libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_router_router libzmq inproc 64 | rg throughput
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_std_zmq_router_router_poll libzmq inproc 64 | rg throughput
```

### Results (10K, 64B)

zlink:
- PAIR 4.79 M/s
- PUBSUB 4.28 M/s
- DEALER_DEALER 4.94 M/s
- DEALER_ROUTER 4.30 M/s
- ROUTER_ROUTER 3.89 M/s
- ROUTER_ROUTER_POLL 3.16 M/s

libzmq:
- PAIR 5.76 M/s
- PUBSUB 4.93 M/s
- DEALER_DEALER 5.68 M/s
- DEALER_ROUTER 4.99 M/s
- ROUTER_ROUTER 4.65 M/s
- ROUTER_ROUTER_POLL 3.83 M/s

### Status

- 일부 패턴 개선 확인.
- 여전히 libzmq 대비 10-20% 정도 갭 존재.
- 추가 프로파일링/벤치 반복 필요.

## Phase 2: ASIO dispatch 실험 (실패)

### Goal

- io_context queue 오버헤드를 줄이기 위해 mailbox schedule 경로에서 `dispatch()` 사용.

### Actions

1. `mailbox_t::schedule_if_needed()`와 `mailbox_safe_t::schedule_if_needed()`를 `post()` → `dispatch()`로 변경.
2. inproc 벤치 반복 실행.

### Result

- 여러 패턴에서 종료 시점에 `pipe.cpp:355` assertion 발생.
- 즉시 `post()`로 복구하여 안정성 확보.

### Verification

```
cmake --build build
BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_pair zlink inproc 64 | rg throughput
```

### Status

- dispatch 접근은 종료 시점 re-entrancy 문제로 보이며 적용 불가.
- 다음 단계는 profiling 및 signaler/schedule 빈도 계측으로 전환.

## Phase 3: Profiling 시도

### Goal

- syscall/CPU 오버헤드의 힌트를 얻기 위해 perf/strace 측정.

### Actions

1. `perf stat` 시도 → WSL 커널용 perf 미설치로 실패.
2. `strace -c`로 zlink/libzmq inproc PAIR 비교.

### Results (strace -c, 10K/64B)

zlink (PAIR/inproc): `write`, `getpid`, `poll` 중심으로 syscall 분포 확인.  
libzmq (PAIR/inproc): 유사한 syscall 분포이며 호출 횟수는 패턴에 따라 차이 존재.

### Notes

- strace는 성능을 크게 저하시켜 절대 throughput 수치는 의미 없음.
- perf 사용 가능 환경이 필요함 (WSL용 linux-tools 설치 필요).

## Phase 4: mailbox_t non-blocking recv 최적화

### Goal

- `recv(timeout=0)`에서 poll syscall을 피해서 inproc command 처리 비용 감소.

### Actions

1. `mailbox_t::recv()`에서 `timeout_ == 0`일 때 `_signaler.wait(0)` 대신 `_signaler.recv_failable()` 사용.

### Status

- 적용 완료.
- 단독으로는 개선폭이 제한적이라 추가 최적화 필요.

## Phase 5: mailbox_t recv upstream 동기화 실험 (Rollback)

### Goal

- libzmq와 동일한 `recv()` 흐름(Active → read 실패 시 wait)로 맞춰 성능 개선 기대.

### Actions

1. `_active` 경로에서 read 실패 시 blocking wait로 진입하도록 변경.

### Result

- PAIR만 소폭 개선, PUBSUB/DEALER 계열이 하락.
- 전반 평균 하락 → 즉시 롤백.

### Status

- 적용 불가 (rollback 완료).

## Phase 6: atomic_* 구현 우선순위 upstream 정렬

### Goal

- hot path(ypipe, refcount)에서 std::atomic 기반 구현을 사용해 오버헤드 감소.

### Actions

1. `atomic_ptr.hpp`에서 C++11 atomic 우선 사용.
2. `atomic_counter.hpp`에서 C++11 atomic 우선 사용.
3. `ZMQ_HAVE_ATOMIC_INTRINSICS`가 있을 때 `*_INTRINSIC` 매크로 누락 정의 보완.

### Bench (5-run avg, 10K/64B)

```
PAIR:         zlink 5,016,878.86  libzmq 6,154,682.63  (81.5%)
PUBSUB:       zlink 4,756,433.48  libzmq 5,571,718.62  (85.4%)
DEALER_DEALER:zlink 5,326,595.20  libzmq 5,994,799.15  (88.9%)
DEALER_ROUTER:zlink 4,841,675.28  libzmq 5,441,086.07  (89.0%)
```

### Status

- 평균 개선폭은 있으나 90% 미달.
- 추가 개선 필요.

## Phase 7: mailbox_t send lock 범위 축소

### Goal

- `_signaler.send()` syscall을 `_sync` 밖으로 이동해 lock hold 시간 감소.

### Actions

1. `mailbox_t::send()`에서 `_signaler.send()`를 unlock 이후 호출.
2. ZMQ_FD signaler 전파는 기존처럼 lock 내부에 유지.

### Bench (5-run avg, 10K/64B)

```
PAIR:         zlink 5,165,488.37  libzmq 5,950,211.54  (86.8%)
PUBSUB:       zlink 4,833,187.64  libzmq 5,561,339.85  (86.9%)
DEALER_DEALER:zlink 5,198,711.55  libzmq 5,964,467.77  (87.2%)
DEALER_ROUTER:zlink 4,852,288.22  libzmq 5,249,911.06  (92.4%)
```

### Status

- PAIR/PUBSUB 개선폭 상승, DEALER_ROUTER는 92% 도달.
- 여전히 평균 88%대, 목표(90%+) 미달.

## Phase 8: inproc inbound_poll_rate 확대 실험 (Rollback)

### Goal

- inproc hot path에서 command polling 빈도 감소로 throughput 개선.

### Actions

1. `socket_base_t::recv()`에서 inproc 전용 poll rate 적용 시도.
2. `inproc_inbound_poll_rate`(1000 → 500) 테스트 후 비교.

### Result (5-run avg, 10K/64B)

- PAIR/PUBSUB 성능 하락, DEALER 계열만 개선.
- 전체 평균 개선 실패 → rollback.

### Status

- 적용 불가 (rollback 완료).

## Phase 9: bench_common hot path 오버헤드 제거

### Goal

- zlink 벤치에서 `bench_send/bench_recv`가 매 호출마다 getenv를 수행하던 오버헤드 제거.

### Actions

1. `bench_debug_enabled()` 결과 캐시.
2. `bench_trace_limit()` 결과 캐시.

### Bench (5-run avg, 10K/64B)

```
PAIR:          zlink 5,836,211.01  libzmq 6,070,732.03  (96.1%)
PUBSUB:        zlink 5,549,127.66  libzmq 5,317,052.04  (104.4%)
DEALER_DEALER: zlink 5,939,377.25  libzmq 6,068,839.75  (97.9%)
DEALER_ROUTER: zlink 5,449,340.81  libzmq 5,400,817.77  (100.9%)
```

### Status

- 모든 패턴 90%+ 달성.
- 벤치 측정 오버헤드 제거 효과로 판단됨.

## Phase 10: ROUTER_ROUTER(_POLL) 벤치 정리 및 재측정

### Goal

- ROUTER_ROUTER_POLL 벤치의 불필요한 로그/로직을 줄여 공정 비교.

### Actions

1. `bench_zlink_router_router_poll.cpp`의 DEBUG 로그를 `BENCH_DEBUG`로 게이트.
2. receiver loop 로직을 libzmq 벤치와 동일한 형태로 단순화.

### Bench (5-run avg, 10K/64B)

```
ROUTER_ROUTER:       zlink 4,246,604.76  libzmq 4,829,863.70  (87.9%)
ROUTER_ROUTER_POLL:  zlink 3,193,955.91  libzmq 3,965,996.57  (80.5%)
```

### Status

- ROUTER_ROUTER_POLL는 여전히 80% 수준으로 큰 갭 유지.
- poll 기반 경로에서 ASIO 오버헤드가 영향 주는 것으로 추정.

## Phase 11: ZMQ_FD 경로 정리 (non-thread-safe)

### Goal

- `zmq_poll`에서 불필요한 추가 signaler를 제거해 poll 기반 성능 개선.

### Actions

1. `mailbox_t::get_fd()` 추가 (내부 signaler FD 사용).
2. non-thread-safe socket은 mailbox signaler FD를 직접 반환.
3. thread-safe socket만 전용 signaler 생성 유지.

### Bench (5-run avg, 10K/64B)

```
ROUTER_ROUTER:       zlink 4,393,858.47  libzmq 4,693,652.88  (93.6%)
ROUTER_ROUTER_POLL:  zlink 3,904,006.62  libzmq 3,829,510.39  (101.9%)
```

### Status

- ROUTER_ROUTER_POLL 갭 해소.
- 전체 패턴 90%+ 달성 가능 구간으로 진입.

## Phase 12: 전체 패턴 재측정 (ROUTER 포함)

### Goal

- ROUTER_ROUTER / ROUTER_ROUTER_POLL을 포함한 전체 패턴 성능 확인.

### Bench (5-run avg, 10K/64B)

```
PAIR:               zlink 5,743,061.05  libzmq 6,021,881.02  (95.4%)
PUBSUB:             zlink 5,531,086.27  libzmq 5,442,737.77  (101.6%)
DEALER_DEALER:      zlink 5,593,515.76  libzmq 5,885,775.34  (95.0%)
DEALER_ROUTER:      zlink 5,373,644.85  libzmq 5,154,279.43  (104.3%)
ROUTER_ROUTER:      zlink 4,162,595.80  libzmq 4,804,415.29  (86.6%)
ROUTER_ROUTER_POLL: zlink 3,874,342.56  libzmq 3,989,887.52  (97.1%)
```

### Status

- ROUTER_ROUTER만 90% 미달 (86.6%).
- 나머지 패턴은 목표 달성.

## Phase 13: ROUTER 벤치 fast-path 적용

### Goal

- ROUTER 벤치에서 `bench_send/bench_recv` 래퍼 오버헤드를 제거해
  패턴 간 공정성 확보.

### Actions

1. `bench_zlink_router_router.cpp`와
   `bench_zlink_router_router_poll.cpp`에서
   `bench_send/bench_recv` → `bench_send_fast/bench_recv_fast`로 교체.
2. 전체 패턴 5회 평균 재측정 (10K/64B).

### Bench (5-run avg, 10K/64B)

```
PAIR:               zlink 5,892,733.63  libzmq 6,057,853.25  (97.3%)
PUBSUB:             zlink 5,397,095.95  libzmq 5,477,690.58  (98.5%)
DEALER_DEALER:      zlink 5,894,758.57  libzmq 6,014,657.65  (98.0%)
DEALER_ROUTER:      zlink 5,267,554.36  libzmq 5,214,712.16  (101.0%)
ROUTER_ROUTER:      zlink 4,387,095.92  libzmq 4,941,333.94  (88.8%)
ROUTER_ROUTER_POLL: zlink 4,076,634.06  libzmq 4,147,785.16  (98.3%)
```

### Status

- ROUTER_ROUTER_POLL 개선 (98.3%).
- ROUTER_ROUTER는 여전히 90% 미달 (88.8%).
- 전체 평균 달성률 ~97.0%.

## Phase 14: ROUTER out_pipe 캐시 (routing ID 재사용)

### Goal

- 동일 peer로 반복 전송 시 ROUTER routing ID lookup 비용 감소.

### Actions

1. `router_t`에 마지막 routing ID → pipe 캐시 추가.
2. pipe 종료/핸드오버 시 캐시 무효화.
3. 전체 패턴 5회 평균 재측정 (10K/64B).

### Bench (5-run avg, 10K/64B)

```
PAIR:               zlink 5,790,634.56  libzmq 6,008,575.87  (96.4%)
PUBSUB:             zlink 5,594,298.15  libzmq 5,545,821.73  (100.9%)
DEALER_DEALER:      zlink 5,952,167.50  libzmq 6,125,861.35  (97.2%)
DEALER_ROUTER:      zlink 4,877,115.98  libzmq 5,359,150.74  (91.0%)
ROUTER_ROUTER:      zlink 4,465,222.36  libzmq 4,706,922.89  (94.9%)
ROUTER_ROUTER_POLL: zlink 4,120,716.69  libzmq 4,178,150.68  (98.6%)
```

### Status

- ROUTER_ROUTER 90%+ 달성 (94.9%).
- DEALER_ROUTER는 91.0%로 하락했으나 목표(90%+) 유지.
- 전체 평균 달성률 ~96.5%.

## Phase 15: 안정성 확인 (10-run avg)

### Goal

- ROUTER/DEALER 변동성이 큰 구간의 평균 안정성 확인.

### Actions

1. 전체 패턴 10회 평균 재측정 (10K/64B).

### Bench (10-run avg, 10K/64B)

```
PAIR:               zlink 5,884,582.47  libzmq 5,954,414.03  (98.8%)
PUBSUB:             zlink 5,614,573.76  libzmq 5,600,259.82  (100.3%)
DEALER_DEALER:      zlink 5,877,958.71  libzmq 5,985,187.05  (98.2%)
DEALER_ROUTER:      zlink 4,838,598.20  libzmq 5,224,919.06  (92.6%)
ROUTER_ROUTER:      zlink 4,403,566.40  libzmq 4,833,059.60  (91.1%)
ROUTER_ROUTER_POLL: zlink 4,267,825.10  libzmq 3,988,249.72  (107.0%)
```

### Status

- 전 패턴 90%+ 유지.
- ROUTER_ROUTER/DEALER_ROUTER는 5회 평균보다 낮지만 목표 유지.

## Phase 16: ROUTER 회귀 테스트

### Goal

- ROUTER 관련 변경에 대한 기본 회귀 확인.

### Actions

1. `ctest -R router --output-on-failure` 실행.

### Result

- `test_router_mandatory`, `test_probe_router`, `test_spec_router`,
  `test_router_handover`, `test_router_multiple_dealers`,
  `test_router_mandatory_hwm` 모두 Pass.

## Phase 17: zlink 벤치 fast-path 확대

### Goal

- zlink 벤치에서 `bench_send/bench_recv` 래퍼 오버헤드를 전 패턴에서 제거.

### Actions

1. `bench_zlink_pair/pubsub/dealer_dealer/dealer_router.cpp`를
   `bench_send_fast/bench_recv_fast`로 변경.
2. 전체 패턴 10회 평균 재측정.
3. ROUTER_ROUTER/ROUTER_ROUTER_POLL는 100K 메시지 5회 평균으로 재확인.

### Bench (10-run avg, 10K/64B)

```
PAIR:               zlink 5,890,899.08  libzmq 6,216,668.48  (94.8%)
PUBSUB:             zlink 5,509,742.61  libzmq 5,487,252.37  (100.4%)
DEALER_DEALER:      zlink 5,781,372.59  libzmq 5,838,389.14  (99.0%)
DEALER_ROUTER:      zlink 4,921,895.34  libzmq 5,277,637.31  (93.3%)
ROUTER_ROUTER:      zlink 4,405,115.49  libzmq 4,938,882.30  (89.2%)
ROUTER_ROUTER_POLL: zlink 4,155,956.73  libzmq 3,957,240.12  (105.0%)
```

### Bench (5-run avg, 100K/64B, ROUTER only)

```
ROUTER_ROUTER:      zlink 4,212,846.29  libzmq 4,753,224.03  (88.6%)
ROUTER_ROUTER_POLL: zlink 4,118,079.69  libzmq 3,818,331.03  (107.9%)
```

### Status

- ROUTER_ROUTER가 90% 미달로 하락.
- 추가 최적화 필요.

## Phase 18: pipe_t check_write 중복 HWM 체크 제거

### Goal

- ROUTER mandatory 경로에서 `check_hwm()` 중복 호출 제거.

### Actions

1. `pipe_t::check_write(bool *pipe_full_)` 오버로드 추가.
2. `router_t::xsend`에서 `check_write` 결과로 HWM 여부를 바로 판단.
3. 전체 패턴 10회 평균 재측정.
4. `ctest -R router --output-on-failure` 재확인.

### Bench (10-run avg, 10K/64B)

```
PAIR:               zlink 5,952,530.20  libzmq 6,048,563.74  (98.4%)
PUBSUB:             zlink 5,635,905.44  libzmq 5,420,809.76  (104.0%)
DEALER_DEALER:      zlink 5,845,678.60  libzmq 6,076,209.88  (96.2%)
DEALER_ROUTER:      zlink 4,917,647.92  libzmq 5,248,656.85  (93.7%)
ROUTER_ROUTER:      zlink 4,486,274.01  libzmq 4,854,518.26  (92.4%)
ROUTER_ROUTER_POLL: zlink 4,122,020.39  libzmq 4,074,294.90  (101.2%)
```

### Status

- ROUTER_ROUTER 90%+ 회복 (92.4%).
- 전체 평균 달성률 ~97.6%.
- ROUTER 관련 테스트 Pass.

## Phase 19: ROUTER 고부하 재확인 (100K)

### Goal

- ROUTER 패턴의 개선이 고부하에서도 유지되는지 확인.

### Actions

1. ROUTER/ROUTER_POLL 100K 메시지 5회 평균 측정.

### Bench (5-run avg, 100K/64B, ROUTER only)

```
ROUTER_ROUTER:      zlink 4,418,649.26  libzmq 4,831,216.89  (91.5%)
ROUTER_ROUTER_POLL: zlink 4,274,233.54  libzmq 3,956,391.83  (108.0%)
```

### Status

- ROUTER_ROUTER 90%+ 유지.

## Phase 20: 메시지 사이즈별 성능 스윕

### Goal

- 메시지 크기별 성능 편차 확인.

### Actions

1. size: 64/256/1024/65536/131072/262144
2. msg_count: size<=1024 → 10,000, size>1024 → 2,000
3. 3회 평균으로 측정.

### Bench (3-run avg, inproc)

```
size=64, msg_count=10000
PAIR:               zlink 6,068,535.30  libzmq 6,281,406.82  (96.61%)
PUBSUB:             zlink 5,507,097.17  libzmq 5,646,600.53  (97.53%)
DEALER_DEALER:      zlink 5,766,416.21  libzmq 5,948,349.23  (96.94%)
DEALER_ROUTER:      zlink 4,856,115.70  libzmq 5,225,760.57  (92.93%)
ROUTER_ROUTER:      zlink 4,336,193.56  libzmq 4,792,639.83  (90.48%)
ROUTER_ROUTER_POLL: zlink 4,180,083.23  libzmq 4,049,526.05  (103.22%)

size=256, msg_count=10000
PAIR:               zlink 5,019,088.58  libzmq 5,023,539.01  (99.91%)
PUBSUB:             zlink 4,184,846.19  libzmq 4,404,453.49  (95.01%)
DEALER_DEALER:      zlink 4,774,110.10  libzmq 4,976,054.94  (95.94%)
DEALER_ROUTER:      zlink 3,686,812.98  libzmq 3,808,567.01  (96.80%)
ROUTER_ROUTER:      zlink 3,114,331.95  libzmq 3,169,293.84  (98.27%)
ROUTER_ROUTER_POLL: zlink 3,162,276.43  libzmq 3,173,354.92  (99.65%)

size=1024, msg_count=10000
PAIR:               zlink 2,609,926.71  libzmq 2,313,580.80  (112.81%)
PUBSUB:             zlink 2,335,483.01  libzmq 2,348,675.84  (99.44%)
DEALER_DEALER:      zlink 2,406,319.66  libzmq 2,514,377.83  (95.70%)
DEALER_ROUTER:      zlink 1,899,698.14  libzmq 2,481,487.26  (76.55%)
ROUTER_ROUTER:      zlink 1,816,322.58  libzmq 1,782,914.50  (101.87%)
ROUTER_ROUTER_POLL: zlink 1,634,743.27  libzmq 1,746,273.12  (93.61%)

size=65536, msg_count=2000
PAIR:               zlink 217,016.01  libzmq 139,470.66  (155.60%)
PUBSUB:             zlink 138,697.30  libzmq 131,754.36  (105.27%)
DEALER_DEALER:      zlink 154,313.08  libzmq 214,685.55  (71.88%)
DEALER_ROUTER:      zlink 150,040.25  libzmq 153,148.83  (97.97%)
ROUTER_ROUTER:      zlink 149,258.78  libzmq 156,001.92  (95.68%)
ROUTER_ROUTER_POLL: zlink 170,538.15  libzmq 158,444.07  (107.63%)

size=131072, msg_count=2000
PAIR:               zlink 160,513.25  libzmq 81,637.95  (196.62%)
PUBSUB:             zlink 84,362.75  libzmq 103,239.55  (81.72%)
DEALER_DEALER:      zlink 86,035.16  libzmq 107,702.73  (79.88%)
DEALER_ROUTER:      zlink 80,294.10  libzmq 99,046.41  (81.07%)
ROUTER_ROUTER:      zlink 83,330.60  libzmq 96,362.62  (86.48%)
ROUTER_ROUTER_POLL: zlink 110,291.55  libzmq 90,955.81  (121.26%)

size=262144, msg_count=2000
PAIR:               zlink 49,816.90  libzmq 51,127.63  (97.44%)
PUBSUB:             zlink 50,688.17  libzmq 69,724.01  (72.70%)
DEALER_DEALER:      zlink 52,568.21  libzmq 50,314.74  (104.48%)
DEALER_ROUTER:      zlink 49,915.45  libzmq 52,865.35  (94.42%)
ROUTER_ROUTER:      zlink 49,165.08  libzmq 51,833.04  (94.85%)
ROUTER_ROUTER_POLL: zlink 46,518.39  libzmq 57,528.99  (80.86%)
```

### Status

- size별 평균은 90%+ 유지하나 개별 패턴 편차 큼.
- 1024/65536/131072/262144 구간에서 특정 패턴 하락 확인.

## Phase 21: 저하 패턴 재확인 (5-run)

### Goal

- 90% 미만 구간 재측정으로 편차 여부 확인.

### Actions

1. 저하 패턴만 5회 평균 재측정.

### Bench (5-run avg, inproc)

```
size=1024, msg_count=10000
DEALER_ROUTER:      zlink 1,984,321.10  libzmq 2,605,062.02  (76.17%)

size=65536, msg_count=2000
DEALER_DEALER:      zlink 145,120.97  libzmq 210,117.81  (69.07%)

size=131072, msg_count=2000
PUBSUB:             zlink 86,633.44  libzmq 90,878.64  (95.33%)
DEALER_DEALER:      zlink 88,064.03  libzmq 145,003.07  (60.73%)
DEALER_ROUTER:      zlink 82,906.64  libzmq 84,063.21  (98.62%)
ROUTER_ROUTER:      zlink 97,507.01  libzmq 117,833.94  (82.75%)

size=262144, msg_count=2000
PUBSUB:             zlink 50,320.98  libzmq 69,616.75  (72.28%)
ROUTER_ROUTER_POLL: zlink 53,082.59  libzmq 55,111.86  (96.32%)
```

### Status

- DEALER_DEALER(64K/128K), DEALER_ROUTER(1K), PUBSUB(256K),
  ROUTER_ROUTER(128K) 저하 구간 재확인.
