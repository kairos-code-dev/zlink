# Framework Entry Spot actor dispatch 정합성 계획

> 이 문서는 이미 공통 framework spec에 들어간 Entry Spot actor dispatch 계약을
> 언어별 framework 구현, 회귀 테스트, guide, sample에 맞추기 위한 계획이다.
> 새 공개 API를 추가하는 계획이 아니다.

## 목적

Entry Spot은 SpotNode에 들어온 actor가 user Spot으로 이동하기 전 머무는 진입점이다. actor session
bind, admission, client가 actor에게 보내는 초기 packet 같은 흐름이 이 지점을 지난다.

공통 framework spec은 Entry Spot actor packet을 Entry Spot 전체 실행 줄에 세우지 않고, 대상 actor의
mailbox로 처리한다고 정한다. 같은 actor의 packet은 순서대로 처리하지만 서로 다른 actor는 서로 기다리지
않아야 한다.

하지만 일부 언어 구현과 문서에는 Entry Spot actor handler 실행도 Entry Spot의 serial execution queue를
다시 통과하는 흐름이 남아 있다. 이 구조는 객체 동기화가 단순하다는 장점이 있지만, 이미 정해진 공통
계약과 다르게 서로 다른 actor의 packet까지 Entry Spot 하나의 실행 줄에 모이게 만든다.

이 계획의 목적은 Entry Spot actor packet 실행 단위를 Entry Spot 전체가 아니라 actor runtime state로
맞추는 것이다. Entry Spot 자체의 lifecycle, route, subscription 정책은 이 계획에서 바꾸지 않는다.
Entry Spot timer 정합성은 관련 문서와 구현에 충돌이 남아 있으므로 별도 범위로 분리한다.

```text
+--------------------------+
| Entry Spot ingress       |
+--------------------------+
| resolve actor id         |
| resolve actor handler    |
+--------------------------+
             |
             v
+--------------------------+
| Actor dispatch mailbox   |
+--------------------------+
| invoke actor handler     |
+--------------------------+
```

## 핵심 결정

### 1. Entry Spot actor packet은 actor별 dispatch mailbox가 직렬화한다

Entry Spot actor packet handler는 actor instance를 인자로 받는다. 따라서 이 handler의 상태 보호
기준은 Entry Spot 객체가 아니라 actor runtime state여야 한다.

To-be 실행 모델:

```text
Entry Spot actor packet:
  Entry Spot ingress -> actor mailbox -> actor handler

User Spot actor packet:
  User Spot ingress -> user Spot serial queue -> actor handler

Domain Spot route/timer/subscription:
  Spot serial queue -> handler
```

같은 actor에 대한 packet은 actor mailbox에서 순서대로 처리한다. 서로 다른 actor에 대한 packet은
Entry Spot serial queue 때문에 서로 막히지 않아야 한다.

### 2. Entry Spot serial queue는 Entry Spot 자체 상태에만 쓴다

Entry Spot serial queue는 다음 흐름에 유지한다. 이 표는 actor packet과 직접 관련된 범위만 다룬다.

| 흐름 | serial 유지 이유 |
|------|------------------|
| Entry Spot configure | handler registry와 node 초기화 순서를 명확히 유지한다. |
| Entry Spot lifecycle callback | Entry Spot 자체 admission 정책과 lifecycle side effect를 순서대로 처리한다. |
| Entry Spot route/subscription | Entry Spot 객체가 직접 소유하는 상태를 다룰 수 있다. |

Entry Spot actor packet handler는 이 serial queue에 들어가지 않는다. handler가 Entry Spot 객체를
인자로 받더라도, 그 객체의 mutable state를 actor 간 공유 동기화 수단으로 쓰지 않는다는 계약을
문서화한다.

Entry Spot timer는 이 계획의 직접 대상이 아니다. 공통 framework 문서는 Entry Spot timer를 Entry Spot
전체 queue에 묶지 않는 방향을 담고 있지만, 일부 언어 guide에는 반대 설명이 남아 있다. 구현 작업 중
timer 계약 충돌이 발견되면 이 계획에 섞지 않고 별도 timer 정합성 작업으로 분리한다.

### 3. Entry Spot actor handler에서 `yield`는 기본 권장 경로가 아니다

Spot handler의 `yield` terminator는 framework serial turn을 I/O 대기 동안 반납하기 위한 API다.
이 계약은 domain Spot과 user Spot에는 계속 필요하다. 하지만 Entry Spot actor packet handler가
Entry Spot serial queue를 타지 않으면, 서로 다른 actor를 위해 Entry Spot turn을 반납할 필요가
없어진다.

Entry Spot actor handler의 기본 사용법은 일반 async completion이다.

