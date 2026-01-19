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
