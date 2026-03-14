# SPOT Data/Control Plane 구조 개편 계획

> 상태 메모
> 이 문서는 2026-03-14 기준 SPOT 성능 회귀를 구조적으로 해소하기 위한
> 상세 설계안이다.
> 현재 기준 성능은 `main`의 single 보고서
> `core/perf/results/single/report/perf_linux_20260314_003239.txt`를 따른다.
> 최근 rewrite 단일 측정에서 SPOT `tcp/131072`는 `31.546 Kmsg/s`,
> `tcp/262144`는 `14.324 Kmsg/s` 수준이며,
> 같은 구간의 main 기준은 각각 `46.204 Kmsg/s`, `25.550 Kmsg/s`다.
> 즉 현재 rewrite는 main 대비 대략 `0.68x`, `0.56x` 수준이다.

## 1. 목적

이 문서의 목적은 다음 두 가지를 동시에 만족하는 SPOT 구조 개편안을
정의하는 것이다.

- SPOT single 성능을 먼저 `main` 근사치까지 회복한다.
- monitor/readiness 계약을 perf 전용 우회 없이 유지한다.

핵심 방향은 한 줄로 요약된다.

```text
SPOT의 data plane과 peer control plane을 분리한다.
```

즉 현재 `mesh_pub = XPUB`에 실려 있는 역할을 쪼개서,

- data는 다시 `PUB/XSUB` 기반의 단순 fast path로 되돌리고
- subscription/readiness/ack는 별도 peer control plane으로 이동한다.

## 2. 배경과 문제 정의

현재 성능 회귀는 SPOT 전체가 아니라
`remote peer mesh steady-state path`에 집중되어 있다.

확인된 사실:

- local-only SPOT은 빠르다.
- raw `PUBSUB`는 main과 큰 차이가 없다.
- 현재 병목은 SPOT의 remote peer mesh 경로다.

즉 문제는 callback 자체나 local facade가 아니라,
현재 rewrite의 SPOT data/control 배선 방식이다.

### 2.1 main과 current의 본질적 차이

`main`의 SPOT mesh sender는 본질적으로 data 전용이다.

- `mesh_pub = PUB`
- `mesh_xsub = XSUB`
- `fanout = XPUB`

반면 current rewrite는 `mesh_pub`에 control 책임이 섞여 있다.

- `mesh_pub = XPUB`
- remote subscription 수집
- `ready_probe`
- `ready_ack`
- `FIRST_DELIVERY_READY_CHANGED` 계산 보조

이 결과, 현재 rewrite는 remote data sender가 steady-state에서도
`XPUB` 경로 비용을 계속 짊어진다.

### 2.2 현재까지 유효한 개선과 한계

이미 유지 가치가 확인된 core 변경은 남겨 둔다.

- attachment socket pointer cache
- `mesh_pub`/`mesh_xsub` I/O thread affinity 분리
- local `fanout` XPUB subscription inbox drain

이 변경들은 hot path에서 lock/map 또는 backlog를 줄였지만,
근본 원인인 `data/control 혼합` 자체는 그대로다.
즉 미세 최적화만으로는 main 근사치까지 복구되지 않는다.

## 3. 설계 목표

### 3.1 목표

- SPOT data fast path를 `main`과 같은 성격으로 단순화한다.
- monitor/readiness 계약을 유지한다.
- 추가 네트워크 소켓은 `spot handle`별이 아니라 `spot_node`별로 둔다.
- benchmark harness 전용 분기 없이 core 내부 구조로 해결한다.
- hidden default child/default sub 같은 내부 전제에 기대지 않는다.

### 3.2 비목표

- large payload zero-copy 같은 bench-only 최적화
- perf 전용 shortcut 또는 test harness 특화 코드
- monitor 계약을 약하게 만들어 수치를 맞추는 것
- child handle마다 peer socket을 추가하는 것
- 첫 단계에서 public API를 무리하게 크게 바꾸는 것

## 4. 현재 구조가 느린 이유

현재 rewrite의 문제는 단순히 `XPUB`가 느리다는 차원이 아니다.
더 정확한 문제는 다음과 같다.

### 4.1 data socket이 control 상태를 함께 운반한다

현재 `mesh_pub`는 remote peer로 payload를 보내는 socket인 동시에,
아래 상태를 반영하는 관측점이기도 하다.

- remote peer subscription 유무
- ready probe 대상
- ready ack 집계

이 구조에서는 data socket을 data-only로 최적화할 수 없다.

### 4.2 readiness 계약이 socket side-effect에 묶여 있다

monitor가 노출하는 강한 의미는 다음과 같다.

- `SPOT_SUB_FILTER_APPLIED`
- `SPOT_SUB_DELIVERY_READY_CHANGED`
- `SPOT_PUB_DELIVERY_READY_CHANGED`
- `SPOT_PUB_FIRST_DELIVERY_READY_CHANGED`

