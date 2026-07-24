# Runtime Metrics — 공통 스펙

[스펙 목차](../README.ko.md) · [Runtime monitoring](50-runtime-monitoring.ko.md) ·
[Message flow tracing](52-message-flow-tracing.ko.md) · [Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0이 제공하는 집계 runtime metric의 이름, 종류, 단위와 label 계약을
정의한다. 이 문서는 “MeshNode·Spot·Actor·STREAM의 병목과 실패를 시계열로 관찰하면서 label 수가 업무
object 수에 비례해 증가하지 않게 하려면 어떤 계기를 제공해야 하는가?”라는 질문에 답한다.

현재 상태와 typed event는 [50 Runtime monitoring](50-runtime-monitoring.ko.md), message 한 건의 기록은
[52 Message flow tracing](52-message-flow-tracing.ko.md), host termination 계기는
[54 Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)이 소유한다. metric은 dispatch 결과를 바꾸는 제어
표면이 아니다.

## 2. 공통 규칙

- 계기 이름은 lowercase dotted ASCII인 `zlink.<surface>.<name>` 형식을 사용한다.
- 이름, label key와 닫힌 label value는 모든 언어에서 byte 단위로 같다.
- 시간 histogram의 단위는 초(`s`), byte counter의 단위는 `By`, 나머지는 중괄호 count unit을 쓴다.
- counter는 단조 증가하고, updown은 runtime event에서 증감하며, observable은 scrape 시점 snapshot을
  읽고, histogram은 operation별 sample을 기록한다.
- metric reader·exporter failure는 application callback, reply, peer admission과 host termination 결과를 바꾸지
  않는다.

## 3. MeshNode 계기

### 3.1 Peer와 channel

| 계기 | 종류 | 단위 | Label | 의미 |
|---|---|---|---|---|
| `zlink.mesh_node.peers.configured` | observable | `{peer}` | `mesh_name`, `source` | descriptor에 존재하는 peer 수 |
| `zlink.mesh_node.peers.connected` | observable | `{peer}` | `mesh_name`, `source` | transport가 연결된 peer 수 |
| `zlink.mesh_node.peers.ready` | observable | `{peer}` | `mesh_name`, `source` | admission과 handler readiness를 통과한 peer 수 |
| `zlink.mesh_node.channels.ready_members` | observable | `{member}` | `mesh_name`, `channel_name` | select-one에 사용할 수 있는 member 수 |
| `zlink.mesh_node.channel.selections` | counter | `{selection}` | `mesh_name`, `channel_name`, `outcome` | ChannelName select-one 결과 누계 |
| `zlink.mesh_node.requests.inflight` | updown | `{request}` | `mesh_name`, `surface` | reply를 기다리는 request 수 |
| `zlink.mesh_node.request.duration` | histogram | `s` | `mesh_name`, `surface`, `outcome` | submit부터 terminal completion까지의 request 시간 |
| `zlink.mesh_node.request.timeouts` | counter | `{request}` | `mesh_name`, `surface` | request timeout 누계 |

`source`는 `manual|redis|manual_and_redis`, selection `outcome`은
`selected|no_member|not_ready|draining`, `surface`는
`node|channel|spot|instance_spot|actor`의 닫힌 값이다.

### 3.2 Logical Multicast와 drop

| 계기 | 종류 | 단위 | Label | 의미 |
|---|---|---|---|---|
| `zlink.mesh_node.multicast.submits` | counter | `{operation}` | `mesh_name`, `channel_name`, `outcome` | publish operation 결과 누계 |
| `zlink.mesh_node.multicast.targets` | histogram | `{target}` | `mesh_name`, `channel_name` | operation마다 선택한 remote MeshNode 수 |
| `zlink.mesh_node.multicast.pending` | updown | `{operation}` | `mesh_name`, `channel_name` | admission을 기다리는 operation 수 |
| `zlink.mesh_node.multicast.backpressures` | counter | `{operation}` | `mesh_name`, `channel_name`, `reason` | remote ROUTER의 용량 부족 또는 send timeout 누계 |
| `zlink.mesh_node.multicast.drops` | counter | `{target}` | `mesh_name`, `channel_name`, `reason` | local 또는 remote target별 drop 누계 |
| `zlink.mesh_node.messages.dropped` | counter | `{message}` | `mesh_name`, `surface`, `message_kind`, `reason` | Framework가 원인을 확인한 one-way drop 누계 |

multicast `outcome`은 `accepted|backpressured|timed_out|shutdown`, `reason`은
`backpressure|send_timeout|target_closed|shutdown`의 닫힌 값이다. message drop `reason`은
`no_handler|decode_error|backpressure|stale_target|shutdown`의 닫힌 값이다.

Logical Multicast의 `targets`는 remote node 수만 기록한다. local matching Spot 수와 topic별 수를 label로
분해하지 않는다.

### 3.3 Mailbox와 turn

| 계기 | 종류 | 단위 | Label | 의미 |
|---|---|---|---|---|
| `zlink.mesh_node.mailbox.queue.depth` | observable | `{item}` | `mesh_name`, `domain`, `owner_kind` | owner application·infrastructure pending work 수 |
| `zlink.mesh_node.mailbox.active` | observable | `{turn}` | `mesh_name`, `domain`, `owner_kind` | 현재 실행 중인 turn 수 |
| `zlink.mesh_node.mailbox.wait.duration` | histogram | `s` | `mesh_name`, `domain`, `owner_kind` | admission부터 turn 시작까지의 시간 |
| `zlink.mesh_node.turn.duration` | histogram | `s` | `mesh_name`, `owner_kind`, `outcome` | application turn 실행 시간 |

`domain`은 `application|infrastructure`, `owner_kind`는 `node|channel|spot|actor|stream`, turn `outcome`은
`completed|failed|cancelled|shutdown`이다. owner instance identity는 label에 포함하지 않는다.

## 4. Object와 STREAM 계기

| 계기 | 종류 | 단위 | Label | 의미 |
|---|---|---|---|---|
| `zlink.spot.count` | updown | `{spot}` | `mesh_name`, `spot_kind` | 현재 Spot 수 |
| `zlink.spot.queue.depth` | observable | `{item}` | `mesh_name`, `spot_kind` | Spot application queue의 pending work 수 |
| `zlink.spot.queue.wait.duration` | histogram | `s` | `mesh_name`, `spot_kind` | Spot work admission부터 turn 시작까지의 시간 |
| `zlink.actor.count` | updown | `{actor}` | `mesh_name` | 현재 Actor 수 |
| `zlink.actor.queue.depth` | observable | `{item}` | `mesh_name` | Actor application queue의 pending payload 수 |
| `zlink.actor.queue.wait.duration` | histogram | `s` | `mesh_name` | Actor payload admission부터 turn 시작까지의 시간 |
| `zlink.object.capacity.active` | observable | `{object}` | `mesh_name`, `capacity_scope` | Location Store가 확정한 active population 수 |
| `zlink.object.capacity.reserved` | observable | `{object}` | `mesh_name`, `capacity_scope` | Location Store reservation이 확보한 population 수 |
| `zlink.object.capacity.limit` | observable | `{object}` | `mesh_name`, `capacity_scope` | Actor 전체 또는 Spot 전체 limit이며 0은 제한 없음 |
| `zlink.spot.type.capacity.active` | observable | `{spot}` | `mesh_name`, `spot_kind`, `stable_type` | 등록한 Spot type의 active 수 |
| `zlink.spot.type.capacity.reserved` | observable | `{spot}` | `mesh_name`, `spot_kind`, `stable_type` | 등록한 Spot type의 reserved 수 |
| `zlink.spot.type.capacity.limit` | observable | `{spot}` | `mesh_name`, `spot_kind`, `stable_type` | 등록한 Spot type의 limit이며 0은 별도 제한 없음 |
| `zlink.object.activation.active` | observable | `{activation}` | `mesh_name` | 현재 factory·initialization 실행 수 |
| `zlink.object.activation.limit` | observable | `{activation}` | `mesh_name` | population capacity와 분리된 activation concurrency limit |
| `zlink.relocation.started` | counter | `{relocation}` | `mesh_name`, `object_kind`, `policy` | Actor·Instance Spot relocation 시작 누계 |
| `zlink.relocation.completed` | counter | `{relocation}` | `mesh_name`, `object_kind`, `policy`, `outcome` | relocation terminal 결과 누계 |
| `zlink.relocation.duration` | histogram | `s` | `mesh_name`, `object_kind`, `policy`, `outcome` | prepare부터 terminal phase까지의 시간 |
| `zlink.relocation.recovered` | counter | `{relocation}` | `mesh_name`, `object_kind` | recovery coordinator가 이어서 처리한 relocation 수 |
| `zlink.relocation.journal.messages` | histogram | `{message}` | `mesh_name`, `object_kind` | relocation envelope에 포함한 accepted message 수 |
| `zlink.relocation.bytes` | histogram | `By` | `mesh_name`, `object_kind`, `policy` | immutable relocation envelope 크기 |
| `zlink.stream.connections.active` | updown | `{connection}` | `transport` | 현재 STREAM session 수 |
| `zlink.stream.connections.opened` | counter | `{connection}` | `transport` | STREAM session open 누계 |
| `zlink.stream.connections.closed` | counter | `{connection}` | `transport`, `close_reason` | STREAM session close 누계 |

`spot_kind`는 일반 Spot 계기에서 `entry|user|instance`이고 Spot type capacity에서는 `user|instance`만
허용한다. `capacity_scope`는 `actor|spot`이다. `stable_type`은 startup에 등록한 bounded User·Instance Spot
type만 사용하며 Actor type이나 address에서 얻은 값을 사용하지 않는다. Entry Spot은 Spot capacity 계기에
포함하지 않지만 그 안의 Actor는 `capacity_scope=actor`에 포함한다. `object_kind`는
`actor|user_spot|instance_spot`, `policy`는
`recreate|snapshot`, relocation `outcome`은 `completed|aborted|recovered|failed|shutdown`, `transport`는
등록 시점에 정해지는 닫힌 값이다. `close_reason`은
`client_close|idle_timeout|heartbeat_timeout|server_shutdown|protocol_error|transport_error`다.

Instance Spot activation은 다음 계기를 추가한다. `instance_spot_type`은 startup에 등록한 bounded type 이름만
사용하며 address payload에서 동적으로 만든 값을 label로 사용하지 않는다.

| 계기 | 종류 | 단위 | Label | 의미 |
|---|---|---|---|---|
| `zlink.instance_spot.activations` | counter | `{activation}` | `mesh_name`, `instance_spot_type`, `outcome` | owner claim부터 Ready 또는 terminal 실패까지의 결과 누계 |
| `zlink.instance_spot.activation.duration` | histogram | `s` | `mesh_name`, `instance_spot_type`, `outcome` | 첫 address resolve부터 Ready 또는 terminal 실패까지의 시간 |
| `zlink.instance_spot.pending.messages` | observable | `{message}` | `mesh_name`, `instance_spot_type` | activation barrier 앞에서 기다리는 message 수 |
| `zlink.instance_spot.pending.bytes` | observable | `By` | `mesh_name`, `instance_spot_type` | activation barrier 앞에서 예약한 payload byte 수 |
| `zlink.instance_spot.claim.conflicts` | counter | `{claim}` | `mesh_name`, `instance_spot_type`, `reason` | live authority·kind·type 충돌 누계 |
| `zlink.instance_spot.takeovers` | counter | `{takeover}` | `mesh_name`, `instance_spot_type`, `outcome` | 만료 owner row를 caller claim이 교체한 결과 누계 |

Activation `outcome`은 `ready|rejected|conflict|timed_out|shutdown|store_failure|fenced`, claim `reason`은
`authority|spot_kind|spot_type|closing`, takeover `outcome`은 `claimed|lost|failed`의 닫힌 값이다.

## 5. Host termination 계기

| 계기 | 종류 | 단위 | Label | 의미 |
|---|---|---|---|---|
| `zlink.termination.state` | observable | `{runtime}` | `state` | 현재 Framework runtime state 하나에 값 1을 기록 |
| `zlink.termination.duration` | histogram | `s` | `intent`, `outcome` | Retire·Shutdown 시작부터 terminal result까지의 시간 |
| `zlink.termination.blocked` | counter | `{operation}` | `reason` | admission을 바꾸지 않고 끝난 Retire 수 |
| `zlink.termination.forced` | counter | `{operation}` | `intent`, `reason` | bounded teardown으로 끝난 operation 수 |

`intent`는 `retire|shutdown`, `state`는 `preparing|serving|draining|stopped|error`, `outcome`은
`stopped|blocked|force_stopped`의 닫힌 값이다. Reason은
[54 Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)의 termination reason을 그대로 사용한다.

## 6. Location과 classic fanout 계기

| 계기 | 종류 | 단위 | Label | 의미 |
|---|---|---|---|---|
| `zlink.location.records` | observable | `{record}` | `scope_kind`, `scope_name`, `record_kind` | 유효한 descriptor·Spot·Actor record 수 |
| `zlink.location.store.errors` | counter | `{error}` | `operation` | Redis read·write·lease failure 누계 |
| `zlink.location.owner_lease.renew.failures` | counter | `{failure}` | `scope_kind`, `scope_name` | owner lease renew failure 누계 |
| `zlink.location.owner_lease.renew.lateness` | histogram | `s` | `scope_kind`, `scope_name` | 예정 시각 대비 owner lease renew 지연 |
| `zlink.fanout.published` | counter | `{message}` | `channel_name` | classic fanout publish 누계 |
| `zlink.fanout.received` | counter | `{message}` | `channel_name` | classic fanout receive 누계 |
| `zlink.fanout.dropped` | counter | `{message}` | `channel_name`, `reason` | Framework가 원인을 확인한 classic fanout drop 누계 |
| `zlink.observability.events.overflow` | counter | `{event}` | `source` | monitoring·trace observer queue overflow 누계 |

`record_kind`는 `mesh_node_descriptor|client_server_server_descriptor|fanout_publisher_descriptor|spot|instance_spot|actor`,
`scope_kind`는 `mesh|channel`의 닫힌 값이다. MeshNode·Spot·Actor record는 `mesh`와 MeshName을,
ClientServer server·fanout publisher descriptor는 `channel`과 ChannelName을 `scope_name`에 기록한다.
`operation`은 `read|compare_exchange|relocation_put|relocation_get|relocation_delete|lease_renew|release`, fanout
`reason`은 `backpressure|target_closed|shutdown`이다. classic fanout의 packet name과 내부 transport
filter 값은 metric label로 기록하지 않는다.

## 7. Label cardinality

허용 label은 등록 또는 enum으로 닫힌 값이어야 한다.

| 허용 | 금지 |
|---|---|
| `mesh_name`, `channel_name`, `scope_kind`, `scope_name`, 정적 `source`, `surface`, `message_kind`, `outcome`, `reason`, `domain`, `owner_kind`, `object_kind`, `policy`, `spot_kind`, 등록된 `instance_spot_type`, `record_kind`, `transport`, `close_reason`, `intent`, `state` | topic, Actor ID, Spot ID, RID, endpoint, session ID, relocation ID, user ID, correlation ID, flow ID, application metadata value, application state format·version |

MeshName, ChannelName과 `scope_name`도 host 등록값으로 닫힌 집합일 때만 label로 사용한다. 실행 중 payload나 metadata에서
새 label value를 만들지 않는다. packet name별 metric이 필요하면 startup에 등록된 bounded handler key만
별도 application metric에서 사용한다.

개별 Actor·Spot·message 흐름은 metric이 아니라 [52 Message flow tracing](52-message-flow-tracing.ko.md)의
event에서 확인한다.

## 8. Reader와 성능

metric은 언어 표준 meter 또는 registry에 연결하며 특정 exporter를 framework 필수 dependency로 요구하지
않는다. application은 같은 계기 이름과 label로 Prometheus, OpenTelemetry 또는 다른 backend에 export할
수 있다.

- metric 비활성 경로는 payload 복사와 label dictionary 생성을 하지 않는다.
- counter와 updown 갱신은 dispatch ordering을 바꾸지 않는다.
- observable reader는 immutable runtime snapshot을 읽고 application·infrastructure mailbox turn을 획득하지 않는다.
- histogram bucket과 aggregation은 exporter가 정하며 Framework는 unit과 sample 경계만 고정한다.
- reader callback failure는 마지막 정상 scrape를 소급해서 바꾸지 않는다.

## 9. 검증 요구

- 계기 이름, 종류, 단위와 닫힌 label value가 모든 언어에서 같다.
- publish operation의 backpressure와 target별 drop이 별도 counter에 기록된다.
- application·infrastructure mailbox backlog를 domain별로 관찰할 수 있다.
- Actor 전체·Spot 전체·Spot stable type의 active·reserved·limit이 monitoring snapshot과 일치하고 activation
  concurrency와 분리된다.
- topic, Actor ID, Spot ID, RID, endpoint, correlation ID와 flow ID가 어떤 metric label에도 나타나지 않는다.
- observer overflow와 metric reader failure가 message dispatch와 host termination 결과를 바꾸지 않는다.
- termination 계기와 label은 [54 Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)의 terminal result와 일치한다.
- Instance activation의 outcome·duration, pending budget, claim conflict와 takeover를 등록된 type 단위로
  관찰하고 Spot ID·owner ID·authority generation은 label로 사용하지 않는다.
- Instance one-way activation 실패가 `zlink.mesh_node.messages.dropped`의
  `surface=instance_spot`에 기록되고 별도 reply나 replay를 만들지 않는다.
