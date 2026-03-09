# SPOT Proxy 재작성 스펙

## 1. 문서 목적

이 문서는 기존 `SPOT` 구현을 보수하는 계획이 아니라, 기존 구현을
삭제하고 `proxy` 중심 구조로 다시 작성하기 위한 구현 스펙이다.

핵심 판단은 다음과 같다.

- 현재 구현은 facade 의미는 바꿨지만 data path는 얇아지지 않았다.
- `publish` hot path에 control task, 재조립, 재복사, local dispatch가 섞여 있다.
- 목표가 `inproc hop + proxy` 수준 오버헤드라면 현 구조는 실패다.
- 따라서 incremental fix가 아니라 구조 재작성으로 간다.

이 문서는 다음을 고정한다.

- `SPOT` data plane은 `proxy` 기반으로 구현한다.
- `control task`는 discovery / heartbeat / peer 관리만 담당한다.
- `publish` / `recv` / local fanout은 control task를 거치지 않는다.
- `SpotSub handler`는 facade `SUB` socket의 direct dispatch 기능을 사용한다.
- `SpotPub` public API는 thread-safe 계약으로 구현한다.

### 1.1 외부 동작 기준 문서

외부 사용자 관점의 동작 계약은
[`07-3-spot.ko.md`](/home/hep7/project/kairos/zlink/doc/guide/07-3-spot.ko.md)를 기준으로 한다.

이 스펙은 내부 구현 재작성 문서이며, 다음 원칙을 따른다.

- guide에 적힌 public 사용 흐름은 유지한다
- 재작성은 내부 data plane / control plane 구조만 바꾼다
- guide와 충돌하는 내부 설계는 허용하지 않는다
- guide에 아직 부정확한 내부 표현이 있으면, 구현 후 guide도 함께 정정한다

즉, 이번 재작성은 "SPOT을 다른 기능으로 바꾸는 작업"이 아니라
"guide에 적힌 SPOT 동작을 더 얇고 빠른 구조로 다시 구현하는 작업"이다.

## 2. SPOT 기능 정의

### 2.1 SPOT이 해결해야 하는 문제

`SPOT`은 "위치 투명(location-transparent) topic pub/sub"를 제공하는 서비스다.

사용자는 다음을 신경 쓰지 않고 메시지를 다뤄야 한다.

- publisher와 subscriber가 같은 process 안에 있는지
- 같은 node 안에 있는지
- 다른 node에 있는지
- peer가 discovery로 붙었는지 수동 mesh로 붙었는지

사용자가 알아야 하는 것은 오직:

- 어떤 `topic`으로 publish 하는지
- 어떤 `topic` 또는 `pattern`을 subscribe 하는지

뿐이어야 한다.

즉, `SPOT`의 핵심 기능은 "위치가 아니라 topic으로 연결되는 fanout"이다.

### 2.2 대표 시나리오: MMORPG zone adjacency

`SPOT`의 대표 사용 시나리오는 zone 기반 근접 메시지 전파다.

예:

- world가 여러 zone으로 분할되어 있다
- zone마다 하나 이상의 `SpotPub` / `SpotSub`가 존재한다
- zone `A`는 자기 자신과 인접 zone만 subscribe 한다
- zone `A`에서 player movement / npc state / projectile event를 publish 한다
- 해당 메시지는 `A` 자신과 인접 zone에서만 관측되어야 한다

이 시나리오에서 `SPOT`은 다음을 만족해야 한다.

- same-node zone adjacency와 cross-node zone adjacency의 의미가 동일해야 한다
- zone 배치가 바뀌어도 publish / subscribe 코드가 바뀌지 않아야 한다
- discovery 결과에 따라 local/remote 여부가 바뀌어도 전달 semantics가 동일해야 한다

즉, `SPOT`은 "zone adjacency를 topic으로 표현했을 때, 위치 투명하게 동작하는 분산 fanout 계층"이어야 한다.

### 2.3 SPOT의 기능 계약

재작성 후 `SPOT`은 아래 기능을 제공해야 한다.

이 기능 계약은 guide의 다음 사용 모델을 그대로 만족해야 한다.

- 단일 서버 local fanout
- 클러스터 PUB/SUB mesh
- discovery 기반 자동 mesh
- 수동 peer connect mesh
- pub/sub/pattern subscribe/unsubscribe
- poller 통합
- handler 기반 자동 수신
- remote receive의 non-republish 전달 정책

registry 연동 계약은 다음처럼 단순화한다.

- discovery는 peer 발견용
- register/unregister/heartbeat는 node registration API가 직접 담당
- `connect_registry()` 후 `register()`를 호출하는 2단계 설정은 제거한다
- node registration은 attached discovery가 bootstrap 과정에서 학습한 registry uplink를 재사용한다

#### topic fanout

- `SpotPub`가 topic에 publish 하면
- 그 topic 또는 matching pattern을 가진 `SpotSub`만 수신한다
- local / remote 여부는 결과에 영향을 주지 않는다

#### pattern subscription

- exact topic subscription 지원
- 접두어 + `*` pattern 지원
- 같은 subscriber에 exact + pattern이 같이 있어도 메시지는 1회만 전달