문제의 본체는 monitor callback 오버헤드가 아니라,
이 상태를 remote peer 사이에서 판별하고 합의해야 한다는 점이다.
현재는 그 합의 메커니즘이 data mesh에 얹혀 있다.

### 4.3 steady-state path에 불필요한 분기와 polling이 남는다

현재 data plane thread는 payload forwarding 외에도
peer readiness/control 관련 입력을 함께 poll하고 분기한다.
그래서 payload가 큰 구간일수록 회귀가 크게 드러난다.

## 5. 핵심 설계 원칙

### 5.1 plane 분리

SPOT는 아래 두 plane으로 나눈다.

- data plane: payload 전달만 담당
- peer control plane: subscription/readiness/state sync 담당

### 5.2 node 단위 소유

추가 소켓은 `spot_pub_t`/`spot_sub_t`별이 아니라
`spot_node_t`가 공용으로 소유한다.

즉 handle이 늘어도 peer control socket 수는 늘지 않는다.

### 5.3 monitor 의미는 유지하되, 근거는 socket 부수효과가 아니라 명시적 protocol로 바꾼다

현재는 `XPUB` subscription 흐름을 해석해 monitor 상태를 만든다.
개편 후에는 peer control message를 기준으로
subscription 적용/ready ack/peer loss를 판단한다.

### 5.4 data fast path는 payload forwarding만 남긴다

개편 후 data plane에서 steady-state payload 경로는 아래만 남긴다.

- ingress에서 data 수신
- local fanout으로 전달
- remote mesh로 전달

control 관련 bookkeeping은 fast path 밖으로 뺀다.

## 6. 대안 검토

### 6.1 대안 A: 현재 `mesh_pub = XPUB` 유지 후 미세 최적화 계속

장점:

- 코드 변화 범위가 작다.

단점:

- data/control 혼합이 그대로 남는다.
- steady-state data socket이 계속 `XPUB` 경로를 탄다.
- 이미 여러 차례의 미세 최적화로도 main 근사치 복구에 실패했다.

결론:

- 기각한다.

### 6.2 대안 B: data `PUB/XSUB` + control `XPUB/XSUB`

장점:

- 현재 control 의미를 옮기기 쉽다.
- socket family 차이가 작아서 이식 부담이 낮다.

단점:

- control plane도 다시 subscription side-effect에 기대기 쉽다.
- command/ack 의미를 topic/subscription으로 우회 표현하게 된다.
- long-term 관점에서 또 다른 암묵적 coupling이 생길 수 있다.

결론:

- 전환용 임시 단계로는 가능하다.
- 최종 구조로 채택하지는 않는다.

### 6.3 대안 C: data `PUB/XSUB` + 별도 peer control plane

control plane socket family 후보는 두 가지다.

- `PUB/SUB` 기반 명시적 control topic
- `DEALER/ROUTER` 기반 addressed RPC

`DEALER/ROUTER`는 command/ack 의미에는 잘 맞지만,
node-level 공유 socket으로 여러 peer를 다루려면
identity/bootstrap/routing 관리가 커진다.
현재 문제는 control 의미의 부재가 아니라
control이 data plane에 섞여 있다는 점이므로,
이번 리팩터의 1차 채택안은 더 단순한 `PUB/SUB` control plane으로 둔다.

결론:

- 최종 채택안은 `data PUB/XSUB + peer control PUB/SUB`다.
- 향후 control semantics가 더 복잡해질 경우에만
  `ROUTER` 계열로 재검토한다.

## 7. 채택안

### 7.1 목표 토폴로지

개편 후 SPOT topology는 다음과 같다.

```text
                 +----------------------+
                 |      spot_node       |
                 |                      |
pub handle ----> | local pub ingress    | --+
                 |                      |   |
sub handle <---- | local fanout         | <-+
                 |                      |
                 | data mesh pub  (PUB) | ----> peer data xsub
                 | data mesh xsub (XSUB)| <---- peer data pub
                 |                      |
                 | ctrl pub       (PUB) | ----> peer ctrl sub
                 | ctrl sub       (SUB) | <---- peer ctrl pub
                 +----------------------+
```

핵심은 다음 두 줄이다.

- `mesh_pub`는 다시 pure data sender `PUB`로 되돌린다.
- readiness/subscription/ack는 별도 `ctrl_pub`/`ctrl_sub`로 옮긴다.

### 7.2 왜 `PUB/SUB` control plane인가

이번 설계에서 control plane은
downstream subscription 관측이 아니라 명시적 message 교환이 목적이다.
따라서 `XPUB`의 핵심 기능이 더 이상 필요하지 않다.

`PUB/SUB` control plane의 장점:

- node-level 공유 socket 쌍으로 충분하다.
- target node/topic 단위 필터링이 가능하다.
- data plane과 동일한 connect/bind 운영 모델을 재사용할 수 있다.
- `ROUTER`류 identity 관리보다 구현 부담이 작다.