```csharp
public ValueTask HandleAsync(
    CourierEntrySpot entrySpot,
    CourierActor actor,
    ZLinkSpotActorSendContext context,
    CourierDecision message,
    CancellationToken cancellationToken)
{
    _ = entrySpot;
    _ = context;
    cancellationToken.ThrowIfCancellationRequested();
    actor.Complete(message); // actor별 pending request만 완료한다.
    return ValueTask.CompletedTask;
}
```

`yield`가 사라지는 것은 아니다. user Spot handler, route handler, domain Spot timer handler처럼
Spot 공유 상태를 기준으로 직렬 실행되는 곳에서는 계속 명시적 interleaving API로 유지한다.

### 4. Entry Spot actor handler는 Entry Spot 공유 mutable state에 의존하지 않는다

Entry Spot actor handler에 Entry Spot 인자를 넘기는 표면은 유지할 수 있다. 다만 의미를 좁힌다.

허용:

- Entry Spot context 조회
- logger, readonly configuration, stateless helper 사용
- actor instance 상태 변경
- actor bound session send/request
- actor별 directory나 actor runtime state 조회

금지 또는 재검토 대상:

- Entry Spot field에 actor별 mutable state를 저장
- 여러 actor가 공유하는 collection을 Entry Spot actor handler에서 직접 갱신
- await 전후 Entry Spot mutable state snapshot을 의사결정에 사용
- Entry Spot actor handler의 순서가 모든 actor에 대해 전역 FIFO라고 가정하는 코드

여러 actor가 공유하는 업무 상태가 필요하면 user Spot 또는 별도 domain Spot으로 옮긴다.

## 현재 .NET 기준 동작

.NET 구현에는 actor별 mailbox가 이미 있다.

- `ZLinkActorRuntimeState.ExecuteDispatchAsync(...)`가 actor별 dispatch mailbox에 들어간다.
- `ZLinkEntrySpotActorRouter`는 Entry Spot actor packet을 이 actor mailbox로 보낸다.
- 그 뒤 `ZLinkEntrySpotActivation.InvokeActorPacketAsync(...)`가 Entry Spot serial queue를 다시 탄다.

현재 구조:

```text
Actor mailbox ----\
Actor mailbox -----+--> Entry Spot serial queue --> handler
Actor mailbox ----/
```

To-be 구조:

```text
Actor mailbox --> handler
Actor mailbox --> handler
Actor mailbox --> handler
```

이 변경은 actor별 순서를 깨지 않는다. 바뀌는 것은 서로 다른 actor packet이 Entry Spot serial queue에서
다시 한 줄로 모이지 않는다는 점이다.

## 언어별 적용 계획

### .NET

| 영역 | 변경 내용 |
|------|-----------|
| runtime | Entry Spot actor packet handler 호출 경로에서 `ZLinkEntrySpotActivation.ExecuteAsync(...)` 또는 `_serial` 경유를 제거한다. |
| actor mailbox | `ZLinkActorRuntimeState.ExecuteDispatchAsync(...)`는 유지한다. actor별 순서와 `CurrentDispatch` 설정은 이곳이 책임진다. |
| handler executor | Entry Spot actor packet handler를 직접 호출하는 내부 API를 추가한다. public API는 추가하지 않는다. |
| context | handler 실행 중 Entry Spot ambient context, actor context, bound session scope가 유지되는지 확인한다. serial queue를 제거하더라도 `ZLinkSpotAmbientContext`와 Entry Spot current context는 사라지면 안 된다. |
| error/DI | 기존 handler executor의 DI scope, codec registry, error propagation 의미를 유지한다. serial queue 제거가 handler 생성과 예외 관찰 경로를 바꾸면 안 된다. |
| yield | Entry Spot actor handler sample에서 `Yield(...)` 사용을 제거하거나 새로 추가하지 않는다. domain Spot/route handler의 yield 문서는 유지한다. |

검증 대상:

- `ZLinkEntrySpotActorRouter`
- `ZLinkEntrySpotActorDispatch`
- `ZLinkEntrySpotActivation`
- `ZLinkEntrySpotHandlerExecutor`
- actor bound session dispatch scope
- actor request reply response relay

### C++

| 영역 | 변경 내용 |
|------|-----------|
| runtime | Entry Spot actor packet handler가 actor별 dispatch lane에서 실행되는지 확인하고, Entry Spot-wide serial executor를 추가로 타는 경로가 있으면 제거한다. |
| actor state | actor별 mailbox 또는 equivalent dispatch guard가 없다면 먼저 내부 runtime state에 추가한다. |
| handler API | public handler 등록 표면은 바꾸지 않는다. |
| yield | Entry Spot actor handler 예제에서 yield를 권장하지 않는다. user Spot/domain Spot 예제의 cooperative dispatch는 유지한다. |

검증 대상:

- DeliveryDispatch E2E
- YieldDispatch E2E
- SpotService actor packet E2E
- actor session/bound push sample

### Java

