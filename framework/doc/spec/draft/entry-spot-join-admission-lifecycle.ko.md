<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [상위: Draft](./README.ko.md)
<!-- framework-adapter-nav:end -->

# Entry Spot Join Admission과 Joined Callback 전환 초안

상태: 부분 구현(진행 중). 2026-06-17 전수조사 기준:
- core C API와 저수준 바인딩(C/C++/Java/Node/.NET/Python/Go/Rust) data plane은 request/reply
  모델로 반영됨.
- framework admission(`OnActorJoin`/`onActorJoin`)은 C++·Node·.NET·Java/Kotlin에 반영됨.
- 샘플과 언어별 정식 문서는 구현된 Entry Spot admission 표면을 따라가는 중이다.
- 남은 작업은 언어별 정식 spec/guide 반영 범위와 전체 회귀 테스트 범위를 더 넓히는 것이다.

이 문서는 구현 전 결정을 모은 draft이며, 구현 완료 뒤에도 **정식 공개 계약 문서가 아니다**.
정식 공개 계약은 core C API, 바인딩, framework, sample, 테스트가 같은 의미로 정리된 내용을
공통 framework spec, 언어별 spec, guide, sample 문서에 나누어 반영한 결과를 기준으로 한다.

이 문서는 actor가 user Spot 또는 Entry Spot으로 들어갈 때 같은 join admission 모델을
사용하도록 맞추고, commit 이후 lifecycle callback 이름을 `OnJoinedActor` 계열로 정리하는
계획을 정리한다.

## 1. 배경

이 초안을 처음 작성할 때 framework의 actor join 표면은 user Spot과 Entry Spot이 다르게 생겼다.

user Spot join은 caller가 `Message`를 넘기고, target Spot이 요청을 보고 accept/reject와
reply를 결정한다.

```csharp
IZLinkActorJoinSpotCall JoinSpot(
    RoutingId spotRid,
    Message request);

ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
    TActor actor,
    Message request,
    CancellationToken cancellationToken);
```

반면 당시 Entry Spot join은 caller가 `Message`를 넘기지 못하고, Entry Spot에도 admission
callback이 없었다.

```csharp
IZLinkActorJoinEntrySpotCall JoinEntrySpot(
    RoutingId spotNodeRid);

ValueTask onJoinedActor(
    TActor actor,
    CancellationToken cancellationToken);
```

이 차이 때문에 Entry Spot으로 돌아오는 흐름에서 application이 join request를 해석하거나
reply를 돌려줄 수 없다. 또한 `onJoinedActor`라는 이름은 join 요청을 처리하는 callback처럼
읽히지만, 실제 호출 시점은 join commit이 끝난 뒤다. 이름이 동작 시점을 정확히 드러내지
못한다.

## 2. 목표

1. user Spot과 Entry Spot이 같은 join admission 흐름을 사용한다.
2. Entry Spot join도 caller가 `Message` request를 넘길 수 있다.
3. Entry Spot join도 accept/reject와 reply를 반환할 수 있다.
4. commit 이후 callback 이름을 `OnJoinedActor` 계열로 바꾼다.
5. `OnActorJoinAsync`는 admission, `OnJoinedActor`는 commit 이후 lifecycle이라는 책임을
   모든 언어에서 같은 뜻으로 유지한다.
6. 기존 `onJoinActor`/`OnJoinActor` 계열 이름은 compatibility shim 없이 제거한다.
7. core C API, 지원 바인딩, framework, sample, 문서를 같은 순서로 맞춘다.
8. sample은 `JoinSpot(..., request)`와 `JoinEntrySpot(..., request)`가 같은 방식으로
   사용된다는 점을 보여준다.

## 3. 비목표

- actor 생성 callback인 `OnCreateActor`를 제거하지 않는다.
- user Spot의 `OnActorJoinAsync` 의미를 바꾸지 않는다.
- join admission을 packet handler registry로 옮기지 않는다.
- Entry Spot에서 actor destroy 책임을 user Spot으로 옮기지 않는다.
- compatibility overload를 장기간 유지하지 않는다. 기존 API는 breaking change로 정리한다.
- core C API 변경 없이 framework에서만 request/reply를 흉내 내지 않는다.

### 3.1 실행 전 결정값

이 draft를 goal로 실행할 때는 아래 결정을 기본값으로 사용한다. 구현 전에 다른 결정을
선택해야 하면, core API 초안, 언어별 target API, 테스트 요구사항, 문서 반영 대상을 먼저
같은 의미로 고친 뒤 작업을 시작한다.

| 항목 | 결정 |
|------|------|
| 최초 Entry Spot membership | actor 생성 직후 첫 Entry Spot 위치 확정에는 `OnActorJoin`과 `OnJoinedActor`를 호출하지 않는다. `OnCreateActor`만 생성 초기화를 맡는다. |
| `OnJoinedActor` 실패 | caller join result는 admission accept/reply 결과를 유지하고, `OnJoinedActor` 실패는 lifecycle failure로 기록한다. |
| .NET callback 이름 | 공개 async callback은 `OnJoinedActorAsync`로 통일한다. |
| C++ callback 이름 | admission은 기존 `on_actor_join`을 유지하고, commit 이후 callback은 `on_actor_joined`로 통일한다. |
| join result 타입 | user Spot join과 Entry Spot join은 같은 의미의 `ActorJoinResult` 계열 타입을 사용한다. 언어별 이름은 달라도 accepted/rejected, actor, reply 의미는 같아야 한다. |
| request 없는 공개 overload | 새 공개 API는 request 인자를 요구한다. request가 없는 호출이 필요하면 caller가 빈 `Message` 또는 언어별 empty message object를 명시적으로 넘긴다. |

## 4. 용어와 의미

| 용어 | 의미 |
|------|------|
| actor create | actor 객체가 runtime에 처음 만들어지는 일이다. Entry Spot 위치 이동과 같은 뜻이 아니다. |
| join admission | actor가 target Spot에 들어갈 수 있는지 판단하는 단계다. request를 해석하고 accept/reject와 reply를 만든다. |
| join commit | admission이 accept를 반환한 뒤 actor 위치와 membership을 실제로 바꾸는 단계다. |
| joined callback | join commit이 끝난 뒤 호출되는 lifecycle callback이다. request를 해석하는 단계가 아니다. |
| Entry Spot | actor가 생성 직후 위치하고, user Spot에서 leave한 actor가 돌아오는 기본 Spot이다. |
| user Spot | room, game, stage 같은 application domain Spot이다. |

## 5. 초안 작성 당시 계약 문제

### 5.1 이름이 호출 시점을 흐린다

`onJoinedActor`는 "join 요청 처리"처럼 읽힌다. 하지만 당시 구현에서는 admission이 아니라
commit 이후 알림이다. 이 이름 때문에 request가 callback에서 사라진 것처럼 보인다.

정확한 이름은 `OnJoinedActor`다. 과거형을 사용하면 이 callback이 이미 commit된 상태에서
실행된다는 점이 드러난다.

### 5.2 Entry Spot join에 request/reply가 없었다

초안 작성 당시 core C API에서 user Spot join은 payload parts를 받지만 Entry Spot join은
payload를 받지 않았다.

