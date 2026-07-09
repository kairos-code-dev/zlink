# Framework Actor Manager ref 반환 전환 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, 구현과 회귀 테스트가 끝난 뒤
> 언어별 정식 spec/guide 문서에 나누어 반영한다.

## 목적

framework의 actor manager는 route handler, channel handler, session handler 같은 Spot callback
밖의 코드에서도 actor를 만들거나 조회할 수 있게 한다. 그런데 일부 언어의 public API는 생성된 actor
객체 자체를 반환한다. 호출자가 이 객체를 직접 캐스팅하고 상태를 바꾸면 Spot이 보장하는 메시지
직렬 처리 경계를 우회한다.

이 계획의 목적은 actor manager의 public 반환값을 actor 객체에서 `ActorRef`로 바꾸고, actor 생성 시
초기화 payload를 Spot-owned callback으로 전달하는 것이다. actor 객체는 framework runtime과
Spot/Entry Spot callback 내부에만 머문다. Spot 밖의 코드는 actor를 직접 만지지 않고, actor ref를
session bind, actor packet relay, Entry Spot admission 요청에만 사용한다.

## 설계 기준

- actor 객체는 Spot 직렬 처리 경계 안에서만 접근한다.
- Spot 밖 public API는 actor 객체를 반환하지 않는다.
- actor manager 함수 이름은 유지한다. `Create` / `Find` / `GetOrCreate` 계열 이름을 바꾸지 않는다.
- 반환 타입만 actor 객체에서 actor ref로 바꾼다.
- actor 생성 요청에는 optional message payload를 실을 수 있어야 한다.
- 생성 payload는 `OnCreateActor` 계열 callback으로 전달되어야 한다.
- actor 상태 초기화와 상태 변경은 Entry Spot, user Spot, actor request handler, lifecycle callback에서
  처리한다.
- core C API는 이미 actor 객체 대신 `zlink_actor_ref_t`를 반환하지만, 생성 payload를 받지 못하므로
  C API 생성 계약 변경이 필요하다.

## Core C API 기준 상태

| 항목 | As-Is | To-Be |
|------|-------|-------|
| actor 생성 | `zlink_spot_node_actor_new(..., zlink_actor_ref_t *actor_out_)`가 actor id만 받고 actor ref를 반환한다. | actor id와 optional message parts를 함께 받아 actor ref를 반환한다. C API는 계속 actor 객체를 노출하지 않는다. |
| actor 생성 payload | 생성 요청에 payload를 실을 수 없다. 생성 직후 handler가 actor 객체를 직접 만져 초기화하기 쉽다. | 생성 payload를 Entry Spot의 actor-created lifecycle 또는 별도 create callback event로 전달한다. |
| actor 초기 위치 | actor 생성 시 local node의 Entry Spot state에 들어가고 lifecycle joined event가 예약된다. | 유지한다. framework 문서에서 이 전제를 명확히 설명한다. |
| Entry Spot join | `zlink_spot_node_actor_join_entry_spot(...)`은 Entry Spot admission request를 보내고, 다른 node면 actor를 이동한다. 같은 Entry Spot이면 lifecycle은 다시 늘지 않는다. | 유지한다. framework는 이 API를 actor ref 기반 내부 동작으로 감싼다. |

### Core C API 초안 위치

구현 전 C API 계약 초안은 정식 spec 문서에 섞지 않고 별도 draft 문서에 둔다.

- draft: `doc/spec/draft/actor-create-payload.ko.md`
- 구현 전 단계에서는 이 draft를 검토 기준으로 사용한다.
- 구현과 회귀 테스트가 끝난 뒤에는 공개 header에 실제로 들어간 계약만 정식 spec 문서에 나누어 반영한다.
- framework plan 문서는 core API 세부 시그니처를 계약처럼 고정하지 않고, core 변경이 필요한 이유와 적용
  순서만 설명한다.

### Core 구현 계획

core 변경은 framework 변경보다 먼저 끝나야 한다. bindings와 framework는 core C API가 payload를
보존하고 callback까지 전달한다는 계약 위에서만 안전하게 구현할 수 있다.

| 단계 | 변경 내용 |
|------|-----------|
| public header | `core/include/zlink/service/spot.h`의 actor 생성 API에 payload parts 인자를 추가하거나, ABI 유지가 필요하면 `zlink_spot_node_actor_new_with_request`를 추가한다. |
| actor state | actor 생성 요청에 들어온 multipart payload를 actor 생성 lifecycle event와 함께 보관한다. |
| lifecycle event | actor-created lifecycle 또는 Entry Spot create callback에서 payload를 받을 수 있도록 event 구조와 recv API를 확장한다. |
| ownership | payload parts는 core가 callback 소비 시점까지 소유한다. callback/recv 이후에는 기존 join request payload와 같은 방식으로 정리한다. |
| 실패 처리 | payload adopt 실패, callback decode 실패, create callback reject/exception에 해당하는 실패를 actor 생성 실패로 연결한다. |
| 동시성 | 같은 actor id 생성이 겹치면 첫 생성 요청만 actor-created payload를 제공한다. 나머지는 생성 완료 후 같은 actor ref를 받는다. |
| route state | actor 생성 직후 Entry Spot route publish와 lifecycle event 순서가 기존과 호환되는지 유지한다. |

### Core 회귀 테스트 계획

core 테스트는 framework나 bindings를 거치지 않고 C API 계약을 직접 검증한다.

