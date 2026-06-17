<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [상위: Draft](./README.ko.md)
<!-- framework-adapter-nav:end -->

# Entry Spot Actor Destroy 구현 계획

이 문서는 **구현 전 초안**이며 **현재 공개 계약이 아니다**.
정식 공개 계약은 구현과 테스트가 끝난 뒤 공통 framework spec, 언어별 spec, guide,
sample 문서에 나누어 반영한다.

이 문서는 .NET, C++, Node.js, Java, Kotlin framework 표면에 actor destroy API와
actor lifecycle callback을 추가하고, actor 수명 종료 책임을 Entry Spot에 모으는
계획을 정리한다. 구현자는 이 문서를 작업 goal의 기준 문서로 사용한다.

## 1. 배경

현재 actor는 생성 직후 Entry Spot에 위치하고, user Spot에 join한 뒤 leave하면 다시
Entry Spot으로 돌아온다. 이 흐름은 actor의 위치 이동과 actor 객체의 수명 종료를
분리한다.

문제는 framework 공개 API에 actor 객체를 완전히 제거하는 명시적 API가 언어별로
일관되게 노출되어 있지 않다는 점이다. core와 일부 backend 포트에는 actor destroy
동작이 있지만, application이 Entry Spot에서 "이 actor는 더 이상 필요 없다"를 표현할
공통 API가 부족하다.

생성이 Entry Spot에서 시작되므로 삭제도 Entry Spot에서 끝나는 편이 책임이 분명하다.
user Spot은 room, game, stage 같은 domain 상태를 소유하고, actor를 room에서 내보내는
leave까지만 맡는다. actor 객체와 native actor ref 제거는 actor가 Entry Spot으로 돌아온
뒤 Entry Spot context를 통해 수행한다.

## 2. 목표

1. 모든 framework 언어에 Entry Spot context의 actor destroy API를 추가한다.
2. actor destroy는 actor가 Entry Spot에 있을 때만 허용한다.
3. user Spot에서 actor를 끝내야 하는 흐름은 먼저 leave로 Entry Spot에 돌려보낸 뒤
   Entry Spot에서 destroy하도록 문서화한다.
4. actor 생성 시 모든 언어에서 같은 이름의 `onCreateActor` callback을 호출한다.
5. destroy가 native actor ref, managed actor registry, actor-session binding, current
   membership, pending mailbox 상태를 함께 정리하도록 구현한다.
6. destroy는 `onLeaveActor` 또는 같은 lifecycle callback을 호출하지 않고 정리만 수행한다.
7. destroy 뒤 같은 actor id로 actor를 다시 만들 수 있어야 한다.
8. disconnect, leave, destroy의 차이를 공통 spec, 언어별 spec, guide, sample 문서에
   명확히 적는다.
9. 모든 framework 언어가 같은 의미의 공개 API, runtime, sample, 문서 회귀 테스트를
   추가한다. 함수 이름은 언어별 public API 관례를 따르되, Entry Spot context에만
   destroy API가 있고 user Spot context에는 없다는 경계를 동일하게 유지한다.

## 3. 비목표

- user Spot context에 actor destroy API를 추가하지 않는다.
- user Spot의 `leaveActor` 또는 같은 의미 API가 actor 객체를 삭제하도록 바꾸지 않는다.
- `onLeaveActor` callback 안에서 framework가 actor를 자동 destroy하지 않는다.
- stream session disconnect가 actor destroy를 자동으로 수행하도록 바꾸지 않는다.
- destroy 전용 callback을 추가하지 않는다.
- compatibility shim을 남기지 않는다. 이름이 잘못 들어간 임시 API가 있으면 정리한다.
- core C API의 actor destroy 계약 자체를 이 계획에서 새로 설계하지 않는다. framework는
  이미 존재하는 core/backend destroy 의미를 사용한다.
- remote Entry Spot에 있는 actor를 다른 node의 Entry Spot context에서 destroy하는 기능은
  이번 계획에 넣지 않는다. 현재 Entry Spot activation이 소유한 actor만 대상으로 한다.

## 3.1 현재 저장소 상태

현재 작업 tree에는 actor destroy 구현과 테스트가 여러 언어에 이미 들어와 있다. 이후
작업은 아래 상태를 기준으로 누락된 정리와 검증을 이어 간다.

