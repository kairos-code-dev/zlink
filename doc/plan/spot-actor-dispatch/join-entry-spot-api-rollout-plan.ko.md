# JoinEntrySpot API 적용 계획

이 문서는 Actor 를 특정 SpotNode 의 Entry Spot 으로 되돌리는
`JoinEntrySpot` 계열 API 를 core C API 부터 .NET framework 샘플까지 반영하기 위한
실행 계획이다. 현재 공개 계약은 아직 이 API 를 포함하지 않으며, 구현 완료 후
정식 spec 과 guide 에 반영한다.

## 1. 배경

현재 core 에는 Actor 를 user Spot 으로 합류시키는
`zlink_spot_node_actor_join_spot(...)` 만 있다. 이 함수는 target Spot rid 를 직접
받고, 구현에서 Entry Spot target 을 거부한다. 반면 Actor 가 생성될 때나 user Spot 에서
leave 될 때는 Entry Spot 위치로 이동하는 의미가 이미 존재한다.

TicTacToe SessionGateway 샘플에서는 Session 쪽에서 Actor 를 만들고, Play SpotNode 의
Entry Spot 으로 위치를 맞춘 뒤 반환된 Actor ref 로 session bind 를 수행해야 한다.
이 흐름을 user Spot join API 로 표현하면 Entry Spot 이 user Spot 처럼 보이고,
application payload 를 join callback 으로 전달해야 하는지 혼란이 생긴다.

따라서 Entry Spot 이동은 별도 API 로 둔다.

## 2. 결정 사항

1. Entry Spot 은 SpotNode 당 하나만 존재하므로 public API 는 `entrySpotRid` 가 아니라
   `spotNodeRid` 를 받는다.
2. `JoinEntrySpot` 은 message payload 를 받지 않는다.
3. `JoinEntrySpot` 은 application actor join handler 를 호출하지 않는다.
4. `JoinEntrySpot` 성공 시 lifecycle callback 만 발생한다.
   - 이전 위치가 user Spot 이면 user Spot left callback 이 발생한다.
   - target Entry Spot joined callback 이 발생한다.
5. 이미 같은 target SpotNode 의 Entry Spot 에 있는 Actor 는 idempotent success 로
   처리한다.
6. remote SpotNode 로 이동하는 경우 기존 Actor 이동, route 갱신, bound session relay
   갱신 규칙을 따른다.
7. 이번 변경은 source compatibility 를 유지하지 않는다. 기존
   `zlink_actor_join_handler_fn` 이름은 제거하고, user Spot join completion 은
   `zlink_actor_join_spot_handler_fn` 으로 교체한다.
8. 새 Entry Spot API 는 payload 가 없으므로 전용 callback typedef 를 사용한다.
9. core C API 이름 변경은 모든 binding 에 동시에 반영한다. 특정 언어를 follow-up 으로
   남기지 않는다.

## 3. C API 설계

### 3.1 callback typedef

`core/include/zlink/socket.h` 에 아래 typedef 를 추가한다.

```c
typedef void (*zlink_actor_join_spot_handler_fn) (
  zlink_request_result_t result_,
  const zlink_actor_ref_t *actor_,
  zlink_msg_t *parts_,
  size_t count_,
  void *userdata_);

typedef void (*zlink_actor_join_entry_spot_handler_fn) (
  zlink_request_result_t result_,
  const zlink_actor_ref_t *actor_,
  const zlink_routing_id_t *target_node_rid_,
  void *userdata_);
```

의미는 다음과 같다.

| callback | 의미 |
|----------|------|
| `zlink_actor_join_spot_handler_fn` | user Spot join callback 의 명확한 이름이다. 기존 `zlink_actor_join_handler_fn` 을 대체하며 alias 를 남기지 않는다. |
| `zlink_actor_join_entry_spot_handler_fn` | Entry Spot 이동 완료 callback 이다. reply payload 가 없으므로 result, actor ref, target node rid 만 전달한다. |

### 3.2 public 함수

`core/include/zlink/spot.h` 에 아래 함수를 추가한다.

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_node_actor_join_entry_spot (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  const zlink_routing_id_t *dest_node_rid_,
  zlink_actor_join_entry_spot_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_);
