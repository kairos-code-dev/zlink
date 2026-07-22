# Host Retire, Shutdown & Handoff — 공통 스펙

[스펙 목차](../README.ko.md) · [MeshNode](21-mesh-node.ko.md) ·
[ClientServer Channel](12-client-server-channel.ko.md) ·
[Async policy](../04-async-execution-policy.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Runtime monitoring](50-runtime-monitoring.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 process를 교체하거나 종료할 때 host가 service admission, accepted
work, stateful object, session과 transport를 어떤 순서로 정리하는지 정의한다. 논리 object를 다른 host에서
계속 처리해야 하면 `Retire`를 사용하고, 연속성을 새로 준비하지 않고 현재 host를 유한 시간 안에 종료해야
하면 `Shutdown`을 사용한다.

두 operation은 host가 소유한 모든 RouteMesh MeshNode, ClientServer server와 fanout publisher를 함께
조정한다. Application은 MeshName, ChannelName이나 node RID를 넘겨 종료 순서를 조립하지 않는다. Claim과
cancellation의 기본 의미는 [04 Async policy](../04-async-execution-policy.ko.md), descriptor와 transfer
authority는 [40 Location runtime](40-location-runtime.ko.md), Actor membership ordering은
[23 Spot Actor](23-spot-actor.ko.md)가 소유한다.

## 2. Runtime state, result와 public operation

Host lifecycle은 `FrameworkRuntimeState` 하나가 소유한다. 값은 다음 다섯 개로 고정한다.

| 값 | State | 계약 |
|---:|---|---|
| 0 | `Preparing` | registration, bind, descriptor 검증과 recovery를 진행하며 application admission은 닫혀 있음 |
| 1 | `Serving` | readiness와 신규 application admission이 열려 있음 |
| 2 | `Draining` | host 종료가 시작되어 신규 application admission과 target selection이 닫혀 있음 |
| 3 | `Stopped` | application·infrastructure resource와 listener 정리가 terminal 상태임 |
| 4 | `Error` | startup 또는 runtime 오류로 service를 제공할 수 없으며 readiness가 닫혀 있음 |

Host의 `IsReady`는 `Serving`에서만 true다. Host의 `Draining`은 독립 command가 아니라 `Retire` 또는
`Shutdown` 진행 중에 관측되는 상태다. Host state에는 `Drained`와 `ForceStopping`을 추가하지 않는다.
MeshNode와 다른 topology component의 lifecycle snapshot은 component 상태를 관측하는 정보다. Component별 종료
command는 제공하지 않으며 host state가 모든 topology의 종료 순서를 결정한다.

허용 transition은 `Preparing -> Serving|Error`, `Serving -> Draining|Error`, `Error -> Draining`,
`Draining -> Stopped`다. `Stopped`에서 다른 상태로 전환하지 않는다. `Error` 상태의 host는 continuity
preflight를 시작할 수 없으므로 `Retire`가 `Blocked/RuntimeNotReady`로 끝난다. `Shutdown`은 `Error`에서도
bounded cleanup을 시작할 수 있다. `ForceStopped`는 cleanup 방식에 대한 outcome이며 cleanup 뒤 host state는
`Stopped`다.

Host는 local RouteMesh MeshNode, ClientServer server와 fanout publisher의 startup·termination을 조정해
`FrameworkRuntimeState`를 결정한다. Startup 중에는 `Preparing`, 모든 required component가 ready면 `Serving`,
termination admission seal 뒤에는 `Draining`, 모든 resource cleanup 뒤에는 `Stopped`다. Termination 전 required
component 하나라도 service를 제공할 수 없으면 `Error`다. Host termination snapshot은 이 state를 제공한다.
`ZLinkMeshNodeState` 같은 component state는 각 component의 상태를 표현하며
`FrameworkRuntimeState`로 이름이나 enum 값을 바꾸지 않는다.

`Drain`, `AwaitDrained`, `Stop` 계열 공개 member는 각 언어별 exact interface가 정한 host-wide compatibility
facade다. 이 표면은 continuity를 준비하지 않는 `Shutdown` intent와 같은 coordinator를 사용한다. MeshName,
ChannelName이나 node RID를 받는 partial termination operation은 제공하지 않는다. Rolling replacement에는
`Retire`를 사용한다.

Host termination 결과는 다음 닫힌 값으로 고정한다.

Result는 호출을 처리한 `EffectiveIntent`, `Outcome`, `Reason`을 함께 제공한다. EffectiveIntent wire 값은
`Retire=0`, `Shutdown=1`이다.

| 값 | Outcome | 허용 reason | 계약 |
|---:|---|---|---|
| 0 | `Stopped` | `None` | 요청한 host 종료가 정상 완료됨 |
| 1 | `Blocked` | `TargetUnavailable`, `StoreUnavailable`, `TransferDisabled`, `StateIncompatible`, `DeadlineExceeded`, `RuntimeNotReady` | `Retire` preflight가 continuity를 준비하지 못했거나 runtime이 preflight를 시작할 수 없으며 admission과 runtime state를 바꾸지 않음 |
| 2 | `ForceStopped` | `DeadlineExceeded`, `TransferFailed`, `TeardownFailed` | admission을 닫은 뒤 bounded teardown으로 host resource를 정리함 |

Outcome wire 값은 `Stopped=0`, `Blocked=1`, `ForceStopped=2`다. Reason wire 값은 `None=0`,
`TargetUnavailable=1`, `StoreUnavailable=2`, `TransferDisabled=3`, `StateIncompatible=4`,
`DeadlineExceeded=5`, `TransferFailed=6`, `TeardownFailed=7`, `RuntimeNotReady=8`이다. 정의하지 않은 outcome과
reason 조합은 protocol 오류다.

Published Location authority가 가리키는 Transfer root의 permanent missing, checksum mismatch 또는 inventory digest
mismatch는 object-level non-retriable `TransferDataLost`다. 진행 중인 `Retire` 결과는
`ForceStopped/TransferFailed`로 끝내고 monitoring error detail에 `TransferDataLost`를 보존한다. Commit된 owner와
membership을 source로 rollback하지 않는다.

| Operation | 계약 |
|---|---|
| `Retire(deadline, cancellation)` | 모든 local stateful object의 continuity를 preflight한 뒤 admission seal, transfer와 host 종료를 하나의 operation으로 수행함 |
| `Shutdown(deadline, cancellation)` | 새 transfer나 target reservation을 시작하지 않고 accepted work와 resource를 deadline 안에 정리함 |

두 operation의 기본 deadline은 30초이며 명시한 deadline은 0보다 커야 한다. Concurrent `Retire` waiter는 같은
preflight attempt를 공유한다. `Blocked`는 host terminal result가 아니므로 그 waiter들에게 결과를 전달한 뒤
attempt를 닫으며, 다음 `Retire`는 새 preflight transaction을 시작할 수 있다. Deadline은 최초 호출 시점부터
계산한다.

Host state가 `Draining`으로 전환되면 먼저 시작한 termination operation의 intent와 deadline이 shared operation에
고정된다. 이후 같은 intent와 cross-intent 호출은 새 transaction을 만들지 않고 진행 중인 operation 또는
저장된 terminal result를 사용한다. Caller cancellation은 해당 waiter만 끝내며 shared operation, transfer나
teardown을 취소하지 않는다. 뒤에 합류한 호출의 deadline은 진행 중인 operation을 줄이거나 늘리지 않는다.

`Draining`에서 호출한 `Retire` 또는 `Shutdown`은 먼저 시작한 host 정리 operation에 합류하고 그 operation의
effective intent가 포함된 terminal result를 받는다. 따라서 `Retire`가 먼저 시작됐으면 뒤의 `Shutdown`도
`EffectiveIntent=Retire`, `Shutdown`이 먼저 시작됐으면 뒤의 `Retire`도 `EffectiveIntent=Shutdown`을
관측한다. 후자의 경우 새 continuity transaction을 시작하지 않는다. `Retire`가 `Blocked`로 끝나면
termination operation이 시작되지 않았으므로 application은 조건을 고친 뒤 `Retire`를 다시 시작하거나 별도
`Shutdown`을 시작할 수 있다.

`Retire` preflight와 `Shutdown`의 admission seal은 같은 host maintenance barrier에서 순서를 정한다.
`Shutdown`이 seal을 먼저 commit하면 진행 중인 `Retire` preflight는 reservation을 해제하고
`EffectiveIntent=Shutdown` operation에 합류한다. `Retire`가 preflight와 seal을 먼저 commit하면
`Shutdown`이 `EffectiveIntent=Retire` operation에 합류한다.

`Preparing` 또는 `Error`에서 호출한 `Retire`는 state와 admission을 바꾸지 않고
`Blocked/RuntimeNotReady`로 끝난다. `Shutdown`은 `Preparing`에서는 startup을 중단하고 `Error`에서는 오류
수습을 포함한 bounded cleanup을 시작해 `Stopped` 또는 `ForceStopped`로 유한 완료된다. `Stopped`에서 호출한
두 operation은 저장된 terminal result가 있으면 그 결과를 반환한다. 저장된 결과가 없으면 요청 intent를
`EffectiveIntent`로 사용한 `Stopped/None`을 idempotent하게 반환하며 새 operation을 시작하지 않는다.

## 3. Retire all-or-none preflight

`Serving`에서 시작한 `Retire`는 host state나 어느 local component의 admission도 바꾸기 전에 다음 항목을
하나의 host maintenance barrier에서 확인한다.

1. Barrier가 새 Spot·Actor 생성, join, Instance placement, session binding과 inbound transfer를 inventory와
   직렬화한다.
2. 모든 local MeshNode의 Actor, Spot, timer, session과 진행 중인 infrastructure operation을 inventory한다.
3. Location authority, 필요한 Transfer Store와 target descriptor의 lease를 확인한다.
4. Standalone Actor, User Spot aggregate와 Instance Spot의 transfer policy, state contract compatibility와 target의
   bounded headroom을 확인한다. Exact inventory가 아직 없으므로 final reservation은 만들지 않는다.
5. Target이 `Serving`이고 application version, type capability, maintenance wave와 bounded capacity를
   모두 만족하는지 확인한다.

Barrier를 획득한 시점이 inventory의 linearization point다. 일반 message는 admission seal 전까지 수락할 수
있지만 seal이 확정한 accepted queue boundary에 포함되어야 한다.

하나라도 준비할 수 없으면 preflight read와 tentative coordination을 정리하고 `Blocked`를 반환한다. 이 경우 host state는
`Serving`이고 readiness, descriptor와 application admission도 그대로 유지된다. 한 process에
여러 MeshNode가 있으면 한 Mesh의 blocker가 host 전체 `Retire`를 차단한다.

Maintenance barrier나 target reservation을 deadline 전에 확보하지 못하면 `TargetUnavailable`, Store read·write
또는 lease 확인이 실패하면 `StoreUnavailable`, transfer policy가 허용하지 않으면 `TransferDisabled`,
application version·type capability·state contract가 호환되지 않으면 `StateIncompatible`를 사용한다.

## 4. Retire 진행 순서

Preflight가 성공하면 다음 순서를 한 번만 수행한다.

1. Host maintenance barrier가 모든 local component의 신규 application·timer admission을 reversible하게 seal한다.
   이 시점에는 아직 `Draining` descriptor를 publish하지 않는다.
2. Seal 전에 accept한 handler와 timer turn을 완료하고 object별 exact participant boundary와 byte count를 고정한다.
3. 각 object를 `Preparing → Captured`로 진행해 immutable transfer root를 authority에 연결한다.
4. Exact inventory로 target offer·accept·reservation ACK를 완료하고 `Prepared` authority를 CAS한다. Preflight의
   headroom 확인을 final reservation으로 사용하지 않는다.
5. 모든 대상 object가 `Prepared`가 되면 host state와 descriptor를 `Draining`으로 publish하고 remote selector가
   해당 host를 제외하도록 bounded convergence를 기다린다.
6. Prepared target으로 standalone Actor, User Spot aggregate와 Instance Spot transfer, durable source cleanup과
   Completed CAS를 진행한다.
7. Bound STREAM route commit·ACK와 maintenance authority의 steady normalization을 끝낸 뒤 target admission을 연다.
8. Local Spot, owner authority와 descriptor lease를 current fence로 정리한다.
9. ClientServer listener, fanout publisher, peer connection과 raw transport resource를 닫는다.
10. Host state를 `Stopped`로 바꾸고 descriptor와 terminal event에 투영한 뒤 `Stopped/None`을 완료한다.

`Draining`으로 전환한 뒤에는 `Blocked`로 돌아가지 않는다. Deadline이나 transfer·teardown failure가 발생하면
신규 admission을 닫은 상태로 bounded teardown을 수행하고 `ForceStopped`를 한 번만 완료한다. Commit된
transfer의 durable recovery는 source host의 `ForceStopped` 완료와 독립적으로 계속될 수 있다.

Store failure가 admission seal 전에 발생하면 preflight를 `Blocked/StoreUnavailable`로 끝낸다. `Draining`
전환 뒤 authority 또는 transfer root 진행이 실패하면 `ForceStopped/TransferFailed`, descriptor·owner cleanup이나
transfer root 삭제를 확인할 수 없으면 `ForceStopped/TeardownFailed`로 끝낸다. 이 처리보다 deadline이 먼저
끝나면 reason은 `DeadlineExceeded`다. `StoreUnavailable`은 `ForceStopped` reason으로 사용하지 않는다.

Deadline이 preflight 또는 reversible seal·Captured CAS 전 단계에서 끝나면 source authority와 admission을 원래
상태로 복원하고 `Blocked/DeadlineExceeded`로 끝낸다. Captured CAS가 transfer root를 authority에 연결한 뒤에는
accepted journal durability가 시작되므로 deadline을 이유로 `Blocked`로 되돌리지 않는다. 이 시점 이후 deadline은
bounded teardown과 recovery handoff를 수행한 뒤 `ForceStopped/DeadlineExceeded`로 한 번만 완료한다.

Descriptor를 `Draining`으로 게시했다는 이유만으로 peer connection을 즉시 끊지 않는다. In-flight reply,
transfer control과 STREAM barrier가 같은 ROUTER connection을 사용할 수 있기 때문이다.

## 5. Shutdown 진행 순서

`Shutdown`은 target eligibility, transfer policy와 spare capacity를 blocker로 사용하지 않으며 `Blocked`를
반환하지 않는다. 다음 순서를 수행한다.

1. Host state를 `Draining`으로 바꾸고 모든 local component의 신규 application admission과 새 transfer reservation을
   닫는다.
2. `Draining` descriptor를 게시하고 새 ChannelName·Instance placement·fanout selection에서 제외한다.
3. 이미 수락한 handler, request completion과 진행 중인 transfer·session barrier를 deadline까지 처리한다.
4. 새 object transfer를 시작하지 않고 local Actor·Spot, owner record, listener와 transport를 정리한다.
5. Deadline 안에 끝나면 `Stopped/None`, 끝나지 않으면 bounded teardown 뒤 `ForceStopped`를 완료한다.

`Shutdown`은 logical continuity를 보장하지 않는다. `Disabled` object, 호환 가능한 target 부재와 Transfer Store
provider 부재는 `Shutdown`을 차단하지 않는다. Hardware failure나 SIGKILL로 operation을 실행할 수 없는
경우의 owner recovery는 [40 Location runtime](40-location-runtime.ko.md)의 lease와 durable authority를 따른다.

## 6. Selection과 admission

| Surface | `Draining` 이후 계약 |
|---|---|
| ChannelName select-one | 새 selection 대상에서 제외한다. 이미 submit된 operation은 terminal completion까지 진행한다. |
| Logical Multicast | 새 target snapshot에서 제외한다. 이미 수락한 local·remote 제출은 유지한다. |
| Node direct application request | 신규 admission seal 뒤 유한한 shutdown 또는 moving 결과로 끝낸다. 이미 수락한 request는 reply·error·timeout 가운데 하나로 끝낸다. |
| Node direct infrastructure control | completion, transfer, binding, recovery와 shutdown barrier에 필요한 control은 deadline까지 수락한다. |
| Spot·Actor direct | 신규 application payload를 거부하고 이미 queue에 수락한 turn을 처리한다. |
| Spot·Actor create와 join | 신규 owner와 membership admission을 거부한다. |
| Instance Spot placement | 새 target claim에서 제외한다. Seal 전에 수락한 activation만 terminal 상태까지 진행한다. |
| STREAM | 신규 session을 받지 않고 기존 session의 pending reply와 binding barrier를 처리한다. |
| ClientServer server | 새 target selection과 handler admission을 닫고 accepted request의 reply route를 유지한다. |
| classic fanout publisher | 새 automatic subscriber 연결과 publish admission을 닫고 local transport가 이미 수락한 event만 처리한다. |

Retire transfer에서 seal 뒤 도착한 request는 retry 가능한 `TargetMoving`으로 끝내고 one-way operation은
shutdown drop으로 관측한다. Framework가 새 operation ID로 다른 owner에 숨은 재제출을 시작하지 않는다.
[23 Spot Actor](23-spot-actor.ko.md)의 bounded stale-route forwarding이 적용되는 Actor message는 같은 operation
ID와 object·authority owner generation fence를 유지하며 별도 application retry로 취급하지 않는다. Shutdown에서 seal 뒤
도착한 request는 shutdown 결과로 끝난다.

Application callback이 비동기 작업을 기다리는 동안에도 infrastructure claim은 send-ready, request
completion, peer lifecycle, transfer recovery와 session binding을 진행할 수 있어야 한다. Observer, metric
reader와 runtime event handler는 termination progress를 막는 claim을 소유하지 않는다.

## 7. STREAM barrier

Standalone Actor, User Spot aggregate와 Instance Spot transfer는
[40 Location runtime](40-location-runtime.ko.md)의 owner·transfer authority CAS를 사용한다. `Retire`는 type
등록의 `Disabled`, `Recreate`, `Snapshot` policy를 적용한다.

- `Disabled` object가 남아 있으면 preflight를 `TransferDisabled`로 차단한다.
- `Recreate`는 target factory를 같은 logical ID로 실행하며 application state section 없이 accepted journal과
  recovery payload를 transfer envelope에 기록한다.
- `Snapshot`은 typed adapter가 capture한 application state와 seal 전 accepted journal을 Transfer Store에
  기록하고 target activation 전에 restore한다.
- Entry Spot은 transfer하지 않고 target node startup에서 Framework가 새 identity로 구성한다.
- User Spot은 Spot과 preflight linearization point의 member Actor 전체를 하나의 transfer aggregate로 처리한다.
  Spot과 각 Actor에 등록한 policy·state contract를 함께 검사하며 participant 하나라도 `Disabled`이거나 호환
  target을 찾을 수 없으면 commit 전에 전체 aggregate를 `TransferDisabled`로 차단한다.
- User Spot aggregate는 non-zero 128-bit aggregate ID, 최대 1024 participant와 encoded 최대 1 MiB record를
  사용한다. Spot owner, Actor owner와 membership은 한 commit generation에서 함께 전환한다. Commit 전 실패는
  source aggregate 전체를 유지하고, commit 뒤 실패는 일부 participant를 source로 되돌리지 않고 target aggregate
  recovery를 계속한다.
- Cross-node Actor `JoinSpot`·`JoinEntrySpot`은 target proposal, shared policy preflight, source seal, durable capture, target reservation과
  prepare, owner·membership aggregate commit, restore·callback·ACK 순서로 진행한다. Commit 전 실패는 source owner와
  membership을 유지하고 commit 뒤 실패는 target recovery를 계속한다. Same-node join은 relocation이 아니므로
  transfer policy로 차단하지 않는다.
- Instance Spot의 public activation은 Manager의 명시적인 create intent만 시작한다. 일반 message와 find는 existing
  authority를 사용하며 hidden create를 시작하지 않는다. `Retire` target materialization은 logical create와 다른
  maintenance transaction이다.

Bound STREAM connection 자체는 이동하지 않는다. Actor owner commit 뒤 session relay authority와 binding
generation을 갱신하고 stale packet과 reply를 거부한다. Runtime timer handle과 callback continuation도
이동하지 않는다. Snapshot state는 다음 실행 시각과 업무상 필요한 timer state를 포함할 수 있으며 target
restore 뒤 새 timer를 등록한다.

Application이 명시적으로 시작한 Instance Spot `Close`와 maintenance transfer는 같은 authority CAS에서
순서를 정한다. `Closing`이 먼저 commit되면 `Retire`는 close completion을 기다리고 transfer하지 않는다.
Transfer가 먼저 commit되면 늦은 `Close`는 moving 결과로 끝나며 Framework가 새 owner에 자동 재제출하지
않는다.

## 8. Location과 resource cleanup

`Draining` 동안 descriptor와 owner lease를 계속 renew한다. Lease를 먼저 중단하면 accepted request,
transfer와 session barrier가 사용하는 fence를 잃을 수 있다. Barrier가 끝난 뒤 다음 순서로 정리한다.

1. local lifecycle callback과 scope cleanup을 완료한다.
2. Current authority를 가진 source만 Actor·Spot owner와 transfer participant record를 advance하거나 release한다.
3. MeshNode, ClientServer server와 fanout publisher descriptor·owner lease를 release한다.
4. Peer connection, listener, executor와 binding raw transport를 닫는다.

Store failure 뒤에도 local readiness와 admission seal은 닫힌 상태를 유지한다. Deadline까지 authority나 cleanup을
확인할 수 없으면 `Stopped`로 보고하지 않고 bounded teardown 뒤 `ForceStopped`를 완료한다. Manual peer mode는
store record 대신 admission된 peer control로 state를 전파하고 barrier 뒤 connection을 닫는다.

## 9. Observability

`FrameworkRuntimeState` 변화는 `zlink.runtime.host.termination_changed` event로 관찰한다. MeshNode,
ClientServer와 fanout lifecycle event는 같은 host state의 descriptor projection을 제공한다. Terminal event는
observer overflow로 잃지 않는다.

| Metric | 종류 | Label | 의미 |
|---|---|---|---|
| `zlink.termination.state` | observable | `state` | 현재 host state 하나에 값 1을 기록 |
| `zlink.termination.duration` | histogram | `intent`, `outcome` | Retire 또는 Shutdown 시작부터 terminal result까지 걸린 시간 |
| `zlink.termination.blocked` | counter | `reason` | admission을 바꾸지 않고 끝난 Retire 수 |
| `zlink.termination.forced` | counter | `intent`, `reason` | bounded teardown으로 끝난 operation 수 |
| `zlink.transfer.completed` | counter | `object_kind`, `policy` | Retire에서 완료한 object transfer 수 |

Metric label에는 Actor ID, Spot RID, node RID, endpoint, session ID와 transfer ID를 넣지 않는다. 개별 blocker와
transfer 상태는 bounded diagnostic query와 trace에서 확인한다. Observer callback 실패는 termination과
transfer 진행을 막지 않는다.

## 10. 검증 요구

- `Retire` preflight blocker가 있으면 host state가 `Serving`이고 모든 local admission이 유지된다.
- Multi-Mesh host의 preflight와 admission seal이 all-or-none으로 동작한다.
- `Retire` 성공은 supported standalone Actor·User Spot aggregate·Instance Spot continuity, session barrier와 host
  cleanup을 모두 완료한다.
- `Shutdown`은 새 transfer를 시작하지 않고 `Stopped` 또는 `ForceStopped`로 유한 완료된다.
- 기본 deadline은 30초이며 caller cancellation은 waiter만 끝낸다.
- Concurrent `Retire` waiter는 같은 preflight attempt를 공유하지만 `Blocked` result는 host terminal로 저장하지
  않는다.
- `Draining` 이후 반복 호출과 `Stopped`·`ForceStopped` 뒤 호출은 같은 shared operation 또는 저장된 terminal
  result를 사용한다.
- `Preparing`·`Error`의 `Retire`는 admission을 바꾸지 않고 `Blocked/RuntimeNotReady`로 끝난다.
- Cross-intent waiter는 `Draining`을 시작한 operation과 그 `EffectiveIntent` terminal result에 합류한다.
- `Blocked` 뒤의 `Retire`와 `Shutdown`은 새 operation을 시작할 수 있다.
- `Draining` 뒤 실패는 `Blocked`가 아니라 `ForceStopped`로 끝난다.
- Preflight·pre-Captured deadline은 `Blocked/DeadlineExceeded`, post-Captured deadline은
  `ForceStopped/DeadlineExceeded`로 끝난다.
- Store failure가 seal 전에는 `Blocked/StoreUnavailable`, seal 뒤에는 단계에 따라
  `ForceStopped/TransferFailed|TeardownFailed`로 끝난다.
- User Spot과 member Actor가 하나의 bounded aggregate로 preflight·commit되고 partial owner·membership이 공개되지
  않는다.
- `Serving`이 아닌 node는 새 ChannelName, Logical Multicast, Instance placement와 transfer target에서 제외된다.
- 이미 수락한 request는 reply·error·timeout·shutdown 가운데 하나로 한 번만 끝난다.
- Application callback이 대기 중이어도 infrastructure completion과 termination barrier가 진행된다.
- Actor transfer, bound session과 accepted journal이 generation·authority fence를 보존한다.
- Preflight는 final reservation을 만들지 않고 reversible seal 뒤 exact inventory로 모든 object를 Prepared한 다음
  host를 Draining으로 publish한다.
- Target은 Activated 뒤에도 sealed이며 durable source cleanup, Completed, bound-session route ACK와 steady
  normalization 뒤에만 Ready다.
- Precommit abort는 durable Aborted CAS 전에 session route를 되돌리거나 source admission을 열지 않는다.
- Instance maintenance transfer가 logical create나 stale ref의 hidden create를 시작하지 않는다.
- Store failure와 deadline 경쟁에서도 terminal result는 한 번만 완료된다.
- `FrameworkRuntimeState`, outcome, reason, event와 metric 값이 공통 wire 값 및 실제 terminal result와 일치한다.
- ClientServer와 fanout cleanup이 MeshNode descriptor나 Spot·Actor authority를 잘못 변경하지 않는다.