```c
zlink_spot_node_actor_join_spot(
    node,
    actor,
    dest_node_rid,
    dest_spot_rid,
    parts,
    part_count,
    handler,
    userdata,
    flags,
    timeout_ms);

zlink_spot_node_actor_join_entry_spot(
    node,
    actor,
    dest_node_rid,
    handler,
    userdata,
    timeout_ms);
```

이 구조에서는 framework가 `JoinEntrySpot(..., request)`를 공개하더라도 하위 runtime으로
request를 보낼 수 없다. Entry Spot admission을 제대로 지원하려면 core C API와 모든 바인딩
표면이 함께 바뀌어야 한다.

### 5.3 Entry Spot join 결과가 너무 좁다

초안 작성 당시 framework의 Entry Spot join 결과는 대부분 `ActorRef`만 반환했다. user Spot join처럼
accept/reject와 reply를 표현할 공간이 없다.

Entry Spot join도 admission을 갖게 되면 결과 타입은 user Spot join과 같은 의미를 가져야
한다.

## 6. 목표 흐름

### 6.1 user Spot join

```text
actor calls JoinSpot(spotRid, request)
        |
        v
target user Spot OnActorJoin(actor, request)
        |
        +-- reject(reply) --> caller receives rejected result
        |
        +-- accept(reply)
                |
                v
            commit actor membership
                |
                v
            target user Spot OnJoinedActor(actor)
                |
                v
            caller receives accepted result and reply
```

### 6.2 Entry Spot join

```text
actor calls JoinEntrySpot(spotNodeRid, request)
        |
        v
target Entry Spot OnActorJoin(actor, request)
        |
        +-- reject(reply) --> caller receives rejected result
        |
        +-- accept(reply)
                |
                v
            commit actor membership
                |
                v
            target Entry Spot OnJoinedActor(actor)
                |
                v
            caller receives accepted result and reply
```

명시적 join operation에서 Entry Spot과 user Spot의 차이는 target 주소뿐이다. user Spot은
`spotRid`로 대상 Spot을 고르고, Entry Spot은 `spotNodeRid`로 대상 node의 Entry Spot을 고른다.
actor 생성 직후 첫 Entry Spot 위치 확정은 명시적 join operation이 아니므로 별도 생성 흐름으로
다룬다.

## 7. 제안 공통 API 의미

### 7.1 actor context

공통 의미:

```text
JoinSpot(spotRid, request) -> ActorJoinResult
JoinEntrySpot(spotNodeRid, request) -> ActorJoinResult
```

`request`는 `Message` 또는 언어별 object messaging 표면을 통해 만들어진 메시지다. object
messaging을 지원하는 framework 언어에서는 기존 messaging 원칙과 같이 caller가 codec detail을
알 필요가 없어야 한다.

### 7.2 user Spot

공통 의미:

```text
OnActorJoin(actor, request) -> ActorJoinResult
OnJoinedActor(actor) -> void
OnLeaveActor(actor) -> void
OnDisconnectedActor(actor) -> void
```

`OnActorJoin`은 join admission이다. 여기서 request를 decode하고, domain rule을 검증하고,
accept/reject와 reply를 만든다.

`OnJoinedActor`는 commit 이후 lifecycle이다. 여기서는 이미 actor가 target Spot에 들어와
있다. request를 다시 전달하지 않는다.

### 7.3 Entry Spot

공통 의미:

```text
OnCreateActor(actor) -> void
OnActorJoin(actor, request) -> ActorJoinResult
OnJoinedActor(actor) -> void
OnLeaveActor(actor) -> void
OnDisconnectedActor(actor) -> void
```

`OnCreateActor`는 actor 객체가 처음 만들어질 때 한 번 호출된다. Entry Spot join마다 호출하는
callback이 아니다.

`OnActorJoin`은 이미 존재하는 actor가 Entry Spot에 들어오려고 할 때 호출된다. 이 문서의 기본
방향은 actor 생성 직후 첫 Entry Spot 위치 확정에는 `OnActorJoin`을 호출하지 않고,
`OnCreateActor`로 actor 생성 초기화만 처리하는 것이다. user Spot에서 Entry Spot으로 돌아오는
명시적 `JoinEntrySpot(..., request)` 호출에는 `OnActorJoin`과 `OnJoinedActor` 흐름을 적용한다.

## 8. .NET 목표 표면 초안

```csharp
public interface IZLinkActorContext
{
    IZLinkActorJoinCall JoinSpot(
        RoutingId spotRid,
        Message request);

    IZLinkActorJoinCall JoinEntrySpot(
        RoutingId spotNodeRid,
        Message request);
}

public sealed record ZLinkActorJoinResult(
    bool Accepted,
    ActorRef Actor,
    Message Reply);

public interface IZLinkActorJoinCall
{
    IZLinkActorJoinCall Timeout(TimeSpan timeout);

    ValueTask<ZLinkActorJoinResult> Async(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpot<TActor> : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        TActor actor,
        Message request,
        CancellationToken cancellationToken);

    ValueTask OnJoinedActorAsync(
        TActor actor,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpot<TActor> : IZLinkEntrySpot
    where TActor : IZLinkActor
{
    ValueTask OnCreateActorAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        TActor actor,
        Message request,
        CancellationToken cancellationToken);

    ValueTask OnJoinedActorAsync(
        TActor actor,
        CancellationToken cancellationToken);
}
```

`.NET`은 public async method 이름에 `Async`를 붙이는 관례가 있으므로 최종 이름은
`OnJoinedActorAsync`로 통일한다. 기존 sample이나 callback에 lower camel 이름이 남아 있으면
전환 대상이다.

## 9. Java / Kotlin 목표 표면 초안

Java:

```java
interface ZLinkActorContext {
    ZLinkActorJoinCall joinSpot(RoutingId spotRid, Object request);

    ZLinkActorJoinCall joinEntrySpot(RoutingId spotNodeRid, Object request);
}

interface ZLinkSpot {
    ZLinkSpotActorJoinResponse onActorJoin(
        ZLinkActor actor,
        Message request,
        CancellationToken cancellationToken);

    void onJoinedActor(
        ZLinkActor actor,
        CancellationToken cancellationToken);
}

interface ZLinkEntrySpot {
    void onCreateActor(
        ZLinkActor actor,
        CancellationToken cancellationToken);

    ZLinkSpotActorJoinResponse onActorJoin(
        ZLinkActor actor,
        Message request,
        CancellationToken cancellationToken);

    void onJoinedActor(
        ZLinkActor actor,
        CancellationToken cancellationToken);
}
```

Kotlin은 Java interface를 그대로 구현하거나 coroutine wrapper에서 `suspend` 표면을 제공한다.
Kotlin 이름은 Java와 같은 의미를 유지한다.

## 10. Node.js 목표 표면 초안

```ts
export interface ZLinkActorContext {
  joinSpot(spotRid: RoutingId, request: unknown): ZLinkActorJoinCall;
  joinEntrySpot(spotNodeRid: RoutingId, request: unknown): ZLinkActorJoinCall;
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> {
  onActorJoin?(
    actor: TActor,
    request: Message,
    signal?: AbortSignal
  ): Promise<ZLinkSpotActorJoinResponse>;

  onJoinedActor?(
    actor: TActor,
    signal?: AbortSignal
  ): Promise<void>;
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> {
  onCreateActor?(
    actor: TActor,
    signal?: AbortSignal
  ): Promise<void>;

  onActorJoin?(
    actor: TActor,
    request: Message,
    signal?: AbortSignal
  ): Promise<ZLinkSpotActorJoinResponse>;

  onJoinedActor?(
    actor: TActor,
    signal?: AbortSignal
  ): Promise<void>;
}
```

