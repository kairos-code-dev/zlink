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
- 벤치 hot path에서 JS 루프 대신 native batch 호출 사용
  - `ROUTER_ROUTER_POLL`은 poll 후 `recvPairDrainInto`로 큐를 한 번에 drain

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
- `ROUTER_ROUTER_POLL`에서 poll 후 cext drain(또는 raw DONTWAIT drain fallback)

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
| GATEWAY | 1172.84 | 64.65 | 57.90 |
| SPOT | 612.81 | 90.31 | 71.56 |

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
| GATEWAY | 64.65 | 57.90 | -10.4% |
| SPOT | 90.31 | 71.56 | -20.8% |

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
- `GATEWAY`, `SPOT`은 Discovery/Registry/Service 경로와 고수준 객체 오버헤드 비중이 커서 현재 fastpath 영향이 작습니다.
- 현재 측정에서는 `GATEWAY`, `SPOT`의 Python fastpath on 값이 off 대비 각각 `-10.4%`, `-20.8%`였습니다.

## 7) 검증

- Node: `npm test` (`bindings/node`) 통과
- Python: `PYTHONPATH=bindings/python/src python3 -m pytest -q bindings/python/tests` 통과
