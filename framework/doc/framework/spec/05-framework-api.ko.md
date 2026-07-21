# ZLink Framework API

[스펙 목차](README.ko.md) · [이전: 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md) ·
[다음: Channel topology](server/10-channel-topology.ko.md)

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
| RouteMesh | MeshName으로 MeshNode 하나를 등록한다 |
| ClientServer Channel | ChannelName으로 client 또는 server 역할 하나를 등록한다 |
| classic fanout | MeshNode와 독립된 publisher/subscriber channel을 등록한다 |
| STREAM node | STREAM endpoint와 session handler를 등록한다 |
| location store | application이 만든 store instance를 명시적으로 등록한다 |
| checkpoint store | Snapshot transfer의 immutable payload를 보관할 store instance를 등록한다 |
| codec extension | typed payload serializer를 등록한다 |
| handler와 filter | dispatch handler, filter와 metadata policy를 등록한다 |
| worker | bounded worker scheduler의 동시성, idle timeout과 queue 상한을 설정한다 |
| network identity | listener가 공통으로 사용할 bind host와 advertised host를 설정한다 |
| deployment identity | target eligibility에 사용할 application version과 maintenance wave를 설정한다 |

같은 root를 process에 두 번 구성하거나 같은 MeshName을 중복 등록하면 startup에서 설정 오류가 발생한다.
같은 ChannelName을 서로 다른 RouteMesh 또는 ClientServer topology에 등록해도 역할과 관계없이 startup에서
실패한다.
Network identity의 공통값과 listener별 override는
[13 Network listener identity](server/13-network-listener-identity.ko.md)가 소유한다.

Root 등록은 process당 Framework runtime singleton 하나를 제공한다. 이 runtime은 host 전체를 대상으로
`Retire`와 `Shutdown`을 수행한다. MeshName, ChannelName이나 node RID별 drain operation은 제공하지 않는다.
State, terminal result, 기본 deadline, 반복 호출과 cancellation 계약은
[54 Host Retire, Shutdown & Handoff](server/54-graceful-drain-handoff.ko.md)가 소유한다.

Framework builder는 service liveness interval과 deadline을 공개하지 않는다. Service runtime은 공통 profile을
내부에서 적용하며 orderly disconnect와 half-open 장애를 구분한다. 고정값, service liveness message와 reconnect
계약은 [55 Transport Liveness](server/55-transport-liveness.ko.md)가 소유한다.

## 3. RouteMesh 등록

RouteMesh 등록은 MeshName 하나를 받고 MeshNode builder를 반환한다. MeshNode builder는 다음 설정을
소유한다.

- routing ID 또는 store 기반 routing ID allocation
- ROUTER bind endpoint와 transport option
- 0개 이상의 immutable ChannelName server membership과 outbound Channel route 선언
- manual peer connection intent
- node direct와 channel handler
- [Actor 모델](server/22-actor-model.ko.md)에 따른 Entry Spot, user Spot factory와 typed Actor factory
- actor-free Instance Spot factory, stable type 이름, type별 active 상한과 activation timeout
- Actor와 Instance Spot type 등록에 연결하는 `Disabled`, `Recreate`, `Snapshot` transfer policy
- Actor transfer deadline과 stale route forwarding window
- Logical Multicast publish policy

MeshName은 물리 mesh의 이름이고 ChannelName은 논리 membership이다. 같은 MeshNode에 ChannelName을 여러
개 등록할 수 있다. `ChannelName` 호출은 별도 socket을 만들지 않는다. host가 시작된 뒤 MeshName,
routing ID, endpoint와 membership set은 바꿀 수 없다.

Actor transfer adapter를 하나라도 등록하면 transfer deadline과 forwarding window를 host start 전에 양수로
설정해야 한다. Deadline은 prepare부터 terminal `Activated` 또는 `Aborted`까지의 상한이다. Forwarding
window는 commit 뒤 old ActorRef로 도착한 straggler를 target으로 전달하는 기간이다. Deadline이 끝나면 commit
전 transfer는 abort하고, commit 뒤 transfer는 target activation recovery를 계속한다. Forwarding window가
끝난 stale route는 `ActorLocationStale`로 실패하며 Framework가 자동으로 다시 보내지 않는다.