Node.js는 TypeScript sample에서 `onJoinedActor`를 모두 `onJoinedActor`로 바꾸고, Entry Spot
join에도 request를 넘기는 예제를 둔다.

## 11. C++ 목표 표면 초안

```cpp
struct game_spot_t {
    actor_join_result_t on_actor_join(player_actor_t &actor,
                                      const message_t &request);

    void on_actor_joined(player_actor_t &actor);
};

struct entry_spot_t {
    void onCreateActor(player_actor_t &actor);

    actor_join_result_t on_actor_join(player_actor_t &actor,
                                      const message_t &request);

    void on_actor_joined(player_actor_t &actor);
};
```

C++는 기존 snake_case admission 이름인 `on_actor_join`을 유지한다. commit 이후 callback도
C++ 공개 API 스타일에 맞춰 `on_actor_joined`로 둔다. 언어별 이름은 달라도 의미는
`OnJoinedActor`와 같다.

## 12. core C API 변경 초안

Entry Spot join C API는 user Spot join과 같은 payload/reply 모델을 가져야 한다.

### 12.1 함수 시그니처

초안 작성 당시:

```c
zlink_spot_node_actor_join_entry_spot(
    void *node,
    const zlink_actor_ref_t *actor,
    const zlink_routing_id_t *dest_node_rid,
    zlink_actor_join_entry_spot_handler_fn handler,
    void *userdata,
    uint32_t timeout_ms);
```

목표:

```c
zlink_spot_node_actor_join_entry_spot(
    void *node,
    const zlink_actor_ref_t *actor,
    const zlink_routing_id_t *dest_node_rid,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_actor_join_entry_spot_handler_fn handler,
    void *userdata,
    zlink_send_flags_t flags,
    uint32_t timeout_ms);
```

### 12.2 결과 구조체

Entry Spot join 결과도 user Spot join 결과처럼 accepted 여부와 reply parts를 표현해야 한다.
결과 구조체가 `ActorRef`만 표현한다면 아래 필드가 필요하다.

```c
typedef struct zlink_actor_join_entry_spot_result_t {
    zlink_request_result_t result;
    bool accepted;
    zlink_actor_ref_t actor;
    zlink_msg_t *reply_parts;
    size_t reply_part_count;
} zlink_actor_join_entry_spot_result_t;
```

정확한 필드 이름과 ownership 규칙은 기존 user Spot join result 구조체와 맞춘다. reply parts가
recv-owned인지 caller가 close해야 하는지는 core의 기존 multipart ownership 정책과 동일하게
문서화한다.

### 12.3 Entry Spot dispatch

현재 `zlink_spot_actor_join_recv(...)`와 `zlink_spot_actor_join_reply(...)`가 user Spot join
요청을 처리한다. Entry Spot join admission도 같은 recv/reply 경로를 쓸 수 있는지 확인해야
한다.

가능하면 새 recv/reply 함수를 늘리지 않고 기존 actor join dispatch가 target Spot 종류를
구분해 Entry Spot에도 전달되도록 한다. 그렇지 않으면 Entry Spot 전용 recv/reply API가
필요하지만, 공개 API가 늘어나므로 먼저 기존 경로 확장이 가능한지 검토한다.

## 13. 바인딩 변경 대상

이 변경은 core C API와 모든 바인딩을 함께 바꿔야 한다.

| 바인딩 | 변경 방향 |
|--------|-----------|
| C | `zlink_spot_node_actor_join_entry_spot(...)`에 request parts, flags, reply result 반영 |
| C++ | `join_actor_entry_spot(actor, nodeRid, request)` 추가 또는 기존 시그니처 변경, 결과 타입을 join result로 확장 |
| Java | `SpotNode.joinActorEntrySpot(actor, nodeRid)`를 request를 받는 builder로 변경 |
| Kotlin | Java 변경을 coroutine/extension 표면에 반영 |
| Node.js | native addon과 TypeScript binding의 `joinActorEntrySpot`에 request payload와 reply result 반영 |
| .NET | native interop, binding `SpotNode.JoinActorEntrySpot`, framework context에 request/reply 반영 |
| Python / Go / Rust | 해당 바인딩의 spot node actor entry join operation에 payload와 reply result 반영 |

framework 적용 범위와 core binding 지원 범위는 서로 다를 수 있다. 구현 계획을 만들 때는
먼저 repository의 실제 지원 범위를 확인하고, framework 언어와 low-level binding 언어를
분리해서 추적한다.

### 13.1 지원 범위 inventory

구현을 시작하기 전에 실제 checkout 기준으로 아래 표를 다시 확인한다. 표의 분류가 바뀌면
17장 테스트 요구사항과 20장 완료 기준도 함께 고친다.

| 언어 | 기본 분류 | 완료 기준 |
|------|-----------|-----------|
| C | low-level binding | C API 시그니처, request/reply roundtrip, ownership 회귀 테스트 |
| C++ | low-level binding, framework, sample | binding 테스트, framework callback 테스트, sample release gate |
| Java | low-level binding, framework, sample | binding 테스트, framework callback 테스트, sample release gate |
| Kotlin | Java binding 위의 framework/sample 표면 | Java binding 테스트와 Kotlin compile 또는 wrapper 회귀 테스트 |
| Node.js | low-level binding, framework, sample | binding 테스트, framework callback 테스트, sample release gate |
| .NET | low-level binding, framework, sample | binding 테스트, framework callback 테스트, sample release gate |
| Python | low-level binding | binding request/reply와 ownership 테스트. framework 표면이 없으면 framework 완료 기준에서 제외 사유 기록 |
| Go | low-level binding | binding request/reply와 ownership 테스트. framework 표면이 없으면 framework 완료 기준에서 제외 사유 기록 |
| Rust | low-level binding | binding request/reply와 ownership 테스트. framework 표면이 없으면 framework 완료 기준에서 제외 사유 기록 |

어떤 언어가 제외되더라도 "지원하지 않음"으로만 끝내지 않는다. 제외 사유, 대체 테스트,
나중에 framework 표면을 추가할 때 필요한 follow-up을 작업 결과에 남긴다.

## 14. framework 변경 대상

### 14.1 공통 runtime

- user Spot join과 Entry Spot join이 같은 admission state machine을 사용한다.
- admission이 reject를 반환하면 actor 위치를 바꾸지 않는다.
- admission이 accept를 반환하면 commit을 수행한다.
- commit 성공 뒤에만 `OnJoinedActor`를 호출한다.
- `OnJoinedActor` 실패는 join reply semantics를 흔들지 않도록 lifecycle failure로 기록한다. caller
  join result는 admission accept/reply 결과를 유지하고, monitoring/logging으로 실패를 확인한다.
- commit 중 실패하면 actor 위치와 membership을 이전 상태로 유지하거나 rollback한다. rollback
  의미는 기존 user Spot join 실패 정책과 맞춘다.

### 14.2 Entry Spot actor 생성

actor 객체가 처음 만들어질 때는 `OnCreateActor`를 호출한다. 이는 join admission이 아니다.

actor 생성 직후 Entry Spot membership을 확정할 때는 join admission을 호출하지 않는다. 이
초안의 기본 정책은 다음과 같다.

