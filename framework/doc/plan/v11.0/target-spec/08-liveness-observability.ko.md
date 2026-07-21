# Service liveness와 observability

이 문서는 ZLink Framework 11.0 service runtime의 transport liveness, reconnect, immutable snapshot,
typed event, metric, message-flow trace와 correlation 계약을 정의한다. Observability는 dispatch와 lifecycle을
제어하지 않으며 observer failure가 application 결과를 바꾸지 않는다.

## 1. 책임 경계

Framework는 raw transport event, protocol admission, Location descriptor와 handler readiness를 합쳐 service
connection 상태를 판정한다. Application은 raw socket monitor, poller, service liveness frame과 reconnect timer를
직접 읽지 않는다.

다음 신호는 목적이 다르며 서로 대신 사용하지 않는다.

| 신호 | 책임 |
|---|---|
| Framework service liveness | RouteMesh·ClientServer probe·ACK와 fanout 단방향 beacon으로 half-open 상태 판정 |
| Transport disconnect event | FIN, RST와 local socket failure의 즉시 관측 |
| Location owner lease | Node·object owner authority와 process-pause fencing |
| Framework STREAM heartbeat | Application packet session의 ping·pong과 idle failure |
| Request timeout | Operation caller가 terminal result를 기다리는 상한 |

Framework public builder는 service liveness interval과 deadline을 노출하지 않는다. 각 언어 runtime은 binding의
public raw socket API와 공통 service protocol로 같은 liveness 결과를 구현한다.

## 2. Transport liveness

Framework service connection의 probe interval은 5초이고 peer deadline은 15초다. 이 profile은
RouteMesh, ClientServer와 classic fanout에 공통으로 적용하며 Channel, handler와 peer별 public option으로
반복하지 않는다. 양방향 service connection과 단방향 fanout은 아래의 서로 다른 wire 규칙을 사용한다.

RouteMesh와 ClientServer는 service admission이 성공하면 initial ready와 15초 deadline을 시작한다. Application
traffic과 무관하게 5초마다 probe tick을 실행하며 connection마다 outstanding non-zero probe ID는 최대 하나다.
Outstanding ID가 없으면 새 ID를 보내고, 있으면 같은 ID를 재전송한다. Current physical connection의 current
outstanding ID와 일치하는 첫 ACK만 deadline을 갱신하고 ID를 제거한다. Duplicate ACK, 이전 ID와 다른 connection의
ACK는 무시한다. Application frame을 포함한 다른 inbound traffic은 진단에만 사용하며 deadline을 갱신하지 않는다.
Probe와 ACK는 payload와 metadata를 포함하지 않으며 application queue나 handler에 전달되지 않는다. Deadline을
넘으면 peer를 not-ready로 바꾸고 connection을 닫는다.

Classic fanout은 PUB 송신과 SUB 수신이 단방향이므로 probe·ACK를 사용하지 않는다. Subscriber는 automatic
descriptor의 publisher마다, manual mode에서는 endpoint마다 전용 SUB socket과 receive loop를 하나씩 둔다.
Publisher는 application event 송신 여부와 무관하게 5초마다 topic frame `01 5A 4C 46 31`과
payload frame `5A 46 01 01`로 이루어진 정확히 두 frame의 beacon을 보낸다. Subscriber는 해당 전용 socket에서 valid application fanout
record 또는 exact beacon을 처음 받은 뒤 ready로 바꾸고, 15초 동안 둘 다 받지 못하면 그 publisher만
not-ready로 바꾼 뒤 연결을 다시 만든다. Beacon은 ACK, application dispatch, message-flow publish와 fanout
수신 metric을 만들지 않는다. Public fanout publish에서 reserved topic을 사용하면 호출 인자 오류다.

Reserved topic 예약은 exact topic 전체에만 적용하므로 같은 prefix의 다른 topic은 application topic이다.
Reserved topic인데 payload가 다르거나 frame 수가 2가 아니면 protocol error로 해당 publisher를 즉시
not-ready로 바꾸고, application delivery와 receive activity를 만들지 않는다.