| 테스트 | 기대 결과 |
|--------|-----------|
| actor 생성 payload 전달 | `zlink_spot_node_actor_new`에 넣은 message parts가 Entry Spot actor-created callback에서 같은 순서와 payload로 보인다. |
| empty payload | payload가 없으면 callback은 empty message 또는 part_count 0으로 일관되게 받는다. |
| multipart payload | 여러 part를 넣어도 part 순서와 ownership이 유지된다. |
| callback 실패 | create callback이 reject/failure를 반환하면 actor ref가 외부에 성공으로 반환되지 않는다. |
| payload adopt 실패 | invalid multipart payload는 actor 생성 전에 실패하고 actor route가 남지 않는다. |
| 같은 actor id 재생성 | 이미 존재하는 actor에 대한 get-or-create 성격 호출은 새 payload로 actor를 다시 초기화하지 않는다. |
| 동시 생성 | 같은 actor id로 여러 생성 요청이 겹치면 하나만 생성되고 첫 payload만 create callback에 전달된다. |
| lifecycle 순서 | actor 생성 시 Entry Spot joined lifecycle과 actor-created payload callback 순서가 문서화한 순서와 맞다. |
| destroy cleanup | create 실패나 destroy 후 payload storage가 누수되지 않는다. |

실행 기준:

- `cmake --build core/build`
- core actor/spot integration test
- C binding contract test가 core header 변경을 따라 빌드되는지 확인

## Core 변경의 bindings 배포 계획

core C API가 바뀌면 bindings와 framework를 같은 checkout에서 바로 검증해야 한다. 릴리스 전 개발
검증은 `scripts/local-package/native/sync-local-core-libs.sh`를 사용한다.

### 로컬 core 산출물 동기화

1. core를 먼저 빌드한다.

```bash
cmake --build core/build
```

2. 로컬 core headers와 `libzlink.so*`를 bindings 워크스페이스로 동기화한다.

```bash
scripts/local-package/native/sync-local-core-libs.sh
```

스크립트 기준:

- 기본 core runtime 경로는 `core/build/lib`이다.
- `CORE_LIB_DIR`로 다른 core lib 디렉토리를 지정할 수 있다.
- C/C++/Go/Rust binding include 디렉토리에 public headers를 복사한다.
- .NET, Java, Node, Python 등 native runtime 디렉토리에 Linux shared library를 복사한다.
- 이 스크립트가 쓴 `bindings/*/native/libzlink.so*`, `prebuilds`, `runtimes` 산출물은 개발용 동기화 결과다. release artifact이므로 커밋하지 않는다.
- 커밋 전에는 script 출력 안내처럼 native library 산출물을 복구한다.

### bindings 라이브러리 적용 계획

bindings는 core C API 변경을 언어별 native 표면에 먼저 반영하고, framework는 그 뒤에 적용한다.

| binding | 적용 내용 | 검증 |
|---------|-----------|------|
| C | `zlink_spot_node_actor_new` 시그니처 또는 새 `*_with_request` 함수 header/source 반영. actor-created payload recv/reply API가 생기면 C wrapper와 contract test 추가. | `bindings/c` build, C contract tests |
| C++ binding | core header sync 후 actor 생성 payload helper가 필요한지 확인한다. framework C++가 core C API를 직접 쓰는 경로가 있으면 typed facade를 추가한다. | C++ binding build/tests |
| .NET binding | P/Invoke/native wrapper의 actor create 시그니처를 payload parts 인자까지 반영한다. `Message` ownership과 SafeHandle lifetime을 검증한다. | `bindings/dotnet/tests/run_tests.sh` |
| Java binding | Panama/JNI native mapping에서 actor create payload 인자를 반영한다. native resource loader가 dev sync runtime을 사용하도록 확인한다. | `bindings/java/tests/run_tests.sh` |
| Node binding | native addon의 actor create binding에 payload parts 인자를 추가한다. `Buffer` ownership과 async callback lifetime을 검증한다. | Node binding tests |
| Python/Go/Rust | framework 직접 대상은 아니지만 core header 변경으로 빌드가 깨지지 않게 최소 mapping 또는 wrapper를 갱신한다. | 각 binding build/tests |

bindings 적용 원칙:

- binding public API는 raw pointer나 native struct를 노출하지 않는다.
- VM 언어는 message object 또는 byte payload ownership을 native 호출 직전에 명확히 이전한다.
- callback이 끝나기 전에 GC/RAII가 native payload storage를 해제하지 않도록 test를 둔다.
- bindings 변경은 framework 변경보다 먼저 검증한다.
- dev sync로 복사된 native library 파일은 커밋 대상에서 제외한다.

## 언어별 As-Is