1. actor object 생성 시 `OnCreateActor`를 호출한다.
2. 생성 직후 Entry Spot 최초 membership commit에는 `OnActorJoin`을 호출하지 않는다.
3. user Spot에서 Entry Spot으로 돌아오는 `JoinEntrySpot(..., request)`에는 `OnActorJoin`을
   호출한다.

이렇게 하면 actor 생성과 actor 이동을 분리할 수 있다. `OnCreateActor`는 생성 초기화만 맡고,
`OnActorJoin`은 이미 존재하는 actor가 명시적으로 Spot을 이동할 때의 admission만 맡는다.

## 15. 문서 변경 대상

정식 구현이 끝난 뒤 아래 문서를 나누어 수정한다.

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/spec/framework-api.ko.md` | 공통 actor join API와 lifecycle 이름 |
| `framework/doc/spec/actor-model.ko.md` | actor create, join admission, joined lifecycle의 차이 |
| `framework/doc/spec/session-actor-dispatch.ko.md` | session-bound actor가 Entry Spot과 user Spot을 오가는 흐름 |
| `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md` | .NET public interface와 async naming |
| `framework/languages/java/doc/spec/handler-interfaces.ko.md` | Java/Kotlin interface와 Spring sample |
| `framework/languages/node/doc/spec/handler-interfaces.ko.md` | TypeScript interface와 NestJS sample |
| `framework/languages/cpp/doc/spec/handler-interfaces.ko.md` | C++ callback 이름과 actor join result |
| `doc/spec/bindings/*` | core C API와 바인딩별 entry join operation |

정식 문서에는 구현된 API만 반영한다. 구현 전에는 이 draft만 링크하거나 계획 문서에서 참조한다.

## 16. sample 변경 대상

sample은 아래 기준으로 정리한다.

1. `onJoinedActor` 이름을 모두 `OnJoinedActor` 계열로 바꾼다.
2. user Spot join request 처리는 `OnActorJoin`에 둔다.
3. Entry Spot join request 처리는 Entry Spot의 `OnActorJoin`에 둔다.
4. actor 생성 초기화는 `OnCreateActor`에 둔다.
5. actor가 user Spot을 떠나 Entry Spot으로 돌아갈 때 필요한 domain reason은
   `JoinEntrySpot(..., request)`로 전달한다.
6. sample에 임시 state map으로 join request를 `onJoinedActor`에 넘기는 workaround를 남기지
   않는다.

## 17. 테스트 요구사항

### 17.1 core

- Entry Spot join이 request parts를 target Entry Spot admission dispatch로 전달한다.
- Entry Spot admission reject가 actor 위치를 바꾸지 않는다.
- Entry Spot admission accept가 actor를 target Entry Spot으로 commit한다.
- Entry Spot admission reply가 caller에게 전달된다.
- timeout과 cancellation에서 request parts ownership이 누수되지 않는다.
- remote node Entry Spot join에서도 request/reply가 유지된다.
- request parts가 0개인 Entry Spot join은 빈 request로 admission에 전달된다.
- malformed request parts가 있으면 기존 user Spot join과 같은 오류 정책을 따른다.
- admission callback이 reply 없이 accept해도 caller가 성공 결과와 빈 reply를 받는다.
- admission callback이 reply와 함께 reject하면 actor 위치는 유지되고 caller가 reject reply를 받는다.
- Entry Spot join 중 기존 user Spot `OnLeaveActor` 호출 순서가 현재 user Spot join/leave 정책과 충돌하지 않는다.
- Entry Spot join 성공 뒤 native actor lookup이 새 Entry Spot 위치를 반환한다.
- stale actor ref 또는 stale generation으로 Entry Spot join을 시도하면 request parts를 소비한 뒤 정해진 오류를 반환하고 actor 위치를 바꾸지 않는다.
- `zlink_spot_actor_join_recv(...)`와 `zlink_spot_actor_join_reply(...)`를 Entry Spot에도 재사용한다면, user Spot과 Entry Spot이 같은 recv/reply ownership 규칙을 갖는지 integration test로 확인한다.
- Entry Spot 전용 recv/reply API를 추가한다면, 기존 user Spot recv/reply와 같은 malformed, timeout, close, cancellation test set을 복제한다.

### 17.2 바인딩

- 각 바인딩의 Entry Spot join builder가 request payload를 보낸다.
- 결과 타입이 accepted/rejected와 reply를 표현한다.
- reply multipart ownership/disposal 규칙이 user Spot join과 같다.
- 기존 payload-less API가 공개 API에 남지 않는다.
- binding public header 또는 generated FFI declaration이 새 C API 시그니처와 일치한다.
- C, C++, Java, Kotlin, Node.js, .NET, Python, Go, Rust 바인딩은 최소 한 개 이상의 Entry Spot join request/reply contract test를 갖는다.
- request 없는 공개 overload가 남지 않는지 공개 API 회귀 테스트로 막는다.
- request가 필요 없는 호출은 overload가 아니라 caller가 빈 `Message` 또는 언어별 empty message object를 넘기는 방식으로 표현한다.
- native runtime을 load하는 테스트는 `bindings/dev_sync_local_core_libs.sh` 실행 뒤 동기화된 `libzlink`를 사용한다.
- local sync로 생긴 native `libzlink.so*` 산출물이 source commit에 포함되지 않는지 staging check를 추가한다.

### 17.3 언어별 바인딩 회귀 테스트

framework 테스트는 framework adapter와 lifecycle policy를 검증한다. 하지만 바인딩
라이브러리 자체가 native Entry Spot join request/reply를 제대로 감싸는지는 별도 테스트로
고정해야 한다. 바인딩 테스트는 framework를 통하지 않고 바인딩 public API만 사용한다.

공통 검증 항목:

- Entry Spot join request가 native C API로 전달된다.
- Entry Spot join accept result가 caller에게 성공 결과와 reply payload를 돌려준다.
- Entry Spot join reject result가 caller에게 reject 결과와 reply payload를 돌려준다.
- Entry Spot join failure가 바인딩별 error/result 정책으로 변환된다.
- request payload, reply payload, packet metadata, flags가 손상되지 않는다.
- reply multipart 또는 message object의 ownership/disposal 규칙이 user Spot join과 같다.
- request 없는 Entry Spot join 공개 API가 남지 않는다.
- 빈 request가 필요한 경우 caller가 명시적으로 빈 `Message` 또는 언어별 empty message object를 넘긴다.
- C header, FFI declaration, generated binding struct layout이 core C API와 일치한다.
- codec extension 또는 serializer가 있는 언어는 object request가 바인딩 표면에서 같은 방식으로 encode된다.
- low-level escape hatch가 필요한 경우에도 sample/framework 업무 코드가 그 escape hatch를 쓰지 않아도 된다.

언어별 최소 테스트 위치와 방향:

| 언어 | 테스트 위치 | 추가할 회귀 테스트 |
|------|-------------|--------------------|
| C | `bindings/c/tests` | `test_c_contract_surface.c`에서 새 C API 시그니처와 기존 payload-less API 제거를 확인한다. behavior test는 Entry Spot join request parts, accept/reject reply, malformed request, timeout/cancel ownership을 검증한다. |
| C++ | `bindings/cpp/tests` | actor header 공개 API 테스트에서 `join_entry_spot(..., request)` 계열 public API를 확인한다. behavior test는 RAII message ownership, accept/reject reply, moved object safety를 검증한다. |
| Java | `bindings/java/src/test`, `bindings/java/tests` | JNI/JNA declaration과 Java public builder가 core C API와 맞는지 확인한다. integration test는 request object 또는 `Message`가 encode되어 Entry Spot admission에 도달하고 reply가 Java result로 decode되는지 검증한다. |
| Kotlin | `bindings/kotlin/samples`와 Java binding test 연동 지점 | Kotlin 전용 바인딩 표면이 있으면 Kotlin compile/contract test를 추가한다. 별도 runtime binding이 없고 Java binding을 그대로 쓰는 구조라면 Kotlin sample compile gate가 새 Java binding API를 사용하는지 확인한다. |
| Node.js | `bindings/node/tests` | `spot_request_to_spot.test.ts`와 같은 위치에 Entry Spot join request/reply test를 추가한다. `api.test.ts` 또는 공개 API 테스트는 기존 payload-less API 제거와 request 인자 필수 여부를 확인한다. |
| .NET | `bindings/dotnet/tests/Zlink.Tests` | public API reflection test로 `JoinEntrySpot(..., request)`와 result type을 확인한다. behavior test는 `Message`/object request encode, accept/reject reply, `IDisposable` ownership을 검증한다. |
| Python | `bindings/python/tests` | `test_core_api_alignment.py`에서 C API alignment를 확인하고, spot callback test에 Entry Spot join request/reply roundtrip을 추가한다. ownership test는 reply multipart close/disposal을 검증한다. |
| Go | `bindings/go`와 `bindings/go/tests` | 공개 API와 계약 테스트에서 function signature와 result type을 확인한다. behavior/ownership test는 request/reply bytes, accept/reject, close semantics를 검증한다. |
| Rust | `bindings/rust/tests` | `ffi_layout_tests.rs`와 `surface_tests.rs`에서 FFI layout과 public API를 확인한다. behavior/ownership tests는 reply ownership, accept/reject result, panic-free drop을 검증한다. |

테스트 이름은 언어별 convention을 따르되, 아래 의미를 반드시 포함한다.

- Entry Spot join sends request payload
- Entry Spot join accept returns reply
- Entry Spot join reject returns reply without moving actor
- Entry Spot join reply ownership matches user Spot join
- payload-less Entry Spot join public API is removed; empty request must be passed explicitly

### 17.4 framework

- `JoinEntrySpot(..., request)`가 Entry Spot `OnActorJoin`에 request를 전달한다.
- reject 시 `OnJoinedActor`를 호출하지 않는다.
- accept 후 commit이 끝난 뒤 `OnJoinedActor`를 호출한다.
- `OnCreateActor`는 actor 생성 때만 호출한다.
- `OnJoinedActor`는 request를 받지 않는다.
- old `onJoinedActor` 이름이 sample과 public interface에 남지 않는다.
- actor 생성 직후 최초 Entry Spot membership에서는 `OnActorJoin`을 호출하지 않는 정책을 테스트로 고정한다.
- user Spot에서 Entry Spot으로 돌아가는 join이 accept되면 이전 user Spot membership 정리와 Entry Spot `OnJoinedActor` 호출 순서가 결정된 정책과 일치한다.
- Entry Spot admission reject는 actor를 이전 user Spot 또는 이전 Entry Spot 위치에 그대로 둔다.
- Entry Spot admission reply가 actor context join result로 그대로 반환된다.
- Entry Spot `OnActorJoin` 실패는 caller에게 정해진 failure result로 전달되고 `OnJoinedActor`를 호출하지 않는다.
- `OnJoinedActor` 실패 처리 정책을 확정하고, caller result와 monitoring/logging 동작을 테스트한다.
- `.NET`, Java, Kotlin, Node.js, C++ framework contract test는 public interface에 `OnJoinedActor` 계열 이름이 있고 old `onJoinedActor` 이름이 없음을 확인한다.
- framework backend fake adapter는 Entry Spot join request parts와 reply parts를 기록해 regression test가 runtime 전달을 검증할 수 있어야 한다.
- framework 테스트는 바인딩 public API를 통과하는 integration path를 최소 한 번 포함해, 바인딩 회귀 테스트와 framework fake test 사이의 간격을 줄인다.

### 17.5 언어별 framework callback 회귀 테스트

각 framework 언어는 callback 호출 순서를 unit 또는 fake backend test로 직접 검증한다. sample
runner만으로는 callback이 정확한 시점에 호출됐는지 구분하기 어렵기 때문이다.

공통 검증 항목:

- user Spot join accept: `OnActorJoin` -> commit -> `OnJoinedActor`
- user Spot join reject: `OnActorJoin`만 호출하고 `OnJoinedActor`는 호출하지 않음
- Entry Spot join accept: Entry Spot `OnActorJoin` -> commit -> Entry Spot `OnJoinedActor`
- Entry Spot join reject: Entry Spot `OnActorJoin`만 호출하고 Entry Spot `OnJoinedActor`는 호출하지 않음
- `OnActorJoin`이 받은 request와 caller가 넘긴 request가 같은 packet name, content type, payload를 가짐
- accept reply가 caller join result로 돌아옴
- reject reply가 caller join result로 돌아오고 actor 위치는 바뀌지 않음
- `OnCreateActor`는 actor 생성 때만 호출되고, Entry Spot 재진입 join에서는 호출되지 않음
- `OnJoinedActor`는 request를 받지 않음
- old `onJoinedActor` 이름을 구현한 test fixture가 더 이상 lifecycle callback으로 호출되지 않음

언어별 최소 테스트 위치와 방향:

| 언어 | 테스트 위치 | 추가할 회귀 테스트 |
|------|-------------|--------------------|
| .NET | `framework/languages/dotnet/tests/Zlink.Framework.ContractTests`와 `Zlink.Framework.E2ETests` | `IZLinkSpot<TActor>`와 `IZLinkEntrySpot<TActor>` fixture에 호출 로그를 남기고, accept/reject별 `OnActorJoinAsync`/`OnJoinedActorAsync` 순서와 reply를 검증한다. 공개 API 테스트는 `onJoinedActor` 제거와 `OnJoinedActorAsync` 추가를 확인한다. |
| Java | `framework/languages/java/zlink-framework-testkit/src/fakeBackendTest`와 `src/contractTest` | fake backend actor join test에서 Entry Spot join request parts를 기록하고, `onActorJoin`/`onJoinedActor` 호출 순서를 검증한다. contract test는 Java sample과 interface source에서 old `onJoinedActor`가 남지 않았는지 확인한다. |
| Kotlin | Java testkit의 Kotlin sample release gate와 Kotlin wrapper test | Kotlin Spot fixture가 Java runtime callback을 구현하거나 coroutine wrapper를 통과할 때 같은 순서로 호출되는지 확인한다. Kotlin sample source는 `override fun onJoinedActor(...)`를 사용해야 한다. |
| Node.js | `framework/languages/node/test/contract/actor-manager.test.js`, `spot-manager.test.js`, `sample-regression.test.js` | fake native coordinator에서 `joinSpot`과 `joinEntrySpot` request를 기록하고, `onActorJoin`/`onJoinedActor` 호출 순서와 reject 시 미호출을 검증한다. 공개 계약 테스트는 `onJoinedActor` 제거와 `onJoinedActor` 추가를 확인한다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.UnitTests`와 contract header/layout tests | test spot fixture에 호출 로그를 두고 `on_actor_join`/`on_actor_joined` 순서를 검증한다. header contract test는 commit 이후 callback 이름을 확정 이름으로만 허용한다. |
| Python / Go / Rust framework | framework 공개 API가 있는 경우 해당 언어 test tree | framework가 아직 draft 또는 미구현이면 이 draft를 기준으로 pending test 또는 계획 항목을 둔다. low-level binding만 있는 언어는 binding entry join request/reply test로 범위를 제한한다. |

테스트 이름은 구현 시점의 기존 naming convention을 따르되, 아래 의미가 이름에서 드러나야 한다.

- `entrySpotJoinAcceptCallsOnActorJoinThenOnJoinedActor`
- `entrySpotJoinRejectDoesNotCallOnJoinedActor`
- `userSpotJoinAcceptCallsOnActorJoinThenOnJoinedActor`
- `onCreateActorIsNotCalledForEntrySpotRejoin`

### 17.6 sample

- TicTacToe, Bingo, SupportChat, DeliveryDispatch, ShoppingMall 계열 sample이 새 이름으로 빌드된다.
- Entry Spot으로 돌아오는 흐름이 있으면 `JoinEntrySpot(..., request)`를 사용한다.
- sample runner가 새 lifecycle 순서를 검증한다.
- sample에서 Entry Spot join request를 임시 field나 map으로 우회 전달하지 않는다.
- sample에서 `OnActorJoin`은 request decode와 accept/reject/reply만 담당하고, `OnJoinedActor`는 commit 이후 알림만 담당한다.
- Entry Spot sample은 actor create 초기화가 `OnCreateActor`에 남아 있고, Entry Spot join admission으로 섞이지 않았음을 보여준다.
- Java/Kotlin, Node.js, .NET, C++ sample release gate는 old `onJoinedActor` 문자열을 금지하거나 migration-only 문서 범위로 제한한다.
- sample README와 guide는 `OnActorJoin`과 `OnJoinedActor` 차이를 같은 용어로 설명한다.

### 17.7 문서 회귀

- 정식 spec과 guide에는 구현되지 않은 Entry Spot join admission 계약을 먼저 넣지 않는다.
- 이 draft와 정식 문서가 서로 다른 공개 계약을 말하지 않도록, 구현 전에는 정식 문서에서 draft 링크만 둔다.
- `framework/doc/spec/draft/README.ko.md`에 이 draft 링크가 유지된다.
- 구현 완료 뒤 정식 문서로 승격할 때 old callback 이름(`onJoinActor`, `OnJoinActor`)이 migration 설명 밖에 남지 않는다.
- `doc/spec/bindings/*` 문서가 새 C API 시그니처, result ownership, 바인딩별 method 이름을 같은 의미로 설명한다.

## 18. Core library 로컬 배포 계획

core C API 시그니처나 public header가 바뀌면 바인딩 테스트는 같은 local core build를
기준으로 실행해야 한다. core 구현과 core regression test가 통과한 뒤, 바인딩 수정 전에
아래 순서로 local core library와 header를 바인딩 작업 영역에 동기화한다.

```bash
cmake --build core/build
/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh
```

`bindings/dev_sync_local_core_libs.sh`는 `core/build/lib`의 `libzlink.so*`와 public header를
각 바인딩 개발 경로로 복사한다. 기본 경로가 아닌 core build를 사용해야 하면
`CORE_LIB_DIR` 환경 변수를 지정한다.

```bash
CORE_LIB_DIR=/path/to/core/build/lib \
  /home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh
```

이 동기화를 건너뛰면 바인딩 테스트가 오래된 native runtime을 로드해 새 symbol, struct
layout, result field를 찾지 못할 수 있다. 바인딩 테스트 실패를 해석하기 전에 실제
로드된 `libzlink` 경로와 빌드 시각을 확인한다.

주의할 점:

- 이 스크립트는 현재 Linux shared library 개발 동기화용이다.
- 스크립트가 복사한 `bindings/*/native/**/libzlink.so*`,
  `bindings/*/prebuilds/**/libzlink.so*`,
  `bindings/*/runtimes/**/native/libzlink.so*` 산출물은 release artifact다.
- 일반 source/doc commit에는 이 native 산출물을 포함하지 않는다.
- commit 전에는 `git status --short`와 `git diff --cached --name-only`로 staged 파일을
  확인한다.
- public header 사본은 바인딩 source 계약에 포함되는 경우가 있으므로, 변경 의도에 맞게
  별도 검토한 뒤 staging한다.

## 19. 문서 수정 계획

구현 전에는 이 draft가 기준이다. 정식 spec과 guide는 실제 public API와 테스트 결과를
확인한 다음 반영한다. 구현 중간 상태를 정식 문서에 먼저 적으면 사용자가 아직 쓸 수 없는
API를 공개 계약으로 오해할 수 있다.

### 19.1 구현 전 문서 규칙

- `framework/doc/spec/draft/entry-spot-join-admission-lifecycle.ko.md`를 설계와 실행 기준으로 유지한다.
- `framework/doc/spec/draft/README.ko.md`에서 이 draft 링크와 요약을 유지한다.
- 구현되지 않은 언어 또는 계층의 정식 spec에는 새 Entry Spot join admission API를 추가하지 않는다.
- 필요하면 정식 spec에서 이 draft를 짧게 링크하되, 아직 구현되지 않은 API가 현재 공개 계약이라고 쓰지 않는다.
- 작업 prompt와 goal 문서는 이 draft 파일 경로를 직접 가리킨다.

### 19.2 구현된 범위의 정식 spec 반영 대상

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/spec/framework-api.ko.md` | `JoinEntrySpot(..., request)`, join result, `OnActorJoin`, `OnJoinedActor`, `OnCreateActor` 책임 분리 |
| `framework/doc/spec/session-actor-dispatch.ko.md` | user Spot과 Entry Spot 이동 순서, accept/reject, reply 전달, callback 호출 순서 |
| `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md` | `.NET` public interface 이름, `Async` suffix, old `OnJoinActor` 제거 |
| `framework/languages/java/doc/spec/handler-interfaces.ko.md` | Java callback 이름, Spring sample handler 표면 |
| `framework/languages/node/doc/spec/handler-interfaces.ko.md` | TypeScript interface, NestJS sample handler 표면 |
| `framework/languages/cpp/doc/spec/handler-interfaces.ko.md` | C++ callback 이름과 join result 타입 |
| `doc/spec/bindings/*` | core C API 변경, 바인딩별 Entry Spot join request/reply 계약, ownership |
| `framework/languages/*/doc/README.ko.md` | 언어별 guide에서 새 lifecycle 이름과 sample 링크 반영 |