| 영역 | 변경 내용 |
|------|-----------|
| runtime | Entry Spot actor handler invocation이 actor별 executor 또는 mailbox 뒤에서 직접 실행되게 한다. |
| Spring/DI | handler bean scope와 Entry Spot instance 접근이 concurrent actor handler 실행에서 안전한지 확인한다. |
| public API | `yield(...)` terminator는 유지하되 Entry Spot actor handler 기본 예제에서는 `submit(...)`/일반 async completion을 사용한다. |
| docs | Java spec/guide에서 Entry Spot actor handler는 actor별 순서만 보장한다고 설명한다. |

검증 대상:

- Java YieldDispatch E2E
- Java DeliveryDispatch E2E 또는 sample
- Java SpotService actor packet E2E
- concurrent actor packet unit test

### Kotlin

| 영역 | 변경 내용 |
|------|-----------|
| coroutine bridge | Entry Spot actor handler coroutine이 Entry Spot-wide mutex를 잡은 채 suspend되지 않게 한다. |
| actor mailbox | Java runtime을 공유하면 Java 변경을 Kotlin E2E로 검증한다. Kotlin 전용 wrapper가 있으면 actor별 dispatch context를 유지한다. |
| public API | `yield(call, ...)` helper는 domain Spot용으로 유지한다. Entry Spot actor handler 예제는 일반 suspend/await 형태를 사용한다. |
| docs | Kotlin guide에서 Entry Spot actor handler의 공유 상태 금지 규칙을 설명한다. |

검증 대상:

- Kotlin YieldDispatch E2E
- Kotlin DeliveryDispatch E2E 또는 sample
- Kotlin SpotService actor packet E2E

### Node

| 영역 | 변경 내용 |
|------|-----------|
| runtime | Entry Spot actor handler promise chain이 Entry Spot serial queue에 묶이지 않게 한다. actor별 queue에서 handler를 호출한다. |
| async context | `AsyncLocalStorage`나 equivalent context를 turn ownership 저장소로 사용하지 않는다. 필요한 context는 call object 생성 시 캡처한다. |
| public API | `yield(...)`는 Spot serial turn을 반납하는 terminator로 유지한다. Entry Spot actor handler 예제는 `submit(...)` 또는 일반 promise await를 사용한다. |
| docs | Node spec의 actor packet 실행 순서 설명이 실제 구현과 계속 맞는지 확인한다. |

검증 대상:

- Node YieldDispatch E2E
- Node DeliveryDispatch.Ts sample
- Node SpotService E2E
- concurrent actor packet unit test

## 샘플 변경 계획

샘플 변경은 public 사용 패턴을 보여주는 작업이다. 단순히 빌드를 맞추기 위해 handler에 내부 helper를
넣지 않는다.

| 샘플 | 변경 기준 |
|------|-----------|
| DeliveryDispatch | Entry Spot actor handler에서 actor별 상태만 다루도록 유지한다. handler 안의 Entry Spot mutable state 의존이 있으면 actor 또는 domain Spot으로 옮긴다. Entry Spot actor handler에서 `Yield`를 쓰지 않는다. |
| Bingo | Entry Spot은 lobby/admission만 맡고 room 상태는 room Spot이 맡는 구조를 유지한다. Entry Spot actor handler의 `Yield` 사용 여부를 점검하고 일반 async로 바꿀 수 있는 곳은 바꾼다. |
| SupportChat | conversation 생성 전 admission은 Entry Spot에서 처리하되, conversation 공유 상태는 Conversation Spot에 둔다. |
| TicTacToe | 수동 연결 샘플 특성은 유지한다. Entry Spot actor handler가 game room 상태를 직접 만지지 않는지 확인한다. |
| ShoppingMall/GameQuest | actor 사용 샘플이 있으면 Entry Spot actor handler의 공유 상태와 yield 사용을 점검한다. |

DeliveryDispatch 기준으로 기대하는 모양:

```csharp
public ValueTask HandleAsync(
    CourierEntrySpot entrySpot,
    CourierActor actor,
    ZLinkSpotActorSendContext context,
    CourierDecision message,
    CancellationToken cancellationToken)
{
    _ = entrySpot;
    _ = context;
    cancellationToken.ThrowIfCancellationRequested();
    actor.Complete(message); // actor별 pending request만 완료한다.
    return ValueTask.CompletedTask;
}
```

## E2E와 테스트 계획

### 공통 scenario

| scenario | 목적 |
|----------|------|
| concurrent entry actor send | 서로 다른 actor의 Entry Spot actor send handler가 Entry Spot serial queue 때문에 전역 FIFO로 막히지 않는지 확인한다. |
| same actor ordering | 같은 actor의 actor packet 순서는 유지되는지 확인한다. |
| entry actor request reply | Entry Spot actor request handler가 reply를 반환하고 framework가 client response를 정상 전송하는지 확인한다. |
| bound session send | Entry Spot actor handler 안에서 bound session send/request가 actor dispatch context를 잃지 않는지 확인한다. |
| entry handler shared state guard | Entry Spot actor handler가 Entry Spot mutable state 전역 FIFO에 의존하지 않도록 문서와 sample을 점검한다. |
| yield compatibility | domain Spot과 route handler의 `Yield` 계약이 유지되는지 확인한다. |