| 언어 | actor manager public 반환 | 확인한 문제 |
|------|---------------------------|-------------|
| .NET | `IZLinkActorManager.CreateAsync` / `FindAsync` / `GetOrCreateAsync`가 `IZLinkActor` 또는 nullable `IZLinkActor`를 반환한다. | route/channel/session handler가 실제 actor 객체를 받아 캐스팅하고 상태를 바꿀 수 있다. Bingo `EnsurePlayerActorHandler`가 `PlayerActor.SetDisplayName`을 직접 호출한다. |
| Java | `ZLinkActorManager.create` / `find` / `getOrCreate`가 `ZLinkActor` 또는 `Optional<ZLinkActor>`를 반환한다. | Spring bean으로 노출된 manager에서도 actor 객체가 handler 밖으로 나온다. Java Bingo/SupportChat/DeliveryDispatch/TicTacToe 샘플이 actor 객체에서 context를 꺼내 `joinEntrySpot`을 호출한다. |
| Kotlin | Kotlin은 Java core `ZLinkActorManager`를 그대로 사용한다. suspending wrapper는 handler 쪽 편의만 제공한다. | Kotlin 샘플도 Java와 같은 actor 객체 반환 표면을 사용한다. |
| Node/NestJS | `ZLinkActorManager.create` / `find` / `getOrCreate`가 `Promise<ZLinkActor>` 또는 `Promise<ZLinkActor | undefined>`를 반환한다. | NestJS handler가 actor 객체를 캐스팅하고 actor 상태나 context에 직접 접근한다. Bingo와 SupportChat 샘플에 직접 접근이 많다. |
| C++ | `session_actor_manager_t::create` / `find` / `get_or_create`가 `session_actor_t`를 반환한다. `session_actor_t`는 ref 기반 핸들이지만 `context()`와 join API를 갖는다. | 실제 actor 객체를 직접 반환하지는 않지만, Spot 밖에서 actor join과 relay를 수행할 수 있는 actor handle이 public으로 나온다. 공통 정책과 맞추려면 ref만 반환해야 한다. |

## 공통 To-Be

| API | As-Is | To-Be |
|-----|-------|-------|
| create | actor 객체 또는 actor handle 반환 | actor ref 반환. optional 생성 payload를 받는다. |
| find | actor 객체/handle optional 반환 | actor ref optional 반환 |
| getOrCreate | actor 객체 또는 actor handle 반환 | actor ref 반환. 새 actor를 만들 때만 optional 생성 payload를 사용한다. |
| session bind | 언어별로 actor 객체와 actor ref를 모두 받을 수 있음 | public guide에서는 actor ref bind만 표준으로 쓴다. 기존 actor 객체 overload는 제거하거나 내부 전용으로 낮춘다. |
| actor context 접근 | manager가 반환한 actor에서 context 접근 가능 | actor context는 actor instance 내부와 Spot/actor handler callback에서만 접근 가능 |
| actor create callback | actor 객체만 받음 | actor 객체와 생성 payload를 함께 받음 |

언어별 이름은 유지한다.

| 언어 | To-Be 반환 타입 |
|------|-----------------|
| .NET | `ValueTask<ActorRef>`, `ValueTask<ActorRef?>` |
| Java | `CompletionStage<ZLinkActorRef>`, `CompletionStage<Optional<ZLinkActorRef>>` |
| Kotlin | Java API를 그대로 받되 coroutine sample에서는 `ZLinkActorRef`를 `await()`한다. 필요하면 suspending facade도 `ZLinkActorRef`를 반환한다. |
| Node/NestJS | `Promise<ActorRef>`, `Promise<ActorRef | undefined>` |
| C++ | `result_t<actor_ref_t>`, `std::optional<actor_ref_t>` |

## 구현 계획

### 1. 내부 runtime과 public API 분리

각 언어 runtime은 내부적으로 actor 객체를 계속 보관한다. Spot dispatch, actor request handler,
lifecycle callback, Entry Spot admission에는 실제 actor instance가 필요하다. 바뀌는 것은 public
manager가 actor instance를 반환하지 않는다는 점이다.

- 내부 생성 결과 타입은 actor object와 actor ref를 함께 가질 수 있다.
- public manager service는 내부 생성 결과에서 actor ref만 추출해 반환한다.
- 내부 dispatcher와 remote actor joiner는 기존처럼 actor object를 찾아 callback에 넘긴다.
- actor ref 갱신이 필요한 remote join 결과는 runtime state에 반영한다.

### 2. 생성 payload와 OnCreateActor 연결

actor manager의 `Create` / `GetOrCreate`는 optional request payload를 받을 수 있어야 한다. 이 payload는
actor factory 생성자나 외부 handler가 직접 해석하지 않는다. runtime이 payload를 저장한 뒤 Entry Spot
또는 actor-created lifecycle callback으로 넘긴다.

언어별 callback 변경 방향:

| 언어 | As-Is | To-Be |
|------|-------|-------|
| .NET | `OnCreateActorAsync(PlayerActor actor, CancellationToken)` | `OnCreateActorAsync(PlayerActor actor, ZLinkMessage request, CancellationToken)` 또는 typed overload |
| Java | `onCreateActor(PlayerActor actor)` | `onCreateActor(PlayerActor actor, ZLinkMessage request)` 또는 typed overload |
| Kotlin | Java callback 형태를 따른다. | suspending callback이면 request 인자를 함께 받는다. |
| Node/NestJS | `onCreateActor(actor)` | `onCreateActor(actor, request)` |
| C++ | `onCreateActor(actor)` | `onCreateActor(actor, message_t request)` 또는 typed callback |

생성 payload는 actor 객체 초기화에만 사용한다. user Spot join admission payload와는 역할이 다르다.

예를 들어 Bingo의 `EnsurePlayerActorReq`는 다음처럼 나뉜다.

| 정보 | 전달 위치 | 이유 |
|------|-----------|------|
| `ActorId` | actor manager create/getOrCreate key | actor identity |
| `DisplayName` | actor 생성 payload 또는 create callback typed payload | `PlayerActor` 초기 상태 |
| `PreferredActorNodeRid` | Entry Spot join/gateway target | actor 배치 대상 node |

`EnsurePlayerActorHandler`는 actor 객체를 직접 받지 않는다. actor manager에 생성 payload를 넘겨 actor ref를
받고, 필요하면 actor ref 기반 Entry Spot join API로 preferred node admission을 수행한다.