## 8. 상세 아키텍처

### 8.1 runtime 확장

`spot_runtime_t`에 아래 필드를 추가한다.

- `peer_ctrl_pub`
- `peer_ctrl_sub`
- `peer_ctrl_pub_endpoint`
- `peer_ctrl_node_id`

기존 내부 command 채널인 `data_ctrl_front`/`data_ctrl_back`은 그대로 유지한다.
즉 worker thread는 여전히 하나이며,
같은 thread가 data plane과 peer control plane을 함께 poll한다.

다만 같은 poll loop를 쓰더라도 처리 규칙은 분리한다.

- data socket 입력을 먼저 소진한다.
- control socket 입력은 tick당 bounded batch로만 처리한다.
- control churn이 큰 경우를 검증 항목에 별도로 넣는다.

즉 초기 구현은 단일 worker thread를 유지하되,
control 처리량이 data fast path를 잠식하지 않도록 budget을 둔다.
이 budget으로도 churn 시 throughput 회귀가 남으면,
그때 control worker 분리를 2차 fallback으로 검토한다.

`peer_ctrl_node_id`는 endpoint에서 유도하지 않는다.
현재 runtime이 이미 가지는 session-scoped `node_id`를 재사용하거나,
같은 수준의 명시적 난수 id를 사용한다.

### 8.2 endpoint 정책

현재 runtime은 아래 inproc endpoint를 가진다.

- `pub_ingress_endpoint`
- `sub_fanout_endpoint`
- `data_ctrl_endpoint`

여기에 peer control용 network endpoint를 추가한다.

- `peer_ctrl_pub_endpoint`

중요한 원칙:

- control endpoint는 data endpoint에서 숨겨서 유도하지 않는다.
- discovery 또는 bootstrap message로 명시적으로 전달한다.
- control endpoint의 transport/security mode는 paired data endpoint와 같아야 한다.
- `tcp://`-`tcp://`, `tls://`-`tls://`, `ws://`-`ws://`, `wss://`-`wss://`처럼
  같은 family로 맞춘다.
- secure/insecure 혼합 쌍은 허용하지 않는다.
- 동일 peer에 대해 data와 control이 서로 다른 transport family를 쓰는 구성은
  invalid configuration으로 본다.

### 8.3 control message는 typed protocol로 보낸다

control plane은 reserved topic prefix와 multipart frame으로 구성한다.

아래 prefix는 library reserved namespace로 선언한다.

- `__zlink.spot.ctrl.`
- `__zlink.spot.bootstrap.`

public SPOT publish/subscribe subject가 이 prefix를 사용하는 것은
invalid input으로 취급한다.

frame 예시:

1. topic
2. source node id
3. target node id 또는 broadcast marker
4. verb-specific payload

모든 control message는 공통 메타를 함께 가진다.

- `protocol_version`
- `subscription_epoch`
- `control_sequence_no`
- `snapshot_generation` 또는 `0`

`ctrl_sub` subscription 규칙:

- 첫 frame의 topic은 verb만이 아니라 routing class를 포함한다.
- 예:
  - `__zlink.spot.ctrl.broadcast.ready_probe`
  - `__zlink.spot.ctrl.node.<target-node-id>.ready_ack`
- 모든 node는 broadcast control prefix를 구독한다.
- 모든 node는 자신의 `peer_ctrl_node_id`를 target으로 하는 prefix를 구독한다.
- 나머지 filtering은 topic과 payload의 verb/type으로 판별한다.

예시 topic:

- `__zlink.spot.ctrl.broadcast.hello`
- `__zlink.spot.ctrl.node.<target-node-id>.hello_ack`
- `__zlink.spot.ctrl.broadcast.heartbeat`
- `__zlink.spot.ctrl.node.<target-node-id>.sub_snapshot_begin`
- `__zlink.spot.ctrl.node.<target-node-id>.sub_snapshot_item`
- `__zlink.spot.ctrl.node.<target-node-id>.sub_snapshot_end`
- `__zlink.spot.ctrl.broadcast.sub_delta_add`
- `__zlink.spot.ctrl.broadcast.sub_delta_remove`
- `__zlink.spot.ctrl.node.<target-node-id>.ready_probe`
- `__zlink.spot.ctrl.node.<target-node-id>.ready_ack`
- `__zlink.spot.ctrl.node.<target-node-id>.resync_request`
- `__zlink.spot.ctrl.broadcast.peer_lost`
- `__zlink.spot.bootstrap.ctrl_descriptor`