### 언어별 회귀 테스트 추가

이번 변경은 일부 언어 구현과 문서의 실행 순서 의미를 공통 계약에 맞추는 작업이다. 따라서 각 언어
framework에 회귀 테스트를 새로 둔다. 기존 sample 실행만으로는 Entry Spot serial queue가 제거됐는지
확인하기 어렵다.

| 언어 | 추가할 회귀 테스트 |
|------|--------------------|
| .NET | 서로 다른 두 actor의 Entry Spot actor send handler가 동시에 들어왔을 때 한 actor handler의 대기 구간이 다른 actor handler 시작을 막지 않는지 검증한다. 같은 actor에 대해서는 두 packet이 순서대로 처리되는지도 함께 확인한다. |
| C++ | Entry Spot actor handler에서 actor A가 latch를 잡고 대기하는 동안 actor B handler가 실행되는지 검증한다. actor A의 두 번째 packet은 첫 번째 packet 완료 전 실행되지 않아야 한다. |
| Java | `CompletableFuture` 또는 framework async call로 actor A handler를 지연시키고, actor B handler가 같은 Entry Spot에서 독립적으로 실행되는지 검증한다. Spring handler bean이 singleton이어도 actor별 상태만 사용한다는 점을 확인한다. |
| Kotlin | suspending Entry Spot actor handler에서 actor A coroutine이 지연될 때 actor B coroutine이 같은 Entry Spot-wide mutex에 막히지 않는지 검증한다. 같은 actor coroutine 순서는 유지한다. |
| Node | actor A handler promise를 지연시킨 상태에서 actor B handler promise가 시작되는지 검증한다. Node event loop에서 promise scheduling만으로 우연히 통과하지 않도록 latch/marker를 사용한다. |

각 회귀 테스트는 다음 세 조건을 모두 검증한다.

- actor A의 첫 packet이 완료되지 않은 상태에서 actor B의 packet handler가 시작된다.
- actor A의 두 번째 packet은 actor A의 첫 packet 완료 뒤에 시작된다.
- handler 안에서 일반 async/await를 사용해도 Entry Spot actor handler는 Entry Spot-wide `yield` 호출에
  의존하지 않는다.

테스트 이름은 언어별 관례를 따르되, 의미가 드러나도록 다음 단어를 포함한다.

```text
EntrySpotActorDispatch
ConcurrentActors
SameActorOrdering
```

### 언어별 runner

| 언어 | 필수 실행 |
|------|-----------|
| .NET | `dotnet build framework/languages/dotnet/Zlink.Framework.sln`, 새 EntrySpotActorDispatch 회귀 테스트, DeliveryDispatch sample, YieldDispatch E2E |
| C++ | framework build, 새 EntrySpotActorDispatch 회귀 테스트, DeliveryDispatch E2E, YieldDispatch E2E, SpotService E2E |
| Java | Gradle/Maven framework build, 새 EntrySpotActorDispatch 회귀 테스트, Java DeliveryDispatch 또는 actor sample, YieldDispatch E2E |
| Kotlin | Kotlin E2E build, 새 EntrySpotActorDispatch 회귀 테스트, DeliveryDispatch 또는 actor sample, YieldDispatch E2E |
| Node | package build/test, 새 EntrySpotActorDispatch 회귀 테스트, DeliveryDispatch.Ts sample, YieldDispatch E2E |

실제 명령은 각 언어의 현재 runner 스크립트를 확인한 뒤 plan 실행 시점에 고정한다. runner 이름을 문서에
추측해서 박지 않는다.

## 문서 변경 계획

### 정식 문서 수정 계획

이 계획을 실행할 때는 `framework/doc/plan/`과 draft 문서를 제외한 정식 문서도 함께 고친다. 구현이 끝났는데
guide와 spec이 예전 Entry Spot-wide serial 모델을 말하면 사용자는 어떤 실행 순서를 믿어야 하는지
판단할 수 없다.

공통 문서 수정 대상:

| 문서 | 수정 내용 |
|------|-----------|
| `framework/doc/framework/common/spec/actor-model.ko.md` | 기존 설명이 `session-actor-dispatch.ko.md`의 actor별 mailbox 계약과 충돌하지 않는지 확인하고, 필요하면 연결 설명만 보강한다. 새 계약처럼 다시 쓰지 않는다. |
| `framework/doc/framework/common/spec/interaction-model.ko.md` | Entry Spot actor packet이 Entry Spot application callback 전체 직렬화 규칙에 포함된 것처럼 읽히는 문장을 분리한다. Entry Spot lifecycle/route/subscription과 Entry Spot actor packet의 실행 단위를 따로 설명한다. |
| `framework/doc/framework/common/spec/framework-api.ko.md` | timer 설명과 actor packet 설명이 서로 섞이지 않게 정리한다. Entry Spot actor packet은 actor mailbox 기준, user Spot packet/timer는 Spot 실행 queue 기준으로 구분한다. |
| `framework/doc/framework/common/spec/session-actor-dispatch.ko.md` | 이미 공통 계약을 담고 있으므로, 새 회귀 테스트 이름과 완료 기준을 추가할 위치만 보강한다. 기존 계약 문장을 바꾸는 작업은 최소화한다. |
| `framework/doc/framework/common/e2e/` | 공통 E2E 문서에 `EntrySpotActorDispatch` 또는 동등한 scenario를 추가한다. 같은 actor 순서와 서로 다른 actor 병렬 진행을 모두 요구한다. |
| `framework/doc/framework/common/sample/` | Entry Spot은 admission/ingress, user Spot은 공유 domain state라는 설명을 sample별로 맞춘다. |

언어별 수정 대상:

| 언어 | 문서 | 수정 내용 |
|------|------|-----------|
| .NET | `framework/doc/framework/dotnet/guide/03-concepts.ko.md` | “같은 Spot에 들어오는 actor callback은 하나의 실행 줄”처럼 Entry Spot actor packet까지 포함하는 문장을 정정한다. |
| .NET | `framework/doc/framework/dotnet/guide/05-spot.ko.md` | Entry Spot timer와 Entry Spot actor packet이 같은 실행 queue라고 쓰인 문장은 별도 timer 정합성 항목으로 표시하거나, actor packet 부분을 분리한다. |
| .NET | `framework/doc/framework/dotnet/guide/06-actor-spot.ko.md` | 실행 직렬화 표에서 Entry Spot actor packet을 Entry Spot 단일 실행 큐가 아니라 actor별 mailbox로 바꾼다. lifecycle callback은 Entry Spot 실행 큐로 남긴다. |
| .NET | `framework/doc/framework/dotnet/guide/samples/spot-samples.ko.md` | Entry Spot actor handler와 user Spot actor handler의 실행 단위 설명을 새 계약에 맞춘다. |
| .NET | `framework/doc/framework/dotnet/guide/samples/bingo-game-sample.ko.md` | Entry Spot은 lobby/admission만 맡고 room 상태는 room Spot이 맡는다는 설명을 actor별 mailbox 계약과 연결한다. |
| .NET | `framework/doc/framework/dotnet/guide/samples/tictactoe-game-sample.ko.md` | Entry Spot actor handler와 game room Spot handler가 같은 직렬화 모델인 것처럼 읽히는 문장을 분리한다. |
| .NET | `framework/doc/framework/dotnet/guide/samples/deliverydispatch-sample.ko.md` | DeliveryDispatch에서 Entry Spot actor handler가 courier actor별 상태만 만지는 예제로 정리한다. |
| .NET | `framework/doc/framework/dotnet/spec/aspnet-core-spot.ko.md` | Entry Spot actor packet이 Entry Spot 실행 queue라고 쓰인 계약, 표, 회귀 테스트 이름을 actor별 mailbox 계약으로 바꾼다. Entry Spot timer 문장은 별도 정합성 항목으로 분리한다. |
| .NET | `framework/doc/framework/dotnet/spec/aspnet-core-actor.ko.md` | actor lifecycle, Entry Spot, session bind 설명이 actor별 mailbox 계약과 맞는지 확인한다. |
| .NET | `framework/doc/framework/dotnet/spec/session-actor-dispatch.ko.md` | Entry Spot actor packet queue 표와 회귀 테스트 이름을 actor별 mailbox 계약으로 정리한다. |
| .NET | `framework/doc/framework/dotnet/spec/handler-interfaces.ko.md` | Entry Spot actor packet 실행 문맥 설명이 전역 직렬 실행으로 읽히는 부분을 actor별 mailbox 기준으로 정리한다. |
| .NET | `framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md` | 기존 Entry Spot actor mailbox dispatch 설명과 새 회귀 테스트 이름을 실제 구현 결과에 맞춘다. |
| C++ | `framework/doc/framework/cpp/spec/cpp-spot.ko.md` | Entry Spot actor packet은 core actor ordering 또는 actor별 mailbox 기준이라는 설명을 유지하고, Entry Spot timer 문장과 섞이지 않게 분리한다. |
| C++ | `framework/doc/framework/cpp/spec/actor-gateway-session-relay.ko.md` | session actor dispatch에서 Entry Spot actor packet 실행 단위가 core actor ordering과 맞는지 확인한다. |
| C++ | `framework/doc/framework/cpp/spec/handler-interfaces.ko.md` | Entry Spot timer와 actor packet 실행 줄을 같은 것으로 설명하는 문장이 있으면 actor packet 부분을 분리한다. |
| C++ | `framework/doc/framework/cpp/guide/08-spot.ko.md`, `framework/doc/framework/cpp/guide/09-actor-session.ko.md` | guide 예제와 설명이 Entry Spot-wide serial 실행을 전제로 하지 않게 정리한다. |
| Java | `framework/doc/framework/java/spec/spring-boot-spot.ko.md` | Entry Spot timer와 actor packet이 같은 실행 줄이라고 쓰인 문장을 분리한다. |
| Java | `framework/doc/framework/java/spec/spring-boot-actor-session.ko.md` | session actor dispatch의 Entry Spot actor packet 실행 단위를 actor별 executor/mailbox로 설명한다. |
| Java | `framework/doc/framework/java/internals/behavior-matrix.ko.md` | `Entry Spot actor packet` 행이 Entry Spot 실행 줄로 되어 있으면 actor별 mailbox 기준으로 바꾼다. |
| Java | `framework/doc/framework/java/internals/regression-test-matrix.ko.md` | 기존 회귀 테스트 설명이 Entry Spot 실행 queue를 전제로 하면 새 회귀 테스트 이름과 기대값으로 갱신한다. |
| Java | `framework/doc/framework/java/guide/05-spot.ko.md`, `framework/doc/framework/java/guide/06-actor-session.ko.md` | handler bean이 서로 다른 actor에 대해 병렬 호출될 수 있음을 설명하고, Entry Spot mutable state 의존을 피하라고 쓴다. |
| Kotlin | `framework/doc/framework/kotlin/guide/05-spot.ko.md`, `framework/doc/framework/kotlin/guide/06-actor-session.ko.md` | suspending Entry Spot actor handler가 actor별 mailbox 기준으로 실행된다는 점과 공유 mutable state 주의점을 설명한다. |
| Node | `framework/doc/framework/node/spec/nestjs-actor.ko.md` | actor별 mailbox 계약의 기준 문서로 유지하고, 구현 결과와 회귀 테스트 이름만 맞춘다. |
| Node | `framework/doc/framework/node/spec/session-actor-dispatch.ko.md` | `Entry Spot actor packet | Entry Spot 실행 queue` 표와 반대 테스트명을 actor별 mailbox 계약으로 정리한다. |
| Node | `framework/doc/framework/node/spec/handler-interfaces.ko.md` | 이미 같은 계약을 담은 문장이 유지되는지 확인하고, 새 회귀 테스트 이름을 필요한 곳에만 보강한다. |
| Node | `framework/doc/framework/node/guide/` | sample guide에서 Entry Spot actor handler에 `yield(...)`를 기본 사용법처럼 소개하지 않도록 정리한다. |