RouteMesh와 ClientServer connection은 다음 조건을 모두 만족한 뒤 ready다.

1. Transport connection이 생성됐다.
2. Service handshake와 protocol version 검증이 끝났다.
3. Identity, lifecycle generation과 security admission이 성공했다.
4. Required handler와 local runtime이 application admission을 받을 수 있다.

Descriptor가 존재하거나 connect operation이 수락됐다는 사실만으로 ready가 되지 않는다. Orderly close와
transport disconnect는 service liveness deadline을 기다리지 않고 ready index에 즉시 반영한다. Transport event가 없는
half-open failure는 15초 안에 not-ready로 전환한다.

Fanout publisher connection은 descriptor 또는 manual endpoint와 연결한 전용 SUB socket에서 첫 valid
application record 또는 beacon을 받은 뒤 ready다. 한 publisher의 inbound timeout은 해당 socket과 publisher
entry만 변경하며 다른 publisher connection과 host state를 변경하지 않는다.

Peer 하나의 failure는 다른 ready peer와 host state를 `Error`로 바꾸지 않는다. Ready target이 없으면 해당
surface의 target-not-found 또는 route-not-connected 결과를 사용하며 timeout을 늘려 failure를 숨기지 않는다.

Reconnect는 same configured intent 또는 current discovery descriptor를 사용한다. RouteMesh와 ClientServer는
service admission을 다시 수행하고 이전 connection identity, ready state, request correlation과 session
binding을 재사용하지 않는다. Fanout은 해당 publisher의 전용 SUB socket을 새로 만들고 첫 valid receive 전에는
ready로 복원하지 않는다. 더 큰 peer lifecycle generation이 admission되면 이전 generation의 늦은 event와
frame은 current connection을 변경하지 못한다.

Connection loss와 reply가 경쟁하면 request owner가 terminal result 하나만 완료한다. Transport 수락 여부가
불명확하거나 이미 수락된 request를 다른 peer에 자동 재제출하지 않는다.

Location polling failure가 발생해도 established connection의 transport liveness는 계속 진행한다. 반대로
transport가 ready여도 만료된 descriptor lease와 object owner lease를 discovery, placement와 transfer authority로
사용하지 않는다.

## 3. Snapshot

Snapshot은 호출이 끝난 뒤에도 안전한 immutable value다. Native handle, runtime-owned buffer와 callback view를
보유하지 않는다. 각 source는 monotonic sequence와 observed timestamp를 제공한다. 서로 다른 source의 sequence를
global clock처럼 비교하지 않는다.

| Snapshot | 공개 관찰 값 |
|---|---|
| Host runtime | `FrameworkRuntimeState`, termination intent·deadline·result, sealed work, blocker, pending request·transfer·STREAM barrier |
| MeshNode | MeshName, RID, node generation, descriptor revision, endpoint, component state와 descriptor source |
| Peer | RID, generation, revision, endpoint, admission·ready state, Server ChannelName set과 last failure |
| Channel | ChannelName, local role·weight, ready target 수와 selectable 여부 |
| Logical Multicast | Submit·backpressure·drop 누계와 last remote·local target detail |
| MeshNode mailbox | Application·infrastructure claim의 active 여부와 pending work 집계 |
| Instance Spot | Type별 activating·ready·closing count, pending message·byte와 last activation outcome |
| Location | Store configured·ready·degraded state와 last success·failure time |
| ClientServer | Channel role, server identity·generation·weight·ready와 pending request 집계 |
| Automatic fanout | ChannelName, publisher descriptor identity, connection intent, actual ready와 last failure |

Host termination은 MeshName에 속하지 않으므로 host snapshot에서 한 번 제공한다. Component snapshot에 같은
termination result를 복제하거나 component enum을 host state로 사용하지 않는다.