#### local-first fanout

- local subscriber가 있으면 같은 process / same node subscriber는 remote 전파와 별개로 fanout 된다
- local subscriber가 없더라도 remote peer가 있으면 mesh publish는 수행된다

#### remote mesh propagation

- peer node로 연결되어 있으면 remote subscriber도 같은 topic semantics로 수신한다
- remote에서 받은 메시지는 local fanout만 수행하고 다시 mesh로 재발행하지 않는다

#### discovery-backed connectivity

- registry / discovery를 통해 peer mesh가 바뀌어도 API 의미는 유지된다
- node는 새 peer가 붙으면 자동으로 그 peer를 mesh 수신 대상에 포함한다
- peer가 사라지면 더 이상 그 peer에서 메시지를 받지 않는다

### 2.4 전달 보장과 비보장

`SPOT`은 queue service가 아니라 live pub/sub service다.

보장:

- 구독이 활성화된 뒤 들어온 메시지는 matching subscriber에게 fanout 된다
- local publish는 local subscriber와 remote peer에게 동시에 fanout 가능하다
- remote receive는 local에만 fanout 되고 재발행되지 않는다
- 한 publisher 인스턴스의 publish 호출 순서는 보존된다

보장하지 않음:

- durable delivery
- ack / retry / exactly-once
- late join subscriber에 대한 과거 메시지 재전송
- cluster-wide subscribe 전파 완료 시점의 강한 동기 보장

따라서 `subscribe()` 반환은 "local socket filter 적용 완료" 의미이지
"모든 peer에 전파 완료" 의미가 아니다.

### 2.5 중복 / 루프 / 순서 규칙

- remote에서 받은 메시지는 다시 mesh publish 하지 않는다
- 동일 메시지가 mesh loop를 돌아 재유입되면 안 된다
- 동일 node 내 exact topic + pattern 중복으로 callback/recv가 2회 나오면 안 된다
- 같은 `SpotPub` 인스턴스에서 연속 publish된 메시지는 수신측에서 순서가 뒤집히면 안 된다
- 서로 다른 `SpotPub` 인스턴스 사이의 전역 순서는 보장하지 않는다

node 간 상대 순서는 transport / scheduling 영향으로 전역 보장하지 않는다.

### 2.6 API 사용자 관점의 의미

사용자는 `SPOT`을 다음처럼 이해해야 한다.

- `SpotPub`는 "어디로 갈지 모르는" 것이 아니라 "topic fanout 계층으로 publish 하는 handle"
- `SpotSub`는 "어디서 왔는지 몰라도" topic 기준으로 메시지를 받는 handle
- `SpotNode`는 local facade와 remote mesh를 이어주는 routing/bridge agent

중요한 점은 `SpotPub` / `SpotSub`이 단순 helper가 아니라
실제 pollable facade socket이라는 점이다.

threading 계약은 다음으로 고정한다.

- `SpotPub`는 thread-safe
- `SpotSub`는 thread-safe가 아님
- `SpotSub`의 `recv`, `set_handler`, `subscribe`, `unsubscribe`는 외부에서 직렬화해야 함

guide 기준으로 다음 사용자 코드 패턴은 모두 계속 유효해야 한다.

- `zlink_spot_node_bind()` 후 `zlink_spot_pub_new()` / `zlink_spot_sub_new()`
- `zlink_spot_node_connect_peer_pub()` 기반 수동 mesh
- `zlink_spot_node_set_discovery()` 기반 자동 mesh
- `zlink_spot_sub_subscribe()` / `subscribe_pattern()` 후 `recv()`
- `zlink_poller_add_spot_sub()` / `zlink_poller_add_spot_pub()` 통합 사용
- `zlink_spot_sub_set_handler()` 기반 callback 수신

예외:

- `zlink_spot_node_connect_registry()`는 재작성에서 제거 대상이다
- guide의 기존 registry 예제는 구현 완료 시 새 API에 맞게 갱신한다

## 3. 비목표

이번 재작성에서 다음은 목표가 아니다.

- 기존 내부 구현과의 호환성 유지
- 기존 node-local queue / local dispatch 모델 유지
- hidden async queue로 성능을 가리는 방식
- 기존 성능 저하 구현 위에 미세 튜닝 추가

## 4. 요구사항

### 4.1 기능 요구사항

- 같은 process / 같은 ctx 안에서 `SpotPub -> SpotSub` local fanout이 동작해야 한다.
- 서로 다른 node 간 `SpotPub -> remote SpotSub`가 동작해야 한다.
- local / remote 여부와 무관하게 topic filter semantics가 동일해야 한다.
- same-process / cross-node 모두 MMORPG zone adjacency 시나리오를 만족해야 한다.
- `recv()`와 `handler`는 상호 배타여야 한다.
- poller / peers / monitor는 facade raw socket 의미를 가져야 한다.

### 4.2 성능 요구사항