RouteMesh Channel builder는 `Client`와 `Server` 역할을 구분한다. `Client`는 ChannelName을 해당 MeshNode의
outbound 송신 경로로 등록하지만 peer에게 target membership으로 광고하지 않으며 weight와 handler를 갖지
않는다. `Server`는 target membership과 handler namespace를 등록하며 weight와 handler 설정을 제공한다.
Server도 같은 ChannelName으로 outbound 호출을 시작할 수 있으므로 같은 이름의 Client 역할을 중복 등록하지
않는다.

Server weight 범위는 0부터 100까지이며 기본값은 100이다. 실행 중에는 server weight만 바꿀 수 있다.
`SetWeight(0)`은 server를 새 선택에서 제외하는 drain 설정이며 Client 역할을 표현하지 않는다.
`MaxMessageSize`를 포함한 topology와 socket 설정은 startup 뒤 바꿀 수 없다.

Framework의 `MaxMessageSize = 0`은 Framework가 transport 기본값보다 작은 별도 상한을 두지 않는다는
뜻이다. 양수는 같은 byte 상한으로 적용하고 음수 값은 설정 오류다. Binding option 표현과 변환은
언어별 internals가 소유하며 application public API에 노출하지 않는다.

MeshNode builder에는 drain policy나 lifecycle command를 추가하지 않는다. Host의 continuity maintenance는
Framework runtime의 `Retire`, 일반 종료는 `Shutdown`이 수행한다. `Draining`은 두 operation 진행 중에
관측되는 state이며 Channel weight 0을 lifecycle state 대신 사용하지 않는다.

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

| API family | 필요한 대상 | handler namespace |
|---|---|---|
| Node direct send/request | MeshName context와 target RID | MeshNode route handler |
| Channel send/request | ChannelName | ChannelName handler |
| Spot direct send/request | resolved Spot handle | target Spot |
| Instance Spot direct send/request | MeshName·Instance type·Spot RID address | resolve·activation 뒤 target Spot |
| Actor send/request | ActorRef 또는 resolved Actor handle | target Actor context |
| Logical Multicast publish | ChannelName과 topic | local Spot subscription |
| classic fanout publish | fanout channel name | fanout subscriber handler |
| STREAM send/request | session 또는 connector context | session packet handler |

Node direct와 channel operation은 target selection과 submit을 한 호출로 수행한다. 공개 `selectNode`,
`selectOne`, `selectMany` 단계는 제공하지 않는다.

Channel client는 ChannelName을 process-local route index에서 찾아 RouteMesh MeshNode 또는 ClientServer
client 하나를 선택한다. Index에 없는 이름은 `RequestTargetNotFound`로 끝내고 다른 MeshNode나
ClientServer client를 검색하거나 relay하지 않는다. 등록된 송신 경로에 ready target pipe가 없으면
`RouteNotConnected`, ready target snapshot 자체가 없으면 `RequestTargetNotFound`를 사용한다.

Logical Multicast도 ChannelName을 같은 process-local route index에서 찾아 owner RouteMesh MeshNode를
선택한다. 호출자는 MeshName이나 endpoint를 제공하지 않는다. 선택된 owner MeshName과 물리
route는 runtime monitoring과 message-flow 관측에 남지만 application 호출 인자로 되돌리지 않는다.

Application 호출은 raw `Message` 대신 업무 객체를 사용한다. Raw message는 bindings의 low-level
transport API와 명시적인 encoded payload 확장에만 둔다. Handler는 typed payload와 읽기 전용 context를
받으며 routing envelope를 직접 조립하지 않는다.

## 6. Call operation

Operation별 call object는 해당 기능에 유효한 설정만 제공한다.