구현된 범위를 정식 spec에 반영할 때는 아래 내용을 같은 의미로 맞춘다.

- `OnActorJoin`은 admission callback이며 request를 받는다.
- `OnJoinedActor`는 commit 이후 callback이며 request를 받지 않는다.
- `OnCreateActor`는 actor 생성 초기화 callback이며 join admission이 아니다.
- user Spot과 Entry Spot은 accept/reject/reply 의미가 같다.
- Entry Spot join request는 framework public API에서 low-level bytes나 native parts로 노출하지 않는다.
- old `onJoinActor`/`OnJoinActor` 이름은 migration 설명 밖에 남기지 않는다.

### 19.3 guide와 sample 문서 반영 대상

- Bingo, TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall sample README가 새 callback 이름을 사용한다.
- Entry Spot으로 돌아가는 흐름이 있는 sample 문서는 `JoinEntrySpot(..., request)`를 사용한다.
- sample 문서에는 request를 임시 field, map, raw buffer로 넘기는 예시를 넣지 않는다.
- codec, packet name, Message 처리 설명은 기존 framework codec 정책 문서와 충돌하지 않게 맞춘다.
- 언어별 guide는 같은 lifecycle 용어를 사용한다. 특정 언어 문법 차이만 언어별 문서에서 설명한다.