- `SpotPub publish` hot path는 `send -> local proxy ingress` 수준이어야 한다.
- payload는 node service layer에서 `recv -> vector -> copy -> send`로 재구성하지 않는다.
- control task wakeup이 publish 1건마다 필수로 개입하면 안 된다.
- multi-client perf는 기존 queue 모델과 동급 또는 근접해야 한다.
- 허용 오버헤드는 본질적으로 `inproc hop + proxy copy` 수준이어야 한다.

## 5. 최종 아키텍처

### 5.1 Node 역할 분리

재작성 후 `spot_node`의 역할은 2개로 제한한다.

- control plane
  - registry connect
  - discovery refresh
  - heartbeat
  - peer endpoint connect/disconnect
- data plane owner
  - local ingress bridge owner
  - local fanout bridge owner
  - mesh publish / mesh receive socket owner
  - proxy worker owner

`spot_node`는 더 이상 다음을 하지 않는다.

- local publish를 직접 recv해서 재조립
- local sub마다 직접 dispatch
- per-sub local queue 관리
- pending publish queue 관리
- node-owned local topic index 유지

### 5.2 Socket topology

node당 소켓 구성은 아래로 고정한다.

#### control plane

- `_dealer`
  - type: `DEALER`
  - 용도: registry/control

#### data plane

- `_mesh_pub`
  - type: `PUB`
  - 용도: 외부 node로 publish
  - bind 대상: `zlink_spot_node_bind()`

- `_mesh_xsub`
  - type: `XSUB`
  - 용도: 외부 node의 `PUB` endpoint 구독 수신
  - connect 대상: `zlink_spot_node_connect_peer_pub()`

- `_local_pub_ingress_sub`
  - type: `SUB`
  - bind: `inproc://zlink.spot.<node-id>.pub-in`
  - subscribe: `""`
  - 용도: 모든 `SpotPub`의 local publish ingress

- `_local_fanout_xpub`
  - type: `XPUB`
  - bind: `inproc://zlink.spot.<node-id>.sub-out`
  - 용도: 모든 `SpotSub` fanout 대상

#### internal socket policy

- `_local_pub_ingress_sub`
  - `RCVHWM=0`
  - `RCVTIMEO=-1`
  - 내부 ingress가 facade `PUB`보다 먼저 병목이 되지 않게 한다

- `_local_fanout_xpub`
  - `SNDHWM=spot_sub_queue_hwm_default` 또는 node 공통 override
  - `RCVHWM=0`
  - `XPUB_NODROP=1`
  - local fanout에서 hidden drop이 발생하지 않게 한다
  - slow `SpotSub`가 있으면 worker가 block되고 backpressure가 상류로 전파된다

- `_mesh_pub`
  - `SNDTIMEO=-1`
  - transport/socket 기본 HWM 또는 node 공통 override 사용

- `_mesh_xsub`
  - `RCVHWM=0`
  - `SNDTIMEO=-1`
  - subscription propagation과 remote receive에서 hidden drop을 만들지 않는다

위 정책의 핵심은 다음이다.

- 내부 bridge socket이 facade/public socket보다 먼저 병목이 되면 안 된다
- hidden queue/drop으로 성능을 꾸미지 않는다
- backpressure는 가능한 한 facade/API 호출자에게 전파한다
- 따라서 `_local_fanout_xpub`는 무제한 버퍼가 아니라 유한 HWM + `XPUB_NODROP`
  정책을 사용한다

trade-off:

- local slow subscriber 하나가 node-local fanout 전체를 늦출 수 있다
- 이는 hidden drop이나 무제한 메모리 증가보다 명시적 backpressure를 선택한 결과다
- slow consumer 격리가 필요하면 라이브러리 내부 queue가 아니라 사용자 queue로 분리한다

#### data plane control

- `_data_ctrl_front` / `_data_ctrl_back`
  - type: `PAIR`
  - 용도: data plane worker command channel
  - 명령:
    - `bind_pub(endpoint)`
    - `connect_peer_pub(endpoint)`
    - `disconnect_peer_pub(endpoint)`
    - `terminate()`
  - 응답:
    - `ok`
    - `error(errno)`

### 5.3 Facade topology

#### SpotPub

- `_socket`
  - type: `PUB`
  - connect: `inproc://zlink.spot.<node-id>.pub-in`

#### SpotSub

- `_socket`
  - type: `SUB`
  - connect: `inproc://zlink.spot.<node-id>.sub-out`

### 5.4 ASCII 아키텍처

#### 단일 node 내부 구조

```text
Application Threads
   |                         +----------------------+
   | publish()              |    spot control task |
   v                        | discovery/heartbeat  |
+----------+               +----------------------+
| SpotPub  |
| PUB      |-- inproc --> (_local_pub_ingress_sub)
+----------+                         |
                                     |
                                     v
                          +------------------------+
                          | spot data plane worker |
                          | poll/forward only      |
                          +------------------------+
                            |            |            ^
                            |            |            |
                            v            v            |
                    (_mesh_pub)   (_local_fanout_xpub)-+
                        |                 |
                        |                 +--> inproc --> +----------+
                        |                                 | SpotSub  |
                        |                                 | SUB      |
                        |                                 +----------+
                        |
                        +--> remote nodes
```

#### 두 node 사이 mesh 구조

