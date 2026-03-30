# with_zmq 벤치 정책

`core/bench/with_zmq/` 는 `libzmq` 와 `zlink` 를 동일 조건으로 비교하기
위한 벤치 표면이다.

이 디렉터리의 목적은 각 라이브러리의 모든 API 변형을 시험하는 것이 아니라,
패턴별로 같은 메시징 의미를 가진 경로만 비교하는 것이다.

## 기본 원칙

1. 같은 패턴이면 두 라이브러리는 같은 메시지 구조를 사용해야 한다.
2. single 벤치는 `msg_t` 기반 send/recv 를 기준으로 맞춘다.
3. 한쪽만 raw `data[]` send/recv 를 쓰고 다른 쪽만 `msg_t` 를 쓰면 안 된다.
4. `with_zmq` 에서는 callback 기반 recv 경로를 사용하지 않는다.
5. stream callback 기반 변형 모드는 `with_zmq` 비교 대상이 아니다.
6. routing, topic, multipart 의미가 있는 패턴은 그 의미를 양쪽에서 그대로
   유지해야 한다.
7. 같은 패턴인데 session setup, handshake, frame 구조가 다르면 그 벤치는
   유효한 비교가 아니다.

## "동일 조건"의 의미

동일 조건은 함수 이름이 같다는 뜻이 아니다.

다음을 맞춘다는 뜻이다.

- 같은 소켓 패턴
- 같은 routing 또는 topic 의미
- 같은 multipart 구조
- 같은 handshake 절차
- 같은 send/recv 모델
- 같은 benchmark 레이어의 payload 처리 방식

함수 이름이 달라도 같은 메시징 동작을 표현하면 대응 API로 볼 수 있다.

## Single 벤치 규칙

`single/` 아래 벤치는 다음 기준을 따른다.

- `PAIR`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER` 는 양쪽 모두
  `msg_t` payload send/recv 를 사용한다.
- `ROUTER_*` 패턴은 양쪽 모두 routing-id frame + payload `msg_t` 구조를
  유지한다.
- `ROUTER_ROUTER` 는 측정 전에 양쪽 모두 explicit handshake 를 수행한다.
- `PUBSUB` 는 양쪽 모두 topic-aware publish/subscribe 의미를 유지한다.
- `PUBSUB` payload 역시 `msg_t` 로 다룬다.
- single 벤치에서는 callback recv dispatch 를 허용하지 않는다.

## 패턴별 기준

### `PAIR`

- 의미: routing 없는 단일 payload 메시지 교환
- 허용 형태:
  - `msg_t` send
  - `msg_t` recv

### `DEALER_DEALER`

- 의미: 외부에 routing frame 이 드러나지 않는 단일 payload 메시지 교환
- 허용 형태:
  - `msg_t` send
  - `msg_t` recv

### `DEALER_ROUTER`

- 의미: dealer 가 payload 1개를 보내고 router 가 source routing id 와
  payload 1개를 받는 구조
- 허용 형태:
  - dealer 쪽: `msg_t` send
  - router 쪽: routed `msg_t` recv

### `ROUTER_ROUTER`

- 의미: 명시적인 routed multipart 전달
- 허용 형태:
  - routed `msg_t` send
  - routed `msg_t` recv
  - 측정 전 explicit handshake

### `PUBSUB`

- 의미: topic frame + payload 1개
- 허용 형태:
  - topic 의미를 가진 publish
  - topic 의미를 가진 subscribe recv
  - payload 는 `msg_t`

## 공용 helper 규칙

`single/common/` 의 공용 helper 는 라이브러리 차이를 숨기는 용도가 아니라,
비교 계약을 강제하는 용도여야 한다.

helper 는 다음까지만 허용한다.

- `msg_t` payload 생성
- `msg_t` move
- routed multipart send/recv
- pub/sub topic + payload send/recv

helper 는 다음을 하면 안 된다.

- 한 라이브러리에서만 raw buffer send/recv 로 우회하기
- `with_zmq` 안에 callback dispatch 넣기
- 두 라이브러리 사이에서 논리적 메시지 구조를 바꾸기

## Multi 벤치 표면

multi 벤치는 실제 비교에 사용하는 split client/server 경로만 공식 비교
표면으로 본다.

- `run_benchmarks_multi.sh` 와 `multi/run_comparison.py` 가 실행하는 바이너리만
  공식 비교 대상이다.
- `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`,
  `STREAM` 의 공식 비교 구현은 `multi/zlink/` 와 `multi/zmq/` 아래 대응 파일이
  직접 소유한다.
- 각 라이브러리 폴더에는 패턴별 client/server 파일이 모두 존재해야 한다.
- 대응 파일을 열면 각 패턴의 핵심 send/recv 흐름이 직접 보여야 한다.
- zlink 와 libzmq 의 구현은 파일 배치까지 대칭으로 유지한다.
- 예외: `STREAM client` 는 raw transport benchmark 이므로
  `core/perf/common/streamclient/` 구현을 직접 사용하는 것을 규칙으로 한다.
- 따라서 `STREAM client` 는 local copy runner 를 두지 않고,
  `perf_stream_bench_client.hpp` / `perf_stream_client_options.hpp` 를 직접 쓴다.
- `core/perf/multi/src/` 는 perf 전용 표면이고, `with_zmq` 의 공식 비교 구현을
  직접 소유하면 안 된다.
- callback 기반 stream 변형은 제외한다.
- 실제 `with_zmq` multi 비교 경로는 recv 기반 benchmark surface 로 유지한다.
- 공식 비교에 쓰이지 않는 standalone multi 실험 코드는 `with_zmq` 공식 비교
  표면에 두지 않는다.

## Multi 구조 원칙

`multi/` 는 `single/` 과 같은 방향으로 유지한다.

- 패턴별 client/server 파일을 열면 메시지 송수신 핵심 흐름이 바로 보여야 한다.
- 공용 helper 는 옵션 파싱, 측정, 공용 metric 처리, resource setup 까지만 맡는다.
- 패턴 고유의 send/recv 의미를 wrapper chain 뒤로 숨기면 안 된다.
- active 비교 패턴에 대해 한쪽 라이브러리만 얇은 wrapper 파일을 두면 안 된다.
- `multi/zlink/` 와 `multi/zmq/` 는 공식 비교 surface 이고, 공통 helper는
  핵심 send/recv 를 숨기면 안 된다.

## 해석 주의

`PUBSUB` 와 `ROUTER_ROUTER` 는 "함수 하나씩 맞추기" 로 끝나는 패턴이 아니다.
핵심은 semantic equivalence 다.

- `PUBSUB` 는 topic-aware delivery 의미가 맞아야 한다.
- `ROUTER_ROUTER` 는 routed multipart delivery 와 session setup 이 맞아야
  한다.

그래서 함수 이름이 달라도 메시징 의미가 같으면 올바른 대응으로 본다.

## 유지보수 규칙

`core/bench/with_zmq/` 아래 코드를 바꿀 때는 이 문서의 기준을 먼저
지켜야 한다.

새 경로가 이 규칙을 위반하면 다음 둘 중 하나로 처리한다.

- 두 라이브러리가 같은 계약을 따르도록 다시 설계한다.
- 아니면 `with_zmq` 비교 표면 밖에 둔다.
