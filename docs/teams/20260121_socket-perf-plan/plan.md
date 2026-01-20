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

소켓별 가설 및 적용 방향

1) PAIR
- 특징: 왕복 지연 민감, 대역폭보다 지연이 중요
- 가설: 과도한 batching/idle backoff는 지연을 악화
- 방향
  - out_batch_size 최소화(기본값 유지 또는 감소)
  - io_context mode: poll 우선
  - speculative_write 루프 제한(ZMQ_ASIO_SINGLE_WRITE=1 고려)
  - TCP 옵션은 기본 off

2) PUBSUB
- 특징: fanout, 대량 전송, 대역폭 우선
- 가설: batching/gather/writev가 효과적
- 방향
  - gather/writev 경로 강화(ZMQ_ASIO_GATHER_WRITE=1, threshold 조정)
  - out_batch_size 확대(소켓 타입별 옵션화)
  - io_context mode: run_for 우선(배치 처리)
  - TCP_NODELAY/QUICKACK는 필요 시만 실험

3) DEALER_DEALER
- 특징: 양방향 균형, 지연/공정성 중요
- 가설: 과도한 batching이 공정성/지연 악화
- 방향
  - out_batch_size 보수적으로 유지
  - speculative_write는 제한적으로만 사용
  - io_context mode: poll 우선

4) DEALER_ROUTER
- 특징: 라우팅/공정성 병목, 큰 메시지에서 회귀 관찰
- 가설: 대형 메시지 배치가 HOL(h head-of-line) 악화
- 방향
  - gather threshold 상향(큰 메시지에서 writev 남발 방지)
  - async_write_some 사용 여부 실험(ZMQ_ASIO_TCP_ASYNC_WRITE_SOME)
  - io_context mode: poll 우선

5) ROUTER_ROUTER
- 특징: 라우팅 프레임 많고 latency 민감
- 가설: completion handler 비용 감소가 효과적
- 방향
  - handler allocator 유지
  - speculative_write 유지
  - io_context mode: run_for 실험
  - 작은 메시지 지연 측면 확인

6) ROUTER_ROUTER_POLL
- 특징: poll 기반 측정, wakeup/idle 정책에 민감
- 가설: idle backoff가 latency 개선에 유효
- 방향
  - idle backoff 유지
  - io_context mode run_for 실험

실험 절차 (소켓별)
1) 소켓 1개 선택
2) 미니 벤치: 1KB/64KB, tcp/inproc/ipc, runs=3
3) 개선 시 runs=10 전체 패턴 중 해당 소켓만 수행
4) 회귀 시 즉시 롤백 및 기록
5) 다른 소켓에 영향 확인(교차 회귀 체크)

벤치 명령 예시
- 미니 벤치:
  - benchwithzmq/run_benchmarks.sh --pattern ROUTER_ROUTER --runs 3 --reuse-build --skip-libzmq --msg-sizes 1024,65536
- 전체 벤치(소켓 1개):
  - benchwithzmq/run_benchmarks.sh --pattern ROUTER_ROUTER --runs 10 --reuse-build --skip-libzmq

튜닝 옵션 후보(게이트)
- ZMQ_ASIO_GATHER_WRITE
- ZMQ_ASIO_GATHER_THRESHOLD
- ZMQ_ASIO_SINGLE_WRITE
- ZMQ_ASIO_TCP_ASYNC_WRITE_SOME
- ZMQ_ASIO_IOCTX_MODE (poll/run_for)
- ZMQ_ASIO_TCP_NODELAY
- ZMQ_ASIO_TCP_QUICKACK
- ZMQ_ASIO_TCP_BUSY_POLL

결과 기록
- results/ 아래에 소켓별 로그와 요약 저장
- 예: results/socket_pubsub_runs10_YYYYMMDD.txt

성공 기준
- 해당 소켓/전송에서 throughput 또는 latency가 일관 개선
- 다른 소켓에서 의미 있는 회귀가 없을 것

WSL2 제한
- perf 미사용
- strace/valgrind(callgrind)로 대체

