# SPOT inproc Facade 재설계 계획

## 요약

현재 `SPOT`는 `spot_node`가 실제 mesh `PUB/SUB/DEALER`를 소유하고,
`spot_pub/sub`는 facade로만 동작한다. 이를 다음 구조로 바꾼다.

- `SpotPub`는 실제 pollable `PUB` 소켓을 소유한다.
- `SpotSub`는 실제 pollable `SUB` 소켓을 소유한다.
- 둘 다 `spot_node`와는 private `inproc` endpoint로 연결된다.
- `spot_node`는 외부 mesh/control과 local bridge만 담당한다.
- 기존 `zlink_spot_pub_*`, `zlink_spot_sub_*` API 이름은 유지한다.
- `peer/monitor` 의미는 facade의 실제 raw socket 기준으로 바뀐다.
- `SpotNode`에는 socket-role API가 아니라 node 공통 옵션 API만 둔다.

## 주요 변경

### 1. 데이터 경로 구조 변경

`spot_node` 내부 소켓을 다음처럼 분리한다.

- 외부 mesh/control:
  - 외부 publish용 mesh `PUB`
  - 외부 receive용 mesh `SUB`
  - registry/control용 `DEALER`
- local bridge:
  - local publisher ingress용 `SUB` (`inproc://.../pub-in`)
  - local subscriber fanout용 `XPUB` (`inproc://.../sub-out`)

`SpotPub` / `SpotSub`는 각각 다음을 소유한다.

- `SpotPub`
  - 실제 `PUB` 소켓 1개
  - node의 `pub-in` endpoint에 `connect`
- `SpotSub`
  - 실제 `SUB` 소켓 1개
  - node의 `sub-out` endpoint에 `connect`

node bridge 동작은 고정한다.

- local publish:
  - node가 local ingress `SUB`에서 수신
  - local fanout `XPUB`로 재송신
  - 외부 mesh `PUB`로도 송신
- remote receive:
  - node가 외부 mesh `SUB`에서 수신
  - local fanout `XPUB`로만 재송신
  - 외부로 재발행하지 않음

이 구조로 기존 `spot_node`의 local topic index, per-sub local queue,
node-owned local dispatch 경로는 제거한다.

### 2. facade API 의미 재정의

기존 public API 이름은 유지하되 내부 의미를 다음으로 고정한다.

- `zlink_spot_pub_publish*`
  - `SpotPub`가 소유한 실제 local `PUB`로 직접 송신
  - per-instance mutex로 publish 직렬화
- `zlink_spot_sub_recv`
  - `SpotSub`가 소유한 실제 local `SUB`에서 직접 수신
- `zlink_poller_add_spot_pub` / `zlink_poller_add_spot_sub`
  - facade의 실제 local socket을 poll 대상으로 사용
  - 더 이상 node-owned socket이나 queue signaler를 보지 않음
- `zlink_spot_sub_subscribe` / `unsubscribe`
  - `SpotSub`의 실제 `SUB` socket에 직접 적용
  - node는 `XPUB` subscription propagation을 통해 local fanout 대상만
    자연스럽게 알게 됨

`set_handler`는 `io_context`의 스레드에서 콜백을 호출하도록 구현한다.
`SpotSub`별 전용 스레드를 만들지 않는다 (sub가 수천 개 생성될 수 있으므로).

- handler가 설정되면 io_context 스레드가 해당 sub의 local fanout
  메시지를 대신 수신하여 callback 호출
- handler 활성 중 `recv`는 `EINVAL` 반환 (상호 배타)
- handler 해제 시 진행 중인 콜백이 모두 완료된 후 반환

### 3. peer / monitor / option surface

`peer/monitor`는 raw socket 의미로 고정한다.

- `zlink_spot_pub_peers` / `zlink_spot_sub_peers`
  - facade가 소유한 실제 local inproc socket의 peer 상태를 반환
- `zlink_spot_pub_monitor_open` / `zlink_spot_sub_monitor_open`
  - facade socket monitor 기준으로 동작
- 기존 service-topology 의미는 더 이상 유지하지 않음

`SpotNode` 공통 옵션 API를 새로 둔다.

- 새 public API:
  - `zlink_spot_node_set_option(node, option, value, size)`
- scope:
  - control tick / idle sleep
  - heartbeat interval
  - discovery refresh interval
  - node 공통 동작만 포함
- 제외:
  - PUB/SUB/DEALER socket-role 옵션
  - local bridge socket 직접 제어
  - 이들은 public에 노출하지 않음

`SpotPub` / `SpotSub` option API는 facade 소켓에 직접 적용되도록 유지한다.

### 4. 문서와 내부 모델 정리

문서 모델을 현재 구현 설명에서 새 구조로 바꾼다.

- `spot_node`는 mesh/control/bridge owner
- `spot_pub/sub`는 실제 inproc-backed socket facade
- poller는 facade socket을 직접 본다
- `peer/monitor`는 raw socket 의미
- callback은 `io_context` 스레드에서 실행

내부 설계 문서와 API 문서에서
“node owns all pub/sub sockets” 설명은 제거한다.

## 테스트 계획

### 기능/구조 테스트

- `SpotPub` 1개, `SpotSub` 1개, same process/same ctx:
  - local publish가 node bridge를 거쳐 local sub로 도착
- remote mesh:
  - node A `SpotPub` publish -> node B `SpotSub` receive
  - remote receive가 다시 외부 mesh로 재발행되지 않음
- multiple local pubs:
  - 여러 `SpotPub`가 하나의 node ingress로 정상 fan-in
- multiple local subs + topic filtering:
  - `XPUB` subscription propagation으로 topic/pattern filtering 정상 동작

### API semantics 테스트

- `zlink_spot_sub_recv`가 실제 SUB readiness 기준으로 동작
- `zlink_poller_add_spot_sub/pub`가 facade socket readiness와 일치
- `set_handler` 활성 시 `recv` 경로와 상호 배타
- `peer/monitor`가 raw inproc facade socket 기준 값으로 바뀐 것을 검증
- `SpotNode` 공통 옵션 API가 control tick/heartbeat/discovery refresh에 반영

### 회귀/bench 테스트

- 기존 SPOT 단위 테스트 전체 통과
- poller 기반 SPOT 예제와 monitor 예제 통과
- `core/perf` single/multi `SPOT` smoke
  - `client=1` baseline
  - `clients=100` baseline
- `Gateway`, `Discovery`, 일반 socket pattern 회귀 없음

## 가정

- 같은 `ctx` 안에서만 facade와 node를 연결한다.
- private `inproc` endpoint는 node 생성 시 고유 이름으로 bind하고,
  facade는 connect만 한다.
- `SpotPub/SpotSub` public API 이름은 유지한다.
- `peer/monitor` 의미 변경은 허용한다.
- `SpotNode` public option API는 node 공통 옵션만 다루고,
  socket-role API는 재도입하지 않는다.
- `SpotSub` callback delivery는 `io_context` 스레드에서 실행한다.
