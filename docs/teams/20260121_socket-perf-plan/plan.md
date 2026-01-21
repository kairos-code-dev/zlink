# 소켓별 최적화 계획 (ASIO/proactor)

목표
- 패턴(소켓 타입)별로 다른 병목을 인정하고, 소켓별로 최적화 프로파일을 분리한다.
- 전반적(모든 패턴) 개선이 어려운 상황에서, 특정 패턴의 확실한 개선을 목표로 한다.

배경
- 전체 runs=10 결과에서 PUBSUB/ROUTER 계열은 개선 경향, PAIR/DEALER 계열은 회귀가 많았다.
- 하나의 공통 최적화로 모든 패턴을 동시에 개선하는 것은 사실상 상충 구조로 보인다.
- 따라서 소켓 타입별로 별도 튜닝/게이트를 둔다.

범위
- ASIO 경로: asio_engine, tcp/ipc transport, asio_poller
- 소켓 타입별 프로파일 분기(옵션/환경변수 기반)

비범위
- 프로토콜 의미 변경
- ASIO backend 교체
- API 변경

전략 개요
- options_.type(소켓 타입) 기준으로 튜닝 프로파일 선택
- 공통 기능은 유지하되, 소켓별로 아래를 개별 적용
  - speculative_write 경로 강도
  - out_batch_size / gather threshold
  - io_context loop 정책(poll/run_for)
  - tcp 옵션(TCP_NODELAY/QUICKACK/BUSY_POLL)
  - async_write_some vs async_write

근거 데이터 요약 (runs=10 기준)
- 전체 결과 요약: docs/teams/20260121_asio-perf-plan/results/
  bench_full_runs10_20260121_021647_summary.md
- ROUTER_ROUTER 미니 실험: docs/teams/20260121_asio-perf-plan/results/
  stepwise_rr_1k_64k_summary.md
- 관측 요약(개선 카운트 기준, zlink vs libzmq diff%):
  - latency diff%가 양수면 지연 개선(더 낮음), 음수면 지연 악화
  - PAIR: throughput +4 / -14, latency +1 / -17
  - DEALER_DEALER: throughput +5 / -13, latency +2 / -16
  - DEALER_ROUTER: throughput +7 / -11, latency +6 / -12
  - PUBSUB: throughput +12 / -6, latency +14 / -4
  - ROUTER_ROUTER: throughput +9 / -9, latency +12 / -6
  - ROUTER_ROUTER_POLL: throughput +10 / -8, latency +13 / -5

소켓별 적용 전략 및 근거 (테스트 결과 포함)

1) PAIR
- 관측: 지연/처리량 회귀 우세.
  - tcp 262144B latency -20.98%
  - inproc 1024B throughput -9.66%
- 전략(보수적 지연 최적화)
  - out_batch_size 최소화(기본값 유지 또는 감소)
  - io_context mode: poll 우선
  - speculative_write 루프 제한(ZMQ_ASIO_SINGLE_WRITE=1 고려)
  - TCP 옵션은 기본 off

2) PUBSUB
- 관측: 개선 우세, 특히 tcp 고용량에서 개선.
  - tcp 131072B throughput +20.43%, latency +16.97%
  - inproc 65536B throughput +5.62%
- 전략(대역폭 우선)
  - gather/writev 경로 강화(ZMQ_ASIO_GATHER_WRITE=1, threshold 조정)
  - out_batch_size 확대(소켓 타입별 옵션화)
  - io_context mode: run_for 우선(배치 처리)
  - TCP_NODELAY/QUICKACK는 필요 시만 실험

3) DEALER_DEALER
- 관측: 회귀 우세, tcp 중대형 메시지에서 악화.
  - tcp 131072B throughput -21.54%
  - tcp 262144B latency -19.60%
- 전략(공정성/지연 보수)
  - out_batch_size 보수적으로 유지
  - speculative_write는 제한적으로만 사용
  - io_context mode: poll 우선

4) DEALER_ROUTER
- 관측: 혼재지만 회귀 우세, ipc/tcp 큰 메시지 지연 악화.
  - ipc 65536B latency -22.16%
  - tcp 262144B latency -18.75%
- 전략(HOL 완화)
  - gather threshold 상향(큰 메시지에서 writev 남발 방지)
  - async_write_some 사용 여부 실험(ZMQ_ASIO_TCP_ASYNC_WRITE_SOME)
  - io_context mode: poll 우선

5) ROUTER_ROUTER
- 관측: 혼재, latency 개선이 더 많음.
  - ipc 65536B latency +14.72%
  - tcp 262144B throughput -17.03% (대형 메시지 주의)
- 미니 실험: stepwise_rr_1k_64k_summary.md에서
  handler allocator/idle backoff 계열이 ipc latency 개선 경향.
- 전략(라우팅 프레임 효율)
  - handler allocator 유지
  - speculative_write 유지
  - io_context mode: run_for 실험
  - 대형 메시지에서 gather threshold 점검

6) ROUTER_ROUTER_POLL
- 관측: 혼재, latency 개선 우세.
  - ipc 65536B latency +15.06%
  - tcp 64B throughput -5.28% (소형 메시지 주의)
- 전략(이벤트 루프 최적화)
  - idle backoff 유지
  - io_context mode: run_for 실험
  - 소형 메시지 회귀 여부 우선 체크

실험 절차 (소켓별)
1) 소켓 1개 선택
2) 미니 벤치: 1KB/64KB, tcp/inproc/ipc, runs=3
3) 개선 시 runs=10 전체 패턴 중 해당 소켓만 수행
4) 회귀 시 즉시 롤백 및 기록
5) 다른 소켓에 영향 확인(교차 회귀 체크)

벤치 명령 예시
- 미니 벤치:
  - benchwithzmq/run_benchmarks.sh --pattern <PATTERN> --runs 3 --reuse-build --skip-libzmq --msg-sizes 1024,65536
- 전체 벤치(소켓 1개):
  - benchwithzmq/run_benchmarks.sh --pattern <PATTERN> --runs 10 --reuse-build --skip-libzmq
  - <PATTERN>: PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER,
    ROUTER_ROUTER_POLL 중 선택

튜닝 옵션 후보(게이트)
- ZMQ_ASIO_GATHER_WRITE
- ZMQ_ASIO_GATHER_THRESHOLD
- ZMQ_ASIO_SINGLE_WRITE
- ZMQ_ASIO_TCP_ASYNC_WRITE_SOME
- ZMQ_ASIO_IOCTX_MODE (poll/run_for)
- ZMQ_ASIO_TCP_NODELAY
- ZMQ_ASIO_TCP_QUICKACK
- ZMQ_ASIO_TCP_BUSY_POLL
- TCP_QUICKACK/BUSY_POLL은 Linux 전용(WSL2 포함)임

결과 기록
- results/ 아래에 소켓별 로그와 요약 저장
- 예: results/socket_pubsub_runs10_YYYYMMDD.txt

성공 기준
- 해당 소켓/전송에서 throughput 또는 latency가 일관 개선
- 다른 소켓/전송에서 회귀가 1건이라도 있으면 적용 보류
- 회귀/개선 판단이 애매하면 동일 조건 재측정 후 판단

WSL2 제한
- perf 미사용
- strace/valgrind(callgrind)로 대체
