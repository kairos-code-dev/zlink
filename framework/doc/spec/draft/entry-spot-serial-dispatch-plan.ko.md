# Entry Spot 직렬 실행과 worker offload 계획

이 문서는 **구현 전 초안**이며 **현재 공개 계약이 아니다**. 구현이 끝나기
전까지 정식 framework 공통 스펙이나 언어별 spec에는 이 내용을 공개 계약처럼
섞지 않는다.

이 계획은 `.NET`, Node.js, C++, Java framework의 Entry Spot 실행 규칙을 일반
Spot과 같은 직렬 실행 모델로 맞추고, 오래 걸리는 작업을 worker로 넘긴 뒤 완료
결과를 원래 Spot 직렬 실행 순서에서 받기 위한 공통 API를 정리한다.

## 1. 배경

일반 Spot은 route packet, actor packet, lifecycle callback, timer callback,
request reply callback처럼 같은 Spot 상태를 만질 수 있는 작업을 한 실행 줄에서
순서대로 처리한다. 이 규칙 덕분에 Spot handler는 Spot 내부 상태를 보호하기 위해
별도 lock을 반복해서 두지 않아도 된다.

Entry Spot은 Actor가 처음 들어오는 입구다. 그래서 초기 구현과 문서에서는 Entry
Spot 전체를 하나의 전역 실행 줄로 묶지 않고, actor id별 실행이나 일부 직접
callback 실행을 허용하는 예외가 있었다. 이 예외는 입구 병목을 피하려는 목적은
있지만, Entry Spot이 실제 application 상태를 갖기 시작하면 문제가 된다.

Entry Spot handler도 인증 상태, actor 입장 대기열, user Spot 선택 결과, 임시
session binding, join 뒤 정리 작업 같은 공유 상태를 가진다. 이 상태를 여러
callback에서 동시에 만지면 결국 lock, concurrent map, 재진입 방지 flag 같은
동기화 장치가 필요하다. 이 비용과 복잡도는 Entry Spot 실행 줄 하나를 두는 비용과
크게 다르지 않으며, 오히려 사용자 코드에 동시성 규칙을 떠넘긴다.

따라서 Entry Spot도 일반 Spot과 같은 규칙을 따른다.

다만 이 계획은 Entry Spot만의 문제를 다루는 문서가 아니다. Spot 자체가 상태
일관성을 위해 직렬 실행을 선택하면, 특정 Actor handler나 timer callback이 오래
실행될 때 같은 Spot의 다른 message가 밀릴 수 있다. 이 현상은 Spot 직렬 실행
모델의 자연스러운 비용이다. framework가 Actor별 병렬 실행을 자동으로 열면 같은
Spot 상태를 여러 callback이 동시에 만질 수 있어 lock과 순서 규칙이 다시 필요해진다.

따라서 기본 해결은 application이 Spot 경계를 적절히 나누고, 긴 작업을 Spot 밖으로
빼는 것이다. framework는 이 선택을 쉽게 하기 위해 worker offload API를 제공한다.
이 API는 긴 작업을 worker thread나 언어별 worker runtime에서 실행하고, 완료 결과를
원래 Spot 또는 Entry Spot의 직렬 실행 줄로 되돌린다.

## 2. 결정

Entry Spot의 application callback은 모두 Entry Spot 직렬 실행 줄에서 실행한다.
기본 실행 규칙은 **비재진입**이다. handler가 framework에 비동기 완료 값을
반환하면, framework는 그 완료가 끝날 때까지 같은 Entry Spot의 다음 callback을
시작하지 않는다. 이 규칙은 user Spot과 같은 의미다.

포함되는 작업은 아래와 같다.

- Entry Spot `onInitialize`와 `onClosing`
- Entry Spot actor join callback
- Entry Spot actor send handler
- Entry Spot actor request handler
- Entry Spot actor lifecycle callback
- Entry Spot timer callback
- Entry Spot route send/request handler
- Entry Spot subscription handler
- Entry Spot handler가 시작한 framework request의 reply callback 또는 continuation

request를 보낼 때는 dispatch thread를 막지 않는다. handler 안에서 다른 channel,
Spot, service로 request를 보낼 수 있지만, 그 결과는 callback, `Task`,
`CompletionStage`, `Promise`, `task_t` 같은 비동기 완료로 이어 붙인다. 완료 뒤
Entry Spot 상태를 바꿔야 하면 다시 Entry Spot 직렬 실행 줄에 넣어 실행한다.

다만 완료를 받는 방식은 두 가지로 구분한다.

- **gated awaitable 경로**: handler가 `Task`, `CompletionStage`, `Promise`, `task_t`
  같은 완료 값을 반환하고, framework가 그 완료를 handler 완료로 본다. 이 동안 같은
  Entry Spot의 다음 callback은 시작하지 않는다.
- **detached callback 경로**: handler가 request 또는 worker job을 제출한 뒤 callback을
  등록하고 반환한다. 이 경우 handler는 끝난 것이므로 다른 Entry Spot callback이 먼저
  실행될 수 있다. 나중에 완료 callback이 Entry Spot 실행 줄에 들어오면 그 시점의
  상태를 다시 확인해야 한다.

Spot 또는 Entry Spot handler가 CPU 계산, blocking SDK 호출, 파일 처리, 외부
라이브러리 호출처럼 오래 걸릴 수 있는 작업을 수행해야 하면 `context.runWorker(...)`
계열 API를 사용한다. 이 API는 작업 자체를 Spot 직렬 실행 줄 밖에서 실행하고, 완료
callback 또는 비동기 완료 continuation을 원래 Spot 직렬 실행 줄 안에서 실행한다.

## 3. 목표

1. Entry Spot과 일반 Spot의 상태 보호 규칙을 같게 만든다.
2. callback과 request continuation이 Entry Spot 상태를 직접 동시에 바꾸지 못하게
   한다.
3. 비재진입 실행을 기본으로 하고, detached callback 경로는 명시적인 인터리빙 opt-in으로
   정의한다.
4. blocking request 대기를 Entry Spot dispatch 문맥에서 금지한다.
5. 긴 작업을 worker로 넘기고 완료를 Spot 순서에서 받는 fluent builder를 제공한다.
6. awaitable 완료와 callback 완료를 같은 작업 builder의 실행 방식으로 구분한다.
7. 언어별 framework가 같은 의미를 각 언어의 비동기 표현으로 제공하게 한다.
8. 기존 Entry Spot 예외 문구를 공통 문서와 언어별 문서에서 제거한다.
9. 회귀 테스트로 순서, 재진입, request continuation 위치, worker completion 위치,
   blocking 금지를 확인한다.

## 3.1 현재 저장소 상태

이 draft는 구현 전 계획으로 시작했지만, 현재 작업 tree에는 일부 언어의 구현과
테스트가 이미 들어와 있다. 이후 작업은 아래 상태를 기준으로 남은 언어와 문서를
맞춘다.