| 언어 | 현재 확인한 상태 | 남은 정리 |
|------|------------------|-----------|
| `.NET` | `IZLinkEntrySpotContext.DestroyActorAsync(...)`가 있고, `IZLinkSpotContext`에는 같은 API가 없다. Contract test는 `.NET`만 `DestroyActorAsync(...)`를 허용 이름으로 다루고 `destroyActor` 변형을 금지한다. E2E에는 Entry Spot destroy, 중복 destroy, user Spot actor direct destroy 실패, leave 후 destroy, destroy 뒤 `SubmitActorByIdAsync(...)`의 `ActorRouteNotFound`, 같은 actor id 재생성 흐름이 들어와 있으며 focused 실행이 통과했다. Entry Spot outbound E2E는 attached channel client route가 준비될 때까지 기존 retry helper로 기다리게 보강했고, `Zlink.Framework.E2ETests.csproj` 전체 96개 테스트가 통과했다. Unit sample regression은 Entry Spot에서 `DestroyActorAsync(...)`, room Spot에서 `leaveActor(...)`를 사용하는지 확인한다. 같은 Unit regression은 .NET guide/spec/internals와 sample README에서 정책 밖 destroy 이름, 오래된 actor lifecycle callback 이름, disconnect 자동 destroy 문구가 다시 들어오지 않는지도 확인한다. `Zlink.Framework.UnitTests.csproj` 137개 테스트와 `samples/run_samples.sh`의 TicTacToe, Bingo, SupportChat, ShoppingMall, DeliveryDispatch, GameQuest sample이 모두 통과했다. `dotnet test framework/languages/dotnet/Zlink.Framework.sln`도 contract 28개, unit 137개, stream connector 43개, E2E 96개 테스트 통과 증거를 확보했다. | aggregate sample runner는 첫 실행에서 stream notify timeout이 한 번 있었지만, 관련 sample 단독 재실행과 aggregate 재실행은 통과했다. |
| Node.js | `ZLinkEntrySpotContext.destroyActor(...)`가 public type과 runtime context에 있고, `ZLinkSpotContext`에는 없다. actor manager/runtime test와 contract-surface test가 destroy 위치, lifecycle callback isolation, destroy 뒤 actor dispatch 거절, bound session disconnect가 actor destroy를 자동 실행하지 않는다는 점을 검증한다. destroy cleanup은 stream binding route와 session-local bound actor index를 함께 제거하고, destroy 뒤 bound session send가 `ActorSessionNotBound`로 실패하는지도 contract test로 확인한다. sample regression test는 TypeScript sample이 room leave 뒤 Entry Spot에서만 `destroyActor(...)`를 호출하고, README와 runner가 client self-check 흐름을 유지하는지 확인한다. `npm run build`, `npm run verify:coverage`, `samples/run_samples.sh`, `verify:abi-matrix`, `verify:runtime-matrix`, `verify:cross-language`가 통과했다. | cross-language smoke는 현재 root public surface가 아니라 framework 내부 테스트 진입점으로 runtime client와 transport를 생성한다. 이는 smoke 전용 검증 방식이며 public API 추가 근거로 보지 않는다. |
| Java / Kotlin | Java `ZLinkEntrySpotContext.destroyActor(...)`와 Kotlin suspending extension `ZLinkEntrySpotContext.destroyActor(actor)`가 있다. fake backend actor runtime test는 Entry Spot destroy, user Spot destroy 금지, duplicate destroy, stale instance guard, leave 후 destroy, disconnect 뒤 actor registry 유지와 나중 destroy를 검증한다. 같은 테스트는 destroy 성공 뒤 stream `unbindActor`, actor context bound session 제거, session-local actor binding index 제거, destroy 뒤 새 actor dispatch 거절도 확인한다. sample release gate는 Java/Kotlin sample의 Entry Spot destroy 위치, user Spot destroy 금지, disconnect가 leave/destroy를 호출하지 않는다는 점을 확인한다. 같은 release gate는 Java/Kotlin guide/spec/internals와 sample README에서 정책 밖 destroy 이름, 오래된 actor lifecycle callback 이름, disconnect 자동 destroy 문구가 다시 들어오지 않는지도 확인한다. Kotlin sample source는 Entry Spot lifecycle callback 안에서 Java `ZLinkAwait.await(...)` 대신 `ZLinkCoroutineRuntime.blocking { context.destroyActor(actor).await() }` 패턴을 사용한다. `samples/run_samples.sh`는 Java/Kotlin TicTacToe와 Bingo sample을 모두 통과했다. | Entry Spot lifecycle callback은 현재 sync 계약이므로 Kotlin sample에서 suspend extension을 receiver 호출로 직접 보여주지는 않는다. Kotlin 문서 정리 때 이 제약을 함께 설명해야 한다. |
| C++ | `entry_spot_context_t.destroyActor(...)`와 `spot_context_t.leaveActor(...)` public header contract test가 있다. `test_cpp_framework_spot_runtime`은 Entry Spot destroy, user Spot destroy 금지, leave 후 destroy, disconnect 뒤 actor registry 유지, gateway actor registry cleanup, 같은 actor id 재생성을 검증한다. 같은 테스트는 actor factory가 만든 actor instance를 현재 Spot의 actor packet handler에 type-erased relay로 전달하고 reply를 받는 경로도 검증한다. `test_cpp_framework_ActorGateway_actor_session_relay`는 ActorGateway registry의 bind, relay, request relay, disconnect, stale generation, Entry Spot rejoin, destroy 뒤 bound session push 실패와 actor relay 실패 경로를 검증하고, relay dispatcher hook이 있을 때 record-only 경로 대신 dispatcher를 호출하는지도 확인한다. app bootstrap은 ActorGateway relay dispatcher를 spot node runtime의 typed Spot dispatch 경로에 연결한다. TicTacToe Play host는 create-room 결과로 room id 기반 game Spot을 만들고, actor join을 해당 game Spot으로 넘긴다. ActorGateway bound session push는 stream host writer로 연결되어 room의 다른 actor에게 join, game state, game ended 알림을 보낸다. game 종료 뒤 user Spot은 actor를 `leaveActor`로 Entry Spot에 되돌리고, Entry Spot은 destroy marker가 있는 actor를 `destroyActor`로 정리한다. sample parity test는 Entry Spot에서만 destroy API를 쓰는지 확인하고, 공통 Bingo/TicTacToe sample spec이 actor 생성, room leave, Entry Spot destroy, destroy callback isolation, disconnect isolation을 계속 설명하는지도 확인한다. `test_cpp_framework_channel_messaging`은 route client request-reply도 native backend peer reply가 없으면 성공하지 않는다는 점과, 일반 channel request가 native dealer/router, hosted server receive loop, framework packet dispatcher를 거쳐 handler reply로 돌아올 수 있다는 점을 확인한다. TicTacToe runner는 Play/API 서버를 별도 process로 계속 실행한 뒤 public client로 HTTP `POST /games`, Stream Connector connect, authenticate, join, gameplay notification, winning move, game 종료 뒤 같은 actor id 재인증까지 확인한다. `framework/languages/cpp/samples/TicTacToe/run_sample.sh`는 full client/server self-check와 actor lifecycle sample gate를 통과했다. Bingo는 Session host가 실제 `bingo_session_t`를 등록하고, API/Play client channel endpoint를 명시 연결하며, Session/API/Play host가 필요한 Protobuf message type을 등록한다. Play host도 local ActorGateway, actor factory, join wiring service를 갖는다. | Stream Connector typed async wait predicate는 첫 join notification에서 raw packet wait 후 명시 decode로 우회했다. stale packet 실패와 post-destroy lookup 실패는 `test_cpp_framework_spot_runtime`으로 검증하고, public TicTacToe client는 post-destroy behavior 중 같은 actor id 재생성을 별도 stream session 재인증으로 검증한다. Bingo runner는 아직 full client/server self-check가 아니라 server role smoke와 lifecycle gate 범위다. 별도 process Session 서버에서 Play 서버 ActorGateway로 actor packet을 relay하는 remote 경로가 아직 없어서, 현재 full client 시도는 authenticate 뒤 `MatchBingoReq`에서 actor relay dispatcher 미구성 오류로 멈춘다. |

정식 문서와 sample README 범위에서 정책 밖의 callback 이름(`onActorLeft`,
`onPostActorJoined` 등)은 발견되지 않았다. 다만 오래된 draft 문서에는 이전 이름 예시가
남아 있으므로, 문서 회귀 테스트를 정리할 때 정식 문서와 draft 문서의 적용 범위를 먼저
분리해야 한다.

내부 destroy 경로는 언어별로 하나의 runtime 진입점으로 모은다. `.NET`은
`ZLinkEntrySpotActivation.DestroyActorAsync(...)`가 `ZLinkFrameworkRuntime.DestroyActorAsync(...)`
를 거쳐 `ZLinkActorSessionManager.DestroyActorAsync(...)`로 위임한다. Node.js는
`ZLinkEntrySpotActivation` context가 `DefaultZLinkActorManager.destroyActor(...)`로
위임한다. Java/Kotlin은 Entry Spot context가 `ZLinkActorRuntime.destroyFromEntrySpot(...)`
로 위임한다. C++는 `entry_spot_context_t.destroyActor_erased(...)`가 Entry Spot 위치와
generation을 판단하는 단일 구현이고, `actor_gateway_runtime_t::destroy_actor(...)`는
ActorGateway와 bound session registry cleanup hook으로만 사용한다.

## 4. 용어와 의미