```text
Node A                                              Node B

+----------+                                        +----------+
| SpotPub  |                                        | SpotSub  |
| PUB      |                                        | SUB      |
+----------+                                        +----------+
     |                                                   ^
     v                                                   |
(_local_pub_ingress_sub)                           (_local_fanout_xpub)
     |                                                   ^
     v                                                   |
[data plane worker] --(_mesh_pub)==== network ====(_mesh_xsub)-- [data plane worker]
     |                                                   |
     +------> (_local_fanout_xpub)                       +--> local fanout only
```

#### subscription propagation 경로

```text
SpotSub SUBSCRIBE(topic)
    |
    v
facade SUB
    |
    | inproc subscription frame
    v
_local_fanout_xpub
    |
    | read by data plane worker
    v
_mesh_xsub
    |
    | upstream subscription replay/propagation
    v
remote peer PUB publishers
```

#### handler dispatch 경로

```text
remote/local publish
    |
    v
facade SpotSub SUB socket
    |
    | sub_dispatch_start()
    v
socket I/O path (io thread)
    |
    v
user handler(topic, parts...)
```

## 6. Data plane 설계

### 6.1 단일 data plane worker

data plane은 thread 1개가 전담한다.

이 worker는 다음 소켓의 유일한 owner다.

- `_mesh_pub`
- `_mesh_xsub`
- `_local_pub_ingress_sub`
- `_local_fanout_xpub`
- `_data_ctrl_back`

이 결정은 필수다.

- `XPUB`를 tx/rx 두 thread가 동시에 사용하면 socket owner 규칙이 깨진다
- `connect` / `disconnect` / `bind`도 owner thread에서 수행돼야 한다
- 따라서 "proxy thread 2개"보다 "proxy-style worker 1개"가 맞다

### 6.2 forward 규칙

data plane worker는 poller loop에서 아래 3개 방향만 처리한다.

#### A. local publish ingress -> remote mesh + local fanout

- source: `_local_pub_ingress_sub`
- destination 1: `_mesh_pub`
- destination 2: `_local_fanout_xpub`

동작:

- `SpotPub`가 local ingress에 넣은 multipart를 worker가 읽는다
- 첫 destination으로는 원본 `msg_t` part를 그대로 forward 한다
- 두 번째 destination으로는 `msg_copy` 기반 shallow copy(refcount increment)로 forward 한다
- `std::vector<msg_t>` 재조립이나 payload deep copy는 하지 않는다

#### B. remote mesh -> local fanout

- source: `_mesh_xsub`
- destination: `_local_fanout_xpub`

동작:

- remote peer에서 들어온 multipart를 worker가 읽는다
- local subscriber fanout만 수행한다
- 다시 `_mesh_pub`로 재발행하지 않는다

#### C. local subscription propagation -> remote mesh

- source: `_local_fanout_xpub`
- destination: `_mesh_xsub`

동작:

- local `SpotSub`의 subscribe / unsubscribe frame을 worker가 읽는다
- 같은 frame을 `_mesh_xsub`로 전달한다
- remote peer `PUB`까지 subscription이 자연스럽게 전파되게 한다

### 6.3 구현 형태

구현은 stock `zlink_proxy_steerable()`를 thread 2개로 띄우는 방식이 아니다.

이유:

- `_local_fanout_xpub`를 tx/rx 양쪽이 공유하게 되면 socket thread-safety를 깨뜨린다
- `bind/connect/disconnect`도 data plane socket owner thread에서 수행돼야 한다

따라서 `core/src/sockets/proxy.cpp`의 burst forwarding 방식을 재사용한
`spot_data_plane_loop()`를 별도 구현한다.

이 loop는 다음 성질을 가져야 한다.

- `msg_t` multipart를 직접 forward
- `std::vector<msg_t>` 재조립 금지
- local ingress / mesh xsub / xpub subscription frame을 한 poller loop에서 처리
- control command도 같은 loop에서 처리
- multipart send는 blocking 정책으로 처리하여 mid-message `EAGAIN` partial send 상태를 만들지 않는다
- non-recoverable send/recv 오류는 node fault로 간주하고 worker 종료 경로로 연결한다

즉, "proxy API 호출"이 아니라 "proxy 알고리즘을 data plane worker에 내장"하는 방식으로 구현한다.

### 6.4 왜 `XSUB`를 쓰는가

`mesh receive`를 `SUB`가 아니라 `XSUB`로 고정하는 이유는 다음과 같다.

- `proxy(frontend=XSUB, backend=XPUB)`가 성립한다
- local `XPUB` subscription frame을 upstream으로 자동 전달할 수 있다
- node가 별도 filter refcount를 계산해서 `setsockopt(SUBSCRIBE)`를 호출할
  필요가 없다

재작성 후 node는 local filter를 직접 집계하지 않는다.

구현 검증 항목:

- 신규 peer `PUB`가 `_mesh_xsub`에 늦게 연결되더라도, 기존 누적 subscription이
  새 pipe로 replay 되어야 한다
- 이는 late-connect peer가 이미 존재하던 topic을 즉시 받을 수 있게 하는 필수 조건이다
- zlink의 `XSUB` 구현이 이 동작을 실제로 보장하는지 재작성 시 테스트로 검증한다