| 언어 | 현재 확인한 상태 | 남은 정리 |
|------|------------------|-----------|
| `.NET` | `IZLinkSpotContext`와 `IZLinkEntrySpotContext`에 `RunWorker(...)`가 있고, `IZLinkWorkerCall<T>`는 `Async(...)`와 `Submit(...)` terminator를 가진다. `WorkerQueueFull`, `WorkerTimedOut`, `WorkerFailed` 오류 분류와 worker pool 테스트도 있다. Entry Spot public destroy API는 `DestroyActorAsync(...)`다. | Entry Spot serial gate와 request continuation 위치가 모든 callback 종류에 적용되는지 테스트를 넓히고, guide/spec의 이전 예외 문구를 제거한다. |
| Node.js | `ZLinkSpotContext`와 `ZLinkEntrySpotContext`에 closure 기반 `runWorker(...)`가 있고, `submit()`과 `onCompleted(...)` terminator, worker option, queue full, timeout, late completion 테스트가 있다. public 주석은 CPU 작업을 별도 worker thread에서 실행한다고 보장하지 않는다. | serial executor 경로가 native callback과 request continuation 전체에 일관되게 적용되는지 유지하고, Node guide/spec에 closure 기반 deferral의 한계를 반영한다. |
| Java / Kotlin | `ZLinkWorkerTask<T>`가 `throws Exception`을 표현하고, `ZLinkWorkerCall<T>`는 `CompletionStage<T> submit()`과 callback `submit(...)`을 가진다. worker pool, worker 오류 예외, Entry Spot worker completion 테스트가 있다. | Entry Spot serial queue 적용 범위를 actor packet, lifecycle, timer, backend request callback까지 문서와 테스트에서 닫는다. Kotlin guide는 blocking adapter와 coroutine suspension의 차이를 분명히 한다. |
| C++ | Entry Spot `destroyActor(...)`, coroutine executor, 일부 내부 offload executor가 있다. `spot_context_t`와 `entry_spot_context_t`에는 public `run_worker(...)` 표면과 `worker_call_t<T>` builder의 첫 contract test가 들어왔다. | `run_worker(...)`를 실제 Spot 직렬 executor와 bounded elastic worker pool에 연결하고, queue full 실패, timeout 뒤 late completion drop, callback `task_t<void>` gate 의미를 unit test로 닫는다. |

따라서 이 문서의 남은 목적은 새 아이디어를 더 넓히는 것이 아니라, 이미 들어온
언어의 의미를 공통 규칙으로 정리하고 아직 덜 구현된 언어가 같은 계약을 따라오게
하는 것이다.

## 4. 비목표

- core C API의 Spot dispatch 계약을 이 계획에서 바꾸지 않는다.
- Actor가 user Spot으로 이동한 뒤의 user Spot 실행 규칙을 바꾸지 않는다.
- Entry Spot을 모든 Actor의 장기 실행 domain 로직 위치로 권장하지 않는다.
- request payload encode/decode 방식을 새로 정의하지 않는다. 이 문서의 직렬 실행은
  message serialization이 아니라 callback 실행 순서를 뜻한다.
- worker API가 application의 부하 분산 정책을 자동으로 정하지 않는다. 어떤 작업을
  worker로 넘길지, Spot을 어떤 단위로 나눌지는 application 설계 책임이다.
- worker API가 Spot 상태를 worker thread에서 안전하게 만질 수 있게 해 주는 것은
  아니다. Spot 상태 변경은 완료 callback 또는 continuation에서 수행한다.
- 각 언어의 기존 async 표면을 무시하고 별도 runtime 의미를 만들지 않는다.

## 5. 용어

| 용어 | 의미 |
|------|------|
| 직렬 실행 줄 | 같은 소유자의 callback을 한 번에 하나씩 순서대로 실행하는 queue 또는 dispatcher |
| Entry Spot 문맥 | Entry Spot handler와 lifecycle callback이 실행되는 framework 문맥 |
| continuation | request reply, timer 완료, coroutine resume처럼 비동기 작업 뒤 이어지는 사용자 코드 |
| blocking 대기 | dispatch 문맥에서 `.Result`, `.Wait()`, `join()`, blocking `await()` adapter, future `.get()`처럼 현재 thread를 붙잡는 동작 |
| 비재진입 | handler가 반환한 완료 값이 끝날 때까지 같은 Spot의 다음 callback을 시작하지 않는 기본 실행 규칙 |
| 명시적 인터리빙 | handler가 detached callback을 등록하고 끝난 뒤, 완료 callback이 나중에 Spot 실행 줄에 다시 들어오는 opt-in 실행 방식 |
| 재진입 | handler가 끝나기 전에 같은 Entry Spot의 다른 callback이 직접 실행되는 상태 |
| worker offload | Spot 직렬 실행 줄 밖에서 오래 걸리는 작업을 수행하고 완료만 Spot 실행 줄로 되돌리는 기능 |

## 6. 실행 모델

Entry Spot 실행 모델은 아래와 같다.

```text
+----------------------------------------------------------+
| Entry Spot Dispatcher                                    |
+----------------------------------------------------------+
| actor join                                               |
| actor send                                               |
| actor request                                            |
| route packet                                             |
| timer tick                                               |
| lifecycle callback                                       |
| request continuation                                     |
+----------------------------------------------------------+
| Entry Spot State                                         |
+----------------------------------------------------------+
```

그림 안의 모든 항목은 같은 Entry Spot 상태를 만질 수 있으므로 한 번에 하나씩
실행한다. 기본 모드는 비재진입이다. handler가 framework에 완료 값을 반환하면 그
완료가 끝날 때까지 같은 Entry Spot의 다음 callback을 시작하지 않는다.

native 또는 binding request callback이 다른 thread에서 도착하더라도 사용자 callback을
바로 실행하지 않는다. framework runtime이 완료 결과를 소유한 뒤 Entry Spot dispatcher에
작업을 넣는다.

## 7. Request 완료와 인터리빙 규칙

Entry Spot handler 안에서 request를 보낼 수 있다. 다만 dispatch 문맥에서 응답을
기다리며 thread를 막으면 안 된다. request 완료를 받는 방식은 gated awaitable 경로와
detached callback 경로로 나눈다.

gated awaitable 경로는 기본 비재진입 규칙을 따른다.

1. Entry Spot handler가 request를 제출한다.
2. handler가 request 완료 값을 반환하거나 coroutine suspension으로 연결한다.
3. framework는 그 완료가 끝날 때까지 같은 Entry Spot의 다음 callback을 시작하지 않는다.
4. 완료 뒤 이어지는 코드는 같은 Entry Spot 실행 줄에서 실행된다.

detached callback 경로는 명시적 인터리빙 opt-in이다.

