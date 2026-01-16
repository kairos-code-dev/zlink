# Phase 5 Test Results (IPC)

## PAIR (ipc, 64B)

### 2K 메시지 5회
- 성공률: 5/5
- Throughput 범위: 3.15 ~ 4.12 M/s

### 10K 메시지 3회
- 성공률: 3/3
- Throughput 범위: 4.83 ~ 4.93 M/s

### 200K 메시지 1회
- 성공률: 1/1
- Throughput: 5.05 M/s

## 패턴별 10K (ipc, 64B)
- PUBSUB: 4.83 M/s
- DEALER_DEALER: 4.37 M/s
- DEALER_ROUTER: 4.24 M/s
- ROUTER_ROUTER: 3.94 M/s
- ROUTER_ROUTER_POLL: 3.85 M/s

## 사용한 명령
```bash
# 2K 메시지 5회
for i in 1 2 3 4 5; do
  BENCH_MSG_COUNT=2000 timeout 10 ./build/bin/comp_zlink_pair zlink ipc 64
  done | grep throughput

# 10K 메시지 3회
for i in 1 2 3; do
  BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_pair zlink ipc 64
  done | grep throughput

# 200K 메시지 1회
BENCH_MSG_COUNT=200000 timeout 60 ./build/bin/comp_zlink_pair zlink ipc 64 | grep throughput

# 패턴별 10K 1회
for pattern in pubsub dealer_dealer dealer_router router_router router_router_poll; do
  BENCH_MSG_COUNT=10000 timeout 20 ./build/bin/comp_zlink_${pattern} zlink ipc 64
  done | grep throughput
```
