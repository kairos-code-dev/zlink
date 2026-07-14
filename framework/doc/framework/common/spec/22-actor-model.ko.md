<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: SpotNode](21-spot-node.ko.md) | [다음: Spot Actor Join / Transfer 공통 스펙](23-spot-actor.ko.md)
<!-- framework-adapter-nav:end -->


[문서 묶음](../README.ko.md) | [개요](01-overview.ko.md) | [상호작용 모델](02-interaction-model.ko.md) | [메시지 모델](03-message-model.ko.md) | [channel topology](10-channel-topology.ko.md) | [framework API](05-framework-api.ko.md) | [Session Actor Dispatch 사용성](31-session-actor-dispatch.ko.md)

# ZLink Framework Actor Model

## 1. 목적

zlink core가 [SPOT Actor Guide](../../../../../core/doc/guide/07-4-actor.md)에서 정의한 actor
개념을 모든 framework binding이 같은 의미로 노출하도록 고정한다. 언어별 표면은
이 문서의 의미를 어겨서는 안 되고, 다음만 자유롭게 정한다.

- 인터페이스 / 클래스 / context 이름 (해당 언어 케이싱 규칙 안에서)
- DI 또는 lifecycle integration 방식
- 메서드 시그니처가 fluent builder인지 함수 인자형인지

actor 개념은 framework가 다루는 가장 큰 추상 단위 중 하나라, 정책 문서 안에서
**1급 주제**로 둔다. session actor dispatch 사용성 문서가 다루는 "session actor dispatch"
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
| **session binding** | unbound ↔ bound | 생성 직후 STREAM session에 묶이지 않은 상태다. 인증 등을 거쳐 framework가 actor를 STREAM session에 bind하면 그 session으로 들어오는 packet이 이 actor로 dispatch된다. session binding은 actor 위치(actor location row)를 결정하지 않는다. |

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
- **actor location row는 actor 생성 전에 Entry 위치로 claim한다.** 활성화가 끝나면 actor
  ref를 publish하고, user Spot join / leave 성공 때 현재 위치로 갱신한다. session
  bind / unbind는 location row를 만들거나 제거하지 않는다
  ([location runtime](40-location-runtime.ko.md) §2.3).
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
- actor location row claim·publish·갱신 (생성 전 Entry claim, 활성화 뒤 actor ref publish,
  user Spot join / leave 성공 시 현재 위치 갱신)

framework는 위 API가 호출될 때 적절한 core API를 호출한다. 그러나 stream disconnect가
room leave나 actor destroy를 자동으로 실행하지는 않는다. application은 actor를 끝내야
하는 시점에 user Spot에서 `leaveActor`를 호출해 Entry Spot으로 이동한 뒤, Entry Spot
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

