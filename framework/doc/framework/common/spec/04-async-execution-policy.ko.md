<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: ZLink Framework Message Model](03-message-model.ko.md) | [다음: ZLink Framework API](05-framework-api.ko.md)
<!-- framework-adapter-nav:end -->


# 비동기 실행과 coroutine 정책

이 문서는 언어별 framework 문서가 따르는 공통 비동기 실행 정책과 언어별 표면
투영 규칙을 정리한다. 공통 의미는 이 문서가 소유한다. 언어별 문서는 여기서 정한
의미를 자기 언어의 관용구, 대표 framework, client connector 표면으로 구체화한다.
bindings 라이브러리의 비동기 완료 표면은
[`doc/spec/bindings/async-coroutine-policy.ko.md`](../../../../../bindings/doc/spec/async-coroutine-policy.ko.md)
를 따른다. framework는 bindings가 제공하는 완료 경계를 감싸서 coroutine, virtual
thread, event loop, handler dispatcher에 연결한다.

## 1. 공통 의미

publish, request packet submit, connect, close, dispatch 같은 network 또는
runtime 상태 전이 호출은 기본적으로 비동기 실행 단위다. send는 one-way 호출이며,
전송 가능 상태와 backpressure 처리는 framework 내부 전송 경로가 맡는다.

send와 publish의 one-way `submit()`은 입력 검증과 bounded local queue 수락까지만
동기로 수행하고 완료 객체를 반환하지 않는다. queue가 가득 찼거나 대상 node 자체를 알 수
없으면 즉시 언어별 framework 예외를 발생시킨다. 대상은 알지만 route가 아직 연결되지 않은
구간은 즉시 실패시키지 않고 send readiness 한계 안에서 연결 수렴을 기다린다. 대기는
polling이 아니라 transport의 send-ready 통지로 재시도하며, 한계는 framework 기본 send
timeout이다. 한계를 넘긴 뒤의 실패 분류는
[24 Spot 주소 메시징](24-spot-address-messaging.ko.md) section 5의 표를 따른다.
수락 뒤 framework는 nonblocking send, pending queue와 ready notification으로 transport를
진행한다. 그 뒤의 실패는 monitoring/error observer로 전달하며 이미 반환한 호출에 예외를
되돌리지 않는다. blocking send를 `Task.Run`, thread pool worker, virtual thread, coroutine
worker로 감싸서 async처럼 보이게 만들지 않는다.

request는 두 단계로 본다.

- request packet submit은 send와 같은 내부 submit 경로를 사용한다.
- reply 대기는 request timeout 정책을 따른다.

Spot과 Entry Spot application callback은 Spot 단위 직렬 실행 줄에서 시작한다. handler가
`Task`, `CompletionStage`, `Promise`, `task_t` 같은 완료 값을 반환하면 framework는 그
완료 값이 끝날 때까지 같은 Spot의 다음 callback을 시작하지 않는다. **유일한 예외는 아래에서
정의하는 framework terminator await다.** `Task.Delay`나 외부 HTTP client처럼 framework
terminator가 아닌 await는 양보 지점이 아니며 실행 줄 전체를 그대로 막는다. callback 안에서
blocking wait로 완료 값을 기다리는 것은 금지한다.

request, join과 worker에는 완료를 기다리는 terminator를 하나만 제공한다. `.NET`의
`Async(...)`, Java의 `submit(...)`, Kotlin의 `await()`, Node.js의 `submit(...)`, C++의
`async()`가 그 terminator다. `Yield(...)`처럼 실행 줄 관리 방식을 호출자가 고르는
별도 public terminator는 두지 않는다.

framework는 현재 실행 문맥과 대상 실행 줄을 알고 있으므로, self-deadlock 없이 완료를
기다리는 방법을 내부에서 선택한다. 보호 장치는 두 겹이다.

- **Spot 직렬 실행 줄**은 실행 구간 자체를 직렬화한다. 같은 Spot의 두 callback 본문이 동시에
  실행되지 않는다.
- **actor와 timer의 mailbox**는 **terminator 양보를 가로질러서도** 재진입을 막는다. 같은 actor나
  같은 timer의 다음 callback은 현재 callback이 완전히 끝난 뒤에만 시작한다.

Spot의 직렬 실행 줄은 handler가 **framework terminator로** 완료 값을 기다리는 지점에서
양보하므로, 같은 Spot의 독립 callback은 그 대기 중에 시작할 수 있다(자동 turn dispatch E2E
ATD-A2가 이 의미를 고정한다). 세 가지를 함께 기억한다.

