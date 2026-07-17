# Graceful Drain & Handoff — 공통 스펙

[스펙 목차](../README.ko.md) · [MeshNode](21-mesh-node.ko.md) ·
[Async policy](../04-async-execution-policy.ko.md) · [Runtime monitoring](50-runtime-monitoring.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0에서 MeshNode가 신규 선택을 닫고 이미 수락한 작업과 handoff barrier를
deadline 안에서 마치는 공통 공개 계약을 정의한다. 이 문서는 “MeshNode가 종료될 때 새 ChannelName·Logical
Multicast target에서 빠지면서 application·infrastructure claim, request reply, Actor transfer와 STREAM
session을 어떤 순서로 완료하는가?”라는 질문에 답한다.

claim과 cancellation의 기본 의미는 [04 Async policy](../04-async-execution-policy.ko.md), descriptor와 owner
lease는 [40 Location runtime](40-location-runtime.ko.md), Actor transfer transaction은
[23 Spot Actor](23-spot-actor.ko.md)가 소유한다.

## 2. State와 public operation

Drain operation이 시작된 뒤의 하위 상태는 아래 닫힌 값이다.

```text
serving -> draining -> drained -> stopped
                |
                +-> force_stopping -> stopped
```

다이어그램의 상태 이름은 metric과 event에서도 lowercase 문자열 그대로 사용한다. 전체 MeshNode state의
`starting`은 이 순서에 들어가기 전 상태이고 `faulted`는 어느 단계에서든 전이할 수 있는 오류 상태다.
`IsReady()`는 `serving`에서만 true다.

| Operation | 계약 |
|---|---|
| `Drain(deadline)` | shared drain을 시작하고 terminal result를 기다린다. |
| `AwaitDrained()` | drain 시작 전에도 등록할 수 있으며 같은 terminal result를 기다린다. |
| `IsReady()` | 신규 자동 선택과 admission에 사용할 readiness를 반환한다. |

`Drain`은 멱등이다. 첫 호출이 positive deadline을 고정하고 후속 호출은 같은 operation에 합류한다.
호출자의 cancellation은 해당 waiter만 끝내며 shared drain을 중단하지 않는다. 인자 없는 호출과 host
shutdown의 기본 deadline은 30초다.

terminal result는 `Drained` 또는 `ForceStopped(reason)`이다. force reason은
`deadline_exceeded|drain_state_publish_failed|owner_cleanup_failed|teardown_failed`의 닫힌 값이다.

### 2.1 MeshNode drain policy

MeshNode 등록은 `ZLinkMeshNodeDrainPolicy`를 하나 설정한다. 기본값은 `DrainNatural`이며 닫힌 값은 다음
둘이다.

| 값 | 계약 |
|---|---|
| `DrainNatural` | 신규 Node·Spot·Actor admission과 새 transfer 배정을 닫고, 이미 수락한 turn과 transfer barrier를 진행한다. Actor handoff를 완료한 뒤 Spot은 application의 자연 종료 조건이 충족될 때까지 유지한다. |
| `ReleaseAndRecreate` | 같은 admission seal과 Actor handoff를 수행한 뒤, Spot의 accepted queue를 비우고 Spot을 닫아 location row를 release한다. 다음 요청은 serving MeshNode에서 `GetOrCreate`로 Spot을 다시 구성한다. |

정책은 Spot 하나가 아니라 MeshNode 전체에 적용한다. 따라서 Node·Channel selection 제외, Node·Spot·Actor
admission seal, 진행 중인 Actor transfer와 STREAM barrier, owner cleanup 순서는 두 값에서 동일하다. 차이는
accepted Spot turn이 끝난 뒤 Spot location ownership을 정리하는 방식이다.

`ReleaseAndRecreate`는 외부 영속 상태에서 Spot을 다시 구성할 수 있을 때만 사용한다. Framework는 Spot의
in-memory domain state를 복사하거나 event를 replay하지 않는다. 재구성할 수 없는 Spot에 이 값을 설정해
발생한 업무 상태 손실을 Framework가 복구하지 않는다.

## 3. Drain 순서

Drain은 다음 단계를 순서대로 수행한다.

1. MeshNode state를 `draining`으로 바꾸고 `IsReady()`를 false로 만든다.
2. Redis descriptor 또는 manual peer control에 drain state를 게시한다.
3. 신규 ChannelName select-one과 Logical Multicast target selection에서 MeshNode를 제외한다.
4. 신규 application admission을 seal하고 이미 수락한 application·infrastructure claim을 계속 처리한다.
5. pending request completion, Actor transfer와 STREAM session barrier를 deadline까지 진행한다.
6. application queue와 infrastructure barrier가 terminal이면 location ownership과 peer connection을
   정리하고 `drained`가 된다.
7. deadline 또는 필수 publish·cleanup failure가 발생하면 `force_stopping`에서 bounded teardown을 수행한
   뒤 `ForceStopped`를 완료한다.

peer connection은 drain state를 게시했다는 이유만으로 즉시 끊지 않는다. in-flight reply, transfer
control과 STREAM barrier가 같은 ROUTER connection을 사용할 수 있기 때문이다.

automatic discovery는 descriptor의 drain state를 사용한다. manual peer mode는 admission된 peer control과
runtime event로 같은 state를 전달하며 Redis를 요구하지 않는다.

## 4. Selection과 admission

| Surface | `draining` 이후 계약 |
|---|---|
| ChannelName select-one | 새 selection 대상에서 제외한다. 이미 선택되어 submit된 operation은 terminal completion까지 진행한다. |
| Logical Multicast | 새 remote target snapshot에서 제외한다. 이미 수락한 local·remote target admission은 NoDrop 계약대로 끝낸다. |
| Node direct application request | 신규 admission seal 뒤 `request_rejected`로 끝낸다. 이미 수락한 request는 reply·error·timeout으로 끝낸다. |
| Node direct infrastructure control | completion, transfer, binding과 shutdown barrier에 필요한 control은 deadline까지 수락한다. |
| Spot direct | 신규 application payload를 거부하고 이미 Spot queue에 수락한 turn을 마친다. |
| Actor direct | 신규 application payload를 거부하고 이미 Actor queue에 수락한 turn을 마친다. |
| Spot·Actor create와 join | 신규 owner·membership admission을 거부한다. |
| STREAM | 신규 session을 받지 않고 기존 session의 pending reply와 binding barrier를 처리한다. |
| classic fanout | 신규 publish를 shutdown 결과로 거부하고 이미 local transport에 수락한 event만 처리한다. |

one-way operation이 seal 뒤 도착하면 shutdown result 또는 drop 관측을 남긴다. 숨은 request로 바꾸거나
다른 MeshNode에 자동으로 다시 제출하지 않는다.

remote node가 drain state를 관찰하기 전에 선택한 operation이 도착할 수 있다. receiver가 신규 admission을
거부하면 request는 `request_rejected`로 유한 완료되고 one-way는 shutdown drop으로 관측된다. application은
이를 성공 reply로 해석하지 않는다.

## 5. Claim progress

Drain 중에도 application domain과 infrastructure domain은 별도 claim으로 진행한다.

- application claim은 drain 시작 전에 수락한 Node·Channel·Spot·Actor·STREAM callback을 실행한다.
- infrastructure claim은 send-ready, request completion, peer lifecycle, transfer control, session binding과
  shutdown barrier를 처리한다.
- application callback이 비동기 작업을 기다리는 동안에도 infrastructure claim을 획득할 수 있어야 한다.
- request reply와 error mapping은 request deadline 또는 drain deadline 중 먼저 도달하는 경계로 끝난다.
- deadline 뒤에는 남은 application claim을 revoke하고 pending operation을 shutdown result로 완료한다.

observer, metric reader와 runtime event handler는 drain progress를 막는 claim을 소유하지 않는다.

## 6. Actor와 Spot handoff

drain이 시작한 Actor handoff와 이미 진행 중인 transfer는 같은 transfer transaction을 사용한다. target은
`serving`이고 Actor type을 수용할 수 있는 MeshNode 중에서 고른다. `draining` node는 새 transfer target이
될 수 없지만 drain 전에 admission된 inbound commit은 transaction terminal까지 수용한다.

- source Actor queue의 accepted payload와 pending request는 transfer barrier가 보호한다.
- target commit과 source release가 끝나기 전에는 handoff 성공으로 집계하지 않는다.
- eligible target이 없으면 Actor를 source에 유지하고 deadline까지 accepted work를 처리한다.
- deadline을 넘긴 transfer는 force stop reason과 runtime error sink에 기록한다.

Framework는 Spot의 in-memory domain state를 다른 node로 자동 복사하지 않는다. Spot은 신규 join과 payload를
seal하고 accepted turn을 마친 뒤 §2.1의 MeshNode drain policy에 따라 자연 종료를 기다리거나 location row를
release한 뒤 닫는다. Actor membership transaction과 Spot control claim의 순서는
[23 Spot Actor](23-spot-actor.ko.md)가 정한다.

## 7. STREAM barrier

신규 STREAM session admission은 drain 시작과 함께 닫는다. 기존 session은 다음 barrier가 terminal일 때까지
유지할 수 있다.

- session callback과 pending client request reply
- bound Actor의 transfer 또는 source 유지 결정
- binding token update와 stale reply 차단
- client close notification의 bounded send

Framework는 server drain reason을 client의 close event에 제공한다. 대체 endpoint 선택과 reconnect는
connector 또는 application 정책이다. close notification 자체가 drain deadline을 연장하지 않는다.

## 8. Location과 owner cleanup

Redis를 사용하는 MeshNode는 `draining` 동안 descriptor와 owner lease를 계속 renew한다. lease를 먼저
중단하면 Spot·Actor row가 stale이 되어 request completion과 handoff fencing을 잃을 수 있다.

accepted work와 barrier가 끝난 뒤 다음 순서로 정리한다.

1. source가 더 이상 소유하지 않는 Spot·Actor row를 current owner token으로 release한다.
2. MeshNode descriptor와 owner lease를 release한다.
3. peer connection과 runtime resource를 닫는다.

store failure 때문에 drain state를 deadline까지 게시하지 못하거나 owner cleanup을 확인할 수 없으면
`Drained`로 완료하지 않고 `ForceStopped`로 끝낸다. local `IsReady = false`와 admission seal은 store failure와
관계없이 즉시 유지한다.

manual peer mode에는 Redis owner cleanup이 없다. peer control로 drain state를 전파하고 barrier가 끝난 뒤
connection을 닫는다.

## 9. Observability identifiers

drain state event identifier는
`zlink.runtime.mesh_node.drain_changed`다. 이 event는 [50 Runtime monitoring](50-runtime-monitoring.ko.md)의
MeshNode sequence를 사용하며 terminal event는 observer overflow로 잃지 않는다.

| 계기 | 종류 | 단위 | Label | 의미 |
|---|---|---|---|---|
| `zlink.drain.state` | observable | `{state}` | `mesh_name`, `state` | 현재 state 하나에 값 1을 기록 |
| `zlink.drain.duration` | histogram | `s` | `mesh_name`, `outcome` | Drain 시작부터 terminal result까지의 시간 |
| `zlink.drain.requests.completed` | counter | `{request}` | `mesh_name` | drain 중 정상 terminal completion을 얻은 request 수 |
| `zlink.drain.actors.handed_off` | counter | `{actor}` | `mesh_name` | 성공한 Actor handoff 수 |
| `zlink.drain.stream_barriers.completed` | counter | `{barrier}` | `mesh_name` | 성공한 STREAM barrier 수 |
| `zlink.drain.forced` | counter | `{item}` | `mesh_name`, `kind`, `reason` | force stop에서 남은 work 수 |

`outcome`은 `drained|force_stopped`, `kind`는 `claim|request|actor|spot|stream|peer`, `reason`은 §2의
force reason 값이다. Actor ID, Spot RID, RID,
endpoint, session ID와 topic을 label로 사용하지 않는다.

## 10. 검증 요구

- `Drain` 호출 직후 `IsReady`가 false이고 새 ChannelName·Logical Multicast selection에서 제외된다.
- peer connection과 owner lease가 accepted request, transfer와 STREAM barrier terminal까지 유지된다.
- application callback이 대기 중이어도 infrastructure completion과 shutdown barrier가 진행된다.
- 이미 수락한 request는 reply·error·timeout으로 한 번만 끝난다.
- `draining` target의 신규 transfer는 거부하고 이미 admission된 commit은 terminal까지 진행한다.
- deadline 초과가 `force_stopping`과 `ForceStopped` result로 유한 완료된다.
- 중복 `Drain` 호출과 여러 `AwaitDrained` waiter가 같은 terminal result를 관찰한다.
- `DrainNatural`은 accepted Spot turn과 자연 종료를 기다리고, `ReleaseAndRecreate`는 queue close 뒤에만
  Spot location row를 release한다.
- event와 metric state, outcome, force reason이 실제 terminal result와 일치한다.