actor를 새로 만들 때는 먼저 actor location key에 대해 `NewClaim`을 수행한다. 이 claim을
얻은 실행만 actor 인스턴스를 활성화하고 `onCreateActor` callback을 한 번 호출한다.
동시에 같은 actor id를 만들려는 다른 실행은 활성화 전에 충돌로 실패해야 한다. 정확한
claim 조건과 generation 발급 규칙은
[location runtime §4](40-location-runtime.ko.md#4-ownergeneration-규칙)의
claim-then-activate 계약을 따른다.

actor 객체 생성이 끝나면 framework는 `onCreateActor` callback을 한 번 호출한다.
이때 actor 생성 요청에 실린 create payload를 함께 전달한다. payload 없는 create 편의
API는 빈 `ZLinkMessage`를 전달한 것과 같다. 이미 존재하는 actor를
`getOrCreate`로 재사용할 때는 새 payload로 lifecycle을 다시 호출하지 않는다.
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
- **target Spot 선택** -- 클라이언트가 들어갈 game room / stage를 결정하고,
  그 domain key에서 user Spot의 `RoutingId`를 얻은 뒤 `JoinSpot(spotRid, request)`를
  호출한다. framework는 `spotRid`로 target node route를 찾지만, application이
  `targetRid + spotRid`를 직접 넘기지는 않는다.
- **session 초기 상태 설정** -- session metadata, profile lookup 등.

actor 코드 입장에서 entry 단계인지 user Spot 단계인지 구분이 필요하면 actor
context의 nullable Spot 식별자로 확인한다. 값이 없으면 Entry Spot 단계이고 값이 있으면
해당 user Spot에 참여한 상태다. 별도 boolean을 함께 제공하면 두 값이 모순될 수 있으므로
join 상태를 중복해서 노출하지 않는다. actor handler 표면은 user Spot의 논리 주소인
`RoutingId spotRid`까지 받지만, target node routing id 같은 transport 위치값은 직접 받지
않는다.

actor join 결과도 승인 boolean과 nullable actor를 독립 필드로 제공하지 않는다. 승인
결과는 actor ref와 reply를 함께 가진 값이고, 거절 결과는 reply만 가진 값이다. 각 언어는
sealed hierarchy, tagged union 또는 `variant`로 두 경우만 표현한다. 따라서 승인됐지만
actor ref가 없거나, 거절됐는데 actor ref가 있는 결과는 만들 수 없다.

actor context의 공통 표면은 다음 의미로 제한한다. 언어별 정확한 이름과 비동기 표현은
언어별 interface 문서에서 고정한다.

| 표면 | 의미 |
| --- | --- |
| nullable current Spot id | 값이 없으면 Entry 단계, 값이 있으면 해당 user Spot에 참여한 상태 |
| bound session | 현재 actor에 연결된 client로 one-way send하거나 연결 종료 |
| join user Spot | spot rid와 typed request로 user Spot 입장 요청 |
| join Entry Spot | target SpotNode의 Entry Spot으로 이동 요청 |

actor context는 channel client, stream 객체, client request/reply 또는 session 위치 조회를
추가로 노출하지 않는다.

## 4. 라이프사이클 단계

**destroy 계약:**

- **destroy는 actor가 Entry Spot에 있을 때만 가능하다.** session disconnect는 destroy나 user Spot
  leave를 자동으로 만들지 않는다.
- **Entry Spot destroy는 leave callback이나 다른 lifecycle callback을 호출하지 않고 actor 상태만
  정리한다.**
- **같은 actor instance에 대한 중복 destroy는 성공으로 끝난다.**

```text
None
  +--(NewClaim + factory create)-> Created (Entry Spot, unbound)
        +--(bind session)-> Entry Spot + bound
        |     +--(JoinSpot)-> user Spot + bound
        |           +--(leaveActor)-> Entry Spot + bound
        |           +--(JoinEntrySpot)-> Entry Spot + bound
        |                 +--(destroyActor)-> None
        +--(disconnect / unbind)-> Entry Spot + unbound
              +--(destroyActor)-> None
```

각 단계에서 framework는 다음 일을 한다.

| 단계 | framework가 하는 일 |
| --- | --- |
| 생성 | actor location `NewClaim` 성공 뒤 application factory 호출, actor context 주입, `Configure()`와 `onCreateActor` 호출 |
| session bind | session ↔ actor 묶음을 framework/core 내부 binding으로 등록한다. actor location row는 생성 claim과 Spot 이동 결과를 따르며 session bind로 바뀌지 않는다 |
| JoinSpot | target spot에 join 요청 전송, accept/reject 결과를 application에 반환 |
| leaveActor | user Spot → Entry Spot 이동, source Spot `onLeaveActor`와 target Entry Spot `onJoinedActor` 호출 |
| destroyActor | Entry Spot actor 정리, 내부 actor-session binding 해제, native actor ref 제거. `onLeaveActor`를 호출하지 않는다 |
| disconnect | stream session이 끊기면 framework는 **session ↔ actor binding만 해제한다.** `onDisconnectActor`를 자동으로 호출하지 않고, leave나 destroy도 자동 실행하지 않는다. actor에게 접속 종료를 알려야 하면 **application이 session actor handle의 `NotifyDisconnected()`를 명시적으로 호출**하며, 그때 `onDisconnectActor`가 실행된다 |
| session-bound actor 등록 | session-bound 경로에서는 local Spot node actor runtime의 actor 생성 또는 handle 준비와 session bind를 하나의 생성·bind 작업으로 묶는다. session 표면은 remote node를 직접 지정하는 actor 생성 API를 제공하지 않는다. |

application은 위 시점에 다음만 책임진다: factory 코드, actor 클래스의
`Configure()` / handler 코드, placement에 사용할 actor id와 spot rid 선택, 그리고 actor
안에서 호출하는 `JoinSpot(...)`. 위치 조회는 framework가 등록된 location store를 통해
수행하므로 application이 별도 route resolver를 구현하지 않는다.

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
Spot 인스턴스를 받을 수 있다. user Spot은 room, game, stage 같은 상태를 소유하므로
같은 spot 실행 queue에서 callback을 직렬화한다. Entry Spot은 공유 admission 상태와
lifecycle 상태를 다루는 Entry packet, lifecycle, route, subscription, timer callback을
하나의 Entry 실행 줄에서 직렬화한다. 반면 Entry actor packet은 대상 actor의 mailbox에서
실행한다. 같은 actor의 순서는 보존하지만 서로 다른 actor는 서로 기다리지 않는다.

### 5.2 실행 순서 모델

actor packet dispatch는 먼저 actor별 입력 순서를 보존한다. actor가 Entry Spot에 있으면
대상 actor mailbox가 실행 경계이며, 서로 다른 actor의 handler는 병렬로 실행될 수 있다.
actor가 user Spot에 있으면 공유 Spot 상태를 보호하기 위해 user Spot 실행 줄로 들어간다.
Entry Spot 자체의 공유 상태를 다루는 callback은 actor mailbox와 분리된 Entry 실행 줄에서
처리한다.

기본 규칙은 아래와 같다.

| 입력 경로 | 실행 줄 | 이유 |
| --- | --- | --- |
| STREAM session에서 Entry/local actor로 전달되는 packet | actor별 순서 보존 뒤 현재 actor 위치로 dispatch | 같은 actor의 packet 순서는 지키되, 최종 handler 실행 위치는 Entry Spot 또는 local actor registry가 결정한다 |
| Entry Spot packet / route / subscription | Entry Spot 실행 queue | Entry Spot 공유 상태를 한 번에 하나의 callback만 변경하게 한다 |
| Entry Spot의 actor packet | actor별 mailbox | 같은 actor의 입력 순서를 지키면서 서로 다른 actor가 서로 기다리지 않게 한다 |
| Entry Spot timer | Entry Spot 실행 queue | timer와 다른 Entry callback이 같은 공유 상태를 동시에 변경하지 않게 한다 |
| user Spot 안의 actor packet | user Spot 실행 queue | room, game, stage 같은 Spot 상태를 actor handler가 함께 다루므로 handler는 Spot 단위 순서를 지킨다 |
| user Spot packet / timer / subscription | user Spot 실행 queue | 같은 Spot 인스턴스의 상태를 한 번에 하나의 callback만 변경하게 한다 |
| Entry Spot lifecycle / join / leave callback | Entry Spot 실행 문맥 | Entry Spot registry와 lifecycle 상태를 일관되게 다룬다 |

actor별 순서 규칙은 같은 actor 안에서 입력 순서를 보장한다. Entry Spot에 있는 actor의
packet은 해당 actor mailbox에서만 직렬화하므로 `actor A` handler가 실행 중이어도
`actor B` handler는 실행할 수 있다. user Spot에 있는 actor의 packet은 user Spot의 공유
상태를 다룰 수 있으므로 해당 Spot 실행 줄에서 다시 직렬화한다.

user Spot 실행 queue는 Spot 인스턴스 하나의 상태를 보호한다. 같은 게임방 안에서
`actor A`와 `actor B`가 모두 board 상태를 바꿀 수 있다면, 두 actor의 handler는 같은
Spot queue에서 순서대로 dispatch된다. 다만 handler가 framework terminator를 await하면 그
지점에서 실행 줄을 양보하므로, 두 handler는 **await 경계에서 인터리브될 수 있다.** await를
가로질러 유지해야 하는 불변식이 있으면 그 구간을 terminator 없이 구성하거나 상태 전이를
await 이후로 모아야 한다.

Entry Spot은 user Spot처럼 room 상태를 소유하는 곳이 아니라 actor가 처음 거치는 공용
입구다. Entry Spot의 packet, lifecycle, route, subscription, timer callback은 같은 Entry
실행 줄에서 직렬화한다. callback이 비동기 완료 값을 반환하면 그 작업이 끝나기 전까지
같은 실행 줄의 다음 callback은 시작하지 않는다. 단 request·join·worker의 **framework
terminator await 지점에서는 실행 줄을 양보**하므로 그 대기 중에 같은 줄의 독립 callback이
시작할 수 있다([04 비동기 실행 정책](04-async-execution-policy.ko.md) section 1). Entry actor
packet은 이 실행 줄에 넣지 않고 actor별 mailbox로 보낸다. timer에서 room, stage, match 상태를
직접 바꿔야 한다면 그 상태를 소유하는 user Spot으로 작업을 전달한다.

### 5.3 lifecycle callback 공개 방식

actor join/leave lifecycle은 Spot lifecycle callback으로 제공한다. 각 언어는 같은
callback을 interface method, overridable method, function object 등 해당 언어에
자연스러운 형태로 제공할 수 있다. 다음 의미는 모든 언어에서 유지해야 한다.

- target Spot은 actor 입장 허용 여부를 결정하는 join callback을 제공한다.
- source Spot은 membership을 제거한 뒤 leave callback을 호출한다.
- target Spot은 membership commit 뒤 joined callback을 호출한다.
- callback은 join/leave commit 이후 같은 실행 문맥에서 호출된다.
- joined와 leave callback은 admission을 결정하지 않는다. 입장 허용 여부는 join
  callback의 accept/reject 결과가 결정한다.

언어별 public method 이름과 async 반환형은 언어별 스펙에서 고정한다. callback을
registry에 등록하는 언어도 위 세 lifecycle 시점과 결과를 동일하게 제공해야 하며,
registry 등록 방식 자체를 다른 언어에 강제하지 않는다.

## 6. Outbound: actor를 부르는 쪽

다른 application 코드가 특정 actor를 부르기 시작할 때는 domain key인 actor id만 알 수 있다.
호출자는 location resolver나 actor manager로 메시징 대상 값인 `ActorRef`를 얻은 뒤 actor client에
넘긴다. actor가 어느 노드 어느 Spot에 존재하는지 직접 조합하거나 별도 routing id를 전달하지 않는다.

actor 안에서 user Spot에 join할 때도 같은 규칙이다. actor context의 `JoinSpot(...)`
public 시그니처는 **`RoutingId spotRid`** 를 받는다. actor handler 표면에는
문자열 spot 이름을 노출하지 않는다. domain key(`gameId`, `matchId`, `roomId` 등)에서
`RoutingId`를 만드는 규칙은 application의 것이고, application 코드는 domain key를
그대로 들고 다니면 된다.

위치 조회는 framework의 location resolver가 맡는다
([location runtime §5](40-location-runtime.ko.md)). 기본 구현은 등록된 location store를
읽고, 결과로 내부 주소 갱신을 소유하는 `SpotHandle`을 돌려준다.

| resolver | 책임 |
| --- | --- |
| actor location resolver | actor id → 그 actor가 위치한 spot의 `SpotHandle` |
| spot location resolver | spot rid → 그 spot의 `SpotHandle` |

위치의 저장소는 application이 등록한 location store(예: 공식 Redis extension)다.
application은 저장소 구현을 선택하지만 조회 resolver를 따로 구현하지 않는다. framework가
store 조회와 owner lease 검증을 수행하고, actor/spot lifecycle이 row를 자동으로
등록·갱신한다.

### 6.1 server-to-actor 메시징

서버 측 caller가 stream session을 거치지 않고 actor mailbox로 보내는 public 표면은 actor client다.
send와 request는 모두 `ActorRef`를 대상으로 받으며, actor id만 받는 overload는 제공하지 않는다.
호출자가 id만 알고 있으면 먼저 resolver나 actor manager에서 ref를 얻어야 한다. 이 경계를 두는
이유는 위치 조회, generation 검증, 실제 전송을 한 호출에 숨기지 않고 stale ref를 명시적으로
판정하기 위해서다.

- actor send의 one-way `submit()` 완료는 입력 검증과 local queue 수락을 뜻한다. 대상
  actor mailbox 인계 이후의 실패는 monitoring/error observer로 관찰한다.
- request 완료는 같은 인계 뒤 actor handler의 reply가 caller에게 돌아왔음을 뜻한다.
- 어느 경우에도 이 호출로 actor의 bound session을 만들거나 바꾸지 않는다.
- 존재하지 않는 actor, stale generation, 끊긴 route는
  [framework API의 오류 분류](05-framework-api.ko.md)에 따라 구분한다.

구체적인 bind 상태별 검증은 [Config 9](../e2e/config-9-to-actor-messaging.ko.md)에 둔다.

## 7. Session actor dispatch (gateway) 패턴

분산 구성에서 가장 큰 use case다. **Session 서버**는 client 연결만 받고, 실제
gameplay 로직은 **Play 서버**의 actor가 처리한다. 그러면서도 client는 단일 stream
연결을 유지하고, Play 서버 actor가 client에 push할 때도 그 stream으로 도달해야
한다.

이 패턴의 핵심 표면:

- **actor location resolver** -- actor id로 framework location store를 조회한다.
- **spot route resolver** -- "이 spot rid는 어느 user Spot에 있다"
- **actor-session binding** -- "이 actor는 현재 어떤 stream session에 묶여 있다".
  이 상태는 framework/core runtime 내부 상태이며 별도 public resolver가 아니다.
- **bound session** -- Play 서버 actor가 자기 client에게 one-way push를 보내거나 연결을
  종료할 때 쓰는 표면. 내부적으로 core actor-session binding을 사용한다.

이 use case의 사용성과 typed handler / route resolver 결정은
[31-session-actor-dispatch.ko.md](31-session-actor-dispatch.ko.md)에 정리되어
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
| actor join admission | Entry Spot 또는 user Spot | join 요청을 받아 입장 허용 여부를 결정한다. **필수 구현**이며 actor id와 join request만 받는다([23 §12](23-spot-actor.ko.md)) |
| actor joined handler | Entry Spot 또는 user Spot | actor type별 join commit 후속 처리 등록 |
| actor left handler | Entry Spot 또는 user Spot | actor type별 leave commit 후속 처리 등록 |
| location store | 위치 조회가 필요한 host의 infrastructure 설정 | application이 store 구현을 등록하고 framework actor/spot resolver가 이를 사용 |
| actor location resolver | framework runtime | actor id → actor가 위치한 Spot handle |
| spot location resolver | framework runtime | spot rid → Spot handle |

언어별 실제 API 이름과 시그니처는 binding 디렉토리의 상세 문서에 둔다. application은
actor factory와 location store를 등록하지만 framework location resolver 구현을 별도로
등록하지 않는다.

각 binding 상세 문서는 위 표면을 모두 보여 주는 짧은 코드 예시를 포함해야 한다.
특히 Entry Spot handler 등록과 user Spot handler 등록을 같은 예시에 함께 보여 주어야
한다. 둘 중 하나만 있으면 다른 binding 작성자가 actor lifecycle의 절반을 빠뜨리기
쉽다.

## 9. binding 매핑

각 binding에서 본 모델을 어떻게 노출하는지는 해당 디렉토리에서 다룬다.

- `.NET`: [handler-interfaces.ko.md](languages/dotnet/02-handler-interfaces.ko.md)
  -- `IZLinkActor`, `IZLinkActorContext`, `IZLinkActorFactory`, typed handler
  인터페이스, `IZLinkActorManager`, `IZLinkBoundSession`, 등록 API
- `Java`, `Node`, `Python`, `Go`, `Rust`, `C++` 등 다른 binding은 각자 디렉토리
  안에 같은 의미의 표면을 각 언어의 관례에 맞춰 적는다 (자세한 규칙은
  [공개 계약 관리 §4](00-public-contract-governance.ko.md#4-언어별-표현-원칙) 참고).

## 10. 결정된 기준

- actor는 ID로 식별되는 stateful object이고, `SpotNode`에 소속된다.
- actor message handler는 actor 객체가 아니라 현재 실행 문맥의 registry에 등록한다.
  Entry Spot과 user Spot은 서로 다른 registry를 가진다.
- entry-stage 로직(인증, target Spot 선택)을 위해 별도 Entry Spot handler 등록
  표면을 둔다. 일반 user Spot의 handler와 섞지 않는다.
- Entry Spot과 user Spot 모두 `on_join` / `on_leave` lifecycle callback handler를
  별도로 등록할 수 있어야 한다.
- `Entry Spot` 자체, leave, destroy, actor location row 갱신은 framework가
  자동으로 관리한다. application은 raw API를 직접 호출하지 않는다.
- user Spot join은 bound session을 요구하지 않는다. application state machine은
  join completion이 반환한 최종 actor ref를 기준으로 후속 attach나 dispatch를 결정한다.
- actor/spot 위치 저장소 구현은 application이 선택한다. framework가 그 store를 읽는
  actor/spot resolver와 row lifecycle을 제공한다.
- server → client one-way push와 연결 종료는 현재 actor context의 bound session을
  통한다. actor가 stream socket이나 session 위치를 직접 보관하지 않는다.
- session-bound actor의 local 생성 또는 handle 준비와 bind는 언어별 생성·bind 작업이
  하나로 묶는다. 이 작업은 local Spot node actor runtime을
  대상으로 하며 remote node를 직접 지정하지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: SpotNode](21-spot-node.ko.md) | [다음: Spot Actor Join / Transfer 공통 스펙](23-spot-actor.ko.md)
<!-- framework-adapter-nav:bottom:end -->
