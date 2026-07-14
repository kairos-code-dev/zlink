<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: STREAM 서버 세션](30-stream-session.ko.md) | [다음: Stream Connector](32-stream-connector.ko.md)
<!-- framework-adapter-nav:end -->


[문서 묶음](../README.ko.md) | [Actor 모델](22-actor-model.ko.md) | [framework API](05-framework-api.ko.md) | [.NET Session Actor Dispatch](languages/dotnet/02-handler-interfaces.ko.md)

# Session Actor Dispatch

## 1. 목적

이 문서는 session server와 actor 실행 server를 분리할 때 사용하는 public contract를
정의한다. application은 session에서 actor handle을 bind하고 typed payload를 전달하며,
actor에서는 현재 연결된 client session으로 메시지를 보낼 수 있어야 한다.

transport routing id, raw relay envelope, stream header와 위치 저장소 조회 절차는
framework 내부에 둔다. 사용자는 actor id, typed payload, actor handle과 현재 actor의
bound session만 사용한다. 언어별 정확한 타입과 시그니처는 언어별 spec에서 고정한다.

이 문서에서 역따옴표로 표시한 구체 `.NET` 타입과 메서드 이름은 역할을 연결하기 위한
비규범 예시다. 공통 계약은 그 이름이나 `Async` 접미사를 요구하지 않으며, 각 언어의
정식 이름과 반환형은 해당 언어의 interface 문서가 고정한다.

## 2. 공개 경계 요구

### 2.1 application에 노출하는 개념

session actor dispatch sample의 핵심 흐름은 아래 네 가지다.

1. client가 session server에 연결하고 인증한다.
2. session server가 domain 정책에 따라 local `SpotNode` actor runtime의 actor를 만들거나
   actor handle을 준비한다.
3. client request가 actor handle을 통해 actor runtime으로 dispatch된다.
4. actor는 자기 context의 bound session을 통해 client에게 one-way message를 보내거나
   연결을 종료한다.

다음 정보는 transport와 runtime 내부에서 관리하며 application public contract에
노출하지 않는다.

| 내부 정보 | 공개 경계 요구 |
|----------|----------------|
| router channel id와 node `RoutingId` | 위치 해석과 전송 계층 안에서 관리한다. |
| raw packet name과 relay envelope | typed handler registry와 dispatch 계층 안에서 관리한다. |
| stream header와 request sequence | session dispatch helper가 보존한다. |
| 위치 저장소 조회와 자동 연결 상태 | location runtime이 관리하고 진단 표면으로 관찰한다. |

application은 actor id, typed message, actor handle과 현재 actor의 bound session만 사용한다.

### 2.2 transport 위치 정보 은닉

`RoutingId`는 zlink routed transport의 목적지다. 반면 game code가 자연스럽게
다루는 값은 `gameId`, `matchId`, `actorId` 같은 domain key다.

domain service는 game이나 actor 같은 논리 대상을 고르고, framework location resolver와
runtime이 전송 node를 결정한다. transport 위치 결정은 framework location runtime과
내부 전송 계층 밖으로 노출하지 않는다.

### 2.3 typed actor dispatch

actor dispatch는 message type과 등록된 handler로 선택한다. application handler는
framework relay envelope를 해석하거나 packet name 문자열로 분기하지 않는다.

### 2.4 session relay 책임

session server 쪽 relay code는 인증, actor 생성, api lookup, play relay,
client reply 보존을 함께 다룬다. 특히 request를 relay할 때는 원본 request
sequence가 유지되어야 하므로 framework Header packet dispatch 경로를 알아야 한다.

actor를 언제 만들지, 어떤 actor type을 쓸지, 현재 session에 어떤 actor handle을
binding할지는 application 정책이다. 반면 request sequence 보존, relay envelope 조립,
reply matching은 framework helper가 맡는다. application은 actor
생성 위치와 dispatch 결정은 직접 내리되, transport 세부 작업을 반복해서 작성하지
않아야 한다.

### 2.5 자동 연결 선언

session-gateway 형태의 sample(`Bingo`)은 자동 연결을 보여 주기 위해 공유 location store를
등록한다. 이 결정은 맞다 — 실제 서비스가 기대하는 topology가 자동 연결이기 때문이다. 수동
연결과 수동 등록의 대조를 보여 주는 것은 `TicTacToe` 하나뿐이다
([샘플 규약](../sample/README.ko.md)).

하지만 application code가 store 연결 정보, 연결 시점, routed channel의
자동 연결 사용 여부를 너무 넓게 알아야 하면 sample의 초점이 흐려진다. store 등록은
"이 host가 같은 mesh에 참여한다"는 선언에 가까워야 한다
([location runtime](40-location-runtime.ko.md) §8).

## 3. 계약 요구

상위 표면은 아래 요구를 만족해야 한다.

- domain code는 `actorId`, `gameId`, request/reply type을 중심으로 작성한다.
- `RoutingId`는 위치 해석 모듈과 framework 내부에 가둔다.
- session dispatch는 application이 명시적으로 작성하되, raw relay envelope와 request
  sequence 보존은 framework helper가 맡는다.
- typed handler 등록을 제공해서 `packetName` switch를 줄인다.
- codec은 framework의 codec registry를 사용한다.
- store 기반 자동 연결은 수동 연결과 섞이지 않으며, 자동 연결 실패를 retry로 숨기지 않는다.
- 일반 send/request public API와 session actor helper는 `RoutingId`, `SpotNodeId` 같은
  transport 위치값을 직접 요구하지 않는다.
- actor와 현재 stream session의 묶음은 사용자가 생성 후 bind(`GetOrCreateAsync` + `BindAsync`) 또는
  `session.Actors.BindAsync(...)`를 호출할 때 framework/core 내부 binding으로 갱신한다.
- direct target send/request와 두 방향을 합친 gateway 객체는 public contract에 포함하지 않는다.

public helper는 사용자가 알아야 하는 결정의 수를 줄여야 한다.

## 4. 계약 범위 밖

이 문서는 아래 기능을 정의하지 않는다.

- actor migration 정책
- game room placement 알고리즘
- location store 제품 자체의 HA/배포 형태
- client protocol 자체의 정식 packet set
- retry policy 또는 connection warmup helper
- in-memory fake transport

session actor dispatch는 전달 경로를 제공한다. actor가 어느 play node에 있어야 하는지,
actor가 이미 존재할 때 create 요청을 어떻게 처리할지, 재연결 시 어느 정책으로 위치를
갱신할지는 application 또는 별도 상위 runtime의 책임으로 남긴다.

## 5. 정보 은닉 요구

| 정보 | 소유 모듈 |
|------|-----------|
| relay envelope와 request sequence | session dispatch helper |
| node `RoutingId`와 route resolve | location resolver와 내부 transport |
| payload decode와 handler 선택 | 실행 문맥별 typed handler registry |
| actor-session binding token | framework/core runtime |
| 인증, actor id/type 선택과 재사용 정책 | application |

## 6. 책임 경계

일반 application은
actor/node/spot 실행 문맥에 domain message handler를 등록하고, session callback에서는
인증, local actor handle 생성, dispatch 여부를 명시적으로 고른다. framework는 route resolve,
envelope 조립, request sequence 보존처럼 반복되면 위험한 작업만 맡는다.

request sequence 보존과 codec 처리는 framework가 흡수한다. actor 생성 위치, actor type,
이미 존재하는 actor를 재사용할지 실패할지는 domain 정책이므로 framework가 자동으로
정하지 않는다. sample 전용 relay helper는 이 책임을 대신하는 공개 계약이 될 수 없다.

## 7. public surface 개요

상위 표면은 세 축으로 나눈다.

| 축 | 역할 |
|----|------|
| actor/node/spot handlers | relayed message를 실행 문맥 안에서 typed message로 처리한다. |
| actor/spot location resolvers | actor id와 spot rid를 `SpotHandle`로 해석한다. |
| session actor helpers | session server가 선택한 actor create/dispatch를 request sequence 손실 없이 수행한다. |

아래 그림은 책임 경계를 보여 준다.