1. Entry Spot handler가 request를 제출한다.
2. handler는 callback을 등록하고 끝난다.
3. handler가 끝났으므로 같은 Entry Spot의 다른 callback이 먼저 실행될 수 있다.
4. reply가 도착하면 runtime이 결과를 Entry Spot dispatcher에 넣는다.
5. Entry Spot dispatcher가 callback을 실행한다.
6. callback은 request 제출 시점의 상태가 여전히 유효한지 다시 확인한 뒤 상태를
   바꾸거나 actor reply를 보낸다.

detached callback은 Entry Spot admission 흐름에서 느린 외부 request가 전체 Entry Spot을
오래 점유하지 않게 하려는 경우에 사용한다. 대신 request 제출 시점과 완료 callback
실행 시점 사이에 다른 callback이 Entry Spot 상태를 바꿀 수 있다. 따라서 callback은
actor epoch, current Spot, session binding, pending admission id 같은 application 상태를
재검증해야 한다.

금지되는 흐름:

1. Entry Spot handler가 request를 제출한다.
2. 같은 handler가 reply를 기다리며 dispatch thread를 막는다.
3. reply callback도 같은 Entry Spot dispatcher로 들어와야 해서 진행하지 못한다.

이 금지는 deadlock 방지뿐 아니라 latency tail을 줄이기 위한 규칙이다. awaitable
경로 자체는 blocking이 아니지만, 비재진입 gate 때문에 완료 전까지 같은 Entry Spot을
논리적으로 점유한다. Entry Spot은 Actor 입구이므로 느린 외부 request가 admission 전체를
잡지 않게 해야 하는 경로에서는 detached callback을 선택한다.

## 8. Worker offload API

Spot 점유 문제의 기본 해법은 application이 긴 작업을 Spot 밖으로 빼는 것이다.
framework는 이를 위해 `SpotContext`와 `EntrySpotContext`에 worker offload builder를
추가한다. 이 API는 Spot을 병렬 실행 단위로 바꾸지 않는다. worker는 Spot 상태를
만지지 않는 작업만 수행하고, 완료 후 Spot 상태 변경이 필요하면 원래 Spot 직렬
실행 줄에서 callback 또는 continuation으로 처리한다.

worker offload는 큰 작업 처리 framework가 아니다. 기본 runtime은 하나의 local worker
pool만 가진다. 짧고 빠른 계산이나 Spot dispatcher에서 직접 실행하기 애매한 작은
작업을 빼내는 용도다. DB 조회, HTTP/gRPC 호출, 긴 CPU 작업, 재시도와 스케일아웃이
필요한 작업은 worker pool에 넣지 않고 ZLink request로 별도 service나 server에
위임한다.

공통 흐름은 아래와 같다.

```text
+----------------------------------------------------------+
| Spot Dispatcher                                          |
+----------------------------------------------------------+
| handler submits worker job                               |
| handler returns                                          |
+----------------------------------------------------------+
          |
          v
+----------------------------------------------------------+
| Worker Runtime                                           |
+----------------------------------------------------------+
| long job without Spot state access                       |
+----------------------------------------------------------+
          |
          v
+----------------------------------------------------------+
| Spot Dispatcher                                          |
+----------------------------------------------------------+
| completion callback updates Spot state                   |
+----------------------------------------------------------+
```

worker offload builder는 실행 방식을 fluent terminator로 고른다.

```text
context.runWorker(work)
  .timeout(...)
  .async()

context.runWorker(work)
  .timeout(...)
  .submit(callback)
```

`async()`는 각 언어의 awaitable 결과를 반환한다. `.NET`은 `ValueTask<T>`, Java는
`CompletionStage<T>`, Node.js는 `Promise<T>`, C++는 `task_t<T>` 같은 형태가 된다.
이 방식은 gated awaitable 경로다. handler가 이 완료를 반환하면 같은 Spot의 다음
callback은 완료 전까지 시작하지 않는다. user Spot처럼 coroutine 또는 async/await 흐름이
자연스럽고 상태 인터리빙을 피하고 싶은 곳에서 쓸 수 있다.

`submit(callback)`은 완료를 callback으로 받는다. callback은 worker thread에서 직접
실행되지 않고 원래 Spot dispatcher에 들어간 뒤 실행된다. Entry Spot처럼 dispatch
문맥에서 느린 작업이 admission 전체를 점유하면 안 되는 곳에서는 이 callback terminator를
공식 예제로 우선 사용한다. 이 방식은 명시적 인터리빙 opt-in이므로 callback에서 상태를
다시 확인해야 한다.

worker 함수 안에서는 아래 작업을 해도 된다.

- CPU 계산
- 파일 또는 압축 처리
- blocking만 제공하는 외부 SDK 호출
- Spot 상태와 무관한 데이터 변환

worker 함수 안에서는 아래 작업을 하면 안 된다.

- Spot 또는 Entry Spot 상태 직접 변경
- Actor 객체의 mutable 상태 변경
- Spot context의 send/request/publish 호출
- framework dispatcher에 재진입한다고 가정하는 callback 호출

완료 callback 또는 await 뒤 continuation에서는 원래 Spot 직렬 실행 줄 안에 있으므로
Spot 상태를 바꿀 수 있다. 단, 그 callback 안에서도 다시 오래 걸리는 작업을 직접
실행하면 같은 Spot을 점유하므로 필요한 경우 다시 worker로 넘긴다.

## 9. Worker pool 정책

framework는 기본으로 단일 elastic bounded worker pool을 제공한다. 별도 named pool을
기본 개념으로 두지 않는다. 여러 pool을 두는 설계는 API와 운영 설정을 키우고, 긴
작업까지 local worker에 넣어도 된다는 오해를 만들 수 있다.

기본값은 아래 의미를 따른다.

| 설정 | 기본값 | 의미 |
|------|--------|------|
| `minThreads` | `0` | 작업이 없으면 thread를 유지하지 않는다 |
| `maxThreads` | `max(2, cpuCount * 2)` | 짧은 blocking 가능성을 고려해 CPU 수보다 약간 여유를 둔다 |
| `idleTimeout` | `30s` | idle thread를 줄이는 시간 |
| `maxQueueLength` | `1024` | 장애 시 memory 증가를 제한한다 |
| `queueFullPolicy` | `fail` | Spot dispatcher를 막지 않고 submit 실패로 돌려준다 |

`maxThreads`와 `maxQueueLength`는 host 설정에서 바꿀 수 있어야 한다. 하지만 기본값보다
훨씬 큰 `maxThreads`가 필요하다면 먼저 작업을 다른 service로 분리할 수 있는지
검토한다. `runWorker(...)`는 local latency를 줄이는 도구이지, batch job이나 대량 I/O를
처리하는 queue가 아니다.

queue가 가득 찼을 때 Spot dispatcher에서 자리가 날 때까지 기다리면 안 된다. 그
대기는 Spot 직렬 실행 줄을 막고, worker offload를 둔 이유를 다시 깨뜨린다. 따라서
기본 정책은 실패다. `dropOldest`나 `callerRuns` 같은 정책은 이 계획의 기본 public
계약에 넣지 않는다. 특히 `callerRuns`는 Spot dispatcher에서 긴 작업을 직접 실행하게
만들 수 있으므로 금지한다.