| 용어 | 의미 |
|------|------|
| Entry Spot | actor가 생성 직후 머무는 기본 실행 문맥이다. 인증, 입장 대상 선택, actor 종료 같은 공통 흐름을 처리한다. |
| user Spot | application이 만든 room, game, stage 같은 domain Spot이다. actor가 join한 뒤 domain 상태를 함께 다룬다. |
| leave | actor를 user Spot에서 Entry Spot으로 되돌리는 위치 이동이다. actor 객체를 삭제하지 않는다. |
| destroy | Entry Spot에 있는 actor의 수명을 끝내고 framework/native 상태를 제거하는 동작이다. |
| disconnect | actor에 묶인 stream session 연결이 끊어진 상태다. leave나 destroy와 같은 뜻이 아니다. |
| onCreateActor | actor 객체가 runtime에 생성되었을 때 한 번 호출되는 callback이다. Spot 이동과 무관하다. |
| onJoinedActor | actor가 특정 Spot membership에 들어왔을 때 호출되는 callback이다. |
| onLeaveActor | actor가 특정 Spot membership에서 다른 Spot으로 이동하기 위해 나갈 때 호출되는 callback이다. destroy 때는 호출하지 않는다. |

## 4.1 언어별 public 이름 정책

이 기능에서 추가하거나 정리하는 framework public 이름은 두 계층으로 관리한다.
callback 이름은 sample 흐름을 언어별로 쉽게 비교할 수 있어야 하므로 같은 철자를
우선한다. 반면 context method는 언어별 framework가 이미 가진 async naming 관례와
맞춘다. 예를 들어 `.NET`은 현재 공개 표면이 `DestroyActorAsync(...)`이고, Node.js,
Java, C++, Kotlin 문서는 `destroyActor(...)`를 사용한다.

공통 의미와 언어별 이름은 아래로 고정한다.

| 의미 | .NET | Node.js / Java / Kotlin / C++ |
|------|------|------------------------------|
| actor 생성 callback | `onCreateActor` | `onCreateActor` |
| actor Spot 진입 callback | `onJoinedActor` | `onJoinedActor` |
| actor Spot 이탈 callback | `onLeaveActor` | `onLeaveActor` |
| actor disconnect callback | `onDisconnectActor` | `onDisconnectActor` |
| user Spot에서 Entry Spot으로 이동 | `leaveActor` | `leaveActor` |
| Entry Spot에서 actor 수명 종료 | `DestroyActorAsync` | `destroyActor` |

비동기 반환 타입이나 cancellation 인자는 언어별 runtime에 맞춘다. 기존 언어별 API가
위 표와 다른 이름을 사용하고 있으면 compatibility shim을 남기지 말고 위 표로 정리한다.
문서 회귀 테스트도 이 표를 기준으로 public source와 sample이 다른 이름으로 새지 않도록
막는다.

## 5. 공통 수명 흐름

actor 수명 흐름은 아래처럼 고정한다.

```text
None
  +-- create --> Entry Spot
        +-- join spot --> User Spot
              +-- leave --> Entry Spot
                    +-- destroy --> None
```

규칙은 다음과 같다.

1. actor 생성은 actor factory와 Entry Spot join 경로를 통해 시작한다.
   생성이 완료되면 framework는 `onCreateActor`를 한 번 호출한다.
2. actor가 user Spot에 있으면 destroy를 바로 수행할 수 없다.
3. user Spot에서 actor를 내보낼 때는 `leaveActor`를 호출한다.
4. leave가 완료되면 source user Spot의 `onLeaveActor`와 target Entry Spot의
   `onJoinedActor`가 각각 실행된다.
5. Entry Spot actor handler 또는 application이 명시적으로 만든 정리 command에서
   언어별 Entry Spot destroy API를 호출하면 actor 수명이 끝난다.
6. destroy는 lifecycle callback을 호출하지 않는다. destroy는 위치 이동이 아니라 수명 종료이므로
   `onLeaveActor` 의미와 섞지 않는다.
7. destroy 뒤 들어온 stale actor packet은 actor route not found 또는 같은 의미의 오류로
   끝나야 한다. 새 actor가 같은 id로 다시 생성된 경우에는 generation 또는 runtime state
   검사를 통해 이전 packet이 새 actor를 오염시키지 않아야 한다.

## 6. 제안 공개 API

### 6.1 .NET

Entry Spot context에 `DestroyActorAsync`를 둔다. `.NET` public surface는 기존 framework
비동기 메서드 관례와 맞춰 PascalCase와 `Async` 접미사를 유지한다.

```csharp
ValueTask DestroyActorAsync(
    IZLinkActor actor,
    CancellationToken cancellationToken = default);
```

적용 지점:

- `Zlink.Framework.Contracts.Spots.IZLinkEntrySpotContext`
- `ZLinkEntrySpotActivation`
- `ZLinkFrameworkRuntimeActors` 또는 actor runtime 내부 facade
- backend spot node destroy 호출 경로

`IZLinkSpotContext`에는 destroy API를 추가하지 않는다. user Spot에서는 `leaveActor(...)`를
사용한다.

### 6.2 C++

Entry Spot context에 `destroyActor`를 추가한다. C++도 snake_case가 아니라 정책 이름을
사용한다.

```cpp
task_t<void> destroyActor(const actor_ref_t &actor_ref, TActor &actor);
```

user Spot context에는 Entry Spot으로 되돌리는 `leaveActor`만 둔다.

```cpp
task_t<actor_ref_t> leaveActor(const actor_ref_t &actor_ref, TActor &actor);
```

### 6.3 Node.js

`ZLinkEntrySpotContext`에 `destroyActor`를 추가한다.

```ts
destroyActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
```

`ZLinkSpotContext`에는 추가하지 않는다. user Spot에서는 기존
`leaveActor(...)`를 사용한다.

NestJS decorator나 handler 등록 API를 새로 만들지 않는다. destroy는 packet handler가
Entry Spot context에서 호출하는 명령이다.

### 6.4 Java/Kotlin

Java Entry Spot context에 `destroyActor`를 추가한다. Java/Kotlin은 `.NET`의
`DestroyActorAsync` 같은 언어별 변형 이름을 만들지 않고 lower camel public method를
사용한다.

```java
CompletionStage<Void> destroyActor(ZLinkActor actor);
```

언어별 async API가 `CompletableFuture`를 기준으로 고정되어 있다면 반환 타입은 기존 async API와
같게 맞춘다.

Kotlin wrapper에는 suspending 확장을 제공한다. 이 wrapper는 Java API를 감싸며 별도
runtime 의미를 추가하지 않는다.

```kotlin
suspend fun ZLinkEntrySpotContext.destroyActor(actor: ZLinkActor)
```

`ZLinkSpotContext`에는 destroy API를 추가하지 않는다. user Spot에서는 `leaveActor(...)`를
사용한다.

## 7. Runtime 구현 요구사항

