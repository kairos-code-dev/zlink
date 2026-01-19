# Benchmarks Plan and Results

**Date:** 2026-01-21  
**Owner:** 팀장님

---

## 1. Commands

Baseline (ZMTP):
- `ZLINK_PROTOCOL=zmtp ./benchwithzmq/run_benchmarks.sh --runs 10 --reuse-build`

Stage A (ZMTP Lite):
- `ZLINK_PROTOCOL=zmtp ./benchwithzmq/run_benchmarks.sh --runs 10 --reuse-build`

Stage B (ZMP):
- `ZLINK_PROTOCOL=zmp ./benchwithzmq/run_benchmarks.sh --runs 10 --reuse-build`

Focused runs:
- `BENCH_TRANSPORTS=tcp,inproc,ipc BENCH_MSG_SIZES=64,256,1024`
Pattern-only (avoids long all-pattern timeouts):
- `./benchwithzmq/run_benchmarks.sh --pattern PAIR --runs 10 --reuse-build --output /tmp/bench_zmtp_lite_pair.txt`
- `./benchwithzmq/run_benchmarks.sh --pattern PUBSUB --runs 10 --reuse-build --output /tmp/bench_zmtp_lite_pubsub.txt`
- `./benchwithzmq/run_benchmarks.sh --pattern DEALER_DEALER --runs 10 --reuse-build --output /tmp/bench_zmtp_lite_dealer_dealer.txt`
- `./benchwithzmq/run_benchmarks.sh --pattern DEALER_ROUTER --runs 10 --reuse-build --output /tmp/bench_zmtp_lite_dealer_router.txt`
- `./benchwithzmq/run_benchmarks.sh --pattern ROUTER_ROUTER --runs 10 --reuse-build --output /tmp/bench_zmtp_lite_router_router.txt`
- `./benchwithzmq/run_benchmarks.sh --pattern ROUTER_ROUTER_POLL --runs 10 --reuse-build --output /tmp/bench_zmtp_lite_router_router_poll.txt`

---

## 2. Metrics

- Throughput (Mmsg/s for small, MB/s for large)
- Latency (us)
- Diff (%) vs libzmq baseline

---

## 3. Notes

- IPC is CPU-bound and often reveals protocol overhead.
- Negative or zero metrics indicate measurement anomalies and should be re-run.

---

## 4. Results

- Baseline (ZMTP, runs=10) outputs:
  - `/tmp/bench_zmtp_baseline_pubsub.txt`
  - `/tmp/bench_zmtp_baseline_dealer_dealer.txt`
  - `/tmp/bench_zmtp_baseline_dealer_router.txt`
  - `/tmp/bench_zmtp_baseline_router_router.txt`
  - `/tmp/bench_zmtp_baseline_router_router_poll.txt`
- Combined all-pattern run timed out; partial output in
  `/tmp/bench_zmtp_baseline.txt` (PAIR completed).
- Stage A (ZMTP Lite, runs=10) outputs:
  - `/tmp/bench_zmtp_lite_pair.txt`
  - `/tmp/bench_zmtp_lite_pubsub.txt`
  - `/tmp/bench_zmtp_lite_dealer_dealer.txt`
  - `/tmp/bench_zmtp_lite_dealer_router.txt`
  - `/tmp/bench_zmtp_lite_router_router.txt`
  - `/tmp/bench_zmtp_lite_router_router_poll.txt`
- Stage B (ZMP, runs=10) outputs:
  - `/tmp/bench_zmp_pair.txt`
  - `/tmp/bench_zmp_pubsub.txt`
  - `/tmp/bench_zmp_dealer_dealer.txt`
  - `/tmp/bench_zmp_dealer_router.txt`
  - `/tmp/bench_zmp_router_router.txt`
  - `/tmp/bench_zmp_router_router_poll.txt`