```text
+------------------------------------------------------------+
| Client                                                     |
| typed packet, stream seq                                   |
+------------------------------------------------------------+
| Session Dispatch                                           |
| auth branch, actor choice                                  |
+------------------------------------------------------------+
| Play Handler                                               |
| typed payload, domain reply                                   |
+------------------------------------------------------------+
```

다이어그램 안의 `Session Dispatch`는 application이 작성하는 session callback이다.
framework helper는 이 callback 안에서 선택된 dispatch의 transport 세부 작업만 숨긴다.

## 8. Actor/Node/Spot Handler Dispatch

### 8.1 내부 actor relay 역할

session에서 actor로 넘어온 relay envelope를 직접 처리하는 raw handler는 framework
내부 역할이며 public contract에 포함하지 않는다. 이 내부 역할은 actor가 client
session으로 보내는 bound session과 별개다. application에는 다음 정보만 제공하는
typed handler를 노출한다.

- actor id를 알고 싶다.
- typed request payload를 받고 싶다.
- typed reply를 반환하고 싶다.

### 8.2 typed handler 계약

typed handler는 bound session이나 routed channel에 붙지 않는다. handler는 actor, node,
spot 같은 실행 문맥에 등록된다. session은 일부 packet을 local 처리하고, 나머지를
actor/node/spot 실행 문맥으로 relay한다. typed handler가 받는 값은 아래로 제한한다.

- typed request payload 한 개
- actor handler 변종이면 actor 인스턴스 한 개
- handler context (metadata snapshot과 필요하면 deadline). actor handler는 actor
  인스턴스의 context에서 현재 Spot과 bound session, join 호출 표면에 접근한다.
- 해당 언어별 handler 계약이 정의한 실행 중단 또는 연결 종료 정보

typed actor context는 source session의 `RoutingId`를 노출하지 않는다. handler가
client에게 push를 보내야 할 때는 현재 actor context의 bound session으로 one-way
`Send(...)`를 호출한다. framework는 core actor-session binding으로 현재 client를 찾기
때문에 application이 session node나 actor id를 다시 지정하지 않는다.

구체 .NET 시그니처는
[bindings/dotnet/handler-interfaces.ko.md](languages/dotnet/02-handler-interfaces.ko.md)
§3을 참고한다.

### 8.2.1 Bound session naming

actor/play 코드에서 client session으로 메시지를 보낼 때 사용하는 public 객체는
현재 actor에 연결된 **bound session**이다. 이 객체는 one-way `Send(...)`와 연결 종료만
제공한다. 임의 actor id를 받는 전역 proxy, client를 향한 request/reply, 별도 binding
store는 public contract에 포함하지 않는다.

### 8.3 Header Metadata 전달

session actor dispatch runtime은 stream header를 그대로 play handler에 넘기지 않는다. raw header에는
session server 내부에서만 의미가 있는 값과 actor handler까지 전달해도 되는 값이
섞여 있기 때문이다.

metadata는 아래처럼 나누어 처리한다.

| 종류 | 예 | 전달 범위 |
|------|----|-----------|
| session-local metadata | stream session id, peer endpoint, raw request sequence | session actor dispatch runtime 내부에서만 사용 |
| framework routing metadata | actor id, router channel id | actor handler context로 전달 |
| request lifecycle metadata | deadline, request kind, 언어별 계약이 제공하는 취소 상태 | actor handler context로 전달 |
| application metadata | trace id, correlation id, tenant id, locale | 명시 허용 목록 또는 typed metadata로 전달 |
| codec metadata | codec id, content type, schema version | codec registry와 handler context에서 사용 |

`ZLinkMessageMetadata`는 application metadata와 codec metadata를 담는 immutable snapshot
이어야 한다. session actor dispatch runtime은 stream packet을 actor relay envelope로 바꿀 때 이
snapshot을 함께 보존한다. 기본 정책은 deny다. application metadata는 명시적으로
허용한 key 또는 typed metadata contract에 속한 값만 actor handler까지 전달한다.

반대로 아래 값은 typed actor handler에 노출하지 않는다.

- framework Header 전체
- stream session id
- peer endpoint
- source session node `RoutingId`
- 원본 request sequence
- native socket pointer나 transport handle

원본 request sequence는 session actor dispatch runtime이 client reply를 맞추는 데 필요하지만,
handler가 직접 읽거나 수정하면 request/reply matching 규칙이 깨질 수 있다. 따라서
framework internal envelope에만 보존하고, public context에는 올리지 않는다.

cross-binding 계약은 아래 두 표면으로 정리한다.

- `ZLinkMessageMetadata`(또는 binding별 동등 이름) -- immutable snapshot. application
  과 codec metadata를 분리해서 보관한다.
- metadata forwarding policy 표면 -- application metadata key별 forwarding 허용 여부를
  결정하는 contract.

stream session에서 actor dispatch로 넘어갈 때 framework는 아래 규칙을 적용한다.

1. codec metadata는 codec registry가 이해하는 key만 `Codec` snapshot에 넣는다.
2. application metadata는 metadata forwarding policy가 허용한 key만 `Application`
   snapshot에 넣는다.
3. framework routing metadata와 session-local metadata는 metadata snapshot에 넣지
   않고 internal envelope field로만 보관한다.
4. actor handler가 `context.Metadata`를 변경할 수 없도록 snapshot은 immutable이어야 한다.

기본 metadata policy는 application metadata를 전달하지 않는다. sample에서
trace id 같은 값을 보여 주려면 framework 등록 단계에서 명시 허용 목록을 둔다.
이 설정은 handler route를 고르는 데 쓰지 않는다. message dispatch key는 packet name과
message kind이고, location resolver 입력은 `actorId` 또는 `spotRid`로 제한한다.

구체 .NET 시그니처(`ZLinkMessageMetadata`, `IZLinkMessageMetadataPolicy`,
`options.ConfigureMetadata(...)`)는
[.NET 인터페이스 §4.4.2](languages/dotnet/02-handler-interfaces.ko.md)를 참고한다.

### 8.4 실행 순서

session actor dispatch는 stream session에서 actor로 packet을 넘기는 경로다. 같은
session의 stream frame 도착 순서는 stream socket이 보존하고, framework는 session별
내부 실행 queue로 session callback을 직렬로 실행한다. 이 queue는 framework 내부
구현이며, 별도 session mailbox를 public 개념으로 추가하지 않는다.

session callback이 actor로 packet을 넘길 때 framework는 현재 actor 위치에 맞는 실행
경계로 넘긴다. actor가 Entry Spot에 있거나 아직 user Spot에 들어가지 않았다면 대상
actor의 mailbox에 packet을 넣는다. 같은 actor로 들어온 packet은 순서대로 실행하고,
서로 다른 actor는 서로 기다리지 않게 한다.

이 규칙은 Entry Spot actor packet과 같다. Entry Spot은 모든 actor가 거쳐 가는 공용
입구이므로 Entry Spot 전체 실행 줄에 actor packet을 세우면 안 된다. actor가 user Spot에
join한 뒤 user Spot handler가 처리하는 packet은 user Spot 실행 queue를 탄다. user
Spot은 room, game, stage 같은 공유 상태를 소유하므로 Spot 단위 순서가 필요하다.

### 8.5 등록 API

routed channel builder는 transport mesh 설정만 가진다. handler 등록을 routed channel
아래에 두면 transport 설정과 domain dispatch 설정이 섞인다.

actor handler는 전역 actor registry가 아니라 actor 객체의 실행 문맥 안에서 등록한다.
host 설정은 actor factory와 routed channel 역할만 등록하고, packet handler는
actor 객체가 자기 lifecycle 진입 단계 (binding별 이름은 다르지만 의미상 `Configure()`)
에서 등록한다. spot handler도 같은 방식으로 spot 객체 안에서 등록한다.

node 또는 spot 실행 문맥에서 처리해야 하는 메시지도 같은 원칙을 따른다. node handler는
node 등록 표면에, spot handler는 spot 등록 표면에 둔다. bound session은 handler
등록 표면을 갖지 않는다.

`packetName`은 기본적으로 message type metadata에서 얻는다. override가 필요하면
객체의 실행 문맥 안에서 명시한다. 동일한 실행 문맥 안에서 같은 `kind + packetName`에
handler가 둘 이상 등록되면 startup에서 실패해야 한다. payload type은 dispatch key가
아니라 decode 대상이다.