Fanout publisher의 `ConnectionIntent`는 desired connection이 존재한다는 뜻이다. `Ready`는 publisher 전용 SUB
socket의 native connection-ready와 같은 socket의 첫 valid application record 또는 beacon 수신을 모두
관찰했다는 뜻이다. Planner target, connect return 또는 native-ready 하나만 actual ready evidence로 사용하지 않는다. Draining과
stale publisher descriptor는 current connection set에 넣지 않고 excluded diagnostic event로 관찰한다.

Snapshot에 RID, endpoint, Actor ID와 Spot RID를 진단 값으로 포함할 수 있지만 metric label로 복사하지 않는다.
Instance Spot snapshot은 type별 aggregate만 제공하며 owner ID, authority generation과 logical Spot 목록을 공개하지
않는다.

Application·infrastructure mailbox 집계는 기존 MeshNode snapshot에서만 제공한다. Mailbox identity를 열거하거나
mailbox만 조회하는 별도 public query는 제공하지 않는다. User Spot·Actor와 STREAM의 queue·session 집계는
runtime metric과 bounded diagnostic event가 소유하며, 이 값만을 위한 public query를 추가하지 않는다.

## 4. Runtime event

공통 runtime event identifier는 다음 문자열로 고정한다.

| Identifier | 발생 조건 |
|---|---|
| `zlink.runtime.mesh_node.state_changed` | MeshNode lifecycle 또는 ready state 변경 |
| `zlink.runtime.mesh_node.peer_changed` | Peer admission, ready, generation 또는 service state 변경 |
| `zlink.runtime.mesh_node.channel_changed` | Channel weight, ready target 수 또는 selectable 상태 변경 |
| `zlink.runtime.mesh_node.multicast_backpressured` | Logical Multicast가 backpressure result로 완료됨 |
| `zlink.runtime.mesh_node.multicast_dropped` | Local·remote target별 drop이 발생함 |
| `zlink.runtime.mesh_node.mailbox_changed` | Application·infrastructure mailbox state 변경 |
| `zlink.runtime.host.termination_changed` | Termination intent, host state, sealed work 또는 terminal result 변경 |
| `zlink.runtime.transfer.changed` | Actor·Instance Spot transfer phase 또는 recovery state 변경 |
| `zlink.runtime.client_server.state_changed` | ClientServer local role, lifecycle 또는 ready state 변경 |
| `zlink.runtime.client_server.server_changed` | Server generation, revision, endpoint, weight, ready 또는 state 변경 |
| `zlink.runtime.fanout.publisher_changed` | Publisher connection intent, ready, disconnect, reconnect 또는 exclusion 변경 |
| `zlink.runtime.location.store_changed` | Location Store의 ready·degraded state 변경 |

모든 event는 identifier, source-local sequence, timestamp와 source kind를 가진다. RouteMesh event는 MeshName,
ClientServer와 fanout event는 ChannelName을 제공한다. 필요한 event만 immutable peer entry, revision, mailbox
domain, count, reason과 state를 추가한다. Payload와 application metadata value를 event에 복사하지 않는다.

Fanout publisher event는 `connecting`, `ready`, `disconnected`, `reconnecting`, `excluded_draining`,
`excluded_stale`의 닫힌 state를 사용한다. Publisher change와 Location Store change는 서로 다른 event variant와
필수 payload를 가지며 nullable field를 한 record에 합치지 않는다.

Event는 변화 알림이고 current state authority는 snapshot이다. Same source event는 sequence 순서로 관찰한다.
Bounded observer queue가 가득 차면 non-terminal state event를 coalesce할 수 있지만 latest snapshot sequence와
counter delta를 잃지 않아야 한다. Host termination과 transfer terminal event는 drop하지 않는다. Sequence gap을
관찰한 consumer는 latest snapshot을 다시 읽는다.

## 5. Message flow와 correlation

개별 message trace identifier는 `zlink.message_flow`, `zlink.dispatch_error`, `zlink.runtime_error` 세 값으로
고정한다.

`zlink.message_flow` phase는 `received`, `admitted`, `dispatched`, `completed`, `replied`, `sent`,
`reply_received`, `backpressured`, `dropped`다. Phase는 delivery guarantee를 확대하지 않는다. `sent`는 local
transport admission, `admitted`는 target queue 또는 remote target set admission을 뜻하며 remote handler
completion을 뜻하지 않는다.