- one-way send와 session Actor relay는 metadata 가능 여부와 관계없이 비동기 submit 결과를 제공한다.
- request는 metadata, reply timeout, 취소와 typed reply를 제공한다.
- Logical Multicast publish는 metadata, ChannelName, topic과 비동기 submit 하나를 사용한다.
- Spot과 Actor 호출은 resolved address의 generation을 보존한다.
- Instance Spot 호출은 logical address를 보존하고 owner·generation resolve와 activation을 Framework 내부에서
  수행한다. Cache 상태와 관계없이 one-way는 비동기 submit만 제공한다.
- STREAM 호출은 session identity와 packet correlation을 보존한다.

Server package의 one-way send·publish·명시적 STREAM reply는
[비동기 실행 정책](04-async-execution-policy.ko.md)의 async-only admission 계약을 따른다. Public call은
즉시 한 번만 시도하는 동기 terminator를 함께 제공하지 않는다. 별도 stream connector package의 send
builder는 connector package 계약을 따른다. Request timeout은 reply 대기에만 적용하고 send timeout은
transport admission 대기에 적용한다.
최초 non-blocking transport submit이 즉시 수락되면 Framework scheduler나 별도 work queue에 추가하지
않고 이미 완료되었거나 resolved된 언어별 awaitable을 반환한다.

Metadata는 Framework가 검증한 immutable snapshot으로 handler에 전달한다. 같은 key를 여러 번 설정하면
마지막 값이 사용된다. metadata 전체의 UTF-8 encoded 크기는 1024 bytes를 넘을 수 없다. reply는 request
metadata를 자동 복사하지 않는다.

## 7. Logical Multicast 결과

MeshNode와 Spot publish API는 publish 전용 전달 정책 option을 제공하지 않는다. Framework의 bounded I/O
executor는 대기 queue 없이 worker slot을 direct handoff한다. 즉시 사용할 slot이 없으면 transport 제출을
시작하지 않고 `Backpressured`로 완료한다. Handoff에 성공하면 runtime은 확정한 target snapshot을 정확히
한 번 처리한다. 각 remote target에는 ROUTER send timeout을 적용하며 local Spot queue는 독립적으로
수락하거나 drop한다. Snapshot 처리가 시작된 뒤에는 cancellation이나 shutdown으로 나머지 target 제출을
중단하지 않는다.

Publish 결과는 remote와 local 각각의 snapshot, admitted, dropped 수와 remote unreachable 수를 제공한다.
Remote capacity
실패는 top-level `Backpressured`, snapshot target이 모두 0이면 `TargetNotFound`로 완료한다. 그 밖의 remote
연결 불가와 local Spot queue drop은 top-level status를 바꾸지 않고 detail에 기록한다. 앞에서 수락한 target은
뒤 target의 실패 때문에 취소하지 않는다. Remote target이 모두 연결 불가여서 admitted count가 0이어도
remote capacity drop이 없으면 `Submitted`다.

## 8. Handler 등록과 dispatch

Handler key는 owner와 message kind를 포함한다.

| owner | dispatch key |
|---|---|
| Node direct | MeshName, route kind, packet name |
| Channel | ChannelName, send/request kind, packet name |
| Spot packet | Spot type, packet kind, packet name |
| Spot subscription | Spot type, ChannelName, topic filter, packet name |
| Actor | Actor type, packet kind, packet name |
| STREAM session | stream node, session type, packet name |

같은 key의 중복 등록은 startup 설정 오류다. 서로 다른 ChannelName이나 owner에는 같은 packet name을
등록할 수 있다. Packet name은 registration descriptor가 한 번 결정하며 codec은 packet name에 관여하지
않는다.

모든 handler가 공유하는 base context는 MeshName을 요구하지 않는다. Channel handler와 filter context는
ChannelName, message kind, packet name, metadata와 correlation 정보를 제공한다. Node direct handler
context는 물리 RID namespace가 실제 대상 계약이므로 MeshName과 source·target RID를 별도 context에
유지한다. 선택된 RouteMesh 또는 ClientServer 종류와 endpoint는 application handler가 아니라 monitoring과
message-flow 관측에서 제공한다.

