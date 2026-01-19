# ASIO zero-copy async write (WIP)

## 변경 내용 요약
- ASIO 엔진의 async write 경로에서 사용자 공간 복사를 제거하고, `_outpos/_outsize`를 그대로 async_write_some에 전달하도록 변경.
- async write 완료 시 `_outpos/_outsize`를 진행시키고, 남은 바이트가 있으면 동일 버퍼로 재전송.
- encoder 문서 주석을 수정해 "async에서도 encode/load_msg 호출이 없으면 zero-copy 가능"함을 명시.

## 핵심 의도
- 큰 메시지(>=8KB 포함)에서 추가 memcpy를 제거해 libzmq 대비 성능 격차(특히 TCP 대용량)를 줄이는 목적.

## 벤치마크 결과 (PAIR, TCP)
- 실행 로그: `docs/team/20260118_feature-asio-zero-copy-write/01_pair_tcp_1k_8k_runs10.txt`
- 설정: `BENCH_TRANSPORTS=tcp BENCH_MSG_COUNT=1000 --runs 10 --msg-sizes 1024,8192`

요약:
- 1024B throughput: +44.31% (latency -1.71%)
- 8192B throughput: +21.69% (latency -1.91%)

## 참고
- `test_pair_tcp` 통과 확인.

## 추가 벤치마크 결과 (TCP)
- PUBSUB: `docs/team/20260118_feature-asio-zero-copy-write/02_pubsub_tcp_1k_8k_runs10.txt`
  - 1024B throughput: +28.70% (latency +22.36%)
  - 8192B throughput: +25.56% (latency +20.31%)
- DEALER_ROUTER: `docs/team/20260118_feature-asio-zero-copy-write/03_dealer_router_tcp_1k_8k_runs10.txt`
  - 1024B throughput: +37.55% (latency +0.29%)
  - 8192B throughput: +19.11% (latency -4.05%)

## 64K/128K/256K 벤치마크 (TCP)
- PAIR: `docs/team/20260118_feature-asio-zero-copy-write/04_pair_tcp_64k_128k_256k_runs10.txt`
  - 64K throughput +45.48% (latency -22.79%)
  - 128K throughput +27.02% (latency -61.14%)
  - 256K throughput +12.03% (latency -97.92%)
- PUBSUB: `docs/team/20260118_feature-asio-zero-copy-write/05_pubsub_tcp_64k_128k_256k_runs10.txt`
  - 64K throughput +40.52% (latency +29.98%)
  - 128K throughput +35.66% (latency +26.30%)
  - 256K throughput +19.82% (latency +16.54%)
- DEALER_ROUTER: `docs/team/20260118_feature-asio-zero-copy-write/06_dealer_router_tcp_64k_128k_256k_runs10.txt`
  - 64K throughput +45.26% (latency -19.38%)
  - 128K throughput +34.69% (latency -51.31%)
  - 256K throughput +14.72% (latency -91.85%)

## 추가 64K/128K/256K (TCP)
- DEALER_DEALER: `docs/team/20260118_feature-asio-zero-copy-write/07_dealer_dealer_tcp_64k_128k_256k_runs10.txt`
  - 64K throughput +22.51% (latency -22.59%)
  - 128K throughput +26.41% (latency -66.32%)
  - 256K throughput +9.85% (latency -98.42%)
- ROUTER_ROUTER: `docs/team/20260118_feature-asio-zero-copy-write/08_router_router_tcp_64k_128k_256k_runs10.txt`
  - 64K throughput +36.03% (latency -47.39%)
  - 128K throughput +21.82% (latency -86.50%)
  - 256K throughput +8.52% (latency -78.90%)
- ROUTER_ROUTER_POLL: `docs/team/20260118_feature-asio-zero-copy-write/09_router_router_poll_tcp_64k_128k_256k_runs10.txt`
  - 64K throughput +33.12% (latency -38.11%)
  - 128K throughput +33.19% (latency -89.99%)
  - 256K throughput +11.85% (latency -71.15%)

## 1K/64K 전체 패턴 (TCP/inproc/ipc)
- PAIR: `docs/team/20260118_feature-asio-zero-copy-write/11_pair_tcp_1k_64k_runs10.txt`
  - TCP 1K +7.37% / 64K +8.83%
- PUBSUB: `docs/team/20260118_feature-asio-zero-copy-write/11_pubsub_tcp_1k_64k_runs10.txt`
  - TCP 1K +4.99% / 64K +14.64%
- DEALER_ROUTER: `docs/team/20260118_feature-asio-zero-copy-write/11_dealer_router_tcp_1k_64k_runs10.txt`
  - TCP 1K +5.79% / 64K +8.86%
- DEALER_DEALER: `docs/team/20260118_feature-asio-zero-copy-write/11_dealer_dealer_tcp_1k_64k_runs10.txt`
  - TCP 1K +4.86% / 64K +9.85%
- ROUTER_ROUTER: `docs/team/20260118_feature-asio-zero-copy-write/11_router_router_tcp_1k_64k_runs10.txt`
  - TCP 1K +9.08% / 64K +19.32%
