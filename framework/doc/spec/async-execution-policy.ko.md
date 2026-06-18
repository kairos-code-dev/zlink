<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: framework API](./framework-api.ko.md) | [다음: Actor 모델](./actor-model.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](./README.ko.md)

# 비동기 실행과 coroutine 정책

이 문서는 언어별 framework 문서가 따르는 공통 비동기 실행 정책과 언어별 표면
투영 규칙을 정리한다. 공통 의미는 이 문서가 소유한다. 언어별 문서는 여기서 정한
의미를 자기 언어의 관용구, 대표 framework, client connector 표면으로 구체화한다.
bindings 라이브러리의 비동기 완료 표면은
[`doc/spec/bindings/async-coroutine-policy.ko.md`](../../../doc/spec/bindings/async-coroutine-policy.ko.md)
를 따른다. framework는 bindings가 제공하는 완료 경계를 감싸서 coroutine, virtual
thread, event loop, handler dispatcher에 연결한다.

## 1. 공통 의미

send, publish, request packet submit, connect, close, dispatch 같은 network 또는
runtime 상태 전이 호출은 기본적으로 비동기 실행 단위다. 응답 payload가 없더라도
전송 가능 상태, backpressure, timeout, cancellation, runtime stop 같은 완료 조건이
있으므로 호출자가 즉시 완료된 동기 함수로 가정하면 안 된다.

send와 publish의 backpressure는 public blocking/nonblocking 옵션으로 나누지 않는다.
framework는 내부에서 nonblocking send, pending queue, ready notification을 사용해
전송 가능 상태까지 비동기로 기다린다. blocking send를 `Task.Run`, thread pool worker,
virtual thread, coroutine worker로 감싸서 async처럼 보이게 만들지 않는다.

request는 두 단계로 본다.

- request packet submit은 send와 같은 비동기 submit 경로를 사용한다.
- reply 대기는 request timeout 정책을 따른다.

Spot과 Entry Spot application callback은 Spot 단위 직렬 실행 줄에서 시작한다. handler가
`Task`, `CompletionStage`, `Promise`, `task_t` 같은 완료 값을 반환하면 framework는 그
완료 값이 끝날 때까지 같은 Spot의 다음 callback을 시작하지 않는다. callback 안에서
blocking wait로 완료 값을 기다리는 것은 금지한다.

짧고 빠른 local 작업을 callback 밖으로 넘겨야 할 때는 언어별 `RunWorker(...)`,
`runWorker(...)`, `run_worker(...)` 표면을 사용한다. worker 함수는 Spot 상태를 직접
만지지 않는다. 완료 callback이나 awaitable continuation은 원래 Spot의 직렬 실행 줄로
돌아온 뒤 실행된다. 큰 CPU 작업, 긴 I/O, 재시도와 scale-out이 필요한 작업은 worker
pool이 아니라 ZLink request로 별도 service나 server에 위임한다.

## 2. Public API 원칙

framework public API는 각 언어의 표준 비동기 표현을 사용하되, fluent operation은
"operation 선택 + 실행 방식 terminator" 형태로 맞춘다. 같은 의미를 가진 blocking
대안 terminator를 별도로 만들지 않는다.

- `.NET`은 fluent operation builder의 awaitable terminator를 `Async(...)`로 둔다.
  `Task`, `ValueTask`, `Task<T>`, `ValueTask<T>`를 반환하지만, `SubmitAsync`처럼
  submit 동사를 반복하지 않는다. 예: `Connect.Async()`, `Send(...).Async()`.
- Java는 `CompletionStage<T>`를 공식 async 결과로 사용한다. 필요한 경우 Java 전용
  `submit(...)`은 같은 작업의 async 시작, `await(...)`는 같은 async 작업의 완료를
  현재 thread에서 기다리는 adapter다.
- Kotlin은 Java `CompletionStage` 기반 계약 위에 `suspend` / `Flow` wrapper를 얹는다.
  Kotlin wrapper는 새로운 runtime 의미를 만들지 않고 coroutine suspension으로 같은
  작업을 기다린다.
- Node.js / TypeScript는 `Promise<T>` 반환 타입과 `await` 사용으로 비동기 계약을
  드러낸다. `Async` suffix를 C#에서 그대로 옮기지 않는다.
- C++ framework는 C++20 coroutine과 `task_t<T>` 같은 awaitable 표면을 사용할 수 있다.
  Coroutine 표면의 terminator는 `async()`로 둔다. `submit(...)`은 coroutine을 쓰지
  않는 callback/result 기반 시작 표면이다. engine adapter나 no-coroutine connector처럼
  대상 runtime이 다르면 coroutine 표면은 framework가 소유하는 adapter로 분리한다.
