# Runtime Monitoring — 공통 스펙

[스펙 목차](../README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Runtime metrics](51-runtime-metrics.ko.md) · [Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 RouteMesh MeshNode와 global Actor·Spot placement 집계, ClientServer Channel과
automatic classic fanout subscriber의 상태를 snapshot과 typed event로 관찰하는 공통 공개 계약을 정의한다.
이 문서는 “운영자가 peer·server·fanout publisher readiness, channel 선택,
object placement·activation, application·infrastructure mailbox와 host 종료 진행을 어떤 안정된 표면으로 확인하는가?”라는
질문에 답한다.

집계 계기 이름은 [51 Runtime metrics](51-runtime-metrics.ko.md), 메시지 한 건의 trace는
[52 Message flow tracing](52-message-flow-tracing.ko.md), host termination state machine은
[54 Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)이 소유한다. 이 문서는 socket 내부 frame, poller와
queue 자료 구조를 공개 계약으로 정하지 않는다.

## 2. Snapshot

Runtime monitoring service는 등록된 MeshName별 MeshNode snapshot, 등록된 ClientServer Channel별 snapshot과
endpoint 없이 등록된 automatic fanout subscriber의 ChannelName별 snapshot을 구분해 반환한다. RouteMesh의
peer·channel·mailbox 상태, ClientServer의 client·server 상태 또는 fanout subscriber의 자동 연결
상태를 서로 다른 service에서 조합하도록 호출자에게 요구하지 않는다.

Host termination은 MeshName에 속하지 않으므로 host runtime snapshot에서 한 번만 제공한다. MeshNode
snapshot의 필드와 state enum을 host lifecycle에 맞춰 바꾸거나 모든 MeshNode snapshot에 같은 host 결과를
중복하지 않는다.

| 영역 | 공개 관찰 값 |
|---|---|
| MeshNode | MeshName, RID, lifecycle generation, descriptor revision, endpoint, service state, descriptor source set, object role, placement weight |
| Peer | RID, lifecycle generation, descriptor revision, endpoint, admission state, ready, service state, ChannelName set, last failure |
| Channel | ChannelName, local weight, ready member 수, 선택 가능 여부 |
| Mailbox | application·infrastructure domain별 active turn과 pending work 수 |
| Object placement | Actor 전체·Spot 전체·Spot kind·stable type별 active·reserved·limit capacity, activation concurrency, reservation failure와 최근 placement outcome |
| Location | store configured 여부, ready·degraded state, 마지막 성공·실패 시각 |
| Host termination | intent, runtime state, deadline, sealed work, blocker, pending request·relocation·STREAM barrier 수와 terminal result |

ClientServer Channel snapshot은 MeshName을 요구하지 않으며 다음 값을 함께 제공한다.

| 영역 | 공개 관찰 값 |
|---|---|
| Channel | ChannelName, local role, 선택 가능 여부, ready server 수 |
| Server | Server RID, lifecycle generation, descriptor revision, endpoint, weight, ready, service state, descriptor source, last failure |
| Client | connection intent 수, ready target 수, pending request 수 |
| Location | store configured 여부, ready·degraded state, 마지막 성공·실패 시각 |

Local role은 `(ChannelName, Role)`의 registration을 집계한 값이다. `client_and_server`는 Client와 Server
registration이 각각 존재한다는 snapshot projection이며 builder role이나 registration key가 아니다.

Automatic fanout subscriber snapshot은 MeshName을 요구하지 않으며 다음 값을 함께 제공한다.

| 영역 | 공개 관찰 값 |
|---|---|
| Channel | ChannelName, current automatic connection intent 수, ready connection 수 |
| Publisher entry | Publisher RID, lifecycle generation, descriptor revision, endpoint, connection intent 여부, ready 여부, state, last failure |
| Location | store configured 여부, ready·degraded state, 마지막 성공·실패 시각 |

Publisher entry는 마지막으로 성공한 resolve 결과에서 관찰한 현재 descriptor identity와 그 descriptor에 대한
연결 결정을 나타낸다. `ConnectionIntent`가 `true`이면 automatic subscriber가 그 endpoint를 현재 연결 대상으로
유지한다. Draining descriptor는 `excluded_draining` entry로 관찰하지만 connection intent에 포함하지 않는다.
더 낮은 generation·revision이나 만료된 owner lease는 current snapshot entry를 대체하지 않으며
`excluded_stale` event로만 관찰한다. 따라서 stale candidate를 진단하기 위해 현재 connection set을 오염시키지
않는다.

`ConnectionIntent=true`는 descriptor가 연결 계획에 들어가 `connect` operation이 수락되었다는 뜻일 뿐
transport readiness가 아니다. Runtime은 publisher descriptor마다 전용 SUB socket을 사용한다.
`Ready=true`와 `ReadyConnectionCount`는 그 socket이 native connection-ready 상태이고 같은 socket에서 첫
valid application fanout record 또는 liveness beacon을 받은 뒤에만 반영한다. `disconnected` publisher
event는 실제 native disconnect 또는 15초 inbound timeout을 관찰한 뒤 발생한다. Planner의 desired set,
`connect` 반환과 내부 active target 목록을 ready evidence로 사용하지 않는다.

RID와 endpoint는 진단 snapshot에 포함할 수 있지만 metric label로 사용하지 않는다. snapshot은 호출이
끝난 뒤에도 안전한 immutable value이며 native handle이나 caller buffer를 보유하지 않는다.
RouteMesh Channel, ClientServer Server와 node-wide placement weight는 public configuration과 같은 signed
integer `0..10000` 값을 제공한다. Monitoring projection이 값을 좁은 unsigned type으로 변환하거나
truncate하면 안 된다.

Operational query는 global ActorId 또는 SpotId의 current ref를 exact 조회하거나 object kind·stable type별 current
authority를 page로 열거한다. Page size는 1..1000이고 encoded 결과는 4 MiB 이하다. Query item은 global ID,
ObjectGeneration, MeshName, NodeRid, state와 stable type을 제공한다. 이 query는 application messaging target 목록이나
placement selector가 아니며 unbounded list를 제공하지 않는다. Missing, Creating과 Store failure를 monitoring
runtime의 negative cache에 보관하지 않는다.

snapshot에는 monotonic `Sequence`와 관찰 시각을 포함한다. 같은 MeshNode, 같은 ClientServer Channel 또는
같은 automatic fanout Channel에서 더 큰 sequence가 더 나중의 상태를 뜻한다. 서로 다른 source의 sequence를
전역 시계처럼 비교하지 않는다.

## 3. Event identifiers

공통 event identifier는 아래 문자열로 고정한다. 언어별 enum이나 record 이름은 달라도 identifier 값은
바꾸지 않는다.

| Identifier | 발생 조건 |
|---|---|
| `zlink.runtime.mesh_node.state_changed` | MeshNode lifecycle 또는 ready state 변경 |
| `zlink.runtime.mesh_node.peer_changed` | peer admission, ready, generation 또는 service state 변경 |
| `zlink.runtime.mesh_node.channel_changed` | channel weight, ready member 수 또는 선택 가능 상태 변경 |
| `zlink.runtime.mesh_node.mailbox_changed` | application 또는 infrastructure mailbox 상태 변경 |
| `zlink.runtime.object.placement_changed` | create reservation, Ready·abort, capacity exhaustion 또는 relocation으로 object placement 집계가 변경 |
| `zlink.runtime.mesh_node.routing_id_conflict` | automatic RID descriptor owner claim이 active conflict로 실패 |
| `zlink.runtime.host.termination_changed` | Retire·Shutdown intent, runtime state, sealed-work 또는 terminal result 변경 |
| `zlink.runtime.relocation.changed` | Standalone Actor·User Spot aggregate·Instance Spot relocation phase 또는 recovery 상태 변경 |
| `zlink.runtime.client_server.state_changed` | ClientServer local role, lifecycle 또는 ready state 변경 |
| `zlink.runtime.client_server.server_changed` | server generation, revision, endpoint, weight, ready 또는 service state 변경 |
| `zlink.runtime.fanout.publisher_changed` | automatic subscriber의 publisher 연결 대상, ready·disconnected·reconnecting 상태, draining 제외 또는 stale candidate 제외가 변경 |
| `zlink.runtime.location.store_changed` | Redis location store의 ready·degraded state 변경 |

모든 event는 identifier, sequence, timestamp와 source 종류를 가진다. RouteMesh event는 MeshName과 source
RID, ClientServer event는 ChannelName과 조건부 Server RID를 가진다. Fanout runtime event는 ChannelName을
가진 닫힌 두 variant다. Publisher changed variant는 변경·제외 대상인 immutable publisher event entry 전체를
필수로 가진다. 이 entry는 해당 전이 시점의 connection intent와 ready 여부를 포함하므로 event 소비자가 별도
private socket 상태를 조회할 필요가 없다. Location changed variant는 같은 시점의 immutable Location snapshot을
필수로 가지며 publisher entry를 요구하지 않는다. Publisher가 0개인 store degraded·recovered 전이도 이
variant로 표현한다. 해당 event에 필요한 경우에만 peer RID, lifecycle generation, descriptor revision, weight,
mailbox domain, message kind, reason과 service state를 추가한다. Payload와
application metadata를 event에 복사하지 않는다.

Placement event는 object kind, stable type, outcome, reason, typed capacity bundle과 현재 node aggregate만
제공한다. Capacity snapshot의 `limit`은 설정값 `0`을 그대로 사용하며 제한이 없다는 뜻이다. Spot type
목록은 등록한 User·Instance Spot capability로 제한하고 Actor stable type별 capacity를 만들지 않는다.
Entry Spot은 Spot 집계에서 제외하지만 Entry Spot에 존재하는 Actor는 Actor 전체 집계에 포함한다.
Activation concurrency의 active·limit은 별도 field로 제공하며 population reserved count와 합치지 않는다.
Global ActorId, SpotId, owner token과 generation은 event나 metric label에 넣지 않는다. 개별 create·message 실패는
[message flow tracing](52-message-flow-tracing.ko.md)의 기존 `zlink.message_flow` event와 operation result에서
관찰한다. RID conflict event는 retry attempt, configured prefix와 terminal 여부를 제공하지만 생성한 RID 후보는
metric label에 넣지 않는다.

| Fanout event variant | Identifier | 필수 payload |
|---|---|---|
| Publisher changed | `zlink.runtime.fanout.publisher_changed` | 변경·제외 대상 publisher entry |
| Location changed | `zlink.runtime.location.store_changed` | current Location snapshot |

Fanout publisher change event는 `connecting`, `ready`, `disconnected`, `excluded_draining`, `excluded_stale`,
`reconnecting`으로 바뀔 때마다 발생한다. Publisher가 정상 제거되어 current snapshot에서 사라지는 경우에도
실제 native disconnect를 관찰한 event에는 제거 직전 identity와 `disconnected` state를 가진 entry를 포함한다.
같은 Publisher RID의 더 큰
lifecycle generation이나 같은 generation의 더 큰 descriptor revision을 적용해 다시 연결할 때는 새 identity와
`reconnecting` state를 가진 entry를 포함한다.

Event entry는 변경이나 제외 판단의 대상이며 current state authority가 아니다. 특히 `excluded_stale` entry는
거부한 candidate를 나타내므로 current snapshot의 publisher entry를 대체하지 않는다. Event를 받은 뒤 현재
연결 상태가 필요하면 같은 ChannelName의 최신 snapshot을 읽는다.

두 fanout variant는 서로의 payload를 nullable field로 함께 넣지 않는다. Identifier가 publisher changed이면
entry만, location changed이면 Location snapshot만 제공한다. 언어별 exact interface는 sealed hierarchy,
discriminated union 또는 variant로 이 닫힌 관계를 보존한다.

### 3.1 닫힌 상태 값

| 필드 | 값 |
|---|---|
| Framework runtime state | `preparing`, `serving`, `retiring`, `draining`, `stopped`, `error` |
| MeshNode service state | `starting`, `serving`, `draining`, `drained`, `force_stopping`, `stopped`, `faulted` |
| Peer state | `configured`, `connecting`, `admitted`, `ready`, `draining`, `disconnected`, `rejected` |
| ClientServer role | `client`, `server`, `client_and_server` |
| ClientServer server state | `configured`, `connecting`, `ready`, `draining`, `disconnected`, `rejected` |
| Fanout publisher connection state | `connecting`, `ready`, `disconnected`, `reconnecting`, `excluded_draining`, `excluded_stale` |
| Mailbox domain | `application`, `infrastructure` |
| Descriptor source | `manual`, `redis`, `manual_and_redis` |
| Store state | `not_configured`, `ready`, `degraded`, `stopped` |
| Placement outcome | `reserved`, `ready`, `aborted`, `capacity_exhausted`, `owner_stale`, `store_failed` |

정확한 오류 객체와 언어별 casing은 언어별 공개 인터페이스 문서가 정한다.
`Framework runtime state`는 host 종료를, `MeshNode service state`는 MeshNode lifecycle을 나타낸다. 두
상태를 같은 enum으로 합치거나 MeshNode enum의 이름과 숫자 값을 host state에 맞춰 바꾸지 않는다.

## 4. Event ordering과 coalescing

같은 MeshNode, ClientServer Channel 또는 automatic fanout Channel source의 event는 sequence 순서로
관찰된다. Event handler가 느려도 message dispatch와 claim progress를 막지 않는다. Bounded observer queue가
가득 차면 상태 변경 event를
coalesce할 수 있지만 다음 규칙을 지켜야 한다.

- 가장 최신 snapshot sequence를 잃지 않는다.
- backpressure와 drop 누계의 증가분을 합쳐도 count를 잃지 않는다.
- terminal termination과 relocation event를 drop하지 않는다.
- coalescing 또는 overflow 자체를 metric으로 기록한다.

event는 변화 알림이며 현재 상태의 authority는 snapshot이다. handler가 event sequence gap을 발견하면
최신 snapshot을 다시 읽어 상태를 맞춘다.

## 5. Observer 격리

Runtime event observer는 MeshName, ClientServer ChannelName 또는 automatic fanout ChannelName별 비동기
event stream을 여러 개 열 수 있다. 한 observer가 읽기를 중단하거나 느려도 다른 observer, message receive와
application callback 결과를 바꾸지 않는다. 각 stream은 호출 시 양수 capacity를 받고 독립 bounded queue를
사용한다.

Observer가 event를 받은 뒤 snapshot을 읽거나 Retire·Shutdown, send 또는 application operation을 호출해도 monitoring
lock을 재진입하게 하지 않는다. Observer 소비 코드의 예외는 application이 소유하며 runtime dispatch 결과를
바꾸지 않는다.

Observer 취소나 observation handle의 close는 해당 observer 등록 하나만 종료한다. 취소를 인식한
뒤에는 새 event를 해당 bounded queue에 넣지 않고 아직 소비하지 않은 queue 항목은 폐기한다. 이미
실행을 시작한 callback은 반환할 수 있지만 취소로 중단시키지 않는다. 취소·close가 반환된 뒤에는
새 callback을 시작하지 않는다. 이 종료는 다른 observer, snapshot sequence, automatic connection intent,
manual endpoint 집합, message dispatch와 runtime lifecycle을 바꾸지 않고 runtime event나 sequence를
새로 만들지 않는다. 언어별 표면은 이 종료를 cancellation exception, aborted iterator,
Reactive Streams subscription cancel 또는 observation handle close로 표현할 수 있다.

## 6. Startup validation

- 등록하지 않은 MeshName의 snapshot 또는 event stream을 요청하면 구성 오류다.
- 등록하지 않은 ClientServer ChannelName의 snapshot 또는 event stream을 요청하면 구성 오류다.
- 등록하지 않았거나 manual subscriber로만 등록한 fanout ChannelName의 automatic snapshot 또는 event stream을
  요청하면 구성 오류다.
- observer queue capacity가 0 이하이면 호출 인자 오류다.
- Redis location store가 없는 runtime은 location event를 만들지 않고 snapshot의 store state를 `not_configured`로 반환한다.
- Object role이 `Client` 또는 `Server`인데 Redis location store가 없으면 monitoring을 시작하기 전에 host startup이 실패한다.
- metric·trace 활성화 여부와 runtime snapshot 사용 가능 여부를 묶지 않는다.

## 7. 검증 요구

- MeshNode snapshot 하나로 peer, channel과 mailbox를 읽고, host termination state는 host runtime
  snapshot 하나에서 읽을 수 있다.
- Publish target 수와 target별 수락·실패 결과를 MeshNode snapshot이나 runtime
  event에 포함하지 않는다.
- ClientServer Channel snapshot 하나로 local role, ready server, weight, service state와 location 상태를 함께 읽을 수
  있으며 MeshName을 요구하지 않는다.
- peer lifecycle generation, descriptor revision과 실제 ready state를 별도 필드로 관찰할 수 있다.
- ClientServer server의 lifecycle generation, descriptor revision과 actual ready state를 별도 필드로 관찰할
  수 있다.
- Automatic fanout subscriber snapshot 하나로 current connection intent 수, ready connection 수, location
  상태와 publisher별 descriptor identity·state를 함께 읽을 수 있으며 MeshName을 요구하지 않는다.
- Fanout publisher의 ready·disconnect·draining 제외·stale 제외·reconnect 변화는 publisher changed event
  entry로 검증할 수 있다. Store degraded·recovered 변화는 publisher 수와 무관하게 location changed event의
  Location snapshot으로 검증한다. Raw socket monitor나 private runtime hook을 evidence로 사용하지 않는다.
- Fanout publisher 하나의 beacon timeout은 해당 publisher entry만 `disconnected`로 바꾸고 다른 publisher
  entry의 ready 상태를 변경하지 않는다.
- application callback이 대기 중이어도 infrastructure mailbox change와 request completion이 관찰된다.
- observer failure나 느린 소비가 dispatch, reply와 termination terminal result를 바꾸지 않는다.
- sequence gap 뒤 snapshot 재조회로 최신 상태를 복원할 수 있다.
- observer 하나를 취소하거나 close해도 다른 observer, automatic connection, manual endpoint 집합과
  message dispatch가 유지되며 취소한 observer에는 새 event가 전달되지 않는다.
- snapshot의 RID, endpoint, topic, Actor ID와 Spot ID가 metric label로 복사되지 않는다.
- Actor 전체·Spot 전체·Spot kind·stable type placement 집계가 active·reserved·limit을 구분하고 Location
  Store count와 일치한다.
- Entry Spot 자체는 Spot 집계에서 제외되고 Entry Spot의 Actor는 Actor 전체 집계에 포함된다.
- Activation concurrency 집계가 population capacity와 분리된다.
- Placement weight 0, capacity exhaustion과 reservation recovery가 descriptor projection 및 event와 일치한다.
- Operational query가 1000 item·4 MiB bound를 지키고 global ID의 current location만 반환한다.
- ActorId, SpotId, owner token과 generation이 event 또는 metric label에 포함되지 않는다.