location store는 framework service 설정에 등록한다. framework는 그 store를 읽는 actor와
spot resolver를 제공한다. transport mesh를 고르는 일과 위치 저장소를 고르는 일을 같은
builder에 넣지 않는다.

구체 .NET 시그니처 (`spot.AddActorFactory<...>(...)`,
`options.AddRouteMeshChannel(...)`, `options.AddSpotMesh(...)`, location store 등록 코드,
actor / spot 객체 등록 sample)는
[bindings/dotnet/handler-interfaces.ko.md](languages/dotnet/02-handler-interfaces.ko.md)
§7을 참고한다.

startup validation은 아래 조건을 확인한다.

| 조건 | 실패 이유 |
|------|-----------|
| `IZLinkBoundSession`를 사용하지만 현재 actor-session binding이 없다. | actor가 어떤 client stream으로 보내야 하는지 확인할 수 없다. |
| actor 위치 조회가 필요한데 location store 가 등록되지 않았다. | actor id를 actor 위치로 해석할 수 없다. |
| 같은 actor type에 factory가 둘 이상 등록된다. | actor create target이 모호하다. |
| 같은 실행 문맥에 같은 packet handler가 둘 이상 등록된다. | dispatch target이 모호하다. |
| raw relay handler와 typed dispatch가 같은 routed channel에 함께 등록된다. | envelope 처리 주체가 두 개가 된다. |

### 8.6 낮은 수준 handler와의 관계

typed actor/node/spot handler는 기존 raw actor dispatch handler를 대체하는 기본 표면이다.
bound session에 handler를 등록하는 표면은 public contract에 포함하지 않는다.

raw actor dispatch handler는 정식 application public API로 남기지 않는다. custom envelope나
protocol adapter가 필요하면 framework internal service 또는 별도 adapter package에서
resolved transport helper를 사용한다. 같은 relay path에서 raw actor dispatch handler와
typed actor/node/spot dispatch를 동시에 선택하는 public 설정은 만들지 않는다.

## 9. Actor / Spot Route Resolvers

### 9.1 필요성

사용자 code에서 `RoutingId`가 반복되는 이유는 framework가 domain key를 transport
target으로 바꾸는 경계를 제공하지 않기 때문이다.

session server가 client packet을 play server로 보내려면 `actorId` 또는 request payload의
domain key에서 actor node `RoutingId`를 찾아야 한다. actor가 user Spot에 들어갈 때는
`spotRid` 또는 `spotRid`에서 target Spot 위치를 찾아야 한다.

actor와 spot의 domain key를 선택하는 일은 application 정책이다. 선택된 key의 위치를
찾는 일은 framework가 등록된 location store를 읽어 수행한다. resolver 입력은 좁게
유지해야 하며 metadata나 raw message를 받지 않는다.

### 9.2 resolver interface

위치 resolver는 framework가 기본 구현을 제공하는 조회 interface다. 기본 구현은
location store를 읽고 owner lease join으로 유효성을 판정하며, 내부 주소 갱신을
소유하는 **SpotHandle**을 반환한다
([location runtime](40-location-runtime.ko.md) §5). 공개 resolver 축은 두 개로 제한한다.

- **actor location resolver** -- `actorId` → "이 actor가 위치한 spot의
  `SpotHandle`".
- **spot location resolver** -- `spotRid` → "이 spot의 `SpotHandle`". actor의
  `JoinSpot(...)` 같은 표면에서 transport 위치값을 숨긴다. 사용자 교체 지점은 location
  store 등록이다.

actor resolver 입력은 `actorId` 하나로 유지한다. spot resolver 입력은 spot domain key
하나로 유지한다. metadata, packet name, raw message, application payload는 resolver에
넘기지 않는다. game 단위 배치가 필요하면 `gameId` 자체를 actor id 또는 spot rid로
설계하거나, session의 domain placement code에서 actor/spot 대상을 먼저 결정한다.
resolver가 raw message 객체나 packet switch를 직접 보지 않게 해야 resolver가 작은
dispatcher가 되지 않는다.

session 위치는 공개 resolver로 두지 않는다. actor가 client에게 메시지를 보낼 때 필요한
session binding은 framework/core runtime 내부 상태다. `IZLinkBoundSession` 같은 표면은
actor id나 현재 actor context를 받아 내부 binding을 사용해야 하며, application이
`actorId -> session node` resolver를 구현하게 하지 않는다.

resolver가 반환하는 target node 식별값(`RoutingId`류)은 사용자가 일반 handler에서
직접 다루는 값이 아니다. transport 위치값은 resolver 구현체와 framework routed
transport 내부에만 머물러야 한다.

actor location resolver는 actor id만 알고 그 actor의 위치를 찾아야 하는 흐름(재연결의
"있으면 re-bind" 판단 등)에서 사용한다. session callback이 이미 bind로 받은 actor
handle(`IZLinkSessionActor` — `Ref`와 `RelayAsync(...)`)을 이미 얻었으면 위치를 다시
조회하지 않는다. relay는 handle 안의 actor ref를 사용하므로 store를 다시 읽지 않는다.

구체 .NET 표면(`IZLinkSessionActors.BindAsync(...)`, `IZLinkSessionActor.RelayAsync(...)`,
`IZLinkActorManager`)은
[bindings/dotnet/handler-interfaces.ko.md](languages/dotnet/02-handler-interfaces.ko.md)를
참고한다.

### 9.3 resolver가 숨기는 정보

resolver가 생기면 session code는 아래 결정을 직접 하지 않는다.

- 이 actor가 어느 play node에 있는가
- 이 game이 어느 play node에 있는가
- 이 spot이 어느 node와 rid에 있는가
- 어떤 routed channel을 써야 하는가

application은 여전히 위치 저장소를 가질 수 있다. 차이는 위치 저장소 접근이
여러 handler에 퍼지는 대신 resolver 한 곳으로 모인다는 점이다.

### 9.4 실패 의미

resolver가 route를 찾지 못하면 framework는 transport request를 보내지 않는다.
실패는 호출자에게 명확하게 돌아와야 한다.

| 상황 | 의미 |
|------|------|
| actor route 없음 | actor 또는 game을 처리할 actor/play node를 찾지 못했다. |
| spot route 없음 | spot id에 대응하는 user Spot을 찾지 못했다. |
| actor-session binding 없음 | actor가 현재 어떤 stream session에도 binding되어 있지 않다. |
| resolver timeout | route 결정 자체가 제한 시간 안에 끝나지 않았다. |
| resolver exception | location store 조회가 실패했다(infrastructure error). |

이 실패들은 retry로 숨기지 않는다. 특히 store 기반 자동 연결이 아직 되지 않은
상황(연결 미수립)과 location row가 없는 상황(위치 없음)은 원인이 다르며, 전송 실패의
fail-fast 분류가 이 둘을 구분한다. sample과 framework helper는
임의 sleep이나 retry loop로 이를 덮지 않아야 한다.

### 9.5 위치 소유와 갱신 책임

actor 위치 정보는 두 층으로 나눈다.

| 계층 | 소유자 | 내용 |
|------|--------|------|
| actor-session binding | framework/core runtime | 현재 actor와 연결된 stream/session을 관리한다. |
| actor/spot location resolution | framework location runtime | `actorId`, `spotRid` 같은 domain key가 어느 node/spot에 있는지 location store의 row로 결정한다. row는 actor/spot lifecycle이 자동 갱신한다. |

framework는 actor-session binding을 내부 상태로 가진다. 인증이 끝나면 현재 stream에
actor를 binding하고, 같은 actor가 다시 연결되면 이전 binding을 교체할 수 있어야 한다.
disconnect나 session close는 현재 binding token이 맞을 때만 해제되어야 한다. 이 규칙은
runtime 내부 상태 갱신 규칙이지 application이 구현하는 resolver 계약이 아니다.

위치의 권위 있는 저장소는 사용자가 등록한 location store다. store 구현체 선택
(공식 Redis extension, 사용자 구현체)은 application 배포 결정이고, row의 갱신은
framework lifecycle이 수행한다. placement 정책(game room을 어느 node에 만들지)은
여전히 application의 것이지만, 그 결과는 location row로 기록된다.

