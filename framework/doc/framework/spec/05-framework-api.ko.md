# ZLink Framework API

[스펙 목차](README.ko.md) · [이전: 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md) ·
[다음: Channel topology](server/10-channel-topology.ko.md)

## 1. 목적

이 문서는 ZLink Framework 10.0.0의 언어 중립 public API family와 등록 규칙을 정의한다. 실제 타입,
generic 제약, overload와 비동기 반환 타입은 각 package의 언어별 스펙이 소유한다. .NET RouteMesh와
MeshNode의 정확한 인터페이스는
[.NET RouteMesh·MeshNode 인터페이스](server/languages/dotnet/05-route-mesh.ko.md)를 따른다.

## 2. Root 등록

Framework root는 process의 host lifecycle과 DI에 한 번 등록한다. Root configuration은 다음 기능을
제공한다.

| 기능 | 등록 결과 |
|---|---|
| RouteMesh | MeshName으로 MeshNode 하나를 등록한다 |
| classic fanout | MeshNode와 독립된 publisher/subscriber channel을 등록한다 |
| STREAM node | STREAM endpoint와 session handler를 등록한다 |
| location store | application이 만든 store instance를 명시적으로 등록한다 |
| codec extension | typed payload serializer를 등록한다 |
| handler와 filter | dispatch handler, filter와 metadata policy를 등록한다 |

같은 root를 process에 두 번 구성하거나 같은 MeshName을 중복 등록하면 startup에서 설정 오류가 발생한다.

## 3. RouteMesh 등록

RouteMesh 등록은 MeshName 하나를 받고 MeshNode builder를 반환한다. MeshNode builder는 다음 설정을
소유한다.

- routing ID 또는 store 기반 routing ID allocation
- ROUTER bind endpoint와 transport option
- 하나 이상의 immutable ChannelName membership
- manual peer connection intent
- node direct와 channel handler
- Entry Spot, user Spot factory, Actor factory와 transfer adapter
- Logical Multicast publish policy

MeshName은 물리 mesh의 이름이고 ChannelName은 논리 membership이다. 같은 MeshNode에 ChannelName을 여러
개 등록할 수 있다. `ChannelName` 호출은 별도 socket을 만들지 않는다. host가 시작된 뒤 MeshName,
routing ID, endpoint와 membership set은 바꿀 수 없다.

ChannelName builder는 해당 membership의 handler namespace와 weight를 소유한다. weight 범위는 0부터
100까지이며 기본값은 100이다. 실행 중에는 channel weight와 MeshNode의 `MaxMessageSize`를 바꿀 수 있다.
그 밖의 topology와 socket 설정은 startup 뒤 바꿀 수 없다.

## 4. Manual peer

Manual peer API는 두 가지 intent를 제공한다.

- endpoint만 지정하면 admission handshake가 remote RID를 확정한다.
- expected RID와 endpoint를 함께 지정하면 handshake RID가 일치할 때만 admission한다.

Runtime control은 connect intent 추가, endpoint 기준 intent 해제와 현재 intent 목록 조회를 제공한다.
Manual peer도 같은 MeshName, RID, generation, immutable ChannelName set과 security identity를 검증한다.
Framework가 별도 reconnect loop를 만들지 않으며 같은 endpoint의 transport 재접속은 Core socket이
담당한다.

## 5. 메시징 API family

Public messaging은 typed payload를 받고 Framework가 packet name과 codec을 결정한다.

| API family | 필요한 대상 | handler namespace |
|---|---|---|
| Node direct send/request | MeshName context와 target RID | MeshNode route handler |
| Channel send/request | MeshName context와 ChannelName | ChannelName handler |
| Spot direct send/request | resolved Spot handle | target Spot |
| Actor send/request | ActorRef 또는 resolved Actor handle | target Actor context |
| Logical Multicast publish | MeshName context, ChannelName과 topic | local Spot subscription |
| classic fanout publish | fanout channel name | fanout subscriber handler |
| STREAM send/request | session 또는 connector context | session packet handler |

Node direct와 channel operation은 target selection과 submit을 한 호출로 수행한다. 공개 `selectNode`,
`selectOne`, `selectMany` 단계는 제공하지 않는다.

Application 호출은 raw `Message` 대신 업무 객체를 사용한다. Raw message는 bindings의 low-level
transport API와 명시적인 encoded payload 확장에만 둔다. Handler는 typed payload와 읽기 전용 context를
받으며 routing envelope를 직접 조립하지 않는다.

## 6. Call operation

Operation별 call object는 해당 기능에 유효한 설정만 제공한다.

- send는 metadata와 submit 실행 방식을 제공한다.
- request는 metadata, reply timeout, 취소와 typed reply를 제공한다.
- Logical Multicast publish는 ChannelName, topic과 NODROP 정책을 사용한다.
- Spot과 Actor 호출은 resolved address의 generation을 보존한다.
- STREAM 호출은 session identity와 packet correlation을 보존한다.

Send, request와 publish는 [비동기 실행 정책](04-async-execution-policy.ko.md)의 terminator 의미를 따른다.
Request timeout은 reply 대기에만 적용하고 send timeout은 transport admission 대기에 적용한다.

Metadata는 Framework가 검증한 immutable snapshot으로 handler에 전달한다. 같은 key를 여러 번 설정하면
마지막 값이 사용된다. metadata 전체의 UTF-8 encoded 크기는 1024 bytes를 넘을 수 없다. reply는 request
metadata를 자동 복사하지 않는다.

## 7. Logical Multicast option

MeshNode와 Spot publish API는 `NODROP`을 설정할 수 있으며 기본값은 `true`다.

