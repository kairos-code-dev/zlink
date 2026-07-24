# ZLink Framework API

[스펙 목차](README.ko.md) · [이전: 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md) ·
[다음: Channel topology](10-channel-topology.ko.md)

## 1. 목적

이 문서는 ZLink Framework 11.0.0의 언어 중립 public API family와 등록 규칙을 정의한다. 실제 타입,
generic 제약, overload와 비동기 반환 타입은 각 package의 언어별 스펙이 소유한다. .NET RouteMesh와
MeshNode의 정확한 인터페이스는
[.NET RouteMesh·MeshNode 인터페이스](server/languages/dotnet/interfaces/03-configuration-topology.ko.md)를 따른다.

### 1.1 Public contract와 runtime implementation의 경계

이 문서와 package별 공통 스펙은 언어에 관계없이 같아야 하는 public 동작을 소유한다. 각 언어의
exact interface 문서는 그 동작을 해당 언어의 타입, method, 반환값과 오류 표현으로 고정한다.
Runtime 내부 socket, queue, dispatch table과 adapter type은 public contract가 아니며 exact interface에
노출하지 않는다. 모든 언어의 exact interface는 공통 계약을 축소하지 않고 같은 public 동작을 투영한다.

## 2. Root 등록

Framework root는 process의 host lifecycle과 DI에 한 번 등록한다. Root configuration은 다음 기능을
제공한다.