### 19.4 문서 리뷰 체크

문서 반영 뒤에는 아래 검색으로 빠진 이름과 금지 표현을 확인한다.

```bash
rg -n "onJoinActor|OnJoinActor" framework doc/spec/bindings
rg -n "language[-]exchange|문서[ ]?작성" framework/doc doc/spec/bindings
rg -n "JoinEntrySpot|joinEntrySpot|OnJoinedActor|onJoinedActor|on_actor_joined" framework/doc doc/spec/bindings
```

old callback 이름은 migration 설명이나 changelog 범위에서만 허용한다. 검색 결과가 나오면
각 위치를 읽고 public contract로 남은 것인지, migration 설명인지 구분한다.

## 20. goal 실행 완료 기준

이 draft를 goal로 실행할 때는 아래 항목을 모두 완료해야 한다. 일부 언어가 아직 framework
framework 공개 API를 제공하지 않는다면, 해당 언어는 왜 제외되는지와 어떤 binding test로 대체했는지를
작업 결과에 남긴다.

### 20.1 core 완료 기준

- Entry Spot join C API가 request parts, flags, reply result를 표현한다.
- Entry Spot admission accept/reject/reply가 user Spot join과 같은 의미로 동작한다.
- core regression test가 local/remote Entry Spot join, timeout, cancellation, malformed request,
  stale actor ref, ownership을 검증한다.