### 7.1 위치 검증

destroy 호출 시 runtime은 actor가 현재 Entry Spot에 있는지 확인한다.

- actor가 user Spot에 있으면 실패한다.
- actor가 이미 destroy되었으면 성공으로 볼지 실패로 볼지 언어별로 갈라지면 안 된다.
  공통 정책은 **idempotent success**로 둔다. 같은 actor instance에 대한 중복 destroy는
  완료로 끝난다.
- actor id가 같지만 generation이 다른 새 actor가 이미 만들어졌으면 이전 actor instance의
  destroy가 새 actor를 지우면 안 된다.

### 7.2 정리 대상

destroy는 아래 상태를 한 작업 단위로 정리한다.

| 대상 | 정리 기준 |
|------|-----------|
| native actor ref | backend spot node destroy 호출이 완료되어야 한다. |
| managed actor registry | actor id와 actor instance mapping을 제거한다. |
| actor runtime state | native ref, activation, join state, generation guard를 정리한다. |
| session binding | actor에 묶인 current session binding을 해제한다. |
| bound session index | native binding token index에 남은 항목을 제거한다. |
| current membership | Entry Spot 또는 user Spot membership cache에 actor가 남지 않게 한다. |
| mailbox / pending dispatch | destroy 이전에 이미 시작된 handler는 완료되게 두고, destroy 이후 새 dispatch는 거절한다. |

### 7.3 callback 관계

destroy는 `onLeaveActor`를 호출하지 않는다.

| 상황 | callback |
|------|----------|
| actor 생성 | `onCreateActor` |
| Entry Spot에서 user Spot으로 join | Entry Spot `onLeaveActor`, user Spot `onJoinedActor` |
| user Spot에서 Entry Spot으로 leave | user Spot `onLeaveActor`, Entry Spot `onJoinedActor` |
| Entry Spot에서 destroy | callback 없음. actor 상태만 정리 |
| session disconnect | `onDisconnectActor`만 실행. leave와 destroy는 자동으로 실행하지 않음 |

destroy 전용 callback은 이번 계획에 넣지 않는다. application이 destroy 전에 해야 할 처리가
있으면 destroy를 호출하는 Entry Spot handler 안에서 명시적으로 수행한다.

### 7.4 확정 정책

구현 중 언어별 판단이 갈라지지 않도록 아래 정책을 고정한다.

| 항목 | 정책 |
|------|------|
| 중복 destroy | 같은 actor instance에 대한 두 번째 destroy는 성공으로 끝낸다. 이미 정리된 객체를 다시 정리하려는 호출은 아무 일도 하지 않는다. |
| user Spot actor destroy | 실패한다. application은 먼저 user Spot에서 leave를 완료해야 한다. |
| destroy 전 session 처리 | actor-session binding만 해제한다. stream 자체를 닫을지는 application이 결정한다. |
| remote actor destroy | 현재 Entry Spot activation이 소유한 actor만 허용한다. 다른 node의 Entry Spot actor를 직접 destroy하지 않는다. |
| callback | destroy 전용 callback은 추가하지 않는다. Entry Spot destroy는 `onLeaveActor`를 호출하지 않는다. |
| 새 actor 생성 | destroy가 끝난 뒤 같은 actor id로 새 actor를 만들 수 있어야 한다. 이전 generation의 작업이 새 actor를 지우면 안 된다. |

## 8. 언어별 구현 순서

### 8.1 공통 선행 확인

```bash
rg -n "destroyActor|leaveActor|onCreateActor|onJoinedActor|onLeaveActor|onDisconnectActor|EntrySpotContext|Entry Spot" framework/languages
rg -n "disconnect.*destroy|destroy.*disconnect|automatic.*destroy|자동.*destroy|자동.*삭제" framework/doc framework/languages/*/doc
```

확인할 항목:

1. 모든 framework 언어의 Entry Spot context 위치와 공개 export 위치
2. 모든 framework 언어의 actor runtime state registry
3. backend adapter의 native actor destroy 연결 여부
4. session binding cleanup 경로
5. 기존 문서에서 disconnect가 자동 destroy라고 설명한 문장

### 8.2 .NET

1. `IZLinkEntrySpotContext`에 `DestroyActorAsync(...)`를 추가한다.
2. `ZLinkEntrySpotActivation`이 context API를 구현한다.
3. actor runtime에 `DestroyActorAsync(IZLinkActor, CancellationToken)` 내부 API를 추가한다.
4. runtime이 actor current location을 확인하고 Entry Spot이 아니면 framework 예외를 던진다.
5. backend actor destroy API를 호출한다.
6. actor registry, session binding, membership, mailbox 상태를 정리한다.
7. contract example과 공개 API test를 갱신한다.
8. Bingo와 TicTacToe sample 모두 Entry Spot handler가 `DestroyActorAsync`를 호출하는 종료
   흐름을 추가한다.

필수 회귀 테스트:

| 테스트 | 검증 |
|--------|------|
| Entry context contract | `IZLinkEntrySpotContext.DestroyActorAsync(...)`가 공개 API에 있다. |
| destroy from Entry Spot | Entry Spot에 있는 actor destroy가 native destroy와 registry cleanup을 수행한다. |
| destroy rejects user Spot actor | user Spot에 join된 actor를 바로 destroy하면 실패한다. |
| leave then destroy | user Spot leave 후 Entry Spot에서 destroy하면 성공한다. |
| recreate same actor id | destroy 뒤 같은 actor id로 새 actor를 만들 수 있다. |
| stale generation guard | 이전 actor instance destroy가 새 generation actor를 지우지 않는다. |
| session binding cleanup | destroy가 bound session index와 actor-session mapping을 제거한다. |
| disconnect is not destroy | session disconnect만으로 actor registry가 제거되지 않는다. |
| destroy callback isolation | destroy가 `onLeaveActor`를 호출하지 않는다. 중복 destroy도 lifecycle callback을 호출하지 않는다. |

### 8.3 C++

1. Entry Spot context contract에 `destroyActor(...)`를 추가한다.
2. runtime context 구현에서 backend actor destroy operation을 호출한다.
3. actor registry와 current spot membership 상태를 정리한다.
4. public header contract test에 새 API 존재 여부를 추가한다.
5. sample parity test에서 Entry Spot context 사용 예시가 user Spot destroy로 새지 않는지
   확인한다.

필수 회귀 테스트:

| 테스트 | 검증 |
|--------|------|
| contract header compile | Entry Spot context에서 `destroyActor(...)`를 호출하는 consumer code가 컴파일된다. |
| entry destroy runtime | Entry Spot actor destroy가 backend destroy를 호출하고 actor lookup에서 사라진다. |
| user spot destroy rejected | user Spot actor를 직접 destroy하면 실패한다. |
| leave then destroy | user Spot leave 후 Entry Spot destroy가 성공한다. |
| recreate actor id | destroy 후 같은 actor id 재생성이 가능하다. |
| sample actor lifecycle gate | sample source와 runtime gate가 room leave 뒤 Entry Spot destroy 흐름, destroy callback isolation, post-destroy lookup 실패 또는 같은 actor id 재생성을 검증한다. |
| full client/server self-check | 별도 process로 실행한 sample client가 API와 Play 서버를 지나 room leave, Entry Spot destroy, post-destroy behavior까지 확인한다. 현재 C++는 이 항목이 아직 남아 있다. |