새 handle을 만드는 resolve는 store에 도달한다. 위치를 반복 사용하는 호출자는
`SpotHandle`만 보관하고 framework가 내부 주소 snapshot과 안전한 1회 갱신을 관리한다.
handler 실행 여부가 불확실한 timeout은 자동 재전송하지 않는다
([location runtime](40-location-runtime.ko.md) §1, §5).

### 9.6 location store 구현 예

framework의 기본 resolver는 등록된 location store를 읽는다. sample은 공식 Redis
extension을 등록하는 방식(`AddLocationStore(new ZLinkRedisLocationStore(...))`)을
보여 주고, 사용자 저장소가 필요한 경우 store 계약(`IZLinkLocationStore`)을 구현해
같은 지점에 등록하는 방식을 보여 준다([Redis extension](41-location-store-redis.ko.md)).

store 선택은 resolver 입력 metadata가 아니다. resolver 입력은 여전히 `actorId`
또는 spot domain key 하나다. store 연결 정보는 등록 시점의 infrastructure 설정이며,
framework가 특정 store 제품을 자동으로 선택하지 않는다는 정책은 binding에 관계 없이
동일하다.

## 10. Session Actor Helpers

### 10.1 목적

session은 application 정책을 가장 많이 아는 ingress다. 그래서 session builder가 actor
생성, actor type 선택, packet별 relay 여부를 자동으로 정하면 안 된다. session 표면은
사용자가 직접 dispatch 흐름을 작성할 수 있게 하되, framework가 반드시 보존해야 하는
transport 세부 작업만 helper로 제공한다.

현재 사용자가 직접 작성하는 흐름은 아래와 비슷하다.

1. stream packet을 받는다.
2. 인증 packet이면 application이 actor id와 actor type을 결정한다.
3. local `SpotNode` actor runtime에 actor를 만들거나 기존 actor handle을 얻기 위해
   actor 생성/조회(`IZLinkActorManager.GetOrCreateAsync`) 후 `session.Actors.BindAsync(...)`를 호출한다.
4. packet별로 actor dispatch, session-local reply, error reply 중 하나를 고른다.
5. request이면 원래 stream request sequence로 reply한다.

framework는 이 결정을 자동으로 하지 않는다. 대신 아래 작업을 깊은 helper로 제공한다.

| 흐름 | framework가 숨기는 작업 |
|------|------------------------|
| 생성 후 bind (`GetOrCreateAsync` + `BindAsync`) | 현재 node의 actor runtime에 actor 생성 요청, 현재 actor-session binding 갱신 |
| 기존 actor bind (`session.Actors.BindAsync`) | local `SpotNode` actor runtime의 dispatch handle 생성, 현재 actor-session binding 갱신 |
| relay (`handle.RelayAsync`) | actor dispatch envelope와 원본 stream request sequence를 보존한 actor dispatch |

### 10.2 session context API

**session context는 packet handler registry를 갖는다.** session은 `Configure()` 안에서
`Context.Handlers`에 packet handler를 등록하고(등록 창은 `Configure()` 실행 중으로 한정,
같은 packet name 중복 등록은 startup 오류), inbound dispatch callback에서 그 registry로
분기한다. **runtime이 registry를 자동으로 호출하지는 않는다** — session의 dispatch callback이
registry의 `TryHandle` 계열 호출로 위임하고, 미등록 packet이면 그 호출이 실패를 반환한다.
등록 축의 공통 계약은 [framework API §3.3](05-framework-api.ko.md)이 소유한다.

session 표면이 노출하는 핵심 흐름은 셋이다.

- **actor 생성 후 bind** — `IZLinkActorManager.GetOrCreateAsync(...)`로 현재 session
  host의 actor runtime에 actor를 만들거나 얻고, `session.Actors.BindAsync(...)`로
  현재 session에 묶는다.
- **기존 actor bind** — `session.Actors.BindAsync(actorRef, ct)`. 현재 session host의
  local `SpotNode` actor runtime에서 dispatch handle을 만든다. remote node를 직접
  지정하지 않는다.
- **relay** — `handle.RelayAsync(payload, ct)`. actor dispatch envelope와 원본
  stream request sequence를 보존한 actor dispatch다.

session actor handle(`IZLinkSessionActor`, binding별 동등 이름)은 local `SpotNode`
actor runtime의 actor에 대한 dispatch handle만 노출한다. application actor 객체
(`IZLinkActor`) 자체와는 다른 표면이며, `Configure()`나 handler registry를 갖지 않는다.

구체 `.NET` 시그니처(`IZLinkSessionContext`, `IZLinkSessionActor`)는
[.NET 인터페이스 §4.4](languages/dotnet/02-handler-interfaces.ko.md)를 참고한다.

생성 후 bind(`GetOrCreateAsync` + `BindAsync`)는 현재 session host가 가진 actor runtime에 create 요청을 보낸다.
이 함수는 actor 생성과 현재 session binding metadata 생성을 하나의 작업으로 묶어,
후속 dispatch가 항상 같은 actor ref 표면을 사용하게 한다.

`session.Actors.BindAsync(...)`도 remote node 선택을 받지 않는다. actor는 local `SpotNode`
actor runtime에서만 생성되므로, 이 호출은 이미 얻은 actor(또는 actor ref)로 local dispatch
handle을 준비하고 현재 session binding metadata를 만든다. 존재하지 않는 actor를 어떻게
만들지는 `IZLinkActorManager`와 factory 정책의 몫이다.

생성 후 bind 와 기존 actor bind는 같은 후속 dispatch 표면을
반환하지만 같은 종류의 create 요청은 아니다. 생성 후 bind(`GetOrCreateAsync` + `BindAsync`)만 현재 node의 actor
runtime에 명시 create를 요청한다. `session.Actors.BindAsync(...)`는 local actor runtime의
handle을 준비하고 현재 actor-session binding을 갱신한다. framework session helper는
remote node actor 생성 정책을 만들지 않는다.

명시 create와 handle 준비는 이후 같은 session actor handle(`IZLinkSessionActor`) 표면으로 dispatch된다. 사용자가
packet마다 `RoutingId`를 넘기지 않게 하고, actor 생성과 session binding을 local
`SpotNode` actor runtime 안에 가두는 것이 이 표면의 목적이다.

`IZLinkActor`와 session actor handle(`IZLinkSessionActor`)는 의도적으로 분리한다. `IZLinkActor`는 actor node에서
실제로 생성되는 application 객체이고, constructor로 받은 `IZLinkActorContext` 안에서
handler를 등록한다. session actor handle(`IZLinkSessionActor`)은 session이 actor로 dispatch할 때 사용하는
handle이다. actor ref는 application actor 객체가 아니므로 `Configure()`나 handler registry를
갖지 않는다.

### 10.2.1 actor create lifecycle

생성 후 bind 와 기존 actor bind는 session-bound actor 경로에서
actor ref와 session binding metadata를 한 호출 안에서 함께 준비하는 helper다.
생성 후 bind(`GetOrCreateAsync` + `BindAsync`)는 local actor 생성까지 묶고, `session.Actors.BindAsync(...)`는
local actor handle 준비와 현재 actor-session binding 갱신만 묶는다. unbound standalone actor는 이
표면으로 만들지 않으며, actor node 측 별도 등록 표면(예: actor node가 직접 부르는
`CreateActor` 계열 helper)으로 다룬다.

1. framework가 현재 session에 대한 새 `BindingToken`을 만든다.
2. 생성 후 bind(`GetOrCreateAsync` + `BindAsync`)이면 현재 node의 actor runtime에 create 요청을 보내고,
   `session.Actors.BindAsync(...)`이면 local actor runtime에서 session actor handle(`IZLinkSessionActor`)를 준비한다.
3. local create가 actor를 만들거나 기존 actor를 반환하면 같은 session actor handle(`IZLinkSessionActor`) 표면으로
   감싼다.
4. current session server의 local binding table에
   `actorId -> sessionId + bindingToken`을 기록한다.
5. framework/core runtime의 actor-session binding 상태를 갱신한다.
6. binding 갱신이 끝나면 session actor handle(`IZLinkSessionActor`)를 caller에게 반환한다.