receiver는 peer별 마지막 `control_sequence_no`를 추적한다.
같은 `subscription_epoch`/`snapshot_generation` 안에서 gap이 보이거나,
heartbeat timeout이 나면 해당 peer를 `ctrl_desynced`로 내리고
`RESYNC_REQUEST`를 보낸 뒤 snapshot 재동기화를 강제한다.
`RESYNC_REQUEST` 자체도 lossy일 수 있으므로,
desynced peer는 heartbeat timeout마다 같은 요청을 재전송한다.
또한 heartbeat에는 현재 `subscription_epoch`와
`snapshot_generation`을 함께 실어,
송신측과 수신측이 epoch mismatch를 스스로 감지하면
명시적 요청이 없어도 snapshot push를 시작할 수 있게 한다.

### 8.4 payload data path

steady-state payload path는 아래로 단순화한다.

1. local pub ingress가 payload를 받는다.
2. local subscriber가 있으면 `fanout`으로 전달한다.
3. remote peer가 있으면 `mesh_pub(PUB)`로 전달한다.

이 경로에서 제거되는 책임:

- remote subscription count 집계
- ready ack topic 생성/해석
- `mesh_pub` subscription inbox drain

remote peer가 payload forwarding 대상이 되는 조건은 명시적으로 제한한다.

- data 연결 완료
- control 연결 완료
- `HELLO`/`HELLO_ACK` 완료
- subscription snapshot sync 완료

즉 peer가 `bootstrap_pending` 또는 `ctrl_desynced` 상태면
해당 peer로의 remote payload forwarding은 하지 않는다.
이 구간에서 payload를 별도 queue에 보관하지도 않는다.
정책은 `hold without buffering`이 아니라 `not yet eligible for forwarding`이다.

예외:

- manual bootstrap용 reserved system topic 송신은 이 gating의 예외다.
- 이 경로는 사용자 payload가 아니라 peer bootstrap control metadata 전용이다.

### 8.5 subscription propagation

local sub가 새 filter를 등록하면 `spot_node_t`는
peer control plane에 `SUB_DELTA_ADD`를 broadcast한다.
unsubscribe는 `SUB_DELTA_REMOVE`를 broadcast한다.

peer가 새로 연결되면 다음 순서로 맞춘다.

1. `HELLO`
2. `HELLO_ACK`
3. local subscription snapshot 송신
4. snapshot 완료 확인
5. 이후 delta만 전송

이때 snapshot과 delta를 구분하기 위해 epoch를 둔다.

- `subscription_epoch`
- `snapshot_generation`
- `control_sequence_no`

peer는 더 오래된 epoch의 delta를 무시한다.
sequence gap이 감지되면 delta 적용을 중단하고
`RESYNC_REQUEST` 이후 새 snapshot 완료 전까지
그 peer의 subscription mirror를 authoritative하지 않은 것으로 본다.

### 8.6 readiness / first-delivery-ready

현재 readiness 계약은 유지한다.
단, 근거가 `mesh_pub XPUB` side-effect가 아니라 control protocol로 바뀐다.

#### sub readiness

- local sub filter 등록
- control plane으로 `SUB_DELTA_ADD`
- peer가 적용 후 `READY_PROBE` 또는 `READY_ACK` 절차 수행
- local node가 `FILTER_APPLIED` / `DELIVERY_READY_CHANGED`를 emit

#### pub readiness

- local pub는 subject별로 `ready source set`을 유지한다.
- remote peer가 자신이 그 subject를 전달 가능하다고 판단하면
  `READY_ACK`를 publish한다.
- local node는 peer node id 단위로 ready source set을 갱신한다.
- set cardinality를 기반으로
  `DELIVERY_READY_CHANGED`와 `FIRST_DELIVERY_READY_CHANGED`를 emit한다.

### 8.7 disconnect / loss 처리

peer control 또는 data 연결이 끊기면 해당 peer의 state를 즉시 내린다.

- peer가 제공하던 ready source 제거
- peer가 가진 subscription mirror 제거
- 필요 시 facade monitor에 peer down / ready down 전파

`ctrl_desynced` 진입도 같은 보수 정책을 따른다.
즉 연결은 살아 있어도 해당 peer의 ready source는 즉시 제거하고,
resync 완료 후 새 snapshot/ack 기준으로만 다시 구성한다.

disconnect는 retry loop가 아니라 state 전이로 본다.
복구는 새 `HELLO`/snapshot cycle로 다시 시작한다.

## 9. bootstrap / discovery / manual peer 연결

### 9.1 discovery 기반 연결

discovery provider metadata에 아래를 함께 넣는다.

- peer data pub endpoint
- peer ctrl pub endpoint
- peer control node id
- protocol version

discovery-managed peer는 이 metadata를 보고
data와 control을 각각 connect한다.

### 9.2 manual `connect_peer_pub()` 호환

manual peer 연결은 현재 data endpoint만 인자로 받는다.
hidden endpoint 유도는 금지이므로, control endpoint를 data endpoint 문자열에서
계산해서는 안 된다.

따라서 manual path는 다음 bootstrap 절차를 둔다.