- ROUTER_ROUTER_POLL: `docs/team/20260118_feature-asio-zero-copy-write/11_router_router_poll_tcp_1k_64k_runs10.txt`
  - TCP 1K +11.69% / 64K +11.93%

## recv 백프레셔 처리 변경 (읽기 중단)
- 변경: `_input_stopped` 시 추가 read를 예약하지 않고 현재 버퍼에 보관하도록 수정.
- 로그: `docs/team/20260118_feature-asio-zero-copy-write/12_pair_tcp_1k_64k_runs10_stop_read.txt`
- PAIR, TCP (1K/64K):
  - 1K throughput -18.30% (latency -3.77%)
  - 64K throughput -11.17% (latency -19.61%)
→ 성능 저하 확인으로 인해 해당 변경은 되돌리고 기존 read-지속+buffer 방식 유지.

## read-지속 방식 복귀 후 재측정
- 로그: `docs/team/20260118_feature-asio-zero-copy-write/13_pair_tcp_1k_64k_runs10_revert_stop_read.txt`
- PAIR, TCP (1K/64K):
  - 1K throughput -13.47% (latency -3.46%)
  - 64K throughput -17.33% (latency -23.13%)

## pending buffer 직접 디코딩 (중간 copy 제거)
- 변경: backpressure 큐에서 decoder 버퍼로 복사하지 않고 바로 decode 호출.
- 로그: `docs/team/20260118_feature-asio-zero-copy-write/14_pair_tcp_1k_64k_runs10_pending_direct_decode.txt`
- PAIR, TCP (1K/64K):
  - 1K throughput -16.40% (latency -3.97%)
  - 64K throughput -20.29% (latency -22.51%)
→ 성능 개선이 확인되지 않아 원래 복사 경로 유지.

## 기준 갱신 후 재측정 (libzmq baseline refresh)
- 로그: `docs/team/20260118_feature-asio-zero-copy-write/16_pair_tcp_1k_64k_runs10_refresh_baseline.txt`
- PAIR, TCP (1K/64K):
  - 1K throughput +21.92% (latency -4.76%)
  - 64K throughput +56.71% (latency -21.17%)

## backpressure read buffer pool (복사 1회 제거)
- 변경: `_input_stopped` 중 async_read는 풀 버퍼로 읽고 그대로 pending 큐에 보관 (추가 memcpy 제거).
- 로그: `docs/team/20260118_feature-asio-zero-copy-write/17_pair_tcp_1k_64k_runs10_backpressure_pool.txt`
- PAIR, TCP (1K/64K):
  - 1K throughput +29.86% (latency -3.46%)
  - 64K throughput +55.14% (latency -23.79%)

## 전체 패턴 재측정 (TCP, baseline refresh)
- 로그: `docs/team/20260118_feature-asio-zero-copy-write/18_all_patterns_tcp_1k_64k_runs10_backpressure_pool.txt`
- PAIR: 1K +47.88%, 64K +11.87% (lat -22.47%)
- PUBSUB: 1K +30.99%, 64K +65.17% (lat +39.44%)
- DEALER_DEALER: 1K +34.98%, 64K +43.75% (lat -17.52%)
- DEALER_ROUTER: 1K +27.95%, 64K +55.87% (lat -27.54%)
- ROUTER_ROUTER: 1K +37.51%, 64K +54.92% (lat -49.55%)
- ROUTER_ROUTER_POLL: 1K +17.05%, 64K +24.32% (lat -41.89%)

## 전체 패턴 재측정 (TCP/IPC/INPROC, baseline refresh)
- 로그: `docs/team/20260118_feature-asio-zero-copy-write/19_all_patterns_all_transports_1k_64k_runs10_backpressure_pool.txt`
- PAIR:
  - TCP: 1K +35.46%, 64K +32.56%
  - INPROC: 1K +134.57%, 64K +10.49%
  - IPC: 1K +33.08%, 64K +19.57%
- PUBSUB:
  - TCP: 1K +31.22%, 64K +44.90%
  - INPROC: 1K +124.30%, 64K -30.16%
  - IPC: 1K +25.00%, 64K +14.43%
- DEALER_DEALER:
  - TCP: 1K +24.82%, 64K +14.61%
  - INPROC: 1K +136.31%, 64K +48.87%
  - IPC: 1K +30.29%, 64K +16.27%
- DEALER_ROUTER:
  - TCP: 1K +30.98%, 64K +45.99%
  - INPROC: 1K +113.14%, 64K -13.02%
  - IPC: 1K +28.26%, 64K +41.16%
- ROUTER_ROUTER:
  - TCP: 1K +30.24%, 64K +73.51%
  - INPROC: 1K +96.79%, 64K +4.12%
  - IPC: 1K +27.01%, 64K +20.49%

## ROUTER_ROUTER_POLL 재측정 보완 (TCP/IPC/INPROC)
- 로그: `docs/team/20260118_feature-asio-zero-copy-write/20_router_router_poll_all_transports_1k_64k_runs10_backpressure_pool.txt`
- ROUTER_ROUTER_POLL:
  - TCP: 1K +32.55%, 64K +37.98%
  - INPROC: 1K +77.75%, 64K -16.56%
  - IPC: 1K +17.60%, 64K +15.22%