binding 갱신이 실패하면 create helper는 실패해야 한다. 이때 framework는 current session
server의 local binding table에서 같은 `bindingToken`을 가진 entry를 제거한다.
생성 후 bind(`GetOrCreateAsync` + `BindAsync`)로 local actor가 이미 만들어졌다면 그 actor lifetime은 actor
runtime 정책을 따른다. session binding 실패는 client-facing route 실패이기 때문이다.

session actor bind 는 target node 선택을 하지 않고 target node id를 받지도
않는다. 다른 node의 actor로 보내야 하는 흐름은 session actor helper가 아니라 actor
location resolver로 주소를 얻는 actor 호출 경로로 다룬다.

session close 또는 stream disconnect가 발생하면 framework는 current session server가
가진 actor binding마다 아래 순서로 정리한다.

1. local binding table에서 같은 `sessionId + bindingToken`인 entry만 제거한다.
2. framework/core runtime의 actor-session binding 상태에서 같은 token을 가진 entry만
   제거한다.

disconnect 정리는 best-effort다. 같은 actor가 이미 새 stream에 연결되었다면 이전
stream의 늦은 정리가 새 binding token을 지우면 안 된다. stale binding이 남으면 bound
session의 send나 disconnect가 core binding token 검증에 실패하고 명확한 error를
돌려준다.

disconnect unbind는 actor-session binding만 정리한다. room leave, Entry Spot 복귀,
actor destroy는 disconnect cleanup에서 자동으로 실행하지 않는다. actor를 끝내야 하는
시점은 application의 room/game/stage lifecycle이 결정하며, 그때 user Spot에서
`leaveActor`로 actor를 Entry Spot으로 이동한 뒤 Entry Spot context의 destroy API를
호출한다.

### 10.3 직접 dispatch 흐름 (정책)

session callback 안에서 정책 code는 아래 흐름을 따른다 (framework가 builder로 자동
제공하는 것이 아니라, application이 직접 분기한다).

1. stream packet을 받는다.
2. 인증 packet이면 application이 actor id와 actor type을 결정한 뒤
   actor 생성/조회(`IZLinkActorManager.GetOrCreateAsync`) 후 `session.Actors.BindAsync(...)`를 호출한다. helper가
   local actor 생성 또는 local handle 준비, session binding metadata 생성, 내부 binding 갱신을
   한 묶음으로 처리한다.
3. 인증된 actor가 있는 packet은 `handle.RelayAsync(payload, ct)`로
   actor 실행 문맥으로 넘긴다. 인증되지 않은 packet은 application이 명시 error로
   거부한다.
4. session-local reply가 필요한 경우 session context의 reply helper를 사용한다.
5. 같은 stream request sequence는 framework가 internal envelope에만 보존하므로,
   application code는 sequence를 직접 다루지 않는다.

구체 .NET 코드 예시(`TicTacToeSession.OnDispatchAsync(...)`,
`session.Actors.BindAsync(...)`, `Context.Reply(...).Submit(...)`,
`ZLinkFrameworkException` 던지기)는
[bindings/dotnet/handler-interfaces.ko.md](languages/dotnet/02-handler-interfaces.ko.md)
§8을 참고한다.

framework가 맡는 규칙은 아래로 제한한다.

- 원본 stream request sequence를 보존한다.
- 원본 payload bytes는 sample serializer가 아니라 framework codec/message 경로로
  다룬다.
- `handle.RelayAsync(...)`는 raw stream frame relay 세부 처리를 숨긴다.
- reply matching은 message name이 아니라 request sequence 기준으로 유지한다.

### 10.4 Session ingress와 bound session

session ingress와 bound session은 방향이 다르다. session ingress는 client packet을
actor 실행 문맥으로 보낸다. bound session은 현재 actor가 연결된 client에 one-way
message를 보내거나 연결을 종료한다.

| 표면 | 역할 | target 판단 |
|------|------|-------------|
| session ingress | auth, ping, reconnect 같은 session-local packet을 처리하거나 actor로 dispatch한다. | actor handle과 framework runtime 내부 |
| bound session | 현재 actor가 client session으로 one-way send하거나 연결을 종료한다. | core actor-session binding |

따라서 **bound session**은 message handler registry를 갖지 않는다. bound session은 actor가
client로 push하는 one-way 표면일 뿐이기 때문이다. handler registry는 actor, node, spot, 그리고
**session**(§10.2) 실행 문맥에 존재한다.

## 11. Bound session 호출 표면

play server의 actor가 자기 client로 보내는 호출은 actor context의 bound session이
맡는다. 호출자는 actor id, session id, session node나 `RoutingId`를 넘기지 않는다.
framework가 현재 actor id로 core actor-session binding을 조회하고 현재 binding token을
검증한 뒤 client stream으로 보낸다.

bound session은 아래 두 동작만 제공한다.

- typed message를 client로 보내는 one-way `Send(...)`
- 현재 actor에 연결된 client stream을 종료하는 `Disconnect(...)`

client를 향한 request/reply는 bound session 계약에 포함하지 않는다. client 응답이
필요한 업무 흐름은 별도의 client protocol message와 이후 session ingress message로
모델링한다. 임의 actor id를 받는 전역 client 전송 객체나 application이 관리하는 별도
binding 저장소도 두지 않는다.

bound session send는 내부적으로 아래 순서로 처리된다.

1. message type에서 packet name을 얻고 framework codec으로 payload를 encode한다.
2. core actor-session binding에서 현재 actor의 session과 binding token을 확인한다.
3. token이 현재 값일 때만 해당 client stream으로 one-way packet을 보낸다.
4. binding이 없거나 token이 stale이면 `ActorSessionNotBound`로 실패한다.

구체 .NET 시그니처(`IZLinkBoundSession`, `IZLinkBoundSessionSendCall`)와 사용 예시는
[bindings/dotnet/handler-interfaces.ko.md](languages/dotnet/02-handler-interfaces.ko.md)
§4를 참고한다.

이 모델에서 actor 객체는 session 위치 정보를 직접 소유하지 않는다. 권위 있는 binding
상태는 core가 맡으며 application public resolver나 저장소로 분리하지 않는다. session에서
actor로 가는 방향은 actor create/bind/relay helper이고, actor에서 client로 가는 방향은
bound session이다. 두 방향을 하나의 gateway나 proxy 객체로 합치거나 호환 alias를 두지
않는다.

### 11.1 push 실패 격리

**stale binding token, 이미 닫힌 stream, 늦게 도착한 push는 그 push 하나만 실패시킨다.**

**route receive loop나 host shutdown 자체를 실패시켜서는 안 된다.** 이 실패는 로그·counter·
message flow error event로 기록한다.

**client의 처리 완료 ack가 필요하면** actor message나 session message로 application이 직접
설계한다. framework가 push 전달 보장을 제공하지 않는다.

## 12. Codec 정책

session actor dispatch sample은 별도 serializer helper를 두면 안 된다. framework에는 codec
등록과 message encode/decode 경로가 있으므로, sample이 다시 `JsonSerializer`를 직접
감싸면 두 가지 문제가 생긴다.

- 사용자는 framework codec을 써야 하는지 sample helper를 써야 하는지 헷갈린다.
- internal packet과 application packet의 serialization 정책이 분리되어 보인다.

상위 표면은 아래 규칙을 따른다.

- application message는 등록된 framework codec registry로 encode/decode한다.
- typed handler는 codec registry가 decode한 payload를 받는다.
- internal session actor dispatch envelope와 bound-session envelope는 framework가 소유한다.
- internal envelope의 wire 형식은 public sample code가 알 필요 없다.
- 내부 routed transport에서는 envelope header와 payload를 한 `Message`로 합치지 않는다.
  서버 간 경로는 공통 message model의 multipart 계약을 따른다.
- sample에는 `SampleJson` 같은 serializer wrapper를 두지 않는다.

internal metadata part가 JSON을 쓰는지, binary codec을 쓰는지는 framework 구현 선택이다.
하지만 metadata part와 payload part를 하나로 합치는 것은 구현 선택이 아니다. 중요한
계약은 application handler가 framework codec registry와 typed message만 보고, 내부
server-to-server wire는 multipart 경계를 유지한다는 점이다.