timeout은 호출자에게 완료 실패를 돌려주는 기준이다. worker 함수가 cancellation을
관찰하지 않는 blocking SDK 호출 안에 있으면 runtime이 그 작업을 강제로 중단할 수
없다. 이 경우 작업은 abandoned work가 된다. timeout 이후 늦게 도착한 완료 결과는
버리고 Spot dispatcher에 사용자 완료 callback을 넣지 않는다. 작업이 실제로 끝날
때까지 worker thread는 점유될 수 있으므로, cancellation을 관찰하지 않는 긴 작업은
`runWorker(...)`에 넣지 않고 별도 service로 위임한다.

worker submit 실패와 실행 실패는 아래처럼 전달한다.

| 조건 | awaitable terminator | callback terminator |
|------|----------------------|---------------------|
| queue full | worker queue full error로 완료 실패 | `onError`를 Spot dispatcher에 넣는다 |
| timeout | timeout error로 완료 실패 | `onError`를 Spot dispatcher에 넣는다 |
| worker exception | worker failed error로 완료 실패 | `onError`를 Spot dispatcher에 넣는다 |
| late completion after timeout | 무시 | 무시 |

queue full도 callback terminator에서는 worker thread나 submit 호출 thread에서 사용자
`onError`를 직접 호출하지 않는다. 오류 callback은 성공 callback과 마찬가지로 원래 Spot
dispatcher에서 실행한다. `.NET`은 `ZLinkFrameworkErrorKind.WorkerQueueFull`,
`WorkerTimedOut`, `WorkerFailed`로 이 세 가지 오류를 표현한다. Node.js도 같은 의미를
`ZLinkFrameworkErrorKind` 값으로 노출한다. Java는 `ZLinkWorkerQueueFullException`,
`ZLinkWorkerTimeoutException`, `ZLinkWorkerFailedException`으로 투영한다. C++는 이 세
가지 의미를 `result_t<T>` 오류 또는 framework 예외 타입 중 어느 쪽으로 노출할지
public surface 확정 단계에서 결정해야 한다.

설정 예시는 아래와 같다.

```csharp
options.Worker.MinThreads = 0;
options.Worker.MaxThreads = Environment.ProcessorCount * 2;
options.Worker.IdleTimeout = TimeSpan.FromSeconds(30);
options.Worker.MaxQueueLength = 1024;
```

```java
workers.minThreads(0)
    .maxThreads(Runtime.getRuntime().availableProcessors() * 2)
    .idleTimeout(Duration.ofSeconds(30))
    .maxQueueLength(1024);
```

```ts
worker: {
  minThreads: 0,
  maxThreads: Math.max(2, os.availableParallelism() * 2),
  idleTimeoutMs: 30_000,
  maxQueueLength: 1024,
}
```

```cpp
worker_options_t{
  .min_threads = 0,
  .max_threads = std::max(2u, std::thread::hardware_concurrency() * 2),
  .idle_timeout = 30s,
  .max_queue_length = 1024,
};
```

## 10. Worker offload 인터페이스 초안

### 10.1 .NET

현재 public context에는 같은 builder가 있다. 이 계획은 아래 표면을 공통 계약의
`.NET` 투영으로 유지한다.

```csharp
public interface IZLinkSpotContext
{
    IZLinkWorkerCall<TResult> RunWorker<TResult>(
        Func<CancellationToken, TResult> work);
}

public interface IZLinkEntrySpotContext
{
    IZLinkWorkerCall<TResult> RunWorker<TResult>(
        Func<CancellationToken, TResult> work);
}

public interface IZLinkWorkerCall<TResult>
{
    IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout);

    ValueTask<TResult> Async(CancellationToken cancellationToken = default);

    void Submit(
        Func<TResult, CancellationToken, ValueTask> onCompleted,
        Func<Exception, CancellationToken, ValueTask>? onError = null,
        CancellationToken cancellationToken = default);
}
```

`Async(...)`는 gated awaitable terminator다. handler가 이 값을 `await`하거나 반환하면
같은 Spot의 다음 callback은 완료 전까지 시작하지 않는다. `Submit(...)`은 detached
callback terminator다. `onCompleted`와 `onError`는 worker thread나 submit 호출 thread에서
직접 실행되지 않고 같은 Spot dispatcher에 들어간 뒤 실행된다.

### 10.2 Java / Kotlin

현재 public context에는 같은 builder가 있다. 이 계획은 아래 표면을 Java/Kotlin 투영으로
유지한다.

```java
public interface ZLinkSpotContext {
    <T> ZLinkWorkerCall<T> runWorker(ZLinkWorkerTask<T> work);
}

public interface ZLinkEntrySpotContext {
    <T> ZLinkWorkerCall<T> runWorker(ZLinkWorkerTask<T> work);
}

@FunctionalInterface
public interface ZLinkWorkerTask<T> {
    T run(CancellationToken cancellationToken) throws Exception;
}

public interface ZLinkWorkerCall<T> {
    ZLinkWorkerCall<T> timeout(Duration timeout);

    CompletionStage<T> submit();

    void submit(
        BiConsumer<T, CancellationToken> onCompleted,
        BiConsumer<Throwable, CancellationToken> onError);
}
```

Kotlin은 `submit()`을 `suspend` extension으로 감쌀 수 있다. 다만 Entry Spot guide와
sample에서는 blocking `await()` adapter가 아니라 callback terminator 또는 Kotlin
coroutine suspension을 사용한다.

### 10.3 Node.js / TypeScript

현재 TypeScript contract에는 같은 builder가 있다. 이 계획은 아래 표면을 Node.js 투영으로
유지한다.

```ts
interface ZLinkSpotContext {
  runWorker<T>(
    work: (signal: AbortSignal) => T | Promise<T>,
  ): ZLinkWorkerCall<T>;
}

interface ZLinkEntrySpotContext {
  runWorker<T>(
    work: (signal: AbortSignal) => T | Promise<T>,
  ): ZLinkWorkerCall<T>;
}

interface ZLinkWorkerCall<T> {
  timeoutMs(durationMs: number): ZLinkWorkerCall<T>;

  submit(signal?: AbortSignal): Promise<T>;

  onCompleted(
    callback: (result: T, signal?: AbortSignal) => void | Promise<void>,
    onError?: (error: unknown, signal?: AbortSignal) => void | Promise<void>,
    signal?: AbortSignal,
  ): void;
}
```

Node.js의 closure는 `worker_threads`로 그대로 전달할 수 없다. 따라서 위 closure 기반
`runWorker(...)`는 기본 public 계약에서는 Spot queue 점유를 풀기 위한 비동기 deferral로
본다. `maxThreads`와 `maxQueueLength`는 동시에 진행시키는 deferral 수와 대기열을
제한하는 의미로 투영한다. CPU 작업을 실제 worker thread로 넘기는 표면은 별도 task
registration 또는 module path 기반 API로 따로 검토한다.