Runtime reflection을 제공하는 언어는 명시한 assembly, module 또는 package 범위에서 handler를 찾을 수
있다. C++는 compile-time type과 명시 builder 등록을 사용한다. 어떤 방식을 사용해도 같은 dispatch key와
중복 검증 규칙을 적용한다.

### 8.1 Handler filter

Handler filter는 ChannelName의 send/request dispatch에만 적용한다. Node direct, Spot, Actor, STREAM session,
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
send-ready와 transfer control은 application handler가 점유할 수 없는 infrastructure 실행 영역에서 진행한다.

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

언어별 server root와 Stream Connector의 codec 등록 표면은 다음 exact interface가 소유한다.

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

## 10. Location store

자동 discovery에 참여하는 classic fanout publisher, endpoint 없는 fanout subscriber, 분산 Spot·Actor address,
InstanceSpotAddress 또는 Actor·Instance Spot transfer를 사용하는 host는 location store를
명시적으로 등록한다. 공식 production store는 별도 package로 제공하는 Redis extension이다. Application은
Redis store instance를 만들고 root의 일반 location store 등록 API에 전달한다. 전용 Redis 등록 함수는
제공하지 않는다.

Redis connection과 key prefix는 store instance를 만들 때 설정한다. 자세한 계약은
[Redis location store](server/41-location-store-redis.ko.md)가 소유한다. Process-local in-memory store는
한 process 안의 contract test에서만 사용할 수 있다.

Manual peer만 사용하고 분산 location 기능을 사용하지 않는 host는 store 없이 MeshNode를 구성할 수 있다.
`Snapshot` policy를 하나라도 등록한 host는 opaque checkpoint store도 등록해야 한다. Location provider가
owner·transfer authority compare-exchange와 store clock capability를 제공하지 않거나 Snapshot host에
checkpoint store가 없으면 startup이 실패한다. Store interface와 등록 조건은
[40 Location runtime](server/40-location-runtime.ko.md)이 소유한다.

## 11. Classic fanout

Classic fanout은 root에서 독립 channel로 등록한다. Location store를 등록한 Publisher 역할은 Publisher RID를
고정하거나 RID allocation으로 얻고, 실제 bind가 끝난 listener endpoint를 fanout 전용 descriptor로
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
[Transport liveness](server/55-transport-liveness.ko.md)가 소유한다.

Manual subscriber builder가 등록한 endpoint 집합은 공통 endpoint 연결 handle로도 제공한다. Application은
이 handle로 runtime 중 endpoint를 연결하거나 해제하고 현재 manual 연결 목록을 조회할 수 있다. 이 handle은
automatic discovery 결과를 수정하는 표면이 아니며 같은 channel을 automatic mode로 전환하지 않는다.

Endpoint 없이 등록한 automatic subscriber의 current connection intent와 ready 상태는
[Runtime monitoring](server/50-runtime-monitoring.ko.md)의 fanout runtime snapshot과 event로만 관찰한다.
Publisher changed event는 publisher entry를, location changed event는 Location snapshot을 필수로 가지며 두
payload를 nullable field로 섞지 않는다. 이 표면은 읽기 전용이며 endpoint 연결·해제 operation을 제공하지
않는다. Manual endpoint 연결 handle은 automatic snapshot이나 event의 entry를 변경할 수 없다.