```

기존 user Spot join 함수 이름은 유지하되 handler 타입 이름은
`zlink_actor_join_spot_handler_fn` 으로 바꾼다. C ABI 상 함수 심볼은 그대로지만, header 와
binding source 는 새 타입 이름으로 컴파일되게 고친다. 이전 typedef 이름은 호환 alias 로
남기지 않는다.

## 4. Core 구현 계획

### 4.1 공통 검증 정리

`core/src/api/actor/spot/service_spot_actor_api.cpp` 에서 기존
`zlink_spot_node_actor_join_spot(...)` 의 검증과 submit 흐름 중 공통 부분을 helper 로
분리한다.

분리 대상:

- `node_`, `actor_ref_`, `dest_node_rid_`, callback 검증
- actor 존재 여부와 generation 검증
- pending join 중복 검증
- source/target node resolve
- remote 이동 시 target actor placeholder 생성
- timeout 완료 처리와 callback 완료 처리

user Spot join 과 Entry Spot join 의 차이는 target state 선택과 callback 형태다.

### 4.2 user Spot join 유지

기존 `zlink_spot_node_actor_join_spot(...)` 은 다음 의미를 유지한다.

- `dest_spot_rid_` 는 반드시 user Spot 이어야 한다.
- target state 가 Entry Spot 이면 기존처럼 invalid argument 로 거부한다.
- `zlink_spot_actor_join_recv(...)` 와
  `zlink_spot_actor_join_reply(...)` 를 통한 application join handler 흐름을 유지한다.
- callback 인자 형태와 reply parts 전달 의미는 유지하되, typedef 이름은
  `zlink_actor_join_spot_handler_fn` 으로 교체한다.

### 4.3 Entry Spot join 추가

`zlink_spot_node_actor_join_entry_spot(...)` 은 target Spot rid 를 받지 않고,
`dest_node_rid_` 로 찾은 target SpotNode 의 Entry Spot state 를 사용한다.

구현 규칙:

- target node 가 없으면 `ZLINK_REQUEST_NOT_CONNECTED` 로 완료한다.
- target Entry Spot facade/state 가 없으면 내부에서 생성한다.
- application join queue 에 넣지 않는다.
- 성공 시 바로 commit 흐름을 실행한다.
- commit 은 기존 accepted join commit 과 같은 actor 이동 규칙을 재사용하되,
  target state 가 Entry Spot 이어도 허용한다.
- 이전 위치가 user Spot 이면 leave lifecycle 을 예약한다.
- target Entry Spot joined lifecycle 을 예약한다.
- 완료 callback 은 `zlink_actor_join_entry_spot_handler_fn` 으로 호출한다.

### 4.4 helper 후보

코드 중복을 줄이기 위해 아래 내부 helper 를 둔다.

```c++
enum actor_join_target_kind_t
{
    actor_join_target_user_spot,
    actor_join_target_entry_spot
};
```

또는 Entry Spot 은 payload 와 recv/reply 흐름이 없으므로 user Spot join helper 와
Entry Spot commit helper 를 분리한다.

선호안은 두 번째다. user Spot join 은 queued request 와 reply parts 를 다루고,
Entry Spot join 은 즉시 commit 성격이 강하다. 억지로 하나의 helper 로 합치면 조건문이
늘어나고 entry-only 규칙이 user join 흐름 안으로 새어 들어간다.

## 5. Core 회귀 테스트

core 테스트 위치는 기존 actor/spot test 구조를 확인한 뒤 가장 가까운 suite 에 추가한다.

필수 테스트:

1. local Actor 가 user Spot 에서 같은 SpotNode 의 Entry Spot 으로 이동한다.
   - user Spot left lifecycle 발생
   - Entry Spot joined lifecycle 발생
   - callback result 는 ok
2. local Actor 가 이미 Entry Spot 에 있으면 idempotent success 이다.
   - 중복 lifecycle 이 발생하지 않거나 기존 idempotent 정책과 일치해야 한다.
3. remote SpotNode 의 Entry Spot 으로 이동한다.
   - target node 에 Actor placeholder/current route 가 생성된다.
   - source node 의 actor route 가 갱신된다.
   - callback result 는 ok
4. 없는 target node 로 join 하면 submit 은 성공하더라도 callback result 가
   not-connected 로 완료된다. 기존 async request 결과 정책과 맞춘다.
5. pending join 중인 Actor 에 대해 Entry Spot join 을 다시 요청하면 busy/invalid-state 로
   거부된다.
6. Entry Spot join 중 application join recv queue 에 데이터가 생기지 않는다.

## 6. Binding sync

core 구현과 core 테스트가 통과한 뒤 아래 스크립트로 local core library 를 bindings 로
배포한다.

```bash
/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh
```

확인 사항:

- sync 결과로 변경된 native library 산출물을 제외하지 않는다.
- sync 후 모든 binding 의 copied header 와 native library 가 새 core surface 를 포함하는지
  확인한다.
- `bindings/c`, `bindings/cpp`, `bindings/dotnet`, `bindings/go`, `bindings/java`,
  `bindings/node`, `bindings/python`, `bindings/rust` 에 남아 있는
  `zlink_actor_join_handler_fn` 참조를 제거하거나 새 이름으로 교체한다.
- 각 binding 이 `zlink_spot_node_actor_join_entry_spot` symbol 을 로드하거나 FFI/P/Invoke
  표면에 노출하는지 확인한다.
- dirty tree 에 다른 작업 변경이 있어도 이번 API 변경과 sync 산출물만 staging 대상으로
  분리한다.

## 7. Binding 적용 계획

core C API 의 handler typedef 이름을 바꾸므로 이번 작업은 모든 binding 을 같은 rollout 에서
고친다. 한 언어라도 이전 이름을 유지하면 synced header 와 wrapper 구현이 서로 어긋나고,
사용자는 언어마다 다른 계약을 보게 된다.

### 7.1 공통 binding 규칙

모든 binding 에 아래 변경을 적용한다.

- copied `include/zlink/socket.h` 에서 `zlink_actor_join_handler_fn` 을 제거하고
  `zlink_actor_join_spot_handler_fn` 을 사용한다.
- copied `include/zlink/spot.h` 의 `zlink_spot_node_actor_join_spot(...)` handler
  parameter 타입을 `zlink_actor_join_spot_handler_fn` 으로 맞춘다.
- native wrapper, trampoline, callback 등록 코드에서 old typedef 이름을 새 이름으로
  교체한다.
- `zlink_spot_node_actor_join_entry_spot(...)` 을 각 언어 표면에 추가한다.
- Entry Spot join 은 payload/reply parts 를 받지 않는 operation 으로 노출한다.
- user Spot join 과 Entry Spot join 의 이름을 분리한다. user Spot join 은 application
  join payload/reply 를 담당하고, Entry Spot join 은 SpotNode rid 로 lifecycle 이동만
  수행한다.
- old typedef 이름을 compatibility alias 로 남기지 않는다.

### 7.2 binding 별 작업 범위

| binding | 필수 변경 |
|---------|-----------|
| `bindings/c` | synced header 와 sample/test callback 타입을 `zlink_actor_join_spot_handler_fn` 으로 교체한다. Entry Spot join C sample 또는 smoke test 를 추가한다. |
| `bindings/cpp` | header sync 뒤 C++ wrapper callback 타입과 actor join helper 를 새 이름으로 맞춘다. Entry Spot join helper 를 추가한다. |
| `bindings/dotnet` | `ISpotNode.JoinActorEntrySpot(...)`, P/Invoke, operation/result/handler 타입과 회귀 테스트를 추가한다. |
| `bindings/go` | cgo wrapper 의 actor join trampoline cast 를 새 typedef 로 바꾸고 Entry Spot join wrapper 를 추가한다. |
| `bindings/java` | native bridge symbol table, JNI wrapper, public service API 에 Entry Spot join 을 추가하고 old typedef 의 문서/생성물 참조를 제거한다. |
| `bindings/node` | native addon 의 join callback 타입 이름과 TypeScript/JavaScript 표면을 맞추고 Entry Spot join API 를 추가한다. |
| `bindings/python` | ffi symbol 등록과 Python service API 에 Entry Spot join 을 추가한다. |
| `bindings/rust` | synced header 와 unsafe wrapper surface 를 새 typedef 이름으로 맞추고 Entry Spot join wrapper 를 추가한다. |

### 7.3 .NET binding public surface

`bindings/dotnet/src/Zlink/Contracts/Service/Spot.cs` 의 `ISpotNode` 에 아래 API 를
추가한다.

```csharp
ActorJoinEntrySpotOperation JoinActorEntrySpot(
    ActorRef actor,
    RoutingId destNodeRid);