### 12.1 내부 routed wire 계약

**server 사이를 잇는 내부 route transport는 [message-model](03-message-model.ko.md)의 multipart
계약을 따른다.** public API는 typed object 중심이지만, wire는 part로 나뉜다.

**core의 actor gateway가 이 구간을 소유한다.** 아래 3-part 구성은 **core gateway의 하위 전송
계층**이며 [03 message model](03-message-model.ko.md)의 framework envelope(`parts[0]`=header,
`parts[1]`=payload)과 **다른 계층이다.** framework handler는 이 control part들을 보지 않는다 —
언어별 framework runtime은 라우팅 정보를 wire part가 아니라 gateway가 제공하는 필드로 받을 수
있다. framework는 stream frame과 payload를 넘기고, gateway가 각 delivery를 다음 구성으로 만든다.

| part | 내용 |
|---|---|
| `parts[0]` | **routed control.** spot routed 계층이 붙이는 라우팅 head |
| `parts[1]` | **actor gateway control.** kind, session rid, **actor id**, **generation**, multipart 종료 flag |
| `parts[2]` | **payload part** — framework envelope 또는 완전한 stream frame이 들어간다 |

**gateway control에는 packet name이 없다.** routed control과 gateway control은 **target을
결정할 뿐 application handler를 결정하지 않는다.** handler는 **payload part의 stream header를
decode한 뒤 그 packet name으로** 고른다.

#### 12.1.1 Session 서버 → Play 서버 actor

**stream header와 stream payload를 하나의 메시지에 4개 part로 묶지 않는다.** framework는 두 part를
넘기고, **gateway가 각 part를 별도 delivery로 보낸다.** 각 delivery는 위 3-part 구성을 갖는다.

**gateway control의 flag는 header/payload 종류를 나타내는 값이 아니라 multipart 종료 표시다** —
첫 delivery에 `MORE`, 마지막에 `FINAL`이 붙는다. **받는 쪽은 순서로 판단한다.** 첫 part를 stream
header로 decode하고, 그 `MORE` 값으로 **body가 뒤따르는지** 판단한 뒤 다음 part를 body로 받는다.

#### 12.1.2 Play 서버 actor → Session 서버의 client stream

**bound session push는 완전한 stream frame 하나를 보낸다.** framework가 stream header와 payload를
**하나의 frame으로 encode한 뒤** gateway에 넘기므로, `parts[2]`는 **header + payload가 합쳐진 완전한
stream frame**이다.

**reply도 같다** — 완전한 frame 하나를 전달한다.

#### 12.1.3 왜 이렇게 나누는가

**application payload를 route 경로에서 재인코딩하지 않기 위해서다.**

- **route와 dispatch는 control part만 읽고 target과 handler를 결정할 수 있어야 한다.**
- **application payload는 handler가 정해진 뒤에 등록된 codec이 decode해야 한다.**

그래야 큰 payload·binary payload·압축 payload·attachment가 들어와도 **framework 내부 route
경로가 payload 재인코딩 비용을 떠안지 않는다.**

**금지하는 형태:**

- **단일 DTO 안에 stream header와 payload bytes를 같이 넣고, 그 DTO 전체를 다시 직렬화**하는 방식
- **control part 하나만 보내고 그 안에서 header와 payload를 모두 decode**하는 방식

#### 12.1.4 STREAM은 예외다

**client와 Session 서버 사이의 STREAM transport는 stream packet 하나 안에 header와 payload frame을
그대로 담는다**([Stream Connector §4](32-stream-connector.ko.md)). 위 multipart 계약은
**framework 서버끼리 주고받는 routed transport 구간에만** 적용한다.

## 13. 자동 연결 정책

session actor dispatch sample의 기본 연결 방식은 location store 기반 자동 연결이어야
한다. session server와 play server가 늘고 줄 수 있는 구조이므로, 수동 peer endpoint를
sample 중심에 두면 실제 사용 모델과 어긋난다.

상위 표면은 아래 정책을 유지한다.

- routed channel은 location store가 등록되어 있으면 store 기반 mesh 구성원으로
  참여한다.
- service channel client와 routed channel은 location store가 등록되어 있으면
  store의 peer row로 peer를 찾는다.
- 같은 역할에 수동 endpoint가 있으면 그 역할은 수동 연결로 확정되고 자동 연결 reconcile이
  돌지 않는다([10 §5](10-channel-topology.ko.md)). peer source가 아예 없으면 startup에서 실패한다.
- sample은 자동 연결 실패를 retry loop나 sleep으로 숨기지 않는다.
- 바로 연결되지 않으면 location runtime query(status, peer list, runtime이 합성한 topology 보기)를
  먼저 확인할 수 있어야 한다.

diagnostic helper는 retry helper와 다르다. diagnostic helper는 location runtime query
결과와 local routed channel state를 보여 주는 역할만 한다. 구체 .NET
시그니처(`IZLinkTopologyDiagnostics`)는
[bindings/dotnet/handler-interfaces.ko.md](languages/dotnet/02-handler-interfaces.ko.md)
§10을 참고한다.

이 표면은 session actor dispatch의 필수 API가 아니라 운영 점검 표면에 가깝다. 다만 sample
자동 연결 상태 확인은 monitoring 계약과
함께 관찰할 수 있어야 한다.

## 14. SPOT direct target API 정책

`Spot` 호출에서도 사용자가 `SpotRid`와 함께 node `RoutingId`를 알아야 하는 표면은
transport 위치 정보를 application 코드에 노출하므로 public contract에 포함하지 않는다.

framework public API 표면은 spot 호출이 node 경계를 넘을 때 spot location
resolver로 얻은 handle을 사용한다. session-gateway sample에서 game room이 같은 Play
서버 안에만 있으면 resolve가 드러나지 않을 수 있지만, 문서의 cross-node 계약은
handle 조회와 안전한 내부 갱신 규칙을 포함해야 한다. 멀티게임 sample의 game room은 도메인 핵심 실행 문맥이므로
Play 서버 안에서는 `IZLinkSpotManager`로 room SPOT을 만든다. **room 식별자는 application이
정하는 도메인 값**(예: `RoomId`)이며, spot routing id는 그 값에서 파생한다. core routing id의
hex 문자열을 client 계약에 그대로 노출하지 않는다([샘플 규약](../sample/README.ko.md)). actor가
join한 room 안에서 `PlaceMarkReq`가 처리된다. location row는 actor play route sample,
spot route sample, actor-session binding 보조 상태를 각각 분리해서 관리해야 한다.

`SpotRid` 기반 public 호출에서 resolver 입력은 request 객체가 아니라
`RoutingId spotRid` 하나로 제한해야 한다. metadata가 필요해 보인다면 resolver가 route
조회 이외의 정책을 함께 하려는 신호다. 그런 정책은 spot rid를 정하는 caller나 domain
placement code에 둔다.

direct target API는 public application 표면에서 기본으로 노출하지 않는다.

```csharp
// 일반 application 표면으로 권장하지 않는다.
await spotClient
    .SendTo(spotNodeRid, spotRid, message)
    .Submit(cancellationToken);
```

`SpotNodeId`나 `RoutingId`를 알아야만 호출할 수 있는 API가 public 문서와 sample에
함께 있으면 두 가지 사용법이 경쟁한다. 그러면 사용자는 resolver를 써야 하는지,
직접 target을 넘겨야 하는지 계속 판단해야 한다. framework의 사용성 목표가 transport
위치값 노출 제거라면, application public API는 `SpotRid` 기반으로 통일해야 한다.

resolved route를 받는 transport helper는 public application API가 아니라
runtime/internal service로 둔다.

bound session과 session actor helper도 같은 원칙을 따른다. `RoutingId`를 받는
`SendToActor(...)`류 send/request 표면은 제거한다. sample은 actor -> client 방향에서
현재 actor context의 bound session만 사용하고, session -> actor binding은 local
`SpotNode` actor runtime의 actor id/type만 사용해야 한다.

## 15. Timeout과 retry 정책

상위 표면은 retry를 기본 제공하지 않는다.

session actor dispatch에서 실패 원인은 서로 다르다.