- **대기가 없으면 양보도 없다.** terminator가 기다릴 작업이 이미 동기 완료된 상태면 줄을
  양보하지 않고 그대로 진행한다.
- **continuation은 즉시 재개하지 않는다.** 양보한 줄에 다시 올라가 재개하므로, 그 사이 큐에
  들어온 다른 작업이 먼저 실행될 수 있다. 재개 시점에는 줄을 다시 배타적으로 점유한다.
- **재개 thread는 계약이 아니다.** 보장하는 것은 같은 논리적 실행 줄에서 순서대로 재개된다는
  것뿐이며, 특정 thread로 돌아온다는 뜻이 아니다.

현재 callback이 시작하고 직접 기다리는 동일 실행 줄의 후속 작업은 같은 논리적 turn의 일부로
순서대로 실행할 수 있다. 이 인과 관계가 없는 callback은 그 사이에 끼워 넣지 않는다. 이렇게 하면
보호 상태의 직렬성을 유지하면서, 현재 callback의 결과를 만들기 위해 같은 실행 줄이 필요한
경우에도 교착하지 않는다. 독립된 channel, 다른 actor, 다른 Spot과 worker 실행도 진행할 수
있어야 한다. 따라서 호출자는 await 전에 turn을 반납할지 판단하지 않는다.

**Entry Spot actor packet은 이 규칙의 예외다.** Entry Spot의 actor packet은 Entry Spot 직렬 줄에
올리지 않고 **대상 actor의 mailbox로 직렬화**하며, 실행 중에는 Spot turn을 잡지 않는다. 따라서 그
handler 안의 terminator await는 Entry Spot 직렬 줄의 양보 지점이 아니고, Entry Spot의 다른
callback과 **실제로 병행 실행될 수 있다.** Entry Spot에서는 spot 상태를 직렬성에 기대어 다루지
않는다.

짧고 빠른 local 작업을 callback 밖으로 넘겨야 할 때는 언어별 `RunWorker(...)`,
`runWorker(...)`, `run_worker(...)` 표면을 사용한다. worker 함수는 Spot 상태를 직접
만지지 않는다. 완료 callback이나 awaitable continuation은 원래 Spot의 직렬 실행 줄로
돌아온 뒤 실행된다. 큰 CPU 작업, 긴 I/O, 재시도와 scale-out이 필요한 작업은 worker
pool이 아니라 ZLink request로 별도 service나 server에 위임한다.

## 2. Public API 원칙

framework public API는 각 언어의 표준 비동기 표현을 사용하되, fluent operation은
"operation 선택 + 실행 방식 terminator" 형태로 맞춘다. 같은 의미를 가진 blocking
대안 terminator를 별도로 만들지 않는다.

- `.NET`은 request, connect, close 같은 awaitable terminator를 `Async(...)`로 둔다.
  `Task`, `ValueTask`, `Task<T>`, `ValueTask<T>`를 반환하지만, `SubmitAsync`처럼
  submit 동사를 반복하지 않는다. 예: `Connect.Async()`, `Request(...).Async<TReply>()`.
- Java는 `CompletionStage<T>`를 공식 async 결과로 사용한다. 서버 framework의
  `submit(...)`은 같은 작업의 async 시작과 완료를 나타내며 blocking `await(...)`를
  함께 제공하지 않는다.
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
있다. 다만 one-way send/publish/push/reply 계열은 callback completion을 공개 계약으로 두지
않는다. 호출자가 기다릴 결과가 있는 request/wait 계열에서만 완료 객체나 callback을 공개한다.

message dispatch error observer 도 같은 원칙을 따른다. observer 는 request error reply, 기본 로그,
metric/counter 기록 뒤에 실행되는 관측 callback 이며 dispatch 결정을 바꾸는 hook 이 아니다. observer
event 는 native frame, raw message, caller-provided buffer 를 들고 있지 않은 snapshot 이어야 한다.
callback 이 예외를 던지거나 rejected future/promise 를 반환해도 원래 dispatch 결과는 바뀌지 않는다.

언어별 구현은 receive path 에서 observer user code 를 직접 실행하지 않는다. framework executor,
serial executor, microtask/task runner, 또는 bounded queue 로 분리한다. bounded queue 를 쓰는 구현은
queue overflow 때 아직 전달하지 않은 새 event 를 drop 하고 overflow counter 를 올린다. shutdown 은
짧은 drain 기회를 줄 수 있지만 observer 때문에 무기한 대기하지 않는다.

### 2.1 취소 계약