수정하지 않는 위치:

- `framework/doc/plan/`
- `framework/doc/**/draft/`
- `framework/languages/<lang>/doc/`

정식 문서를 고칠 때는 구현 결과와 회귀 테스트 이름이 확정된 뒤 반영한다. 구현 전에 정식 spec에 아직
없는 동작을 새로 쓰지 않는다. 이번 작업은 이미 공통 spec에 있는 Entry Spot actor dispatch 계약을
언어별 문서에 맞추는 것이므로, 새 API 초안을 정식 spec에 섞는 작업이 아니다.

### `Yield` 문서

`Yield` 문서는 삭제하지 않는다. 대신 사용 지침을 다음처럼 좁힌다.

- domain Spot, user Spot, route handler처럼 Spot serial turn을 I/O 대기 중 반납해야 하는 곳에서 사용한다.
- Entry Spot actor handler는 actor별 mailbox에서 실행되므로, 다른 actor를 위해 Entry Spot turn을
  반납할 필요가 없다.
- 같은 actor 안에서 긴 I/O 대기 중 같은 actor의 다음 packet까지 interleave하고 싶다는 요구는 별도
  actor reentrancy 설계가 필요하다. 이 계획에는 포함하지 않는다.

## 호환성 검토

| 항목 | 판단 |
|------|------|
| public method signature | 변경하지 않는다. |
| handler 등록 API | 변경하지 않는다. |
| actor별 packet 순서 | 유지한다. |
| 서로 다른 actor의 Entry Spot actor handler 전역 FIFO | 더 이상 보장하지 않는다. 기존에 암묵적으로 기대한 코드가 있으면 수정 대상이다. |
| Entry Spot mutable state 접근 | actor handler에서 공유 mutable state를 직접 다루는 패턴은 금지 또는 guide 비권장으로 옮긴다. |
| `Yield` API | 유지한다. Entry Spot actor handler에서는 기본 권장 사용처에서 제외한다. |