| 값 | admission 계약 |
|---|---|
| `true` | 모든 snapshot remote pipe와 local Spot queue를 reserve한 뒤 한 번에 commit한다 |
| `false` | 막힌 target만 제외하고 admission 가능한 target에 commit할 수 있다 |

Blocking publish는 MeshNode send timeout까지 기다린다. Non-blocking publish는 즉시 backpressure 결과를
반환한다. Publish 결과는 snapshot remote target 수, admitted remote target 수, 제외된 target 수와 local
Spot match 수를 관찰할 수 있어야 한다.

## 8. Handler 등록과 dispatch

Handler key는 owner와 message kind를 포함한다.

| owner | dispatch key |
|---|---|
| Node direct | MeshName, route kind, packet name |
| Channel | MeshName, ChannelName, send/request kind, packet name |
| Spot packet | Spot type, packet kind, packet name |
| Spot subscription | Spot type, ChannelName, topic filter, packet name |
| Actor | Actor type, packet kind, packet name |
| STREAM session | stream node, session type, packet name |

같은 key의 중복 등록은 startup 설정 오류다. 서로 다른 ChannelName이나 owner에는 같은 packet name을
등록할 수 있다. Packet name은 registration descriptor가 한 번 결정하며 codec은 packet name에 관여하지
않는다.

Runtime reflection을 제공하는 언어는 명시한 assembly, module 또는 package 범위에서 handler를 찾을 수
있다. C++는 compile-time type과 명시 builder 등록을 사용한다. 어떤 방식을 사용해도 같은 dispatch key와
중복 검증 규칙을 적용한다.

Core ready callback은 payload를 application callback 인자로 전달하지 않는다. Framework scheduler는
ready owner의 claim을 받아 batch로 drain하고, Node, Spot과 Actor handler를 해당 application 실행 문맥에서
호출한다. Completion, send-ready와 transfer control은 별도 infrastructure 실행 영역에서 진행한다.

## 9. Codec

JSON은 typed message의 기본 codec이다. JSON만 사용하는 application은 메시지 타입마다 codec을 등록하지
않는다. Protobuf, MessagePack과 사용자 codec은 선택 extension package로 root codec registry에 등록한다.

Codec은 업무 객체와 payload bytes 사이의 변환만 담당한다. Packet name, routing, correlation과 handler
선택은 Framework가 소유한다. Application metadata와 payload ownership은
[메시지 계약](03-message-model.ko.md)을 따른다. 내부 multipart 구조는 public Framework API에 노출하지
않는다.

## 10. Location store

자동 discovery, 분산 Spot·Actor address 또는 Actor transfer를 사용하는 host는 location store를
명시적으로 등록한다. 공식 production store는 별도 package로 제공하는 Redis extension이다. Application은
Redis store instance를 만들고 root의 일반 location store 등록 API에 전달한다. 전용 Redis 등록 함수는
제공하지 않는다.

Redis connection과 key prefix는 store instance를 만들 때 설정한다. 자세한 계약은
[Redis location store](server/41-location-store-redis.ko.md)가 소유한다. Process-local in-memory store는
한 process 안의 contract test에서만 사용할 수 있다.

Manual peer만 사용하고 분산 location 기능을 사용하지 않는 host는 store 없이 MeshNode를 구성할 수 있다.

## 11. Classic fanout

Classic fanout은 root에서 독립 channel로 등록한다. Publisher 역할은 bind endpoint를, subscriber 역할은
하나 이상의 manual endpoint를 설정한다. Fanout handler namespace는 packet name으로
구분하며 transport topic filter를 public API로 제공하지 않는다.

Fanout publish 완료는 local publisher transport가 event를 받아들였다는 뜻이다. Subscriber 수신과 handler
완료는 확인하지 않는다. 자세한 전달 계약은
[Channel 메시징](server/11-channel-messaging.ko.md#5-classic-fanout)이 소유한다.

## 12. Spot, Actor와 STREAM owner

Spot factory와 Actor factory는 owner MeshNode에 등록한다. Spot manager는 local Spot 생성과 조회를
제공하고 remote address는 location resolver가 typed handle로 제공한다. Application은 target node RID와
Spot RID를 따로 조립하지 않는다.

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

`RouteNotConnected`는 알려진 target의 pipe가 준비되지 않은 상태이고, `RequestTargetNotFound`는 현재 mesh
member snapshot에 target이 없는 상태다. `ActorLocationStale`은 address generation이 달라진 상태다.

## 14. Startup validation

Framework는 host가 message를 받기 전에 최소한 다음 설정을 검증한다.

- root, MeshName, ChannelName과 stream node 이름의 중복
- MeshNode routing ID, bind endpoint와 하나 이상의 ChannelName
- handler key 중복과 필요한 handler 누락
- channel 종류와 handler 종류의 일치
- location 기능을 사용할 때 location store 등록
- manual peer endpoint와 expected RID 형식
- Spot, Actor, STREAM session factory와 owner 관계
- TLS certificate, key와 trust 설정의 완전성

설정 오류는 lazy first call까지 미루지 않고 host startup을 실패시킨다.

## 15. Runtime query와 monitoring

Runtime query는 DI에서 사용할 수 있는 일반 public service다. MeshNode status, peer admission, ChannelName
membership과 weight, location row, lifecycle state와 backlog를 caller-owned snapshot으로 반환한다.

Monitoring event는 source kind, MeshName 또는 node 이름, RID, lifecycle generation과 구조화된 오류를
제공한다. Topic, Actor ID와 Spot RID처럼 값의 종류가 매우 많은 식별자는 metric label로 사용하지 않는다.
