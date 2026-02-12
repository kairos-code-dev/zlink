# Node/Python Binding 성능개선 기록

- 작성일: 2026-02-12
- 범위: `bindings/node`, `bindings/python`, `bindings/bench/common`
- 측정 조건: `tcp`, `64B`, `BENCH_MSG_COUNT=200000`, 단일 실행(`runs=1`)

## 1) 적용한 개선 내용

### Node
- 네이티브 배치/드레인 API 추가
  - `socketSendMany`, `socketRecvManyInto`
  - `socketSendRoutedMany`, `socketRecvPairManyInto`
  - `socketRecvPairDrainInto` (POLL 경로 전용 드레인)
  - `gatewaySendManyConst` (고정 payload 다건 전송)
  - `spotPublishManyConst`, `spotRecvMany`
- 벤치 hot path에서 JS 루프 대신 native batch 호출 사용
  - `ROUTER_ROUTER_POLL`은 poll 후 `recvPairDrainInto`로 큐를 한 번에 drain
  - `GATEWAY`, `SPOT` throughput 구간도 native batch 호출로 전환

### Python
- 벤치 fastpath를 기본 활성화
  - `BENCH_PY_FASTPATH_CEXT=1` 기본
  - `BENCH_PY_FASTPATH_REQUIRE=1` 시 로드/빌드 실패 즉시 에러
- `_zlink_fastpath` C-extension 로딩/자동빌드 경로 운영화
  - import 실패 시 `setup_fastpath.py build_ext --inplace` 자동 시도
- C-extension fastpath API 확장
  - `send_many_const`, `recv_many_into`
  - `send_routed_many_const`, `recv_pair_many_into`
  - `recv_pair_drain_into` (POLL 경로 드레인)
  - `gateway_send_many_const`, `spot_publish_many_const`, `spot_recv_many`
- `ROUTER_ROUTER_POLL`에서 poll 후 cext drain(또는 raw DONTWAIT drain fallback)
  - `GATEWAY`는 `gateway_send_many_const + recv_pair_many_into` 조합 사용
  - `SPOT`은 `spot_publish_many_const + spot_recv_many` 조합 사용

### 공통 벤치 러너
- Python binding 벤치 실행 시 fastpath 자동 빌드 시도
- Python 벤치 로그에 fastpath mode(`on/off`) 출력

## 2) 측정 결과 (Throughput)

단위: Kmsg/s

| Pattern | Node | Python (fastpath off) | Python (fastpath on) |
|---|---:|---:|---:|
| PAIR | 4737.75 | 1158.01 | 6616.66 |
| PUBSUB | 4938.34 | 1105.24 | 6076.77 |
| DEALER_DEALER | 4697.84 | 1150.75 | 6474.04 |
| DEALER_ROUTER | 4146.90 | 778.05 | 5636.81 |
| ROUTER_ROUTER | 3883.17 | 567.90 | 4882.99 |
| ROUTER_ROUTER_POLL | 3797.81 | 560.16 | 5158.80 |
| STREAM | 2432.78 | 492.58 | 5257.34 |
| GATEWAY | 4765.89 | 89.03 | 4740.14 |
| SPOT | 2106.73 | 95.95 | 1895.67 |

## 3) Python fastpath on/off 개선율

기준: `Python fastpath off`

| Pattern | off (Kmsg/s) | on (Kmsg/s) | 개선율 |
|---|---:|---:|---:|
| PAIR | 1158.01 | 6616.66 | +471.4% |
| PUBSUB | 1105.24 | 6076.77 | +449.8% |
| DEALER_DEALER | 1150.75 | 6474.04 | +462.6% |
| DEALER_ROUTER | 778.05 | 5636.81 | +624.5% |
| ROUTER_ROUTER | 567.90 | 4882.99 | +759.8% |
| ROUTER_ROUTER_POLL | 560.16 | 5158.80 | +821.0% |
| STREAM | 492.58 | 5257.34 | +967.3% |
| GATEWAY | 89.03 | 4740.14 | +5224.2% |
| SPOT | 95.95 | 1895.67 | +1875.7% |

## 4) ROUTER_ROUTER_POLL 전후 핵심 수치