## 구현 순서

1. 공통 actor dispatch 계약과 현재 언어별 구현 차이를 inventory로 확인한다.
2. .NET EntrySpotActorDispatch 회귀 테스트를 추가하고, 현재 구현에서 실패하거나 전역 FIFO 증거가 드러나는지 먼저 확인한다.
3. .NET 구현에서 Entry Spot actor handler의 Entry Spot serial 경유를 제거한다.
4. .NET unit/E2E/sample로 actor별 병렬성과 같은 actor 순서를 검증한다.
5. DeliveryDispatch, Bingo, SupportChat 등 .NET sample에서 Entry Spot actor handler의 `Yield` 사용과
   Entry Spot mutable state 의존을 정리한다.
6. 공통 E2E scenario를 추가하거나 기존 YieldDispatch/SpotService에 항목을 보강한다.
7. C++ EntrySpotActorDispatch 회귀 테스트를 먼저 추가하고, 구현 차이를 확인한 뒤 같은 계약으로 맞춘다.
8. Java EntrySpotActorDispatch 회귀 테스트를 먼저 추가하고, Java/Kotlin 공유 runtime 영향을 검증한다.
9. Kotlin wrapper와 회귀 테스트, E2E를 검증한다.
10. Node EntrySpotActorDispatch 회귀 테스트를 추가하고, 기존 Node spec 계약과 구현이 계속 일치하는지 확인한다.
11. 공통 문서, 언어별 spec/guide, sample 문서를 실제 구현과 맞춰 갱신한다.
12. 전체 변경을 언어별로 review하고, public contract 차이가 남으면 feature-map에 gap으로 남긴다.

## 실행 체크리스트

이 문서로 goal을 만들어 작업할 때는 아래 체크리스트를 완료 기준으로 사용한다. 항목을 건너뛰면
후속 언어 또는 문서 정합성에서 같은 문제가 다시 나온다.

### 1. Inventory

- [ ] 공통 spec의 기준 문장을 확인한다: `common/spec/session-actor-dispatch.ko.md`.
- [ ] 각 언어 runtime에서 Entry Spot actor packet이 actor별 mailbox 뒤에서 실행되는지 확인한다.
- [ ] 각 언어 runtime에서 Entry Spot-wide serial executor를 추가로 타는 경로를 찾는다.
- [ ] Entry Spot timer 관련 충돌은 별도 목록으로 분리하고, 이 작업 범위에 섞지 않는다.
- [ ] `framework/doc/plan/`, `framework/doc/**/draft/`, `framework/languages/<lang>/doc/`는 수정 대상에서 제외한다.

### 2. .NET

- [ ] 현재 실패하거나 전역 FIFO 증거를 보이는 `EntrySpotActorDispatch` 회귀 테스트를 먼저 추가한다.
- [ ] 같은 actor 순서 유지와 서로 다른 actor 병렬 시작을 같은 테스트 또는 같은 fixture에서 검증한다.
- [ ] `ZLinkEntrySpotActivation.ExecuteAsync(...)` / `_serial` 경유 제거 전후로 `ZLinkSpotAmbientContext`가 유지되는지 검증한다.
- [ ] DI scope, codec registry, handler executor, error propagation 경로가 유지되는지 테스트한다.
- [ ] DeliveryDispatch sample에서 Entry Spot actor handler가 actor별 상태만 만지는지 확인한다.
- [ ] Bingo, SupportChat, TicTacToe sample에서 Entry Spot mutable state 의존과 `Yield` 기본 사용 예시를 점검한다.
- [ ] .NET 정식 문서와 internals regression matrix를 구현 결과에 맞춘다.
- [ ] `dotnet build framework/languages/dotnet/Zlink.Framework.sln`와 관련 unit/E2E/sample runner를 실행한다.

### 3. C++

- [ ] C++ runtime의 Entry Spot actor packet 실행 경로가 core actor ordering 또는 actor별 dispatch lane 기준인지 확인한다.
- [ ] `EntrySpotActorDispatch` 회귀 테스트를 먼저 추가한다.
- [ ] 같은 actor 순서 유지와 서로 다른 actor 병렬 시작을 검증한다.
- [ ] DeliveryDispatch, YieldDispatch, SpotService 관련 runner를 실행한다.
- [ ] C++ spec/guide/internals 문서를 구현 결과에 맞춘다.

### 4. Java

- [ ] Java runtime에서 Entry Spot actor handler가 Entry Spot-wide executor나 mutex를 추가로 타는지 확인한다.
- [ ] `EntrySpotActorDispatch` 회귀 테스트를 먼저 추가한다.
- [ ] `CompletableFuture` 또는 framework async call 지연으로 actor A/B 병렬 시작과 같은 actor 순서를 검증한다.
- [ ] singleton handler bean에서 Entry Spot mutable state를 공유하지 않는다는 문서/샘플 기준을 확인한다.
- [ ] Java guide/spec/internals behavior matrix와 regression matrix를 구현 결과에 맞춘다.
- [ ] Java build, YieldDispatch E2E, DeliveryDispatch 또는 actor sample runner를 실행한다.