C++ full client/server self-check는 sample runner 문구만 바꿔서 완료로 보지 않는다.
`sample_cpp_framework_tictactoe_client`나 같은 의미의 public client 실행 파일이 실제
별도 process API/Play 서버에 대해 `POST /games`와 Stream Connector scenario를 끝까지
완료해야 한다. 이 선행 조건은 client/server channel request가 native transport를 통해
server handler reply를 받을 수 있어야 한다는 뜻이다. 이 channel request 선행 조건은
`test_cpp_framework_channel_messaging`과 별도 process TicTacToe API/Play 서버의 직접
`POST /games` 확인으로 고정했다. 남은 작업은 public client 실행 파일이 반환된 Play
endpoint로 Stream Connector gameplay, room leave, Entry Spot destroy, post-destroy
behavior까지 끝내도록 이어서 검증하고 수정하는 것이다.

### 8.4 Node.js

1. `ZLinkEntrySpotContext` TypeScript contract에 `destroyActor(...)`를 추가한다.
2. runtime Entry Spot activation/context 구현에 같은 메서드를 연결한다.
3. package root export와 generated JS contract 파일을 함께 맞춘다.
4. NestJS provider/scanner에는 새 decorator를 추가하지 않는다.
5. actor manager/runtime registry cleanup을 추가한다.
6. `contract-surface.test.js`, `actor-manager.test.js`, `spot-manager.test.js`,
   `sample-regression.test.js`를 갱신한다.

필수 회귀 테스트:

| 테스트 | 검증 |
|--------|------|
| contract API | `ZLinkEntrySpotContext.destroyActor`가 public type과 runtime object에 있다. |
| no user context destroy | `ZLinkSpotContext` 공개 API에는 `destroyActor`가 없다. |
| entry destroy runtime | Entry Spot actor destroy가 backend destroy와 registry cleanup을 수행한다. |
| user spot rejected | user Spot actor direct destroy가 거절된다. |
| leave then destroy | leave 후 Entry Spot destroy가 성공한다. |
| disconnect not destroy | stream close 또는 disconnect notification만으로 actor가 삭제되지 않는다. |
| sample regression | Bingo와 TicTacToe sample이 room leave 뒤 Entry Spot 정리를 검증한다. |

### 8.5 Java/Kotlin

1. Java `ZLinkEntrySpotContext`에 destroy API를 추가한다.
2. runtime Entry Spot context 구현과 fake backend port를 연결한다.
3. Kotlin wrapper에 suspending helper를 추가한다.
4. Spring Boot starter public scan/export 정책을 확인한다.
5. Java sample과 Kotlin sample에서 Entry Spot actor 종료 흐름을 같은 의미로 맞춘다.
6. fake backend, integration, sample release gate를 갱신한다.

필수 회귀 테스트:

| 테스트 | 검증 |
|--------|------|
| Java public contract | `ZLinkEntrySpotContext.destroyActor(...)`가 compile contract에 있다. |
| Kotlin wrapper | suspending `destroyActor(...)` wrapper가 Java API와 같은 의미로 동작한다. |
| fake backend destroy | Entry Spot destroy가 fake backend destroy 호출과 registry cleanup을 남긴다. |
| user spot rejected | user Spot actor direct destroy가 실패한다. |
| leave then destroy | user Spot leave 뒤 Entry Spot destroy가 성공한다. |
| recreate actor id | destroy 후 같은 actor id 재생성이 가능하다. |
| disconnect not destroy | disconnect callback만으로 actor가 제거되지 않는다. |
| sample release gate | Java sample runner와 Kotlin sample runner가 actor 종료 흐름을 포함해 통과한다. |

## 9. 문서 반영 계획

### 9.1 공통 문서

수정 대상:

- `framework/doc/spec/actor-model.ko.md`
- `framework/doc/spec/session-actor-dispatch.ko.md`
- `framework/doc/spec/framework-api.ko.md`
- `framework/doc/spec/sample/bingo/README.ko.md`
- `framework/doc/spec/sample/tictactoe/README.ko.md`
- 필요한 경우 `framework/doc/spec/sample/supportchat/README.ko.md`

반영 내용:

1. disconnect가 자동 destroy라는 설명을 제거한다.
2. actor 수명 흐름을 `create -> Entry Spot -> user Spot -> leave -> Entry Spot -> destroy`
   로 고친다.
3. destroy는 Entry Spot에서만 가능하다고 적는다.
4. user Spot은 actor 삭제가 아니라 leave를 통해 Entry Spot 복귀까지만 맡는다고 적는다.
5. destroy가 `onLeaveActor` callback을 호출하지 않는다고 적는다.

### 9.2 언어별 문서

수정 대상:

| 언어 | 문서 |
|------|------|
| .NET | `framework/languages/dotnet/doc/spec/aspnet-core-actor.ko.md`, `handler-interfaces.ko.md`, `doc/guide/06-actor-session.ko.md`, `doc/guide/11-interface-catalog.ko.md`, sample guide |
| C++ | `framework/languages/cpp/doc/internals/cpp-framework-policy.ko.md`, public guide/spec 문서가 있으면 해당 문서 |
| Node.js | `framework/languages/node/doc/spec/nestjs-actor.ko.md`, `handler-interfaces.ko.md`, `spot-node.ko.md`, guide와 regression matrix |
| Java | `framework/languages/java/doc/spec/spring-boot-spot.ko.md`, guide actor/session 문서, internals regression matrix |
| Kotlin | `framework/languages/java/doc` 아래 Kotlin framework guide/spec 문서와 Kotlin sample README |

문서 회귀 테스트:

- 모든 framework 언어의 documentation regression test에 Entry Spot destroy API가 Entry
  Spot context에만 나타나는지 검사한다. `.NET`은 `DestroyActorAsync`, Node.js,
  Java/Kotlin, C++는 `destroyActor`를 기준으로 삼는다.
- `destroyActorAsync`, `destroy_actor`, `OnActorLeft`, `onActorLeft`,
  `on_actor_left`, `OnCreateActor`, `on_actor_created`, `onPostActorJoined`처럼
  이 문서의 이름 정책을 벗어나는 public 함수 이름은 금지 문자열로 둔다. `.NET`의
  `DestroyActorAsync`는 허용 이름이다. 이 금지 문자열 검사는 정식 spec, guide, sample
  README, 공개 API source를 대상으로 하고, 이 draft처럼 금지 예시를 설명하는 문서는
  제외한다.