1. 우선 data peer만 연결한다.
2. reserved system topic으로 자신의 control descriptor를 보낸다.
3. 상대도 같은 방식으로 control descriptor를 보낸다.
4. 양쪽이 `ctrl_pub`/`ctrl_sub`를 연결한다.
5. `HELLO`/snapshot sync가 끝나면 peer를 forwarding eligible로 승격한다.
6. 이후 steady-state control은 data plane을 더 이상 사용하지 않는다.

즉 data plane의 reserved system topic 사용은 bootstrap 한정이다.
steady-state에서 control을 다시 data mesh에 태우지 않는다.

중요한 정책:

- bootstrap 완료 전 peer 상태는 `bootstrap_pending`이다.
- `bootstrap_pending` peer는 active remote forwarding peer로 취급하지 않는다.
- 이 구간에서 payload를 따로 buffer하지 않는다.
- 따라서 manual peer 연결 직후 publish된 data는 local path만 흐르고,
  해당 remote peer에는 bootstrap 완료 후부터만 전달된다.
- application이 최초 remote 전달을 요구하면
  `DELIVERY_READY_CHANGED` 또는 `FIRST_DELIVERY_READY_CHANGED`를 기준으로
  publish 시점을 제어해야 한다.

이 정책은 undefined race를 없애고
strong readiness 계약과도 자연스럽게 맞는다.

manual bootstrap을 위해 각 node의 `mesh_xsub`에는
`__zlink.spot.bootstrap.` prefix에 대한 runtime-owned internal subscription을
상시 유지한다.
이 subscription은 user child/default sub 전제가 아니라
node transport bootstrap을 위한 명시적 system subscription이다.

### 9.3 향후 API 정리 후보

장기적으로는 manual peer API를 아래 형태로 정리하는 편이 낫다.

```c
zlink_spot_connect_peer_ex(spot, data_endpoint, ctrl_endpoint)
```

다만 이번 리팩터의 1차 목표는 성능 복구와 계약 보존이므로,
기존 API 호환을 깨지 않는 bootstrap 방식으로 먼저 진행한다.

## 10. 상태 모델

peer마다 아래 상태를 둔다.

- `peer_node_id`
- `peer_data_endpoint`
- `peer_ctrl_endpoint`
- `bootstrap_pending`
- `data_forwarding_enabled`
- `ctrl_connected`
- `ctrl_desynced`
- `hello_exchanged`
- `subscription_snapshot_synced`
- `subscription_epoch_seen`
- `snapshot_generation_seen`
- `last_control_sequence_seen`
- `ctrl_heartbeat_deadline`
- `ready_sources_by_filter`

local node 공통 상태:

- `local_subscription_epoch`
- `local_ctrl_descriptor_version`
- `pending_ready_probes`
- `pub_delivery_ready_sources`

자료구조 권장안:

- `pending_ready_probes`:
  `std::map<ready_probe_key_t, ready_probe_state_t>`
- `ready_probe_key_t`:
  `(target_peer_node_id, raw_filter)`
- `ready_sources_by_filter`:
  `std::map<std::string, std::set<uint64_t> >`

필요한 불변식:

- `FIRST_DELIVERY_READY_CHANGED`는 peer ready source set의 실제 변화에만 반응한다.
- 같은 peer의 중복 `READY_ACK`는 count를 증가시키지 않는다.
- disconnect 이후 stale ack는 무시한다.
- snapshot 이전 delta는 적용하지 않는다.

## 11. 구현 단계

### 11.1 1단계: runtime 뼈대 추가

- `spot_runtime_t`에 `peer_ctrl_pub/sub`와 endpoint 추가
- bind/connect/close lifecycle 정리
- 아직 동작 전환은 하지 않는다

완료 기준:

- build 및 기존 SPOT 테스트가 깨지지 않는다.

### 11.2 2단계: control protocol 골격 추가

- control topic/verb enum 또는 상수 정의
- `HELLO`, `HELLO_ACK`, snapshot/delta frame codec 추가
- peer state table 추가

완료 기준:

- control descriptor 송수신 로그까지 확인 가능
- payload 경로는 아직 기존과 동일

### 11.3 3단계: subscription mirror 이관

- local subscription add/remove를 control plane으로 전송
- peer별 snapshot sync 구현
- 현재 `mesh_pub XPUB` 경로와 병행하여 shadow compare

shadow compare 정책:

- 기본 release/perf 실행에서는 끈다.
- debug 또는 명시적 internal diagnostic flag에서만 켠다.
- perf acceptance 수치는 shadow compare가 꺼진 상태에서만 측정한다.

완료 기준:

- 두 경로의 effective remote subscription set이 일치한다.

### 11.4 4단계: readiness 이관

- `ready_probe`
- `ready_ack`
- `pub_delivery_ready_sources`

를 control plane 기반으로 이관한다.

완료 기준:

- `SPOT_SUB_FILTER_APPLIED`
- `SPOT_SUB_DELIVERY_READY_CHANGED`
- `SPOT_PUB_DELIVERY_READY_CHANGED`
- `SPOT_PUB_FIRST_DELIVERY_READY_CHANGED`