- Python, Go, Rust 같은 다른 언어도 같은 의미를 각 언어의 async 표면으로 투영한다.

callback 기반 completion API는 awaitable 값을 반환하지 않으므로, 언어별 관례에 따라
`Submit(callback)`, `submit(callback)`, `onCompleted(...).start()` 같은 이름을 유지할 수
있다. 이 경우에도 network 의미는 위의 async 실행과 같아야 한다.

## 3. 서버와 클라이언트 표면 구분

언어별 framework 문서에서는 서버 framework 표면과 client connector 표면을 구분해서
설명해야 한다. 두 표면은 같은 async submit 의미를 공유하지만, 이름과 runtime 제약은
서로 다를 수 있다.

| 구분 | 포함하는 API | 공통 기준 |
|------|--------------|-----------|
| 서버 framework | channel handler, outbound client, Spot, actor, session, Registry, monitoring | host framework의 lifecycle과 DI 규칙을 따른다. handler 반환 타입과 outbound submit terminator가 해당 언어의 async 표면을 사용한다. |
| client connector | Stream Connector, game/UI client connector, wait/request/send helper | 서버 framework와 독립된 client 라이브러리로 둘 수 있다. manual dispatch, callback, coroutine adapter 같은 runtime별 표면을 별도 설명한다. |
| runtime adapter | Unity, Unreal, Godot, Axmol, Kotlin coroutine wrapper 같은 환경별 adapter | core 의미를 바꾸지 않고, main thread 또는 coroutine/dispatcher 규칙에 맞게 감싼다. |

서버 framework가 `Async`, `submit`, `CompletionStage`, `task_t` 같은 awaitable
표면을 제공하더라도, client connector는 callback completion 표면을 함께 제공할 수 있다.
이 경우 callback 표면은 awaitable 표면과 같은 timeout, cancellation, error 의미를 가져야
한다.

## 4. 언어별 네이밍과 인터페이스 투영

이 절은 언어별 문서에 흩어진 비동기 표면 정책을 모아 둔 기준이다. 서버 framework와
client connector의 async 의미, terminator 이름, coroutine adapter 경계는 이 절을
기준으로 맞춘다. 언어별 문서는 이 절을 다시 정의하지 않고, 실제 사용 예시나 구현 진행
상태만 보충한다.

### 4.1 .NET

`.NET` framework와 connector는 fluent operation builder의 awaitable terminator를
`Async(...)`로 둔다. `Async`는 suffix가 아니라 실행 방식 terminator다. 앞 단계가
이미 `Connect`, `Send`, `Request`, `WaitFor`처럼 operation을 고르므로 terminator에서
`Submit` 동사를 반복하지 않는다. public 타입은 `PascalCase`를 쓰고, 서버 framework
타입은 `ZLink` prefix를 사용한다. client Stream Connector 타입은 서버 framework와
독립된 `Systems.Zlink.Stream.Connector` 라이브러리의 `Zlink*` 타입이다.

서버 framework 표면:

| 영역 | 인터페이스 / 메서드 | 비동기 표면 |
|------|--------------------|-------------|
| channel send | `IZLinkSendCall` | `Async(CancellationToken)` |
| channel request | `IZLinkRequestCall` | `Async<TReply>(CancellationToken)` |
| fanout publish | `IZLinkPublishCall` | `Async(CancellationToken)` |
| session push/reply | `IZLinkSessionSendCall`, `IZLinkSessionReplyCall` | `Async()` |
| bound session push | `IZLinkBoundSessionSendCall` | `Async(CancellationToken)` |
| handler | `IZLinkRequestHandler<TReq,TReply>`, `IZLinkSendHandler<T>` | `ValueTask<TReply>` / `ValueTask` 반환 |

client connector 표면:

| 영역 | 인터페이스 / 메서드 | 비동기 표면 |
|------|--------------------|-------------|
| lifecycle | `IZlinkStreamConnector` | `Connect.Async(...)`, `Close.Async(...)`, `Dispatch.Async(...)` |
| send | `IZlinkStreamSendCall` | `Async(CancellationToken)` |
| request | `IZlinkStreamRequestCall` | `Async(CancellationToken)` 또는 `Submit(callback)` |
| wait | `IZlinkStreamWaitCall` | `Where(...)`, `Timeout(...)`, `Async(CancellationToken)` |

`Submit(callback)`은 awaitable을 반환하지 않는 callback completion 표면이므로 이름을
유지할 수 있다. 반대로 `ValueTask`를 반환하는 public terminator는 `Async`로 쓴다.

### 4.2 Java / Kotlin

Java framework는 `CompletionStage<T>`를 공식 async 결과로 사용한다. public 메서드는
`camelCase`, 클래스와 annotation은 `PascalCase`를 쓴다. blocking과 non-blocking을
별도 동사 이름으로 나누지 않는다.

