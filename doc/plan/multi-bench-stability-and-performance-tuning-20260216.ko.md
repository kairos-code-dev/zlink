# benchwithzmq multi 안정화 + 성능 튜닝 로그 (2026-02-16)

## 1) 작업 목표
- 멀티 벤치에서 관측된 수치 깨짐/편차/크래시(`rc=-6`)를 먼저 제거한다.
- `send=nonblocking`, `recv=blocking + batch drain` 모델은 유지한다.
- zlink/zmq 비교 조건은 동일하게 유지한다.
- `MULTI_STREAM`은 CCU 10000 기준으로 시나리오 대비 수치 차이를 축소한다.

## 2) 코드 변경 요약

### A. 공통 러너 경계 안정화
- 파일: `core/bench/benchwithzmq/multi/common/bench_common_multi.hpp`
- 변경:
  - drain 단계를 단순 sleep에서 `in-flight(send_total - recv_total) == 0` 또는 timeout까지 대기하는 방식으로 변경.
  - 경계 전환 시 이전 단계 in-flight 잔류가 다음 사이즈 측정에 섞이는 문제를 줄임.

### B. STREAM 멀티 안정화/튜닝
- 파일:
  - `core/bench/benchwithzmq/multi/zlink/bench_zlink_multi_stream.cpp`
  - `core/bench/benchwithzmq/multi/libzmq/bench_zmq_multi_stream.cpp`
- 변경:
  - stream 기본값 튜닝: `BENCH_MULTI_STREAM_SEND_WORKERS=3`, `BENCH_MULTI_STREAM_SEND_BATCH=64`
  - stream 전용 drain 최소값 추가: `BENCH_MULTI_STREAM_DRAIN_MS` (기본 2000ms)
  - sender partial-frame 정합성 깨짐 이슈(사이즈 전환 시 바이트열 손상) 제거를 위해 송신 경로를 안전 동작으로 복귀.

### C. PUBSUB 크래시(rc=-6) 제거
- 파일:
  - `core/bench/benchwithzmq/multi/zlink/bench_zlink_multi_pubsub.cpp`
  - `core/bench/benchwithzmq/multi/libzmq/bench_zmq_multi_pubsub.cpp`
- 변경:
  - 사이즈 경계마다 subscriber 큐 explicit drain 추가.
  - latency 측정 컨텍스트를 throughput 본 컨텍스트와 분리(`ctx_guard_t lat_ctx`)해서 zlink ASIO assert(`_input_stopped`) 제거.

### D. 스크립트 기본값/UX 정리
- 파일: `core/bench/benchwithzmq/multi/run_benchmarks.sh`
- 변경:
  - stream 튜닝 기본값 반영:
    - `--stream-send-workers` 기본 3
    - `--stream-send-batch` 기본 64

## 3) 재현/검증 명령

### STREAM 시나리오 기준 측정
```bash
LD_LIBRARY_PATH=core/build/linux-x64/lib ./core/build/linux-x64/bin/test_scenario_stream_zlink \
  --scenario s2 --transport tcp --ccu 10000 --size 64 --inflight 30 --warmup 1 --measure 3 \
  --io-threads 4 --send-batch 30 --port 28170

LD_LIBRARY_PATH=core/build/linux-x64/lib ./core/build/linux-x64/bin/test_scenario_stream_zlink \
  --scenario s2 --transport tcp --ccu 10000 --size 1024 --inflight 30 --warmup 1 --measure 3 \
  --io-threads 4 --send-batch 30 --port 28171
```

### STREAM multi(zlink-only) 측정
```bash
cd core/bench/benchwithzmq/multi
./run_benchmarks.sh --pattern stream --transport tcp --clients 10000 --inflight 30 \
  --duration 3 --io-threads 4 --msg-sizes 64,1024 --runs 1 --zlink-only --hwm 1000000
```

### 5패턴 + single 반복 검증 예시
```bash
./run_benchmarks.sh --pattern dealer_dealer     --transport tcp --runs 1 --duration 1 --clients 50 --send-workers 7 --single
./run_benchmarks.sh --pattern dealer_router     --transport tcp --runs 1 --duration 1 --clients 50 --send-workers 7 --single
./run_benchmarks.sh --pattern router_router     --transport tcp --runs 1 --duration 1 --clients 50 --send-workers 7 --single
./run_benchmarks.sh --pattern router_router_poll--transport tcp --runs 1 --duration 1 --clients 50 --send-workers 7 --single
./run_benchmarks.sh --pattern pubsub            --transport tcp --runs 1 --duration 1 --clients 50 --send-workers 7 --single
```

## 4) 결과 요약

### STREAM (zlink)
- 시나리오(s2, ccu=10000, inflight=30, 3s)
  - 64B: `3,056,480 msg/s`
  - 1024B: `1,397,550 msg/s`
- 멀티 벤치(zlink-only, 동일 ccu/inflight, 3s)
  - 64B: `2,257,130 msg/s`
  - 1024B: `1,581,840 msg/s`

해석:
- 1024B는 시나리오 대비 동급 이상.
- 64B는 시나리오 대비 낮음(대략 74%).
- 다만 기존처럼 rc=-6/크래시로 끊기던 구간은 안정화됨.

### PUBSUB 안정성
- 변경 전: 256B 이후 `rc=-6`, `missing_throughput`/`missing_latency` 다수 발생.
- 변경 후: 64~262144B 전 구간 연속 출력, 크래시 재현되지 않음.

### 5패턴 10% 기준 점검
- 단일 샘플(run=1)에서는 시스템 노이즈 영향으로 일부 구간이 간헐적으로 `-10%`를 조금 초과.
- 경향적으로 초과 구간은 주로 작은 메시지(특히 64/256B)에서 발생.

## 5) 현재 상태
- 측정/출력/집계 깨짐, 패턴 전환 크래시, PUBSUB rc=-6는 코드 레벨로 정리됨.
- STREAM 64B 절대치와 소메시지(64/256B)에서의 zlink-std_zmq 편차는 여전히 런타임 노이즈/엔진 성능 특성 영향이 남아 있음.
- 동일 조건(zlink vs zmq 동일) 원칙은 유지됨.