관련 기존 테스트가 유지된다.

### 11.5 5단계: data plane 단순화

- `mesh_pub`를 `PUB`로 복구
- data plane poll set에서 `mesh_pub` control 입력 제거
- `ready_ack` filter 파싱 제거
- remote data subscription count bookkeeping 제거

완료 기준:

- remote mesh steady-state가 data forwarding 중심으로 단순화된다.

### 11.6 6단계: bootstrap / discovery 정리

- discovery metadata 확장
- manual peer bootstrap reserved topic 정리
- protocol version mismatch 처리

완료 기준:

- discovery/manual 둘 다 control plane을 형성할 수 있다.

### 11.7 7단계: 정리와 제거

- current XPUB 기반 control 잔재 제거
- dead field/dead code 정리
- 문서/API 설명 갱신

## 12. 검증 계획

### 12.1 기능 검증

우선 유지해야 할 핵심 테스트:

- `test_spot_pubsub_scenario`
- `test_spot_service_introspection`
- `test_monitor_service_contract`

추가로 확인할 것:

- peer connect/disconnect 반복 시 ready count 정확성
- subscription snapshot 직후 delta ordering
- stale ack 무시
- manual peer bootstrap 후 control plane 전환
- peer 수가 많고 subscription churn이 큰 경우에도 data throughput이 급락하지 않는지

### 12.2 성능 검증

성능 검증 순서는 고정한다.

1. single SPOT 먼저 main 근사치까지 복구
2. single GATEWAY 확인
3. multi를 `core/perf/results/multi/report/perf_linux_20260314_002645.txt`와 비교
4. 마지막에만 `perf full` 전체 패턴/사이즈 무실패 확인

single SPOT의 1차 수치 목표:

- 아래 90% 기준은 `single -> multi` 진행을 허용하는 최소 gate다.
- 최종 목표치는 이보다 높다.

- `tcp/131072`: `46.204 Kmsg/s`의 90% 이상
- `tcp/262144`: `25.550 Kmsg/s`의 90% 이상

즉 대략:

- `tcp/131072 >= 41.6 Kmsg/s`
- `tcp/262144 >= 23.0 Kmsg/s`

최종 지향 수치:

- 구조 분리 후 안정화 단계에서는 main의 95% 이상을 우선 목표로 둔다.
- 90% 이상 95% 미만에 머무르면 perf profile 근거 없이 종료하지 않는다.

### 12.3 회귀 방지 포인트

- large payload bench-only 최적화 금지
- readiness contract 약화 금지
- hidden endpoint derivation 금지
- default child/default sub 전제 금지
- `iothread` 증가는 진단/튜닝용으로만 사용하고 구조 개선의 대체재로 쓰지 않음

## 13. 리스크와 대응

### 13.1 bootstrap 복잡도 증가

manual peer path는 control endpoint를 별도로 알아야 한다.
숨은 규칙으로 endpoint를 유도하면 안 되므로 bootstrap 절차가 필요하다.

대응:

- data plane bootstrap은 connect 직후 한정된 reserved system topic으로만 사용
- steady-state control은 반드시 분리

### 13.2 snapshot/delta ordering 버그

subscription mirror는 epoch가 없으면 stale delta 적용 문제가 생긴다.

대응:

- snapshot generation
- local subscription epoch
- peer별 last applied epoch

를 명시적으로 둔다.

### 13.3 ready count 중복/누락

같은 peer의 중복 ack 또는 disconnect 후 stale ack는
`FIRST_DELIVERY_READY_CHANGED`를 쉽게 깨뜨린다.

대응:

- peer node id 단위 set 기반 집계 유지
- disconnect 시 peer source 전부 제거
- snapshot 재동기화 시 old epoch ack 무시

### 13.4 control plane이 다시 hot path에 새 비용을 만들 위험

control 분리를 해도 구현을 잘못하면 payload 전송 함수 안에
control 상태 분기가 다시 들어갈 수 있다.

대응:

- payload forwarding 함수는 payload 전달 외 책임을 갖지 않게 강제
- control 수신은 별도 handler로 제한
- same-thread budget으로도 throughput 영향이 남으면 control worker 분리 검토

### 13.5 control backlog가 다시 성능 병목이 될 위험

control plane도 subscription delta와 ack가 누적되면 backlog가 쌓일 수 있다.
이 경우 data plane은 분리돼도 state sync가 늦어져 readiness가 흔들릴 수 있다.

대응:

- data/control socket HWM을 분리해서 관리
- 같은 filter에 대한 연속 delta는 coalesce
- peer별 sync 상태가 깨지면 delta 누적 대신 snapshot resync로 강등
- 모든 control message에 sequence number를 싣고 gap detection 수행
- heartbeat timeout도 resync trigger로 사용