- `disconnect -> destroy` 자동 흐름을 설명하는 문장이 남지 않도록 금지 문자열 검사를 둔다.
- sample README가 room leave와 actor destroy를 같은 동작처럼 쓰지 않는지 검사한다.

## 10. Sample 반영 계획

우선 적용 sample:

1. Bingo
2. TicTacToe

sample은 문서 시나리오를 먼저 고친 뒤 그 시나리오대로 코드를 수정한다. 기존 코드에
비슷한 흐름이 있더라도 문서 시나리오와 이름, callback 의미가 다르면 sample code를 다시
맞춘다. sample은 framework 사용자가 그대로 읽는 실행 예제이므로, 내부 구현 편의보다
시나리오의 책임 분리가 먼저다.

### 10.1 공통 sample 시나리오

Bingo와 TicTacToe sample 문서에는 아래 흐름을 공통으로 적는다.

1. client가 actor를 만들거나 인증한다.
2. framework는 actor 객체 생성이 끝난 뒤 `onCreateActor`를 한 번 호출한다.
3. actor는 Entry Spot에서 match, create, join 같은 입장 요청을 처리한다.
4. actor가 room user Spot에 들어갈 때 Entry Spot `onLeaveActor`와 room `onJoinedActor`가
   호출된다.
5. room에서 나가야 하면 room user Spot handler가 `leaveActor`를 호출해 actor를 Entry
   Spot으로 돌려보낸다.
6. leave가 끝나면 room `onLeaveActor`와 Entry Spot `onJoinedActor`가 호출된다.
7. Entry Spot handler가 client의 종료 요청, 게임 종료 후 퇴장 요청, 또는 sample의 정리
   단계에서 언어별 Entry Spot destroy API를 호출한다.
8. destroy는 `onLeaveActor`를 호출하지 않는다.
9. sample self-check가 destroy 뒤 같은 actor id 재생성 또는 actor route not found를 확인한다.

disconnect 시나리오는 별도로 적는다.

1. client connection을 끊으면 `onDisconnectActor`만 호출된다.
2. disconnect만으로 room leave나 Entry Spot destroy가 자동 실행되지 않는다.
3. sample이 reconnect 또는 cleanup command를 제공한다면, reconnect 후 application
   command가 명시적으로 leave와 destroy를 수행한다.

### 10.2 sample 문서 수정 대상

| sample | 공통 spec 문서 | 반영 내용 |
|--------|----------------|-----------|
| Bingo | `framework/doc/spec/sample/bingo/README.ko.md` | room 입장, room leave, Entry Spot 복귀, actor destroy, disconnect 차이를 시나리오에 추가한다. |
| TicTacToe | `framework/doc/spec/sample/tictactoe/README.ko.md` | game room 입장, 게임 종료 또는 포기 후 leave, Entry Spot destroy, disconnect 차이를 시나리오에 추가한다. |

언어별 sample README가 있으면 공통 sample spec과 같은 흐름으로 맞춘다. 언어별 README에는
이 문서의 public 이름 정책에 맞는 이름을 사용한다. `.NET` README는 `DestroyActorAsync`,
다른 언어 README는 `destroyActor`를 사용하고, callback 이름은 `onCreateActor`,
`onJoinedActor`, `onLeaveActor`, `onDisconnectActor`로 맞춘다.

### 10.3 sample code 수정 대상

| 언어 | sample code 대상 | 수정 기준 |
|------|------------------|-----------|
| .NET | `framework/languages/dotnet/samples/Bingo`, `framework/languages/dotnet/samples/TicTacToe` | Entry Spot handler에 종료 command를 두고 `DestroyActorAsync`를 호출한다. room Spot은 `leaveActor`까지만 호출한다. |
| C++ | `framework/languages/cpp/samples/Bingo`, `framework/languages/cpp/samples/TicTacToe` | Entry Spot context에서만 `destroyActor`를 호출하게 하고, room handler와 lifecycle callback에는 destroy를 넣지 않는다. |
| Node.js | `framework/languages/node/samples/Bingo.Ts`, `framework/languages/node/samples/TicTacToe.Ts` | TypeScript sample의 Entry Spot context 사용 예시를 `destroyActor`로 맞추고 user Spot context에는 destroy 사용이 없음을 보여준다. |
| Java | `framework/languages/java/samples/java/Bingo`, `framework/languages/java/samples/java/TicTacToe` | Java sample도 public 이름 정책을 따르고 Entry Spot context에서만 `destroyActor`를 호출한다. |
| Kotlin | `framework/languages/java/samples/kotlin/Bingo`, `framework/languages/java/samples/kotlin/TicTacToe` | Kotlin sample은 wrapper가 있더라도 `destroyActor` 이름만 보여주고 Java sample과 같은 시나리오 순서로 동작한다. |

sample code는 아래 구조로 읽혀야 한다.

1. client scenario가 room 입장 요청을 보낸다.
2. Entry Spot handler가 room join을 수행한다.
3. room handler가 leave 요청을 받으면 `leaveActor`를 호출하고, actor가 Entry Spot으로
   돌아왔다는 응답이나 event를 client scenario가 확인한다.
4. client scenario가 Entry Spot 종료 요청을 보낸다.
5. Entry Spot handler가 언어별 Entry Spot destroy API를 호출한다.
6. client scenario 또는 sample runner가 destroy 이후 packet 전송 실패, actor lookup
   실패, 또는 같은 actor id 재생성 성공 중 하나를 명시적으로 확인한다.

### 10.4 sample self-check 기준

sample runner는 성공 로그만 보고 통과하면 안 된다. 각 sample은 아래 항목을
server-side assertion 또는 client scenario assertion으로 확인한다. post-destroy
behavior는 sample 구조에 맞는 확인 방법 하나를 고르되, destroy 이후 상태를 반드시
검증해야 한다.

| 확인 항목 | 의미 |
|-----------|------|
| `onCreateActor` count | actor 생성당 한 번만 호출된다. |
| join/leave callback order | Entry Spot -> room join, room -> Entry Spot leave 순서에서 `onJoinedActor`와 `onLeaveActor`가 기대한 Spot에서 호출된다. |
| destroy callback isolation | Entry Spot destroy API 호출 뒤 `onLeaveActor`가 추가로 호출되지 않는다. |
| user Spot destroy absence | room handler 또는 user Spot context에서 `destroyActor`를 호출하지 않는다. |
| disconnect isolation | disconnect는 `onDisconnectActor`만 검증하고 destroy 성공으로 간주하지 않는다. |
| post-destroy behavior | destroy 뒤 stale actor route가 실패하거나 같은 actor id 재생성이 성공한다. |

샘플에서 피할 것:

- room Spot에서 직접 actor destroy를 호출하지 않는다.
- `onLeaveActor` callback에 actor destroy를 숨기지 않는다.
- sleep으로 destroy 완료를 기다리지 않는다. 명시적 reply, event, lookup 실패, 또는
  sample runner의 server-side assertion으로 확인한다.