### 3. Entry Spot admission helper 정리

기존 샘플은 manager에서 actor를 받은 뒤 `actor.Context.JoinEntrySpot(...)` 또는 `actor.context().joinEntrySpot(...)`을
호출한다. 반환 타입을 actor ref로 바꾸면 이 호출 경로가 사라진다.

공통 대안은 다음 둘 중 하나를 검토했다.

| 대안 | 내용 | 판단 |
|------|------|------|
| A | actor manager가 `GetOrCreate`만 제공하고, Entry Spot join은 별도 public gateway API로 제공한다. | Spot 밖 코드에 admission 제어를 다시 노출하므로 취소한다. |
| B | actor manager의 `GetOrCreate`가 생성 후 target Entry Spot admission까지 함께 수행할 수 있는 overload를 제공한다. | 호출부가 단순하지만 생성과 admission 의미가 섞이므로 채택하지 않는다. |

최종 방향은 별도 session relay public API를 두지 않는 것이다. actor manager는 actor 생성과 조회만
맡고 `ActorRef`를 반환한다. actor join/admission은 Entry Spot이 소유한 workflow 또는 framework 내부
runtime 경로에서만 처리한다.

### 4. actor 상태 변경 경로 이동

actor 상태 변경은 actor manager 호출 직후에 하지 않는다. 기존 샘플의 직접 상태 변경은 다음 방식으로
옮긴다.

| 기존 코드 | 변경 방향 |
|-----------|-----------|
| `actor.SetDisplayName(...)` | actor 생성 payload를 받은 `OnCreateActor` 또는 actor request handler에서 처리 |
| `actor.context().joinEntrySpot(...)` | Entry Spot이 소유한 생성/초기화 workflow 또는 framework 내부 runtime 경로로 처리 |
| `actor.context().joinSpot(...)` | Entry Spot actor request handler 같은 Spot-owned workflow 안에서만 처리 |
| actor 객체 필드 직접 읽기 | actor request payload, actor state snapshot reply, 또는 Spot-owned domain state에서 읽기 |

Bingo의 경우 `EnsurePlayerActorReq`의 `DisplayName`은 actor 생성 payload로 넘기고,
`BingoEntrySpot.OnCreateActorAsync` 또는 동등한 actor-created callback에서 `PlayerActor` 상태에 반영한다.
room join에 필요한 `ActorId`와 `DisplayName`은 actor request handler가 actor 상태에서 읽거나, client/API
request payload에서 다시 받는다. `PreferredActorNodeRid`는 actor 생성 payload가 아니라 Entry Spot join
target으로 사용한다.

## 언어별 변경 계획

### .NET

As-Is:

- `IZLinkActorManager`가 `IZLinkActor`를 반환한다.
- `ZLinkActorManagerService`가 `CreateActorAsync` / `CreateLocalActorAsync` 결과의 `Actor`를 그대로 반환한다.
- backend wrapper의 `CreateActor`는 actor id만 native C API로 전달한다.
- 샘플과 e2e가 반환 actor에서 `Context`를 꺼내 join을 수행한다.

To-Be:

- `IZLinkActorManager.CreateAsync`는 `ValueTask<ActorRef>`를 반환하고 optional `ZLinkMessage` 또는 typed request overload를 제공한다.
- `IZLinkActorManager.FindAsync`는 `ValueTask<ActorRef?>`를 반환한다.
- `IZLinkActorManager.GetOrCreateAsync`는 `ValueTask<ActorRef>`를 반환하고 새 actor 생성 시에만 payload를 사용한다.
- runtime 내부 `CreateActorResult`는 유지하되 public service에서 `NativeActorRef`를 `ActorRef`로 변환해 반환한다.
- backend contract `CreateActor`는 message payload를 C API에 전달할 수 있어야 한다.
- `OnCreateActorAsync`는 생성 payload를 받을 수 있게 바꾼다.
- actor ref 기반 Entry Spot join API를 추가한다.
- `IZLinkActorContext`는 actor instance 내부 표면으로 유지하고, actor manager 반환값에서는 접근할 수 없게 한다.

수정 대상:

- `Contracts/Actors/IZLinkActorManager.cs`
- `Contracts/Spots/ZLinkSpot.cs`
- `Runtime/Actors/ZLinkActorManagerService.cs`
- `Runtime/Actors/ZLinkActorCreationCoordinator.cs`
- `Runtime/Host/ZLinkFrameworkRuntimeActors.cs`
- `Runtime/Backend/Contracts/IZLinkBackendSpotContracts.cs`
- `Runtime/Backend/DotNet/Wrappers/ZLinkBackendSpotNodeWrapper.cs`
- contract/unit/e2e tests
- Bingo, SupportChat, DeliveryDispatch, TicTacToe 샘플의 actor manager 사용부

적용 순서:

1. bindings/dotnet의 native actor create wrapper가 payload parts를 받을 수 있게 한다.
2. framework backend contract `CreateActor`에 `ZLinkMessage` 또는 encoded payload를 추가한다.
3. `IZLinkActorManager` 반환 타입을 `ActorRef`로 바꾸고 create/get-or-create typed payload overload를 추가한다.
4. `OnCreateActorAsync` payload 인자를 적용한다.
5. Bingo `EnsurePlayerActorHandler`의 `SetDisplayName` 직접 호출을 제거하고 create payload로 이동한다.
6. `.NET` contract tests와 `SpotService` e2e를 갱신한다.

