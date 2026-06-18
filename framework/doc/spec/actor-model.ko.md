<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework API](./framework-api.ko.md) | [다음: Session Actor Dispatch Usability (Policy)](./session-actor-dispatch.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](./README.ko.md)

[문서 묶음](./README.ko.md) | [개요](./overview.ko.md) | [상호작용 모델](./interaction-model.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [framework API](./framework-api.ko.md) | [Session Actor Dispatch 사용성](./session-actor-dispatch.ko.md)

# ZLink Framework Actor Model

## 1. 목적

zlink core가 [SPOT Actor Guide](../../../doc/guide/07-4-actor.md)에서 정의한 actor
개념을 모든 framework binding이 같은 의미로 노출하도록 고정한다. 언어별 표면은
이 문서의 의미를 어겨서는 안 되고, 다음만 자유롭게 정한다.

- 인터페이스 / 클래스 / context 이름 (해당 언어 케이싱 규칙 안에서)
- DI 또는 lifecycle integration 방식
- 메서드 시그니처가 fluent builder인지 함수 인자형인지

actor 개념은 framework가 다루는 가장 큰 추상 단위 중 하나라, 정책 문서 안에서
**1급 주제**로 둔다. session gateway usability 문서가 다루는 "session actor dispatch"
는 이 actor 모델의 한 가지 use case일 뿐이고, 본 문서가 actor의 핵심 개념을 더
넓은 시야로 잡는다.

## 2. Actor 개념

framework에서 **actor**는 ID로 식별되는 application object다. 다음 속성을 가진다.

- `SpotNode`에 소속된다. actor는 어딘가의 spot에 항상 있다.
- 같은 `actorId`로 들어오는 모든 메시지는 같은 인스턴스가 받는다 (stateful).
- 인스턴스는 application이 등록한 **factory**가 만든다.
- 자기에게 들어올 packet의 handler 묶음은 현재 실행 문맥의 registry가 정한다.
  Entry Spot과 user Spot은 서로 다른 registry를 가진다.

이 점이 일반 handler와 다르다. 일반 handler는 stateless가 기본이고, 매 메시지마다
새 DI scope에서 resolve되며, packet 매핑은 channel 등록이 담당한다. actor는 stateful
이고, identity가 packet routing의 1차 기준이며, packet 매핑은 자기 자신이 한다.

### 2.1 두 가지 직교 축

actor의 라이프 상태는 두 축으로 본다.

| 축 | 값 | 의미 |
| --- | --- | --- |
| **위치** | Entry Spot ↔ user Spot | 생성 직후 `Entry Spot` 에 있고, application이 `JoinSpot(...)`을 호출하면 user Spot으로 옮겨 간다. user Spot에서 다시 Entry Spot으로 돌아가려면 `leaveActor`를 호출하고, 완전히 제거하려면 Entry Spot에서 `destroyActor`를 호출한다. |
| **session binding** | unbound ↔ bound | 생성 직후 STREAM session에 묶이지 않은 상태다. 인증 등을 거쳐 framework가 actor를 STREAM session에 bind하면 그 session으로 들어오는 packet이 이 actor로 dispatch된다. session binding은 actor 위치나 discovery active route를 결정하지 않는다. |

application 입장에서 자주 보는 두 조합:

- **standalone actor** -- session bind 없이 routed 호출만 받는다. Entry Spot에
  머무른다. background worker 패턴.
- **session-bound actor** -- 인증된 client stream과 묶이고, 보통 user Spot에 join
  해서 game room / stage 같은 domain 객체로 동작한다. 대부분의 gateway 시나리오의
  기본형이다.

### 2.2 핵심 제약

zlink core의 actor 모델에서 다음 제약은 모든 binding이 그대로 따른다.

- **user Spot join은 bound session을 요구하지 않는다.** Actor 위치 이동과 STREAM
  session binding은 서로 독립된 상태 전이다.
- **destroy는 actor가 Entry Spot에 있을 때만 가능.** user Spot에 있으면 `leaveActor`가
  먼저 끝나야 `destroyActor`가 허용된다.
- **discovery actor route publish는 user Spot join 성공 뒤에 갱신된다.** session
  bind / unbind는 active route를 만들거나 제거하지 않는다.
- **1 session ↔ N actor / 1 actor ↔ ≤1 session.** 한 session은 여러 actor를 묶을
  수 있지만, 한 actor는 동시에 두 session에 묶이지 않는다.

## 3. application 로직 vs framework 자동 처리

`Entry Spot` raw handle과 core 메커니즘 연결은 framework가 관리한다.
application은 raw Entry Spot handle을 직접 소유하지 않지만, Entry Spot에서 실행될
message handler와 lifecycle callback handler는 별도 표면으로 설정한다. 두 가지를
구분해서 본다.

### 3.1 framework가 자동으로 관리하는 것

- `Entry Spot` 자체의 생성과 소멸 (binding이 raw API를 직접 호출하지 않는다)
- user Spot → Entry Spot leave를 수행하는 public context API 연결
- Entry Spot actor destroy를 수행하는 public context API 연결
- discovery actor route 갱신 (user Spot join / leave 성공 시 core active route 기준)

framework는 위 API가 호출될 때 적절한 core API를 호출한다. 그러나 stream disconnect가
room leave나 actor destroy를 자동으로 실행하지는 않는다. application은 actor를 끝내야
하는 시점에 user Spot에서 `leaveActor`를 호출해 Entry Spot으로 돌려보낸 뒤, Entry Spot
handler 또는 명시적 정리 command에서 `destroyActor`를 호출한다.

`leaveActor`와 `destroyActor`는 서로 다른 책임이다. `leaveActor`는 actor 위치를 user
Spot에서 Entry Spot으로 옮기는 작업이고, actor 객체를 제거하지 않는다. `destroyActor`는
Entry Spot에 돌아온 actor의 수명을 끝내는 작업이며, actor registry, actor-session
binding, native actor ref 같은 framework 상태를 함께 정리한다. 이 분리를 유지해야
room/domain 정리와 actor 수명 종료가 같은 callback에 섞이지 않는다.

Entry Spot과 user Spot의 actor join/leave commit 알림은 spot lifecycle callback으로
전달한다. 각 binding은 core의 `on_join` / `on_leave` 의미를 보존하되, framework public
surface에 native actor ref를 그대로 드러내지 않는다. application은 actor id와 이동
전/후 spot 위치를 보고 room/stage 상태 정리나 운영 event 기록을 수행한다. native
commit epoch를 얻을 수 있는 binding은 그 값을 함께 전달하고, framework membership
변경만으로 만든 알림은 epoch를 `0`으로 둔다.

actor 객체 생성이 끝나면 framework는 `onCreateActor` callback을 한 번 호출한다.
Spot membership에 들어오면 `onJoinedActor`, 다른 Spot으로 나가면 `onLeaveActor`를
호출한다. `destroyActor`는 위치 이동이 아니라 actor 수명 종료이므로 `onLeaveActor`나
다른 lifecycle callback을 호출하지 않고 상태를 정리한다. stream disconnect는
`onDisconnectActor`만 의미하며, leave나 destroy와 같은 뜻이 아니다.
disconnect cleanup만으로 actor destroy가 실행되지 않는다.

### 3.2 application이 구현하는 Entry Spot 로직

actor가 `Entry Spot`에 있는 동안 받는 packet의 handler는 application이 정한다.
이 handler 묶음은 user Spot handler와 별도로 등록한다. Entry 단계와 user Spot 단계는
같은 actor 객체를 보더라도 의미가 다르기 때문이다. 예를 들어 인증, 입장 대상 선택,
초기 session metadata 설정은 Entry Spot에서 처리하고, room/stage 안의 move나 state
update는 user Spot에서 처리한다.

framework는 Entry Spot 자체의 생성과 raw handle 관리는 자동으로 맡지만, Entry Spot의
message handler와 `on_join` / `on_leave` lifecycle callback handler는 application이
별도로 설정할 수 있어야 한다.

이 단계에서 자주 구현하는 로직:

- **인증 / 권한 확인** -- 인증 packet 검증 후 reply 또는 fail.
- **target Spot 선택** -- 클라이언트가 들어갈 game room / stage 결정 후
  `JoinSpot(targetSpotRid, request)` 호출. `targetSpotRid`은 application
  domain spot 이름(`string`)이고 `RoutingId` 변환은 framework 내부 spot route
  resolver가 푼다.
- **session 초기 상태 설정** -- session metadata, profile lookup 등.

actor 코드 입장에서 entry 단계인지 user Spot 단계인지 구분이 필요하면 actor
context의 join 상태 (예: `IsJoined` 노출) 로 확인한다. `RoutingId`
같은 transport 위치값은 actor handler 표면에 노출하지 않는다.

## 4. 라이프사이클 단계

```text
None
  +--(factory create)-> Created (Entry Spot, unbound)
        +--(bind session)-> Entry Spot + bound
        |     +--(JoinSpot)-> user Spot + bound
        |           +--(leaveActor)-> Entry Spot + bound
        |                 +--(destroyActor)-> None
        +--(disconnect / unbind)-> Entry Spot + unbound
              +--(destroyActor)-> None
```

각 단계에서 framework는 다음 일을 한다.

| 단계 | framework가 하는 일 |
| --- | --- |
| 생성 | application factory 호출, actor의 context 주입, `Configure()` 호출 |
| session bind | session ↔ actor 묶음을 framework/core 내부 binding으로 등록한다. discovery active route는 session bind가 아니라 user Spot join / leave 결과를 따른다 |
| JoinSpot | target spot에 join 요청 전송, accept/reject 결과를 application에 반환 |
| leaveActor | user Spot → Entry Spot 이동, source Spot `onLeaveActor`와 target Entry Spot `onJoinedActor` 호출 |
| destroyActor | Entry Spot actor 정리, 내부 actor-session binding 해제, native actor ref 제거. `onLeaveActor`를 호출하지 않는다 |
| disconnect | current stream binding 해제와 `onDisconnectActor` 호출. leave나 destroy를 자동 실행하지 않는다 |
| session-bound actor 등록 | session-bound 경로에서는 local `SpotNode` actor runtime의 actor 생성 또는 handle 준비와 session bind를 `CreateAndBindActorAsync(...)` / `BindActorHandleAsync(...)`로 묶는다. session 표면은 remote node를 직접 지정하는 actor 생성 API를 제공하지 않는다. |

application은 위 시점에 다음만 책임진다: factory 코드, actor 클래스의
`Configure()` / handler 코드, actor/spot route resolver 구현, 그리고 actor 안에서 호출하는
`JoinSpot(...)`.

## 5. Dispatch 모델

actor packet dispatch의 key는 다음 셋이다.

- **actor identity** (`actorId`)
- **message kind** (`request` / `command` / `event`). response는 client측 reply correlation 전용이라 dispatch key로 쓰지 않는다.
- **packet name** (default: payload 타입 이름)

actor가 받을 packet handler는 현재 실행 문맥이 등록한다. 즉 handler namespace는
Entry Spot 또는 user Spot type에 속한다. 같은 actor type이라도 Entry Spot에 있을 때와
user Spot에 있을 때 서로 다른 packet handler와 lifecycle callback handler를 가질 수
있다.

같은 actor 안에서 같은 `kind + packet name` 조합이 둘 이상 매핑되면 startup
validation 오류로 막는다.

### 5.1 Entry Spot과 user Spot registry

모든 binding은 Entry Spot registry와 user Spot registry를 분리해서 노출해야 한다.
두 registry는 같은 actor 객체를 보더라도 다른 실행 문맥이다. Entry Spot registry는
인증, 초기 상태 설정, target Spot 선택처럼 actor가 user Spot에 join하기 전의 packet을
처리한다. user Spot registry는 room, game, stage 같은 domain spot 안에서의 packet을
처리한다.

binding별 실제 이름은 언어 관례에 맞게 정하되, 의미상 아래 항목을 빠뜨리면 안 된다.

| 등록 표면 | Entry Spot | user Spot |
| --- | --- | --- |
| actor packet | Entry Spot context에서 actor type과 handler를 묶는다 | Spot context에서 actor type과 handler를 묶는다 |
| actor joined lifecycle | Entry Spot context에서 actor type과 joined handler를 묶는다 | Spot context에서 actor type과 joined handler를 묶는다 |
| actor left lifecycle | Entry Spot context에서 actor type과 left handler를 묶는다 | Spot context에서 actor type과 left handler를 묶는다 |
| handler 실행 인자 | entry spot + actor + payload 또는 entry spot + actor + lifecycle info | spot + actor + payload 또는 spot + actor + lifecycle info |

Entry Spot과 user Spot은 기능 표면을 맞춘다. 따라서 Entry Spot handler도 Entry
Spot 인스턴스를 받을 수 있다. 차이는 상태 보호 방식이다. user Spot은 room, game,
stage 같은 상태를 소유하므로 같은 spot 실행 queue에서 callback을 직렬화한다. Entry
Spot은 여러 actor가 공유하는 입구이므로 서로 관계없는 packet callback을 Entry Spot
전체 실행 queue에 묶지 않는다.

### 5.2 실행 순서 모델

actor packet dispatch는 먼저 actor별 입력 순서를 보존한 뒤, 현재 actor 위치에 맞는
Spot 실행 줄로 들어간다. Entry Spot도 application callback을 하나의 실행 줄에서
처리하므로 admission 상태를 별도 lock 없이 일관되게 갱신할 수 있다.

기본 규칙은 아래와 같다.

| 입력 경로 | 실행 줄 | 이유 |
| --- | --- | --- |
| STREAM session에서 Entry/local actor로 전달되는 packet | actor별 순서 보존 뒤 현재 actor 위치로 dispatch | 같은 actor의 packet 순서는 지키되, 최종 handler 실행 위치는 Entry Spot 또는 local actor registry가 결정한다 |
| Entry Spot packet / actor packet | Entry Spot 실행 queue | Entry Spot admission 상태를 한 번에 하나의 callback만 변경하게 한다 |
| Entry Spot timer | Entry Spot 실행 queue | Entry Spot의 packet, lifecycle callback, request continuation과 같은 실행 줄을 쓴다. 같은 timer instance callback은 겹치지 않는다 |
| user Spot 안의 actor packet | user Spot 실행 queue | room, game, stage 같은 Spot 상태를 actor handler가 함께 다루므로 handler는 Spot 단위 순서를 지킨다 |
| user Spot packet / timer / subscription | user Spot 실행 queue | 같은 Spot 인스턴스의 상태를 한 번에 하나의 callback만 변경하게 한다 |
| Entry Spot lifecycle / join / leave callback | Entry Spot 실행 문맥 | Entry Spot registry와 lifecycle 상태를 일관되게 다룬다 |

actor별 순서 규칙은 같은 actor 안에서 입력 순서를 보장한다. Entry Spot 또는 user Spot
application handler를 호출할 때는 해당 Spot 실행 queue에서 다시 직렬화한다. 그래서
`actor A` handler가 실행 중이면 같은 Entry Spot으로 들어온 `actor B` handler도 앞선
callback이 완료될 때까지 기다린다.

user Spot 실행 queue는 Spot 인스턴스 하나의 상태를 보호한다. 같은 게임방 안에서
`actor A`와 `actor B`가 모두 board 상태를 바꿀 수 있다면, 두 actor의 handler는 같은
Spot queue에서 순서대로 실행되어야 한다.

Entry Spot은 user Spot처럼 room 상태를 소유하는 곳이 아니라 actor가 처음 지나가는
공용 입구다. 그래도 Entry Spot의 packet, actor packet, lifecycle callback, timer
callback은 같은 Entry Spot 실행 줄에서 직렬화한다. handler가 완료 값을 반환하면 그
완료 값이 끝나기 전까지 같은 Entry Spot의 다음 callback은 시작하지 않는다. timer에서
room, stage, match 상태를 직접 바꿔야 한다면 그 상태를 소유하는 user Spot으로 옮겨야
한다.

### 5.3 lifecycle callback 공개 방식

actor join/leave lifecycle은 Spot method override가 아니라 registry 등록으로
표현한다. 즉 `OnJoinedActor`, `OnLeaveActor` 같은 이름의 method를 framework public
계약으로 요구하지 않는다. 각 언어가 관례상 callback 이름을 다르게 정할 수는 있지만,
다음 의미는 유지해야 한다.

- lifecycle handler는 Entry Spot 또는 user Spot의 registry에 명시 등록된다.
- 같은 registry 안에서 같은 actor type의 joined handler는 하나만 허용한다.
- 같은 registry 안에서 같은 actor type의 left handler는 하나만 허용한다.
- callback은 join/leave commit 이후 같은 실행 문맥에서 호출된다.
- callback은 admission을 결정하는 hook이 아니다. 입장 허용 여부는 join request
  handler나 별도 admission handler가 결정한다.

이 규칙을 두는 이유는 actor packet handler와 lifecycle handler의 소유권을 같은
registry에 두기 위해서다. Entry 단계와 user Spot 단계를 하나의 actor class callback에
섞으면, handler가 현재 위치를 직접 분기해야 하고 binding마다 다른 lifecycle 모양이
생긴다.

## 6. Outbound: actor를 부르는 쪽

다른 application 코드가 특정 actor를 부를 때는 **actor id**만 안다. 호출자는
actor가 어느 노드 어느 spot에 사는지 알 필요가 없다.

actor 안에서 user Spot에 join할 때도 같은 규칙이다. actor context의 `JoinSpot(...)`
public 시그니처는 **`RoutingId spotRid`** 를 받는다. actor handler 표면에는
문자열 spot 이름을 노출하지 않는다. domain key에서 `RoutingId`를 얻는 규칙은 application
spot route resolver가 푼다. application 코드는 `gameId`, `matchId`, `roomId` 같은
domain key를 그대로 들고 다니면 된다.

framework는 application이 등록한 actor/spot resolver에 라우팅을 위임한다.

| resolver | 책임 |
| --- | --- |
| actor route resolver | actor id → 그 actor가 사는 routed channel + 노드 routing id |
| spot route resolver | spot rid → user Spot 위치 |

application 저장소(in-memory cache, Redis, registry 등)는 application이 소유한다.
framework는 그 저장소를 만들지 않는다.

## 7. Session actor dispatch (gateway) 패턴

분산 구성에서 가장 큰 use case다. **Session 서버**는 client 연결만 받고, 실제
gameplay 로직은 **Play 서버**의 actor가 처리한다. 그러면서도 client는 단일 stream
연결을 유지하고, Play 서버 actor가 client에 push할 때도 그 stream으로 도달해야
한다.

이 패턴의 핵심 표면:

- **actor route resolver** -- "이 actor id는 어느 actor/play node에 있다"
- **spot route resolver** -- "이 spot rid는 어느 user Spot에 있다"
- **actor-session binding** -- "이 actor는 현재 어떤 stream session에 묶여 있다".
  이 상태는 framework/core runtime 내부 상태이며 별도 public resolver가 아니다.
- **session proxy** -- Play 서버 actor가 자기 client에게 push를 보낼 때 쓰는
  표면. 내부적으로 actor-session binding을 사용해 현재 client stream으로 보낸다

이 use case의 사용성과 typed handler / route resolver 결정은
[session-gateway-usability.ko.md](./session-actor-dispatch.ko.md)에 정리되어
있다. 본 문서는 그 표면이 actor model의 일부라는 점만 고정한다.

## 8. 등록 표면 (binding 중립)

binding마다 이름은 케이싱 규칙에 따라 다르지만, 의미는 다음 표면을 공통으로
제공한다.

| 의미 | 누가 등록하나 | 무엇을 한다 |
| --- | --- | --- |
| actor factory | actor를 만드는 서버 (Play / SPOT host) | actorType 키로 factory 매핑 |
| Entry Spot | actor runtime을 가진 서버 | Entry Spot context와 handler registry 설정 |
| user Spot factory | user Spot을 만드는 서버 | Spot 타입 기준 factory 매핑 |
| actor packet handler | Entry Spot 또는 user Spot | actor type + message kind + packet name을 handler에 매핑 |
| actor joined handler | Entry Spot 또는 user Spot | actor type별 join commit 후속 처리 등록 |
| actor left handler | Entry Spot 또는 user Spot | actor type별 leave commit 후속 처리 등록 |
| actor route resolver | actor를 외부에서 부르는 모든 서버 | actor id → actor node routing |
| spot route resolver | spot을 이름/id로 부르는 서버 | spot rid → spot routing |

언어별 실제 API 이름과 시그니처는 binding 디렉토리의 상세 문서에 둔다 (예:
.NET은 `AddActorFactory<>`, `AddActorPlayRouteResolver<>` 등).

각 binding 상세 문서는 위 표면을 모두 보여 주는 짧은 코드 예시를 포함해야 한다.
특히 Entry Spot handler 등록과 user Spot handler 등록을 같은 예시에 함께 보여 주어야
한다. 둘 중 하나만 있으면 다른 binding 작성자가 actor lifecycle의 절반을 빠뜨리기
쉽다.

## 9. binding 매핑

각 binding에서 본 모델을 어떻게 노출하는지는 해당 디렉토리에서 다룬다.

- `.NET`: [aspnet-core-actor.ko.md](../../languages/dotnet/doc/spec/aspnet-core-actor.ko.md)
  -- `IZLinkActor`, `IZLinkActorContext`, `IZLinkActorFactory`, typed handler
  인터페이스, `IZLinkActorClient`, `IZLinkSessionProxy`, 등록 API
- `Java`, `Node`, `Python`, `Go`, `Rust`, `C++` 등 다른 binding은 각자 디렉토리
  안에 같은 의미의 표면을 같은 cross-language 네이밍 규칙으로 적는다 (자세한
  규칙은 [README.ko.md §5.2.1](./README.ko.md) 참고).

## 10. 결정된 기준

- actor는 ID로 식별되는 stateful object이고, `SpotNode`에 소속된다.
- actor message handler는 actor 객체가 아니라 현재 실행 문맥의 registry에 등록한다.
  Entry Spot과 user Spot은 서로 다른 registry를 가진다.
- entry-stage 로직(인증, target Spot 선택)을 위해 별도 Entry Spot handler 등록
  표면을 둔다. 일반 user Spot의 handler와 섞지 않는다.
- Entry Spot과 user Spot 모두 `on_join` / `on_leave` lifecycle callback handler를
  별도로 등록할 수 있어야 한다.
- `Entry Spot` 자체, leave, destroy, discovery active route 갱신은 framework가
  자동으로 관리한다. application은 raw API를 직접 호출하지 않는다.
- user Spot join은 bound session을 요구하지 않는다. application state machine은
  join completion이 반환한 최종 actor ref를 기준으로 후속 attach나 dispatch를 결정한다.
- actor/spot 위치 저장소는 application이 소유한다. framework는 actor와 spot resolver
  인터페이스만 제공한다.
- server → client push는 반드시 session proxy를 통한다. actor가 stream socket을
  직접 들고 있지 않다.
- session-bound actor의 local 생성 또는 handle 준비 + bind는 `CreateAndBindActorAsync(...)`와
  `BindActorHandleAsync(...)`가 묶는다. 두 API 모두 local `SpotNode` actor runtime을
  대상으로 하며 remote node를 직접 지정하지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework API](./framework-api.ko.md) | [다음: Session Actor Dispatch Usability (Policy)](./session-actor-dispatch.ko.md)
<!-- framework-adapter-nav:bottom:end -->
