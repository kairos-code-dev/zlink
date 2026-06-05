# STREAM 소켓 성능개선 계획

> 범위: `core/` 내부의 `STREAM` socket data plane을 대상으로, public API를 유지한
> 상태에서 small-message throughput을 현재 대비 `10%+` 개선하는 구조 변경 계획이다.

## 1. 관련 문서

- 성능 가이드:
  [`doc/guide/10-performance.ko.md`](/home/hep7/project/kairos/zlink/doc/guide/10-performance.ko.md)
- 스레드 안전성 가이드:
  [`doc/guide/11-thread-safety.ko.md`](/home/hep7/project/kairos/zlink/doc/guide/11-thread-safety.ko.md)
- thread-safe 공개 계약 계획:
  [`doc/plan/thread-safe/thread-safe-socket-plan.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/thread-safe/thread-safe-socket-plan.ko.md)

이 문서는 `STREAM` socket의 user-visible API를 다시 설계하는 문서가 아니다.
목표는 `STREAM`의 내부 data plane을 더 깊은 모듈로 정리해, 공개 계약은
단순하게 유지하면서도 실제 처리량을 올리는 데 있다.

## 2. 배경

현재 `STREAM` socket의 계약은 다음과 같다.

- 생성은 `recv-first` 모델로 시작한다.
- callback 진입은 public attach API로만 들어간다.
- attach 이후 수신은 전용 callback mode로 동작한다.
- `STREAM` receive hot path는 generic `socket_base` callback bridge가 아니라
  `stream.cpp` 내부의 direct dispatch 경로를 사용한다.

이렇게 정리하면서 callback 공통 surface는 단순해졌지만, small-message benchmark에서는
여전히 `STREAM` 고유 data plane 비용이 남는다. 특히 `64B` 수준의 callback
workload에서 route 해석, message handoff, flush/wakeup 고정비가 throughput을
직접 제한한다.

이번 계획의 기본 전제는 다음과 같다.

- public API는 유지한다.
- `STREAM` 전용 성능 API는 추가하지 않는다.
- 기준 빌드/실행 경로는 항상 `core/build/`만 사용한다.
- 성능개선은 echo 전용 분기가 아니라 `STREAM recv/send` 일반 data plane 개선으로
  설명 가능해야 한다.

## 3. 현재 상태와 기준 수치

현재 기준 workload는 다음과 같다.

- benchmark: `STREAM_CALLBACK/tcp/64B`
- clients: `10000`
- build dir: `core/build`
- runner:
  `./core/perf/run_benchmarks_multi.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern STREAM_CALLBACK --transports tcp --msg-sizes 64 --runs 1 --warmup 1 --duration 3`

현재 기준 수치는 다음 파일에 기록해 두었다.

- 현재 기준 결과:
  [`perf_linux_20260317_100613.txt`](../../../core/perf/results/multi/report/perf_linux_20260317_100613.txt)
  - `STREAM_CALLBACK/tcp/64B = 357.802 Kops/s`
- steady-state fast path 반영 직후 확인 결과:
  [`perf_linux_20260317_095831.txt`](../../../core/perf/results/multi/report/perf_linux_20260317_095831.txt)
  - `STREAM_CALLBACK/tcp/64B = 361.234 Kops/s`

이 문서의 1차 목표는 다음과 같다.

- `STREAM_CALLBACK/tcp/64B` throughput을 현재 기준 대비 `10%+` 향상
- 기능 계약 회귀 없음
- p95/p99 latency 악화가 throughput 개선 대비 과도하지 않을 것

단, 구현 단계는 두 단계 gate로 나눈다.

- 1단계: `stream.cpp` / socket-level data plane만으로 `5~10%` 개선 가능성 확인
- 2단계: 1단계가 `10%`에 못 미치면 `asio_engine` / transport write-read policy를
  다음 구현 단계로 승격

## 4. 병목 분석

### 4.1 실제 병목으로 판단한 것

현재 `STREAM` receive/send hot path에서 비용이 큰 축은 다음 세 가지다.

1. `xstream_dispatch_msg()`의 `msg_t` handoff 비용
- 현재 경로는 callback 전달 전에 `msg_t init -> move -> src init`를 수행한다.
- 메시지가 작을수록 이 고정비가 더 크게 드러난다.

2. callback context의 send path가 여전히 route lookup과 flush 비용을 떠안는다
- callback 안에서 보내는 경로도 `routing_id -> route shard -> pipe` 해석을 다시 탄다.
- same-connection send에서도 같은 구조를 쓰므로 불필요한 비용이 남는다.

3. small-message transport flush/wakeup 성격의 고정비
- `STREAM` send path는 결과적으로 작은 payload에서도 빠르게 flush를 유발한다.
- 이 비용은 echo뿐 아니라 일반적인 request/response, framed stream workload 전반에
  영향을 준다.

4. socket-level 최적화만으로는 transport/engine 비용이 상한을 만들 수 있다
- 현재 TCP `STREAM` 송신은 `asio_engine`의 speculative write / async fallback
  루프를 탄다.
- 따라서 `stream.cpp`에서 route/handoff를 줄여도 일정 수준 이후에는 engine/transport
  비용이 throughput 상한이 될 수 있다.

### 4.2 주병목이 아닌 것으로 판단한 것

다음 시도들은 주효과가 아니거나 오히려 손해였다.

1. route table 자료구조 교체
- `std::map`을 hash table로 바꾸고 shard 수를 늘린 실험은 유의미한 개선을
  만들지 못했고 오히려 throughput이 내려갔다.
- 결론: 현재 주병목은 route 자료구조 자체가 아니다.

2. `pipe` write/flush lock 병합
- `write()`와 `flush()`를 한 lock 범위에서 처리한 실험도 오히려 throughput이
  더 낮아졌다.
- 결론: 단순 lock 횟수보다 lock 범위 확대가 더 나쁜 영향이었다.

### 4.3 현재까지 남긴 판단

- 자료구조 미세 튜닝은 우선순위가 낮다.
- `STREAM` 전용 data plane ownership과 handoff 구조를 줄여야 socket-level 이득을
  먼저 확보한다.
- 다만 `10%+`를 보장하려면 transport/engine 단계가 후속 병목일 가능성을
  계획에 포함해야 한다.
- 그래서 `stream.cpp` 단계의 목표와 `asio_engine` 승격 기준을 분리해 다룬다.

## 5. 최종 개선 방향

이번 계획에서 최종안으로 고정하는 축은 세 가지다.

### 5.1 `STREAM receive handoff` 전용화

- `xstream_dispatch_msg()`에서 callback 전달을 위한 `msg_t` handoff 비용을 줄인다.
- generic multipart bridge나 generic callback framing을 다시 끌어오지 않는다.
- `STREAM` receive callback 전용 내부 handoff helper를 도입해, ownership 이전과
  정리를 `stream.cpp` 안에서 끝내는 구조로 바꾼다.

### 5.2 per-pipe send state 기반 fast path

- `STREAM` 송신 경로를 `routing_id -> route table -> pipe` 한 단계 모델로만 보지 않고,
  내부적으로 `per-pipe send state`를 캐시하는 방향으로 재구성한다.
- callback context의 same-connection send는 그 상태를 활용하는 첫 번째 fast path일 뿐,
  목표는 callback 안/밖을 포함한 일반 `STREAM` send 경로의 해석 비용을 줄이는 데 있다.
- direct pipe path를 쓸 수 없는 경우에만 기존 route-table 경로로 떨어진다.
- `per-pipe send state`의 canonical owner는 `stream_t` 내부 side-table이다.
  `stream_dispatch_tls`는 현재 callback context의 fast-path 힌트만 보관하고,
  `pipe_t`나 public surface에는 새 상태를 추가하지 않는다.

### 5.3 `STREAM` 전용 flush policy

- 메시지마다 즉시 flush하는 현재 모델을 그대로 두지 않는다.
- callback context 안에서는 per-pipe short burst를 허용하고, callback 종료 또는
  pending threshold 도달 시 flush하는 `STREAM` 전용 정책을 도입한다.
- 새 public socket option은 추가하지 않는다. 1차 구현은 내부 상수 기반으로 시작한다.

## 6. 단계별 구현 계획

### Phase 1. receive handoff 전용화

목표:
- `xstream_dispatch_msg()`의 `msg_t init/move/init` 고정비를 줄인다.

구현 원칙:
- `STREAM callback delivery` 내부 helper를 `stream.cpp`에 둔다.
- callback 전달에 필요한 ownership 정리는 helper가 전담한다.
- `socket_base` generic callback path는 쓰지 않는다.

완료 조건:
- `STREAM` receive hot path에서 heap allocation, generic multipart wrapping,
  generic TLS bookkeeping이 새로 생기지 않는다.

### Phase 2. callback-context send fast path

목표:
- 일반 `STREAM` send 경로의 route 해석 비용을 줄인다.

구현 원칙:
- `stream_dispatch_tls`에 현재 callback pipe와 routing id 외에 현재 send target pipe도
  함께 보관한다.
- `stream.cpp` 내부에 `per-pipe send state`를 두고, callback 안/밖 모두에서 활용할
  수 있는 일반 fast path를 설계한다.
- `per-pipe send state`는 `stream_t`가 보유하는 side-table로 구현한다.
- callback TLS는 해당 side-table entry를 빠르게 찾는 힌트로만 쓴다.
- callback context same-connection send는 direct pipe path를 탄다.
- direct pipe path를 쓸 수 없는 경우에만 기존 route-table 경로를 유지한다.

완료 조건:
- callback context same-connection send 경로가 route shard lock 없이 동작한다.
- callback 밖 일반 send도 `per-pipe send state`를 재사용할 수 있는 구조가 된다.
- fallback 경로는 기존 routing id semantics를 그대로 보존한다.

### Phase 3. `STREAM` 전용 flush policy

목표:
- small-message send에서 flush/wakeup 고정비를 줄인다.

구현 원칙:
- callback context direct send는 per-pipe pending write 수를 추적한다.
- 미flush write는 callback 종료 시 반드시 정리한다.
- multipart 경계와 pending threshold를 flush trigger로 쓴다.
- public option 추가 없이 내부 상수로 시작한다.

완료 조건:
- callback 종료 후 pipe에 미flush payload가 남지 않는다.
- 일반 `STREAM` send semantics와 close semantics가 유지된다.
- send-ready/backpressure/reader wakeup semantics가 기존과 충돌하지 않는다.

### Phase 4. 재측정 및 수용 판정

목표:
- socket-level data plane 재구성만으로 얻을 수 있는 이득과, engine 단계 승격 필요성을
  함께 판정한다.

구현 원칙:
- 먼저 `stream.cpp` 내부 개편 효과를 독립적으로 확인한다.
- `5~10%` 미만이면 transport/asio를 다음 구현 단계로 즉시 승격한다.

완료 조건:
- socket-level 개선만으로 `10%`를 달성하거나,
- socket-level 개선이 `5~10%` 수준에 머물고 `asio_engine`이 남은 주병목이라는
  근거가 명확해야 한다.

## 7. 구현 경계

### 7.1 변경 대상

- [`core/src/sockets/stream.cpp`](../../../core/src/sockets/stream.cpp)
- 필요 시 [`core/src/sockets/stream.hpp`](../../../core/src/runtime/sockets/stream/stream.hpp)
- 필요 시 [`core/src/core/session_base.cpp`](../../../core/src/runtime/core/session_base.cpp)

### 7.2 변경 제외

- public API surface
- `socket_base` generic callback lifecycle 전반
- route 자료구조 교체
- `pipe` write/flush lock 병합
- transport/asio 레이어 대수술

### 7.3 후속 단계로 미루는 것

socket-level data plane 개편이 `10%`에 못 미치면 아래를 다음 단계 구현 범위로
승격한다.

- `asio_raw_engine`의 stream write/read batching
- transport wakeup/flush 정책 재조정
- TCP/IPC/TLS/WS 공통 speculative write/read budget 조정

## 8. 검증 계획

### 8.1 기능 검증

- `STREAM` callback attach 후 recv 금지 계약 유지
- close during callback은 계속 `EBUSY`
- connect/disconnect/connection-ready monitor event 회귀 없음
- same-connection send와 arbitrary routing-id send 모두 기존 semantics 유지
- send-ready/backpressure/reader wakeup semantics 회귀 없음
- callback 종료 시 flush 보장으로 close/teardown 순서가 흔들리지 않을 것

### 8.2 성능 검증

필수 benchmark:

- `STREAM_CALLBACK/tcp/64B`
- `STREAM_CALLBACK/tcp/256B`
- `STREAM_CALLBACK/tcp/1KiB`

필수 smoke:

- `core/build/bin/comp_src_stream_callback_server`
- `core/build/bin/perf_stream_client --ccu 10000`

### 8.3 수용 기준

- `STREAM_CALLBACK/tcp/64B` throughput `+10%` 이상
- `256B`, `1KiB`에서도 명백한 역회귀가 없을 것
- p95/p99 latency 악화가 throughput 향상 대비 과도하지 않을 것
- 기존 `core` stream integration tests와 perf callback server smoke가 통과할 것
- socket-level 단계가 `5~10%`에 그치면 transport/asio 단계 승격 근거를 함께 남길 것

## 9. 명시적 가정

- public API는 유지한다.
- 기준 빌드 디렉토리는 `core/build/` 하나만 쓴다.
- route 자료구조 교체는 이번 계획에서 제외한다.
- echo 전용 최적화는 금지한다.
- transport/asio 수정은 2단계 후보로 남기되, socket-level 결과가 `5~10%`에 그치면
  즉시 승격한다.

## 10. 핵심 결정 문장

- `STREAM` 성능 문제는 public API가 아니라 internal data plane의 handoff와
  flush 구조에서 해결한다.
- 자료구조 튜닝보다 ownership과 per-pipe send state 단순화가 우선이다.
- `stream.cpp` 단계는 socket-level 개선 한계를 먼저 확인하는 단계이고,
  목표 미달 시 `asio_engine` 단계로 바로 올라간다.