### Java

As-Is:

- `ZLinkActorManager`가 `ZLinkActor`를 반환한다.
- `ZLinkActorRuntime`은 backend `createActor`로 `ZLinkBackendActorRef`를 받은 뒤 actor factory를 실행하고 actor 객체를 반환한다.
- backend `createActor`는 actor id만 받는다.
- Spring bean wrapper도 같은 반환 타입을 노출한다.

To-Be:

- `ZLinkActorManager.create`는 `CompletionStage<ZLinkActorRef>`를 반환하고 optional request overload를 제공한다.
- `find`는 `CompletionStage<Optional<ZLinkActorRef>>`를 반환한다.
- `getOrCreate`는 `CompletionStage<ZLinkActorRef>`를 반환하고 새 actor 생성 시에만 payload를 사용한다.
- runtime 내부 map은 actor 객체와 context를 계속 보관한다.
- backend `createActor`는 message payload를 C API에 전달한다.
- `onCreateActor`는 생성 payload를 받을 수 있게 바꾼다.
- actor ref 기반 Entry Spot join API를 추가하거나 기존 runtime 내부 joiner를 public gateway로 감싼다.
- Spring Boot starter bean 반환 타입도 함께 바꾼다.

수정 대상:

- `zlink-framework-core/src/main/java/systems/zlink/framework/actors/ZLinkActorManager.java`
- `runtime/actors/ZLinkActorRuntime.java`
- backend adapter contracts
- `zlink-framework-spring-boot-starter/.../ZLinkFrameworkActorManagerBean.java`
- Java integration tests
- Java samples

적용 순서:

1. bindings/java native mapping에 actor create payload 인자를 반영한다.
2. Java framework backend adapter `ZLinkBackendSpotNode.createActor`에 message payload를 추가한다.
3. `ZLinkActorManager` 반환 타입을 `ZLinkActorRef`로 바꾸고 create/get-or-create payload overload를 추가한다.
4. `onCreateActor` payload 인자를 적용한다.
5. Java Bingo/SupportChat/DeliveryDispatch/TicTacToe 샘플에서 actor 객체 직접 접근을 제거한다.
6. Spring bean wrapper와 integration tests를 갱신한다.

### Kotlin

As-Is:

- Kotlin sample은 Java `ZLinkActorManager`를 주입받아 `await()`로 `ZLinkActor`를 받는다.
- Kotlin-specific suspending actor manager 표면은 별도로 크지 않고 Java core에 의존한다.

To-Be:

- Java 변경을 그대로 따른다.
- Kotlin guide와 sample은 `val actorRef = actors.getOrCreate(...).await()` 형태로 바꾼다.
- 생성 payload를 넘기는 typed overload가 필요하면 Kotlin helper를 추가한다.
- actor 상태 변경은 `onCreateActor` 또는 actor handler 내부로 옮긴다.

수정 대상:

- Kotlin samples under `framework/languages/java/samples/kotlin/`
- Kotlin guide/interface catalog
- Java core 변경으로 깨지는 Kotlin tests

적용 순서:

1. Java core 변경을 받은 뒤 Kotlin sample compile 오류를 먼저 정리한다.
2. Kotlin coroutine 호출부가 `ZLinkActorRef`를 받도록 바꾼다.
3. 생성 payload helper가 필요하면 Kotlin extension 또는 suspending helper를 추가한다.
4. Kotlin Bingo/SupportChat/DeliveryDispatch/TicTacToe 샘플에서 actor 객체 직접 접근을 제거한다.
5. Kotlin sample smoke를 실행한다.

### Node/NestJS

As-Is:

- `ZLinkActorManager`가 `Promise<ZLinkActor>`를 반환한다.
- `DefaultZLinkActorManager`는 내부 `createOrGet` 결과에서 `actor`를 반환한다.
- backend `createActor`는 actor id만 받는다.
- Node samples가 반환 actor를 캐스팅해서 actor 상태와 `context`에 접근한다.

To-Be:

- `ZLinkActorManager.create`는 `Promise<ActorRef>`를 반환하고 optional request를 받을 수 있다.
- `find`는 `Promise<ActorRef | undefined>`를 반환한다.
- `getOrCreate`는 `Promise<ActorRef>`를 반환하고 새 actor 생성 시에만 request를 사용한다.
- `DefaultZLinkActorManager`는 내부 actor state를 유지하되 public 반환은 `ActorRef`로 제한한다.
- backend `createActor`는 message payload를 C API에 전달한다.
- `onCreateActor`는 생성 request를 받을 수 있게 바꾼다.
- 내부 host/spot runtime에서 routed actor 제공이 필요한 경로는 internal-only method를 사용한다.
- actor ref 기반 Entry Spot join API를 추가한다.

수정 대상:

- `packages/framework/src/contracts/Actors/ZLinkActorManager.ts`
- `packages/framework/src/runtime/actors/index.ts`
- `packages/framework/src/runtime/backend/contracts/index.ts`
- `packages/framework/src/runtime/host/index.ts`
- NestJS provider wiring
- Node samples and tests

적용 순서:

1. bindings/node native addon에 actor create payload 인자를 반영한다.
2. framework backend contract `createActor`에 request payload를 추가한다.
3. `ZLinkActorManager` 반환 타입을 `ActorRef`로 바꾸고 create/get-or-create request overload를 추가한다.
4. `onCreateActor(actor, request)` 계약을 적용한다.
5. NestJS provider와 host internal actor provider가 public manager 변경을 우회하지 않게 internal-only 경로를 분리한다.
6. Node Bingo/SupportChat/DeliveryDispatch/TicTacToe 샘플과 framework tests를 갱신한다.