Fanout publish 완료는 local publisher transport가 event를 받아들였다는 뜻이다. Subscriber 수신과 handler
완료는 확인하지 않는다. 자세한 전달 계약은
[Channel 메시징](server/11-channel-messaging.ko.md#5-classic-fanout과의-경계)이 소유한다.

Classic fanout publish의 공통 입력은 ChannelName, topic과 typed event다. 정확한 언어별 interface는
topic을 명시하는 호출과 topic을 생략하는 typed 편의 호출을 함께 제공한다. 편의 호출은
Framework가 결정한 packet name을 topic으로 사용하며, 명시적 topic 호출을 제거하거나 의미를
바꾸지 않는다. Framework는 typed message 등록에서 packet name과 codec을 결정한다. 발행 호출은 publisher socket의
유한한 send timeout까지 admission을 기다리는 비동기 terminator 하나만 제공한다. 결과는 공통 one-way
submit status이며 remote·local target별 count를 집계하는 Logical Multicast publish result가 아니다.
Subscriber가 0이어도 local publisher queue가 event를 수락하면 `Submitted`다. 결과에는 subscriber 수,
수신 또는 handler 완료 정보를 포함하지 않는다.

## 12. Spot, Actor와 STREAM owner

Spot factory와 typed Actor factory는 owner MeshNode에 등록한다. Spot manager는 local Spot 생성과 조회를
제공하고 remote address는 location resolver가 typed handle로 제공한다. Application은 target node RID와
Spot RID를 따로 조립하지 않는다.

Instance Spot factory도 MeshNode에 등록하지만 Domain Spot factory와 다른 actor-free marker를 사용한다.
같은 stable type 또는 같은 implementation class를 두 factory 종류에 중복 등록할 수 없다. Instance factory의
Instance factory option을 생략하면 type과 local MeshNode마다 `MaxActiveInstances=4096`,
`ActivationTimeout=3초`를 사용한다. Option을 명시하면 두 값은 모두 0보다 커야 하며 0을
기본값 sentinel이나 무제한으로 해석하지 않는다. Actor handler, Actor membership과
Logical Multicast subscription은 Instance registration 또는 activation에서 거부한다.

InstanceSpotAddress는 `MeshName`, `InstanceSpotType`, `SpotRid`만 가진다. Global Spot client와 Spot outbound
context는 Spot direct call과 같은 이름의 address overload를 제공하며 별도 placement client를 만들지 않는다.
Caller는 `createIfMissing`, target node, owner token, generation이나 retry option을 전달하지 않는다. 첫 direct
send/request가 owner claim을 시작할 수 있고 Ready barrier 뒤 Spot packet handler를 사용한다.
SpotHandle과 manager의 Create·GetOrCreate·Resolve는 existing-only·local-only 의미를 유지한다.

Actor factory와 Instance Spot factory는 생성할 구체 type과 transfer policy를 같은 type registration에서
고정한다. `Disabled`는 남은 instance가 있는 `Retire`를 차단하고, `Recreate`는 application state payload 없이
같은 logical ID의 typed factory를 실행하며, `Snapshot`은 typed state adapter로 업무 상태를 capture·restore한다.
Transfer ID, target RID, checkpoint reference, journal cursor와 authority revision은 application callback에
노출하지 않는다. Entry Spot과 User Spot factory에는 transfer policy parameter를 제공하지 않는다.

Actor factory는 Actor lifecycle을 만들고 Actor handler는 Actor context의 handler registry에 등록한다.
Actor message는 Actor mailbox로 직접 dispatch한다. Actor message를 Node callback이나 Spot packet handler가
다시 분류하지 않는다.

STREAM node는 MeshNode와 독립적으로 등록할 수 있다. Session과 Actor binding을 사용하면 STREAM session
service가 raw STREAM과 MeshNode의 관계를 소유한다. Session ingress는 bound Actor mailbox로 전달되고,
Actor egress는 bound session FIFO를 사용한다.

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

`RouteNotConnected`는 알려진 target의 pipe가 준비되지 않은 상태이고, `RequestTargetNotFound`는 등록한
송신 경로에 현재 선택 가능한 target snapshot이 없거나 ChannelName 송신 경로 자체가 없는 상태다.
`ActorLocationStale`은 address generation이 달라진 상태다.

### 13.1 Operation 결과 변환

Framework는 target selection과 transport admission 결과를 다음 공통 결과로 변환한다. Direct call은 Node
RID, Spot·Actor handle의 owner와 generation, session binding token으로 표현된 논리 identity를 유지하며
다른 target으로 전환하지 않는다. 물리 peer lifecycle generation은 public commitment가 아니다.
RouteMesh·ClientServer select-one ChannelName은 성공한 admission 전까지 현재 eligible member를 다시 선택할
수 있지만 수락 또는 terminal completion 뒤에는 같은 operation을 다시 제출하지 않는다.

| 관찰한 조건 | Framework 결과 |
|---|---|
| 해당 operation family의 source outbound admission이 operation을 수락함 | one-way send·publish는 `Submitted`, request는 pending completion으로 전환 |
| 일반 one-way의 첫 submit이 backpressured임 | send-ready를 기다린다. Pending admission 공간도 가득 차면 `Backpressured`, send timeout까지 수락되지 않으면 `TimedOut` |
| Logical Multicast snapshot 처리 중 remote capacity가 부족함 | `Backpressured`와 partial detail을 반환 |
| 알려진 direct target의 route가 준비되지 않음 | `RouteNotConnected` |
| 송신 경로나 eligible target snapshot이 없음 | one-way는 `TargetNotFound`, request는 `RequestTargetNotFound` |
| target admission seal 또는 application policy가 거부함 | `RequestRejected` 또는 해당 one-way rejection 결과 |
| host shutdown으로 신규 admission이 닫힘 | one-way는 `Shutdown`, request는 shutdown 오류 |
| invalid argument·state, 지원하지 않는 operation 또는 내부 불변 조건 위반 | 언어별 local call 오류. remote error reply로 바꾸지 않음 |

`TimedOut`은 일반 one-way admission waiter가 family별 send timeout까지 수락되지 않았을 때 Framework가 만드는
결과다. Cancellation은 submit status가 아니며 해당 언어의 cancelled awaitable로 표현한다. Invalid
argument·handle·state, 이미 사용한 reply token과 중복 terminator 실행은 exceptional completion이다.
STREAM reply의 유효한 첫 terminator는 transport 시도 전에 one-shot token을 원자적으로 소비한다.
Backpressure, timeout 또는 cancellation으로 완료되어도 해당 token을 다시 사용할 수 없다. 같은
token의 두 call이 경쟁하면 하나만 transport admission을 시작한다.
Direct pending one-way operation은 Node RID, Spot·Actor owner와 handle generation, session binding token을 유지한다.
Send-ready 또는 lifecycle signal 뒤의 재시도는 그 identity의 현재 route만 사용한다. 재시도 시점에
해당 route가 없으면 `RouteNotConnected`로 완료하고 다른 논리 target으로 이전하지 않는다.
Select-one ChannelName은 성공한 admission 전까지 eligible member를 다시 선택할 수 있지만, 이미
수락된 뒤에는 다른 target으로 replay하지 않는다.

InstanceSpotAddress는 node RID와 generation 대신 logical address를 유지한다. Eligible node가 없으면
`RequestTargetNotFound`, address owner는 확인했지만 route가 없으면 `RouteNotConnected`다. Store 실패는
infrastructure 오류로 처리하고, kind·type 충돌과 owner authority 거부를 서로 다른 error kind로
구분한다. CAS loser의 pre-admission redirect 한 번 외에는 다른 owner로 자동 재제출하지 않는다.

Instance Spot request와 source local admission 전에 끝난 one-way exceptional completion은 새 error kind를
추가하지 않고 다음 공통 `ZLinkFrameworkErrorKind` 값으로 변환한다. 숫자 값도 모든 언어에서 같아야 한다.

| Instance 실패 조건 | public error kind | 값 |
|---|---|---:|
| Location Store resolve·claim·commit 실패 또는 activation infrastructure 실패 | `RequestFailed` | 16 |
| Domain Spot과의 kind 충돌 또는 다른 Instance Spot type과의 충돌 | `SpotTypeMismatch` | 6 |
| stale owner token·epoch 또는 `Closing` 상태에서 application admission 전 거부 | `RequestRejected` | 14 |

표에 든 request 실패는 확인 시점과 관계없이 위 error kind로 한 번만 완료한다. One-way send는 source의 local
outbound admission 전에 실패를 확인했을 때만 위 kind의 exceptional completion을 반환할 수 있다. Source가
record를 수락해 `Submitted`로 완료한 뒤 remote activation이나 admission 실패를 확인한 경우에는 이미 반환한
결과를 바꾸지 않는다. 이 실패는 drop metric과 message-flow event로 관측하며 error reply를 만들거나 다른
owner에게 replay하지 않는다.

Request admission 뒤에는 typed reply, typed Framework error, timeout, cancellation, shutdown 또는 protocol
오류 가운데 하나만 terminal 결과가 된다. Generation 충돌은 Spot·Actor stale 결과이고, target busy와
capacity 부족은 admission 오류다. Framework는 이 결과를 이유로 다른 logical owner에 자동 재제출하지
않는다. 호출자 cancellation은 waiter 결과이며 cancellation 뒤 도착한 transport completion은 correlation을
정리하되 두 번째 terminal 결과를 만들지 않는다.

### 13.2 Dispatch 실패 action owner

Dispatch 실패 observer의 reason, action과 caller 결과 대응은
[Message Flow Tracing §4](server/52-message-flow-tracing.ko.md#4-event-fields)가 단일 owner다.
언어별 exact interface는 그 닫힌 값을 해당 언어의 enum 또는 문자열로 투영하며 값을 추가하거나 줄이지
않는다.

## 14. Startup validation

Framework는 host가 message를 받기 전에 최소한 다음 설정을 검증한다.

- root, MeshName, ChannelName과 stream node 이름의 중복
- MeshNode routing ID와 bind endpoint. Channel handler를 제공하는 MeshNode는 Server membership이 하나
  이상이어야 하지만 호출 또는 Node direct 전용 MeshNode는 membership 0개를 허용한다
- RouteMesh Channel의 Client·Server 역할 중복, Server가 아닌 역할의 weight·handler 설정
- ClientServer Channel의 Client·Server 역할, automatic discovery 사용 시 location store 등록
- process-local ChannelName 송신 경로 중복과 빈 ChannelName 등록
- handler key 중복과 필요한 handler 누락
- channel 종류와 handler 종류의 일치
- location 기능을 사용할 때 location store 등록
- manual peer endpoint와 expected RID 형식
- Spot, Actor, STREAM session factory와 owner 관계
- Instance factory의 stable type·class 중복, actor-free lifecycle, type별 active 상한·activation timeout,
  Redis location store 등록과 MeshName별 source Entry Spot 단일성
- Actor·Instance Spot typed factory와 transfer policy type 일치, Snapshot state contract ID와 checkpoint store
- 분산 owner 또는 transfer를 사용할 때 authority CAS·store clock capability
- 양수인 Actor transfer deadline과 forwarding window, host termination deadline
- application version, maintenance wave와 state reader capability의 유효성
- TLS certificate, key와 trust 설정의 완전성
- bind host, advertised host와 실제 bound port로 만든 endpoint의 유효성

설정 오류는 lazy first call까지 미루지 않고 host startup을 실패시킨다.

## 15. Runtime query와 monitoring

Runtime query는 DI에서 사용할 수 있는 일반 public service다. MeshNode status, peer admission, RouteMesh
Channel membership과 weight, ClientServer server readiness·weight·state, location row, lifecycle state와
backlog를 caller-owned snapshot으로 반환한다.

Monitoring event는 source kind, ChannelName, 조건부 MeshName 또는 server identity, lifecycle generation과
구조화된 오류를 제공한다. Topic, Actor ID와 Spot RID처럼 값의 종류가 매우 많은 식별자는 metric label로
사용하지 않는다.