| 실패 | 원인 | framework 기본 동작 |
|------|------|--------------------|
| route 없음 | actor/game/spot 위치가 없다. | 즉시 실패 |
| mesh peer 미연결 | routed mesh가 아직 연결되지 않았다. | `RouteNotConnected` 계열 fail-fast error |
| send backpressure | socket이 지금 보낼 수 없다. | bounded pending queue와 `SendTimeout` 적용 |
| request reply 없음 | peer가 reply하지 않았다. | `RequestTimeout` 적용 |
| client session 없음 | actor의 현재 actor-session binding이 없다. | bound session send 또는 disconnect 실패 |

retry를 넣으면 위 원인이 섞인다. sample에서는 특히 자동 연결 버그와 topology 설정
문제를 숨기므로 넣지 않는다.

`SendTimeout`은 core blocking send timeout이 아니라 framework async pending deadline
으로 사용한다. stream connector public option에는 `SendTimeout`을 두지 않고,
request/reply 대기는 `RequestTimeout`으로 다룬다.

## 16. typed dispatch 계약

### 16.1 public contract에서 제외하는 raw relay 방식

raw actor dispatch handler가 framework envelope와 packet switch를 직접 다루는 방식은
application public contract에서 제외한다.
application handler가 packet name switch, raw payload decode, relay envelope 조립과
domain service 호출을 한곳에서 수행해서는 안 된다. 이 책임은 typed dispatcher,
codec registry와 session actor helper가 나누어 맡는다.

### 16.2 정식 방식

정식 계약에서는 actor handler가 framework envelope와 packet switch를 몰라도 된다.
domain message handler는 actor 실행 문맥에만 등록된다.

정식 계약의 코드 모양은 아래 흐름으로 정리한다.

- actor handler -- typed request payload, actor instance와 해당 언어의 handler context만
  받는다. raw envelope나 packet switch는 없다.
- actor 객체 -- `Configure()` 단계에서 자기에게 보낼 packet handler를 명시적으로
  등록한다.
- host 등록 -- transport(routed channel, stream node)와 actor factory만 등록한다.
- session callback -- 인증 packet에서 actor id와 actor type을 결정한 뒤 actor를
  생성하거나 조회하고 session에 bind한다. 이후 domain packet은 session actor handle의
  typed relay 작업으로 전달한다.

framework가 보장하는 부분은 local actor create 또는 handle 준비와 현재 session binding
metadata 갱신을 한 흐름으로 묶고, dispatch helper가 원본 request sequence와 codec 경로를
보존한다는 점이다.

구체 `.NET` 코드 (`JoinMatchHandler`, `PlaceMarkHandler`, `TicTacToeActor`,
`TicTacToeSession`, `options.AddRouteMeshChannel(...)`, `spot.AddActorFactory(...)`,
`options.AddStreamNode(...)`)는
[.NET Session Actor Dispatch](languages/dotnet/02-handler-interfaces.ko.md)
§7-8에 옮겼다.

## 17. Error 의미

상위 표면은 error를 숨기지 않는다. 다만 error를 사용자마다 다르게 조립하지 않도록
framework가 공통 error kind를 제공해야 한다.

| error kind | 발생 조건 |
|------------|-----------|
| `RequestRejected` | 인증되지 않은 session이 relay 대상 packet을 보냈다. detail에서 인증 실패를 구분한다. |
| `ActorRouteNotFound` | play route를 찾지 못했다. resolver 결과의 `RouterChannelId` 또는 `TargetNodeRid`가 빠지면 같은 error로 본다. |
| `ActorCreateFailed` | 생성 후 bind(`GetOrCreateAsync` + `BindAsync`)에서 local actor를 만들지 못했다. |
| `ActorAlreadyExists` | local actor runtime 정책상 같은 actor id의 중복 create를 허용하지 않는다. |
| `ActorSessionNotBound` | 현재 actor의 client session binding이 없다. |
| `RequestFailed` | actor dispatch request가 제한 시간 안에 완료되지 않았거나 handler가 reply를 만들지 못했다. detail에서 timeout과 handler 실패를 구분한다. |
| `PayloadDecodeFailed` | payload decode가 실패했다. encode 실패는 request 전송 전 호출 오류로 처리한다. |

request/reply 경로에서는 가능한 한 error reply로 돌려보낸다. one-way send 경로는
완료 객체를 반환하지 않으므로 기본 로그, counter와 message-flow error event로
실패를 기록한다. client stream에 어떤 wire error를 보낼지는 stream failure semantics
문서와 맞춘다.

각 binding은 위 error kind들을 자기 idiom에 맞는 단일 exception/error family로 모아야
한다. retry 가능 여부도 오류 family 안에서 구분할 수 있어야 한다. 이 분류는
framework가 자동 retry한다는 뜻이 아니라 caller가 retry policy를 만들 때 참고하는
정보다. sample은 이 값을 사용해 retry loop를 만들지 않는다.

구체 .NET 시그니처(`ZLinkFrameworkException`, `ZLinkFrameworkErrorKind`)는
[.NET Session Actor Dispatch](languages/dotnet/02-handler-interfaces.ko.md)
§9를 참고한다.

## 18. 금지하는 public 표면

application public API는 session actor helper와 현재 actor의 bound session으로
통일한다. 다음 표면은 정식 계약에 포함하지 않는다.

- session에서 actor로 가는 dispatch와 actor에서 client로 가는 push를 하나의 gateway
  객체에 합친 표면
- raw actor relay handler와 raw envelope dispatch 등록
- target session node routing id를 application이 직접 넘기는 send/request
- actor-session binding token이나 session 위치 row를 application이 직접 갱신하는 API

actor에서 client로 보내는 방향은 bound session 표면을 사용한다. session에서 actor로
보내는 방향은 actor 생성 또는 조회, session bind와 session actor handle relay로 표현한다.

내부 runtime에는 resolved transport helper가 필요할 수 있다. 그러나 public application
표면에 같이 노출하면 사용자가 두 사용법 사이에서 판단해야 하므로, 정식 문서와 sample에는
목표 public API만 보여 준다.

## 19. 테스트 기준

구현이 완료되었다고 보려면 아래 회귀 테스트가 필요하다. 테스트 이름은 구현 단계에서
프로젝트 규칙에 맞게 정해도 되지만, 표의 확인 내용은 빠지면 안 된다.

### 19.1 typed dispatch와 public API

| 테스트 | 확인 내용 |
|--------|-----------|
| typed actor request dispatch | `packetName` switch 없이 typed handler가 호출된다. |
| typed actor send dispatch | reply 없는 relay packet이 typed send handler로 들어간다. |
| request sequence 보존 | 같은 packet name으로 여러 request를 보내도 sequence 기준으로 reply가 매칭된다. |
| actor object/ref 분리 | actor ref가 application actor 객체나 `Configure()`를 노출하지 않는다. |
| 오래된 public API 제거 | 두 방향을 합친 gateway, 전역 client 전송 객체, raw actor relay 등록이 새 public sample과 guide에 남지 않는다. |
| raw relay public registration 제거 | raw relay public handler와 typed dispatch를 같은 routed channel에서 동시에 켤 수 없다. |

### 19.2 location resolve와 direct target 제거

| 테스트 | 확인 내용 |
|--------|-----------|
| actor resolve path | actor location resolver가 actor id로 주소를 찾고, 그 주소로 routed message가 간다. |
| spot resolve path | spot location resolver가 spot rid로 주소를 찾고, `JoinSpot(...)`과 spot 호출이 그 주소로 routed message를 보낸다. |
| bound session binding path | bound session push가 core actor-session binding으로 client 대상 session을 찾아 보낸다. |
| bound handle path | session bind로 받은 actor handle relay는 위치를 다시 조회하지 않는다. |
| resolver 입력 제한 | actor resolver는 actor id만, spot resolver는 spot rid만 입력으로 받고 metadata, packet name, payload를 받지 않는다. |
| missing store validation | 위치 조회가 필요한 표면 사용 시 location store가 없으면 명확한 framework error가 난다. |
| missing actor-session binding validation | `IZLinkBoundSession` 사용 시 actor-session binding이 없으면 명확한 framework error가 난다. |
| direct target send/request 제거 | public send/request sample과 guide에서 `RoutingId` target을 직접 받는 API를 사용하지 않는다. |