```

operation 타입은 payload 가 없으므로 user Spot join operation 과 분리한다.

```csharp
public sealed record ActorJoinEntrySpotResult(
    RequestResult Result,
    ActorRef Actor,
    RoutingId TargetNodeRid);

public delegate void ActorJoinEntrySpotHandler(
    ActorJoinEntrySpotResult result);

public interface ActorJoinEntrySpotOperation
{
    ActorJoinEntrySpotOperation Timeout(TimeSpan timeout);

    Task<ActorJoinEntrySpotResult> SubmitAsync(
        CancellationToken ct = default);

    bool Submit(ActorJoinEntrySpotHandler callback);
}
```

이름은 C API 와 맞춰 `JoinActorEntrySpot` 을 우선 사용한다. 기존 binding 의
`JoinActor(...)` 문장 순서와 맞추기 위해 `JoinEntrySpotActor` 같은 형태는 피한다.

### 7.4 .NET native interop

`NativeMethods.Service.cs` 에
`zlink_spot_node_actor_join_entry_spot` P/Invoke 를 추가한다.

`ActorInterop` 에 Entry Spot join 전용 submit helper 를 추가한다.

검증 사항:

- timeout 처리 방식은 기존 actor join 과 동일하게 맞춘다.
- cancellation 시 GCHandle 정리 누락이 없어야 한다.
- callback result mapping 이 `RequestResult` 와 일치해야 한다.
- payload parts 가 없으므로 `OperationMessageBuffer` 를 사용하지 않는다.

### 7.5 binding 회귀 테스트

모든 binding 의 기존 actor join 테스트가 새 typedef 이름으로 컴파일되는지 확인한다.
가능한 binding 은 Entry Spot join smoke 또는 unit test 를 추가한다.

`bindings/dotnet` 테스트에는 다음을 추가한다.

1. user Spot 에 join 된 Actor 가 `JoinActorEntrySpot(...)` 으로 Entry Spot 으로 이동한다.
2. callback/async API 둘 다 result 를 돌려준다.
3. invalid actor ref 또는 없는 target node 에 대한 결과가 기존 request result 정책과
   일치한다.

## 8. .NET framework 적용 계획

### 8.1 framework public surface

`IZLinkActorContext` 에 아래 API 를 추가한다.

```csharp
IZLinkActorJoinEntrySpotCall JoinEntrySpot(
    RoutingId spotNodeRid);