| 기능 | 등록 결과 |
|---|---|
| [RouteMesh](01-glossary.ko.md#routemesh) | MeshName으로 [MeshNode](01-glossary.ko.md#meshnode) 하나를 등록한다 |
| ClientServer Channel | ChannelName으로 client 또는 server 역할 하나를 등록한다 |
| classic fanout | MeshNode와 독립된 publisher/subscriber channel을 등록한다 |
| STREAM node | STREAM endpoint와 session handler를 등록한다 |
| Location Store | owner, location, generation, relocation authority와 aggregate를 원자적으로 저장할 instance를 등록한다 |
| Relocation Store | cross-node relocation의 immutable state·journal·replay payload를 저장할 instance를 등록한다 |
| codec extension | typed payload serializer를 등록한다 |
| handler와 filter | dispatch handler, filter와 metadata policy를 등록한다 |
| worker | bounded worker scheduler의 동시성, idle timeout과 queue 상한을 설정한다 |
| network identity | listener가 공통으로 사용할 bind host와 advertised host를 설정한다 |
| deployment identity | target eligibility에 사용할 application version과 maintenance wave를 설정한다 |
| relocation limits | process 전체 outbound·inbound unit, `Capture`·`Restore` callback과 encoded payload in-flight 상한을 설정한다 |

같은 root를 process에 두 번 구성하거나 같은 [MeshName](01-glossary.ko.md#meshname)을 중복 등록하면 startup에서 설정 오류가 발생한다.
같은 [ChannelName](01-glossary.ko.md#channelname)을 서로 다른 RouteMesh 또는 ClientServer topology에 등록해도 역할과 관계없이 startup에서
실패한다.
Network identity의 공통값과 listener별 override는
[13 Network listener identity](13-network-listener-identity.ko.md)가 소유한다.

Root 등록은 process당 Framework runtime singleton 하나를 제공한다. 이 runtime은 host 전체를 대상으로
`Retire`와 `Shutdown`을 수행한다. MeshName, ChannelName이나 node RID별 drain operation은 제공하지 않는다.
State, terminal result, 기본 deadline, 반복 호출과 cancellation 계약은
[54 Host Retire, Shutdown & Handoff](54-graceful-drain-handoff.ko.md)가 소유한다.

Framework builder는 service liveness interval과 [deadline](01-glossary.ko.md#deadline)을 공개하지 않는다. Service runtime은 공통 profile을
내부에서 적용하며 orderly disconnect와 half-open 장애를 구분한다. 고정값, service liveness message와 reconnect
계약은 [55 Transport Liveness](55-transport-liveness.ko.md)가 소유한다.

## 3. RouteMesh 등록

RouteMesh 등록은 MeshName 하나를 받고 MeshNode builder를 반환한다. MeshNode builder는 다음 설정을
소유한다.

- explicit manual topology의 고정 routing ID 또는 automatic topology의 diagnostic prefix
- ROUTER bind endpoint와 transport option
- 0개 이상의 immutable ChannelName server membership과 outbound Channel route 선언
- manual peer connection intent
- node direct와 channel handler
- `None`, `Client`, `Server` 중 하나인 object role
- Object Server의 Entry Spot, user Spot, typed Actor와 actor-free Instance Spot factory
- 모든 object [factory](01-glossary.ko.md#factory)에 명시하는 `Disabled`, `Recreate`, `Snapshot` relocation policy
- node별 Actor·Spot 수와 Spot type별 capacity, node-wide placement weight
- route cache age와 stale route forwarding window
- Logical Multicast publish policy

MeshName은 물리 mesh의 이름이고 ChannelName은 논리 [membership](01-glossary.ko.md#membership)이다. 같은 MeshNode에 ChannelName을 여러
개 등록할 수 있다. `ChannelName` 호출은 별도 socket을 만들지 않는다. host가 시작된 뒤 MeshName,
[routing ID](01-glossary.ko.md#routing-id), endpoint와 membership set은 바꿀 수 없다.

Location option의 `RouteCacheMaxAge` 기본값은 15초이고 `RelocationForwardingWindow` 기본값은 30초다. 둘 다
0이면 route cache와 forwarding을 끈다. 양수이면 cache age가 forwarding window보다 최소 5초 작아야 한다.
실행 중 변경한 값은 새 cache entry와 새 relocation부터 적용한다. Forwarding window가 끝난 stale route는
stale-location 오류로 실패하며 Framework가 자동으로 다시 보내지 않는다.

RouteMesh Channel builder는 `Client`와 `Server` 역할을 구분한다. `Client`는 ChannelName을 해당 MeshNode의
outbound 송신 경로로 등록하지만 peer에게 target membership으로 광고하지 않으며 [weight](01-glossary.ko.md#weight)와 handler를 갖지
않는다. `Server`는 target membership과 handler namespace를 등록하며 weight와 handler 설정을 제공한다.
Server도 같은 ChannelName으로 outbound 호출을 시작할 수 있으므로 같은 이름의 Client 역할을 중복 등록하지
않는다.

Channel Server weight 범위는 0부터 10000까지이며 기본값은 100이다. 실행 중에는 server weight만 바꿀 수 있다.
`SetWeight(0)`은 server를 새 선택에서 제외하는 drain 설정이며 Client 역할을 표현하지 않는다.
`MaxMessageSize`를 포함한 topology와 socket 설정은 startup 뒤 바꿀 수 없다.

Object role은 닫힌 값이다. `None`은 object manager, factory와 placement runtime을 만들지 않는다. `Client`는
global Actor·Spot operation을 시작할 수 있지만 factory와 placement target을 제공하지 않는다. `Server`는
Client capability를 포함하며 factory와 placement target을 제공한다. `Client`와 `Server`는 [location store](01-glossary.ko.md#location-store)가
필수다. Factory는 Object Server builder에만 등록하며 같은 stable type을 중복 등록하면 startup이 실패한다.

Object Server는 Channel weight와 독립된 node-wide placement weight를 사용한다. 범위는 0부터 10000까지이고
기본값은 100이다. 0인 node는 새 placement와 relocation target에서 제외하지만 Ready object와 이미 reservation을
확보한 attempt는 유지한다. 비용이 다른 Actor와 Spot을 합산하는 node 전체 active object 제한은 두지 않는다.
Node별 Actor, User·Instance Spot 전체와 Spot stable type별 limit은 `0`이 기본값이며 제한 없음을 뜻한다.
양수이면 해당 node의 최대 개수이고 음수이면 startup configuration 오류다. Entry Spot은 Object Server
node마다 하나로 고정하며 configurable Spot count에 포함하지 않는다.

상한 판정은 Active count와 factory가 완료되기 전에 확보한 reserved slot을 함께 계산한다. Location Store가
reservation과 authority를 같은 transaction에서 확정하며 descriptor count는 후보 선택용 projection이다.
Capacity를 만족하는 후보가 없으면 `PlacementCapacityExhausted`로 완료한다. 기존 pending activation `128`
제한은 object population limit이 아니라 동시에 진행되는 activation을 보호하는 별도 admission 제한이다.

RouteMesh Channel Server, ClientServer Server와 node-wide placement weight는 모두 정수 `0..10000`, 기본값
`100`을 사용한다. Startup 설정과 runtime 변경에서 음수나 `10000`보다 큰 값은 configuration error다.
Weighted selection은 후보 weight 합계를 최소 64-bit 정수로 계산한다. Logical Multicast는 positive weight의
크기와 관계없이 eligible remote member를 한 번만 포함하며 weight `0`인 member는 제외한다.

Create call은 target RID, predicate나 selection callback을 제공하지 않는다.

Framework의 `MaxMessageSize = 0`은 Framework가 transport 기본값보다 작은 별도 상한을 두지 않는다는
뜻이다. 양수는 같은 byte 상한으로 적용하고 음수 값은 설정 오류다. Binding option 표현과 변환은
언어별 internals가 소유하며 application public API에 노출하지 않는다.

MeshNode builder에는 drain policy나 lifecycle command를 추가하지 않는다. Host의 continuity maintenance는
Framework runtime의 `Retire`, 일반 종료는 `Shutdown`이 수행한다. `Retiring`은 permit을 얻은 relocation unit부터
진행하면서 나머지 unit의 application 처리를 유지하는 state이고, `Draining`은 source application dispatch를 모두
끝낸 뒤 resource를 정리하는 state다. Channel weight 0을 lifecycle state 대신 사용하지 않는다.

## 4. Manual peer

Manual peer API는 두 가지 intent를 제공한다.

- endpoint만 지정하면 admission handshake가 remote RID를 확정한다.
- expected RID와 endpoint를 함께 지정하면 handshake RID가 일치할 때만 admission한다.

Runtime control은 connect intent 추가, endpoint 기준 intent 해제와 현재 intent 목록 조회를 제공한다.
Manual peer도 같은 MeshName, RID, generation, immutable ChannelName set과 security identity를 검증한다.
같은 endpoint의 transport 재접속은 Framework service runtime이 binding의 raw socket reconnect 계약을
사용해 관리한다. Application은 reconnect loop, pipe identity와 transport backoff를 구성하지 않는다.

## 5. 메시징 API family

Public messaging은 typed payload를 받고 Framework가 packet name과 codec을 결정한다.

| API family | 필요한 대상 | [handler namespace](01-glossary.ko.md#handler-namespace) |
|---|---|---|
| [Node direct](01-glossary.ko.md#node-direct) send/request | MeshName context와 target RID | MeshNode route handler |
| Channel send/request | ChannelName | ChannelName handler |
| [Spot](01-glossary.ko.md#spot) send/request | global Spot ID | current [Ready](01-glossary.ko.md#ready) Spot |
| Actor send/request | global Actor ID | current Ready Actor context |
| [Logical Multicast](01-glossary.ko.md#logical-multicast) publish | ChannelName과 topic | local Spot subscription |
| [classic fanout](01-glossary.ko.md#classic-fanout) publish | fanout channel name | fanout subscriber handler |
| STREAM send/request | session 또는 connector context | session packet handler |

Node direct와 channel operation은 target selection과 submit을 한 호출로 수행한다. 공개 `selectNode`,
`selectOne`, `selectMany` 단계는 제공하지 않는다.

Channel client는 ChannelName을 process-local route index에서 찾아 RouteMesh MeshNode 또는 ClientServer
client 하나를 선택한다. Index에 없는 이름은 `RequestTargetNotFound`로 끝내고 다른 MeshNode나
ClientServer client를 검색하거나 relay하지 않는다. 등록된 송신 경로에 ready target pipe가 없으면
`RouteNotConnected`, [ready target](01-glossary.ko.md#ready-target) snapshot 자체가 없으면 `RequestTargetNotFound`를 사용한다.

Logical Multicast도 ChannelName을 같은 process-local route index에서 찾아 [owner](01-glossary.ko.md#owner) RouteMesh MeshNode를
선택한다. 호출자는 MeshName이나 endpoint를 제공하지 않는다. 선택된 owner MeshName과 물리
route는 runtime monitoring과 message-flow 관측에 남지만 application 호출 인자로 되돌리지 않는다.

Application 호출은 raw `Message` 대신 업무 객체를 사용한다. Raw message는 bindings의 low-level
transport API와 명시적인 encoded payload 확장에만 둔다. Handler는 typed payload와 읽기 전용 context를
받으며 routing envelope를 직접 조립하지 않는다.

## 6. Call operation

Operation별 call object는 해당 기능에 유효한 설정만 제공한다.

- one-way send와 session Actor relay는 metadata 가능 여부와 관계없이 비동기 submit 결과를 제공한다.
- request는 metadata, reply timeout, 취소와 typed reply를 제공한다.
- Logical Multicast publish는 metadata, ChannelName, [topic](01-glossary.ko.md#topic)과 비동기 submit 하나를 사용한다.
- Spot과 Actor message 호출은 global ID를 보존하고 current Ready [authority](01-glossary.ko.md#authority)를 Framework 내부에서 resolve한다.
- Create·lookup이 반환한 ref는 exact incarnation을 변경하거나 session에 bind할 때 사용하며 일반 message의
  target으로 사용하지 않는다.
- STREAM 호출은 session identity와 packet correlation을 보존한다.

Server package의 one-way send·publish·명시적 STREAM reply는
[비동기 실행 정책](04-async-execution-policy.ko.md)의 async-only admission 계약을 따른다. Public call은
즉시 한 번만 시도하는 동기 terminator를 함께 제공하지 않는다. 별도 stream connector package의 send
builder는 connector package 계약을 따른다. Request timeout은 reply 대기에만 적용하고 send timeout은
transport admission 대기에 적용한다.
최초 non-blocking transport submit이 즉시 수락되면 Framework scheduler나 별도 work queue에 추가하지
않고 이미 완료되었거나 resolved된 언어별 awaitable을 반환한다.

Metadata는 Framework가 검증한 immutable [snapshot](01-glossary.ko.md#snapshot)으로 handler에 전달한다. 같은 key를 여러 번 설정하면
마지막 값이 사용된다. metadata 전체의 UTF-8 encoded 크기는 1024 bytes를 넘을 수 없다. reply는 request
metadata를 자동 복사하지 않는다.

## 7. Logical Multicast 결과

MeshNode와 Spot publish API는 publish 전용 전달 정책 option을 제공하지 않는다. Framework의 bounded I/O
executor는 publish operation을 send timeout까지 admission한다. Timeout 전에 시작하지 못하면
`DeadlineExceeded`, cancellation 또는 `RuntimeShutdown` 중 먼저 확정된 예외로 완료한다. 시작한 뒤에는
확정한 target snapshot을 정확히 한 번 처리하며 cancellation이나 shutdown으로 나머지 target 제출을
중단하지 않는다.

Remote와 local 각각의 snapshot, admitted, dropped 수와 remote unreachable 수는 public publish 결과가
아니라 monitoring metric과 runtime event로 제공한다. Snapshot target이 0개여도 정상 완료한다. Transaction
시작 뒤 remote capacity·연결 실패와 local Spot queue drop은 전체 publish를 rollback하거나 exceptional
completion으로 바꾸지 않는다. 앞에서 수락한 target은 뒤 target의 실패 때문에 취소하지 않는다.

## 8. Handler 등록과 dispatch

Handler key는 owner와 message kind를 포함한다.

| owner | dispatch key |
|---|---|
| Node direct | MeshName, route kind, [packet name](01-glossary.ko.md#packet-name) |
| Channel | ChannelName, send/request kind, packet name |
| Spot packet | Spot type, packet kind, packet name |
| Spot [subscription](01-glossary.ko.md#subscription) | Spot type, ChannelName, topic filter, packet name |
| Actor | Actor type, packet kind, packet name |
| STREAM session | stream node, session type, packet name |

같은 key의 중복 등록은 startup 설정 오류다. 서로 다른 ChannelName이나 owner에는 같은 packet name을
등록할 수 있다. Packet name은 registration descriptor가 한 번 결정하며 codec은 packet name에 관여하지
않는다.

모든 handler가 공유하는 base context는 MeshName을 요구하지 않는다. Channel handler와 filter context는
ChannelName, [message kind](01-glossary.ko.md#message-kind), packet name, metadata와 correlation 정보를 제공한다. Node direct handler
context는 물리 RID namespace가 실제 대상 계약이므로 MeshName과 source·target RID를 별도 context에
유지한다. 선택된 RouteMesh 또는 ClientServer 종류와 endpoint는 application handler가 아니라 monitoring과
message-flow 관측에서 제공한다.

Runtime reflection을 제공하는 언어는 명시한 assembly, module 또는 package 범위에서 handler를 찾을 수
있다. C++는 compile-time type과 명시 builder 등록을 사용한다. 어떤 방식을 사용해도 같은 dispatch key와
중복 검증 규칙을 적용한다.

### 8.1 Handler filter

Handler filter는 ChannelName의 send/request dispatch에만 적용한다. Node direct, Spot, Actor, [STREAM session](01-glossary.ko.md#stream-session),
Logical Multicast와 classic fanout dispatch에는 적용하지 않는다. Filter는 root에 등록한 순서대로 handler
앞에서 실행되며, 각 filter는 `next`를 최대 한 번 호출할 수 있다. `next`를 호출하면 남은 filter와 handler를
실행하고, 호출하지 않으면 해당 dispatch를 종료한다. Filter 또는 handler에서 발생한 예외는 같은 dispatch
실패 처리 규칙을 따른다. 각 filter는 해당 dispatch의 DI scope에서 resolve하며 handler와 같은 scoped
dependency를 사용한다. Root singleton으로 한 번 resolve해 여러 dispatch에서 공유하지 않는다.

언어별 exact interface는 filter context와 `next`의 구체적인 타입, 비동기 반환 타입과 short-circuit 결과
표현을 소유한다. 적용 범위와 실행 순서는 이 절이 소유하므로 언어별 구현이 다른 dispatch owner까지 filter를
임의로 확장하면 안 된다.

Framework scheduler는 ready owner의 bounded mailbox를 drain하고 Node, Spot과 Actor handler를 해당
application 실행 문맥에서 호출한다. Transport readiness는 application callback 인자가 아니다. Completion,
send-ready와 relocation control은 application handler가 점유할 수 없는 infrastructure 실행 영역에서 진행한다.

## 9. Codec

JSON은 typed message의 기본 codec이다. JSON만 사용하는 application은 메시지 타입마다 codec을 등록하지
않는다. Protobuf, MessagePack과 사용자 codec은 선택 extension package로 root codec registry에 등록한다.

송신할 업무 타입과 일치하는 extension이 없으면 JSON codec을 선택한다. 반면 수신 envelope가 명시한
non-JSON content-type과 일치하는 codec이 registry에 없으면 payload를 JSON으로 다시 해석하지 않고
`PayloadDecodeFailed`로 완료한다. 송신 타입 선택의 기본값과 수신 wire content-type 검증은 서로 다른
경계이므로 같은 fallback 규칙을 적용하지 않는다.

Codec은 업무 객체와 payload bytes 사이의 변환만 담당한다. Packet name, routing, correlation과 handler
선택은 Framework가 소유한다. Application metadata와 payload ownership은
[메시지 계약](03-message-model.ko.md)을 따른다. 내부 multipart 구조는 public Framework API에 노출하지
않는다.

언어별 server root와 [Stream Connector](01-glossary.ko.md#stream-connector)의 codec 등록 표면은 다음 exact interface가 소유한다.

| 언어 | server root 등록 | Stream Connector 등록 | exact interface owner |
|---|---|---|---|
| `.NET` | `Codecs.Use(extension)` | `ZlinkStreamConnectorOptions.PayloadCodec` | [server](server/languages/dotnet/interfaces/11-serialization.ko.md), [connector](stream-connector/languages/dotnet/03-stream-connector.ko.md) |
| Java | `codecs().use(extension)` | connector의 `typedCodec` option | [server](server/languages/java/interfaces/README.ko.md), [connector](stream-connector/languages/java/03-stream-connector.ko.md) |
| Kotlin | `codecs().use(extension)` | connector의 `typedCodec` option | [server](server/languages/kotlin/interfaces/README.ko.md), [Java/Kotlin connector](stream-connector/languages/java/03-stream-connector.ko.md) |
| Node.js | `codecs().use(extension)` | connector의 `codec` option | [server](server/languages/node/interfaces/README.ko.md), [connector](stream-connector/languages/typescript/03-stream-connector.ko.md) |
| C++ | `codecs().use(extension)` | `connector_options_t::typed_codec` | [server](server/languages/cpp/interfaces/01-common-runtime.ko.md), [connector](stream-connector/languages/cpp/03-stream-connector.ko.md) |

두 등록 표면은 같은 typed payload 계약을 투영하지만 server extension 객체와 connector option의 구체적인
타입까지 같아야 한다는 뜻은 아니다. JSON 기본 codec은 별도 등록 없이 사용하며, 다른 codec도 메시지마다
등록하지 않고 root 또는 connector instance에 한 번 등록한다.

## 10. Location Store와 Relocation Store

자동 discovery에 참여하는 classic fanout publisher, endpoint 없는 fanout subscriber와 Object Client·Server
role을 사용하는 host는 location store를 명시적으로 등록한다. 공식 production store는 별도 package로
제공하는 Redis extension이다. Application은
Redis store instance를 만들고 root의 일반 location store 등록 API에 전달한다. 전용 Redis 등록 함수는
제공하지 않는다.

Redis connection과 key prefix는 store instance를 만들 때 설정한다. 자세한 계약은
[Redis location store](41-location-store-redis.ko.md)가 소유한다. Process-local in-memory store는
한 process 안의 contract test에서만 사용할 수 있다.

Object role이 `None`이고 manual peer만 사용하는 host는 store 없이 MeshNode를 구성할 수 있다.
Object Server factory에 `Recreate` 또는 `Snapshot` policy가 하나라도 있거나 [Instance Spot](01-glossary.ko.md#entry-user-instance-spot) factory가 하나라도
있으면 opaque Relocation Store를 정확히 하나 등록해야 한다. Same-node Actor join은 Relocation payload를 만들지
않지만 factory 등록 시점에는 향후 cross-node join과 host `Retire`를 배제할 수 없으므로 이 조건을 완화하지 않는다.
Instance Spot factory가 없고 모든 factory가 `Disabled`인 same-node 구성만 Relocation Store를 생략할 수 있으며,
cross-node relocation은 capture 전에 거부한다.

Location provider가 owner·relocation authority compare-exchange, generic placement reservation·aggregate commit과
store clock capability를 제공하지 않거나 required Relocation Store가 없거나 둘 이상이면 socket bind 전에 startup
configuration error로 실패한다. 두 Store를 함께 등록하거나 Redis 구현을 직접 등록하는 전용 API는 제공하지
않는다. 공식 Redis Relocation Store와 cross-store 규칙은 [Redis Relocation Store](42-relocation-store-redis.ko.md),
Store interface와 이동별 사용 조건은 [40 Location runtime](40-location-runtime.ko.md)이 소유한다.

Location Store interface와 Relocation Store interface는 서로 상속하지 않는다. Root는 각각의 generic Store instance를
받는 두 registration operation을 독립적으로 제공한다. Actor·Spot별 Store, 두 Store를 한 번에 등록하는 bundle과
Redis 전용 registration operation은 public contract에 포함하지 않는다.

Location option은 process 전체 relocation 제한 다섯 개를 제공한다. 기본값은 active outbound 64, active inbound
64, concurrent `Capture` 8, concurrent `Restore` 8, encoded payload in-flight 268,435,456 bytes(256 MiB)다.
모든 값은 양수여야 한다. Payload byte에는 application state, 실행하지 않은 message queue, accepted journal,
timer logical registration·pending tick, relocation manifest와 Framework metadata를 포함한다.

Framework는 active unit, callback과 예상 payload byte permit을 모두 nonblocking으로 확보한 queue turn 경계에서만
source unit을 seal한다. Permit을 얻지 못하면 application message와 timer dispatch를 계속하고 intent notification을
다시 예약한다. 단일 User Spot aggregate의 encoded payload reservation이 byte 상한보다 크면 다른 payload가
in-flight가 아닌 동안에만 oversized aggregate 하나로 진행한다. Standalone Actor와 Instance Spot unit은 configured
byte gate 안에서만 admit한다. 실행 중 option 변경은 새 relocation admission에만 적용하며 이미 permit을 얻은
unit의 상한을 줄이지 않는다.

## 11. Classic fanout

Classic fanout은 root에서 독립 channel로 등록한다. Location store를 등록한 Publisher 역할은 Publisher RID를
고정하거나 RID allocation으로 얻고, 실제 bind가 끝난 listener endpoint를 fanout 전용 [descriptor](01-glossary.ko.md#descriptor)로
게시한다. Store가 없는 publisher는 application이 endpoint를 manual subscriber에 전달하는
방식으로 사용할 수 있으며 descriptor를 게시하지 않는다. Subscriber 역할은 endpoint를 받지 않는 automatic discovery와 하나 이상의 endpoint를
직접 등록하는 manual mode 중 하나를 선택한다. 두 mode를 같은 subscriber registration에 섞으면 startup
설정 오류다.

Automatic subscriber는 같은 ChannelName의 live publisher descriptor를 모두 연결하고 publisher마다 전용
SUB socket과 receive loop를 하나씩 만든다. Manual subscriber도 endpoint마다 전용 SUB socket을 사용한다.
다른 ChannelName,
다른 descriptor 종류, draining publisher와 만료된 owner lease는 연결하지 않는다. Automatic subscriber와
RID allocation을 설정한 publisher는 location store가 없으면 startup에서 실패한다. Manual subscriber와
고정 endpoint만 제공하는 publisher는 다른 분산
기능을 사용하지 않으면 location store 없이 명시한 endpoint만 연결한다. Fanout handler namespace는
packet name으로 구분한다. Publisher가 정한 topic은 handler context와 관측 정보에 보존하지만
handler 선택 key로 사용하지 않는다. Subscriber별 transport topic filter를 별도 public 설정으로
제공하지 않는다.

Framework는 fanout liveness에 사용하는 exact topic byte `01 5A 4C 46 31`을 내부용으로 예약한다. Public
fanout publish에 이 topic을 전달하면 호출 인자 오류다. 이 topic의 beacon은 handler와 application observer에
전달하지 않는다. Beacon과 publisher별 ready 판정은
[Transport liveness](55-transport-liveness.ko.md)가 소유한다.

Manual subscriber builder가 등록한 endpoint 집합은 공통 endpoint 연결 handle로도 제공한다. Application은
이 handle로 runtime 중 endpoint를 연결하거나 해제하고 현재 manual 연결 목록을 조회할 수 있다. 이 handle은
[automatic discovery](01-glossary.ko.md#automatic-discovery) 결과를 수정하는 표면이 아니며 같은 channel을 automatic mode로 전환하지 않는다.

Endpoint 없이 등록한 automatic subscriber의 current connection intent와 ready 상태는
[Runtime monitoring](50-runtime-monitoring.ko.md)의 fanout runtime snapshot과 event로만 관찰한다.
Publisher changed event는 publisher entry를, location changed event는 Location snapshot을 필수로 가지며 두
payload를 nullable field로 섞지 않는다. 이 표면은 읽기 전용이며 endpoint 연결·해제 operation을 제공하지
않는다. Manual endpoint 연결 handle은 automatic snapshot이나 event의 entry를 변경할 수 없다.

Fanout publish 완료는 local publisher transport가 event를 받아들였다는 뜻이다. Subscriber 수신과 handler
완료는 확인하지 않는다. 자세한 전달 계약은
[Channel 메시징](11-channel-messaging.ko.md#6-classic-fanout과의-경계)이 소유한다.

Classic fanout publish의 공통 입력은 ChannelName, topic과 typed event다. 정확한 언어별 interface는
topic을 명시하는 호출과 topic을 생략하는 typed 편의 호출을 함께 제공한다. 편의 호출은
Framework가 결정한 packet name을 topic으로 사용하며, 명시적 topic 호출을 제거하거나 의미를
바꾸지 않는다. Framework는 typed message 등록에서 packet name과 codec을 결정한다. 발행 호출은 publisher socket의
유한한 send timeout까지 admission을 기다리는 비동기 terminator 하나만 제공한다. 정상 완료에는 public
결과값이 없으며 remote·local target별 count를 집계하는 Logical Multicast publish result도 없다.
Subscriber가 0이어도 local publisher queue가 event를 수락하면 정상 완료한다. Monitoring에는 subscriber
수, 수신 또는 handler 완료 정보를 포함하지 않는다.

## 12. Spot, Actor와 STREAM owner

Spot factory와 typed Actor factory는 Object Server builder에 등록한다. User·Instance Spot type은 UTF-8
1..255 bytes의 case-sensitive stable name이며 언어 class 이름을 wire·Store identity로 사용하지 않는다.
Entry Spot ID는 Framework가 Object Server MeshNode lifecycle마다
`<prefix>-entry-<lowercase-canonical-uuid-v4>` 형식으로 발급하고 caller가 생성하지 않는다. MeshNode와
Entry Spot은 같은 diagnostic prefix를 사용하되 각각 별도의 UUID v4를 생성한다. 같은 lifecycle에서는 같은
Entry Spot ID를 유지하고 replacement lifecycle에서는 새 RID를 발급한다. MeshNode descriptor가 exact Entry
Spot ID와 lifecycle generation의 관계를 게시하며 Actor placement와 Entry Spot join은 이 mapping을
사용한다. Spot ID 문자열을 parsing하여 node 관계를 추론하지 않는다.

Entry Spot ID가 global Spot ID authority와 충돌하면 새 UUID나 reservation을 만들지 않고 startup을 즉시
`SpotIdConflict`로 끝낸다. Caller가 User·Instance Spot ID로
`<prefix>-entry-<lowercase-canonical-uuid-v4>` 예약 형식을 지정하면 Store operation이나 factory를 시작하기
전에 `InvalidConfiguration`으로 거부한다. Instance Spot은 actor-free lifecycle을 사용하며 Actor handler,
Actor membership과 Logical Multicast subscription을 등록할 수 없다.

Actor manager와 User Spot manager는 global ID를 받는 `Create`, `GetOrCreate`, `Find` family를 제공한다. Actor
`Create`·`GetOrCreate`는 Actor ID와 stable type을, User Spot `GetOrCreate`는 caller가 정한 [Spot ID](01-glossary.ko.md#spot-id)와 stable
type을 필수로 받는다. User Spot `Create`는 Framework가 global Spot ID를 생성한다. Optional fluent 설정은 initial Mesh,
최대 1 MiB로 encode되는 creation request와 deadline이다. 같은 option을 두 번
설정하면 `InvalidConfiguration`, terminal submit을 두 번 실행하면 `AlreadySubmitted`다.

Initial Mesh를 명시하면 해당 Mesh를 사용한다. 생략했을 때 object role Mesh가 하나면 자동으로 선택하고,
없으면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`다. 존재하지 않는 Mesh를 명시하면
`MeshNotFound`다. Instance Spot은 manager create family를 제공하지 않는다. Spot direct fluent call에서
Instance intent를 명시한 경우에만 Missing authority의 cold activation을 시작한다. Stable type을 생략하면
선택한 Mesh의 serving descriptor에 등록된 distinct Instance type이 하나일 때만 자동 선택한다. 여러 type이면
caller가 stable type을 명시해야 한다.

Missing Instance Spot call에서 source Framework는 최초 message와 operation identity·reply correlation·deadline,
optional metadata presence·frame, 선택한 Mesh·stable type과 target descriptor fence를 activation envelope에 넣어
eligible target으로 전송한다.
Source는 owner claim이나 reservation을 먼저 만들지 않는다. Target runtime은 complete [activation envelope](01-glossary.ko.md#activation-envelope)를
Relocation Store에 immutable recovery root로 저장한 뒤 current authority와 local exact instance를 확인하고,
Missing이면 자신을 owner로 generic reservation을 획득한다. Pending authority snapshot은 provider가 발급한
reservation fence와 recovery root receipt를 반환한다. CAS winner만 factory와 initialize를 실행하고 envelope
message를 durable activation inbox의 첫 record로 확정한다. Handler barrier를 유지한 상태에서 recovery
root·cursor를 포함한 `Ready`를 commit하고, 첫 record를 local queue head로 복원한 뒤 barrier를 연다. CAS loser는
local instance를 만들지 않으며 source는 `Ready` 뒤 같은 message를 다시 전송하지 않는다. 이 순서는 public
call을 check와 create로 나누거나 application에 target node를 노출하지 않는다.
Recovery pointer는 첫 handler terminal completion을 durable하게 기록하고 cursor를 inbox sequence까지 갱신한 뒤에만
Preserve CAS로 제거한다. Queue admission만으로 제거하지 않는다.

Create는 같은 ID의 Ready incarnation이 있으면 already-exists 오류로 끝난다. GetOrCreate는 같은 stable type의
Ready incarnation을 `Existing`으로 반환한다. Creating
[attempt](01-glossary.ko.md#creation-attempt)가 있으면 authority 변경을 deadline까지 기다린다.
다른 object kind나 stable type은 type-mismatch 오류다. Reservation CAS에서 패배한 caller는 별도 factory를
시작하거나 다른 owner를 선택하지 않는다. Creation request는 reservation 전에 immutable content reference와
hash로 저장한다. Factory는 logical key, ObjectGeneration과 attempt를 기준으로 at-least-once 실행되어도 같은
결과로 수렴해야 한다.

Actor creation callback이 승인하면 `Created`, 정상적으로 거절하면 `Rejected`를
publish한다. Callback exception은 `Failed`, recovery cleanup은 terminal을 만들지 않는
`Abort`로 구분한다. Creating을 기다린 서로 다른 operation은 Ready가 되면 `Existing`을
반환하고, rejection·failure cleanup으로 Missing이 되면 새 reservation을 경쟁한다.
앞선 attempt의 application reply를 공유하지 않는다. 같은 source Node RID·lifecycle
generation·`OperationId`의 재전송만 retained terminal을 읽는다. Terminal record에는
request correlation과 reply route가 없는 `creation-operation-terminal-v1` semantic
envelope를 저장하고, 재전송 reply는 현재 correlation과 reply route로 새로 encode한다.
`Rejected`와 `Failed`에서는 Ready authority와 active capacity를 만들지 않고 reserved
capacity를 반환한다. Terminal record는 original deadline 뒤 5분에 TTL로 제거한다.

Actor·User Spot·Instance Spot factory는 생성할 구체 type과 `Disabled`, `Recreate`, `Snapshot` 중 하나의
relocation policy를 같은 type registration에서 고정한다. `Disabled`는 cross-node relocation을 거부하고,
`Recreate`는 application state payload 없이 같은 logical ID의 typed factory를 실행한다. `Snapshot`은
Actor factory에 `ActorRelocationAdapter`, User·Instance Spot factory에 `SpotRelocationAdapter`를 지정한다.
Adapter의 `Capture`는 source instance에서 opaque byte sequence를 반환하고 `Restore`는 target factory가 만든
instance에 같은 byte sequence를 적용한다. Application이 byte format, version과 migration을 관리하며
Framework는 state contract ID, state type, relocation codec 등록 API를 제공하지 않는다. Same-node Actor join에는
relocation policy를 적용하지 않는다.
Relocation ID, target RID, relocation reference, journal cursor와 authority revision은 application callback에
노출하지 않는다.

Create와 lookup은 immutable `ActorRef` 또는 `SpotRef`를 반환한다. Ref는 global ID, non-zero unsigned 63-bit
`ObjectGeneration`, 조회 시점의 MeshName과 NodeRid를 담은 location snapshot이다. JSON generation은 decimal
string으로 encode한다. Ref는 runtime resource나 local object를 소유하지 않는다. 일반 message는 global ID로
current authority를 resolve하며 ref의 location을 target으로 고정하지 않는다. Destroy와 Close는 exact ref를
받는다. 같은 incarnation이 없으면 `false`, generation이 다르거나 relocation seal 중이면 typed stale·moving
오류로 끝나며 current ref를 다시 찾아 다른 incarnation을 종료하지 않는다.

Manager `Find`는 global ID의 current Ready ref를 반환한다. Actor가 현재 속한 User Spot을 조회하는 operation도
current `SpotRef`만 반환한다. Location operational query는 page size 1..1000과 encoded page 최대 4 MiB를
지키는 bounded page를 반환한다. Public object handle, directory, resolver와 unbounded list는 제공하지 않는다.

Actor factory는 Actor lifecycle을 만들고 Actor handler는 Actor context의 handler registry에 등록한다.
Actor message는 Actor mailbox로 직접 dispatch한다. Actor message를 Node callback이나 Spot packet handler가
다시 분류하지 않는다.

`Yield` terminator는 Channel request, Spot request, Actor request와 CPU·I/O worker call에만 제공한다.
Actor join, Actor·Spot create·get-or-create, send, publish, timer 등록, close와 destroy에는 제공하지 않는다.
`Yield`는 `SpotWide` User Spot 또는 Instance Spot의 shared execution gate를 잠시 반납할 수 있는 문맥에서만
유효하다. Entry Spot, `PerActor` User Spot, Node·Channel handler와 owner turn 밖의 client에서 호출할 수 있는
공통 call type을 사용하는 언어는 operation 제출 전에 문맥을 검사하고, 지원하지 않는 문맥이면 outbound
admission·queue 변경·turn 반납 없이 `InvalidConfiguration`으로 완료한다.

`SpotWide` User Spot의 member Actor가 request나 worker call을 `Yield`하면 User Spot execution gate만
반납하고 현재 Actor queue head를 실행할 권한은 유지한다. 따라서 다른 Actor·Spot handler·timer·lifecycle
callback은 진행할 수 있지만 같은 Actor의 다음 job은 현재 continuation이 gate를 다시 얻어 완료할 때까지
실행하지 않는다. 같은 Actor 자신에게 보낸 request도 queue를 우회하거나 inline으로 실행하지 않는다.

[Spot direct](01-glossary.ko.md#spot-direct) 시작 method는 global Spot ID와 payload를 받고 Spot 전용 send/request call을 반환한다. 이 call은
metadata와 terminal 외에 [Instance intent](01-glossary.ko.md#instance-intent), optional stable type과 initial Mesh를
설정할 수 있다. Instance intent가 없는 call은 existing-only이며 Missing에서 target-not-found다. Instance
intent를 가진 call은 Location resolve와 [cold activation](01-glossary.ko.md#cold-activation) claim을 분리하지 않고 하나의 terminal operation으로
수행한다. Existing authority가 있으면 저장된 kind·type과 current Mesh를 사용하며 cold activation option으로
현재 owner를 제한하거나 이동시키지 않는다.

STREAM node는 MeshNode와 독립적으로 등록할 수 있다. Session과 Actor binding을 사용하면 STREAM session
service가 raw STREAM과 MeshNode의 관계를 소유한다. Session ingress는 bound Actor mailbox로 전달되고,
Actor egress는 bound session FIFO를 사용한다. Actor dispatch capability를 활성화하는 설정은 MeshName을 받지
않는다. Startup 시 같은 root에 [Object Client](01-glossary.ko.md#object-role) 또는 Server role과 location store가 하나 이상 있어야 한다.

## 13. 오류 kind

언어별 exception과 error object는 다음 공통 kind와 숫자 값을 보존한다. 값 0도 유효한 kind다.

| 값 | kind | 기본 재시도 |
|---:|---|---|
| 0 | `ActorRouteNotFound` | no |
| 1 | `ActorCreateFailed` | no |
| 2 | `ActorAlreadyExists` | no |
| 3 | `ActorTypeMismatch` | no |
| 4 | `SpotCreateFailed` | no |
| 5 | `SpotRouteNotFound` | no |
| 6 | `SpotTypeMismatch` | no |
| 7 | `ActorSessionNotBound` | no |
| 8 | `HandlerNotFound` | no |
| 9 | `RouteHandlerNotFound` | no |
| 10 | `ActorDispatchHandlerNotFound` | no |
| 11 | `PayloadDecodeFailed` | no |
| 12 | `RouteNotConnected` | yes |
| 13 | `RequestTargetNotFound` | no |
| 14 | `RequestRejected` | no |
| 15 | `RequestProtocolError` | no |
| 16 | `RequestFailed` | no |
| 17 | `WorkerQueueFull` | no |
| 18 | `WorkerTimedOut` | no |
| 19 | `WorkerFailed` | no |
| 20 | `ActorLocationStale` | yes |
| 21 | `ActorCreateRejected` | no |
| 22 | `ObjectClientNotConfigured` | no |
| 23 | `MeshSelectionRequired` | no |
| 24 | `MeshNotFound` | no |
| 25 | `InvalidConfiguration` | no |
| 26 | `AlreadySubmitted` | no |
| 27 | `ActorGenerationStale` | no |
| 28 | `ActorMoving` | yes |
| 29 | `DeadlineExceeded` | yes |
| 30 | `PlacementCapacityExhausted` | yes |
| 31 | `RoutingIdConflict` | no |
| 32 | `SpotGenerationStale` | no |
| 33 | `SpotMoving` | yes |
| 34 | `RelocationDataLost` | no |
| 35 | `SpotIdConflict` | no |
| 36 | `RuntimeShutdown` | no |

`RouteNotConnected`는 알려진 target의 pipe가 준비되지 않은 상태이고, `RequestTargetNotFound`는 등록한
송신 경로에 현재 선택 가능한 target snapshot이 없거나 ChannelName 송신 경로 자체가 없는 상태다.
`ActorLocationStale`은 cached route와 forwarding mapping으로 current owner에 도달할 수 없는 상태다.
Generation stale은 exact ref가 current incarnation과 다른 상태이고 moving은 pre-commit seal이 새 application
admission을 거부한 상태다. `RelocationDataLost`는 Location authority가 publish한 immutable Relocation root를 영구적으로
읽을 수 없거나 checksum·inventory digest가 일치하지 않는 상태다. 이미 commit된 owner·membership을 source로
rollback하지 않으며 같은 reference를 다시 시도해 복구 가능한 오류로 분류하지 않는다.
`RoutingIdConflict`는 MeshNode transport RID owner claim 충돌이고, `SpotIdConflict`는 global Spot ID
namespace의 Entry·User·Instance Spot identity claim 충돌이다. `RuntimeShutdown`은 host shutdown으로
새 operation admission이 닫힌 상태다.

### 13.1 Operation 결과 변환

Framework는 target selection과 transport admission 결과를 다음 공통 결과로 변환한다. Node direct call은
Node RID를, Spot·Actor message는 global ID를, session binding은 exact object generation과 binding token을
유지한다. 물리 peer lifecycle generation은 public commitment가 아니다.
RouteMesh·ClientServer select-one ChannelName은 성공한 admission 전까지 현재 eligible member를 다시 선택할
수 있지만 수락 또는 terminal completion 뒤에는 같은 operation을 다시 제출하지 않는다.

| 관찰한 조건 | Framework 결과 |
|---|---|
| 해당 operation family의 source outbound admission이 operation을 수락함 | one-way send·publish는 결과값 없이 정상 완료하고 request는 pending completion으로 전환 |
| 일반 one-way의 첫 submit이 backpressured임 | send timeout까지 send-ready를 기다린다. Timeout 전 capacity가 생기면 한 번 제출하고, deadline이 먼저 끝나면 `DeadlineExceeded` exception으로 완료 |
| Logical Multicast snapshot 처리 중 remote capacity가 부족함 | 앞에서 수락한 target은 유지하고 target별 실패를 monitoring metric·event에 기록한다. Public result나 전체 rollback은 만들지 않음 |
| 알려진 direct target의 route가 준비되지 않음 | `RouteNotConnected` |
| Actor·Spot authority 또는 Node·Channel 송신 경로가 없음 | operation별 기존 route-not-found·`MeshNotFound`·`RequestTargetNotFound` exception |
| target admission seal 또는 application policy가 거부함 | `RequestRejected` 또는 해당 one-way rejection 결과 |
| host [shutdown](01-glossary.ko.md#shutdown)으로 신규 admission이 닫힘 | `RuntimeShutdown` exception |
| invalid argument·state, 지원하지 않는 operation 또는 내부 불변 조건 위반 | 언어별 local call 오류. remote error reply로 바꾸지 않음 |

`DeadlineExceeded`는 일반 one-way admission waiter가 family별 send timeout까지 수락되지 않았을 때
Framework가 만드는 exception이다. Cancellation은 해당 언어의 cancelled awaitable로 표현한다. Invalid
argument·handle·state, 이미 사용한 reply token과 중복 terminator 실행은 exceptional completion이다.
STREAM reply의 유효한 첫 terminator는 transport 시도 전에 one-shot token을 원자적으로 소비한다.
Backpressure, timeout 또는 cancellation으로 완료되어도 해당 token을 다시 사용할 수 없다. 같은
token의 두 call이 경쟁하면 하나만 transport admission을 시작한다.
Direct pending one-way operation은 Node RID, global Spot·Actor ID 또는 session [binding token](01-glossary.ko.md#binding-token)을 유지한다.
Send-ready 또는 lifecycle signal 뒤의 재시도는 그 identity의 현재 route만 사용한다. 재시도 시점에
해당 route가 없으면 `RouteNotConnected`로 완료하고 다른 논리 target으로 이전하지 않는다.
[Select-one](01-glossary.ko.md#select-one) ChannelName은 성공한 admission 전까지 eligible member를 다시 선택할 수 있지만, 이미
수락된 뒤에는 다른 target으로 replay하지 않는다.

Global object message의 missing·route·exact-incarnation 결과는 다음처럼 구분한다.

| Operation | missing authority | route unavailable | exact ref generation mismatch | pre-commit seal |
|---|---|---|---|---|
| Actor one-way | `ActorRouteNotFound` | `RouteNotConnected` | 해당 없음 | 해당 없음 |
| Actor request | `ActorRouteNotFound` | `RouteNotConnected` | 해당 없음 | 해당 없음 |
| Spot one-way | `SpotRouteNotFound` | `RouteNotConnected` | 해당 없음 | 해당 없음 |
| Spot request | `SpotRouteNotFound` | `RouteNotConnected` | 해당 없음 | 해당 없음 |
| exact ActorRef session bind | `ActorRouteNotFound` | `RouteNotConnected` | `ActorGenerationStale` | `ActorMoving` |
| exact ActorRef destroy | idempotent `false` | `RouteNotConnected` | `ActorGenerationStale` | `ActorMoving` |
| exact SpotRef close | idempotent `false` | `RouteNotConnected` | `SpotGenerationStale` | `SpotMoving` |

Create·GetOrCreate에서 eligible node가 없거나 capacity가 부족하면 `PlacementCapacityExhausted`, reservation을
확보한 owner route가 준비되지 않았으면 `RouteNotConnected`다. Store resolve·reservation·commit과 activation
infrastructure 실패는 `RequestFailed`, object kind·stable type 충돌은 Actor 또는 Spot type-mismatch 오류,
stale authority fence와 application admission 거부는 `RequestRejected`로 완료한다. 다른 owner로 자동
재제출하지 않는다.

이 request 실패는 확인 시점과 관계없이 해당 error kind로 한 번만 완료한다. One-way send는 source의 local
outbound admission 전에 실패를 확인했을 때만 위 kind의 exceptional completion을 반환할 수 있다. Source가
record를 수락해 반환 데이터 없이 완료한 뒤 remote activation이나 admission 실패를 확인한 경우에는 이미
완료된 call을 바꾸지 않는다. 이 실패는 drop metric과 message-flow event로 관측하며 error reply를 만들거나 다른
owner에게 replay하지 않는다.

Request admission 뒤에는 typed reply, typed Framework error, timeout, cancellation, shutdown 또는 protocol
오류 가운데 하나만 terminal 결과가 된다. Generation 충돌은 Spot·Actor stale 결과이고, target busy와
capacity 부족은 admission 오류다. Framework는 이 결과를 이유로 다른 logical owner에 자동 재제출하지
않는다. 호출자 cancellation은 waiter 결과이며 cancellation 뒤 도착한 transport completion은 correlation을
정리하되 두 번째 terminal 결과를 만들지 않는다.

### 13.2 Dispatch 실패 action owner

Dispatch 실패 observer의 reason, action과 caller 결과 대응은
[Message Flow Tracing §4](52-message-flow-tracing.ko.md#4-event-fields)가 단일 owner다.
언어별 exact interface는 그 닫힌 값을 해당 언어의 enum 또는 문자열로 투영하며 값을 추가하거나 줄이지
않는다.

## 14. Startup validation

Framework는 host가 message를 받기 전에 최소한 다음 설정을 검증한다.

- root, MeshName, ChannelName과 stream node 이름의 중복
- MeshNode routing ID와 bind endpoint. Channel handler를 제공하는 MeshNode는 Server membership이 하나
  이상이어야 하지만 호출 또는 Node direct 전용 MeshNode는 membership 0개를 허용한다
- RouteMesh Channel의 Client·Server 역할 중복, Server가 아닌 역할의 weight·handler 설정
- [ClientServer Channel](01-glossary.ko.md#clientserver-channel)의 Client·Server 역할, automatic discovery 사용 시 location store 등록
- process-local ChannelName 송신 경로 중복과 빈 ChannelName 등록
- handler key 중복과 필요한 handler 누락
- channel 종류와 handler 종류의 일치
- Object Client·Server 또는 automatic location 기능을 사용할 때 location store 등록
- manual peer endpoint와 expected RID 형식
- fixed RID는 Object role `None`인 explicit manual topology에서만 사용하며 automatic RID prefix는
  ASCII `[A-Za-z0-9._-]` 1..64자로 제한
- Object role과 manager·factory·placement target의 일치
- Spot, Actor, STREAM session factory와 owner 관계
- User·Instance Spot stable type 중복, actor-free Instance lifecycle, node·type별 active·pending capacity
- 모든 Actor·User Spot·Instance Spot factory의 explicit relocation policy, Snapshot adapter kind와 대상 type의 일치,
  Instance Spot factory가 하나라도 있거나 `Recreate` 또는 `Snapshot` 사용 시 정확히 하나의 Relocation Store
- 분산 owner 또는 relocation을 사용할 때 authority CAS·store clock capability
- placement reservation·aggregate commit capability와 object descriptor limit
- route cache age·forwarding window 조합과 host termination deadline
- application version, maintenance wave와 relocation adapter registration의 유효성
- TLS certificate, key와 trust 설정의 완전성
- bind host, advertised host와 실제 bound port로 만든 endpoint의 유효성

설정 오류는 lazy first call까지 미루지 않고 host startup을 실패시킨다.

## 15. Runtime query와 monitoring

Runtime query는 DI에서 사용할 수 있는 일반 public service다. MeshNode status, peer admission, RouteMesh
Channel membership과 weight, object role·placement weight·active·pending capacity, ClientServer server
readiness·weight·state, bounded location page, lifecycle state와 backlog를 caller-owned snapshot으로 반환한다.

Monitoring event는 source kind, ChannelName, 조건부 MeshName 또는 server identity, [lifecycle generation](01-glossary.ko.md#lifecycle-generation)과
구조화된 오류를 제공한다. Topic, Actor ID와 Spot ID처럼 값의 종류가 매우 많은 식별자는 metric label로
사용하지 않는다.