- `cmake --build core/build`가 성공한다.

### 20.2 binding 완료 기준

- C, C++, Java, Kotlin, Node.js, .NET, Python, Go, Rust 바인딩이 같은 Entry Spot join 의미를 제공한다.
- 각 바인딩에 Entry Spot join request/reply contract test가 있다. 이 테스트는 framework를 통하지
  않고 바인딩 public API만 사용한다.
- 각 바인딩에 공개 API 회귀 테스트가 있다. 새 request 인자, result type, 기존 payload-less API
  제거 또는 empty request 정책을 직접 확인한다.
- 각 바인딩에 native C API alignment test가 있다. header mirror, FFI declaration, struct layout,
  enum value, ownership 함수 이름이 core C API와 맞는지 확인한다.
- 각 바인딩에 ownership/disposal regression test가 있다. Entry Spot join reply는 user Spot join
  reply와 같은 방식으로 해제되어야 한다.
- 각 바인딩에 accept/reject 양쪽 테스트가 있다. reject 테스트는 actor 위치가 바뀌지 않는다는
  의미까지 검증한다.
- codec extension 또는 serializer가 있는 언어는 object request가 Entry Spot join request로
  encode되고 reply가 result로 decode되는 테스트를 추가한다.
- 기존 payload-less public API 제거를 공개 API 회귀 테스트로 확인한다.
- 빈 request가 필요한 경우 모든 바인딩에서 caller가 명시적으로 빈 `Message` 또는 언어별 empty message object를 넘기는지 테스트한다.
- core 변경 뒤 `/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`를 실행하고,
  native 산출물이 source commit에 섞이지 않았는지 확인한다.
- Kotlin처럼 독립 runtime binding이 아니라 Java binding 위의 표면만 있는 언어는, Java binding
  회귀 테스트와 Kotlin compile/public API gate를 함께 통과해야 완료로 본다.

### 20.3 framework 완료 기준

- `.NET`, Java, Kotlin, Node.js, C++ framework가 `JoinEntrySpot(..., request)`를 제공한다.
- `.NET`, Java, Kotlin, Node.js, C++ framework가 `OnActorJoin`과 `OnJoinedActor`를 분리한다.
- `OnJoinedActor`는 request를 받지 않는다.
- `OnCreateActor`는 actor 생성 때만 호출된다.
- actor 생성 직후 최초 Entry Spot membership은 `OnCreateActor`만 호출하고 `OnActorJoin`과
  `OnJoinedActor`를 호출하지 않는다.
- old `onJoinActor`/`OnJoinActor` public 이름이 interface, sample, 일반 guide에서 제거된다.
- framework fake backend 또는 test adapter가 Entry Spot join request/reply를 검증할 수 있다.
- framework 테스트는 최소 한 개 이상의 바인딩 public API 기반 통합 경로를 포함한다.
  해당 언어에서 framework 표면이 없으면 13.1 inventory 기준으로 제외 사유와 대체 binding
  테스트를 기록한다.
- 언어별 callback 회귀 테스트가 user Spot accept/reject와 Entry Spot accept/reject를 모두 검증한다.

### 20.4 sample 완료 기준

- Bingo, TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall sample이 새 lifecycle 이름으로 빌드된다.
- Entry Spot 재진입 흐름은 `JoinEntrySpot(..., request)`를 사용한다.
- sample 업무 코드에 raw bytes, `Buffer`, 직접 parse/decode helper, 임시 state map이 추가되지 않는다.
- sample release gate가 old callback 이름과 표준 호출 표면 이탈을 잡는다.

### 20.5 문서 완료 기준

- 구현된 public API만 정식 spec에 반영한다.
- draft 내용은 구현 결과와 맞춰 정식 spec, guide, 언어별 README로 나누어 반영한다.
- 정식 문서와 sample 코드의 callback 이름, join result 이름, request 전달 방식이 일치한다.
- 구현 완료 뒤 이 draft는 완료 상태, superseded 상태, 또는 남은 후속 작업 상태 중 하나로 표시한다.

### 20.6 검증 완료 기준

- core test, binding test, framework contract test, sample release gate를 실행한다.
- 실패가 있으면 실패 원인과 남은 범위를 결과에 남긴다.
- 실행하지 못한 test가 있으면 환경 제약과 대체 검증을 적는다.
- commit 전 staged 파일 목록을 확인해 native sync 산출물이 포함되지 않았는지 검토한다.

### 20.7 단계 전환 게이트

주요 단계는 구현과 테스트가 끝났다는 이유만으로 다음 단계로 넘어가지 않는다. 각 단계가
끝날 때 Codex 에이전트를 이용한 독립 리뷰와 POSD 기반 리팩토링을 진행하고, 남은 이슈가
없어야 다음 단계로 진행한다.

적용 대상 단계:

- core C API와 native actor join 구현
- bindings 라이브러리 전체
- framework adapter와 public lifecycle API
- sample과 sample release gate
- 정식 spec, guide, 언어별 README로의 문서 반영

각 단계의 전환 조건:

1. 해당 단계의 구현과 회귀 테스트가 끝난다.
2. 해당 단계의 test suite를 실행하고 결과를 기록한다.
3. Codex 에이전트로 read-only 리뷰를 실행한다.
4. 리뷰는 누락된 요구사항, 공개 API 변경 누락, ownership 오류, callback 순서 오류,
   언어별 의미 차이, 문서와 코드 불일치를 확인한다.
5. 리뷰에서 나온 이슈를 모두 수정한다.
6. 수정 뒤 같은 범위의 테스트를 다시 실행한다.
7. 해당 단계 코드에 대해 POSD 기반 리팩토링 검토를 진행한다.
8. POSD 검토는 얕은 wrapper, 중복 추상화, caller에게 밀려난 내부 지식, raw bytes 노출,
   임시 adapter, 이름만 다른 병렬 API가 남았는지 확인한다.
9. POSD 검토에서 수정이 필요하면 리팩토링하고 테스트를 다시 실행한다.
10. Codex 에이전트로 같은 범위를 한 번 더 read-only 리뷰한다.
11. 마지막 리뷰에서 unresolved issue가 하나도 없어야 다음 단계로 넘어간다.

sample 단계의 추가 전환 조건:

- sample runner와 언어별 release gate를 실행한다.
- sample 업무 코드가 raw bytes, `Buffer`, 직접 parse/decode helper, 임시 state map을 쓰지
  않는지 read-only 리뷰로 확인한다.
- sample이 binding/framework 표준 public API만 사용하는지 확인한다.

문서 반영 단계의 추가 전환 조건:

- AGENTS.md의 doc/spec, doc/guide, doc/internals 역할 분리를 기준으로 문서 위치를 확인한다.
- 금지 표현 검색과 ASCII diagram 규칙 검사를 실행한다.
- 링크와 파일 경로가 실제 checkout에 존재하는지 확인한다.
- 정식 spec에는 구현된 public API만 반영했는지 read-only 리뷰로 확인한다.
- draft가 완료, superseded, 또는 남은 후속 작업 상태 중 하나로 정리됐는지 확인한다.

리뷰 결과는 작업 로그나 PR 설명에 남긴다. 단순히 "리뷰 완료"라고 쓰지 말고, 검토 범위,
실행한 테스트, 발견한 이슈 수, 수정한 이슈, 남은 risk를 적는다.

Codex 리뷰 prompt에는 최소한 아래 항목을 포함한다.

- 이 draft 파일 경로
- 리뷰 대상 단계와 변경 파일 목록
- 해당 단계의 완료 기준
- 실행한 테스트 명령과 결과
- "파일을 수정하지 말고 리뷰 결과만 출력" 지시
- blocker/major/minor 분류
- 남은 issue가 있으면 severity와 관계없이 다음 단계 진행 불가 지시

## 21. 구현 순서

1. 3.1 결정값과 13.1 지원 범위 inventory를 재확인하고 core C API 최종 시그니처를 확정한다.
2. core actor join entry spot request/reply data plane을 구현하고 integration test를 추가한다.
3. `cmake --build core/build`로 core runtime을 다시 빌드한다.
4. core regression test를 실행한다.
5. core C API/native 구현에 대해 Codex read-only 리뷰를 실행하고 남은 이슈를 모두 수정한다.
6. core C API/native 구현에 대해 POSD 기반 리팩토링을 진행한다.
7. `cmake --build core/build`와 core test를 다시 실행한다.
8. core C API/native 구현을 다시 Codex read-only 리뷰로 확인한다. unresolved issue가
   남아 있으면 5번으로 돌아간다.
9. `/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh`로 local core library와 headers를 바인딩 작업 영역에 동기화한다.
10. C binding/header mirror를 맞추고 C binding 회귀 테스트를 추가한다.
11. C++, Java, Kotlin, Node.js, .NET, Python, Go, Rust 바인딩을 core 계약에 맞춘다.
12. 각 바인딩의 공개 API, C API alignment, request/reply roundtrip, ownership 회귀 테스트를 추가한다.
13. 바인딩 테스트 matrix를 먼저 실행해 framework가 올라갈 기반을 고정한다.
14. bindings 라이브러리 전체에 대해 Codex read-only 리뷰를 실행하고 남은 이슈를 모두 수정한다.
15. bindings 라이브러리 전체에 대해 POSD 기반 리팩토링을 진행한다.
16. 바인딩 테스트 matrix를 다시 실행한다.
17. bindings 라이브러리 전체를 다시 Codex read-only 리뷰로 확인한다. unresolved issue가
    남아 있으면 14번으로 돌아간다.
18. framework backend adapter에서 Entry Spot join request/reply를 전달한다.
19. framework public interface에서 `JoinEntrySpot(..., request)`와 `OnJoinedActor` 계열 이름을 반영한다.
20. `onJoinActor`/`OnJoinActor` public 이름을 제거한다.
21. framework 언어별 callback 회귀 테스트를 추가한다.
22. framework contract test와 필요한 integration test를 실행한다.
23. framework 변경 전체에 대해 Codex read-only 리뷰를 실행하고 남은 이슈를 모두 수정한다.
24. framework 변경 전체에 대해 POSD 기반 리팩토링을 진행한다.
25. framework test를 다시 실행한다.
26. framework 변경 전체를 다시 Codex read-only 리뷰로 확인한다. unresolved issue가
    남아 있으면 23번으로 돌아간다.
27. sample을 새 표면으로 수정한다.
28. 언어별 contract test와 sample release gate를 새 이름과 새 Entry Spot join semantics로 바꾼다.
29. sample runner와 언어별 release gate를 실행한다.
30. sample 변경 전체에 대해 Codex read-only 리뷰를 실행하고 남은 이슈를 모두 수정한다.
31. sample runner와 언어별 release gate를 다시 실행한다.
32. sample 변경 전체를 다시 Codex read-only 리뷰로 확인한다. unresolved issue가 남아 있으면
    30번으로 돌아간다.
33. 전체 sample runner와 바인딩 test matrix를 실행한다.
34. native sync 산출물이 source commit에 포함되지 않았는지 staging을 검토한다.
35. 정식 spec/guide/sample 문서를 구현된 API에 맞게 나누어 반영한다.
36. 문서 변경 전체에 대해 Codex read-only 리뷰를 실행하고 AGENTS.md 문서 규칙, 링크, 파일
    경로, 금지 표현을 확인한다.
37. 문서 리뷰에서 나온 이슈를 모두 수정하고 같은 문서 검사를 다시 실행한다.
38. 문서 변경 전체를 다시 Codex read-only 리뷰로 확인한다. unresolved issue가 남아 있으면
    36번으로 돌아간다.

## 22. 결정값 변경 절차

3.1의 결정값을 바꾸려면 아래 순서로 문서를 먼저 고친다.

1. 3.1 결정표를 수정한다.
2. 8-13장의 언어별 target API와 core C API 초안을 수정한다.
3. 17장의 테스트 요구사항을 수정한다.
4. 19장의 문서 반영 대상을 수정한다.
5. 20-21장의 완료 기준과 구현 순서를 수정한다.
6. Codex read-only 리뷰에서 unresolved issue가 없음을 확인한다.

이미 구현된 범위에서 결정값을 바꿔야 한다면 같은 절차를 적용한 뒤, 관련 core, bindings,
framework, sample, 문서, 테스트를 함께 다시 맞춘다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [상위: Draft](./README.ko.md)
<!-- framework-adapter-nav:bottom:end -->