### C++

As-Is:

- `session_actor_manager_t::create` / `find` / `get_or_create`가 `session_actor_t`를 반환한다.
- `session_actor_t`는 actor ref 기반 핸들이지만 `context()`와 join API를 제공한다.
- 생성 manager는 actor id/type만 받고 생성 payload를 Spot callback으로 전달하지 않는다.
- C++ sample 일부는 이미 ensure response의 actor ref를 session bind에 사용한다.

To-Be:

- `session_actor_manager_t::create`는 `result_t<actor_ref_t>`를 반환하고 optional `message_t` 또는 typed request overload를 제공한다.
- `find`는 `std::optional<actor_ref_t>`를 반환한다.
- `get_or_create`는 `result_t<actor_ref_t>`를 반환하고 새 actor 생성 시에만 request를 사용한다.
- session bind는 `actor_ref_t`만 받는 형태를 표준으로 유지한다.
- `onCreateActor`는 생성 request를 받을 수 있게 바꾼다.
- Spot 밖 public handler는 join/admission을 직접 수행하지 않는다.
- join/admission이 필요하면 Entry Spot actor request handler 같은 Spot-owned workflow 또는 framework 내부 runtime 경로로 옮긴다.
- 내부 dispatch용 `actor_context_t`와 actor instance 접근은 Spot runtime 내부에 둔다.

수정 대상:

- `framework/languages/cpp/framework/include/zlink/framework/contracts/actors/actor.hpp`
- `framework/languages/cpp/framework/include/zlink/framework/contracts/spots/spot.hpp`
- `framework/languages/cpp/framework/src/runtime/actors/actor_gateway_runtime.cpp`
- `framework/languages/cpp/framework/src/runtime/host/actor_gateway_spot_bridge.cpp`
- `framework/languages/cpp/framework/src/runtime/spots/spot_runtime.cpp`
- C++ samples and regression tests

적용 순서:

1. C/C++ headers sync 후 C++ framework가 새 core create payload API를 호출하게 한다.
2. `session_actor_manager_t` 반환 타입을 `actor_ref_t`로 바꾼다.
3. `onCreateActor(actor, message_t request)` 또는 typed callback을 추가한다.
4. Spot 밖 public join/admission 호출을 제거하고 Spot-owned workflow로 옮긴다.
5. C++ Bingo/SupportChat/DeliveryDispatch/TicTacToe 샘플과 regression tests를 갱신한다.

## Framework 문서 반영 계획