Surface는 `node`, `channel`, `spot`, `instance_spot`, `logical_multicast`, `actor`, `stream`,
`classic_fanout`, `actor_transfer`의 닫힌 값을 사용한다. Message kind는 `send`, `request`, `response`,
`error`, `publish`, `control`이다.

Trace event는 surface와 kind, outcome, 조건부 reason, logical route, correlation과 target count를 포함한다.
Payload body, metadata value, native handle, raw frame와 exception object를 포함하지 않는다. Channel trace는
`route_mesh`와 `client_server` physical route를 구분하지만 이 값을 handler dispatch key로 사용하지 않는다.

Request와 terminal reply는 opaque `correlation_id` 하나를 사용한다. 같은 causal work의 여러 hop과 fan-out
branch는 `flow_id` 하나를 공유한다. Correlation ID는 request matching authority이고 flow ID는 관측용이므로
deduplication, retry와 owner fencing에 사용하지 않는다.

`flow_id`는 lowercase hyphenated UUIDv7 36-byte 문자열이다. `flow_origin`은 `inbound`, `timer`,
`application`, `lifecycle` 중 하나다. New downstream request는 new correlation ID를 만들고 current flow ID를
이어받는다. Response와 error는 original correlation과 flow를 보존한다.

Runtime은 handler async context에서 flow를 보존하고 callback 종료 뒤 이전 context를 복원한다. Detached task와
Framework가 관리하지 않는 executor에는 implicit propagation을 보장하지 않는다. Logical Multicast와 classic
fanout branch는 root flow ID를 유지한다.

## 6. Metrics와 cardinality

Metric 이름은 lowercase dotted ASCII인 `zlink.<surface>.<name>` 형식을 사용한다. Time histogram 단위는 초,
byte counter 단위는 byte다. Counter는 단조 증가하고 observable은 scrape 시점 immutable snapshot을 읽는다.

Service runtime은 다음 metric family를 제공한다.

| Family | 필수 계기 |
|---|---|
| Peer·Channel | `zlink.mesh_node.peers.configured`, `.connected`, `.ready`, `zlink.mesh_node.channels.ready_members`, `zlink.mesh_node.channel.selections` |
| Request | `zlink.mesh_node.requests.inflight`, `zlink.mesh_node.request.duration`, `zlink.mesh_node.request.timeouts` |
| Multicast·drop | `zlink.mesh_node.multicast.submits`, `.targets`, `.pending`, `.backpressures`, `.drops`, `zlink.mesh_node.messages.dropped` |
| Mailbox·turn | `zlink.mesh_node.mailbox.queue.depth`, `.active`, `.wait.duration`, `zlink.mesh_node.turn.duration` |
| Spot·Actor | `zlink.spot.count`, `.queue.depth`, `.queue.wait.duration`, `zlink.actor.count`, `.queue.depth`, `.queue.wait.duration` |
| Instance Spot | `zlink.instance_spot.activations`, `.activation.duration`, `.pending.messages`, `.pending.bytes`, `.claim.conflicts`, `.takeovers` |
| Transfer·checkpoint | `zlink.transfer.started`, `.completed`, `.duration`, `.recovered`, `.journal.messages`, `zlink.checkpoint.bytes` |
| STREAM | `zlink.stream.connections.active`, `.opened`, `.closed` |
| Termination | `zlink.termination.state`, `.duration`, `.blocked`, `.forced` |
| Location·fanout | `zlink.location.records`, `.store.errors`, `.owner_lease.renew.failures`, `.owner_lease.renew.lateness`, `zlink.fanout.published`, `.received`, `.dropped` |
| Observer | `zlink.observability.events.overflow` |

Metric label에는 Actor ID, Spot RID, session ID, endpoint, connection ID, transfer ID, correlation ID와 flow ID를
넣지 않는다. `mesh_name`, bounded `channel_name`, object kind·type, policy, state, reason과 outcome처럼 configured
bounded set만 사용한다. Instance type과 state contract도 startup registration으로 bounded되어야 한다.