Node 구현은 closure를 main event loop에서 오래 실행하면 안 된다. public 계약의 핵심은
완료 callback이 Spot dispatcher에서 실행된다는 점이며, CPU offload를 보장하는 계약은
이 초안의 기본 `runWorker(...)`에 넣지 않는다.

### 10.4 C++

C++에는 아직 이 draft의 public `run_worker(...)` builder가 없다. 아래 표면은 구현 전
후보이며, 성공과 실패를 `result_t<T>` 하나로 받는 callback terminator를 우선한다.

```cpp
auto call = context.run_worker([] {
    return load_profile();
});

call.timeout(2s)
    .submit([](result_t<profile_t> result) -> task_t<void> {
        co_return;
    });
```

또는 coroutine terminator를 사용한다.

```cpp
auto call = context.run_worker([] {
    return load_profile();
});

profile_t profile = co_await call.async();
```

`submit(...)`과 `async()`는 같은 call object에서 둘 다 호출하는 메서드가 아니라 서로
다른 terminator다. `submit(...)` callback과 `co_await call.async()` 뒤 coroutine resume은
모두 원래 Spot executor에서 실행된다. `submit(...)` callback이 `task_t<void>`를 반환하면
runtime은 그 task 완료를 callback 완료로 관찰해야 한다. `work`는 Spot executor 밖에서
실행된다.

## 11. 일반 Spot과 Entry Spot의 동일 기준

정식 구현 후에는 아래 기준을 공통 계약으로 둔다.

| 항목 | 일반 Spot | Entry Spot |
|------|-----------|------------|
| handler 실행 | Spot별 직렬 실행 | Entry Spot별 직렬 실행 |
| actor packet | 같은 Spot 안에서 순서 보장 | Entry Spot 안에서 순서 보장 |
| lifecycle callback | 같은 Spot 상태와 같은 실행 줄 | Entry Spot 상태와 같은 실행 줄 |
| timer callback | 같은 Spot callback과 겹치지 않음 | Entry Spot callback과 겹치지 않음 |
| request continuation | Spot 실행 줄로 재진입 | Entry Spot 실행 줄로 재진입 |
| worker completion | Spot 실행 줄로 재진입 | Entry Spot 실행 줄로 재진입 |
| blocking request 대기 | dispatch 문맥에서 금지 | dispatch 문맥에서 금지 |

Actor가 user Spot으로 이동한 뒤에는 user Spot 실행 줄이 그 Actor의 domain 상태를
보호한다. Entry Spot은 admission과 초기 라우팅을 맡고, 장기 game room 또는 workflow
상태는 user Spot으로 옮기는 설계를 권장한다.

## 12. 언어별 구현 계획

### 12.1 .NET

현재 `.NET` framework에는 `RunWorker(...)`, worker pool 옵션, worker 오류 분류,
`DestroyActorAsync(...)` public 표면이 있다. 구현 점검은 아래 방향으로 맞춘다.

- Entry Spot 전용 dispatcher 또는 mailbox를 명확한 소유 모듈로 둔다.
- actor join, actor packet, route packet, subscription, timer, lifecycle callback을
  같은 dispatcher에 넣는다.
- request reply callback은 native callback thread에서 사용자 코드를 실행하지 않고
  Entry Spot dispatcher로 post한다.
- `ValueTask` handler는 완료될 때까지 같은 Entry Spot의 다음 callback을 시작하지
  않는다.
- `RunWorker(...).Async(...)`와 `RunWorker(...).Submit(...)` 완료는 Entry Spot dispatcher로
  돌아온 뒤 실행한다.
- worker pool은 기본 단일 elastic bounded pool을 사용하고, host 옵션으로 `minThreads`,
  `maxThreads`, `idleTimeout`, `maxQueueLength`를 설정한다.
- `WorkerQueueFull`, `WorkerTimedOut`, `WorkerFailed`가 awaitable terminator와 callback
  terminator에서 같은 의미로 전달되는지 확인한다.
- `IZLinkEntrySpotContext.DestroyActorAsync(...)`가 public 이름이다. 이전 draft나 guide에
  `destroyActor(...)` 같은 표기가 남아 있으면 `.NET` 문서에서는 `DestroyActorAsync(...)`로
  정리한다.
- Entry Spot dispatch 문맥에서 `.Result`, `.Wait()`, blocking bridge 사용을 감지할 수
  있으면 runtime guard 또는 analyzer/test로 막는다.
- `ConfigureAwait(false)`는 사용하더라도 Entry Spot 상태를 만지는 continuation은
  dispatcher로 돌아온 뒤 실행한다.

대상 파일군:

- `framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkEntrySpotActivation*.cs`
- `framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/ZLinkSpotActivation*.cs`
- `framework/languages/dotnet/src/Zlink.Framework/Contracts/Spots/`
- `framework/languages/dotnet/tests/Zlink.Framework.*Tests/`

### 12.2 Node.js / TypeScript

Node는 JavaScript event loop 덕분에 한 thread에서 실행되는 것처럼 보이지만,
`await` 뒤의 continuation 순서와 native callback 진입 위치를 framework가 명확히
정해야 한다. 구현은 아래 방향으로 맞춘다.

- Entry Spot별 promise queue 또는 async dispatcher를 둔다.
- handler가 `Promise`를 반환하면 그 promise가 settle될 때까지 같은 Entry Spot의 다음
  callback을 시작하지 않는다.
- native/binding callback은 사용자 callback을 바로 부르지 않고 Entry Spot queue에
  작업을 추가한다.
- `requestToSpot(...).submit()` 같은 API는 `Promise`를 반환하되, Entry Spot 상태 변경
  continuation은 queue 안에서 실행되게 한다.
- `runWorker(...).submit()`과 `runWorker(...).onCompleted(...)`의 완료 처리는 Entry Spot
  queue에 들어간 뒤 실행한다.
- worker runtime은 기본 단일 bounded deferral scheduler 의미를 따른다. Node의 closure
  기반 `runWorker(...)`는 CPU 작업을 별도 worker thread에서 실행하지 않으므로
  `maxThreads`는 실제 worker thread 수가 아니라 동시에 진행시킬 deferral 수로 설명한다.
- CPU를 오래 잡는 동기 작업은 closure 기반 `runWorker(...)`로 해결할 수 없으므로 guide에서
  user Spot 분리, 별도 process, 또는 request 기반 service 위임을 권장한다.
- `WorkerQueueFull`, `WorkerTimedOut`, `WorkerFailed`가 `ZLinkFrameworkErrorKind`로
  전달되고, timeout 뒤 late completion이 사용자 callback을 다시 실행하지 않는지 유지한다.

대상 파일군:

- `framework/languages/node/packages/framework/src/contracts/Spots/`
- `framework/languages/node/packages/framework/src/runtime/`
- `framework/languages/node/packages/framework/src/contracts/Handlers/`
- `framework/languages/node/test/contract/`

### 12.3 C++

C++ framework는 callback/result 표면과 coroutine 표면을 함께 가질 수 있다. 구현은
아래 방향으로 맞춘다.

- Entry Spot runtime에 일반 Spot runtime과 같은 serial executor 또는 mailbox를 사용한다.
- callback 기반 request 완료는 Entry Spot executor에 post한다.
- `task_t` coroutine resume은 Entry Spot executor를 통해 일어나야 한다.
- `run_worker(...).submit(...)` callback과 `co_await run_worker(...).async()` 뒤 resume은
  Entry Spot executor에서 실행한다.
- worker runtime은 기본 단일 elastic bounded pool을 사용한다. 현재 내부 coroutine/offload
  executor가 있더라도, public `run_worker(...)` 계약은 queue full 실패, timeout, late
  completion drop, dispatcher 재진입까지 포함하는 별도 표면으로 확정한다.
- `submit(...)` callback이 `task_t<void>`를 반환하면 executor는 그 task 완료를 callback
  완료로 관찰한다. 이 규칙이 없으면 callback 안의 coroutine이 끝나기 전에 같은 Spot의
  다음 callback이 시작될 수 있어 비재진입 의미가 깨진다.
- C++ 오류 전달은 `result_t<T>` 오류 값으로 통일할지, framework 예외 타입으로 투영할지
  public header를 확정하기 전에 결정한다.
- `.result()` 같은 blocking bridge를 Entry Spot dispatch 문맥에서 호출하지 못하게
  guard한다.
- 일반 Spot과 Entry Spot이 같은 dispatcher 추상화를 공유하되, user Spot별 queue와 Entry
  Spot별 queue는 별도 인스턴스로 둔다.

대상 파일군:

- `framework/languages/cpp/framework/src/runtime/spots/`
- `framework/languages/cpp/framework/include/zlink/framework/contracts/spots/`
- `framework/languages/cpp/tests/Zlink.Framework.UnitTests/`
- `framework/languages/cpp/tests/Zlink.Framework.ContractTests/`

### 12.4 Java / Kotlin

Java framework는 `CompletionStage`를 내부 완료 표현으로 사용한다. 현재
`ZLinkWorkerTask<T>`는 checked exception을 표현할 수 있고, `ZLinkWorkerCall<T>`는
awaitable `submit()`과 callback `submit(...)`을 가진다. 구현 점검은 아래 방향으로 맞춘다.

- Entry Spot actor packet, actor join, lifecycle, timer를 Entry Spot serial queue로
  통합한다.
- backend request callback은 `CompletableFuture`를 완료시키는 데서 끝내고, 사용자
  continuation은 Entry Spot queue에서 실행한다.
- `runWorker(...).submit()` 완료와 callback terminator는 Entry Spot queue로 돌아온 뒤
  사용자 코드를 실행한다.
- worker pool은 기본 단일 elastic bounded pool을 사용하고, framework registration에서
  thread와 queue 한도를 설정한다.
- `ZLinkWorkerQueueFullException`, `ZLinkWorkerTimeoutException`,
  `ZLinkWorkerFailedException`이 awaitable terminator와 callback terminator에서 같은
  의미로 전달되는지 확인한다.
- handler가 직접 값을 반환하든 `CompletionStage`를 반환하든 runtime은 하나의 stage로
  정규화하고, 그 stage 완료 전에는 같은 Entry Spot의 다음 callback을 시작하지 않는다.
- Java blocking `await()` adapter는 connector나 sample client 편의 표면에만 허용하고,
  framework dispatch 문맥에서는 금지한다.
- Kotlin `suspend` handler는 Java serial queue 의미를 유지한다. coroutine dispatcher가
  별도 순서를 만들면 안 된다.

대상 파일군:

- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/spots/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/binding/`
- `framework/languages/java/zlink-framework-core/src/test/`
- `framework/languages/java/zlink-framework-testkit/src/fakeBackendTest/`
- `framework/languages/java/zlink-framework-kotlin/src/test/`

## 13. 기존 문서 변경 계획

이 draft가 구현으로 닫히면 아래 문서를 수정한다.

### 13.1 공통 framework 문서

- `framework/doc/spec/async-execution-policy.ko.md`
  - callback 기반 completion도 소유 dispatcher로 재진입해야 한다는 문장을 추가한다.
  - dispatch 문맥의 blocking 대기 금지를 공통 규칙으로 올린다.
  - worker offload의 awaitable terminator와 callback terminator 의미를 추가한다.
  - worker pool은 기본 단일 elastic bounded pool이며 큰 작업은 request로 service에
    위임한다는 기준을 추가한다.
  - 비재진입이 기본이고 detached callback은 명시적 인터리빙 opt-in이라는 기준을 추가한다.
- `framework/doc/spec/interaction-model.ko.md`
  - Spot과 Entry Spot의 callback 실행 위치를 같은 규칙으로 설명한다.
- `framework/doc/spec/actor-model.ko.md`
  - Entry Spot은 admission 입구이지만 실행 규칙은 Spot과 같다는 점을 명시한다.
- `framework/doc/spec/session-actor-dispatch.ko.md`
  - session에서 actor로 relay한 뒤 Entry Spot handler와 continuation이 어디에서 실행되는지
    설명한다.
- `framework/doc/spec/message-model.ko.md`
  - 이 변경이 payload encode/decode가 아니라 callback 실행 순서 정책임을 명확히 한다.

### 13.2 언어별 spec 문서

- `.NET`
  - `framework/languages/dotnet/doc/spec/aspnet-core-spot.ko.md`
  - `framework/languages/dotnet/doc/spec/aspnet-core-actor.ko.md`
  - `framework/languages/dotnet/doc/spec/session-actor-dispatch.ko.md`
  - `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md`
  - 현재 public `IZLinkEntrySpotContext`는 `DestroyActorAsync(...)`를 가진다. 구현 전
    draft나 오래된 guide에 `destroyActor(...)`처럼 lower camel 표기가 남아 있으면
    `.NET` 문서에서는 `DestroyActorAsync(...)`로 정리하고, contract test로 재발을 막는다.
- Node.js
  - `framework/languages/node/doc/spec/nestjs-spot.ko.md`
  - `framework/languages/node/doc/spec/nestjs-actor.ko.md`
  - `framework/languages/node/doc/spec/session-actor-dispatch.ko.md`
  - `framework/languages/node/doc/spec/handler-interfaces.ko.md`
- C++
  - `framework/languages/cpp/doc/spec/cpp-spot.ko.md`
  - `framework/languages/cpp/doc/spec/actor-gateway-session-relay.ko.md`
  - `framework/languages/cpp/doc/spec/handler-interfaces.ko.md`
  - `framework/languages/cpp/doc/internals/cpp-framework-policy.ko.md`
- Java / Kotlin
  - `framework/languages/java/doc/spec/spring-boot-spot.ko.md`
  - `framework/languages/java/doc/spec/spring-boot-actor-session.ko.md`
  - `framework/languages/java/doc/spec/handler-interfaces.ko.md`
  - `framework/languages/java/doc/internals/lifecycle-and-failure-semantics.ko.md`

Java spec의 이전 예외 문구는 제거한다. 정식 spec은 Entry Spot도 Spot과 같은 직렬 실행
규칙을 따른다고 설명해야 한다.

### 13.3 Guide 문서

guide에는 내부 queue 구현을 자세히 설명하지 않는다. 대신 사용자가 알아야 하는 규칙만
적는다.

- Spot 또는 Entry Spot handler 안에서 공유 상태를 직접 lock으로 보호하지 않아도 같은
  Spot callback끼리는 동시에 실행되지 않는다.
- handler 안에서 외부 request를 보낼 때는 async API를 사용한다.
- dispatch 문맥에서 blocking 대기를 하면 안 된다.
- 오래 걸리는 작업은 user Spot, channel worker, 외부 service, 또는 `runWorker(...)`
  offload로 분리한다.
- Entry Spot 예제에서는 worker 완료를 callback terminator로 받는 방식을 우선 보여 준다.
- `runWorker(...)`는 짧고 빠른 local 작업에만 사용하고, 큰 작업은 request로 다른
  service에 넘긴다.

대상 문서:

- `framework/languages/dotnet/doc/guide/05-spot.ko.md`
- `framework/languages/dotnet/doc/guide/06-actor-session.ko.md`
- `framework/languages/node/doc/guide/06-spot.ko.md`
- `framework/languages/node/doc/guide/07-actor-session.ko.md`
- `framework/languages/cpp/doc/guide/08-spot.ko.md`
- `framework/languages/cpp/doc/guide/09-actor-session.ko.md`
- `framework/languages/java/doc/guide/06-spot.ko.md`
- `framework/languages/java/doc/guide/07-actor-session.ko.md`

## 14. 회귀 테스트 계획

### 14.1 공통 테스트 시나리오

각 언어는 같은 의미의 테스트를 가진다.

| 테스트 | 검증 내용 |
|--------|-----------|
| Entry Spot 순서 | actor send 두 개와 lifecycle callback 하나를 동시에 넣어도 기록 순서가 dispatcher 순서와 같다 |
| request continuation 재진입 | Entry Spot handler가 request를 보내고 reply callback에서 Entry Spot 상태를 바꿔도 다른 callback과 겹치지 않는다 |
| 재진입 금지 | handler 실행 중 native callback이 도착해도 사용자 callback이 직접 중첩 호출되지 않는다 |
| blocking 금지 | Entry Spot dispatch 문맥에서 blocking wait adapter를 호출하면 실패하거나 테스트 규칙에 걸린다 |
| timer 직렬성 | timer callback과 actor packet handler가 같은 Entry Spot 상태를 동시에 바꾸지 않는다 |
| worker 완료 재진입 | worker thread에서 완료된 작업의 callback이 Spot dispatcher 순서 안에서 실행된다 |
| worker 상태 접근 금지 | worker 함수 안에서 Spot 상태나 context outbound를 직접 사용하지 않는 계약을 테스트 또는 analyzer로 검출한다 |
| worker queue full | worker queue가 가득 차면 Spot dispatcher를 기다리게 하지 않고 submit 실패로 끝난다 |
| worker idle shrink | 작업이 없으면 기본 worker thread가 idle timeout 뒤 줄어든다 |
| worker timeout late completion | timeout 뒤 늦게 끝난 worker 결과가 Spot callback을 다시 실행하지 않는다 |
| worker 오류 투영 | queue full, timeout, worker exception이 각 언어의 정해진 오류 타입이나 오류 kind로 전달된다 |
| worker terminator 단일성 | 같은 worker call에서 awaitable terminator와 callback terminator를 둘 다 호출하면 실패한다 |
| detached callback 상태 재검증 | request 제출 뒤 다른 callback이 상태를 바꾼 경우 completion callback이 stale 상태를 감지한다 |
| user Spot 분리 | Actor가 user Spot으로 이동한 뒤 user Spot 상태는 user Spot queue에서 보호되고 Entry Spot queue와 섞이지 않는다 |
| failure cleanup | request 실패, timeout, handler 예외 뒤에도 Entry Spot dispatcher가 다음 작업을 처리한다 |
| admission burst perf | 많은 Actor가 동시에 Entry Spot으로 들어올 때 baseline 대비 처리량과 tail latency가 허용 범위 안에 있다 |

### 14.2 .NET 테스트

- `Zlink.Framework.UnitTests`
  - Entry Spot activation에 fake dispatcher clock 또는 controlled task source를 넣어 순서를
    검증한다.
  - request callback이 `TaskCompletionSource` 완료 thread에서 바로 실행되지 않는지 확인한다.
  - `RunWorker(...).Submit(...)` callback이 worker thread가 아니라 Entry Spot dispatcher에서
    실행되는지 확인한다.
- `Zlink.Framework.ContractTests`
  - Entry Spot handler 계약에서 blocking API 사용 예제가 없는지 확인한다.
- `Zlink.Framework.E2ETests`
  - TicTacToe/Bingo Entry Spot 흐름에서 actor join, request, destroy, disconnected callback이
    순서대로 처리되는지 확인한다.

### 14.3 Node.js 테스트

- framework unit test
  - fake backend callback을 `setImmediate`와 `queueMicrotask`로 섞어도 Entry Spot 기록 순서가
    유지되는지 확인한다.
  - `Promise`가 settle되기 전 다음 Entry Spot callback이 시작되지 않는지 확인한다.
  - `runWorker(...).onCompleted(...)` callback이 Entry Spot queue에서 실행되는지 확인한다.
- contract/sample regression
  - NestJS Spot/Actor handler가 `Promise` 기반으로만 작성되는지 확인한다.
  - Bingo/TicTacToe sample smoke에서 Entry Spot request continuation 순서를 기록한다.
  - Node의 closure 기반 `runWorker(...)`가 CPU 작업을 별도 worker thread에서 실행한다고
    문서나 타입에서 말하지 않는지 확인한다.

### 14.4 C++ 테스트

- unit test
  - Entry Spot executor에 actor packet, timer, request completion을 넣고 순서를 확인한다.
  - callback completion과 coroutine resume이 같은 executor에서 실행되는지 확인한다.
  - `run_worker(...).submit(...)`과 `run_worker(...).async()` 완료가 Spot executor로 돌아오는지
    확인한다.
- contract test
  - public header에 blocking-only Entry Spot request helper가 생기지 않았는지 확인한다.
- CTest sample smoke
  - TicTacToe/Bingo sample에서 Entry Spot admission 중 request를 보내는 흐름을 추가하거나
    기존 흐름의 callback 순서 log를 확인한다.

### 14.5 Java / Kotlin 테스트

- `:zlink-framework-core:test`
  - Entry Spot serial queue 순서와 handler stage 완료 기준을 검증한다.
- `:zlink-framework-core:integrationTest`
  - backend request callback이 Entry Spot 사용자 callback을 직접 실행하지 않는지 확인한다.
  - `runWorker(...).submit(callback)` callback이 Entry Spot serial queue에서 실행되는지
    확인한다.
- `:zlink-framework-testkit:fakeBackendTest`
  - fake backend에서 actor packet과 request reply callback을 교차 발생시켜 순서를 검증한다.
- `:zlink-framework-kotlin:test`
  - Kotlin `suspend` handler의 continuation이 Java core serial queue 의미를 깨지 않는지
    확인한다.
- sample build
  - Java/Kotlin TicTacToe/Bingo sample에서 blocking `await()`가 server dispatch 문맥에 남지
    않았는지 확인한다.

## 15. 구현 순서

1. 현재 네 언어의 Entry Spot dispatch와 일반 Spot dispatch 차이를 표로 정리한다.
2. 공통 dispatcher 요구사항을 확정한다.
3. 비재진입 기본과 detached callback 인터리빙 opt-in 규칙을 확정한다.
4. worker offload builder의 공통 계약과 언어별 이름을 확정한다.
5. worker pool 기본값과 host 설정 표면을 확정한다.
6. 한 언어에서 먼저 구현해 순서 테스트, request continuation 테스트, worker completion
   테스트를 통과시킨다.
7. admission burst 성능 측정 기준을 잡고 baseline을 남긴다.
8. 같은 구조를 나머지 언어로 옮긴다.
9. 언어별 문서에서 Entry Spot 예외 문구를 제거한다.
10. 공통 spec에 직렬 실행과 worker offload 정책을 승격한다.
11. sample과 guide에서 blocking request 대기 예제를 제거한다.
12. Entry Spot 예제에는 callback terminator 기반 worker offload 예제를 추가한다.
13. 네 언어의 unit, contract, sample smoke를 통과시킨다.

## 16. 검증 명령 후보

실제 명령은 각 언어의 현재 빌드 상태에 맞춰 조정한다.

```bash
# .NET
dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore
dotnet test framework/languages/dotnet/tests/Zlink.Framework.ContractTests/Zlink.Framework.ContractTests.csproj --no-restore
dotnet test framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj --no-restore