정식 문서는 구현 완료 뒤 반영한다. 계획 단계에서는 아래 문서가 반영 대상이다.

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/framework/common/spec/actor-model.ko.md` | actor 객체는 Spot 내부 소유이고, 외부 public API는 actor ref만 사용한다는 공통 원칙 추가 |
| `framework/doc/framework/common/spec/session-actor-dispatch.ko.md` | session bind 표준 입력을 actor ref로 고정 |
| `framework/doc/framework/common/spec/framework-api.ko.md` | actor manager 반환 타입과 생성 payload 공통 정책 추가 |
| `framework/doc/framework/dotnet/spec/aspnet-core-actor.ko.md` | `.NET` actor manager와 `OnCreateActorAsync` 계약 갱신 |
| `framework/doc/framework/java/spec/spring-boot-actor-session.ko.md` | Java/Kotlin actor manager와 `onCreateActor` 계약 갱신 |
| `framework/doc/framework/node/spec/nestjs-actor.ko.md` | Node actor manager와 `onCreateActor` 계약 갱신 |
| `framework/doc/framework/cpp/spec/actor-gateway-session-relay.ko.md` | C++ `session_actor_manager_t` 반환 정책과 생성 payload 계약 갱신 |
| 언어별 guide `06-actor-session.ko.md` / C++ `09-actor-session.ko.md` | 샘플 코드에서 actor 객체 직접 접근 제거 |
| 언어별 interface catalog | 반환 타입, 생성 payload overload, 제거된 overload 반영 |
| `framework/doc/framework/common/sample/bingo/README.ko.md` | Bingo ensure actor와 actor 생성 payload 흐름을 actor ref 기준으로 수정 |
| `framework/doc/framework/common/sample/supportchat/README.ko.md` | SupportChat actor ensure, session bind, conversation join 흐름을 actor ref 기준으로 수정 |
| `framework/doc/framework/common/sample/deliverydispatch/README.ko.md` | DeliveryDispatch customer actor ensure, tracking join, notify 흐름을 actor ref 기준으로 수정 |
| `framework/doc/framework/common/sample/tictactoe/README.ko.md` | TicTacToe authenticate, session bind, game join 흐름을 actor ref 기준으로 수정 |
| 언어별 Bingo/SupportChat/DeliveryDispatch/TicTacToe sample guide | 실제 샘플 코드 변경과 맞춰 갱신 |

문서 작성 시 guide에는 내부 socket, inproc endpoint, native actor handle 세부 구현을 넣지 않는다. core C API
세부 동작은 필요하면 internals 또는 spec에 두고 guide에서는 “actor 생성 payload는 create callback으로
전달되고, 외부 코드는 actor ref를 받아 bind한다”는 사용 흐름만 설명한다.

## 샘플 수정 계획

| 샘플 | 수정 내용 |
|------|-----------|
| Bingo | `EnsurePlayerActorHandler`가 actor 객체를 캐스팅하지 않는다. `DisplayName`은 actor 생성 payload로 전달하고, `OnCreateActor`에서 `PlayerActor`에 설정한다. handler는 actor ref를 받고, 필요하면 Entry Spot join target으로 `PreferredActorNodeRid`를 사용한다. |
| SupportChat | support user actor ensure와 agent 상태 변경 channel handler가 actor 객체를 직접 만지지 않는다. 초기 display name, role, availability 같은 값은 생성 payload 또는 actor request로 전달한다. |
| DeliveryDispatch | customer actor ensure와 subscribe flow에서 actor 객체 context 직접 사용을 제거한다. customer 초기 정보는 생성 payload로 넘기고, actor ref 기반 join/admission API로 바꾼다. |
| TicTacToe | session authenticate flow에서 manager 반환 actor 객체를 직접 사용하지 않는다. actor ref를 bind하고, game join은 actor request/Entry Spot handler 안에서 수행한다. |
| 언어별 sample README | “actor manager가 actor 객체를 반환한다”는 설명을 제거하고 actor ref 및 생성 payload 흐름으로 갱신한다. |

샘플 변경 원칙:

- 샘플에 임시 helper나 actor 객체 캐스팅 우회를 넣지 않는다.
- handler에서 actor 상태를 직접 바꾸던 코드는 create callback 또는 Spot-owned handler로 이동한다.
- `ActorRefSnapshot` DTO는 유지할 수 있지만 source는 manager 반환 actor ref여야 한다.
- user 정보가 필요한 actor 초기화는 생성 payload로 넘긴다.
- user Spot 입장 정보는 join/admission payload로 넘긴다.

## E2E 및 회귀 테스트 수정 계획

### 공통 검증 항목

| 검증 | 기대 결과 |
|------|-----------|
| actor manager create | actor ref를 반환하고 actor 객체는 public으로 나오지 않는다. |
| actor manager create payload | 생성 payload가 `OnCreateActor` 계열 callback에 전달되고 actor 초기 상태가 Spot 안에서 설정된다. |
| actor manager find | 없으면 empty/null optional, 있으면 actor ref 반환 |
| actor manager getOrCreate | 같은 actor id를 재사용하고 actor ref를 반환한다. 기존 actor가 있으면 새 payload로 재초기화하지 않는다. |
| 생성 실패 | create callback decode/검증 실패 시 actor 생성은 실패하고 actor ref를 반환하지 않는다. |
| 동시 생성 | 같은 actor id로 동시에 생성하면 첫 생성 payload만 create callback에 적용된다. |
| Entry Spot admission | actor ref 기반 API로 same-node admission request가 성공한다. 같은 Entry Spot이면 lifecycle joined count가 중복 증가하지 않는다. |
| remote Entry Spot join | actor ref 기반 API로 target node Entry Spot으로 이동하고 반환 actor ref의 node rid가 갱신된다. |
| session bind | actor ref만으로 bound session bind가 성공한다. |
| actor 객체 비노출 | public manager API에서 actor context나 actor instance를 얻을 수 없다. |

### 언어별 테스트 대상

| 언어 | 수정/추가 테스트 |
|------|------------------|
| core C API | actor 생성 payload 전달, empty payload, multipart payload, callback 실패, 동시 생성, 기존 actor 재조회 payload 무시 |
| bindings | C/.NET/Java/Node/C++ binding wrapper가 새 core C API를 호출하고 payload ownership을 지키는지 검증 |
| .NET | `Zlink.Framework.ContractTests/Actors`, `StreamContracts`, `SpotService` e2e, Bingo/SupportChat/DeliveryDispatch/TicTacToe sample regression |
| Java | `ActorManagerTest`, `SessionActorsRuntimeIntegrationTest`, `StreamSessionTest`, Spring auto-configuration tests, Bingo/SupportChat/DeliveryDispatch/TicTacToe sample regression |
| Kotlin | Kotlin Bingo/SupportChat/DeliveryDispatch/TicTacToe sample compile/run checks, Kotlin guide snippets if test fixture가 있으면 함께 수정 |
| Node | framework actor manager unit tests, stream bind tests, Bingo/SupportChat/DeliveryDispatch/TicTacToe sample smoke |
| C++ | session manager tests, Bingo/SupportChat/DeliveryDispatch/TicTacToe sample compile, actor ref bind regression |

### 샘플 E2E 기준

- Bingo는 `DisplayName`이 create callback에서 actor에 설정되고, 두 player가 서로 다른 Play node actor ref를 받으며, remote room join 검증이 유지되어야 한다.
- SupportChat은 session bind는 actor ref로 유지하고, conversation join은 Spot-owned workflow 안에서 수행해야 한다.
- DeliveryDispatch는 customer actor ensure 이후 delivery tracking join/notify가 유지되어야 한다.
- TicTacToe는 authenticate 이후 session bind와 game join이 유지되어야 한다.

## 리뷰 루프 계획

구현은 한 번의 수정으로 끝난 것으로 보지 않는다. 각 단계가 끝날 때마다 문서, public API, core 계약,
bindings, framework runtime, 샘플, e2e가 같은 방향을 가리키는지 반복해서 확인한다.

반복 기준:

- 리뷰에서 계약 누락, 언어별 불일치, 테스트 누락, 문서 과장, 샘플 우회 코드가 나오면 바로 수정한다.
- 수정 뒤에는 같은 범위를 다시 리뷰한다.
- 같은 범위에서 더 이상 substantive correctness, completeness, public-contract 이슈가 나오지 않을 때만 다음 단계로 넘어간다.
- 단순 문장 취향이나 이름 취향은 public 계약 혼동을 만들 때만 이슈로 본다.

필수 리뷰 체크포인트:

| 체크포인트 | 확인 내용 |
|------------|-----------|
| core 계약 리뷰 | actor 생성 payload, callback 전달, 실패/동시성/cleanup 계약이 C API와 테스트에 모두 반영되었는지 확인 |
| bindings 리뷰 | 각 binding이 새 core C API를 감싸며 payload ownership과 runtime sync 산출물 정책을 지키는지 확인 |
| framework 언어별 리뷰 | public manager가 actor 객체를 노출하지 않고 `Create` / `Find` / `GetOrCreate` 이름과 actor ref 반환 정책을 지키는지 확인 |
| sample 리뷰 | Bingo, SupportChat, DeliveryDispatch, TicTacToe에 actor 객체 캐스팅이나 context 직접 join 우회가 남지 않았는지 확인 |
| 문서 리뷰 | guide/spec/interface catalog가 구현된 공개 계약만 설명하고, 내부 구현 세부 사항을 guide에 넣지 않았는지 확인 |
| e2e 리뷰 | sample smoke, framework e2e, bindings tests의 실패가 코드 문제인지 환경 문제인지 분리해서 기록했는지 확인 |

마지막 단계에서는 Codex 에이전트로 별도 교차 리뷰를 수행한다. 이 교차 리뷰는 구현자가 작성한 체크리스트를
그대로 신뢰하지 않고, 실제 변경 파일과 테스트 결과를 기준으로 누락된 작업이나 남은 이슈를 찾는다.

Codex 에이전트 교차 리뷰가 이슈를 찾으면 다시 수정하고 같은 교차 리뷰를 반복한다. 최종 완료는 Codex
에이전트 교차 리뷰에서 남은 correctness/completeness/public-contract 이슈가 없다고 판단된 뒤에만 선언한다.

## 적용 순서

1. core C API의 actor 생성 payload 계약과 ABI 정책을 확정한다.
2. core actor 생성 payload 저장, lifecycle 전달, 실패/동시성 테스트를 구현한다.
3. `cmake --build core/build`와 core actor/spot regression을 통과시킨다.
4. `scripts/local-package/native/sync-local-core-libs.sh`로 로컬 core headers/runtime을 bindings 워크스페이스에 동기화한다.
5. C, .NET, Java, Node, C++ bindings가 새 core C API를 호출하도록 갱신한다.
6. bindings test를 실행하고 native library 산출물이 커밋 대상에 들어가지 않도록 확인한다.
7. 언어별 framework backend adapter의 `createActor`에 payload 인자를 연결한다.
8. 공통 actor manager 반환 타입과 생성 payload overload 이름을 확정한다.
9. .NET에서 public manager 반환 타입과 `OnCreateActorAsync` payload를 적용한다.
10. .NET 샘플과 e2e를 actor ref 및 생성 payload 흐름으로 고친다.
11. Java core와 Spring bean을 같은 방식으로 바꾼다.
12. Kotlin sample을 Java 변경에 맞춘다.
13. Node framework manager와 NestJS wiring을 바꾼다.
14. C++ `session_actor_manager_t` 반환 타입과 create payload callback을 바꾼다.
15. 공통 sample 문서와 언어별 guide/spec/interface catalog를 갱신한다.
16. 전체 샘플 smoke와 framework e2e를 실행한다.
17. 리뷰 루프 계획의 체크포인트를 반복 수행하고, 발견된 이슈를 수정한 뒤 같은 범위를 다시 확인한다.
18. 마지막으로 Codex 에이전트 교차 리뷰를 실행한다. 누락이나 남은 이슈가 나오면 수정 뒤 다시 교차 리뷰를 반복한다.

## 완료 기준

- core C API actor 생성 요청이 optional payload를 받고, create callback까지 전달한다.
- core actor/spot 회귀 테스트가 payload 전달, 실패, 동시성, cleanup을 검증한다.
- bindings 라이브러리가 새 core C API를 감싸고, local dev sync 후 각 binding test가 통과한다.
- `scripts/local-package/native/sync-local-core-libs.sh`가 만든 native runtime 산출물은 커밋하지 않는다.
- public actor manager에서 actor 객체나 actor context를 받을 수 없다.
- 모든 언어에서 `Create` / `Find` / `GetOrCreate` 계열 함수 이름은 유지되고 반환 타입만 actor ref로 바뀐다.
- 새 actor 생성 시 초기화 payload가 Spot-owned create callback에서 처리된다.
- Spot 밖 샘플 코드에 actor 객체 캐스팅, actor 상태 직접 변경, actor context 직접 join 호출이 남아 있지 않다.
- actor state 변경은 create callback, Entry Spot, user Spot, actor handler, lifecycle callback 중 하나에서만 일어난다.
- 언어별 spec/guide/interface catalog가 새 public 계약과 맞는다.
- Bingo, SupportChat, DeliveryDispatch, TicTacToe 샘플과 관련 e2e가 새 계약으로 통과한다.
- 자체 리뷰 루프에서 남은 correctness, completeness, public-contract 이슈가 없음을 확인한다.
- 마지막 Codex 에이전트 교차 리뷰에서 작업 누락과 남은 substantive correctness, completeness, public-contract 이슈가 없음을 확인한다.