```

call 타입은 reply payload 가 없으므로 별도로 둔다.

```csharp
public interface IZLinkActorJoinEntrySpotCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);

    ValueTask SubmitAsync(
        CancellationToken cancellationToken = default);
}
```

`JoinSpot(...)` 은 user Spot 전용으로 유지한다.

### 8.2 runtime 구현

`ZLinkActorContext.JoinEntrySpot(...)` 은 actor 와 target SpotNode rid 를 담은 call 객체를
만든다.

`ZLinkFrameworkRuntimeActors` 와 `ZLinkFrameworkActorFacade` 에 Entry Spot join 경로를
추가한다.

native 경로 조건:

- actor state 에 native actor ref 가 있어야 한다.
- source SpotNode 가 있어야 한다.
- managed stream dispatch 중이라도 기존 user Spot join 과 같은 reentrancy 정책을 적용한다.

local in-memory fallback:

- 같은 process 안의 target node runtime 을 찾을 수 있으면 Entry Spot activation 으로
  lifecycle 을 적용한다.
- 하지만 최종 경로는 native core API 를 우선 사용한다. Entry Spot join 은 actor location
  과 session relay state 갱신을 core 와 일치시켜야 하기 때문이다.

### 8.3 framework 회귀 테스트

`framework/languages/dotnet/tests/Zlink.Framework.E2ETests` 에 다음을 추가한다.

1. `ActorContext_JoinEntrySpot_Moves_UserSpotActor_To_EntrySpot`
   - Actor 를 user Spot 에 join 한다.
   - actor handler 안에서 `actor.Context.JoinEntrySpot(targetNodeRid)` 를 호출한다.
   - user Spot left 와 Entry Spot joined recorder 를 확인한다.
   - 이후 Entry Spot actor packet 이 정상 dispatch 되는지 확인한다.
2. `ActorContext_JoinEntrySpot_Is_Idempotent_WhenAlreadyAtEntrySpot`
   - Entry Spot 에 있는 actor 에 대해 호출한다.
   - 성공하고 중복 이동 부작용이 없는지 확인한다.
3. `SessionGateway_Authenticate_CanBind_Actor_After_PlayEntrySpotJoin`
   - TicTacToe SessionGateway 샘플 흐름을 API 기반으로 되돌린다.
   - Session 에서 actor 생성 또는 actor ref 확보 후 Play SpotNode rid 로
     `JoinEntrySpot(...)` 을 호출한다.
   - 반환된 actor ref 로 session bind 를 수행한다.

## 9. TicTacToe SessionGateway 반영

현재 임시 구조는 Play 서버 요청으로 actor ref snapshot 을 받아 bind 하는 방식이다.
최종 구조는 다음처럼 정리한다.

1. Session 인증 handler 가 API 서버에서 actor id 를 인증한다.
2. Session runtime 또는 Play runtime 에서 Actor 를 생성할 위치를 명확히 정한다.
   - 목표 구조가 “Session 에서 Actor 생성 후 Play EntrySpot 으로 join” 이라면
     Session SpotNode 의 actor factory 와 Play 쪽 actor type/factory 배치가 필요하다.
   - Actor 구현 타입을 Shared 로 둘지, Play 서버 소유로 둘지 별도 결정한다.
3. Actor context 로 `JoinEntrySpot(topology.PlayRid)` 를 호출한다.
4. join 완료 뒤 `BindActorHandleAsync(...)` 로 session bind 를 수행한다.
5. 이후 `JoinMatchReq`, `PlaceMarkReq` 는 Entry Spot actor request 또는 user Spot actor
   request 로 흘러간다.

이 단계에서 Bingo 구조와의 차이는 명확히 유지한다.

- Bingo: Play 서버가 actor 생성과 bind 대상 ref 제공을 담당한다.
- TicTacToe SessionGateway: Session 흐름에서 actor 를 생성하고 Play Entry Spot 으로
  이동한 뒤 bind 한다.

## 10. 문서 반영 계획

구현 완료 뒤 문서는 `draft 정리 -> core/binding spec -> framework spec -> guide/sample`
순서로 갱신한다. 먼저 잘못된 결정을 제거하고, 그 다음 공개 계약을 고정한 뒤,
마지막에 사용법 문서를 맞춘다. 이렇게 해야 guide 가 아직 확정되지 않은 내부 설계를
앞질러 설명하지 않는다.

### 10.1 draft 정리

`doc/spec/draft/actor-gateway-session-relay.ko.md` 는 현재 “EntrySpot direct join 은
허용하지 않는다”는 결정을 담고 있다. 구현 후 이 문장을 그대로 두면 core 와 framework
계약이 충돌한다.

수정 기준:

- “Entry Spot join 금지” 결정을 폐기한다.
- 새 결정은 “Entry Spot join 은 message 없는 lifecycle 이동 API 다”로 쓴다.
- `JoinEntrySpot` 은 actor join handler 를 호출하지 않는다고 명시한다.
- `JoinSpot` 은 user Spot join 과 application join payload/reply 를 담당한다고 분리한다.
- ActorGateway session relay 설명은 Entry Spot join 성공 뒤 current location 이
  Entry Spot 으로 갱신된다는 의미만 반영한다.
- draft 첫머리의 “현재 공개 계약이 아님” 문구는 유지한다.

### 10.2 core C API spec

core 공개 계약 문서가 있으면 `core/include/zlink/spot.h` 의 새 surface 와 같은
의미로 갱신한다. 현재 repo 에서 core C API 정식 spec 위치가 분산되어 있으면,
최소한 `doc/spec/bindings/*` 와 framework spec 이 참조하는 core 계약 문장을 같은
뜻으로 맞춘다.

반영 내용:

- `zlink_actor_join_spot_handler_fn` 은 user Spot join completion callback 이며, 제거된
  이전 이름 `zlink_actor_join_handler_fn` 의 alias 가 아니라고 설명한다.
- `zlink_actor_join_entry_spot_handler_fn` 은 reply parts 를 받지 않는 전용 callback
  이라고 설명한다.
- `zlink_spot_node_actor_join_entry_spot(...)` 의 target 은 `dest_node_rid_` 이며,
  target Spot 은 해당 SpotNode 의 Entry Spot 으로 고정된다고 적는다.
- callback result 값과 timeout/not-connected/idempotent 의미를 기존 async request
  결과 정책과 맞춰 설명한다.
- Entry Spot join 이 `zlink_spot_actor_join_recv(...)` queue 를 만들지 않는다고
  명시한다.

### 10.3 binding spec

binding 문서는 core API 를 각 언어 표면으로 옮긴 계약을 다룬다. 이번 작업은
source compatibility 를 유지하지 않으므로 모든 binding spec 과 README 를 같은 변경 묶음에서
갱신한다. “core API 추가됨, binding surface 반영 예정” 같은 follow-up 상태는 남기지 않는다.

필수 반영:

- `doc/spec/bindings/dotnet/README.md`
  - `ISpotNode.JoinActorEntrySpot(ActorRef actor, RoutingId destNodeRid)` 를 추가한다.
  - `ActorJoinEntrySpotOperation`, `ActorJoinEntrySpotResult`,
    `ActorJoinEntrySpotHandler` 계약을 추가한다.
  - message payload 와 reply parts 가 없다고 명시한다.
  - user Spot join 인 `JoinActor(...)` 와 Entry Spot 이동 API 의 차이를 표로 정리한다.
- `doc/spec/bindings/README.md`
  - core callback 이름을 `zlink_actor_join_spot_handler_fn` 으로 갱신한다.
  - `zlink_actor_join_handler_fn` 은 제거된 이름으로 남기지 않는다.
  - `zlink_spot_node_actor_join_entry_spot(...)` 의 message-less 계약을 추가한다.
- 각 binding README/spec
  - C, C++, Go, Java, Node, Python, Rust, .NET 표면이 모두 같은 의미의 Entry Spot join
    API 를 제공한다고 정리한다.
  - 특정 언어에서 이름이 달라질 경우 언어 관례 때문인지 설명하고, core 의미는 동일하게
    유지한다.

### 10.4 .NET framework spec

framework spec 은 public framework API 와 handler 의미를 정확히 맞춘다.

필수 반영:

- `framework/languages/dotnet/doc/spec/aspnet-core-actor.ko.md`
  - `IZLinkActorContext.JoinEntrySpot(RoutingId spotNodeRid)` 를 추가한다.
  - `JoinSpot(RoutingId spotRid, request)` 는 user Spot join 이라고 제한한다.
  - `JoinEntrySpot` 은 `IZLinkActorJoinEntrySpotCall` 로 끝나며 reply generic 이 없다고
    설명한다.
  - `JoinSpotAsync` 와 string 기반 `JoinSpot` 설명은 제거한다.
- `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md`
  - Entry Spot actor request/send handler 와 Entry Spot join lifecycle 의 차이를
    분리한다.
  - `JoinEntrySpot` 은 `IZLinkEntrySpotActorRequestHandler` 도
    `IZLinkSpotActorJoinHandler` 도 호출하지 않는다고 명시한다.
  - Entry Spot joined/left lifecycle callback 이 어떤 경우 호출되는지 정리한다.
- `framework/languages/dotnet/doc/spec/session-actor-dispatch.ko.md`
  - session bound actor 가 user Spot 에서 Entry Spot 으로 이동해도 session binding 은
    logical binding 으로 유지된다는 점을 추가한다.
  - session handler 가 Entry Spot rid 를 직접 알 필요 없고 SpotNode rid 를 사용한다고
    설명한다.
- `framework/languages/dotnet/doc/spec/spot-node.ko.md`
  - `ConfigureEntrySpot(...)` 의 routing id 설명과 새 `JoinEntrySpot(spotNodeRid)` 의
    target 식별자가 다르다는 점을 명확히 한다.
  - Entry Spot routing id 는 native facade 식별/monitoring 용도이고, framework actor
    API 는 SpotNode rid 로 Entry Spot 을 선택한다고 적는다.

### 10.5 guide 와 sample 문서

guide 는 내부 transport 를 설명하지 않고 사용자가 선택해야 하는 API 차이를 보여 준다.

수정 대상:

- `framework/languages/dotnet/doc/guide/06-actor-session.ko.md`
  - `JoinSpotAsync` 항목을 제거한다.
  - user Spot 이동은 `JoinSpot(roomRid, request).Timeout(...).SubmitAsync<TReply>()` 로
    설명한다.
  - Entry Spot 복귀는 `JoinEntrySpot(playNodeRid).Timeout(...).SubmitAsync()` 로
    설명한다.
- `framework/languages/dotnet/doc/guide/11-interface-catalog.ko.md`
  - `IZLinkActorContext` 표면에서 `JoinSpotAsync` 를 제거하고 `JoinEntrySpot` 을 추가한다.
- `framework/languages/dotnet/doc/guide/samples/bingo-game-sample.ko.md`
  - Bingo 는 기존처럼 Play 서버가 actor ref 를 보장하는 구조임을 유지한다.
- `framework/languages/dotnet/doc/guide/samples/tictactoe-game-sample.ko.md`
  - TicTacToe SessionGateway 와 일반 TicTacToe 샘플의 Entry Spot 사용 차이를
    혼동하지 않도록 정리한다.

### 10.6 문서 회귀 검증

문서 수정 뒤 아래 검색을 반드시 수행한다.

```bash
rg -n "JoinSpotAsync|JoinSpot\\(spotName|EntrySpot direct join 은 허용하지 않는다|EntrySpot direct join|zlink_actor_join_handler_fn" \
  doc framework/languages/dotnet/doc
rg -n "zlink_actor_join_handler_fn" core bindings framework/languages/dotnet \
  -g '!**/bin/**' -g '!**/obj/**'
```

검색 결과 처리 기준:

- `JoinSpotAsync` 는 새 계약에서 제거되었으므로 설명 문서에 남기지 않는다.
- `JoinSpot(spotName` 은 string 기반 API 제거 후 남기지 않는다.
- “EntrySpot direct join 금지” 문장은 새 결정과 충돌하므로 제거하거나 과거 결정으로
  명확히 표시한다.
- draft 에 과거 결정 이력을 남길 때도 현재 결정과 구분되게 “폐기된 결정”으로 쓴다.
- `zlink_actor_join_handler_fn` 은 no-compat 결정에 따라 제거되어야 한다. 과거 결정 설명이
  꼭 필요하면 “제거된 이전 이름”이라고 분명히 쓰고, header/binding source 에는 남기지
  않는다.

문서 회귀 테스트가 있는 경우 다음도 실행한다.

```bash
dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj \
  --filter FullyQualifiedName~Documentation --no-restore
```

### 10.7 반영 대상 요약

| 문서 | 변경 내용 |
|------|-----------|
| `doc/spec/draft/actor-gateway-session-relay.ko.md` | “EntrySpot direct join 은 허용하지 않는다” 결정을 새 결정으로 교체한다. Entry Spot join 은 message-less lifecycle 이동임을 명시한다. |
| `framework/languages/dotnet/doc/spec/aspnet-core-actor.ko.md` | `IZLinkActorContext.JoinEntrySpot(RoutingId spotNodeRid)` public surface 와 의미를 추가한다. `JoinSpot` 은 user Spot 전용이라고 정리한다. |
| `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md` | Entry Spot join 은 actor join handler 를 호출하지 않고 lifecycle callback 만 발생한다고 명시한다. 기존 `JoinSpotAsync` 와 string 기반 `JoinSpot` 언급을 제거한다. |
| `framework/languages/dotnet/doc/guide/06-actor-session.ko.md` | actor 가 user Spot 으로 들어갈 때와 Entry Spot 으로 돌아갈 때의 API 차이를 예제로 설명한다. |
| `doc/spec/bindings/README.md` | core callback 이름을 `zlink_actor_join_spot_handler_fn` 으로 교체하고 `zlink_actor_join_handler_fn` 을 제거된 이전 이름으로 정리한다. |
| `doc/spec/bindings/dotnet/README.md` | .NET binding 의 `JoinActorEntrySpot` surface 와 callback/result 타입을 추가한다. |
| 각 binding README/spec | C, C++, Go, Java, Node, Python, Rust, .NET 의 Entry Spot join 표면과 user Spot join callback 이름을 같은 의미로 맞춘다. |

문서 정리 시 주의할 점:

- Entry Spot 내부를 user guide 에 과도하게 설명하지 않는다.
- spec 문서는 public 계약만 다룬다.
- 내부 ActorGateway transport 세부 구현은 internals 또는 draft 에만 둔다.

## 11. 검증 순서

작업은 다음 순서로 진행한다.

1. draft 에 no-compat 결정과 제거되는 이름을 먼저 고정한다.
2. core C API typedef/function 변경
   - `zlink_actor_join_handler_fn` 제거
   - `zlink_actor_join_spot_handler_fn` 추가
   - `zlink_actor_join_entry_spot_handler_fn` 추가
   - `zlink_spot_node_actor_join_entry_spot(...)` 추가
3. core 구현
4. core 회귀 테스트
5. core build/test
6. `bindings/dev_sync_local_core_libs.sh` 실행
7. 모든 binding 의 synced header, native wrapper, public surface 변경
8. 모든 binding 의 actor join compile/test 와 Entry Spot join 회귀 테스트 추가
9. framework public API 와 runtime 구현
10. framework E2E 회귀 테스트
11. TicTacToe SessionGateway 샘플 수정
12. spec/guide/sample 문서 갱신
13. old-name negative search 와 전체 검증

권장 검증 명령:

```bash
cmake --build core/build
ctest --test-dir core/build --output-on-failure
/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh
rg -n "zlink_actor_join_handler_fn" core bindings framework/languages/dotnet \
  -g '!**/bin/**' -g '!**/obj/**'
dotnet test bindings/dotnet/Zlink.sln --maxcpucount:1 --no-restore
dotnet test framework/languages/dotnet/Zlink.Framework.sln --maxcpucount:1 --no-restore
timeout 90s framework/languages/dotnet/samples/TicTacToe.SessionGateway/run_sample.sh
```

실제 solution/test entrypoint 이름은 현재 repo 상태에 맞춰 확인한 뒤 실행한다. `rg` 검증은
기본적으로 결과가 없어야 한다. 문서에서 과거 이름을 설명해야 하는 경우에는 “제거된 이전
이름” 문맥만 예외로 인정하고, header/source 에 남은 결과는 실패로 본다.

## 12. 완료 기준

완료로 인정하는 조건은 다음과 같다.

- core public header 에 Entry Spot join C API 가 추가되어 있다.
- core public header 에 `zlink_actor_join_handler_fn` 이 남아 있지 않고,
  user Spot join 은 `zlink_actor_join_spot_handler_fn` 을 사용한다.
- core 테스트가 local/remote/idempotent/error case 를 검증한다.
- sync 스크립트 실행 후 bindings native library 가 최신 core 를 포함한다.
- 모든 binding 의 copied header, native wrapper, public surface 가 새 core API 와 이름을
  반영한다.
- 모든 binding 에서 기존 user Spot join compile/test 가 통과한다.
- 가능한 모든 binding 에 Entry Spot join API 와 회귀 테스트 또는 smoke test 가 있다.
- framework `IZLinkActorContext` 에 `JoinEntrySpot(RoutingId spotNodeRid)` 가 있다.
- framework E2E 테스트가 user Spot 에서 Entry Spot 으로 이동하는 흐름을 검증한다.
- TicTacToe SessionGateway 샘플이 `JoinEntrySpot` 기반 흐름으로 동작한다.
- 관련 spec/guide/draft 문서가 서로 모순되지 않는다.
- old typedef 이름과 제거된 framework API 이름에 대한 negative search 가 통과한다.

## 13. 위험 요소

1. core 의 기존 user Spot join 과 Entry Spot join commit 흐름을 무리하게 합치면 조건문이
   늘고 lifecycle 버그가 생길 수 있다. Entry Spot join 은 payload 없는 commit helper 로
   따로 두는 편이 안전하다.
2. remote Entry Spot join 은 session binding relay location 갱신과 직접 연결된다.
   core 테스트에서 bound session 이 stale actor ref 를 쓰지 않는지 확인해야 한다.
3. 기존 draft 문서에는 Entry Spot direct join 금지 결정이 남아 있다. 구현 뒤 문서가
   충돌하면 이후 작업자가 잘못된 결정을 따를 수 있다.
4. native library sync 산출물을 빼고 commit 하면 binding 테스트가 로컬에서는 통과해도
   다른 환경에서 symbol missing 이 날 수 있다.
5. source compatibility 를 유지하지 않는 변경이므로 일부 binding 을 follow-up 으로 미루면
   동일한 core version 안에서 언어별 API 계약이 갈라진다. 모든 binding 변경과 문서 반영을
   같은 commit 범위로 묶어야 한다.