### 6.5 Data plane sequence

#### local publish -> local fanout + remote mesh

```text
App Thread          SpotPub         inproc SUB         Data Worker
   |                  |                 |                  |
   | publish()        |                 |                  |
   |----------------->|                 |                  |
   |                  | send multipart  |                  |
   |                  |---------------->|                  |
   |                  | return          |                  |
   |<-----------------|                 |                  |
   |                  |                 | recv multipart   |
   |                  |                 |----------------->|
   |                  |                 |                  | send -> _mesh_pub
   |                  |                 |                  | send -> _local_fanout_xpub
```

#### remote mesh receive -> local fanout only

```text
Remote Node       _mesh_xsub       Data Worker      _local_fanout_xpub      SpotSub
    |                 |                |                    |                  |
    | publish ------->|                |                    |                  |
    |                 | recv multipart |                    |                  |
    |                 |--------------->|                    |                  |
    |                 |                | send multipart     |                  |
    |                 |                |------------------->|                  |
    |                 |                |                    | deliver/filter   |
    |                 |                |                    |----------------->|
```

#### subscribe propagation -> late peer 포함

```text
App Thread      SpotSub        _local_fanout_xpub    Data Worker      _mesh_xsub
   |               |                  |                  |                |
   | subscribe()   |                  |                  |                |
   |-------------->| setsockopt       |                  |                |
   |               |----------------->|                  |                |
   |               | return           |                  |                |
   |<--------------|                  | subscription msg |                |
   |               |                  |----------------->|                |
   |               |                  |                  | send --------->|
   |               |                  |                  |                |
   |               |                  |                  | new peer connect
   |               |                  |                  |-------------> XSUB replay required
```

#### handler active path

```text
Publish Path --> facade SpotSub SUB --> io thread dispatch --> user handler

Rules:
- handler active 동안 recv() 금지
- handler clear는 inflight callback 0까지 대기
- blocking handler는 다른 I/O 진행에 영향
```

## 7. Threading model

### 7.1 thread 역할

- `io_thread`
  - transport wire I/O
  - facade `SUB` direct dispatch callback

- `spot control task`
  - discovery refresh
  - heartbeat
  - peer connect/disconnect
  - registry interactions

- `spot data plane worker`
  - `_local_pub_ingress_sub` / `_mesh_xsub` / `_local_fanout_xpub` / `_mesh_pub` owner
  - proxy-style forwarding 수행
  - bind/connect/disconnect command 수행

### 7.2 금지 규칙

다음은 금지한다.

- control task에서 publish payload를 recv/send 하는 것
- control task에서 local sub callback을 호출하는 것
- sub별 전용 thread 생성
- publish 1건마다 control task wakeup을 강제하는 것
- data plane socket을 owner thread 외부에서 직접 조작하는 것

## 8. API 의미

### 8.1 `zlink_spot_pub_publish*`

동작:

1. `SpotPub` instance mutex 획득
2. topic 검증
3. facade `PUB` socket으로 multipart send
4. 반환

제약:

- `SpotPub` instance는 concurrent publish에 대해 thread-safe 해야 한다
- node control task wakeup 없음
- hidden async queue 없음
- `EAGAIN` / `SNDTIMEO` / `SNDHWM`은 facade `PUB` socket 의미를 따른다

### 8.1.1 `SpotPub` thread-safety 계약

재작성 후 `SpotPub`는 사용자 계약상 thread-safe로 구현한다.

최소 보장:

- 여러 스레드가 같은 `SpotPub` 인스턴스에 동시에 `publish()` 호출 가능
- `publish()`와 `peers()` / `routing_id()` / `monitor_open()`이 동시에 호출되어도
  내부 race가 없어야 함
- `set_option()`은 socket option 변경과 concurrent publish 사이에서 내부 상태가
  깨지지 않아야 함

구현 규칙:

- `spot_pub_t`는 per-instance mutex를 가진다
- facade socket 접근은 이 mutex로 보호한다
- node로의 indirect queue / helper thread 위임 없이, facade `PUB` socket send를
  직접 직렬화한다

destroy 규칙:

- `destroy()`는 다른 thread가 같은 `SpotPub`를 계속 사용하는 동안 안전한
  shared-lifetime를 제공할 필요는 없다
- 즉, 일반 handle과 마찬가지로 lifetime 종료는 호출자가 동기화해야 한다
- 단, destroy 내부 race로 인한 use-after-free가 발생하지 않도록 내부적으로는
  mutex와 detach 순서를 지켜야 한다

기존 async pub queue 옵션은 재작성에서 제거한다.

- 남길 public constant가 필요하면 `ENOTSUP`로 명확히 실패시킨다
- no-op success는 금지한다

### 8.1.2 Registry registration API 단순화

재작성에서는 아래 API를 제거한다.

- `zlink_spot_node_connect_registry()`

제거 이유:

- discovery 연결과 registration 연결이 분리돼 사용자 모델이 불필요하게 복잡하다
- 실제 사용에서는 `connect_registry() + register()`가 거의 항상 한 세트로 쓰인다
- registry control endpoint를 node에 미리 설정하는 2단계 API는 실익이 작다