취소는 비동기 작업을 반드시 즉시 멈추는 명령이 아니라, 더 이상 그 작업의 완료를
기다리지 않거나 계속 진행할 필요가 없음을 framework에 전달하는 협력적 요청이다.
취소를 지원하는 작업과 취소 전달 인자는 언어별 정식 public contract에 명시한다.
caller가 public method와 callback에 전달하는 `CancellationToken` 타입은 `.NET`
framework의 언어별 계약이다. 다른 framework 언어에는 이 타입을 복제하지 않는다.
취소가 필요한 작업은 해당 언어의 표준 async와 lifecycle 관례로 표현한다.

공통 취소 의미는 다음과 같다.

- 취소 요청 전에 완료된 결과는 취소로 바꾸지 않는다.
- 취소는 이미 수락되거나 전송된 one-way 메시지를 되돌리지 않으며, 상대에게 메시지가
  전달되지 않았음을 보장하지 않는다.
- reply 대기 취소는 호출자의 대기를 끝내고 관련 waiter와 callback registration을
  정리한다. 이미 전송된 request의 원격 처리를 rollback한다는 뜻은 아니다.
- connect, close, dispatch와 worker 작업은 각 언어별 정식 스펙이 취소 대상으로
  명시한 범위에서만 취소 요청을 관찰한다.
- timeout은 framework가 정한 제한 시간이 지난 결과이고, cancellation은 호출자 또는
  상위 lifecycle이 요청한 결과다. 언어별 오류 표현이 다르더라도 두 원인을 구분한다.
- 취소를 관찰한 framework는 더 이상 필요하지 않은 timer, waiter, callback과
  registration을 정리해야 한다.

공통 계약은 모든 언어에 같은 cancellation 타입이나 인자 위치를 요구하지 않는다.

| 언어 | public cancellation 인자 계약 |
|------|------------------------------|
| `.NET` | 취소를 지원하는 메서드의 `CancellationToken` |
| Java | handler token 없음. 호출 결과와 host lifecycle은 `CompletionStage`, timeout과 Spring lifecycle로 관리한다. |
| Kotlin | 별도 token 인자 없음. `suspend` 함수는 coroutine lifecycle을 따른다. |
| Node.js / TypeScript | 취소가 필요한 장기 작업은 optional `AbortSignal`을 사용할 수 있다. 일반 handler에는 자동으로 추가하지 않는다. |
| C++ | 취소가 필요한 장기 작업은 C++ 표준 중단과 수명 관례를 사용할 수 있다. custom token을 기본 callback 인자로 복제하지 않는다. |

특정 언어에 명시적 취소 인자가 없는 것은 그 자체로 parity gap이 아니다. 반대로
언어 관례상 취소가 자연스러운 장기 작업은 해당 언어별 spec에서 정확한 표면을
정의할 수 있다. timeout, host shutdown, connection close와 resource cleanup은 취소
인자와 별개의 공통 계약으로 유지한다.

## 3. 서버와 클라이언트 표면 구분

언어별 framework 문서에서는 서버 framework 표면과 client connector 표면을 구분해서
설명해야 한다. 두 표면은 같은 async submit 의미를 공유하지만, 이름과 runtime 제약은
서로 다를 수 있다.

| 구분 | 포함하는 API | 공통 기준 |
|------|--------------|-----------|
| 서버 framework | channel handler, outbound client, Spot, actor, session, Registry, monitoring | host framework의 lifecycle과 DI 규칙을 따른다. one-way outbound submit은 완료 객체를 반환하지 않고, request처럼 응답을 기다리는 호출만 async 결과를 반환한다. |
| client connector | Stream Connector, game/UI client connector, wait/request/send helper | 서버 framework와 독립된 client 라이브러리로 둘 수 있다. send는 송신 완료를 공개하지 않고, wait/request와 lifecycle completion만 별도 설명한다. |
| runtime adapter | Unity, Unreal, Godot, Axmol, Kotlin coroutine wrapper 같은 환경별 adapter | core 의미를 바꾸지 않고, main thread 또는 coroutine/dispatcher 규칙에 맞게 감싼다. |

서버 framework가 `Async`, `submit`, `CompletionStage`, `task_t` 같은 awaitable 표면을
제공하더라도, one-way send/publish/push/reply 계열에는 이 표면을 붙이지 않는다. client
connector의 callback completion도 request/wait/lifecycle처럼 완료 의미가 있는 호출에만 둔다.

## 4. 비규범 부록: 언어별 투영 찾아보기