# Node.js
npm run build --prefix framework/languages/node
npm test --prefix framework/languages/node
node --test framework/languages/node/test/contract/*.test.js

# C++
cmake --build framework/languages/cpp/build --target test_cpp_framework_spot_runtime test_cpp_framework_contract_headers -j2
ctest --test-dir framework/languages/cpp/build --output-on-failure -R 'framework.*spot|contract_headers|sample'

# Java / Kotlin
./gradlew :zlink-framework-core:test :zlink-framework-core:integrationTest --no-daemon --no-parallel
./gradlew :zlink-framework-testkit:fakeBackendTest :zlink-framework-kotlin:test --no-daemon --no-parallel
./gradlew buildAllSamples --no-daemon --no-parallel
```

## 17. 위험과 대응

| 위험 | 대응 |
|------|------|
| Entry Spot 입구 병목 | Entry Spot에는 admission과 초기 라우팅만 두고 장기 domain 상태는 user Spot으로 옮기도록 guide에 적는다 |
| worker API가 자동 병렬화로 오해됨 | worker 함수 안에서 Spot 상태를 만지면 안 된다는 계약과 예제를 함께 둔다 |
| worker pool이 job queue로 오해됨 | 단일 local pool, bounded queue, 큰 작업은 request 위임이라는 기준을 문서화한다 |
| queue full 처리가 Spot을 막음 | queue full은 submit 실패로 끝내고 wait 정책은 제공하지 않는다 |
| thread가 계속 남음 | idle timeout 뒤 줄어드는 elastic bounded pool을 기본으로 둔다 |
| timeout 뒤 worker가 계속 실행됨 | timeout은 caller 완료만 실패시키며 늦은 완료는 버린다. cancellation을 관찰하지 않는 긴 작업은 request로 외부 service에 위임한다 |
| worker completion이 callback thread에서 실행됨 | completion 위치 테스트를 각 언어에 둔다 |
| detached callback이 stale 상태를 사용함 | detached callback은 명시적 인터리빙 opt-in이며 상태 재검증 책임이 있음을 guide와 sample에 넣는다 |
| 기존 handler가 callback thread에서 바로 실행된다는 가정 | contract test와 migration note로 금지한다 |
| async handler가 완료되지 않아 queue가 멈춤 | timeout, cancellation, shutdown drain 테스트를 둔다 |
| blocking wait로 deadlock | dispatch 문맥 guard와 sample regression을 둔다 |
| 언어별 queue 의미 불일치 | 공통 테스트 시나리오 이름과 기대 결과를 맞춘다 |
| callback 실패 후 dispatcher 중단 | 실패를 runtime event로 기록하고 다음 작업이 진행되는지 테스트한다 |

## 18. 완료 기준

이 계획은 아래 조건을 만족하면 정식 문서로 승격할 수 있다.

1. 네 언어 framework가 Entry Spot callback과 request continuation을 같은 Entry Spot 직렬
   실행 줄에서 실행한다.
2. 비재진입이 기본이고 detached callback 경로가 명시적 인터리빙 opt-in으로 문서화된다.
3. dispatch 문맥 blocking 대기를 금지하거나 테스트로 검출한다.
4. `SpotContext`와 `EntrySpotContext`에 worker offload builder가 있고, 완료 callback과
   awaitable continuation이 원래 Spot 실행 줄로 돌아온다.
5. worker pool은 기본 단일 elastic bounded pool이며, 기본 max thread는 `cpuCount * 2`
   수준이고 idle timeout 뒤 줄어든다.
6. worker queue full은 Spot dispatcher를 막지 않고 submit 실패로 처리된다.
7. worker timeout 뒤 늦은 완료는 사용자 callback을 다시 실행하지 않는다.
8. 많은 Actor가 동시에 Entry Spot에 들어오는 admission burst 성능 측정이 baseline과
   비교되어 허용 범위 안에 있다. 기본 허용 기준은 처리량 10% 이상 하락 또는 p95
   latency 20% 이상 증가를 blocker로 보는 것이다. 구현 언어나 runtime 특성 때문에
   다른 기준이 필요하면 rollout 시작 전에 문서에 명시한다.
9. 일반 Spot과 Entry Spot의 실행 규칙 차이를 설명하던 기존 문서 문구가 제거된다.
10. 공통 spec과 언어별 spec, guide가 같은 의미를 말한다.
11. 각 언어의 unit, contract, sample smoke가 통과한다.
12. Entry Spot 병목을 피하는 설계 권장 사항과 worker offload 사용 예제가 guide에 짧게
   반영된다.