### 19.3 actor create와 session location lifecycle

| 테스트 | 확인 내용 |
|--------|-----------|
| local actor create | 생성 후 bind(`GetOrCreateAsync` + `BindAsync`)가 현재 node actor runtime에 create 요청을 보내고 actor-session binding을 조건부 갱신한다. |
| local actor handle | `session.Actors.BindAsync(...)`가 local `SpotNode` actor runtime의 handle을 만들고 session binding metadata를 전달한다. |
| binding update failure rollback | actor-session binding 갱신이 실패하면 helper가 실패하고 local binding table의 같은 token entry를 제거한다. |
| disconnect unbind | stream close 뒤 같은 `sessionId + bindingToken` entry만 actor-session binding에서 제거한다. |
| disconnect does not destroy | stream close 또는 stale unbind가 room leave나 Entry Spot destroy를 자동으로 실행하지 않는다. |
| stale unbind 차단 | 이전 stream의 늦은 unbind가 새 binding token을 가진 actor-session binding을 지우지 못한다. |
| stale session binding 차단 | target session server가 binding token이 맞지 않는 bound-session message를 거부한다. |
| reconnect binding 교체 | 같은 actor가 다른 session server에 연결하면 새 actor-session binding이 사용된다. |
| direct node 지정 차단 | `session.Actors.BindAsync(...)`는 target node `RoutingId`를 받지 않는다. |

### 19.4 metadata, codec, timeout, error

| 테스트 | 확인 내용 |
|--------|-----------|
| metadata 기본 deny | 별도 forwarding 설정이 없으면 application metadata가 actor context로 전달되지 않는다. |
| metadata propagation | 허용된 application metadata는 actor context까지 전달되고 raw request sequence는 노출되지 않는다. |
| metadata raw header 비노출 | actor handler context가 stream session id, peer endpoint, source session node rid, native handle을 노출하지 않는다. |
| codec registry 사용 | sample serializer 없이 framework codec으로 payload가 encode/decode된다. |
| codec failure | payload decode 실패는 `PayloadDecodeFailed`로 전달되고, encode 실패는 전송 전 호출 오류로 끝난다. |
| bound session request 부재 | bound session public contract에 client request/reply 호출이 없다. |
| actor dispatch timeout | actor dispatch request timeout이 pending request를 정리하고 `RequestFailed`와 `actor-dispatch-timeout` detail로 실패한다. |
| route not found error | play route가 없거나 actor-session binding이 없으면 transport send를 시도하지 않고 명확한 error로 실패한다. |
| unauthenticated dispatch 차단 | session callback이 인증 전 domain packet을 play server로 보내지 않는다. |

### 19.5 location metadata sample

| 테스트 | 확인 내용 |
|--------|-----------|
| location service/routed channel | 수동 연결 없이 location store 로 service request와 routed request가 통과한다. |
| location/manual 혼합 실패 | 같은 역할에서 두 방식을 섞으면 startup에서 실패한다. |
| location failure를 retry로 숨기지 않음 | sample과 helper가 retry loop나 warmup sleep 없이 실패를 드러낸다. |
| location metadata resolve sample | location metadata sample이 framework actor/spot resolver로 route를 읽는다. |
| location resolver 연결 | location store를 등록하면 framework actor/spot resolver가 같은 store를 사용하고, store 없이 위치 조회가 필요한 구성은 startup에서 실패한다. |

sample 검증도 필요하다.

- 기존 `TicTacToe` sample은 그대로 유지한다.
- session actor dispatch sample은 별도 project로 유지한다.
- session actor dispatch sample은 fake transport를 사용하지 않는다.
- session actor dispatch sample은 service channel과 routed channel 모두 수동 연결을
  사용하지 않는다. server bind endpoint는 provider 등록용이고, client 연결은
  location 기반 자동 연결로만 표현한다.
- session actor dispatch sample은 retry나 route warmup sleep을 사용하지 않는다.
- session actor dispatch sample은 direct target send/request API를 사용하지 않는다.
  session -> actor 방향은 actor create/dispatch helper를 쓰고, actor -> client 방향은
  현재 actor context의 bound session을 사용한다.
- 실행 결과는 두 actor 인증, match 생성, 재연결, join, move, 승패 판정을 모두
  보여야 한다.

## 20. 구현 적합성 요구

구현은 typed handler registry, location resolver, actor-session binding token 검증,
`ZLinkMessageMetadata` snapshot과 bound-session call을 이 문서의 계약대로 제공해야 한다.
raw relay registration과 typed dispatch를 같은 routed channel에서 동시에 활성화해서는 안
된다. public sample과 guide에는 direct target send/request, packet-name switch,
serializer helper 또는 route warmup sleep이 없어야 한다.

## 21. 적합성 기준

구현과 공개 sample은 아래 기준을 만족해야 한다.

- session server sample code가 framework helper 없이 raw relay envelope를 직접
  조립하지 않는다.
- play server sample code가 actor relay envelope를 직접 다루지 않는다.
- play server sample code가 `packetName` switch를 갖지 않는다.
- game service와 scenario code가 `RoutingId`를 직접 다루지 않는다.
- spot sample code가 `SpotNodeId`나 node `RoutingId`를 직접 다루지 않는다.
- actor-session binding 갱신은 local actor handle와 disconnect 흐름에서 runtime
  내부 상태로만 수행된다.
- stale disconnect는 binding token 조건으로 새 actor-session binding을 지우지 못한다.
- sample serializer helper가 없다.
- routed location 연결은 정식 location runtime 선언으로만 표현된다.
- 실패를 감추는 retry helper나 warmup sleep이 없다.
- request/reply matching은 request sequence 기준으로 검증된다.
- direct target send/request API는 public sample과 guide에 나오지 않는다.
- 두 방향을 합친 gateway나 전역 client 전송 객체는 새 public sample과 guide에 나오지
  않는다.
- 제거된 low-level gateway public API를 전제로 한 integration test는 bound session과
  session actor helper 기준으로 다시 작성한다.

## 22. 모듈 책임 경계

public surface와 구현의 책임 경계는 다음 요구를 만족해야 한다.

| 경계 항목 | 요구 |
|-----------|------|
| `RoutingId` 노출 | send/request public API와 session actor helper에서 제거하고 resolver 구현체와 internal transport helper 안에 가둔다. |
| session 위치 stale state | public session route API를 두지 않고 framework/core actor-session binding을 `BindingToken`으로 조건부 갱신한다. |
| 얕은 raw actor dispatch handler | raw relay handler 대신 actor/node/spot 실행 문맥의 typed handler를 기본 표면으로 둔다. |
| packet name switch | 실행 문맥별 handler registry와 message type metadata로 분리한다. |
| raw relay envelope 누출 | request sequence와 session-local metadata는 dispatch/relay helper 안에 둔다. |
| store 결합 과잉 | framework 본체는 특정 store 제품에 의존하지 않는다. 공식 Redis extension도 별도 package로 등록해서 쓴다. |
| direct target API 혼란 | 정식 sample과 guide에서는 send/request와 session actor helper 모두 resolver 또는 local actor id/type 기반 API로 보여 준다. |
| client 전송 이름 혼란 | actor/play 코드는 현재 actor의 bound session만 사용한다. |
| remote node direct handle sample | session actor dispatch sample은 local `SpotNode` actor runtime handle만 보여 주고 remote node direct handle을 보여 주지 않는다. |

호출자 코드는 다음 조건을 만족해야 한다.

- session server code가 auth, actor id/type 선택, local actor handle, dispatch 선택만
  가진다.
- actor/node/spot handler code가 `RoutingId`, raw envelope, packet switch를 보지 않는다.
- actor가 client로 send할 때 session route 정보를 직접 보관하지 않는다.
- actor가 client로 send할 때 현재 actor context의 bound session만 사용한다.
- 사용자 store 구현 sample은 store 계약 구현 예일 뿐 framework 기본값처럼 보이지
  않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: STREAM 서버 세션](30-stream-session.ko.md) | [다음: Stream Connector](32-stream-connector.ko.md)
<!-- framework-adapter-nav:bottom:end -->