Metric reader와 exporter failure는 handler, reply, peer admission, transfer와 termination result를 바꾸지 않는다.
Metrics는 runtime control surface가 아니다.

## 7. Observer isolation과 privacy

Runtime event observer와 message-flow observer는 각각 독립 bounded queue를 사용한다. Slow observer와 callback
failure가 message receive, application turn, reply와 termination을 block하지 않는다. Observer user code를
transport receive와 mailbox worker에서 직접 실행하지 않는다.

Observer cancel 또는 close는 그 registration만 종료한다. Cancellation을 인식한 뒤 새 event를 넣거나 callback을
시작하지 않는다. 다른 observer, connection intent, manual endpoint, snapshot sequence와 runtime lifecycle은
변경하지 않는다.

Message-flow observer failure는 별도 runtime error sink에 `observer_failed`로 보고한다. Runtime error sink
failure는 bounded fallback logger에만 기록하고 recursive runtime error event를 만들지 않는다.

Structured log와 trace에는 secret, payload, metadata value와 stack trace를 기본으로 기록하지 않는다. Flow와
correlation ID에 user ID, Actor ID, Spot RID와 endpoint를 encode하지 않는다.

## 8. Termination cleanup과 검증

`Retire`·`Shutdown` admission seal 뒤에도 accepted reply, transfer와 STREAM barrier에 필요한 liveness progress를
deadline까지 유지한다. Terminal cleanup은 service liveness timer, reconnect timer, observer subscription과 pending
callback을 connection보다 늦게 남기지 않는다.

다음 조건을 검증한다.

- Orderly disconnect는 service liveness deadline을 기다리지 않고 ready index에서 제외된다.
- Half-open connection은 15초 안에 not-ready가 된다.
- RouteMesh·ClientServer admission은 initial ready와 15초 deadline을 만들고 application traffic과 무관하게
  5초마다 probe tick을 실행한다.
- Connection마다 outstanding probe ID는 최대 하나이며 ACK 전 tick은 같은 ID를 재전송한다.
- Current connection의 current outstanding ID와 일치하는 첫 ACK만 deadline을 갱신하고 ID를 제거한다. Duplicate,
  previous ID, other-connection ACK와 application frame은 deadline을 갱신하지 않는다.
- Probe와 ACK는 infrastructure reserve에서 처리된다.
- Fanout은 publisher마다 전용 SUB socket을 사용하며 첫 valid application record 또는 exact beacon 전에는
  ready가 아니다.
- Fanout beacon은 reserved topic·payload의 두-frame record를 사용하고 ACK와 application dispatch를 만들지 않으며, 한
  publisher의 15초 timeout이 다른 publisher를 변경하지 않는다.
- Malformed reserved-topic record는 application delivery나 receive activity 없이 해당 publisher만 즉시
  not-ready로 바꾼다.
- Fanout publisher는 application traffic과 무관하게 5초마다 beacon을 전송한다.
- Store polling failure와 transport liveness가 서로의 authority를 대신하지 않는다.
- RouteMesh·ClientServer reconnect가 service admission을 다시 수행하고 stale generation state를 재사용하지
  않으며, fanout reconnect가 전용 SUB socket과 first-receive barrier를 새로 만든다.
- Snapshot은 immutable이고 event sequence gap 뒤 latest state를 복원할 수 있다.
- Terminal termination·transfer event와 metric counter delta가 observer overflow에서 유실되지 않는다.
- Observer·logger·metric exporter failure가 dispatch와 terminal result를 바꾸지 않는다.
- Request surface마다 terminal trace event가 한 번 발생한다.
- Instance one-way activation failure가 `instance_spot` drop으로 관측되고 hidden retry를 만들지 않는다.
- Metric label cardinality가 configured bounded 집합 안에 있다.
- Termination cleanup 뒤 service liveness·reconnect timer, observer callback과 runtime-owned resource가 남지 않는다.