## 14. 최종 결론

현재 회귀의 본질은 `SPOT에서 control 의미가 data socket에 얹혀 있는 구조`다.
따라서 근본적인 해결은 미세 최적화 누적이 아니라
`data/control plane 분리`여야 한다.

이번 리팩터의 최종 방향은 다음과 같다.

- data mesh는 `PUB/XSUB`로 복구한다.
- monitor/readiness 계약은 node-level peer control plane으로 유지한다.
- 추가 네트워크 소켓은 `spot_node`당 `ctrl_pub`/`ctrl_sub` 한 쌍만 둔다.
- discovery/manual bootstrap은 명시적 descriptor로 처리한다.

이 설계가 완료되면,
SPOT payload 경로는 다시 main과 같은 성격의 단순한 fast path를 가지면서도
현재 monitor/readiness contract를 유지할 수 있다.

## 15. 새 컨텍스트 실행 지침

이 절은 새 컨텍스트에서 이 문서만 읽고
구조 개편부터 single/multi 성능 복구, 최종 perf full 검증까지
중단 없이 진행하기 위한 실행 규칙이다.

### 15.1 기준과 종료 조건

기준 파일:

- single main 기준:
  `/home/hep7/project/kairos/zlink/core/perf/results/single/report/perf_linux_20260314_003239.txt`
- multi main 기준:
  `/home/hep7/project/kairos/zlink/core/perf/results/multi/report/perf_linux_20260314_002645.txt`

종료 조건은 아래 3개를 모두 만족할 때만 충족된다.

1. single의 SPOT, GATEWAY가 main 근사치까지 회복됨
2. multi의 SPOT, GATEWAY가 main 근사치까지 회복됨
3. `perf full` single/multi 전체 실행에서 실패가 없음

이 문서에서 `main 근사치`의 실행 기준은 다음으로 둔다.

- 우선 목표: main 대비 95% 이상
- 최소 gate: main 대비 90% 이상
- 단, SPOT/GATEWAY 주요 tcp 구간에서 95% 미만이면
  perf profile 근거 없이 종료하지 않는다.
- full report에서 SPOT/GATEWAY 어떤 셀이라도 90% 미만이면 종료하지 않는다.

### 15.2 작업 방식

- 목표가 닫히기 전까지 중간 보고, 대기, 확인 요청 없이 계속 진행한다.
- 새로운 의미 있는 변경이 나오면 바로 구현, 빌드, 측정, 비교까지 이어서 한다.
- 멈출 수 있는 경우는 아래뿐이다.
  - 현재 변경과 직접 충돌하는 사용자 수정이 새로 들어온 경우
  - 환경 자체가 깨져서 빌드/실행이 불가능한 경우
  - destructive action이 필요한데 사용자가 명시적으로 요청하지 않은 경우

즉 기본 동작은 `계속 작업`이며,
마지막 보고는 종료 조건을 만족했을 때만 한다.

### 15.3 새 컨텍스트 시작 시 첫 확인 항목

1. 이 문서를 먼저 끝까지 읽는다.
2. `git status --short`로 dirty tree를 확인한다.
3. unrelated diff는 건드리지 않는다.
4. SPOT/GATEWAY core path 관련 변경만 본다.
5. 현재 worktree에서 이미 수정된 아래 파일은 우선 검토 대상이다.

- `core/src/services/spot/spot_data_plane.cpp`
- `core/src/services/spot/spot_node.cpp`
- `core/src/services/spot/spot_node.hpp`
- `core/src/services/spot/spot_pub.cpp`
- `core/src/services/spot/spot_pub.hpp`
- `core/src/services/spot/spot_sub.cpp`
- `core/src/services/spot/spot_sub.hpp`
- `core/src/sockets/fq.cpp`
- `core/src/sockets/router.cpp`
- `core/src/sockets/socket_base.cpp`
- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/xpub.cpp`
- `core/src/sockets/xpub.hpp`
- `core/src/sockets/xsub.cpp`
- `core/src/sockets/xsub.hpp`

### 15.4 금지사항 재확인

- perf 수치만 올리기 위한 harness 특화 코드 금지
- large payload zero-copy 같은 bench-only 최적화 금지
- hidden default child/default sub 전제 금지
- benchmark에서만 켜는 내부 shortcut 금지
- `PERF_IO_THREADS` 증가는 진단 용도로만 사용
- 최종 성능 복구를 perf 환경변수 조합에만 의존해서 닫지 않음

`PERF_IO_THREADS`를 실험해도 된다.
하지만 최종 완료는 core 구조 개선으로 설명 가능해야 하며,
기본 동작이 아닌 perf-only 설정값에 기대서는 안 된다.

### 15.5 실행 순서

항상 아래 순서로 진행한다.

1. 구조 개편을 먼저 진행한다.
2. single SPOT를 main 근사치까지 끌어올린다.
3. single GATEWAY를 확인하고 필요한 회귀를 닫는다.
4. multi SPOT/GATEWAY를 main 기준과 비교하며 닫는다.
5. 마지막에만 full single/multi 전체 패턴을 돈다.

single이 닫히기 전에는 multi나 full에 시간을 쓰지 않는다.
full은 오직 최종 게이트다.

### 15.6 권장 perf 실행 명령

집중 측정:

```bash
core/perf/run_benchmarks.sh \
  --reuse-build \
  --pattern SPOT,GATEWAY \
  --transports tcp \
  --msg-sizes 131072,262144