세션 내 패치 전/후 동일 조건 측정 스냅샷 (tcp/64B/200000)

| Case | Before (Kmsg/s) | After (Kmsg/s) | 개선율 |
|---|---:|---:|---:|
| Node ROUTER_ROUTER_POLL | 913.51 | 3797.81 | +315.7% |
| Python ROUTER_ROUTER_POLL (fastpath on) | 307.31 | 5158.80 | +1578.7% |
| Python ROUTER_ROUTER_POLL (fastpath off) | 307.31 | 560.16 | +82.3% |

## 5) 재현 명령

```bash
# Node
BENCH_MSG_COUNT=200000 node bindings/node/benchwithzlink/pattern_router_router_poll.js tcp 64

# Python fastpath on (기본)
BENCH_MSG_COUNT=200000 python3 bindings/python/benchwithzlink/pattern_router_router_poll.py tcp 64

# Python fastpath off
BENCH_MSG_COUNT=200000 BENCH_PY_FASTPATH_CEXT=0 \
  python3 bindings/python/benchwithzlink/pattern_router_router_poll.py tcp 64
```

Gateway/Spot 추가 측정:

```bash
# Node
BENCH_MSG_COUNT=200000 node bindings/node/benchwithzlink/pattern_gateway.js tcp 64
BENCH_MSG_COUNT=200000 node bindings/node/benchwithzlink/pattern_spot.js tcp 64

# Python fastpath on (기본)
BENCH_MSG_COUNT=200000 python3 bindings/python/benchwithzlink/pattern_gateway.py tcp 64
BENCH_MSG_COUNT=200000 python3 bindings/python/benchwithzlink/pattern_spot.py tcp 64

# Python fastpath off
BENCH_MSG_COUNT=200000 BENCH_PY_FASTPATH_CEXT=0 \
  python3 bindings/python/benchwithzlink/pattern_gateway.py tcp 64
BENCH_MSG_COUNT=200000 BENCH_PY_FASTPATH_CEXT=0 \
  python3 bindings/python/benchwithzlink/pattern_spot.py tcp 64
```

## 6) 해석 노트

- 이번 fastpath 개선은 메시지 송수신 hot path(PAIR/PUBSUB/DEALER/ROUTER/STREAM)에 집중되어 있습니다.
- `GATEWAY`, `SPOT`도 2차 개선에서 native many-const fastpath를 추가해 throughput 구간의 오버헤드를 제거했습니다.
- 현재 측정에서는 `GATEWAY`, `SPOT`의 Python fastpath on 값이 off 대비 각각 `+5224.2%`, `+1875.7%`입니다.

## 7) Gateway/Spot 2차 개선 전후 비교 (2026-02-12)

단위: Kmsg/s, 조건: `tcp`, `64B`, `BENCH_MSG_COUNT=200000`, 단일 실행(`runs=1`)

| Case | Before | After | 개선율 |
|---|---:|---:|---:|
| Node GATEWAY | 1172.84 | 4765.89 | +306.4% |
| Node SPOT | 612.81 | 2106.73 | +243.8% |
| Python GATEWAY (fastpath on) | 57.90 | 4740.14 | +8086.8% |
| Python SPOT (fastpath on) | 71.56 | 1895.67 | +2549.1% |
| Python GATEWAY (fastpath off) | 64.65 | 89.03 | +37.7% |
| Python SPOT (fastpath off) | 90.31 | 95.95 | +6.2% |

원본 측정값:

- Node GATEWAY (after): throughput `4765891.0894`, latency(us) `10251.333`
- Node SPOT (after): throughput `2106725.7134`, latency(us) `10246.68983`
- Python GATEWAY fastpath on (after): throughput `4740142.9910`, latency(us) `64.86963`
- Python SPOT fastpath on (after): throughput `1895674.2947`, latency(us) `1156.8572`
- Python GATEWAY fastpath off (after): throughput `89027.5724`, latency(us) `50.05065`
- Python SPOT fastpath off (after): throughput `95948.5517`, latency(us) `1138.53288`

## 8) 검증

- Node: `npm test` (`bindings/node`) 통과
- Python: `PYTHONPATH=bindings/python/src python3 -m pytest -q bindings/python/tests` 통과