이 절은 언어별 정식 스펙을 찾기 위한 요약이며 공통 계약을 추가하지 않는다. 정확한
타입, terminator 이름, overload와 취소 인자는 `languages/<lang>/` 문서가 소유한다.
이 부록과 언어별 정식 스펙이 다르면 언어별 정식 스펙을 따른다.

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
| channel send | `IZLinkSendCall` | `Submit(CancellationToken)`; 완료 객체를 반환하지 않음 |
| channel request | `IZLinkRequestCall` | `Async<TReply>(CancellationToken)` |
| fanout publish | `IZLinkPublishCall` | `Submit(CancellationToken)`; 완료 객체를 반환하지 않음 |
| session push/reply | `IZLinkSessionSendCall`, `IZLinkSessionReplyCall` | `Submit(CancellationToken)`; 완료 객체를 반환하지 않음 |
| bound session push | `IZLinkBoundSessionSendCall` | `Submit(CancellationToken)`; 완료 객체를 반환하지 않음 |
| actor send | `IZLinkActorSendCall` | `Submit(CancellationToken)`; 완료 객체를 반환하지 않음 |
| handler | `IZLinkRequestHandler<TReq,TReply>`, `IZLinkSendHandler<T>` | `ValueTask<TReply>` / `ValueTask` 반환 |

client connector 표면:

| 영역 | 인터페이스 / 메서드 | 비동기 표면 |
|------|--------------------|-------------|
| lifecycle | `IZlinkStreamConnector` | `Connect.Async(...)`, `Close.Async(...)`, `Dispatch.Async(...)` |
| send | `IZlinkStreamSendCall` | `Submit(CancellationToken)`; 완료 객체를 반환하지 않음 |
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
| channel outbound | `ZLinkClient`, `ZLinkRouteClient`, `ZLinkFanoutClient` | one-way call의 `submit()`은 완료 객체를 반환하지 않고 request call만 `CompletionStage<TReply>`를 반환 |
| Spot / actor / session | `ZLinkSpot`, `ZLinkActorContext`, `ZLinkSessionContext` | lifecycle, join, bind, relay가 `CompletionStage` 반환 |
| manual connection | `ZLinkEndpointConnections` 계열 | `connect(endpoint)`, `disconnect(endpoint)` 같은 제어 표면. 연결 단위는 `channel + capability` 또는 `spot node + capability` |

client connector 표면:

| 영역 | 메서드 | 의미 |
|------|--------|------|
| lifecycle | `connect().submit()`, `dispatch().submit()` | `submit()`은 비동기 작업을 시작하고 `CompletionStage`를 반환한다. blocking `await()` 대안은 제공하지 않는다. |
| request / wait | `request(...).submit()`, `waitFor(...).submit()` | request timeout은 connector option 기본값을 따르고, 필요할 때만 호출별 timeout을 지정한다. |

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

| 영역 | Node 표면 | 공통 의미 |
|------|-----------|-----------|
| handler | `handle()` | 반환은 `Promise<T>` 또는 `Promise<void>`이며 완료까지 callback 실행이 끝나지 않는다. |
| channel outbound | `sendToChannel(...).submit()`, `requestToChannel(...).submit<T>()` | fluent call에서 선택한 operation을 실행한다. |
| lifecycle | `start()`, `stop()`, NestJS lifecycle hook | host 시작과 종료에 framework 수명을 연결한다. |
| DI | `ZLINK_CHANNEL_CLIENT`, `ZLINK_FANOUT_CLIENT`, `ZLINK_SPOT_MANAGER` 같은 provider token | NestJS provider 주입 표면이다. |

client connector 표면:

| 영역 | Node 표면 | 의미 |
|------|-----------|------|
| lifecycle | `connect()`, `close()`, `dispatch()` | `Promise<void>` 반환 |
| send/request/wait | `send(...).submit()`, `request(...).submit<T>()`, `waitFor(...).where(...).submit()` | `send`는 완료값을 반환하지 않는다. `request`와 `waitFor`는 응답을 기다리므로 `Promise<T>`를 반환한다. `Async` suffix를 붙이지 않는다. |

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
| lifecycle / request / wait | framework connector가 소유하는 callback 또는 result 기반 표면. 예외와 coroutine에 의존하지 않음 | `connect().async()`, `close().async()`, `request(...).async()`, `wait_for(...).async()` (`connector.dispatch()`는 별도) |
| one-way send | 호출자가 송신 수락이나 backpressure 완료를 기다리지 않도록 framework 내부 submitter가 처리 | `send(...).submit()` |
| callback completion | core connector의 request/wait `submit(callback)` | callback 기반 completion이 필요할 때 사용 |
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
[스펙 목차](README.ko.md) | [이전: ZLink Framework Message Model](03-message-model.ko.md) | [다음: ZLink Framework API](05-framework-api.ko.md)
<!-- framework-adapter-nav:bottom:end -->