서버 framework 표면:

| 영역 | 인터페이스 / 메서드 | 비동기 표면 |
|------|--------------------|-------------|
| handler | `ZLinkRequestHandler`, `ZLinkSendHandler`, `ZLinkPublishHandler` | `CompletionStage<TReply>` / `CompletionStage<Void>` 반환 |
| channel outbound | `ZLinkClient`, `ZLinkRouteClient`, `ZLinkFanoutClient` | call builder가 `submit(...)`으로 `CompletionStage` 반환 |
| Spot / actor / session | `ZLinkSpot`, `ZLinkActorContext`, `ZLinkSessionContext` | lifecycle, join, bind, relay가 `CompletionStage` 반환 |
| manual connection | `ZLinkEndpointConnections` 계열 | `connect(endpoint)`, `disconnect(endpoint)` 같은 제어 표면. 연결 단위는 `channel + capability` 또는 `spot node + capability` |

client connector 표면:

| 영역 | 메서드 | 의미 |
|------|--------|------|
| lifecycle | `connect().submit()`, `connect().await()`, `dispatch().submit()`, `dispatch().await()` | `submit()`은 비동기 작업을 시작하고 `CompletionStage`를 반환한다. `await()`는 같은 작업의 완료를 현재 thread에서 기다리는 Java adapter다. |
| request / wait | `request(...).submit()`, `request(...).await()`, `waitFor(...).submit()`, `waitFor(...).await()` | request timeout은 connector option 기본값을 따르고, 필요할 때만 호출별 timeout을 지정한다. |

Kotlin은 Java 계약 위에 `suspend` / `Flow` wrapper를 얹는다. Kotlin wrapper는
새 runtime 의미를 만들지 않고, Java `CompletionStage`를 coroutine suspension으로
기다린다. Kotlin `suspend fun` handler도 Java annotation handler와 같은 registry
공간에 등록되어야 하며, framework가 소유하는 coroutine adapter가 suspend function을 실행하고
결과를 `CompletionStage`로 Java core에 돌려준다.

### 4.3 Node.js / TypeScript

Node framework는 `Promise<T>` 반환 타입과 `await` 사용으로 비동기 계약을 드러낸다.
`Async` suffix는 C#에서 그대로 옮기지 않는다. 메서드와 필드는 `camelCase`, 클래스,
interface, decorator, enum 타입은 `PascalCase`를 쓴다.

서버 framework 표면:

| 영역 | Node 표면 | .NET 기준과의 대응 |
|------|-----------|-------------------|
| handler | `handle()` | `.NET` `HandleAsync` 의미. 반환은 `Promise<T>` 또는 `Promise<void>` |
| channel outbound | `sendToChannel(...).submit()`, `requestToChannel(...).submit<T>()` | `.NET` fluent `Async` 의미 |
| lifecycle | `start()`, `stop()`, NestJS lifecycle hook | `.NET` `StartAsync`, `StopAsync` 의미 |
| DI | `ZLINK_CHANNEL_CLIENT`, `ZLINK_FANOUT_CLIENT`, `ZLINK_SPOT_MANAGER` 같은 provider token | `.NET` DI 주입 표면 대응 |

client connector 표면:

| 영역 | Node 표면 | 의미 |
|------|-----------|------|
| lifecycle | `connect()`, `close()`, `dispatch()` | `Promise<void>` 반환 |
| send/request/wait | `send(...).submit()`, `request(...).submit<T>()`, `waitFor(...).where(...).submit()` | `Promise<T>` 또는 `Promise<void>` 반환. `Async` suffix를 붙이지 않는다. |
| cancellation | `signal?: AbortSignal` | `.NET` `CancellationToken`의 Node 투영 |

codec 변환, packet name 계산, 값 객체 생성처럼 network I/O를 하지 않는 순수 helper는
동기 함수일 수 있다. 이 구분은 함수 이름이 아니라 반환 타입과 역할로 판단한다.

### 4.4 C++

C++ framework는 C++20 이상을 기준으로 삼고, `task_t<T>` 같은 awaitable 표면과
coroutine handler를 사용할 수 있다. 메서드는 `snake_case`, 타입은 `_t` 접미사를
기준으로 적는다. public async 표면에는 `std::future`를 사용하지 않는다.

서버 framework 표면:

| 영역 | C++ 표면 | 의미 |
|------|----------|------|
| handler | `task_t<T>` 또는 `task_t<void>` 반환 handler | framework handler coroutine executor에서 실행 |
| call object | `async()` / `co_await call.async()` | 같은 완료 결과와 error kind를 사용 |
| executor | handler coroutine executor | `.result()` blocking bridge 없이 task 완료로 coroutine을 재개 |

