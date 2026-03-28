# `with_zmq` 출력 형식 차이 메모

## 1. 결론

- 현재 워크트리와 기준 레포의 `report formatter` 자체는 같다.
  - `core/bench/with_zmq/run_benchmarks.sh`
  - `core/bench/with_zmq/single/run_comparison.py`
  - `core/bench/with_zmq/multi/run_comparison.py`
- 현재 출력이 더 길어 보이는 직접 원인은 `formatter` 변경이 아니라
  `queue telemetry RESULT`가 추가로 들어오기 때문이다.
- 추가 행은 아래 3개다.
  - `SndPending`
  - `RcvPending`
  - `RcvEnd`
- 즉 보이는 차이는
  `같은 markdown 테이블 formatter + 다른 metric set`
  의 결과다.

## 2. formatter가 같은 근거

- `single/run_comparison.py`는 기준 레포와 현재 워크트리에서 동일하다.
- transport table header와 metric row 조립 코드는 동일하다.
  - `build_transport_report_header_lines()`
  - `build_size_report_lines()`
- `multi/run_comparison.py`와 top-level `run_benchmarks.sh`도 동일하다.

정리하면 `report 본문을 어떤 모양으로 찍을지`는 바뀌지 않았다.

## 3. 현재 출력이 달라 보이는 직접 이유

`single/run_comparison.py`의 `build_size_report_lines()`는
기본적으로 아래 2개 행을 항상 만든다.

- `Throughput`
- `Latency`

그리고 아래 metric이 존재할 때만 조건부로 추가 행을 만든다.

- `snd_pending_max` -> `SndPending`
- `rcv_pending_max` -> `RcvPending`
- `rcv_pending_end` -> `RcvEnd`

즉 현재 report에 queue 행이 보인다면,
formatter가 바뀐 것이 아니라 benchmark binary가
`RESULT,...,snd_pending_max,...` 같은 metric을 실제로 뿌린 것이다.

## 4. queue metric이 실제로 생성되는 코드 경로

single bench는 현재 `PAIR`, `PUBSUB`, `DEALER_DEALER`,
`DEALER_ROUTER`, `ROUTER_ROUTER`에서 공통적으로 아래 구조를 쓴다.

1. `queue_probe_t queue_probe(...)` 생성
2. active phase에서
   `queue_probe->sample_send_if_due()` /
   `queue_probe->sample_recv_if_due()` 호출
3. 종료 시 `queue_probe.snapshot()`으로 `queue_stats_t` 획득
4. `print_result(..., queue_stats)` 호출
5. 공통 helper가 `snd_pending_max`, `rcv_pending_max`,
   `rcv_pending_end`를 `RESULT` 라인으로 추가 출력

예:

- `core/bench/with_zmq/single/zlink/bench_zlink_pair.cpp`
- `core/bench/with_zmq/single/zmq/bench_zmq_pair.cpp`
- `core/bench/with_zmq/single/common/bench_common.hpp`
- `core/bench/with_zmq/single/common/bench_common_zlink.hpp`

즉 queue 행은 특정 결과 파일의 우연이 아니라,
현재 single bench 코드가 의도적으로 내는 부가 telemetry다.

## 5. libzmq와 zlink 열이 다르게 보이는 이유

`libzmq` 쪽 공통 헤더에서는 `zlink_socket_peers()`가 stub이다.

- `core/bench/with_zmq/single/common/bench_common.hpp`
  - `zlink_socket_peers(...)` -> `ENOTSUP`

반면 `zlink` 쪽 공통 헤더에서는 `queue_probe_t`가
실제 `zlink_socket_peers()`를 호출해서 peer queue 상태를 읽는다.

- `core/bench/with_zmq/single/common/bench_common_zlink.hpp`
  - `read_first_peer_info()`
  - `maybe_sample_send()`
  - `maybe_sample_recv()`

그래서 최종 report에서는 보통 이렇게 나온다.

- `Standard libzmq`: `N/A`
- `zlink`: 숫자

이건 formatter 차이가 아니라
`libzmq는 queue telemetry source가 없고`,
`zlink는 queue telemetry source가 있다`
는 차이다.

## 6. 현재 출력에서 실제로 달라진 부분

현재 워크트리의 full single report
`core/bench/with_zmq/results/single/report/perf_linux_20260328_220114.txt`
를 보면 `PAIR`, `PUBSUB` 등 각 transport/size 블록 아래에
아래 행이 추가된다.

- `SndPending`
- `RcvPending`
- `RcvEnd`

이 3개를 제외한

- `## Effective Options`
- `## PATTERN`
- `### Transport`
- `Throughput`
- `Latency`
- `Failures`
- `Saved result file`

형식은 formatter 코드 기준으로 동일하다.

## 7. 운영 판단

- 현재 차이는 `출력 포맷 버그`가 아니다.
- 현재 차이는 `queue telemetry 행이 추가된 상태를 그대로 report가 반영한 것`이다.
- 따라서 기준 레포와 `완전히 같은 보이는 형태`만 원하면,
  bench/report 정책 차원에서 아래 둘 중 하나를 선택해야 한다.
  - queue metric `RESULT` 자체를 내지 않게 한다.
  - `run_comparison.py`에서 queue metric 행을 렌더링하지 않게 한다.

하지만 이건 formatter 복구가 아니라
`보여주는 metric set을 줄이는 정책 변경`이다.