```

multi 집중 측정:

```bash
core/perf/run_benchmarks_multi.sh \
  --reuse-build \
  --pattern SPOT,GATEWAY \
  --transports tcp \
  --msg-sizes 131072,262144
```

진단용 `iothread` 실험:

```bash
core/perf/run_benchmarks.sh \
  --reuse-build \
  --pattern SPOT \
  --transports tcp \
  --msg-sizes 131072,262144 \
  --io-threads 2
```

최종 full 게이트:

```bash
core/perf/run_benchmarks.sh --reuse-build --pattern ALL
core/perf/run_benchmarks_multi.sh --reuse-build --pattern ALL
```

필요 시 `--clean-build`, `--duration`, `--runs`, `--output`을 사용한다.
단, 최종 acceptance에 쓰는 수치는 perf 정책 기본값과 크게 다른 실험 설정이 아니어야 한다.

### 15.7 각 단계의 완료 기준

#### 15.7.1 구조 개편 단계

아래가 모두 만족되기 전에는 구조 개편이 끝난 것이 아니다.

- `mesh_pub`가 다시 data 전용 `PUB`가 됨
- peer control plane이 subscription/readiness를 담당함
- manual/discovery bootstrap이 동작함
- readiness contract 테스트가 유지됨

#### 15.7.2 single 단계

아래 순서로 확인한다.

1. focused single run으로 SPOT/GATEWAY tcp 대형 payload 비교
2. 결과 report를 main single 기준 파일과 비교
3. SPOT가 main 근사치에 도달할 때까지 반복
4. GATEWAY 회귀가 있으면 함께 닫음

single 단계는 아래를 만족해야 통과다.

- SPOT 주요 tcp 셀이 95% 이상에 근접
- GATEWAY 주요 tcp 셀이 95% 이상에 근접
- full single report에서 SPOT/GATEWAY 셀 중 90% 미만이 없음
- single 실행 실패가 없음

#### 15.7.3 multi 단계

single이 닫힌 뒤에만 진행한다.

1. focused multi run으로 SPOT/GATEWAY tcp 대형 payload 비교
2. 결과 report를 main multi 기준 파일과 비교
3. peer churn, readiness, bootstrap 경로에서 회귀가 없는지 확인
4. throughput과 failure를 함께 본다

multi 단계는 아래를 만족해야 통과다.

- SPOT 주요 tcp 셀이 95% 이상에 근접
- GATEWAY 주요 tcp 셀이 95% 이상에 근접
- full multi report에서 SPOT/GATEWAY 셀 중 90% 미만이 없음
- multi 실행 실패가 없음

### 15.8 구현 우선순위

single SPOT가 여전히 크게 낮으면 아래 순서로 본다.

1. `spot_data_plane.cpp`의 data/control 분리 완료 여부
2. `spot_node.cpp`의 subscription/readiness state machine
3. `spot_pub.cpp`, `spot_sub.cpp`의 event/readiness bookkeeping
4. `xpub.cpp`, `xsub.cpp`, `fq.cpp`, `router.cpp`, `socket_base.cpp`의
   socket hot path 영향

판단 기준:

- local-only는 빠른데 remote mesh만 느리면 SPOT peer path 문제다.
- raw `PUBSUB`가 main과 가깝다면 generic transport 문제가 아니다.
- `iothread` 증가로만 개선되면 구조 병목이 숨은 것일 가능성이 높다.

### 15.9 검증 순서

구조 변경 후에는 아래 순서를 반복한다.

1. 관련 unit/integration/e2e 테스트
2. focused single
3. single 기준 비교
4. focused multi
5. multi 기준 비교
6. 마지막 full single/multi

최소 유지 테스트:

- `test_spot_pubsub_scenario`
- `test_spot_service_introspection`
- `test_monitor_service_contract`

필요 시 관련 GATEWAY 테스트도 추가한다.

### 15.10 문서만으로 판단이 안 될 때의 우선순위

판단이 애매하면 아래 원칙을 따른다.

1. perf-only가 아닌 core hot path 개선인가
2. readiness contract를 약화하지 않는가
3. hidden internal assumption을 추가하지 않는가
4. single SPOT main 근사치 복구에 직접 연결되는가

위 조건을 만족하면 구현하고 측정한다.
만족하지 않으면 버리고 다음 가설로 바로 넘어간다.
