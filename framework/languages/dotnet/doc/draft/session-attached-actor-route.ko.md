<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft -- ZLink Framework .NET Registry-Backed Routing Defaults](./registry-backed-routing-defaults.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [Actor](../spec/aspnet-core-actor.ko.md) | [Session Actor Dispatch](../spec/session-actor-dispatch.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [Regression Matrix](../internals/regression-test-matrix.ko.md)

# Draft -- ZLink Framework .NET Session-Attached Actor Route

> 이 문서는 **구현 전 초안**이다.
> 아직 공개 계약[^public-contract]이 아니며, session 이 actor 에 붙은 뒤
> actor node rid 를 어떻게 보관하고 갱신해야 하는지 정리한다.

## 1. 목적

session actor dispatch 에서 session 은 client stream 으로 들어온 packet 을 이미
붙어 있는 actor 로 전달한다. 이 경로는 message hot path[^hot-path]다. 따라서
매 packet 마다 actor id 를 route resolver 에 넘겨 actor node rid 를 다시 찾으면
안 된다.

이 초안의 목표는 세 가지다.

1. `IZLinkActorPlayRouteResolver` 의 역할을 session relay 경로에서 분리한다.
2. session 이 actor 에 attach 할 때 actor route snapshot[^route-snapshot] 을 저장하는
   규칙을 정한다.
3. actor node rid 가 바뀌었을 때 session 에 붙어 있는 actor route 를 갱신하는 기능과
   회귀 테스트 기준을 정한다.

## 2. 현재 문제

현재 구현과 샘플은 session message 처리 중 actor route 를 다시 조회할 수 있다.

- sample session handler 가 `IZLinkActorPlayRouteResolver.ResolvePlayRouteAsync(...)`
  를 직접 호출한다.
- 그 뒤 `BindActorHandleAsync(...)` 로 actor handle 을 얻는 과정에서도 framework 가
  resolver 를 다시 사용할 수 있다.
- `SessionActorRouteCache` 는 route 를 비교하고 저장하지만, 실제 relay route 의 단일
  기준은 아니다.

이 구조에서는 사용자가 보기에는 route cache 가 있는 것처럼 보이지만, 실제로는
message 처리마다 외부 route 조회가 섞일 수 있다. actor 위치 조회가 느리거나 일시적으로
흔들리면 이미 attach 된 session relay 까지 영향을 받는다. 또한 actor 위치 변경을
명시적인 상태 전이로 다루지 못하고, 다음 message 에서 우연히 새 resolver 결과를 읽는
방식이 된다.

## 3. 결정

session actor dispatch 에서는 `IZLinkActorPlayRouteResolver` 를 사용하지 않는다.

session 은 actor 에 attach 할 때 다음 값을 route snapshot 으로 저장한다.

- actor id
- actor type
- router channel id
- target node rid
- actor generation
- session binding token

여기서 route snapshot 은 framework 내부에 저장되는 attached actor 상태다.
`ZLinkActorRoute` 는 그중 router channel id, target node rid, actor generation 만 담는다.
session binding token 은 actor-session binding cleanup 과 stale disconnect 방어용으로
별도 저장한다. 이 token 을 application 이 만들거나 route 값에 넣지 않는다.

session 에 attach 되는 route 는 concrete route 여야 한다. `ActorGeneration == 0` 은
unchecked route 이므로 session attach 입력과 route update 입력에서 거부한다. session 이
unchecked route 를 받아들이면 stale route 를 구분할 수 없고, actor 재생성이나 이동 뒤
이전 route 로 relay 할 수 있기 때문이다.

`ActorGeneration` 은 core actor ref 의 `generation` 과 같은 값이다. 같은 actor id 가
destroy 후 재생성되거나 다른 node 의 새 actor slot 으로 이동하면 새 concrete actor
generation 을 가진다. framework 는 actor generation 으로 오래된 attached ref 가 새 actor
slot 으로 잘못 갱신되는 것을 막는다.

초기 route snapshot 은 actor 를 생성하거나 확인한 쪽에서 session 으로 돌려준다. 예를
들어 session 이 Play 서버에 `EnsurePlayerActor` 요청을 보내면, 응답은 actor id 와 함께
현재 actor route snapshot 을 포함해야 한다. session 은 이 값을 attach 입력으로 넘길
뿐이고, actor id 로 route 를 다시 조회하지 않는다.

`RelayToActorAsync(...)` 는 저장된 actor ref 의 route snapshot 을 사용한다. 이 호출은
resolver 를 호출하지 않는다. route 가 없으면 framework 는
`ZLinkFrameworkErrorKind.ActorRouteNotFound` 로 실패해야 한다. 숨은 fallback 으로
resolver 를 호출하면 안 된다.

actor node rid 가 바뀌면 route 변경을 아는 runtime 또는 backend adapter 가 session
binding 쪽에 route update 를 전달한다. session 은 새 generation 이 현재 generation
보다 클 때만 attached actor route 를 교체한다. 이전 update 가 늦게 도착해도 새 route
를 되돌리지 않기 위해서다.

## 4. `IZLinkActorPlayRouteResolver` 의 사용 범위

`IZLinkActorPlayRouteResolver` 는 여전히 필요하다. 다만 session relay 에 쓰는
resolver 가 아니다.

이 resolver 는 아직 구현되지 않은 **backend 에서 actor 로 메시지를 보내는 기능**에서
사용한다. 그 경로는 특정 client stream session 에 붙어 있는 actor handle 이 없다.
따라서 backend service 는 actor id 만 알고 있고, actor runtime 이 어느 node 에 있는지
찾아야 한다. 이때 `IZLinkActorPlayRouteResolver` 가 actor id 를 play/runtime route 로
바꾼다.

이 resolver 도 `ZLinkActorRoute` 를 반환하므로 actor generation 을 함께 돌려줘야 한다.
backend actor messaging 은 이 generation 으로 오래된 route 결과와 새 route update 를
구분할 수 있다. session relay 는 같은 타입을 route snapshot 값으로 받을 수 있지만,
그 값을 resolver 에서 직접 조회하지 않는다. resolver 가 반환하는 `ActorGeneration` 도
`0` 이면 안 된다.

정리하면 역할은 다음과 같이 나뉜다.

| 경로 | route 기준 | `IZLinkActorPlayRouteResolver` 사용 |
|------|------------|--------------------------------------|
| session -> attached actor relay | attach 시점에 저장한 route snapshot | 사용하지 않음 |
| actor -> current client session push | actor-session binding store | 사용하지 않음 |
| backend service -> actor messaging | actor id 기반 play/runtime route 조회 | 사용 |
| actor -> spot name/rid 호출 | `IZLinkSpotRouteResolver` | 사용하지 않음 |

이 구분이 있어야 `IZLinkActorPlayRouteResolver` 가 "모든 actor 메시징의 숨은 조회
경로"가 되지 않는다. resolver 는 session 상태가 없는 backend 호출에서만 필요하다.

## 5. Session attach 모델

현재 `BindActorHandleAsync(actorId, actorType, ...)` 는 actor route 를 어디서 얻는지
호출자에게 보이지 않는다. 구현이 resolver 를 내부에서 호출하면 session hot path 에
route 조회가 다시 들어온다.

구현 단계에서는 `ZLinkActorRoute` 에 actor generation 을 추가하고, session attach
표면에는 route 를 받는 `BindActorHandleAsync(...)` overload 를 추가한다. attach 입력은
route snapshot 을 명시적으로 받아야 한다.

```csharp
public readonly record struct ZLinkActorRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    ulong ActorGeneration);
```

```csharp
ValueTask<IZLinkActorRef> BindActorHandleAsync(
    string actorId,
    string actorType,
    ZLinkActorRoute route,
    CancellationToken cancellationToken = default);
```

`ZLinkActorRoute` 는 actor id 나 session binding token 이 아니라 실제 relay 위치를 담는
public 값이다. session handler 가 이 값을 직접 계산한다는 뜻은 아니다. 보통은 actor 를
생성하거나 확인하는 Play/API 서버 응답에서 받은 값을 그대로 넘긴다. route update 기능은
public 값이 아니므로 여기에 포함하지 않는다.

`ZLinkActorRoute` 의 생성자는 `ActorGeneration` 을 필수로 받는다. 따라서 구현 단계에서는
모든 resolver 구현체, sample route store, test double 의 `new ZLinkActorRoute(...)`
호출을 함께 갱신해야 한다. resolver 가 generation 을 모르면 session attach 용 route 를
반환할 수 없다.

route 를 받는 `BindActorHandleAsync(...)` overload 는 `route.ActorGeneration == 0` 이면
`ZLinkFrameworkErrorKind.ActorRouteNotFound` 로 실패한다. 의미상 unchecked route 거부에
가깝지만, 첫 구현에서는 error kind 확장을 피하고 기존 `ActorRouteNotFound` 를 재사용한다.
unchecked route 로 actor ref 를 만들면 이후 stale update guard 가 동작하지 않기 때문이다.

`IZLinkActorRef` 는 public 으로 route 를 노출하지 않는다. framework 내부 구현은 actor ref
가 mutable route state 를 참조하게 만들고, `RelayToActorAsync(...)` 는 매 호출마다 그
state 의 최신 snapshot 을 읽는다. 이렇게 하면 session handler 는 같은 actor ref 를 계속
사용하면서도, actor 위치 변경 뒤에는 새 target node rid 로 relay 된다.

`actorId` 만 받아 내부 resolver 로 route 를 찾는 표면은 session relay 기본 경로가 될 수
없다.

기존 `BindActorHandleAsync(actorId, actorType, ...)` 는 호환성 경로로만 남긴다.
이 경로는 local actor bind 에만 쓸 수 있고, remote actor route 를 resolver 로 찾지
않는다. local actor 가 없으면 `ActorRouteNotFound` 로 실패해야 한다. local actor 가
있으면 framework 는 local runtime 이 가진 concrete actor generation 으로 route state 를
만든다. local compatibility 경로도 `ActorGeneration == 0` actor ref 를
만들면 안 된다.

구현 순서는 다음으로 고정한다.

1. route 를 받는 `BindActorHandleAsync(...)` overload 를 추가하고 sample 을 새 overload 로 옮긴다.
2. 기존 API 는 local-only compatibility 경로로 제한한다.
3. 기존 API 안에서 `IZLinkActorPlayRouteResolver` 를 호출하는 코드를 제거한다.
4. 정식 spec 에서 session actor dispatch 의 기본 API 를 새 attach 표면으로 바꾼다.

## 6. Route update 모델

actor node rid 변경은 별도 기능으로 다룬다. session 이 다음 message 에서 resolver 를
다시 호출해서 우연히 새 위치를 알게 되는 구조는 허용하지 않는다.

route update 는 다음 정보를 가져야 한다.

- actor id
- router channel id
- 새 target node rid
- expected actor generation
- new actor generation

`expectedActorGeneration == 0` 이거나 `newActorGeneration == 0` 인 update 는 거부한다.
route update 는 concrete actor route 로만 적용한다.

session binding 쪽 내부 기능은 다음 의미를 가져야 한다.

```csharp
ValueTask UpdateAttachedActorRouteAsync(
    string actorId,
    string routerChannelId,
    RoutingId targetNodeRid,
    ulong expectedActorGeneration,
    ulong newActorGeneration,
    CancellationToken cancellationToken);
```

이 API 는 초안용 형태이며 public application API 가 아니다. actor 위치 변경을 감지한
framework runtime 또는 backend adapter 가 내부에서 호출한다. session handler 와
application service 는 이 기능을 몰라도 된다. 기본 원칙은 route update 가
actor-session binding 을 소유한 framework 내부 한 곳으로 모이는 것이다.

적용 범위는 actor id 와 expected actor generation 기준이다. 같은 framework runtime 안에서
해당 actor id 에 attach 된 live `IZLinkActorRef` 중 현재 actor generation 이
`expectedActorGeneration` 과 같은 route state 만 갱신한다. session binding token 은
cleanup 과 stale disconnect 방어에 계속 쓰지만, route update 대상을 하나의 session token
으로 좁히지 않는다. actor 위치는 actor slot 단위 상태이므로 같은 actor id 와 같은 actor
generation 에 붙은 session ref 들은 같은 최신 route 를 공유해야 한다.

동시성 규칙은 다음과 같다.

- `expectedActorGeneration == 0` 또는 `newActorGeneration == 0` update 는 실패한다.
- 현재 actor generation 이 `expectedActorGeneration` 과 같으면 target node rid 와 actor
  generation 을 `newActorGeneration` 으로 교체한다.
- 현재 actor generation 이 `expectedActorGeneration` 과 다르면 stale update 로 무시한다.
- 현재 route 가 이미 같은 target node rid 와 `newActorGeneration` 을 가리키면 idempotent
  하게 성공한다.
- 같은 expected generation 에서 서로 다른 target node rid 나 new generation 으로 가는
  update 가 동시에 들어오면 충돌로 보고 하나만 적용한다.
- route 교체는 relay 가 읽는 snapshot 기준으로 atomic 해야 한다.

`IZLinkActorRef` public surface 는 단순하게 유지한다. 내부 구현은 actor ref 를 새 객체로
교체하지 않고, ref 가 참조하는 내부 route state 를 atomic 하게 교체한다. session state 나
sample cache 가 기존 actor ref 를 계속 들고 있어도 route update 가 적용되어야 하기
때문이다. 이렇게 하면 route 변경 지식이 session handler 로 새어 나오지 않는다.

## 7. 기대 흐름

session 인증 또는 입장 흐름에서 application 은 actor 를 준비한다. 이 단계에서 actor
route snapshot 을 얻는다. 예를 들어 API 서버나 Play 서버의 ensure actor 응답이 actor
id 와 함께 route snapshot 을 돌려준다.

그 뒤 session 은 한 번만 actor handle 을 attach 한다. 이후 같은 session 의 game packet,
match packet, input packet 은 저장된 actor ref 로 바로 relay 된다.

actor 가 다른 node 로 이동하거나 runtime route 가 바뀌면 route owner 가 update 를
발행한다. framework 는 actor-session binding 을 찾아 attached actor route 를 갱신한다.
다음 relay 는 새 target node rid 를 사용한다.

이 흐름에서 session handler 는 다음 일을 하지 않는다.

- actor id 로 play route 를 직접 resolve 하지 않는다.
- route resolver 를 DI 로 받지 않는다.
- message 마다 actor route 를 다시 계산하지 않는다.
- route update 를 직접 polling 하지 않는다.

## 8. 회귀 테스트

현재 구현이 유지해야 하는 테스트는 다음과 같다.

| 테스트 케이스 | 확인 기준 |
|-------------|-----------|
| `StreamIntegrationTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | request sequence 기반 reply routing 은 route snapshot 구조에서도 유지된다. |
| `StreamIntegrationTests.SessionActorDispatch_Uses_Multipart_Routed_Actor_Dispatch` | session -> actor routed multipart framing 은 바뀌지 않는다. |
| `StreamIntegrationTests.SessionProxy_Uses_Multipart_Routed_Client_Push` | actor -> client push 는 actor-session binding 을 계속 사용한다. |
| `SpotIntegrationTests.ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream` | stale disconnect 가 최신 actor-session binding 을 지우지 않는다. |

## 9. 구현 후 추가 테스트 계획

구현할 때 다음 테스트를 추가한다.

| 예정 항목 | 확인 기준 |
|-----------|-----------|
| `StreamIntegrationTests.SessionActorBind_Does_Not_Resolve_PlayRoute` | session attach 와 relay 중 `IZLinkActorPlayRouteResolver` 가 호출되지 않는다. resolver 를 등록해도 call count 는 0 이다. |
| `StreamIntegrationTests.SessionActorBind_Rejects_Unchecked_ActorGeneration` | route 를 받는 bind overload 에 `ActorGeneration == 0` 을 넘기면 `ActorRouteNotFound` 로 실패한다. |
| `StreamIntegrationTests.SessionActorBind_WithoutRoute_Is_LocalOnly` | 기존 `BindActorHandleAsync(actorId, actorType, ...)` 는 local actor 가 있을 때만 성공하고 remote resolver 를 호출하지 않는다. |
| `StreamIntegrationTests.SessionActorRelay_Reuses_Bound_Route_For_Multiple_Messages` | 같은 session 에서 여러 packet 을 relay 해도 attached actor ref 의 route snapshot 을 재사용한다. |
| `StreamIntegrationTests.SessionActorRouteUpdate_Changes_Attached_TargetNodeRid` | route update 후 같은 `IZLinkActorRef` relay 가 새 target node rid 로 전송된다. |
| `StreamIntegrationTests.SessionActorRouteUpdate_Ignores_Stale_ExpectedActorGeneration` | expected actor generation 이 현재 attached ref 와 다르면 route 를 바꾸지 않는다. |
| `StreamIntegrationTests.SessionActorRouteUpdate_Allows_Idempotent_Same_Target` | 같은 target node rid 와 actor generation 을 가리키는 update 는 중복 도착해도 성공한다. |
| `StreamIntegrationTests.SessionActorRouteUpdate_Rejects_Conflicting_Target_For_Same_ExpectedGeneration` | 같은 expected actor generation 에서 서로 다른 target node rid 나 new generation 으로 가는 update 가 동시에 들어오면 하나만 적용한다. |
| `StreamIntegrationTests.SessionActorRelay_Fails_When_Bound_Route_Is_Missing` | attach route 없이 relay 를 시도하면 resolver fallback 없이 명확한 오류가 난다. |
| `RegistrationValidationTests.SessionActorDispatch_Does_Not_Require_ActorPlayRouteResolver` | session actor dispatch 구성은 `IZLinkActorPlayRouteResolver` 등록을 요구하지 않는다. |
| `SampleRegressionTests.Bingo_SessionHandlers_Do_Not_Inject_ActorPlayRouteResolver` | Bingo session handler 에 `IZLinkActorPlayRouteResolver` 주입과 직접 route resolve 호출이 없다. |
| `SampleRegressionTests.TicTacToe_SessionHandlers_Do_Not_Inject_ActorPlayRouteResolver` | TicTacToe session gateway session handler 에도 같은 회귀가 없다. |
| `BackendMessagingTests.BackendActorMessaging_Uses_ActorPlayRouteResolver` | backend -> actor messaging 기능이 추가된 뒤 resolver 사용 위치가 backend messaging 경로로 제한된다. |
| `BackendMessagingTests.BackendActorMessaging_Rejects_Unchecked_Resolved_Route` | resolver 가 `ActorGeneration == 0` route 를 반환하면 backend -> actor messaging 은 전송하지 않고 실패한다. |

## 10. Framework 문서 반영 계획

구현이 끝나면 이 draft 내용을 아래 문서에 나누어 반영한다.

| 문서 | 반영 내용 |
|------|-----------|
| `framework/languages/dotnet/doc/spec/session-actor-dispatch.ko.md` | session relay 는 attached route snapshot 을 사용하고 `IZLinkActorPlayRouteResolver` 를 호출하지 않는다는 계약, route 를 받는 `BindActorHandleAsync(...)` overload, 내부 route update 규칙 |
| `framework/languages/dotnet/doc/spec/aspnet-core-actor.ko.md` | actor lifecycle 에서 session attach 와 actor route update 를 분리해서 설명하고, resolver 의 backend messaging 용도를 명시 |
| `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md` | `ZLinkActorRoute.ActorGeneration` 과 route 를 받는 `BindActorHandleAsync(...)` overload 를 인터페이스 기준 문서에 반영. 내부 route update 기능은 public interface 로 싣지 않는다. |
| `framework/languages/dotnet/doc/spec/aspnet-core-stream.ko.md` | stream session context 가 actor route 를 저장한 handle 을 통해 relay 한다는 설명 |
| `framework/languages/dotnet/doc/internals/behavior-matrix.ko.md` | resolver 미등록 session actor dispatch, route update, stale actor generation, missing route 오류의 허용 / 비허용 동작 |
| `framework/languages/dotnet/doc/internals/regression-test-matrix.ko.md` | §8 의 유지 테스트와 §9 의 신규 테스트 계획을 release gate 에 추가 |
| `framework/languages/dotnet/doc/internals/lifecycle-and-failure-semantics.ko.md` | actor route update 와 session disconnect, reconnect, stale expected actor generation update 의 순서 규칙 |
| `framework/languages/dotnet/doc/guide/samples/bingo-game-sample.ko.md` | session handler 가 resolver 를 받지 않고 attach route 를 저장한 뒤 relay 하는 샘플 흐름 |
| `framework/languages/dotnet/doc/guide/samples/tictactoe-game-sample.ko.md` | TicTacToe session gateway 도 같은 attach route 흐름으로 수정 |
| `framework/languages/dotnet/doc/README.ko.md` | draft 링크를 구현 완료 후 정식 문서 링크로 정리 |

## 11. Framework 공통 문서 반영 계획

이 변경은 `.NET` adapter 안의 구현 세부만이 아니다. Framework 공통 문서도
`actor route resolver` 의 용도를 session relay 에서 분리해서 설명해야 한다.

| 공통 문서 | 반영 내용 |
|-----------|-----------|
| `doc/spec/draft/framework-route-resolvers.ko.md` | actor route resolver 는 backend service -> actor messaging 처럼 attached session actor ref 가 없는 호출에서만 actor id 를 node rid 로 바꾸는 역할로 정리한다. session gateway hot path 의 route 조회 수단으로 설명하지 않는다. |
| `doc/spec/draft/spot-actor-dispatch.ko.md` | actor route row 의 node rid, generation, route sync 의미를 session-attached actor route update 의 source 로 연결한다. session relay 는 actor id resolve 가 아니라 attached route snapshot 을 사용한다는 점을 반영한다. |
| `doc/spec/draft/discovery-owner-bound-routes.ko.md` | actor generation 과 owner-bound 갱신이 stale update 를 막는 기준이 되는지 확인하고, 필요한 경우 actor route update 전파 의미를 추가한다. |
| `doc/spec/core/service/discovery.ko.md` | `ResolveActor` 계열 계약이 backend actor messaging 용 route 조회라는 점을 분명히 하고, session relay 의 필수 조회 경로처럼 읽히지 않게 정리한다. |
| `doc/spec/core/socket/stream.ko.md` | stream actor bind 성공 뒤 active route sync 가 켜질 때 생성되는 actor route 가 attached session route update 의 입력이 될 수 있음을 설명한다. |
| `doc/guide/07-4-actor.ko.md` | 사용자 가이드에서 session handler 가 actor route resolver 를 직접 호출하지 않고 attach 된 actor handle 로 relay 한다는 사용 모델을 설명한다. |

공통 문서에 반영할 때도 내부 route update 기능을 public application API 로 소개하지
않는다. 사용자는 actor 위치 변경 전파를 직접 호출하는 것이 아니라, framework 가 관리하는
actor lifecycle 과 route sync 의 결과로 attached actor route 가 갱신된다고 이해해야 한다.

## 12. 구현 순서

1. 현재 session actor dispatch 에서 `ResolvePlayRouteAsync(...)` 호출 지점을 제거할
   위치를 먼저 고정한다.
2. `ZLinkActorRoute.ActorGeneration` 과 route 를 받는 `BindActorHandleAsync(...)` overload 를 추가한다.
3. `ZLinkActorRef` 내부가 mutable route state 를 참조하고 relay 시점에 최신 snapshot 을 읽도록 바꾼다.
4. route update 를 actor-session binding 내부 한 곳으로 모으는 internal 기능을 추가한다.
5. expected actor generation 기반 stale update guard 를 추가한다.
6. 기존 `new ZLinkActorRoute(...)` 호출 지점 전체를 `ActorGeneration` 포함 형태로 갱신한다.
7. Bingo 와 TicTacToe sample 의 ensure actor 응답에 attach route snapshot 을 포함하고, session handler 에서 resolver 주입을 제거한다.
8. §9 의 회귀 테스트를 추가하고 `Zlink.Framework.sln` 기준으로 검증한다.
9. §10 과 §11 의 정식 spec, guide, internals 문서를 실제 구현과 맞춰 갱신한다.

## 13. 구현 시 고정할 세부 선택

다음 항목은 구현 전 다시 논의하지 않고 같은 방향으로 적용한다.

- route update 기능은 internal 로만 둔다. public application API 로 노출하지 않는다.
- actor migration 이 아직 없다면 테스트에서는 internal test double 로 route update 를
  발생시킨다. 테스트 double 도 public package 표면에 노출하지 않는다.
- 기존 `BindActorHandleAsync(...)` 는 local-only compatibility 경로로 제한한다.
- remote actor 에 붙는 session handler 와 sample 은 route 를 받는 `BindActorHandleAsync(...)` overload 만 사용한다.

[^public-contract]: 공개 계약은 구현과 테스트가 끝난 뒤 정식 spec 문서에 반영된 API 의미다.
[^hot-path]: hot path 는 요청이 자주 지나가는 실행 경로다. 이 경로에는 불필요한 외부 조회를 넣지 않는다.
[^route-snapshot]: route snapshot 은 특정 시점의 router channel id, target node rid, generation 을 묶은 값이다.