## 11. 검증 순서

각 언어는 아래 순서로 검증한다.

1. public contract compile 또는 typecheck
2. runtime unit test
3. fake backend 또는 adapter integration test
4. actor/session/spot integration test
5. sample regression test
6. full sample runner
7. documentation regression test
8. `git diff --check`

권장 명령은 구현 시점의 runner를 다시 확인한 뒤 사용한다.

```bash
# .NET
dotnet test framework/languages/dotnet/Zlink.Framework.sln
framework/languages/dotnet/samples/run_samples.sh

# C++
cmake --build framework/languages/cpp/build
ctest --test-dir framework/languages/cpp/build --output-on-failure
framework/languages/cpp/samples/run_samples.sh

# Node.js
npm --prefix framework/languages/node run build
npm --prefix framework/languages/node run verify:coverage
framework/languages/node/samples/run_samples.sh

# Java/Kotlin
framework/languages/java/gradlew test integrationTest fakeBackendTest
framework/languages/java/samples/run_samples.sh
```

명령 이름은 현재 checkout의 실제 runner와 다를 수 있다. 구현자는 실행 전에 각 언어의
README, build file, runner script를 다시 확인한다.

## 12. Goal 기반 진행 규칙

이 기능은 언어와 문서를 모두 건드리므로 goal을 작게 나누어 추적한다. 구현자는 작업을
시작할 때 "Entry Spot actor destroy를 모든 framework 언어와 문서, 회귀 테스트에
반영하고 언어별 public 이름 정책과 의미를 맞춘다"는 목표로 goal을 만들고, 아래 checklist를
완료할 때마다 진행 상태를 갱신한다.

| Goal 단계 | 완료 조건 |
|-----------|-----------|
| 공통 계약 확정 | 이 draft의 정책과 실제 core/backend destroy 동작이 충돌하지 않는지 확인했다. 충돌하면 draft를 먼저 고쳤다. |
| .NET 구현 | .NET 공개 API, runtime cleanup, tests, sample, 문서가 모두 반영되고 .NET 검증 명령이 통과했다. |
| C++ 구현 | C++ 공개 header, runtime cleanup, tests, sample, 문서가 모두 반영되고 C++ 검증 명령이 통과했다. |
| Node.js 구현 | TypeScript 계약, runtime cleanup, generated JS, tests, sample, 문서가 모두 반영되고 Node 검증 명령이 통과했다. |
| Java 구현 | Java 계약, runtime cleanup, fake backend, tests, Java sample, 문서가 모두 반영되고 Java 검증 명령이 통과했다. |
| Kotlin 구현 | Kotlin wrapper, Kotlin sample, 문서가 Java 계약과 같은 이름과 의미로 반영되고 Kotlin 검증 명령이 통과했다. |
| sample 시나리오 검토 | Bingo와 TicTacToe 공통 sample spec을 먼저 고치고, 언어별 sample code가 그 시나리오와 같은 순서로 동작하는지 확인했다. |
| 교차 검토 | 모든 framework 언어가 이 문서의 public 이름 정책을 따르는지 비교했다. user Spot context에 destroy API가 없는지 다시 확인했다. |
| 최종 검증 | full sample runner, 문서 회귀 테스트, `git diff --check`가 통과했다. |

goal을 완료로 표시하기 전에 아래 질문에 모두 "예"라고 답해야 한다.

1. 모든 framework 언어가 Entry Spot context에만 destroy API를 두는가?
2. user Spot actor를 바로 destroy하는 테스트가 실패를 검증하는가?
3. leave 후 Entry Spot destroy가 성공하는 테스트가 있는가?
4. disconnect만으로 destroy가 실행되지 않는다는 테스트가 있는가?
5. destroy 뒤 같은 actor id를 다시 만들 수 있다는 테스트가 있는가?
6. sample self-check가 actor 종료 흐름을 실제로 검증하는가?
7. 공통 문서와 언어별 문서가 같은 수명 흐름을 설명하는가?
8. 자동 destroy를 암시하는 문장이 문서 회귀 테스트로 막히는가?
9. Bingo와 TicTacToe sample code가 공통 sample spec의 시나리오 순서를 그대로 따르는가?

검증을 실행하지 못한 항목이 있으면 goal을 완료로 표시하지 않는다. 환경 문제나 선행
작업 충돌 때문에 진행할 수 없으면, 완료 대신 막힌 조건과 재개에 필요한 입력을 기록한다.
최종 보고에는 아래 증거를 남긴다.

| 증거 | 기록 내용 |
|------|-----------|
| 변경 범위 | 언어별로 수정한 공개 API, runtime, test, sample, 문서 파일 목록 |
| 검증 명령 | 실제 실행한 명령과 성공/실패 결과 |
| 생략 항목 | 실행하지 못한 검증과 이유 |
| 회귀 테스트 | 새로 추가한 테스트 이름과 검증 의미 |
| 남은 위험 | 완료 기준을 통과했더라도 운영상 주의해야 할 항목 |

## 13. 단계별 작업 체크리스트

구현자는 아래 순서로 진행한다. 한 단계를 건너뛰지 않는다.

### Phase 0: 기준선 고정

- [x] core/backend destroy API의 현재 동작을 확인한다.
- [x] 모든 framework 언어의 Entry Spot context와 user Spot context 공개 API를 목록화한다.
- [x] actor registry, session binding, membership cache, mailbox 구현 위치를 언어별로 찾는다.
- [x] disconnect와 destroy를 같은 흐름으로 설명한 문서를 목록화한다.
- [x] 현재 sample에서 room leave 또는 actor 종료 흐름이 있는지 확인한다.

### Phase 1: 공통 계약 반영

- [x] 공통 actor model 문서의 수명 흐름을 이 문서와 맞춘다.
- [x] framework API 문서에 Entry Spot context destroy API 의미를 추가한다.
- [x] session actor dispatch 문서에서 disconnect가 destroy를 뜻하지 않는다고 적는다.
- [x] sample spec에 room leave와 actor destroy의 책임 차이를 적는다.
- [x] Bingo sample spec에 `onCreateActor`, join, leave, Entry Spot destroy, disconnect 시나리오를 추가한다.
- [x] TicTacToe sample spec에 `onCreateActor`, join, leave, Entry Spot destroy, disconnect 시나리오를 추가한다.

### Phase 2: 언어별 runtime 구현

- [x] Entry Spot context 공개 API를 추가한다.
- [x] user Spot context에는 destroy API를 추가하지 않는다.
- [x] runtime 내부 destroy API를 하나로 모은다.
- [x] 현재 actor가 Entry Spot에 있는지 검증한다.
- [x] native destroy를 호출한다.
- [x] managed actor registry와 actor runtime state를 정리한다.
- [x] session binding과 bound session index를 정리한다.
- [x] destroy 이후 새 dispatch를 거절한다.
- [x] destroy 이후 같은 actor id 재생성을 허용한다.

