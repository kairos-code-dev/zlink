[스펙 목차](../../README.ko.md)

[초안 묶음](./README.ko.md) | [개요](./overview.ko.md) | [상호작용 모델](./interaction-model.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [framework API](./framework-api.ko.md) | [Session Actor Dispatch 사용성](./session-gateway-usability.ko.md)

# Draft -- ZLink Framework Actor Model

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, framework가 노출하는 actor 개념의 의미만 cross-binding
> 관점에서 정의한다. 언어별 인터페이스 이름과 시그니처는 각 binding 디렉토리의
> 상세 문서에 둔다.

## 1. 목적

zlink core가 [SPOT Actor Guide](../../../../guide/07-4-actor.md)에서 정의한 actor
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
- 자기에게 들어올 packet의 handler 묶음은 actor 자신이 `Configure()` 시점에
  등록한다.

이 점이 일반 handler와 다르다. 일반 handler는 stateless가 기본이고, 매 메시지마다
새 DI scope에서 resolve되며, packet 매핑은 channel 등록이 담당한다. actor는 stateful
이고, identity가 packet routing의 1차 기준이며, packet 매핑은 자기 자신이 한다.

### 2.1 두 가지 직교 축

actor의 라이프 상태는 두 축으로 본다.

| 축 | 값 | 의미 |
| --- | --- | --- |
| **위치** | Entry Spot ↔ user Spot | 생성 직후 `Entry Spot` 에 있고, application이 `JoinSpot(...)`을 호출하면 user Spot으로 옮겨 간다. user Spot에서 다시 Entry Spot으로 돌아가려면 leave (framework 자동), 완전히 제거하려면 destroy (Entry에서만 가능). |
| **session binding** | unbound ↔ bound | 생성 직후 STREAM session에 묶이지 않은 상태다. 인증 등을 거쳐 framework가 actor를 STREAM session에 bind하면 그 session으로 들어오는 packet이 이 actor로 dispatch되고, discovery에 actor route가 publish된다. |

application 입장에서 자주 보는 두 조합:

- **standalone actor** -- session bind 없이 routed 호출만 받는다. Entry Spot에
  머무른다. background worker 패턴.
- **session-bound actor** -- 인증된 client stream과 묶이고, 보통 user Spot에 join
  해서 game room / stage 같은 domain 객체로 동작한다. 대부분의 gateway 시나리오의
  기본형이다.

### 2.2 핵심 제약

zlink core의 actor 모델에서 다음 제약은 모든 binding이 그대로 따른다.

- **user Spot join은 bound session이 있어야 한다.** unbound actor는 Entry Spot에
  머무를 수만 있다.
- **destroy는 actor가 Entry Spot에 있을 때만 가능.** user Spot에 있으면 leave가
  먼저 끝나야 destroy가 허용된다.
- **discovery actor route publish는 session bind 성공 후에만.** unbound actor는
  다른 노드에서 `actorId`로 찾을 수 없다.
- **1 session ↔ N actor / 1 actor ↔ ≤1 session.** 한 session은 여러 actor를 묶을
  수 있지만, 한 actor는 동시에 두 session에 묶이지 않는다.

## 3. application 로직 vs framework 자동 처리

`Entry Spot`, leave, destroy 같은 core 메커니즘은 framework가 자동으로 관리하고
application 표면에 직접 노출하지 않는다. 그러나 그 단계에서 어떤 application 로직이
도는지는 application이 정한다. 두 가지를 구분해서 본다.

### 3.1 framework가 자동으로 관리하는 것

- `Entry Spot` 자체의 생성과 소멸 (binding이 raw API를 직접 호출하지 않는다)
- user Spot → Entry Spot leave (disconnect 시점에 자동)
- actor destroy (disconnect 시점에 자동)
- discovery actor route publish (session bind 성공 시 자동)

framework는 위 시점에 알아서 적절한 core API를 호출하고, application은 결과만
보면 된다.

### 3.2 application이 구현하는 Entry Spot 로직

actor가 `Entry Spot`에 있는 동안 받는 packet의 handler는 application이 정한다.
**별도의 "entry handler" 등록 표면은 두지 않는다.** 같은 actor 클래스의 `Configure()`
에서 등록한 handler들이 그 actor가 entry 단계에 있을 때나 user Spot에 있을 때나
같은 dispatch 묶음으로 동작한다.

이 단계에서 자주 구현하는 로직:

- **인증 / 권한 확인** -- 인증 packet 검증 후 reply 또는 fail.
- **target Spot 선택** -- 클라이언트가 들어갈 game room / stage 결정 후
  `JoinSpot(targetSpotRid, request)` 호출.
- **session 초기 상태 설정** -- session metadata, profile lookup 등.

actor 코드 입장에서 entry 단계인지 user Spot 단계인지 구분이 필요하면 actor
context의 join 상태 (예: `IsJoined`, `SpotRid` 노출) 로 확인한다.

## 4. 라이프사이클 단계

```text
없음
  └─(factory create)→ 생성됨 (Entry Spot 소속, unbound)
        ├─(bind session)→ Entry Spot + bound
        │     └─(JoinSpot)→ user Spot + bound
        │           └─(leave: framework 자동)→ Entry Spot + bound
        └─(disconnect / unbind: framework 자동)→ destroy → 없음
```

각 단계에서 framework는 다음 일을 한다.

| 단계 | framework가 하는 일 |
| --- | --- |
| 생성 | application factory 호출, actor의 context 주입, `Configure()` 호출 |
| session bind | session ↔ actor 묶음 등록, discovery에 actor route publish, application location writer 호출 |
| JoinSpot | target spot에 join 요청 전송, accept/reject 결과를 application에 반환 |
| leave | (자동) user Spot → Entry Spot 이동, spot 쪽에 leave 통보 |
| destroy | (자동) actor 정리, location writer에 unbind 통보, `OnDisconnectedAsync` 호출 |

application은 위 시점에 다음만 책임진다: factory 코드, actor 클래스의
`Configure()` / handler 코드, location resolver / writer 구현, 그리고 actor 안에서
호출하는 `JoinSpot(...)`.

## 5. Dispatch 모델

actor packet dispatch의 key는 다음 셋이다.

- **actor identity** (`actorId`)
- **message kind** (`request` / `command` / `event`). response는 client측 reply correlation 전용이라 dispatch key로 쓰지 않는다.
- **packet name** (default: payload 타입 이름)

actor가 받을 packet handler는 actor 자신이 `Configure()`에서 등록한다. 즉
"actor의 handler namespace는 actor 인스턴스에 속한다". 다른 actor 인스턴스(같은
type이라도 다른 id)는 별도 handler namespace를 가진다.

같은 actor 안에서 같은 `kind + packet name` 조합이 둘 이상 매핑되면 startup
validation 오류로 막는다.

## 6. Outbound: actor를 부르는 쪽

다른 application 코드가 특정 actor를 부를 때는 **actor id**만 안다. 호출자는
actor가 어느 노드 어느 spot에 사는지 알 필요가 없다.

framework는 application이 등록한 두 resolver에 라우팅을 위임한다.

| resolver | 책임 |
| --- | --- |
| play route resolver | actor id → 그 actor가 사는 routed channel + 노드 routing id |
| session route resolver | actor id → 그 actor의 client가 묶인 session 노드 routing id (server → client push에 필요) |

application 저장소(in-memory cache, Redis, registry 등)는 application이 소유한다.
framework는 그 저장소를 만들지 않는다.

## 7. Session actor dispatch (gateway) 패턴

분산 구성에서 가장 큰 use case다. **Session 서버**는 client 연결만 받고, 실제
gameplay 로직은 **Play 서버**의 actor가 처리한다. 그러면서도 client는 단일 stream
연결을 유지하고, Play 서버 actor가 client에 push할 때도 그 stream으로 도달해야
한다.

이 패턴의 핵심 표면 네 개:

- **play route resolver** -- "이 actor id는 어느 play node에 있다"
- **session route resolver** -- "이 actor id의 client는 지금 어느 session node에
  묶여 있다"
- **session location writer** -- session bind / unbind 시점에 application 저장소에
  위치를 기록하는 표면
- **session proxy** -- Play 서버 actor가 자기 client에게 push를 보낼 때 쓰는
  표면. 내부적으로 session resolver로 풀어서 routed channel을 통해 Session
  서버까지 보낸다

이 use case의 사용성과 typed handler / route resolver 결정은
[session-gateway-usability.ko.md](./session-gateway-usability.ko.md)에 정리되어
있다. 본 문서는 그 표면이 actor model의 일부라는 점만 고정한다.

## 8. 등록 표면 (binding 중립)

binding마다 이름은 케이싱 규칙에 따라 다르지만, 의미는 다음 네 가지를 공통으로
제공한다.

| 의미 | 누가 등록하나 | 무엇을 한다 |
| --- | --- | --- |
| actor factory | actor를 만드는 서버 (Play / SPOT host) | actorType 키로 factory 매핑 |
| play route resolver | actor를 외부에서 부르는 모든 서버 | actor id → play node routing |
| session route resolver | actor가 client에 push할 수 있는 서버 (보통 Play) | actor id → session node routing |
| session location writer | session을 받는 서버 (보통 Session) | session bind / unbind 시 위치 기록 |

언어별 실제 API 이름과 시그니처는 binding 디렉토리의 상세 문서에 둔다 (예:
.NET은 `AddActorFactory<>`, `AddActorPlayRouteResolver<>` 등).

## 9. binding 매핑

각 binding에서 본 모델을 어떻게 노출하는지는 해당 디렉토리에서 다룬다.

- `.NET`: [aspnet-core-actor.ko.md](../bindings/dotnet/aspnet-core-actor.ko.md)
  -- `IZLinkActor`, `IZLinkActorContext`, `IZLinkActorFactory`, typed handler
  인터페이스, `IZLinkActorClient`, `IZLinkSessionProxy`, 등록 API
- `Java`, `Node`, `Python`, `Go`, `Rust`, `C++` 등 다른 binding은 각자 디렉토리
  안에 같은 의미의 표면을 같은 cross-language 네이밍 규칙으로 적는다 (자세한
  규칙은 [README.ko.md §5.2.1](./README.ko.md) 참고).

## 10. 결정된 기준

- actor는 ID로 식별되는 stateful object이고, `SpotNode`에 소속된다.
- actor의 packet handler는 actor 자신이 `Configure()`에서 등록한다. 일반 channel
  handler의 attribute scan / 그룹 매핑 모델은 actor에 적용하지 않는다.
- entry-stage 로직(인증, target Spot 선택)을 위해 별도 entry handler 등록 표면을
  두지 않는다. actor의 동일한 handler 묶음이 그 역할을 한다.
- `Entry Spot` 자체, leave, destroy, discovery route publish는 framework가
  자동으로 관리한다. application은 raw API를 직접 호출하지 않는다.
- user Spot join은 bound session이 있어야 한다. unbound actor가 user Spot에 join
  하는 흐름은 application 표면에 두지 않는다.
- actor 위치 저장소는 application이 소유한다. framework는 resolver / writer
  인터페이스만 제공한다.
- server → client push는 반드시 session proxy를 통한다. actor가 stream socket을
  직접 들고 있지 않다.