대체 방향:

- `zlink_spot_node_register()`는 기존 시그니처를 유지한다
- node는 attached discovery가 bootstrap 과정에서 학습한 registry uplink endpoint를 재사용한다
- 성공 시 node는 그 uplink 연결을 heartbeat / unregister에 재사용한다

새 계약:

```c
int zlink_spot_node_register(void *node,
                             const char *service_name,
                             const char *advertise_endpoint);
```

동작:

1. attached discovery 존재 여부 확인
2. discovery가 registry uplink endpoint를 학습했는지 확인
3. publish bind 상태와 advertise endpoint 확인
4. discovery가 알고 있는 registry uplink로 내부 dealer 연결
5. register request 전송
6. 성공 시
   - `_registered=1`
   - `_registration_uplink_endpoint` 저장
   - heartbeat 시작

`zlink_spot_node_unregister()`는 저장된 uplink endpoint를 사용한다.

제약:

- register 전에는 heartbeat 없음
- unregister는 registered state가 아니면 `EINVAL`
- discovery가 attach되지 않았거나 uplink를 아직 학습하지 못했으면 `EFSM` 또는 `EAGAIN`
- register 중복 호출은 동일 service/advertise면 idempotent, 다르면 `EBUSY`

discovery와의 관계:

- `zlink_discovery_connect_registry()`는 계속 유지한다
- discovery는 peer discovery용인 동시에 registry bootstrap/uplink 정보를 제공한다
- node register/heartbeat는 attached discovery가 알아낸 uplink를 재사용한다
- 구현 후 discovery 없이 register만 하고 싶은 요구가 생기면, 그때 별도 explicit API를 추가한다

사용 예제:

```c
void *ctx = zlink_ctx_new();

/* peer discovery용 */
void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_SPOT);
zlink_discovery_connect_registry(discovery, "tcp://registry-pub:5551");
zlink_discovery_subscribe(discovery, "spot-node");

/* local/remote bridge owner */
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9000");

/* 자동 peer 연결 */
zlink_spot_node_set_discovery(node, discovery, "spot-node");

/* self registration + heartbeat 시작 */
zlink_spot_node_register(node, "spot-node", "tcp://node-a:9000");

void *pub = zlink_spot_pub_new(node);
void *sub = zlink_spot_sub_new(node);
zlink_spot_sub_subscribe(sub, "zone:12:*");
```

의미:

- discovery는 bootstrap 과정에서 registry PUB와 uplink/control 정보를 학습한다
- node register는 attached discovery가 학습한 uplink/control 정보를 재사용한다
- 사용자는 discovery를 연결한 뒤 register만 호출하면 된다

수동 mesh만 사용하는 경우:

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);

zlink_spot_node_bind(node, "tcp://*:9000");
zlink_spot_node_connect_peer_pub(node, "tcp://node-b:9000");
zlink_spot_node_connect_peer_pub(node, "tcp://node-c:9000");