client connector 표면:

| 영역 | callback/result connector 표면 | coroutine adapter |
|------|------------------------------|-------------------|
| lifecycle / send / request | framework connector가 소유하는 callback 또는 result 기반 표면. 예외와 coroutine에 의존하지 않음 | `connect().async()`, `close().async()`, `dispatch().async()`, `request(...).async()`, `wait_for(...).async()` |
| callback completion | `on_completed(...).start()` | callback 기반 completion이 필요할 때 사용 |
| coroutine completion | bindings나 낮은 수준 connector 표면에는 직접 섞지 않음 | `co_await` 가능한 `task_t<T>` 반환 |

Unreal, Godot, Axmol 같은 engine adapter는 기본 connector를 복제하지 않고, engine main
thread와 delegate 모델에 맞는 adapter/plugin으로 둔다. Unreal public 표면에는 일반 C++
coroutine API를 강제하지 않는다.

### 4.5 Python / Go / Rust

이 언어들은 아직 draft 문서가 중심이다. 그래도 공통 기준은 같다.

| 언어 | 네이밍 | async 표면 기준 |
|------|--------|----------------|
| Python | public API는 `snake_case` | framework가 bindings callback completion을 `asyncio.Future`나 framework task로 변환한다. 사용자 표면은 `asyncio`와 `await` 중심으로 투영한다. send/publish는 async submit 의미를 유지한다. |
| Go | exported 이름은 `PascalCase` | `context.Context`, goroutine, channel 같은 Go 관용구로 취소와 완료를 표현한다. blocking/nonblocking을 별도 동사로 쪼개지 않는다. |
| Rust | 메서드는 `snake_case`, 타입은 `PascalCase` | framework가 bindings callback completion을 runtime별 `Future`나 channel로 변환한다. 사용자 표면은 `async fn`, `Future`, `Result<T, E>` 중심으로 투영한다. send/publish backpressure 의미는 public no-wait 옵션이 아니라 framework async submit 의미를 따른다. |

세 언어 모두 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
설명하고, 같은 역할 안에서 Discovery와 manual 연결을 섞지 않는다.

## 5. Coroutine Adapter

Coroutine은 core 또는 bindings 의미가 아니라 언어 또는 runtime 표면이다. Coroutine
wrapper는 framework 또는 runtime adapter가 소유하며 아래 규칙을 따른다.

- core async 작업과 같은 cancellation, timeout, error 의미를 유지한다.
- 별도 queue나 별도 runtime 의미를 만들어 handler 실행 순서를 바꾸지 않는다.
- callback 기반 core 위에 coroutine wrapper를 둘 수 있지만, core 계약을 숨기거나
  서로 다른 완료 의미를 만들면 안 된다.
- game engine, UI runtime, Unity, Unreal처럼 main thread 규칙이 강한 환경에서는
  framework core에 coroutine 전용 API를 강제하지 않는다. 해당 runtime adapter 또는
  application helper가 awaitable 작업을 frame/update 흐름에 맞춰 감싼다.

Unity는 `.NET` Stream Connector를 그대로 사용하고 `MonoBehaviour.Update()`에서
`Dispatch.Async()`를 호출해 callback을 main thread에서 실행한다. `StartCoroutine(...)`
중심의 프로젝트에서는 application helper가 `Task` / `ValueTask` 완료를 frame마다 확인한다.
framework와 connector는 Unity coroutine 전용 public API나 blocking sync API를 따로
제공하지 않는다.

C++ engine adapter도 같은 원칙을 따른다. 일반 C++ connector가 `async()` coroutine
adapter를 제공하더라도, Unreal 같은 engine 표면은 Game Thread dispatch와 engine
delegate 모델을 우선하고 coroutine API를 public 표면에 강제하지 않는다.

## 6. 언어별 문서 규칙

언어별 문서는 이 문서의 의미를 다시 정의하지 않는다. 대신 아래만 적는다.

- 해당 언어에서 async 결과가 어떤 타입으로 보이는지
- coroutine wrapper가 있으면 어떤 core async 작업을 감싸는지
- blocking adapter가 있으면 공식 async API 위에 얹은 편의 표면임을 어떻게 드러내는지
- callback API가 있으면 awaitable API와 같은 timeout, cancellation, error 의미를
  유지하는지

언어별 문서에서 이 정책과 다른 실행 의미가 필요해지면, 먼저 이 공통 문서를 수정한 뒤
언어별 문서를 맞춘다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: framework API](./framework-api.ko.md) | [다음: Actor 모델](./actor-model.ko.md)
<!-- framework-adapter-nav:bottom:end -->
