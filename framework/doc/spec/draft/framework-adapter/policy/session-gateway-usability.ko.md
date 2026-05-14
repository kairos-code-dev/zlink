<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework Actor Model](actor-model.ko.md) | [다음: Session Gateway 초안 보관본](session-gateway.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../README.ko.md)

[초안 묶음](./README.ko.md) | [Actor 모델](./actor-model.ko.md) | [Session Gateway 보관본](./session-gateway.ko.md) | [framework API](./framework-api.ko.md) | [.NET Session Actor Dispatch](../bindings/dotnet/session-actor-dispatch.ko.md)

# Draft -- Session Actor Dispatch Usability (Policy)

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, 이미 구현된 session actor dispatch와 actor relay 표면을
> session actor dispatch 모델로 정리하기 위한 개선 방향을 정리한다.
> API 이름, handler 모양, error 형식은 구현과 테스트가 확정되기 전까지 바뀔 수
> 있다.
>
> 이 문서의 범위는 **cross-binding 정책** -- 의미, 계약, 실패 의미, 테스트 항목,
> POSD 결론, error kind 매트릭스, 회귀 테스트 목록이다. 구체 .NET 시그니처와 등록
> 코드, tic-tac-toe sample은
> [bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
> 를 본다.
>
> actor 자체의 라이프사이클 (Entry Spot 머무름, session bind, user Spot join 등)
> 과 framework 자동 처리 vs application 로직 분담은
> [actor-model.ko.md](./actor-model.ko.md)에서 별도로 정의한다. 본 문서는 그 actor
> 모델의 **session actor dispatch use case**에 한정해 사용성 결정을 다룬다.
>
> Actor 위치 생명주기 개정에 따라 session actor dispatch 정책은 remote actor를
> 직접 만들지 않는다. actor는 기본적으로 local `SpotNode` actor runtime에서 생성되며,
> session actor helper도 remote node를 직접 지정하는 API를 제공하지 않는다.
> user Spot join은 session binding을 요구하지 않고, discovery active route는 join /
> leave 성공 시점에 갱신된다.

## 1. 목적

현재 session actor dispatch 기능은 session server와 play server를 분리하는 구조를 실제로
표현할 수 있다. 별도 `TicTacToe(session-gateway)` sample도 실제 TCP stream
connector, routed channel, discovery, session actor dispatch, actor relay를 함께 사용해서
동작한다.

다만 sample을 작성하면서 framework 사용자가 알아야 하는 개념이 많다는 문제가
드러났다. 사용자는 틱택토 같은 domain 흐름을 설명하고 싶지만, 실제 코드는
`RoutingId`, raw actor dispatch handler, stream header, packet name, actor/session 위치 저장소,
discovery 설정을 함께 다뤄야 한다.

이 문서의 목적은 정식 application public API를 session에서 actor를 생성하고 dispatch하는
표면과 actor에서 client session으로 보내는 `SessionProxy` 표면으로 다시 나누는 것이다.
이번 변경은 compatibility layer를 두지 않는 breaking change로 진행한다. 일반
application은 더 적은 코드로 같은 구조를 표현할 수 있어야 한다.

## 2. 현재 샘플에서 드러난 문제

### 2.1 개념 노출이 많다

session actor dispatch sample의 핵심 흐름은 아래 네 가지다.

1. client가 session server에 연결하고 인증한다.
2. session server가 domain 정책에 따라 local `SpotNode` actor runtime의 actor를 만들거나
   actor handle을 준비한다.
3. client request가 actor handle을 통해 actor runtime으로 dispatch된다.
4. actor는 `SessionProxy`를 통해 client에게 notify나 request를 보낸다.

하지만 사용자는 이 흐름을 작성하기 위해 아래 개념을 동시에 알아야 한다.

| 개념 | 현재 사용자가 알아야 하는 이유 |
|------|-------------------------------|
| `routerChannelId` | session server와 play server가 공유하는 routed mesh를 고르기 위해 필요하다. |
| `RoutingId` | 실제 session node나 play node를 직접 지정해야 한다. |
| `actorId` | 논리 사용자이자 relay domain key다. |
| `packetName` | raw actor dispatch handler 안에서 어떤 domain 동작인지 구분하기 위해 필요하다. |
| stream header | client request sequence를 보존하려면 raw dispatch 경로를 이해해야 한다. |
| raw actor dispatch handler | session server에서 넘어온 actor relay envelope를 play server에서 풀어야 한다. |
| 위치 저장소 | actor가 있는 play node와 session node를 application이 직접 찾아야 한다. |
| discovery | routed channel 연결을 자동으로 붙이려면 registry와 discovery 설정을 알아야 한다. |

이 목록은 기능 자체가 잘못되었다는 뜻이 아니다. 낮은 수준 표면으로는 필요한
정보다. 문제는 간단한 sample도 이 정보 대부분을 직접 노출한다는 점이다.

### 2.2 `RoutingId`가 domain 코드에 새어 나온다

`RoutingId`는 zlink routed transport의 목적지다. 반면 game code가 자연스럽게
다루는 값은 `gameId`, `matchId`, `actorId` 같은 domain key다.

현재 sample은 match 생성, join, move 흐름에서 play node 또는 session node
`RoutingId`를 직접 찾고 전달한다. 이 때문에 domain service가 "어느 game을 찾을지"
뿐 아니라 "어느 zlink node로 보낼지"까지 알아야 한다.

POSD 기준으로 보면 transport 위치 결정이라는 설계 결정이 여러 파일에 흩어져 있다.
이는 정보 은닉이 부족하다는 신호다.

### 2.3 raw actor dispatch handler가 packet switch를 가진다

현재 play server raw actor dispatch handler는 `packetName`을 보고 create, join, move 같은
동작으로 나눈다. 이 방식은 처음에는 단순해 보이지만, message 종류가 늘면 하나의
handler가 작은 router가 된다.

문제는 아래와 같다.

- handler가 framework relay envelope와 game command dispatch를 동시에 안다.
- 새 message를 추가할 때 packet name 문자열과 body decode 대상이 함께 늘어난다.
- packet name 오타는 compile 단계에서 잡히지 않는다.
- handler가 깊은 domain 모듈이 아니라 얕은 분기 모듈이 된다.

### 2.4 session relay code가 너무 많은 세부 작업을 안다

session server 쪽 relay code는 인증, actor 생성, api lookup, play relay,
client reply 보존을 함께 다룬다. 특히 request를 relay할 때는 원본 request
sequence가 유지되어야 하므로 framework Header packet dispatch 경로를 알아야 한다.

actor를 언제 만들지, 어떤 actor type을 쓸지, 현재 session에 어떤 actor handle을
binding할지는 application 정책이다. 반면 request sequence 보존, relay envelope 조립,
reply matching은 framework helper 안에 있을 때 더 자연스럽다. application은 actor
생성 위치와 dispatch 결정은 직접 내리되, transport 세부 작업을 반복해서 작성하지
않아야 한다.

### 2.5 discovery 설정은 기능보다 배선이 더 많이 보인다

기존 `TicTacToe(session-gateway)` sample은 자동 연결을 보여 주기 위해 embedded registry와
discovery를 사용한다. 이 결정은 맞다. 이 sample이 수동 연결을 쓰면 실제 서비스에서
기대하는 topology와 달라지기 때문이다.

하지만 application code가 registry endpoint, discovery attach 시점, routed channel
discovery 사용 여부를 너무 넓게 알아야 하면 sample의 초점이 흐려진다. discovery는
"이 host가 같은 mesh에 참여한다"는 선언에 가까워야 한다.

## 3. 설계 목표

상위 표면은 아래 목표를 만족해야 한다.

- domain code는 `actorId`, `gameId`, request/reply type을 중심으로 작성한다.
- `RoutingId`는 위치 해석 모듈과 framework 내부에 가둔다.
- session dispatch는 application이 명시적으로 작성하되, raw relay envelope와 request
  sequence 보존은 framework helper가 맡는다.
- typed handler 등록을 제공해서 `packetName` switch를 줄인다.
- codec은 framework의 codec registry를 사용한다.
- discovery는 수동 연결과 섞이지 않으며, 자동 연결 실패를 retry로 숨기지 않는다.
- 일반 send/request public API와 session actor helper는 `RoutingId`, `SpotNodeId` 같은
  transport 위치값을 직접 요구하지 않는다.
- actor와 현재 stream session의 묶음은 사용자가 `CreateAndBindActorAsync(...)` 또는
  `BindActorHandleAsync(...)`를 호출할 때 framework/core 내부 binding으로 갱신한다.
- 기존 direct target send/request API와 session gateway naming은 새 public surface에서
  제거한다.

이 목표의 핵심은 "더 많은 helper를 추가한다"가 아니다. 사용자가 알아야 하는
결정의 수를 줄이는 것이다.

## 4. 비목표

이 초안은 아래 기능을 정의하지 않는다.

- actor migration 정책
- game room placement 알고리즘
- 전역 location registry 제품화
- registry host의 배포 형태
- client protocol 자체의 정식 packet set
- retry policy 또는 connection warmup helper
- in-memory fake transport
- registry 기반 route resolver 기본 구현

session actor dispatch는 전달 경로를 제공한다. actor가 어느 play node에 있어야 하는지,
actor가 이미 존재할 때 create 요청을 어떻게 처리할지, 재연결 시 어느 정책으로 위치를
갱신할지는 application 또는 별도 상위 runtime의 책임으로 남긴다.

## 5. POSD 기준 문제 정리

| red flag | 현재 모습 | 위반되는 원칙 | 개선 방향 |
|----------|-----------|---------------|-----------|
| 얕은 raw actor dispatch handler | envelope를 받고 packet name으로 분기한 뒤 domain service를 호출한다. | 깊은 모듈 | actor/node/spot 실행 문맥의 typed handler registry가 dispatch와 decode를 맡는다. |
| 정보 누출 | `RoutingId`가 game scenario와 proxy code에 퍼진다. | 정보 은닉 | actor/game route resolver가 transport 위치를 숨긴다. |
| 특수/범용 코드 혼합 | sample session code가 auth, lookup, relay, stream reply를 함께 가진다. | 복잡성을 아래로 | actor create/dispatch helper가 request sequence 보존과 relay envelope 조립만 맡는다. |
| 시간적 분해 | "먼저 resolve, 그 다음 relay" 순서가 호출자 코드에 반복된다. | 오류를 정의로 제거 | framework call builder가 resolve 후 request를 하나의 의미로 제공한다. |
| 문자열 dispatch | packet name 문자열과 type decode가 별도로 맞아야 한다. | 오류를 정의로 제거 | type 기반 packet metadata와 handler 등록을 사용한다. |

## 6. 대안 검토

### 6.1 대안 A: sample helper만 둔다

첫 번째 방법은 framework API를 바꾸지 않고 sample 안에 helper class를 더 두는
방식이다. 예를 들어 `GameLocationStore`, `PlayActorRelay`,
`SessionRelaySession` 같은 sample class를 더 정돈해서 사용자가 복사할 수 있게 한다.

장점은 구현 범위가 작다는 점이다. 기존 API를 유지하고, framework runtime을 손대지
않아도 된다.

하지만 이 방식은 핵심 복잡성을 sample 밖으로 없애지 못한다. 다른 사용자가 같은
구조를 만들면 packet switch, route lookup, raw dispatch 보존 코드를 다시 작성해야
한다. 복잡성이 framework 아래로 내려가지 않고 application마다 반복된다.

### 6.2 대안 B: actor create/dispatch helper를 추가한다

두 번째 방법은 기존 낮은 수준 API 위에 typed handler, route resolver, actor
create/dispatch helper를 추가하는 방식이다.

이 방식에서는 낮은 수준 public API를 목표 표면으로 교체한다. 일반 application은
actor/node/spot 실행 문맥에 domain message handler를 등록하고, session callback에서는
인증, local actor handle 생성, dispatch 여부를 명시적으로 고른다. framework는 route resolve,
envelope 조립, request sequence 보존처럼 반복되면 위험한 작업만 맡는다.

이 초안은 대안 B를 선택한다. 같은 기능을 여러 application이 반복해서 작성할 가능성이
높고, request sequence 보존과 codec 처리처럼 실수 비용이 큰 부분은 framework가
흡수하는 편이 맞기 때문이다. 반대로 actor 생성 위치, actor type, 이미 존재하는 actor를
재사용할지 실패할지는 domain 정책이므로 framework builder가 자동으로 정하지 않는다.

## 7. 제안 표면 개요

상위 표면은 세 축으로 나눈다.

| 축 | 역할 |
|----|------|
| actor/node/spot handlers | relayed message를 실행 문맥 안에서 typed message로 처리한다. |
| actor/spot route resolvers | actor와 spot domain key를 transport target으로 해석한다. |
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
| typed body, domain reply                                   |
+------------------------------------------------------------+
```

다이어그램 안의 `Session Dispatch`는 application이 작성하는 session callback이다.
framework helper는 이 callback 안에서 선택된 dispatch의 transport 세부 작업만 숨긴다.

## 8. Actor/Node/Spot Handler Dispatch

### 8.1 현재 낮은 수준 actor relay 표면

이전 초안은 session에서 actor로 넘어온 relay envelope를 play side에서 직접 처리하는
raw handler를 public surface처럼 두었다. 기존 구현 이름에 `SessionProxy`가 들어가지만,
이것은 actor가 client session으로 보내는 현재 `SessionProxy`가 아니다. 새 public
surface에서는 이 raw handler 이름을 유지하지 않는다.

이 표면은 framework internal envelope를 직접 다룰 수 있다는 장점이 있다. 하지만
일반 handler에는 너무 넓다. typed handler가 필요한 사용자는 대부분 아래 세 가지를
원한다.

- actor id를 알고 싶다.
- typed request body를 받고 싶다.
- typed reply를 반환하고 싶다.

### 8.2 제안 handler 모양

typed handler는 `SessionProxy`나 routed channel에 붙지 않는다. handler는 actor, node,
spot 같은 실행 문맥에 등록된다. session은 일부 packet을 local 처리하고, 나머지를
actor/node/spot 실행 문맥으로 relay한다. typed handler가 받는 값은 아래로 제한한다.

- typed request body 한 개
- actor handler 변종이면 actor 인스턴스 한 개
- handler context (actor id, router channel id, metadata snapshot, `SessionProxy`,
  필요하면 deadline)
- `CancellationToken`

typed actor context는 source session의 `RoutingId`를 노출하지 않는다. handler가
즉시 client에게 push를 보내야 할 때도 resolver 기반 `SessionProxy.Send(actorId, ...)`
표면을 사용한다. 이렇게 해야 "방금 packet을 보낸 session node"와 "현재 actor가
붙어 있는 session node"를 혼동하지 않는다.

구체 .NET 시그니처는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§3을 참고한다.

### 8.2.1 SessionProxy naming

actor/play 코드에서 client session으로 메시지를 보낼 때 사용하는 public 객체 이름은
`SessionProxy`로 둔다. 이 객체는 network gateway 자체가 아니라, 현재 actor가 연결된
client session을 대신 다루는 proxy다.

`SessionGateway`라는 이름은 topology나 boundary server를 떠올리게 하므로 public
application 표면에는 쓰지 않는다.

### 8.3 Header Metadata 전달

session actor dispatch runtime은 stream header를 그대로 play handler에 넘기지 않는다. raw header에는
session server 내부에서만 의미가 있는 값과 actor handler까지 전달해도 되는 값이
섞여 있기 때문이다.

metadata는 아래처럼 나누어 처리한다.

| 종류 | 예 | 전달 범위 |
|------|----|-----------|
| session-local metadata | stream session id, peer endpoint, raw request sequence | session actor dispatch runtime 내부에서만 사용 |
| framework routing metadata | actor id, router channel id | actor handler context로 전달 |
| request lifecycle metadata | deadline, cancellation, request kind | actor handler context로 전달 |
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
  과 codec metadata를 따로 들고 있는다.
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
message kind이고, route resolver 입력은 `actorId` 또는 `spotId`로 제한한다.

구체 .NET 시그니처(`ZLinkMessageMetadata`, `IZLinkMessageMetadataPolicy`,
`options.ConfigureMetadata(...)`)는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§3.2를 참고한다.

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
host 설정은 actor factory와 routed channel capability만 등록하고, packet handler는
actor 객체가 자기 lifecycle 진입 단계 (binding별 이름은 다르지만 의미상 `Configure()`)
에서 등록한다. spot handler도 같은 방식으로 spot 객체 안에서 등록한다.

node 또는 spot 실행 문맥에서 처리해야 하는 메시지도 같은 원칙을 따른다. node handler는
node 등록 표면에, spot handler는 spot 등록 표면에 둔다. `SessionProxy`는 handler
등록 표면을 갖지 않는다.

`packetName`은 기본적으로 message type metadata에서 얻는다. override가 필요하면
객체의 실행 문맥 안에서 명시한다. 동일한 실행 문맥 안에서 같은 `kind + packetName`에
handler가 둘 이상 등록되면 startup에서 실패해야 한다. body type은 dispatch key가
아니라 decode 대상이다.

resolver는 transport builder가 아니라 framework service 설정에 등록한다. transport
mesh를 고르는 일과 application 위치 정책을 등록하는 일을 같은 builder에 넣지 않기
위해서다. 공개 resolver 축은 actor와 spot 두 개로 제한한다.

구체 .NET 시그니처 (`options.AddActorFactory<...>(...)`,
`options.AddRoutedChannel(...)`, `options.AddSpotNode(...)`, resolver 등록 코드,
actor / spot 객체 등록 sample)는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§7을 참고한다.

startup validation은 아래 조건을 확인한다.

| 조건 | 실패 이유 |
|------|-----------|
| `IZLinkSessionProxy`를 사용하지만 현재 actor-session binding이 없다. | actor가 어떤 client stream으로 보내야 하는지 확인할 수 없다. |
| actor client를 사용하지만 actor route resolver가 없다. | actor id를 actor node 위치로 해석할 수 없다. |
| 같은 actor type에 factory가 둘 이상 등록된다. | actor create target이 모호하다. |
| 같은 실행 문맥에 같은 packet handler가 둘 이상 등록된다. | dispatch target이 모호하다. |
| raw relay handler와 typed dispatch가 같은 routed channel에 함께 등록된다. | envelope 처리 주체가 두 개가 된다. |

### 8.5 낮은 수준 handler와의 관계

typed actor/node/spot handler는 기존 raw actor dispatch handler를 대체하는 기본 표면이다.
`AddSessionProxyHandler<THandler>()` 같은 이름은 새 public API에서 제거한다.

raw actor dispatch handler는 정식 application public API로 남기지 않는다. custom envelope나
protocol adapter가 필요하면 framework internal service 또는 별도 adapter package에서
resolved transport helper를 사용한다. 같은 relay path에서 raw actor dispatch handler와
typed actor/node/spot dispatch를 동시에 선택하는 public 설정은 만들지 않는다.

## 9. Actor / Spot Route Resolvers

### 9.1 필요성

사용자 code에서 `RoutingId`가 반복되는 이유는 framework가 domain key를 transport
target으로 바꾸는 경계를 제공하지 않기 때문이다.

session server가 client packet을 play server로 보내려면 `actorId` 또는 request body의
domain key에서 actor node `RoutingId`를 찾아야 한다. actor가 user Spot에 들어갈 때는
`spotName` 또는 `spotId`에서 target Spot 위치를 찾아야 한다.

이 해석은 application 정책이지만, resolver 입력은 좁게 유지해야 한다. resolver가
metadata나 raw message를 받으면 transport 위치 조회가 작은 dispatcher로 변한다.

### 9.2 제안 interface

route resolver는 application이 구현하고 framework가 호출하는 위치 조회 interface다.
framework는 resolver가 어떤 저장소를 쓰는지 알지 않는다. 공개 resolver 축은 두 개로
제한한다.

- **actor route resolver** -- `actorId` → "이 actor가 사는 actor/play node와 routed channel".
  routing result에는 `RouterChannelId`와 target node 식별값이 들어간다.
- **spot route resolver** -- `spotName` 또는 `spotId` → "이 user Spot이 있는 node와
  spot id". actor의 `JoinSpot(...)` 같은 표면에서 transport 위치값을 숨긴다.

actor resolver 입력은 `actorId` 하나로 유지한다. spot resolver 입력은 spot domain key
하나로 유지한다. metadata, packet name, raw message, application body는 resolver에
넘기지 않는다. game 단위 배치가 필요하면 `gameId` 자체를 actor id 또는 spot id로
설계하거나, session의 domain placement code에서 actor/spot 대상을 먼저 결정한다.
resolver가 raw message 객체나 packet switch를 직접 보지 않게 해야 resolver가 작은
dispatcher가 되지 않는다.

session 위치는 공개 resolver로 두지 않는다. actor가 client에게 메시지를 보낼 때 필요한
session binding은 framework/core runtime 내부 상태다. `IZLinkSessionProxy` 같은 표면은
actor id나 현재 actor context를 받아 내부 binding을 사용해야 하며, application이
`actorId -> session node` resolver를 구현하게 하지 않는다.

resolver가 반환하는 target node 식별값(`RoutingId`류)은 사용자가 일반 handler에서
직접 다루는 값이 아니다. transport 위치값은 resolver 구현체와 framework routed
transport 내부에만 머물러야 한다.

actor route resolver는 actor id만 알고 actor runtime으로 message를 보내는 public
actor client(`IZLinkActorClient` 류)에서 사용한다. session callback이 이미
`CreateAndBindActorAsync(...)` 또는 `BindActorHandleAsync(...)`로 받은 actor ref를 들고
있으면 actor route resolver를 다시 호출하지 않는다. `DispatchToActorAsync(actorRef, ...)`
는 ref 안의 resolved target을 사용하므로 resolver cache나 route store를 다시
조회하지 않는다.

구체 .NET 시그니처(`IZLinkActorPlayRouteResolver`, `ZLinkActorRoute`,
`IZLinkActorClient` builder)는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§5, §6을 참고한다.

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
| spot route 없음 | spot 이름/id에 대응하는 user Spot을 찾지 못했다. |
| actor-session binding 없음 | actor가 현재 어떤 stream session에도 binding되어 있지 않다. |
| resolver timeout | route 결정 자체가 제한 시간 안에 끝나지 않았다. |
| resolver exception | application route store 조회가 실패했다. |

이 실패들은 retry로 숨기지 않는다. 특히 discovery 기반 자동 연결이 아직 되지 않은
상황과 route store에 값이 없는 상황은 원인이 다르다. sample과 framework helper는
임의 sleep이나 retry loop로 이를 덮지 않아야 한다.

### 9.5 위치 소유와 갱신 책임

actor 위치 정보는 두 층으로 나눈다.

| 계층 | 소유자 | 내용 |
|------|--------|------|
| actor-session binding | framework/core runtime | 현재 actor가 어느 stream/session에 붙어 있는지 관리한다. |
| actor/spot route resolution | application resolver | `actorId`, `spotName`, `gameId` 같은 domain key가 어느 node/spot으로 가야 하는지 결정한다. |

framework는 actor-session binding을 내부 상태로 가진다. 인증이 끝나면 현재 stream에
actor를 binding하고, 같은 actor가 다시 붙으면 이전 binding을 교체할 수 있어야 한다.
disconnect나 session close는 현재 binding token이 맞을 때만 해제되어야 한다. 이 규칙은
runtime 내부 상태 갱신 규칙이지 application이 구현하는 resolver 계약이 아니다.

전역 actor/spot 위치 정책은 resolver 구현체가 가진다. 여기에는 아래 결정이 포함된다.

- 위치 저장소가 memory, registry, DB, Redis 중 무엇인지
- actor route와 spot route의 TTL
- cache invalidation 조건
- game room이나 actor placement 정책

성능을 위해 위치 정보 cache가 필요하면 그 cache도 resolver 구현체가 가진다.
framework는 resolver 결과를 다시 cache하지 않는다. framework가 cache를 가지면 stale
route를 언제 버려야 하는지 알기 위해 application placement 정책을 알아야 하고, 이는
정보 은닉을 깨뜨린다.

stale route가 반환되면 framework는 반환된 route로 send/request를 시도하고, 실패를
명확한 error로 돌려준다. 자동으로 다른 route를 다시 찾거나 retry하지 않는다.
cache invalidation 후 재조회가 필요하면 resolver 또는 application service가 명시적으로
수행한다.

### 9.6 registry discovery metadata sample

framework는 registry 기반 actor/spot resolver를 기본 구현으로 제공하지 않는다.
registry를 기본 구현으로 넣으면 registry가 모든 application의 권위 있는 위치 저장소처럼
보이고, cache와 TTL 정책까지 framework 의미로 굳어진다. 대신 sample에서는 registry
discovery metadata를 이용해 actor/spot resolver를 구현하는 방식을 보여 줄 수 있다.

registry metadata는 resolver 입력 metadata가 아니다. resolver 입력은 여전히 `actorId`
또는 spot domain key 하나다. registry metadata는 resolver 구현체 내부에서 읽는
infrastructure state다. framework가 registry metadata store를 자동으로 선택하지 않는다는
정책은 binding에 관계 없이 동일하다.

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
   `CreateAndBindActorAsync(...)` 또는 `BindActorHandleAsync(...)`를 호출한다.
4. packet별로 actor dispatch, session-local reply, error reply 중 하나를 고른다.
5. request이면 원래 stream request sequence로 reply한다.

framework는 이 결정을 자동으로 하지 않는다. 대신 아래 작업을 깊은 helper로 제공한다.

| helper | framework가 숨기는 작업 |
|--------|------------------------|
| `CreateAndBindActorAsync(...)` | 현재 node의 actor runtime에 actor 생성 요청, 현재 actor-session binding 갱신 |
| `BindActorHandleAsync(...)` | local `SpotNode` actor runtime의 dispatch handle 생성, 현재 actor-session binding 갱신 |
| `DispatchToActorAsync(...)` | actor dispatch envelope와 원본 stream request sequence를 보존한 actor dispatch |

### 10.2 제안 session context API

session context는 handler registry를 갖지 않는다. 사용자는 기존 session callback 안에서
직접 분기한다. session context가 노출해야 하는 핵심 helper는 셋이다.

- `CreateAndBindActorAsync(actorId, actorType, ct)` -- 현재 session host의 actor runtime에
  create 요청을 보낸다. actor 생성과 session bind를 한 호출에 묶는다.
- `BindActorHandleAsync(actorId, actorType, ct)` -- 현재 session host의 local
  `SpotNode` actor runtime에서 dispatch handle을 만든다. remote node를 직접 지정하지 않는다.
- `DispatchToActorAsync(actorRef, packet, ct)` -- actor dispatch envelope와 원본
  stream request sequence를 보존한 actor dispatch.

`IZLinkActorRef` (binding별 동등 이름)는 local `SpotNode` actor runtime의 actor에 대한
dispatch handle만 노출한다. application actor 객체(`IZLinkActor`) 자체와는 다른 표면이다.
actor ref는 application actor 객체가 아니므로 `Configure()`나 handler registry를 갖지 않는다.

구체 .NET 시그니처(`IZLinkSessionContext`, `IZLinkActorRef`)는
[handler-interfaces.ko.md §4.4](../bindings/dotnet/handler-interfaces.ko.md)와
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§7-8을 참고한다.

`CreateAndBindActorAsync(...)`는 현재 session host가 가진 actor runtime에 create 요청을 보낸다.
이 함수는 actor 생성과 현재 session binding metadata 생성을 하나의 작업으로 묶어,
후속 dispatch가 항상 같은 actor ref 표면을 사용하게 한다.

`BindActorHandleAsync(...)`도 remote node 선택을 받지 않는다. actor는 local `SpotNode`
actor runtime에서만 생성되므로, 이 helper는 actor id와 actor type으로 local actor handle을
준비하고 현재 session binding metadata를 만든다. 이미 존재하는 actor를 재사용할지,
중복 생성으로 실패할지는 local actor runtime과 factory 정책을 따른다.

`CreateAndBindActorAsync(...)`와 `BindActorHandleAsync(...)`는 같은 후속 dispatch 표면을
반환하지만 같은 종류의 create 요청은 아니다. `CreateAndBindActorAsync(...)`만 현재 node의 actor
runtime에 명시 create를 요청한다. `BindActorHandleAsync(...)`는 local actor runtime의
handle을 준비하고 현재 actor-session binding을 갱신한다. framework session helper는
remote node actor 생성 정책을 만들지 않는다.

명시 create와 handle 준비는 이후 같은 `IZLinkActorRef` 표면으로 dispatch된다. 사용자가
packet마다 `RoutingId`를 넘기지 않게 하고, actor 생성과 session binding을 local
`SpotNode` actor runtime 안에 가두는 것이 이 표면의 목적이다.

`IZLinkActor`와 `IZLinkActorRef`는 의도적으로 분리한다. `IZLinkActor`는 actor node에서
실제로 생성되는 application 객체이고, constructor로 받은 `IZLinkActorContext` 안에서
handler를 등록한다. `IZLinkActorRef`는 session이 actor로 dispatch하기 위해 들고 있는
handle이다. actor ref는 application actor 객체가 아니므로 `Configure()`나 handler registry를
갖지 않는다.

### 10.2.1 actor create lifecycle

`CreateAndBindActorAsync(...)`와 `BindActorHandleAsync(...)`는 session-bound actor 경로에서
actor ref와 session binding metadata를 한 호출 안에서 함께 준비하는 helper다.
`CreateAndBindActorAsync(...)`는 local actor 생성까지 묶고, `BindActorHandleAsync(...)`는
local actor handle 준비와 현재 actor-session binding 갱신만 묶는다. unbound standalone actor는 이
표면으로 만들지 않으며, actor node 측 별도 등록 표면(예: actor node가 직접 부르는
`CreateActor` 계열 helper)으로 다룬다.

1. framework가 현재 session에 대한 새 `BindingToken`을 만든다.
2. `CreateAndBindActorAsync(...)`이면 현재 node의 actor runtime에 create 요청을 보내고,
   `BindActorHandleAsync(...)`이면 local actor runtime에서 `IZLinkActorRef`를 준비한다.
3. local create가 actor를 만들거나 기존 actor를 반환하면 같은 `IZLinkActorRef` 표면으로
   감싼다.
4. current session server의 local binding table에
   `actorId -> sessionId + bindingToken`을 기록한다.
5. framework/core runtime의 actor-session binding 상태를 갱신한다.
6. binding 갱신이 끝나면 `IZLinkActorRef`를 caller에게 반환한다.

binding 갱신이 실패하면 create helper는 실패해야 한다. 이때 framework는 current session
server의 local binding table에서 같은 `bindingToken`을 가진 entry를 제거한다.
`CreateAndBindActorAsync(...)`로 local actor가 이미 만들어졌다면 그 actor lifetime은 actor
runtime 정책을 따른다. session binding 실패는 client-facing route 실패이기 때문이다.

`BindActorHandleAsync(...)`는 target node 선택을 하지 않고 target node id를 받지도
않는다. 다른 node의 actor로 보내야 하는 흐름은 session actor helper가 아니라 actor
route resolver와 `IZLinkActorClient` 같은 actor 호출 표면으로 다룬다.

session close 또는 stream disconnect가 발생하면 framework는 current session server가
가진 actor binding마다 아래 순서로 정리한다.

1. local binding table에서 같은 `sessionId + bindingToken`인 entry만 제거한다.
2. framework/core runtime의 actor-session binding 상태에서 같은 token을 가진 entry만
   제거한다.

disconnect 정리는 best-effort다. 같은 actor가 이미 새 stream에 붙었다면 이전 stream의
늦은 정리가 새 binding token을 지우면 안 된다. stale binding이 남으면 `SessionProxy`
send/request가 binding token 검증에 실패하고 명확한 error를 돌려준다.

### 10.3 직접 dispatch 흐름 (정책)

session callback 안에서 정책 code는 아래 흐름을 따른다 (framework가 builder로 자동
제공하는 것이 아니라, application이 직접 분기한다).

1. stream packet을 받는다.
2. 인증 packet이면 application이 actor id와 actor type을 결정한 뒤
   `CreateAndBindActorAsync(...)` 또는 `BindActorHandleAsync(...)`를 호출한다. helper가
   local actor 생성 또는 local handle 준비, session binding metadata 생성, 내부 binding 갱신을
   한 묶음으로 처리한다.
3. 인증된 actor가 있는 packet은 `DispatchToActorAsync(actorRef, packet, ct)`로
   actor 실행 문맥으로 넘긴다. 인증되지 않은 packet은 application이 명시 error로
   거부한다.
4. session-local reply가 필요한 경우 session context의 reply helper를 사용한다.
5. 같은 stream request sequence는 framework가 internal envelope에만 보존하므로,
   application code는 sequence를 직접 다루지 않는다.

구체 .NET 코드 예시(`TicTacToeSession.OnDispatchAsync(...)`,
`Context.BindActorHandleAsync(...)`, `Context.Reply(...).Submit(...)`,
`ZLinkFrameworkException` 던지기)는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§8을 참고한다.

framework가 맡는 규칙은 아래로 제한한다.

- 원본 stream request sequence를 보존한다.
- 원본 body bytes는 sample serializer가 아니라 framework codec/message 경로로
  다룬다.
- `DispatchToActorAsync(...)`는 raw stream frame relay 세부 처리를 숨긴다.
- reply matching은 message name이 아니라 request sequence 기준으로 유지한다.

### 10.4 Session과 SessionProxy의 공통 흐름

session과 session proxy는 방향이 다르다. session은 client packet을 actor 실행 문맥으로
보내는 ingress다. session proxy는 actor가 client session으로 message를 보내는
client-facing endpoint다.

| 표면 | 역할 | target 판단 |
|------|------|-------------|
| session ingress | auth, ping, reconnect 같은 session-local packet을 처리하거나 actor로 dispatch한다. | actor handle과 framework runtime 내부 |
| session proxy request | actor가 client session으로 send/request한다. | actor-session binding과 framework runtime 내부 |

따라서 session proxy는 message handler registry를 갖지 않는다. handler registry는
actor, node, spot 실행 문맥에만 존재한다.

## 11. SessionProxy 호출 표면

play server에서 client로 보내는 호출은 `SessionProxy`가 맡는다. 이 이름은 actor/play
코드가 remote client session을 대신 다룬다는 의미를 드러낸다.

제거 대상인 기존 표면은 session node target을 호출자가 직접 넘겼다. 이 방식은 actor
code가 session 위치를 알아야 하므로 reconnect 뒤 stale route를 만들기 쉽다.

정식 application 표면의 목표는 `SessionProxy`가 `actorId`나 현재 actor context만 받고
framework/core가 가진 actor-session binding으로 target을 찾는 것이다.
`SessionProxy.Request(actorId, request)`의
reply는 별도 reverse-stream을 새로 여는 것이 아니라, 이미 열려 있는 routed channel의
양방향 reply correlation 경로를 그대로 사용한다 (routed channel은 request/reply
matching을 stream 단위 sequence가 아니라 routed channel 단위 sequence로 잡는다).
caller가 `RoutingId`를 모르더라도 같은 routed mesh 위에서 request/reply가 닫힌다.

`SessionProxy.Send(...)` / `.Request(...)` 호출은 내부적으로 아래 순서로 처리된다.

1. message type에서 packet name을 얻는다.
2. framework/core runtime의 actor-session binding에서 `RouterChannelId`, session router
   id, `SessionId`, `BindingToken`을 얻는다. 분산 배포의 `.NET` adapter에서는 이 조회가
   `IZLinkActorSessionBindingStore.FindSessionAsync(...)` 구현을 통해 닫힐 수 있다.
3. 반환된 `RouterChannelId`와 session router id로 내부 routed transport call을 만든다.
4. routed transport payload는 multipart로 만든다. route header는 `parts[0]`에 두고,
   `ActorId`, `SessionId`, `BindingToken`, packet name, application metadata snapshot 같은
   session proxy metadata는 별도 metadata part에 둔다. application body는 별도 body
   part로 유지한다.
5. target session server가 metadata의 `SessionId`와 `BindingToken`을 현재 local
   binding과 비교한 뒤 client stream으로 전송한다.
6. request이면 routed channel reply correlation 경로로 reply를 기다린다.

호출자는 `RoutingId`를 모르지만, binding 조회 실패는 명확한 예외 또는 error result로
받아야 한다.

구체 .NET 시그니처(`IZLinkSessionProxy`, `IZLinkSessionProxySendCall`,
`IZLinkSessionProxyRequestCall`)와 사용 예시는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§4를 참고한다.

이 모델에서 actor 객체는 session 위치 정보를 직접 소유하지 않는다. actor 객체가
`SessionId`와 `SessionRouterId`를 들고 있으면 빠른 path처럼 보이지만, client가 다른
session server로 재접속할 때 stale 상태가 되기 쉽다. 권위 있는 위치 상태는
framework/core의 actor-session binding이 맡고, application public resolver로 분리하지
않는다. 외부 저장소가 필요하면 resolver를 추가하지 않고 binding store 구현으로
감춘다.

### 11.1 이름 변경 계획

`SessionGateway`라는 기존 이름은 새 public API에서 제거한다. 새 public 모델은 두
방향으로 나눈다. session에서 actor로 가는 방향은 actor create/dispatch다. actor에서
client session으로 가는 방향만 `SessionProxy`다.

| 제거할 이름 | 목표 표면 | 처리 |
|-------------|-----------|------|
| `IZLinkSessionGateway` | `IZLinkSessionProxy`와 session actor helper | 하나의 gateway 객체로 합치지 않는다. |
| `EnableSessionGateway()` | session actor helper와 session proxy service 등록 | 하나의 gateway feature switch로 묶지 않는다. |
| `SendToActor(...)` | `SessionProxy.Send(actorId, message)` | actor -> client 방향 호출로만 남긴다. |
| `RequestActor(...)` | `SessionProxy.Request(actorId, request)` | reply type은 기존 call builder 규칙을 따른다. |

alias는 두지 않는다. 정식 sample, guide, 새 draft 문서에서는 session -> actor 방향을
`Gateway`로 부르지 않는다.

## 12. Codec 정책

session actor dispatch sample은 별도 serializer helper를 두면 안 된다. framework에는 codec
등록과 message encode/decode 경로가 있으므로, sample이 다시 `JsonSerializer`를 직접
감싸면 두 가지 문제가 생긴다.

- 사용자는 framework codec을 써야 하는지 sample helper를 써야 하는지 헷갈린다.
- internal packet과 application packet의 serialization 정책이 분리되어 보인다.

상위 표면은 아래 규칙을 따른다.

- application message는 등록된 framework codec registry로 encode/decode한다.
- typed handler는 codec registry가 decode한 body를 받는다.
- internal session actor dispatch envelope와 session proxy envelope는 framework가 소유한다.
- internal envelope의 wire 형식은 public sample code가 알 필요 없다.
- 내부 routed transport에서는 envelope header와 body를 한 `Message`로 합치지 않는다.
  서버 간 경로는 공통 message model의 multipart 계약을 따른다.
- sample에는 `SampleJson` 같은 serializer wrapper를 두지 않는다.

internal metadata part가 JSON을 쓰는지, binary codec을 쓰는지는 framework 구현 선택이다.
하지만 metadata part와 body part를 하나로 합치는 것은 구현 선택이 아니다. 중요한
계약은 application handler가 framework codec registry와 typed message만 보고, 내부
server-to-server wire는 multipart 경계를 유지한다는 점이다.

## 13. Discovery 정책

session actor dispatch sample의 기본 연결 방식은 discovery여야 한다. session server와 play
server가 늘고 줄 수 있는 구조이므로, 수동 peer endpoint를 sample 중심에 두면 실제
사용 모델과 어긋난다.

상위 표면은 아래 정책을 유지한다.

- routed channel은 global discovery 설정이 있으면 discovery-attached router로
  참여한다.
- service channel client와 routed channel은 global discovery 설정이 있으면
  discovery 기반으로 peer를 찾는다.
- 같은 capability에서 discovery와 수동 연결을 섞으면 startup에서 실패한다.
- sample은 discovery 연결 실패를 retry loop나 sleep으로 숨기지 않는다.
- 바로 연결되지 않으면 registry/discovery/topology 상태를 먼저 확인할 수 있어야
  한다.

diagnostic helper는 retry helper와 다르다. diagnostic helper는 현재 registry view,
discovery member, local routed channel state를 보여 주는 역할만 한다. 구체 .NET
시그니처(`IZLinkTopologyDiagnostics`)는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§10을 참고한다.

이 표면은 session actor dispatch의 필수 API가 아니라 운영 점검 표면에 가깝다. 다만 sample
문제 분석에서 discovery 상태 확인이 중요했으므로, 향후 monitoring/registry 초안과
함께 검토할 필요가 있다.

## 14. SPOT direct target API 정책

actor route와 같은 문제가 `Spot` 호출에도 있다. 사용자가 `SpotId`만으로는 메시지를
보낼 수 없고 `SpotNodeId`나 node `RoutingId`까지 알아야 한다면, transport 위치 정보가
application 코드에 새어 나온다.

framework public surface는 spot name 또는 spot id 기반 호출이 node 경계를 넘을 때
spot route resolver를 사용한다. session-gateway sample에서 game room이 같은 Play
서버 안에만 있으면 resolver 호출이 드러나지 않을 수 있지만, 문서의 cross-node 계약은
spot resolver를 포함해야 한다. 멀티게임 sample의 game room은 도메인 핵심 실행 문맥이므로
Play 서버 안에서는 `IZLinkSpotManager`로 room SPOT을 만든다. client는 match id나 room
name을 지정하지 않는다. `MatchId`는 생성된 room의 `SpotId` hex이며, actor가 join한
room 안에서 `PlaceMarkReq`가 처리된다. registry metadata는 actor play route sample,
spot route sample, actor-session binding 보조 상태를 각각 분리해서 관리해야 한다.

`SpotId` 기반 public 호출에서 resolver 입력은 request 객체가 아니라
`ZLinkSpotId spotId` 하나로 제한해야 한다. metadata가 필요해 보인다면 resolver가 route
조회 이외의 정책을 함께 하려는 신호다. 그런 정책은 spot id를 정하는 caller나 domain
placement code에 둔다.

direct target API는 public application 표면에서 기본으로 노출하지 않는다.

```csharp
// 일반 application 표면으로 권장하지 않는다.
await spotClient
    .SendTo(spotNodeRid, spotId, message)
    .Submit(cancellationToken);
```

`SpotNodeId`나 `RoutingId`를 알아야만 호출할 수 있는 API가 public 문서와 sample에
함께 있으면 두 가지 사용법이 경쟁한다. 그러면 사용자는 resolver를 써야 하는지,
직접 target을 넘겨야 하는지 계속 판단해야 한다. framework의 사용성 목표가 transport
위치값 노출 제거라면, application public API는 `SpotId` 기반으로 통일해야 한다.

향후 구현 내부에는 resolved route를 받는 transport helper가 필요할 수 있다. 그 helper는
public application API가 아니라 runtime/internal service로 두며, 정식 public 계약에
들어가기 전에는 별도 draft에서 먼저 검토한다.

session proxy와 session actor helper도 같은 원칙을 따른다. `RoutingId`를 받는
`SendToActor(...)`류 send/request 표면은 제거한다. sample은 actor -> client 방향에서
resolver 기반 `SessionProxy` API만 사용하고, session -> actor binding은 local
`SpotNode` actor runtime의 actor id/type만 사용해야 한다.

## 15. Timeout과 retry 정책

상위 표면은 retry를 기본 제공하지 않는다.

session actor dispatch에서 실패 원인은 서로 다르다.

| 실패 | 원인 | framework 기본 동작 |
|------|------|--------------------|
| route 없음 | actor/game/spot 위치가 없다. | 즉시 실패 |
| discovery peer 없음 | routed mesh가 아직 연결되지 않았다. | send/request 실패 또는 timeout |
| send backpressure | socket이 지금 보낼 수 없다. | bounded pending queue와 `SendTimeout` 적용 |
| request reply 없음 | peer가 reply하지 않았다. | `RequestTimeout` 적용 |
| client session 없음 | actor의 현재 actor-session binding이 없다. | session proxy request 실패 |

retry를 넣으면 위 원인이 섞인다. sample에서는 특히 discovery 버그와 topology 설정
문제를 숨기므로 넣지 않는다.

`SendTimeout`은 core blocking send timeout이 아니라 framework async pending deadline
으로 사용한다. stream connector public option에는 `SendTimeout`을 두지 않고,
request/reply 대기는 `RequestTimeout`으로 다룬다.

## 16. Before / After

### 16.1 현재 방식

현재 방식은 명시적이고 강력하지만, 일반 sample에는 많은 배선이 보인다. 특히 play
side의 raw actor dispatch handler가 framework envelope와 packet switch를 직접 다룬다.
이 코드는 `SessionProxy`를 상속한 것이 아니다. 이름이 비슷해도 actor가 client로
보내는 `SessionProxy`와는 다른 제거 대상 raw relay handler다.

```csharp
public sealed class PlayActorRelayHandler : IZLinkActorRelayHandler
{
    public async ValueTask<Message?> HandleAsync(
        IZLinkActorRelayContext context,
        ZLinkActorRelayMessage message,
        CancellationToken cancellationToken)
    {
        string packetName = message.PacketName;

        if (packetName == "game.create")
        {
            CreateMatchReq req = message.Body.Decode<CreateMatchReq>();
            CreateMatchRes rep = await game.CreateAsync(
                context.ActorId,
                req,
                cancellationToken);
            return rep.ToMessage();
        }

        if (packetName == "game.move")
        {
            MoveReq req = message.Body.Decode<MoveReq>();
            MoveRep rep = await game.MoveAsync(
                context.ActorId,
                req,
                cancellationToken);
            return rep.ToMessage();
        }

        throw new InvalidOperationException($"Unknown packet: {packetName}");
    }
}
```

이 예시는 설명을 위해 축약한 코드다. 실제 구현에서는 codec extension과 error
처리도 더 들어간다. 문제는 handler가 actor domain logic과 relay envelope 해석을 함께
안다는 점이다.

### 16.2 제안 방식

제안 방식에서는 actor handler가 framework envelope와 packet switch를 몰라도 된다.
domain message handler는 actor 실행 문맥에만 등록된다.

제안 방식에서 코드 모양은 아래 흐름으로 정리한다.

- actor handler -- typed request body와 actor instance, `CancellationToken`만 받는다.
  raw envelope나 packet switch는 없다.
- actor 객체 -- `Configure()` 단계에서 자기에게 보낼 packet handler를 명시적으로
  등록한다.
- host 등록 -- transport(routed channel, stream node)와 actor factory만 등록한다.
- session callback -- 인증 packet에서 actor id와 actor type을 결정한 뒤
  `CreateAndBindActorAsync(...)` 또는 `BindActorHandleAsync(...)`를 호출하고, 이후 packet은
  `DispatchToActorAsync(actorRef, packet, ct)`로 넘긴다.

framework가 보장하는 부분은 local actor create 또는 handle 준비와 현재 session binding
metadata 갱신을 한 흐름으로 묶고, dispatch helper가 원본 request sequence와 codec 경로를
보존한다는 점이다.

구체 .NET 코드 (`JoinMatchHandler`, `PlaceMarkHandler`, `TicTacToeActor`,
`TicTacToeSession`, `options.AddRoutedChannel(...)`, `options.AddActorFactory(...)`,
`options.AddStreamNode(...)`)는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§7-8에 옮겼다.

## 17. Error 의미

상위 표면은 error를 숨기지 않는다. 다만 error를 사용자마다 다르게 조립하지 않도록
framework가 공통 error kind를 제공해야 한다.

| error kind | 발생 조건 |
|------------|-----------|
| `ActorNotAuthenticated` | 인증되지 않은 session이 relay 대상 packet을 보냈다. |
| `ActorRouteNotFound` | play route를 찾지 못했다. resolver 결과의 `RouterChannelId` 또는 `TargetNodeRid`가 빠지면 같은 error로 본다. |
| `ActorCreateFailed` | `CreateAndBindActorAsync(...)`에서 local actor를 만들지 못했다. |
| `ActorAlreadyExists` | local actor runtime 정책상 같은 actor id의 중복 create를 허용하지 않는다. |
| `ActorSessionNotBound` | `SessionProxy` 대상 actor의 session binding이 없다. |
| `SessionProxyTimeout` | session proxy request가 제한 시간 안에 완료되지 않았다. |
| `ActorDispatchTimeout` | actor dispatch request가 제한 시간 안에 완료되지 않았다. |
| `ActorDispatchHandlerFailed` | play handler가 예외를 던졌거나 reply를 만들지 못했다. |
| `CodecFailed` | body encode/decode가 실패했다. |

request/reply 경로에서는 가능한 한 error reply로 돌려보낸다. send 경로에서는 호출자의
`Submit(...)` task가 실패한다. client stream에 어떤 wire error를 보낼지는 stream
failure semantics 문서와 맞춘다.

각 binding은 위 error kind들을 자기 idiom에 맞는 단일 exception/error family로 모아야
한다. retry 여부 분류값(예: `IsRetriable` 같은 boolean)도 함께 노출할 수 있지만,
framework가 자동 retry한다는 뜻이 아니라 caller가 retry policy를 만들 때 참고할 수
있는 분류다. sample은 이 값을 사용해 retry loop를 만들지 않는다.

구체 .NET 시그니처(`ZLinkFrameworkException`, `ZLinkFrameworkErrorKind`)는
[bindings/dotnet/session-actor-dispatch.ko.md](../bindings/dotnet/session-actor-dispatch.ko.md)
§9를 참고한다.

## 18. Breaking Change

이 개선은 compatibility layer 없이 한 번에 적용한다. 정식 application public API 목표는
session actor helper와 resolver 기반 `SessionProxy` 표면으로 통일하는 것이다.

- `EnableSessionGateway()`는 제거한다.
- `AddSessionProxyHandler<THandler>()`처럼 이름이 맞지 않는 raw actor dispatch handler 등록
  표면은 제거한다.
- `IZLinkSessionContext.OpenActorRelay(...)`와 `IZLinkActorRelay.DispatchAsync(...)`는
  public sample과 guide에서 제거한다.
- `IZLinkSessionGateway.SendToActor(...targetSessionNodeRid...)`류 direct target API는
  제거한다.
- 새 `IZLinkSessionProxy` API는 actor -> client 방향의 기본 권장 표면으로 추가한다.
- session -> actor 방향은 `CreateAndBindActorAsync(...)`, `BindActorHandleAsync(...)`,
  `DispatchToActorAsync(...)`로 표현한다.

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
| old public API 제거 | `EnableSessionGateway()`, `AddSessionProxyHandler(...)`, `OpenActorRelay(...)`, `SendToActor(...)`, `RequestActor(...)`가 새 public sample과 guide에 남지 않는다. |
| raw relay public registration 제거 | raw relay public handler와 typed dispatch를 같은 routed channel에서 동시에 켤 수 없다. |

### 19.2 route resolver와 direct target 제거

| 테스트 | 확인 내용 |
|--------|-----------|
| route resolver play path | `IZLinkActorClient`가 actor id로 play route를 찾고 routed message를 보낸다. |
| route resolver spot path | spot name/id 기반 호출과 `JoinSpot(...)`이 spot route를 찾고 routed message를 보낸다. |
| session proxy binding path | session proxy가 actor-session binding으로 session router id와 session id를 찾아 client에게 보낸다. |
| dispatch ref path | `DispatchToActorAsync(...)`는 이미 받은 `IZLinkActorRef` target을 사용하고 play route resolver를 다시 호출하지 않는다. |
| resolver 입력 제한 | actor resolver는 `actorId`만, spot resolver는 spot name/id만 입력으로 받고 metadata, packet name, body를 받지 않는다. |
| missing play resolver validation | `IZLinkActorClient` 사용 시 play route resolver가 없으면 startup 또는 첫 호출에서 명확한 framework error가 난다. |
| missing spot resolver validation | spot name/id 기반 client 또는 `JoinSpot(...)` 사용 시 spot route resolver가 없으면 명확한 framework error가 난다. |
| missing actor-session binding validation | `IZLinkSessionProxy` 사용 시 actor-session binding이 없으면 명확한 framework error가 난다. |
| direct target send/request 제거 | public send/request sample과 guide에서 `RoutingId` target을 직접 받는 API를 사용하지 않는다. |

### 19.3 actor create와 session location lifecycle

| 테스트 | 확인 내용 |
|--------|-----------|
| local actor create | `CreateAndBindActorAsync(...)`가 현재 node actor runtime에 create 요청을 보내고 actor-session binding을 조건부 갱신한다. |
| local actor handle | `BindActorHandleAsync(...)`가 local `SpotNode` actor runtime의 handle을 만들고 session binding metadata를 전달한다. |
| binding update failure rollback | actor-session binding 갱신이 실패하면 helper가 실패하고 local binding table의 같은 token entry를 제거한다. |
| disconnect unbind | stream close 뒤 같은 `sessionId + bindingToken` entry만 actor-session binding에서 제거한다. |
| stale unbind 차단 | 이전 stream의 늦은 unbind가 새 binding token을 가진 actor-session binding을 지우지 못한다. |
| stale session binding 차단 | target session server가 binding token이 맞지 않는 `SessionProxy` message를 거부한다. |
| reconnect binding 교체 | 같은 actor가 다른 session server에 연결하면 새 actor-session binding이 사용된다. |
| direct node 지정 차단 | `BindActorHandleAsync(...)`는 target node `RoutingId`를 받지 않는다. |

### 19.4 metadata, codec, timeout, error

| 테스트 | 확인 내용 |
|--------|-----------|
| metadata 기본 deny | 별도 forwarding 설정이 없으면 application metadata가 actor context로 전달되지 않는다. |
| metadata propagation | 허용된 application metadata는 actor context까지 전달되고 raw request sequence는 노출되지 않는다. |
| metadata raw header 비노출 | actor handler context가 stream session id, peer endpoint, source session node rid, native handle을 노출하지 않는다. |
| codec registry 사용 | sample serializer 없이 framework codec으로 body가 encode/decode된다. |
| codec failure | body encode/decode 실패가 `CodecFailed` 계열 error로 전달된다. |
| session proxy timeout | `SessionProxy.Request(...).Timeout(...)` 대기가 timeout되면 pending request를 정리하고 `SessionProxyTimeout`으로 실패한다. |
| actor dispatch timeout | actor dispatch request timeout이 pending request를 정리하고 `ActorDispatchTimeout`으로 실패한다. |
| route not found error | play route가 없거나 actor-session binding이 없으면 transport send를 시도하지 않고 명확한 error로 실패한다. |
| unauthenticated dispatch 차단 | session callback이 인증 전 domain packet을 play server로 보내지 않는다. |

### 19.5 discovery와 registry metadata sample

| 테스트 | 확인 내용 |
|--------|-----------|
| discovery service/routed channel | 수동 연결 없이 registry/discovery로 service request와 routed request가 통과한다. |
| discovery/manual 혼합 실패 | 같은 capability에서 두 방식을 섞으면 startup에서 실패한다. |
| discovery failure를 retry로 숨기지 않음 | sample과 helper가 retry loop나 warmup sleep 없이 실패를 드러낸다. |
| registry metadata resolver sample | registry discovery metadata sample이 actor/spot resolver 구현에서 route를 읽는다. |
| registry 기본 구현 없음 | framework DI 기본값으로 registry 기반 resolver가 자동 등록되지 않는다. |

sample 검증도 필요하다.

- 기존 `TicTacToe` sample은 그대로 유지한다.
- session actor dispatch sample은 별도 project로 유지한다.
- session actor dispatch sample은 fake transport를 사용하지 않는다.
- session actor dispatch sample은 service channel과 routed channel 모두 수동 연결을
  사용하지 않는다. server bind endpoint는 provider 등록용이고, client 연결은
  `UseDiscovery(...)` 기반 자동 연결로만 표현한다.
- session actor dispatch sample은 retry나 route warmup sleep을 사용하지 않는다.
- session actor dispatch sample은 direct target send/request API를 사용하지 않는다.
  session -> actor 방향은 actor create/dispatch helper를 쓰고, actor -> client 방향은
  `SessionProxy` 이름을 사용한다.
- 실행 결과는 두 actor 인증, match 생성, 재연결, join, move, 승패 판정을 모두
  보여야 한다.

## 20. 구현 순서

권장 구현 순서는 아래와 같다.

1. actor/node/spot 실행 문맥의 typed handler registry 연동을 추가한다.
2. old raw actor relay public registration을 제거하고, internal adapter path가 typed
   dispatch와 같은 routed channel에서 동시에 켜지지 않게 검증한다.
3. actor/spot route resolver interfaces와 DI 등록을 추가한다.
4. actor-session binding 갱신과 token 검증을 runtime 내부 상태로 추가한다.
5. `ZLinkMessageMetadata` snapshot 전달 규칙을 추가한다.
6. `IZLinkSessionProxy`에 actor-session binding 기반 call surface를 추가한다.
7. `CreateAndBindActorAsync(...)`, `BindActorHandleAsync(...)`,
   `DispatchToActorAsync(...)` session actor dispatch helper를 추가한다. session-gateway
   sample은 local `SpotNode` actor runtime 기반 handle model만 사용한다.
8. direct target send/request API를 public sample과 guide에서 제거하고
   internal/resolved helper로 분류한다.
9. session actor dispatch sample을 typed handler와 명시적 session actor dispatch helper 기반으로
    단순화한다.
10. sample POSD 리뷰에서 packet switch, `RoutingId` 누출, serializer helper가 사라졌는지
   다시 확인한다.
12. draft 내용을 구현 결과와 맞춘 뒤 정식 spec 또는 binding 문서로 나누어 반영한다.

## 21. 완료 기준

이 개선의 완료 기준은 단순히 새 API가 존재하는 것이 아니다. sample에서 사용자가 보는
코드가 실제로 줄어야 한다.

완료 기준은 아래와 같다.

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
- routed discovery 연결은 `UseDiscovery(...)` 계열 선언으로만 표현된다.
- 실패를 감추는 retry helper나 warmup sleep이 없다.
- request/reply matching은 request sequence 기준으로 검증된다.
- direct target send/request API는 public sample과 guide에 나오지 않는다.
- `SessionGateway` 이름은 새 public sample과 guide에 나오지 않는다.
- 제거된 low-level session gateway public API를 전제로 한 integration test는 새 public
  API 기준으로 다시 작성한다.

## 22. POSD Review Result

이 초안의 최종 설계 방향은 아래 기준으로 정리한다.

| red flag | 처리 |
|----------|------|
| `RoutingId` 노출 | send/request public API와 session actor helper에서 제거하고 resolver 구현체와 internal transport helper 안에 가둔다. |
| session 위치 stale state | public session route API를 두지 않고 framework/core actor-session binding을 `BindingToken`으로 조건부 갱신한다. |
| 얕은 raw actor dispatch handler | raw relay handler 대신 actor/node/spot 실행 문맥의 typed handler를 기본 표면으로 둔다. |
| packet name switch | 실행 문맥별 handler registry와 message type metadata로 분리한다. |
| raw relay envelope 누출 | request sequence와 session-local metadata는 dispatch/relay helper 안에 둔다. |
| registry 기본 구현 과잉 | registry resolver는 sample로만 제공하고 framework 기본 구현으로 내장하지 않는다. |
| direct target API 혼란 | 정식 sample과 guide에서는 send/request와 session actor helper 모두 resolver 또는 local actor id/type 기반 API로 보여 준다. |
| gateway/proxy naming 혼란 | actor/play 코드가 쓰는 public 이름은 `SessionProxy`로 통일한다. |
| remote node direct handle sample | session-gateway sample은 local `SpotNode` actor runtime handle만 보여 주고 remote node direct handle을 보여 주지 않는다. |

구현 전 spike는 새 결정을 찾기 위한 단계가 아니라, 이 기준이 실제 호출자 code를 줄이는지
확인하는 단계다. spike에서 아래 조건을 만족하지 못하면 API 이름보다 책임 경계를 먼저
다시 봐야 한다.

- session server code가 auth, actor id/type 선택, local actor handle, dispatch 선택만
  가진다.
- actor/node/spot handler code가 `RoutingId`, raw envelope, packet switch를 보지 않는다.
- actor가 client로 send할 때 session route 정보를 직접 들고 있지 않다.
- actor가 client로 send할 때 `SessionProxy`만 사용한다.
- registry 기반 resolver sample은 resolver 구현 예일 뿐 framework 기본값처럼 보이지
  않는다.