void *pub = zlink_spot_pub_new(node);
void *sub = zlink_spot_sub_new(node);
```

## 8.2 `zlink_spot_sub_recv`

동작:

- facade `SUB` socket에서 직접 recv

제약:

- handler active이면 `EINVAL`
- node queue / signaler / helper thread 경유 금지

## 8.3 `zlink_spot_sub_subscribe` / `unsubscribe`

동작:

- facade `SUB` socket에 직접 `setsockopt(ZLINK_SUBSCRIBE/UNSUBSCRIBE)`

부수효과:

- local `SUB -> XPUB -> XSUB` subscription propagation은 소켓 계층이 처리한다
- node가 별도 `_filter_refcount`를 들고 관리하지 않는다

주의:

- 반환 시점은 “local facade socket이 filter를 수용했다”는 의미다
- cluster 전체 전파 완료를 보장하지 않는다

## 8.4 poller / peers / monitor

- `zlink_poller_add_spot_pub`
  - facade `PUB` raw socket을 poll 대상으로 사용

- `zlink_poller_add_spot_sub`
  - facade `SUB` raw socket을 poll 대상으로 사용

- `zlink_spot_pub_peers`
  - facade `PUB`의 local inproc peer 상태를 반환

- `zlink_spot_sub_peers`
  - facade `SUB`의 local inproc peer 상태를 반환

- `zlink_spot_pub_monitor_open`
- `zlink_spot_sub_monitor_open`
  - facade socket monitor 의미로 고정

주의:

- 위 API는 remote mesh topology를 보여주지 않는다
- remote peer 상태가 필요하면 별도 node introspection API를 추후 추가한다

## 9. Handler 설계

### 9.1 기본 원칙

handler는 별도 queue worker가 아니라 facade `SUB`의 direct dispatch 기능을 쓴다.

구현은 `socket_base_t::sub_dispatch_start()` / `sub_dispatch_stop()`를 사용한다.

즉:

- callback은 facade `SUB`의 socket dispatch 경로에서 바로 호출된다
- control task는 callback delivery에 관여하지 않는다

### 9.2 `set_handler(handler != NULL)`

동작:

1. `_recv_in_progress > 0`이면 `EBUSY`
2. `_handler_state == none`인지 확인
3. `_socket->sub_dispatch_start(&spot_sub_t::dispatch_from_io, this)` 호출
4. `_handler`, `_handler_userdata`, `_handler_state=active` 저장

### 9.3 dispatch callback

dispatch callback은 `msg_t *parts, size_t part_count`를 받는다.

`spot_sub_t::dispatch_from_io()`는 다음만 한다.

1. topic part / payload part 포인터 해석
2. `_callback_inflight++`
3. user handler 호출
4. `_callback_inflight--`
5. clear 대기 중이면 condition variable notify

금지:

- callback delivery를 위해 payload를 다시 복사하는 것
- node를 다시 경유하는 것

### 9.4 `set_handler(NULL, NULL)`

동작:

1. `_handler_state=clearing`
2. `_socket->sub_dispatch_stop()`
3. in-flight callback이 0이 될 때까지 대기
4. `_handler = NULL`
5. `_handler_state = none`

규칙:

- 현재 callback 내부에서 자기 자신을 clear하는 경우 deadlock 없이 즉시 반환 가능해야 한다
- clear 이후 새 callback이 시작되면 안 된다

### 9.5 문서 제약

가이드에는 다음을 명시한다.

- callback은 socket dispatch 경로에서 실행된다
- callback에서 blocking 작업 금지
- 필요하면 사용자가 callback 내부에서 자기 queue로 넘겨 별도 thread에서 처리

## 10. Lifecycle

### 10.1 node 생성

생성 순서:

1. `spot_node_t` 생성
2. endpoint 문자열 생성
3. `_data_ctrl_front/back` 생성
4. data plane worker 시작
5. worker thread 안에서 `_local_pub_ingress_sub`, `_local_fanout_xpub`,
   `_mesh_pub`, `_mesh_xsub` 생성 및 bind
6. worker ready ack 수신
7. `_dealer` 생성
8. control task 등록

node는 생성 시점에 data plane을 완전히 준비한 뒤 반환한다.

### 10.2 pub 생성

1. facade `PUB` 생성
2. node ingress endpoint connect
3. 필요한 socket option 적용
4. facade peer count가 1이 될 때까지 최대 `1000ms` bounded wait
5. 반환

실패 규칙:

- timeout 시 `ETIMEDOUT`
- monitor/peer error가 먼저 관측되면 해당 `errno`

### 10.3 sub 생성

1. facade `SUB` 생성
2. node fanout endpoint connect
3. 필요한 socket option 적용
4. facade peer count가 1이 될 때까지 최대 `1000ms` bounded wait
5. 반환

`ready barrier`는 고정 sleep이 아니라 facade peer/monitor 기반 bounded wait로 구현한다.

### 10.4 node API의 thread ownership 규칙

다음 API는 data plane worker에 command를 보내고, 완료 ack를 기다린 뒤 반환한다.

- `zlink_spot_node_bind`
- `zlink_spot_node_connect_peer_pub`
- `zlink_spot_node_disconnect_peer_pub`

즉, 호출 스레드나 control task가 `_mesh_pub` / `_mesh_xsub`를 직접 조작하지 않는다.

다음 API는 control plane 경로를 통해 직접 처리한다.

- `zlink_spot_node_register`
- `zlink_spot_node_unregister`
- `zlink_spot_node_set_discovery`

반환 규칙:

- worker가 성공 응답을 보내면 API는 `0` 반환
- worker가 `error(errno)` 응답을 보내면 API는 그 `errno`로 실패
- bounded timeout 내 응답이 없으면 `ETIMEDOUT`로 실패

### 10.5 destroy 순서

`spot_node_destroy()` 순서는 아래로 고정한다.

전제조건:

- 호출자는 `spot_node_destroy()` 전에 관련 `SpotPub` / `SpotSub`의 외부 사용을 중단해야 한다
- lifetime 종료와 동시 사용까지 라이브러리가 보장하지는 않는다

1. `_stop=1`
2. control task 제거
3. data control에 `terminate`
4. data plane worker join
5. 남아 있는 `SpotPub` / `SpotSub` detach 및 close
6. mesh/local/control socket close
7. 내부 집합/endpoint 정리

`remove_task()` 없이 `_task_id=0`만 지우는 방식은 금지한다.

## 11. 내부 상태 구조

### 11.1 `spot_node_t`에서 제거할 필드/로직

재작성 시 아래 항목은 삭제 대상이다.

- `_pending_pub`
- `_pub_queue_sync`
- `_pub_queue_hwm`
- `_pub_mode`
- `_pub_queue_full_policy`
- `_filter_refcount`
- `_pending_subscribe`
- `_pending_unsubscribe`
- `dispatch_local()`
- `process_local_pub_ingress()`
- `process_async_publish()`
- `process_sub()`
- `forward_topic_payload()`
- `pump_facade_io()`
- 기존 `_local_sub_fanout_xpub` 이름과 그에 연결된 helper 로직

### 11.2 `spot_node_t`에 새로 필요한 필드

- `_mesh_pub`
- `_mesh_xsub`
- `_local_pub_ingress_sub`
- `_local_fanout_xpub`
- `_data_ctrl_front`
- `_data_ctrl_back`
- `_data_plane_thread`
- `_pub_ingress_endpoint`
- `_sub_fanout_endpoint`

### 11.3 `spot_pub_t`

필수 필드:

- `_node`
- `_socket`
- `_sync`
- `_tag`

더 이상 필요 없는 필드/로직:

- internal publish queue 관련 상태
- node control tick request

### 11.4 `spot_sub_t`

필수 필드:

- `_node`
- `_socket`
- `_topics`
- `_patterns`
- `_handler`
- `_handler_userdata`
- `_handler_state`
- `_recv_in_progress`
- `_callback_inflight`
- `_callback_sync`
- `_callback_cv`

삭제 대상:

- node helper thread 관련 상태
- local queue / helper join 관련 상태

## 12. 파일 단위 재작성 범위

### 12.1 전면 재작성

- `core/src/services/spot/spot_data_plane.cpp`
- `core/src/services/spot/spot_data_plane.hpp`
- `core/src/services/spot/spot_node.cpp`
- `core/src/services/spot/spot_node.hpp`
- `core/src/services/spot/spot_pub.cpp`
- `core/src/services/spot/spot_pub.hpp`
- `core/src/services/spot/spot_sub.cpp`
- `core/src/services/spot/spot_sub.hpp`

### 12.2 API 정리

- `core/src/api/zlink.cpp`
- `core/include/zlink.h`

정리 내용:

- `zlink_spot_node_connect_registry()` 제거
- `zlink_spot_node_register()` 내부 동작을 discovery-uplink 재사용 방식으로 변경
- queue 관련 option 설명 정리
- handler thread 문구를 `io_context`가 아니라 socket dispatch / io path로 수정

### 12.3 테스트 재작성

- `core/tests/spot/test_spot_mode_split.cpp`
- `core/tests/spot/test_spot_service_introspection.cpp`
- `core/tests/spot/test_spot_pubsub_scenario.cpp`

### 12.4 perf 검증

- `core/perf/single/src/perf_spot.cpp`
- `core/perf/multi/src/perf_spot_client.cpp`
- `core/perf/multi/src/perf_spot_server.cpp`

## 13. 테스트 스펙

### 13.1 구조 테스트

- local same-process publish/recv
- remote mesh publish/recv
- multiple local pubs fan-in
- multiple local subs fanout
- peer connect/disconnect 후 recovery

### 13.2 API semantics 테스트

- facade poller readiness 일치
- facade peer/monitor 의미 일치
- handler active 시 `recv == EINVAL`
- clear barrier 동작
- callback 내부 self-clear 동작
- queue 옵션 `ENOTSUP` 확인

### 13.3 MMORPG 시나리오 테스트

- single-node deterministic adjacency
- multi-node deterministic adjacency via discovery
- local/cross-node 위치 투명성 exact set 검증
- non-adjacent zone non-delivery 검증

### 13.4 stress / perf

- single SPOT perf
- multi SPOT perf
- env-gated large adjacency scenario

성능 acceptance는 다음을 본다.

- single / multi 모두 기존 queue 모델 대비 대폭 열화가 없어야 한다
- 현재처럼 order-of-magnitude regression이 나오면 구현 실패로 본다

## 14. 구현 순서

### Phase 1. skeleton

1. 기존 `spot_node/pub/sub` 구현 제거
2. 새 socket topology 생성
3. data plane worker만 올린 최소 경로 구현
4. local publish/recv smoke 통과

### Phase 2. facade semantics

1. pub/sub poller integration 복구
2. peer/monitor facade 의미 복구
3. subscribe/unsubscribe direct semantics 복구
4. recv path 복구

### Phase 3. handler

1. `sub_dispatch_start/stop` 기반 handler 구현
2. recv/handler 상호배타
3. clear barrier

### Phase 4. control plane

1. bind/connect/discovery/register/heartbeat 복구
2. multi-node discovery 시나리오 복구

### Phase 5. perf / cleanup

1. perf baseline 측정
2. 불필요한 copy / lock / wakeup 제거
3. guide / api 문서 업데이트

## 15. 승인 기준

다음이 모두 만족돼야 재작성 완료로 본다.

- publish hot path에서 control task 개입이 없다
- node가 payload를 재조립하지 않는다
- local/remote fanout이 proxy 경로로만 동작한다
- handler는 facade `SUB` direct dispatch를 사용한다
- deterministic adjacency 테스트 통과
- env-gated large scenario 통과
- perf가 기존 queue 모델과 동급 또는 근접하다

## 16. 기존 계획 문서와의 관계

이 문서는 [`spot-inproc-facade-redesign.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/spot-inproc-facade-redesign.ko.md)를 대체하는 후속 스펙이다.

기존 문서는 facade 의미 변경 방향을 정의하는 데는 유효했지만,
실제 구현이 `proxy` 수준 data path를 보장하지 못했다.

이번 재작성은 “facade inproc화”가 아니라 “proxy 기반 data plane 재구축”이 핵심이다.