### Phase 3: 언어별 테스트와 sample

- [x] 공개 API test를 추가한다.
- [x] Entry Spot destroy 성공 테스트를 추가한다.
- [x] user Spot direct destroy 실패 테스트를 추가한다.
- [x] leave 후 destroy 성공 테스트를 추가한다.
- [x] stale generation guard 테스트를 추가한다.
- [x] session binding cleanup 테스트를 추가한다.
- [x] disconnect가 destroy를 실행하지 않는 테스트를 추가한다.
- [x] destroy가 `onLeaveActor` callback을 호출하지 않고 중복 destroy에서도 lifecycle callback을 호출하지 않는 테스트를 추가한다.
- [x] .NET Bingo와 TicTacToe sample code를 공통 sample spec 시나리오대로 수정한다.
- [x] C++ Bingo와 TicTacToe sample code를 공통 sample spec 시나리오대로 수정한다.
- [x] Node.js Bingo와 TicTacToe sample code를 공통 sample spec 시나리오대로 수정한다.
- [x] Java Bingo와 TicTacToe sample code를 공통 sample spec 시나리오대로 수정한다.
- [x] Kotlin Bingo와 TicTacToe sample code를 공통 sample spec 시나리오대로 수정한다.
- [x] 각 sample self-check에 actor 종료 흐름과 destroy callback isolation 확인을 넣는다.
- [x] C++ TicTacToe sample runner를 full client/server self-check로 승격하려면 먼저 일반 channel
      request가 별도 process channel server에서 reply를 받을 수 있게 구현한다. 이 항목은
      native client request, hosted server receive loop, handler reply, 별도 process
      `POST /games` 200 응답까지 확인했다. public client 실행 파일은 이제 connect,
      authenticate, `join_game`, Stream Connector gameplay까지 완료한다. runner가 full
      self-check를 표시하기 위해 game 종료 뒤 room leave와 Entry Spot destroy 경로를 지나
      같은 actor id 재인증까지 확인한다.
- [x] C++ Bingo sample의 별도 Session/API/Play 프로세스 구성을 점검하고, 실제 session
      factory 등록, 명시 channel endpoint, host별 Protobuf type 등록, Play local
      ActorGateway와 actor factory wiring 누락을 보강한다.
- [x] C++ Bingo sample runner를 full client/server self-check로 승격한다. 별도 process
      Registry, API, Play, Session 서버를 계속 실행하고 public client 실행 파일로
      authenticate, match, room Spot join, card submit, server draw, winner 판단을
      끝까지 확인한다.

### Phase 4: 문서와 회귀 검사

- [x] 언어별 spec을 갱신한다.
- [x] 언어별 guide를 갱신한다.
- [x] sample README를 갱신한다.
- [x] 언어별 sample README가 공통 sample spec과 같은 순서와 언어별 public 이름 정책을 쓰는지 확인한다.
- [x] 문서 회귀 테스트에 Entry Spot context destroy API 노출을 추가한다.
- [x] 문서 회귀 테스트에 user Spot context destroy API 금지를 추가한다.
- [x] 문서 회귀 테스트에 disconnect 자동 destroy 설명 금지를 추가한다.

### Phase 5: 최종 검증

- [x] 각 언어 build/test 명령을 통과시킨다. .NET solution test, Node.js
      `build`와 `verify:coverage`, Java/Kotlin `test`, `integrationTest`,
      `fakeBackendTest`, C++ full CTest를 통과시켰다.
- [x] 각 언어 sample runner를 통과시킨다.
- [x] C++는 `sample_cpp_framework_tictactoe_client` 또는 같은 수준의 public client
      executable이 실제 별도 process API/Play 서버를 상대로 `POST /games`, Stream
      Connector connect, authenticate, `join_game`, gameplay, actor 종료 scenario를
      끝까지 완료하는지 확인한다. 현재 TicTacToe public client는 game 종료 뒤 room leave와
      Entry Spot destroy 경로를 지나 같은 actor id 재인증까지 완료한다. stale packet 실패와
      post-destroy lookup 실패는 runtime gate가 검증한다.
- [x] 전체 문서 회귀 테스트를 통과시킨다. `.NET` documentation/sample regression,
      Node.js documentation/contract-surface/sample regression, Java/Kotlin
      `SampleReleaseGateContractTest`, C++ contract header/layout/sample parity test가
      모두 통과했다.
- [x] `git diff --check`를 통과시킨다.
- [x] goal 완료 조건을 다시 읽고 빠진 항목이 없음을 확인한다.

## 14. 완료 기준

기능 완료는 아래 조건을 모두 만족해야 한다.

1. 모든 framework 언어가 Entry Spot context destroy API를 언어별 public 이름 정책에 맞게 제공한다.
2. 모든 framework 언어의 user Spot context에는 destroy API가 없다.
3. user Spot actor direct destroy가 회귀 테스트에서 실패로 검증된다.
4. leave 후 Entry Spot destroy가 회귀 테스트에서 성공으로 검증된다.
5. destroy 뒤 actor registry, session binding, native actor ref가 남지 않는다.
6. destroy 뒤 같은 actor id를 다시 만들 수 있다.
7. disconnect만으로 destroy가 실행되지 않는다는 테스트가 있다.
8. Bingo와 TicTacToe 공통 sample spec을 먼저 수정했고, 모든 framework 언어의 sample
   code가 그 시나리오 순서를 따른다.
9. sample runner가 actor 종료 흐름과 destroy callback isolation을 검증한다.
10. 공통 문서와 언어별 문서가 같은 의미로 맞춰져 있다.
11. 문서 회귀 테스트가 자동 destroy 설명, user Spot destroy API 노출, 정책 밖의 이름을 막는다.

## 15. 구현 중 주의할 위험

| 위험 | 대응 |
|------|------|
| native destroy만 호출하고 managed registry를 남김 | destroy를 actor runtime의 단일 내부 API로 모으고 cleanup 대상을 테스트로 검증한다. |
| disconnect와 destroy가 다시 섞임 | disconnect 테스트와 destroy 테스트를 분리하고, 문서 금지 문자열 검사를 둔다. |
| user Spot에서 destroy를 열어 책임이 흐려짐 | 공개 API test로 user Spot context에 destroy가 없는지 확인한다. |
| user Spot `onLeaveActor`에서 자동 삭제를 수행함 | Entry Spot destroy API만 actor를 삭제할 수 있음을 테스트로 확인한다. |
| stale packet이 새 actor generation을 건드림 | generation guard 또는 actor instance identity 검사를 runtime 테스트에 넣는다. |
| public 함수 이름은 언어별로 달라도 의미가 갈라짐 | 공통 contract matrix를 두고 각 언어 테스트 이름과 검증 내용을 같은 의미로 맞춘다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [상위: Draft](./README.ko.md)
<!-- framework-adapter-nav:bottom:end -->