### 5. Kotlin

- [ ] Java runtime 공유 여부를 확인하고, Kotlin wrapper에서 별도 mutex나 coroutine context가 Entry Spot-wide 직렬화를 만들지 않는지 확인한다.
- [ ] Kotlin `EntrySpotActorDispatch` 회귀 테스트를 추가한다.
- [ ] suspending handler에서 actor A/B 병렬 시작과 같은 actor 순서를 검증한다.
- [ ] Kotlin guide 문서를 actor별 mailbox 계약과 공유 mutable state 주의점에 맞춘다.
- [ ] Kotlin E2E와 DeliveryDispatch 또는 actor sample runner를 실행한다.

### 6. Node

- [ ] `nestjs-actor.ko.md`의 actor별 mailbox 계약을 기준으로 runtime 구현을 확인한다.
- [ ] `session-actor-dispatch.ko.md`의 반대 표와 테스트명을 정리한다.
- [ ] Node `EntrySpotActorDispatch` 회귀 테스트를 추가한다.
- [ ] promise 지연과 latch/marker로 actor A/B 병렬 시작과 같은 actor 순서를 검증한다.
- [ ] Node guide/spec 문서에서 Entry Spot actor handler의 `yield(...)` 기본 사용 예시를 제거하거나 정정한다.
- [ ] Node build/test, DeliveryDispatch.Ts sample, YieldDispatch E2E를 실행한다.

### 7. 최종 검증

- [ ] 다섯 언어 모두 `EntrySpotActorDispatch` 회귀 테스트가 있다.
- [ ] 다섯 언어 모두 같은 actor packet 순서를 보존한다.
- [ ] 다섯 언어 모두 서로 다른 actor의 Entry Spot actor packet이 Entry Spot-wide serial queue 때문에 대기하지 않는다.
- [ ] Entry Spot actor handler에서 `Yield`를 기본 사용법으로 소개하는 sample/doc가 없다.
- [ ] domain Spot과 user Spot의 `Yield` 계약은 변경되지 않았다.
- [ ] 정식 문서가 구현 결과와 맞고, plan/draft/languages doc 제외 규칙을 지켰다.
- [ ] 충돌이 남은 Entry Spot timer 문장은 별도 정합성 작업으로 분리했다.
- [ ] `git diff --check`를 통과했다.

## 완료 기준

- 다섯 언어에서 Entry Spot actor packet handler가 actor별 dispatch mailbox를 기준으로 실행된다.
- 같은 actor packet 순서가 깨지지 않는다.
- 서로 다른 actor packet이 Entry Spot serial queue 하나 때문에 대기하지 않는다.
- Entry Spot actor handler 예제에서 `Yield`를 기본 사용법으로 소개하지 않는다.
- domain Spot과 user Spot의 `Yield` 계약은 유지되고 기존 YieldDispatch 검증이 통과한다.
- DeliveryDispatch sample이 새 실행 모델에서 통과한다.
- 다섯 언어에 EntrySpotActorDispatch 회귀 테스트가 추가되어 통과한다.
- 공통 spec/guide와 언어별 문서가 실제 구현과 맞다.
- public API를 새로 추가하지 않는다.
- Entry Spot 공유 mutable state에 의존하는 sample 코드는 남기지 않는다.
- Entry Spot timer 계약은 이 계획에 섞지 않고, 충돌이 남으면 별도 정합성 작업으로 분리한다.

## 고정 결정

아래 항목은 이번 계획에서 다시 설계하지 않는다. 구현과 문서 작업은 이 결정을 전제로 진행한다.

| 항목 | 결정 |
|------|------|
| Entry Spot actor handler의 `entrySpot` 인자 | 유지한다. handler는 Entry Spot context와 readonly helper/config에 접근할 수 있어야 한다. 바뀌는 것은 handler 실행 queue이지 handler 인자 계약이 아니다. |
| Entry Spot lifecycle callback | actor별 mailbox로 옮기지 않는다. lifecycle callback은 Entry Spot 자체의 admission, membership, node 상태를 다루는 별도 흐름이다. actor packet dispatch 정합성과 섞지 않는다. |
| 같은 actor 안의 packet interleaving | 허용하지 않는다. 같은 actor의 packet은 actor mailbox에서 순서대로 처리한다. |
| Entry Spot actor handler 전역 FIFO 의존 코드 | 공통 계약과 맞지 않는 코드로 보고 수정한다. 여러 actor가 공유하는 업무 상태는 user Spot/domain Spot으로 옮기거나, 명시적 동기화 책임을 업무 코드가 소유하게 한다. |
