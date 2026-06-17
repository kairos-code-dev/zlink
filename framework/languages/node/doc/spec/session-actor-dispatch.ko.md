<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework NestJS Actor](./nestjs-actor.ko.md) | [다음: ZLink Framework NestJS Monitoring](./nestjs-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[Node 묶음](../README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Actor](./nestjs-actor.ko.md) | [STREAM](./nestjs-stream.ko.md) | [policy/Session Actor Dispatch 사용성](../../../../doc/spec/session-actor-dispatch.ko.md)

# ZLink Framework Node.js Session Actor Dispatch

> 이 문서는 [.NET session-actor-dispatch spec](../../../dotnet/doc/spec/session-actor-dispatch.ko.md)
> 의 Node.js / NestJS 표면 이식이다. **dispatch 의미(순서, Entry Spot vs user Spot
> routing, 실행 문맥)는 backend 와 무관한 공통 의미론이므로 .NET 과 완전히
> 동일하다.** 표면 용어(`IZLinkX`→`ZLinkX`, `HandleAsync`→`handle`,
> `ValueTask`→`Promise`, attribute→decorator, `RoutingId`→string, ASP.NET→NestJS)
> 만 옮긴다. 이 문서대로 구현하면 .NET 과 동일한 dispatch 동작이 나온다. 표기가
> .NET 문서와 어긋나면 .NET **코드**(`framework/languages/dotnet/src`)가 기능의
> 최종 기준이다.

## 1. 목적

이 문서가 다루는 범위는 다음과 같다.

- session 서버와 play 서버를 분리하는 구조를 `Node.js` 사용자가 실제 시그니처와
  module 등록 코드 모양으로 살펴 볼 수 있도록 정리한다.
- cross-binding 의미 자체는
  [policy/session-gateway-usability.ko.md](../../../../doc/spec/session-actor-dispatch.ko.md)
  에서 다룬다.
- 따라서 여기서는 `Node.js` 표면만 다룬다.

## 2. 핵심 표면 요약

이 절은 session actor dispatch 가 `Node.js` 에서 어떤 형태로 노출되는지를 한눈에
정리한다. 핵심 표면은 다음 네 축이다.

| 축 | `Node.js` 표면 |
|----|-------------|
| session → actor relay | `ZLinkSessionContext.actors.bind(...)`, `ZLinkSessionActor.relay(...)` |
| spot actor handler | `ZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>`, `ZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>`, `ZLinkSpotActorSendHandler<TSpot, TActor, TMessage>`, `ZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` |
| actor → own client push | Spot actor handler 가 받은 actor 의 `context.boundSession.send(msg).submit(...)` |
| 다른 actor → client push | 먼저 대상 actor 에 메시지를 보내고, 대상 Spot actor handler 가 actor `context.boundSession` 으로 push |
| route 해석 | session relay 는 logical actor id/type handle 을 사용하고, core ActorGateway 가 현재 actor 위치를 해석한다. actor → client push 방향은 framework/core가 가진 actor-session binding[^actor-session-binding]을 사용한다 |

인터페이스 전체 정의는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)
§4.4, §5.5, §5.6 에 모여 있다. 이 문서에서는 사용 모양과 등록 코드
예시만 모아 둔다.

## 2.1 내부 routed wire 계약

이 절에서는 session 서버와 play 서버 사이의 wire 단계 규약을 정리한다.

session actor dispatch 의 public API 는 typed object 중심이다. 다만 서버
사이를 잇는 내부 route transport 단계에서는 공통
[message-model.ko.md](../../../../doc/spec/message-model.ko.md) 가 정한 multipart 계약을
그대로 따른다.

Session 서버에서 Play 서버 actor 로 보내는 actor dispatch request / send 는
아래와 같은 part 구성을 사용한다.

| part | 내용 |
|------|------|
| `parts[0]` | routed framework header. packet name은 internal actor dispatch packet 이름을 사용한다 |
| `parts[1]` | actor dispatch metadata. actor route와 함께, local actor를 새로 만들어야 하는 경우를 위한 `actorId`, `actorType`만 둔다 |
| `parts[2]` | encoded stream header bytes. stream packet kind, codec, request sequence, packet name, metadata snapshot은 모두 이 part에 둔다 |
| `parts[3]` | application payload bytes. framework codec이나 stream packet codec이 만든 payload를 그대로 둔다 |

반대 방향도 같은 원칙을 따른다. Play 서버 actor 에서 Session 서버의 client
stream 으로 보내는 bound session send / request 는 다음 part 구성을 사용한다.

| part | 내용 |
|------|------|
| `parts[0]` | routed framework header. packet name은 internal bound session packet 이름을 사용한다 |
| `parts[1]` | bound session metadata. `actorId`, `bindingToken`, client packet name, reply 필요 여부, metadata snapshot을 함께 담는다 |
| `parts[2]` | application payload bytes |

reply 도 같은 원칙을 따른다. routed reply header 는 `parts[0]` 에 두고, reply
payload 는 별도 part 로 둔다. payload 가 없으면 빈 payload part 를 그대로 남긴다.

다음과 같은 형태는 이 문서의 내부 routed wire 계약이 아니다.

- `ZLinkActorDispatchPacket` 같은 단일 DTO 안에 `streamHeader` 와 `payload: Buffer`
  를 같이 넣고, 그 DTO 전체를 다시 JSON 으로 직렬화하는 방식
- `ZLinkBoundSessionPacket` 같은 단일 DTO 안에 proxy metadata 와 payload bytes 를
  함께 묶는 방식
- `parts[0]` 한 part 만 보내고 그 안에서 header 와 payload 를 모두 decode 하는
  방식

이 제한은 단순히 성능 때문만이 아니다. 다음 두 가지 이유 때문이다.

- route 와 dispatch 는 header 와 metadata 만 읽고도 target 과 handler 를
  결정할 수 있어야 한다.
- application payload 는 handler 가 정해진 뒤에 등록된 codec 이 decode 해야 한다.

그래야 큰 payload, binary payload, 압축 payload, attachment 가 들어와도 framework 내부
route 경로가 payload 재인코딩 비용을 떠안지 않는다.

STREAM 자체는 예외다. client 와 Session 서버 사이의 STREAM transport 는
stream packet 하나 안에 stream header / payload frame 을 그대로 담는다. 위에서
말한 multipart 계약은 Session 서버와 Play 서버처럼 framework 서버끼리
주고받는 routed transport 구간에만 적용한다.

## 2.2 actor mailbox와 실행 순서

이 절은 actor 가 받은 packet 이 어디서 직렬화되는지, 그리고 왜 별도의 session
mailbox 를 두지 않는지 설명한다.

먼저 기본 전제는 이렇다.

- stream socket 은 같은 session 으로 들어온 frame 의 도착 순서를 보존한다.
- framework 는 같은 session 의 callback 을 직렬로 실행한다.
- 따라서 session actor dispatch 를 위해 별도의 session mailbox 를 application
  표면에 따로 두지 않는다.

Session 서버가 받은 stream packet 을 actor 로 relay 할 때, framework 는 현재
actor 위치에 맞는 실행 경계로 그 packet 을 넘긴다. actor 가 Entry Spot 에 있거나
아직 user Spot 에 들어가지 않은 상태라면 대상 actor 의 순서 규칙을 거친다.
actor 별 순서 규칙은 두 가지 역할을 한다.

- 같은 actor 로 들어온 packet 사이의 순서를 지킨다.
- 서로 다른 actor 의 packet 이 서로의 처리를 막지 않도록 보장한다.

이 규칙은 session relay 뿐 아니라 Entry Spot[^entry-spot] actor packet 에도
그대로 적용한다. Entry Spot 은 모든 actor 가 처음 거치는 공용 입구이기
때문이다. Entry Spot 전체에 실행 줄을 하나만 두고 actor packet 을 처리하면,
서로 관련 없는 actor 들이 한 줄로 묶여 막혀 버린다.

반면 user Spot[^user-spot] 안에서 처리되는 actor packet 은 user Spot 의 실행
queue 에서 handler 를 실행한다. user Spot handler 는 `spot` 인스턴스와 `actor`
인스턴스를 함께 받기 때문이다. 즉 room 이나 game 상태를 바꿀 수 있는 자리다.

같은 user Spot 안의 여러 actor 가 같은 상태를 만질 수 있으므로 Spot 단위로
순서를 지켜야 한다. managed runtime 경로에서는 actor 별 순서 규칙을 거친 뒤
user Spot queue 로 들어갈 수 있다. 반면 native bound actor 경로에서는 user
Spot queue 가 반드시 필요한 직렬화 경계다.

이를 정리하면 `Node.js` runtime 의 실행 규칙은 아래와 같다.

| 입력 경로 | 실행 위치 |
| --- | --- |
| stream session → Entry/local actor | actor별로 순서를 보존한 뒤 현재 actor 위치로 dispatch |
| Entry Spot actor packet | Entry Spot 실행 queue |
| stream session → user Spot actor | user Spot 실행 queue |
| user Spot actor packet | user Spot 실행 queue |
| user Spot packet / timer / subscription | user Spot 실행 queue |
| Entry Spot initialize / closing / lifecycle callback | Entry Spot 실행 문맥 |

## 2.3 실행 직렬화 핵심 코드

이 절의 코드는 public API 계약이 아니다. 구현자가 실행 의미를 같은 방식으로
이해할 수 있도록 돕는 code-level 설계 기준이다. 실제 class 이름은 달라도
무방하다. 다만 queue 의 소유자와 completion 의미는 이 구조를 따라야 한다.

> Node.js 는 single-threaded event loop 이므로 .NET 의 `SemaphoreSlim` /
> `lock` 같은 OS-level 동기화 primitive 는 필요 없다. 그러나 **직렬화 경계의
> 의미는 동일하다.** 같은 queue 에 들어간 work item 은 입력 순서대로 한 번에
> 하나씩, 이전 work item 의 `Promise` 가 settle 된 뒤에 다음 work item 을
> 실행한다. 서로 다른 queue 인스턴스는 서로 독립적으로 진행한다. 아래 코드는
> 이 의미를 TypeScript 로 구체화한 참조 구현이며, 핵심은 class 이름이 아니라
> queue 소유자·completion 의미·event loop 분리(microtask 가 아닌 별도 task)다.

### 2.3.1 work item과 completion 의미

이 절은 실행 queue 에 들어가는 단위(`work item`)와 그 단위의 끝(`completion`)
이 무엇을 뜻하는지 정리한다.

실행 queue 에는 다음 두 가지를 함께 넣는다.

- 실행할 일(callback)
- 그 일이 끝났음을 알리는 completion

다만 모든 호출자가 이 completion 을 끝까지 기다리는 것은 아니다.

```ts
class ZLinkSerialWorkItem {
  private readonly callback: (signal: AbortSignal) => Promise<void>;
  private resolveCompletion!: () => void;
  private rejectCompletion!: (error: unknown) => void;
  readonly completion: Promise<void>;

  constructor(callback: (signal: AbortSignal) => Promise<void>) {
    this.callback = callback;
    this.completion = new Promise<void>((resolve, reject) => {
      this.resolveCompletion = resolve;
      this.rejectCompletion = reject;
    });
    // completion 을 기다리는 호출자가 없어도 unhandled rejection 으로 남지
    // 않도록 미리 관찰해 둔다(.NET 의 _ = _completion.Task.Exception 대응).
    this.completion.catch(() => {});
  }

  async invoke(
    onUnhandledException: (error: unknown) => void,
    signal: AbortSignal,
  ): Promise<void> {
    try {
      await this.callback(signal);
      this.resolveCompletion();
    } catch (error) {
      if (signal.aborted && isAbortError(error)) {
        this.rejectCompletion(error);
        return;
      }
      this.rejectCompletion(error);
      onUnhandledException(error);
    }
  }
}
```

completion 이 어떤 의미인지는 호출 종류에 따라 달라진다.

- send 나 fire-and-forget relay 는 target queue 에 work item 을 넣는 시점까지만
  기다리면 충분하다.
- request / reply relay 는 handler 가 reply 를 만들어 내거나 오류를 낼 때까지
  기다린다.
- lifecycle callback 은 runtime shutdown 이나 close 흐름에서 completion 을
  기다릴 수 있다.
- fire-and-forget handler 예외는 completion 을 기다리는 호출자가 없더라도
  runtime error sink 에 반드시 기록해야 한다.
- completion `Promise` 에 저장한 예외는 fire-and-forget 경로에서도
  unhandled promise rejection 으로 남지 않도록 관찰 처리해야 한다.
- 어떤 경우에도 transport callback 흐름에서 application handler 를 직접
  호출하지 않는다.

### 2.3.2 단일 실행 queue

이 절은 한 session 또는 한 actor 의 실행 줄을 어떻게 한 줄로 묶는지를 다룬다.

먼저 헷갈리기 쉬운 부분을 짚어 둔다. `draining` 플래그는 handler 하나하나를
감싸는 lock 이 아니다. 아래 코드의 `draining` 은 queue 를 비우는 drain loop
가 동시에 두 개 이상 실행되지 않도록 막는 용도다.

실제 실행 순서는 배열 `queue` 에 들어간 work item 의 입력 순서가 만든다.

```ts
class ZLinkSerialExecutionQueue {
  private readonly taskRunner: ZLinkRuntimeTaskRunner;
  private readonly errorSink: ZLinkRuntimeErrorSink;
  private readonly executionSignal: AbortSignal;
  private readonly queue: ZLinkSerialWorkItem[] = [];
  private draining = false;
  private completed = false;
  private pendingCount = 0;
  private resolveDrained!: () => void;
  private readonly drained: Promise<void>;

  constructor(
    taskRunner: ZLinkRuntimeTaskRunner,
    errorSink: ZLinkRuntimeErrorSink,
    executionSignal: AbortSignal,
  ) {
    this.taskRunner = taskRunner;
    this.errorSink = errorSink;
    this.executionSignal = executionSignal;
    this.drained = new Promise<void>((resolve) => {
      this.resolveDrained = resolve;
    });
  }

  post(
    callback: (signal: AbortSignal) => Promise<void>,
    signal?: AbortSignal,
  ): ZLinkSerialWorkItem {
    signal?.throwIfAborted();
    const item = new ZLinkSerialWorkItem(callback);
    this.pendingCount++;
    this.queue.push(item);
    this.scheduleDrain();
    return item;
  }

  tryPost(
    callback: (signal: AbortSignal) => Promise<void>,
  ): ZLinkSerialWorkItem | undefined {
    if (this.completed) {
      return undefined;
    }
    return this.post(callback);
  }

  async run(
    callback: (signal: AbortSignal) => Promise<void>,
    signal?: AbortSignal,
  ): Promise<void> {
    const item = this.post(callback, signal);
    await waitWithSignal(item.completion, signal);
  }

  async dispose(): Promise<void> {
    if (this.completed) {
      return;
    }
    this.completed = true;
    if (this.pendingCount === 0) {
      this.resolveDrained();
    }
    await this.drained;
  }

  private scheduleDrain(): void {
    this.taskRunner.runDetached('serial-queue-drain', () => this.drain());
  }

  private async drain(): Promise<void> {
    if (this.draining) {
      return;
    }
    this.draining = true;
    try {
      let item: ZLinkSerialWorkItem | undefined;
      while ((item = this.queue.shift()) !== undefined) {
        await item.invoke(
          (error) => this.reportHandlerException(error),
          this.executionSignal,
        );
        this.completePendingItem();
      }
    } finally {
      this.draining = false;
    }

    if (this.queue.length > 0) {
      this.scheduleDrain();
    }
  }

  private reportHandlerException(error: unknown): void {
    try {
      this.errorSink.reportHandlerException(error);
    } catch (reportError) {
      this.taskRunner.reportErrorSinkFailure(
        'handler-exception-report',
        reportError,
      );
    }
  }

  private completePendingItem(): void {
    this.pendingCount--;
    if (this.pendingCount === 0 && this.completed) {
      this.resolveDrained();
    }
  }
}
```

이 queue 의 동작은 다음과 같이 읽으면 된다.

- `post(...)` 는 work item 을 queue 에 넣고 drain task 를 깨운다.
- 이미 drain 중이라면 `draining` 플래그가 `true` 다. 그래서 새로 만들어진
  drain task 는 곧바로 끝난다.
- 진행 중이던 drain task 가 queue 를 계속 비우므로, handler 가 하나씩 차례로
  실행된다.
- 서로 다른 `ZLinkSerialExecutionQueue` 인스턴스는 서로 다른 실행 줄이다.
  따라서 인스턴스가 다르면 서로 독립적으로 진행한다.

`post(...)` 와 `run(...)` 에 넘긴 `signal` 은 queue 에 들어가기 이전 단계의
대기나 completion 대기를 취소하기 위한 값이다. 즉 이미 queue 에 들어간 work
item 을 중간에서 빼낸다는 뜻은 아니다.

handler 실행 자체를 멈추는 값은 따로 있다. runtime shutdown signal 이나
handler 가 별도로 받은 operation signal 로 분리해야 한다. 이렇게 분리해
두어야 request timeout 이 같은 queue 안에 줄 서 있는 다음 work item 의
순서를 깨지 않는다.

### 2.3.3 runtime task runner

이 절은 queue drain task 를 누가 만들고, 그 task 의 예외를 어떻게 처리할지
다룬다.

queue drain 은 transport callback 흐름에서 직접 실행하지 않는다. framework
runtime 은 분리된 task runner 를 통해 drain task 를 만든다. 이 runner 는
fire-and-forget task 의 예외를 반드시 관찰해 monitoring 이나 runtime error
sink 로 넘겨야 한다.

```ts
interface ZLinkRuntimeErrorSink {
  reportHandlerException(error: unknown): void;

  reportRuntimeTaskException(taskName: string, error: unknown): void;
}

class ZLinkRuntimeTaskRunner {
  private readonly errorSink: ZLinkRuntimeErrorSink;
  private readonly shutdownSignal: AbortSignal;

  constructor(errorSink: ZLinkRuntimeErrorSink, shutdownSignal: AbortSignal) {
    this.errorSink = errorSink;
    this.shutdownSignal = shutdownSignal;
  }

  runDetached(
    name: string,
    callback: (signal: AbortSignal) => Promise<void>,
  ): void {
    // microtask 가 아니라 새 macro task 로 떼어 내, transport callback 의
    // 호출 스택과 분리한다. queueMicrotask 대신 setImmediate 를 쓴다.
    setImmediate(() => {
      void this.runDetachedCore(name, callback);
    });
  }

  private async runDetachedCore(
    name: string,
    callback: (signal: AbortSignal) => Promise<void>,
  ): Promise<void> {
    try {
      await callback(this.shutdownSignal);
    } catch (error) {
      if (this.shutdownSignal.aborted && isAbortError(error)) {
        return;
      }
      try {
        this.errorSink.reportRuntimeTaskException(name, error);
      } catch {
        // error sink 자체의 실패는 삼킨다.
      }
    }
  }

  reportErrorSinkFailure(_name: string, _error: unknown): void {
    // 구현체가 마지막 fallback 으로 처리한다.
  }
}
```

이런 runner 를 따로 두는 이유는 아래와 같다.

- transport callback 은 queue 에 item 을 넣은 뒤 곧장 빠져나와야 한다.
- application handler 는 반드시 runtime 이 만든 분리된 task 안에서 실행되어야
  한다.
- fire-and-forget task 의 예외와 fire-and-forget handler 의 예외가 unhandled
  rejection 으로 남으면 안 된다.
- `setImmediate` 으로 떼어 내, NestJS request context 나 임의 async context 에
  묶이지 않도록 한다.

`setImmediate(...)` 대신 `queueMicrotask(...)` 를 쓰면 transport callback 의
microtask checkpoint 안에서 실행될 수 있으므로 쓰지 않는다. 중요한 것은 다음
두 가지다.

- task 생성 위치를 runtime 한곳에 모은다.
- 예외 관찰과 shutdown signal 처리를 동일한 규칙으로 적용한다.

### 2.3.4 stream session runtime

이 절은 stream transport 에서 들어온 frame 이 어떻게 session 별 실행 줄로
이어지는지를 다룬다.

stream transport 가 frame 을 읽으면, session runtime 은 packet 을 만든 뒤
session 별 queue 에 넣는다. 같은 session 에서 들어온 frame 순서는 stream
socket 이 이미 보존한다. 따라서 framework 는 그 순서를 session callback 순서로
이어 주면 된다.

```ts
class ZLinkStreamSessionRuntime {
  private readonly sessionQueue: ZLinkSerialExecutionQueue;
  private readonly session: ZLinkSession;
  private readonly context: ZLinkSessionContext;

  constructor(
    taskRunner: ZLinkRuntimeTaskRunner,
    errorSink: ZLinkRuntimeErrorSink,
    executionSignal: AbortSignal,
    session: ZLinkSession,
    context: ZLinkSessionContext,
  ) {
    this.sessionQueue = new ZLinkSerialExecutionQueue(
      taskRunner,
      errorSink,
      executionSignal,
    );
    this.session = session;
    this.context = context;
  }

  async onTransportConnected(signal?: AbortSignal): Promise<void> {
    this.sessionQueue.post(
      (s) => this.session.onConnected(this.context, s),
      signal,
    );
  }

  async onTransportFrame(
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<void> {
    this.sessionQueue.post(
      (s) => this.session.onDispatch(header, payload, s),
      signal,
    );
  }

  async onTransportDisconnected(signal?: AbortSignal): Promise<void> {
    this.sessionQueue.post(
      (s) => this.session.onDisconnected(this.context, s),
      signal,
    );
  }
}
```

위 코드에서 transport 진입점은 `post(...)` 가 work item 을 queue 에 넣는
시점까지만 책임진다. 즉 work item 을 queue 에 넣고 곧바로 돌아오며, handler 가
실제로 처리되는 것까지는 기다리지 않는다는 뜻이다. 이렇게 둔 이유는 두
가지다.

- 첫째, frame 이 들어오는 통로(frame ingress)와 connect / disconnect 같은
  lifecycle 신호가 들어오는 통로(lifecycle ingress)를, application 쪽 handler
  가 끝날 때까지 기다리도록 묶어 두지 않기 위해서다. handler 가 느려도
  transport 가 다음 입력을 계속 받을 수 있어야 한다.
- 둘째, 같은 session 의 callback 은 어차피 session queue 안에서 한 줄로
  실행되므로, 같은 session 안에서 두 callback 이 동시에 겹쳐 실행될 일은 없다.
  따라서 transport 단계에서 handler 의 완료를 다시 기다릴 필요가 없다.

요약하면, transport 진입점은 handler 의 reply 를 기다려서 응답으로 돌려 주는
public request / reply 경로가 아니다. 그저 work item 을 queue 에 안전하게
밀어 넣을 때까지만 책임지는 자리다.

다만 shutdown 처럼 "지금까지 들어온 모든 일이 끝날 때까지 반드시 기다려야"
하는 runtime 흐름은 따로 있다. 이 흐름은 별도의 drain / stop 단계에서, queue
에 쌓여 있던 work item 들이 모두 끝났는지(`completion`) 를 직접 관찰한다.

### 2.3.5 session에서 actor로 relay

이 절은 session 의 실행 줄 안에서 actor 의 실행 줄로 packet 을 어떻게
넘기는지를 다룬다.

session callback 안에서 actor 에게 packet 을 넘기는 public 표면은
bind 결과로 받은 `ZLinkSessionActor.relay(...)` 다. 이 handle method 는
session queue 를 actor 실행 queue 로 이어 주는 bridge 역할을 한다.

session callback 으로 들어온 `payload: Message` 는 framework runtime 이 소유한
수신 payload 를 callback 동안 빌려준 값이다. 따라서 `relay(...)`
는 caller payload 를 해제하거나 `move()` 로 소비하지 않는다. actor 실행 queue 로
수명이 넘어가야 하는 경우에는 framework 내부에서 별도 copy 또는 move 대상
message 를 만들어 소유권을 분리한다.

```ts
class ZLinkSessionActorImpl implements ZLinkSessionActor {
  constructor(private readonly context: ZLinkSessionContext) {}

  async relay(
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<void> {
    await this.context.relayActorRef(this, header, payload, signal);
  }
}
```

두 helper 는 끝나는 시점이 다르다.

- send 성격의 `relay(...)` 는 actor 의 실행 줄에 packet 을 넣는
  데 성공한 시점에 끝난다.
- 반면 request 성격의 actor relay 는 actor handler 가 만든
  reply 를 받을 때까지 기다린다.

두 경우 모두 같은 actor 로 들어간 packet 의 enqueue 순서는 그대로 보존해야
한다.

### 2.3.6 actor 위치 snapshot과 dispatch 결정

이 절은 actor 의 위치(Entry Spot 인지 user Spot 인지) 를 보고 어느 queue 로
보낼지 결정하는 단계를 다룬다.

actor dispatch runtime 은 actor 의 현재 위치를 본 뒤 handler 실행 위치를
고른다. 이때 다음 두 동작은 하나의 actor dispatch 결정으로 묶여야 한다.

- 위치 snapshot 을 읽는 동작
- queue 를 선택하는 동작

그래야 join 직후에 들어온 packet 이 이전 Entry Spot handler 쪽으로 빠지는 일이
없다. 구체적으로는, 같은 actor 로 들어온 앞 packet 이 자기 actor mailbox turn
안에서 join 을 끝낼 때까지 다음 packet 이 mailbox turn 을 받지 못한다. 다음
packet 은 turn 을 받은 뒤에야 위치 snapshot 을 다시 읽으므로, 새 user Spot 위치로
dispatch 된다.

```ts
class ZLinkActorDispatchRuntime {
  private readonly taskRunner: ZLinkRuntimeTaskRunner;
  private readonly errorSink: ZLinkRuntimeErrorSink;
  private readonly executionSignal: AbortSignal;

  private readonly entryActorQueues = new Map<string, ZLinkSerialExecutionQueue>();

  constructor(
    taskRunner: ZLinkRuntimeTaskRunner,
    errorSink: ZLinkRuntimeErrorSink,
    executionSignal: AbortSignal,
  ) {
    this.taskRunner = taskRunner;
    this.errorSink = errorSink;
    this.executionSignal = executionSignal;
  }

  async postFromSession(
    actorRef: ZLinkSessionActor,
    binding: ZLinkActorBoundSession,
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<void> {
    const item = this.createActorWorkItem<void>(
      actorRef,
      binding,
      header,
      payload,
      /* expectReply */ false,
    );

    await this.enqueueByCurrentLocation(actorRef, item, signal);
  }

  async invokeFromSession<TReply>(
    actorRef: ZLinkSessionActor,
    binding: ZLinkActorBoundSession,
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<TReply> {
    const item = this.createActorWorkItem<TReply>(
      actorRef,
      binding,
      header,
      payload,
      /* expectReply */ true,
    );

    const queued = await this.enqueueByCurrentLocation(actorRef, item, signal);

    await waitWithSignal(queued.completion, signal);
    return item.getReply();
  }

  private enqueueByCurrentLocation(
    actorRef: ZLinkSessionActor,
    item: ZLinkActorDispatchWorkItem,
    signal?: AbortSignal,
  ): Promise<ZLinkSerialWorkItem> {
    const location = actorRef.readCurrentLocation();

    if (location.isEntrySpot) {
      let actorQueue = this.entryActorQueues.get(actorRef.actorId);
      if (actorQueue === undefined) {
        actorQueue = new ZLinkSerialExecutionQueue(
          this.taskRunner,
          this.errorSink,
          this.executionSignal,
        );
        this.entryActorQueues.set(actorRef.actorId, actorQueue);
      }

      return Promise.resolve(
        actorQueue.post((s) => this.invokeEntrySpotHandler(item, s), signal),
      );
    }

    return Promise.resolve(
      location.userSpot.executionQueue.post(
        (s) => this.invokeUserSpotHandler(location.userSpot, item, s),
        signal,
      ),
    );
  }
}
```

> 위 참조 구현은 `entryActorQueues` 를 plain `Map` 으로 둔다. Node.js event
> loop 의 단일 실행 모델 덕분에 위치 snapshot 읽기와 queue 선택 사이에 다른
> dispatch 가 끼어들지 않는다. .NET 처럼 `ConcurrentDictionary` + lock 으로
> snapshot/select 를 묶어야 하는 race 가 event loop 단일 turn 안에서는
> 발생하지 않는다. 다만 같은 actor 로 들어온 packet 사이의 순서를 보장하는
> 실제 직렬화 경계는 actor mailbox turn(§2.3.6 문단 설명)이라는 점은 동일하다.

이 코드를 보면 Entry Spot 에는 actor 별 queue 만 둔다. 즉 Entry Spot 전체
queue 에 actor packet 을 넣지 않는다. 그래서 `actor A` 의 handler 가 아무리
오래 걸려도, `actor B` 의 Entry Spot packet 은 같은 actor queue 에 끌려
들어가지 않는다.

user Spot 은 그 반대다. actor 별 queue 로만 끝내 버리면 같은 room state 를
handler 두 개가 동시에 건드릴 수 있다. 그래서 actor 가 user Spot 에 있으면
최종 handler 호출은 반드시 `location.userSpot.executionQueue` 안에서 한다.

### 2.3.7 user Spot queue

이 절은 user Spot 의 실행 queue 가 어떤 입력을 한 줄로 묶는지를 다룬다.

user Spot queue 는 actor packet 만 처리하는 곳이 아니다. 다음 항목 모두 같은
queue 로 들어와야 한다.

- Spot packet
- timer
- subscription
- channel reply continuation

```ts
class ZLinkUserSpotRuntime {
  readonly executionQueue: ZLinkSerialExecutionQueue;

  constructor(
    taskRunner: ZLinkRuntimeTaskRunner,
    errorSink: ZLinkRuntimeErrorSink,
    executionSignal: AbortSignal,
  ) {
    this.executionQueue = new ZLinkSerialExecutionQueue(
      taskRunner,
      errorSink,
      executionSignal,
    );
  }

  enqueueSpotPacket(
    packet: ZLinkSpotPacket,
    signal?: AbortSignal,
  ): ZLinkSerialWorkItem {
    return this.executionQueue.post(
      (s) => this.invokeSpotPacketHandler(packet, s),
      signal,
    );
  }

  enqueueActorPacket(
    item: ZLinkActorDispatchWorkItem,
    signal?: AbortSignal,
  ): ZLinkSerialWorkItem {
    return this.executionQueue.post(
      (s) => this.invokeUserSpotActorHandler(item, s),
      signal,
    );
  }

  enqueueTimerTick(
    tick: ZLinkTimerTick,
    signal?: AbortSignal,
  ): ZLinkSerialWorkItem {
    return this.executionQueue.post(
      (s) => this.invokeTimerHandler(tick, s),
      signal,
    );
  }

  enqueueChannelReply(
    reply: ZLinkChannelReply,
    signal?: AbortSignal,
  ): ZLinkSerialWorkItem {
    return this.executionQueue.post(
      (s) => this.completeChannelRequest(reply, s),
      signal,
    );
  }
}
```

이 queue 하나가 user Spot 의 mutable state 를 지킨다. `Spot` 객체의 필드,
room board, stage actor count 같은 값은 모두 이 queue 안에서만 읽고 쓴다는
것을 전제로 한다.

### 2.3.8 Entry Spot lifecycle queue

이 절은 Entry Spot 의 lifecycle 신호가 actor packet 과 다른 줄에서 처리되는
이유와 그 줄을 어떻게 묶는지를 다룬다.

Entry Spot lifecycle 은 actor packet 과 분리해 둔다. 초기화, 종료, joined /
left callback 처럼 Entry Spot registry 나 lifecycle state 를 만지는 작업은
Entry Spot lifecycle 실행 문맥에서 직렬화할 수 있다.

```ts
class ZLinkEntrySpotRuntime {
  private readonly lifecycleQueue: ZLinkSerialExecutionQueue;
  private readonly entrySpot: ZLinkEntrySpot;

  constructor(
    taskRunner: ZLinkRuntimeTaskRunner,
    errorSink: ZLinkRuntimeErrorSink,
    executionSignal: AbortSignal,
    entrySpot: ZLinkEntrySpot,
  ) {
    this.lifecycleQueue = new ZLinkSerialExecutionQueue(
      taskRunner,
      errorSink,
      executionSignal,
    );
    this.entrySpot = entrySpot;
  }

  initialize(signal?: AbortSignal): Promise<void> {
    return this.lifecycleQueue.run((s) => this.entrySpot.onInitialize(s), signal);
  }

  closing(signal?: AbortSignal): Promise<void> {
    return this.lifecycleQueue.run((s) => this.entrySpot.onClosing(s), signal);
  }

  actorJoined(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    return this.lifecycleQueue.run(
      (s) => this.entrySpot.onJoinedActor?.(actor, s),
      signal,
    );
  }

  actorLeft(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    return this.lifecycleQueue.run(
      (s) => this.entrySpot.onLeaveActor?.(actor, s),
      signal,
    );
  }
}
```

이 queue 에는 Entry Spot actor packet 을 넣지 않는다. Entry Spot actor packet
은 `ZLinkActorDispatchRuntime` 의 actor 별 queue 를 사용한다.

Entry Spot timer 도 이 lifecycle queue 에 넣지 않는다. Entry Spot 은 여러 actor 가
처음 거쳐 가는 공용 입구이므로, 긴 timer callback 이 관계없는 Entry Spot packet
이나 actor packet 을 전역으로 막으면 안 된다. 다만 같은 timer instance 안에서는
이전 callback 이 끝나기 전에 다음 callback 을 겹쳐 실행하지 않는다.

### 2.3.9 독립 node message task

이 절은 어떤 Spot 이나 actor 에도 묶이지 않는 node 단위 message 를 어떻게
실행하는지 다룬다.

특정 Spot 이나 actor 의 상태를 보호할 필요가 없는 node-level message 는
message 하나를 runtime task 하나로 실행한다. 이 경로에는 전역 node queue 를
두지 않는다. 전역 queue 를 두면 서로 무관한 node message 들이 한 줄로 묶여
막혀 버리기 때문이다.

```ts
class ZLinkNodeMessageRuntime {
  constructor(
    private readonly taskRunner: ZLinkRuntimeTaskRunner,
    private readonly invoker: ZLinkNodeMessageHandlerInvoker,
  ) {}

  onNodeMessage(message: ZLinkNodeMessage): void {
    this.taskRunner.runDetached('node-message', (s) =>
      this.invoker.invoke(message, s),
    );
  }
}
```

node message handler 가 actor 상태나 user Spot 상태를 바꿔야 한다면, handler
안에서 직접 상태를 만지면 안 된다. 그 대신 actor dispatch runtime 이나 user
Spot queue 로 다시 넘겨야 한다.

정리하면, node message task 는 독립 작업을 실행하는 단위일 뿐이며 공유 상태를
보호하는 단위는 아니다.

### 2.3.10 전체 흐름

이 절은 앞에서 본 여러 queue 들이 실제 흐름에서 어떻게 이어지는지를 한 장의
다이어그램으로 보여 준다.

```text
Stream frame
  -> Session queue
  -> Session callback
  -> Actor dispatch runtime
       Entry Spot actor -> Actor queue
       User Spot actor  -> User Spot queue

Entry Spot lifecycle
  -> Entry Spot lifecycle queue

Entry Spot timer
  -> Independent runtime task

Node message
  -> Independent runtime task

User Spot packet, timer, subscription, channel reply
  -> User Spot queue
```

session queue 는 stream callback 을 직렬화하기 위한 장치다. actor 의 실행
순서를 최종적으로 결정하는 곳은 actor dispatch runtime 이다.

이 둘을 섞어 버리면 다음 두 가지 문제가 생긴다.

- Entry Spot actor packet 이 전역으로 직렬화된다.
- user Spot 상태가 서로 다른 actor handler 에서 동시에 변경된다.

## 3. Actor handler 표면

이 절은 actor 쪽에서 application 이 구현하게 되는 handler 시그니처와, handler
가 받는 metadata 의 모양을 정리한다.

### 3.1 typed handler 시그니처

```ts
interface ZLinkEntrySpotActorRequestHandler<
  TEntrySpot,
  TActor extends ZLinkActor,
  TRequest,
  TReply,
> {
  handle(
    entrySpot: TEntrySpot,
    actor: TActor,
    context: ZLinkSpotActorRequestContext,
    request: TRequest,
  ): Promise<TReply>;
}

interface ZLinkSpotActorRequestHandler<
  TSpot,
  TActor extends ZLinkActor,
  TRequest,
  TReply,
> {
  handle(
    spot: TSpot,
    actor: TActor,
    context: ZLinkSpotActorRequestContext,
    request: TRequest,
  ): Promise<TReply>;
}

interface ZLinkEntrySpotActorSendHandler<
  TEntrySpot,
  TActor extends ZLinkActor,
  TMessage,
> {
  handle(
    entrySpot: TEntrySpot,
    actor: TActor,
    context: ZLinkSpotActorSendContext,
    message: TMessage,
  ): Promise<void>;
}

interface ZLinkSpotActorSendHandler<
  TSpot,
  TActor extends ZLinkActor,
  TMessage,
> {
  handle(
    spot: TSpot,
    actor: TActor,
    context: ZLinkSpotActorSendContext,
    message: TMessage,
  ): Promise<void>;
}
```

handler 는 transport raw header 를 직접 받지 않는다. Session route 와 binding
token 같은 내부 값은 framework runtime 이 들고 있고 public handler 에 노출하지
않는다. stream packet 이름과 전달 허용된 application metadata 는
`ZLinkSpotActorSendContext` / `ZLinkSpotActorRequestContext` 로 전달된다.

typed actor context 는 source session 의 routing id 를 노출하지 않는다.
handler 가 즉시 자기 client 로 push 를 보내야 하는 경우에도 마찬가지다. 이때도
handler 가 받은 actor 의 `context.boundSession.send(message)` 처럼 현재 actor 에
묶인 표면을 사용해야 한다.

request handler 의 응답 payload 는 handler 의 반환값이다. 다만 응답 stream
header 에 application metadata 를 추가하거나 payload compression 을 켜야 하는
경우에는 `ZLinkSpotActorRequestContext.reply` 를 사용한다.

```ts
async handle(
  spot: GameSpot,
  actor: PlayerActor,
  context: ZLinkSpotActorRequestContext,
  request: JoinMatchReq,
): Promise<JoinMatchRes> {
  context.reply
    .metadata('trace-id', 'reply-trace')
    .compress();

  return { matchId: request.matchId };
}
```

`context.reply` 는 응답을 직접 보내는 API 가 아니다. handler 가 반환한 값을
framework 가 response frame 으로 만들 때 사용할 header/body encoding 옵션만
기록한다.

### 3.2 metadata snapshot

이 절은 application metadata 를 어디까지 actor 쪽에 흘려 보낼지 정하는 표면을
다룬다.

```ts
class ZLinkMessageMetadata {
  static readonly empty: ZLinkMessageMetadata;

  readonly values: ReadonlyMap<string, string>;

  find(key: string): string | undefined;
}

interface ZLinkMessageMetadataPolicy {
  canForward(key: string): boolean;
}
```

기본 `ZLinkMessageMetadataPolicy` 는 client stream metadata 를 actor handler
쪽으로 전달하지 않는다. trace id 같은 값을 actor handler 까지 함께 흘려
보내려면, framework 등록 단계에서 명시적으로 허용해야 한다. 허용된 값은
`ZLinkMessageMetadata.values` 에 저장되며, 사용자 코드는 `find(key)` 가
`undefined` 를 반환하면 해당 key 가 없다고 보면 된다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .options({ metadata: { forward: ['trace-id', 'tenant-id'] } })
    .build()
);
```

## 4. BoundSession 호출 표면

이 절은 actor 가 client session 쪽으로 push 를 보내거나 client stream 을 닫을 때 사용하는
public 표면을 정리한다.

```ts
interface ZLinkBoundSession {
  send<TMessage>(message: TMessage): ZLinkBoundSessionSendCall;

  disconnect(signal?: AbortSignal): Promise<void>;
}

interface ZLinkBoundSessionSendCall {
  packetName(packetName: string): ZLinkBoundSessionSendCall;

  metadata(key: string, value: string): ZLinkBoundSessionSendCall;

  submit(signal?: AbortSignal): Promise<void>;
}
```

호출 모양은 아래와 같다.

```ts
await context.boundSession
  .send(new GameStateChangedMsg(gameId, board))
  .submit(signal);
```

기존에 사용하던 `SessionGateway` 라는 이름은 새 public API 에서 제거한다.
이름이 두 갈래로 정리된다.

- session → actor 방향: `bind(...)`, `ZLinkSessionActor.relay(...)` 를 사용한다.
  disconnect 알림과 binding cleanup 은 session lifecycle 경로에서 처리한다.
- actor → 자기 client 방향: `ZLinkBoundSession` 를 사용한다.
- 다른 actor 의 client session 에 보내야 하는 application service 는 먼저 대상
  actor 로 메시지를 보내고, 대상 actor handler 가 자기 `boundSession` 을 사용한다.

`ZLinkBoundSession.send(...).submit(...)` 은 one-way push 다. 이 호출은
framework route send 제출이 끝났다는 의미일 뿐이다. 즉 client application
handler 가 메시지를 처리 완료했다는 ack 는 아니다.

`ZLinkBoundSession.disconnect(...)` 는 actor 가 현재 actor id 에 묶인
client stream 을 끊어야 한다고 판단했을 때 호출한다. 이 close 는
application 이 의도한 동작이므로 session 의 `onDisconnected(...)` callback
을 다시 올리지 않는다. framework 는 stream close 와 actor-session binding
정리만 수행한다.

stale binding token, 이미 닫힌 stream, 늦게 도착한 push 는 해당 push 하나만
실패해야 한다. 즉 route receive loop 나 host shutdown 자체를 실패시켜서는 안
된다.

client 처리 완료 ack 가 계약상 필요하면, actor message 나 session message 로
별도의 application-level reply 를 설계해야 한다. `boundSession` 자체는 request / reply
표면을 제공하지 않는다.

재접속은 actor id 기준으로 idempotent 해야 한다. 같은 actor id 가 새 stream
session 에서 `bind(...)` 로 다시 들어오면, framework 는 다음과
같이 동작한다.

- 기존 actor instance 와 spot membership 은 그대로 유지한다.
- session binding 만 새 stream 으로 옮긴다.

이 규칙이 있어야 client reconnect 가 "새 게임에 참여"가 아니라 "기존 actor 의
새 연결"로 동작한다.

## 5. Session에서 actor로 relay

session actor dispatch 에서 session 은 actor runtime 을 직접 호출하는 범용
public client 를 사용하지 않는다. client stream 에서 받은 packet 은
`ZLinkSession.onDispatch(...)` 로 올라오고, session 구현은 actor handle 을
만든 뒤 `ZLinkSessionActor.relay(...)` 로 전달한다.

```ts
interface ZLinkSessionActors {
  readonly bound: ReadonlyArray<ZLinkSessionActor>;

  bind(actor: ZLinkActor, signal?: AbortSignal): Promise<ZLinkSessionActor>;

  bind(actor: ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;

  find(actorId: string): ZLinkSessionActor | undefined;
}
```

이 표면만 남기면 session 코드의 의도가 분명해진다. session 은 "받은 client
packet 을 어떤 actor 에 relay 할지"만 결정한다. 같은 process 의 actor 는 actor id
overload 로 bind 할 수 있고, 다른 process 의 actor 는 `joinSpot(...)` /
`joinEntrySpot(..., request)` 결과의 `actor` 를 넘기는 overload 로 bind 한다.
한 session 이 여러 actor 를 bind 할 수 있으므로 이미 bind 한 actor handle 이 필요하면
`bound` 로 현재 binding snapshot 을 보거나 `find(actorId)` 로
actor id 기준 조회를 한다. session 은 actor handle 목록을 별도 application 상태로
복제하지 않는다.

packet relay 와 disconnect notification 은 bind 결과로 받은 `ZLinkSessionActor` 가
수행한다. 즉 session code 는 대상 handle 을 고른 뒤 `actor.relay(...)` 또는
`actor.notifyDisconnected(...)` 를 호출한다.

session disconnect 는 bound actor 전체에 자동 전파되지 않는다. 연결이 끊겼을 때
어떤 actor 에게 알려야 하는지는 application 이 판단한다. 알림이 필요한 경우
`onDisconnected(...)` 안에서 `find(...)` 또는 `bound` 로
대상을 고른 뒤 `actor.notifyDisconnected(...)` 를 호출한다. 이 호출은
actor 의 현재 Spot 실행 문맥에서 disconnected handler 를 실행할 뿐이며, actor 를
room 에서 leave 시키지 않는다.

## 6. Spot remote address resolver 등록

이 절은 actor 가 Spot 으로 join 하거나 Spot client 를 사용할 때 필요한 resolver 표면을 정리한다.

- session 에 이미 attach 된 actor 로 relay 할 때는 actor remote address resolver 를
  사용하지 않는다. session 은 framework 가 만든 actor handle 을 저장한다.
- actor 가 현재 연결된 client session 으로 push 를 보낼 때는,
  framework / core 가 가진 actor-session binding 상태를 사용한다.
- actor 가 `joinSpot(spotRid, ...)` 로 user Spot 에 들어가는 경로가 node
  경계를 넘을 수 있다면, spot remote address resolver 도 함께 등록한다.

```ts
interface ZLinkSpotRemoteAddressResolver {
  resolveSpotRemoteAddress(
    spotRid: RoutingId,
    signal?: AbortSignal,
  ): Promise<ZLinkSpotRemoteAddress>;
}

enum ZLinkSpotKind {
  Invalid = 0,
  Entry = 1,
  User = 2,
}

interface ZLinkSpotRemoteAddress {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind: ZLinkSpotKind;
}
```

Module 등록 (Session 서버):

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .useDiscovery()
      .addRegistryEndpoint('tcp://registry1:5551')
    .addRouteMeshChannel('game.rooms')
    .options({ registrySpotRemoteAddresses: { namespace: 'game' } })
    .build()
);
```

Module 등록 (Play 서버):

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .actorFactory('player', PlayerActorFactory)
    .options({ registrySpotRemoteAddresses: { namespace: 'game' } })
    .build()
);
```

### 6.1 Actor-session binding 상태

이 절은 actor 와 stream session 의 연결 정보를 누가 들고 있는지를 정리한다.

framework 는 session route resolver 를 public 기본 표면으로 제공하지 않는다.
session binding 은 다음 흐름에서 framework / core 가 갱신해 두는 내부
상태이기 때문이다.

- actor handle 생성
- stream attach
- stream disconnect

actor-session route 는 session bind 시 actor runtime state 에 저장된다. value 에는
session rid 와 binding token 이 들어간다. unbind 는 binding token 을 확인한 뒤
수행하므로 이전 stream 의 늦은 unbind 가 새 binding 을 지우지 못한다. 별도의 public
session route resolver 나 저장소 계약은 두지 않는다.

## 7. 등록 표면 (host 측)

이 절은 host 가 framework 를 띄울 때 작성하는 등록 코드 모양을 보여 준다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .useDiscovery()
      .addRegistryEndpoint(registryEndpoint)
    .addRouteMeshChannel('backend')
      .enableRouter(playEndpoint)
    .actorFactory('player', TicTacToeActorFactory)
    .build()
);

@Injectable()
export class TicTacToeActor implements ZLinkActor {
  constructor(
    readonly actorId: string,
    readonly context: ZLinkActorContext,
  ) {}
}
```

spot handler는 spot 객체 안에서 등록한다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .useDiscovery()
      .addRegistryEndpoint(registryEndpoint)
    .addSpotNode('play')
      .enableRouter(spotEndpoint)
      .addEntrySpot(TicTacToeEntrySpot)
      .addSpotFactory(TicTacToeGame)
    .build()
);

@Injectable()
export class TicTacToeEntrySpot implements ZLinkEntrySpot {
  readonly context!: ZLinkEntrySpotContext;

  configure(): void {
    this.context.addHandler(JoinMatchEntryHandler);
    this.context.addHandler(TicTacToeEntryJoinedHandler);
    this.context.addHandler(TicTacToeEntryLeftHandler);
  }
}

@Injectable()
export class TicTacToeGame implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;

  async onActorJoin(actor: ZLinkActor, request: Message): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true };
  }

  configure(): void {
    this.context.addHandler(PlaceMarkHandler);
    this.context.addHandler(MoveHandler);
  }
}
```

## 8. 직접 dispatch 예시 (session callback)

이 절은 session callback 한곳에서 어떤 결정을 내리고, 어디서부터 framework
helper 를 부르는지 그림을 잡아 둔다.

session callback 이 직접 결정하는 것은 다음과 같다.

- 인증
- actor type 선택
- local actor handle 생성
- dispatch 여부

framework helper 는 actor-session binding 과 transport 세부 작업만 가려 준다.

```ts
@Injectable()
export class TicTacToeSession implements ZLinkSession {
  constructor(readonly context: ZLinkSessionContext) {}

  async onDispatch(
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<void> {
    if (header.name === 'auth') {
      const request = payload.decode<AuthReq>();

      const actor = await this.context.actors.bind(request.actorId, signal);

      this.authenticatedActors.remember(request.actorId, actor);

      await this.context.client.reply(new AuthRep(true)).submit(signal);
      return;
    }

    const actor = this.authenticatedActors.tryGet(header);
    if (actor !== undefined) {
      await actor.relay(header, payload, signal);
      return;
    }

    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorRouteNotFound,
      'No actor route is bound to this session packet.',
    );
  }

  async onConnected(): Promise<void> {}

  async onDisconnected(): Promise<void> {
    this.authenticatedActors.clear();
  }

  async onError(_error: ZLinkStreamError): Promise<void> {}
}

@Injectable()
export class JoinMatchHandler {
  @ZLinkSpotActorRequest()
  async handle(
    entrySpot: PlayerEntrySpot,
    actor: PlayerActor,
    request: JoinMatchReq,
  ): Promise<JoinMatchRes> {
    void entrySpot;
    // request.matchId는 application 도메인이 정한 match id다.
    // application registry가 user Spot routing id로 변환하거나 조회한다.
    const matchSpotRid = RoutingId.from(request.matchId);
    const joined = await actor.context
      .joinSpot(matchSpotRid, request)
      .submit<JoinMatchSpotResult>();
    return joined.reply.toReply();
  }
}

@Injectable()
export class PlaceMarkHandler {
  @ZLinkSpotActorRequest()
  async handle(
    spot: TicTacToeGameSpot,
    actor: PlayerActor,
    request: PlaceMarkReq,
  ): Promise<PlaceMarkRes> {
    const room = spot;
    return room.placeMark(actor.actorId, request.cell);
  }
}
```

## 9. Error 표현 (`Node.js` exception)

이 절은 framework 가 던지는 오류가 `Node.js` 표면에서 어떤 모양으로 보이는지를
정리한다.

public `Node.js` API 에서는 framework error 를 하나의 exception family 로
모은다.

```ts
class ZLinkFrameworkException extends Error {
  readonly kind: ZLinkFrameworkErrorKind;
  readonly isRetriable: boolean;

  constructor(kind: ZLinkFrameworkErrorKind, message: string) {
    super(message);
    this.kind = kind;
    this.isRetriable = false;
  }
}

enum ZLinkFrameworkErrorKind {
  ActorRouteNotFound,
  ActorCreateFailed,
  ActorAlreadyExists,
  ActorTypeMismatch,
  SpotCreateFailed,
  SpotRouteNotFound,
  SpotTypeMismatch,
  ActorSessionNotBound,
  HandlerNotFound,
  RouteHandlerNotFound,
  ActorDispatchHandlerNotFound,
  PayloadDecodeFailed,
  RouteNotConnected,
  RequestTargetNotFound,
  RequestRejected,
  RequestProtocolError,
  RequestFailed,
}
```

각 kind 의 발생 조건과 cross-binding 의미는
[policy/session-gateway-usability.ko.md](../../../../doc/spec/session-actor-dispatch.ko.md)
§17 error-kind 매트릭스에서 다룬다.

`ActorCreateFailed`, `ActorAlreadyExists`, `ActorTypeMismatch` 는
`ZLinkActorManager` 로 local actor 를 준비할 때 사용한다.
`SpotCreateFailed`, `SpotRouteNotFound`, `SpotTypeMismatch` 는 `ZLinkSpotManager` 와
registry 기반 spot route 조회에서 사용한다.
`bind(...)` 와 routed actor dispatch 수신 경로는 actor 를
생성하지 않는다. bind 는 logical actor handle 을 core ActorGateway binding 으로 넘기며,
actor remote address resolver 를 fallback 으로 호출하지 않는다. actor 를 찾을 수 없거나
gateway 경로로 relay 할 수 없으면 `ActorRouteNotFound` 로 분류한다. 현재 actor 에
bound 된 session 이 없어서 client push 를 보낼 수 없으면 `ActorSessionNotBound` 로
분류한다.
handler 를 찾지 못했거나 payload decode 에 실패한 inbound request 는
`HandlerNotFound`, `RouteHandlerNotFound`, `ActorDispatchHandlerNotFound`,
`PayloadDecodeFailed` 로 분류한다. route/request 하부에서 반환되는 실패는
`RouteNotConnected`, `RequestTargetNotFound`, `RequestRejected`,
`RequestProtocolError`, `RequestFailed` 로 매핑한다.

`isRetriable` 은 framework 가 자동으로 retry 해 준다는 의미가 아니다. caller
가 retry policy 를 만들 때 참고할 수 있는 분류일 뿐이다. sample 코드에서도 이
값을 이용해 retry loop 를 만들지 않는다.

## 10. Diagnostic helper

이 절은 `Node.js` 사용자가 connection 이나 topology[^topology] 상태를 점검할 때
쓸 수 있는 helper 문서다. session actor dispatch 의 필수 API 는 아니며,
운영 점검용으로만 둔다.

```ts
interface ZLinkTopologyDiagnostics {
  getRoutedChannel(
    routerChannelId: string,
    signal?: AbortSignal,
  ): Promise<ZLinkRoutedChannelSnapshot>;
}
```

retry helper 와는 성격이 다르다. diagnostic helper 가 보여 주는 것은 다음
세 가지로 한정된다.

- registry view
- discovery member
- local routed channel state

## 11. 다른 문서와의 관계

이 절은 같은 주제를 다른 각도에서 다루는 문서들을 한 자리에 모아 둔다.

- cross-binding 정책, 의미, 회귀 테스트, POSD 결론 →
  [policy/session-gateway-usability.ko.md](../../../../doc/spec/session-actor-dispatch.ko.md)
- 인터페이스 전체 정의 → [handler-interfaces.ko.md](./handler-interfaces.ko.md)
  §4.4 (session), §5.5 (session relay), §5.6 (`ZLinkBoundSession`), §5.7
  (actor remote address resolver)
- actor 라이프사이클과 actor handler 모델 →
  [nestjs-actor.ko.md](./nestjs-actor.ko.md)
- TicTacToe sample contract →
  [tictactoe-game-sample.ko.md](../../../dotnet/doc/guide/samples/tictactoe-game-sample.ko.md)
- STREAM session 라이프사이클 →
  [nestjs-stream.ko.md](./nestjs-stream.ko.md)

## 12. 회귀 테스트

이 절은 session actor dispatch 표면을 지키기 위해 어느 통합 테스트를 함께
유지해야 하는지를 정리한다.

session actor dispatch 항목은 다음 요소가 하나의 흐름으로 맞물려 동작하는지를
확인한다.

- stream session
- actor factory
- logical actor binding
- actor-session binding

또한 이전 stream 에서 늦게 도착한 disconnect 가 현재 actor-session 연결을
끊지 않는지도 함께 확인한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RemoteSessionRelayTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | session callback에서 actor request를 relay하고, request sequence를 통해 reply를 되돌린다. |
| `ActorDisconnectNotifyTests.ClientClose_Cleans_Session_Without_Actor_Disconnect_Callback` | client stream close 는 session binding cleanup 만 수행하고 Actor disconnect callback 을 호출하지 않는다. |
| `ActorBindingTests.BindActorAsync_DoesNot_Create_LocalActor` | logical actor binding 은 session attach 중 local actor 를 새로 만들지 않는다. |
| `ActorBindingTests.SessionActorBind_WithoutRoute_Is_LocalOnly` | route 없는 bind overload 는 local actor 에만 붙고 remote fallback 을 수행하지 않는다. |
| `RemoteProxyDisconnectTests.BoundSessionDisconnect_FromRemoteActor_Closes_Client_Without_Session_Disconnect_Callback` | remote actor 가 `boundSession.disconnect(...)` 를 호출해도 session host 에서 같은 close 의미가 유지된다. |
| `entry spot callbacks from mixed setImmediate/queueMicrotask backend callbacks keep enqueue order without overlap` | backend callback 이 서로 다른 task 문맥에서 도착해도 Entry Spot 실행 줄에서 등록 순서대로 겹치지 않고 실행된다. |
| `entry spot does not start the next callback before the previous handler promise settles` | handler Promise 가 끝나기 전에는 같은 Entry Spot 의 다음 callback 이 시작되지 않는다. |
| `entry spot timer ticks and actor packets share one serial line` | Entry Spot timer 와 actor packet 이 같은 Entry Spot 실행 줄을 사용한다. |
| `LocalActorMailboxExecutionTests.LocalActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors` | user Spot에 들어가지 않은 actor packet도 actor별 순서를 지키되 서로 다른 actor 사이에서는 병렬로 실행될 수 있다. |
| `ActorRegistryExecutionTests.ActorDispatch_Rechecks_CurrentLocation_After_Waiting_For_ActorMailbox` | 같은 actor의 앞 packet이 join을 마치고 나면, 대기 중이던 다음 packet이 새 user Spot 위치로 dispatch된다. |
| `ActorLifecycleTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | actor join 이후의 dispatch가 현재 spot 실행 문맥에서 실행된다. |
| `ActorSessionStateTests.ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream` | 이전 stream의 늦은 disconnect가 현재 actor-session 연결을 끊지 않는다. |
| `HeaderStreamSessionTests.HeaderStreamSession_Can_Close_Current_Client_Stream` | session context가 현재 client stream을 닫고 disconnect callback으로 자연스럽게 이어진다. |
| `SerialExecutorTests.StreamSessionSerialExecutor_Continues_After_Work_Exception` | session queue의 fire-and-forget work 예외가 error sink에 기록되고, 다음 work 실행을 막지 않는다. |
| `SerialExecutorTests.SpotSerialExecutor_Continues_After_Queued_Work_Exception` | Spot queue의 fire-and-forget work 예외가 error sink에 기록되고, 다음 work 실행을 막지 않는다. |
| `runtime task runner observes detached task exceptions without unhandled rejection` | Node runtime task runner 가 detached task 예외를 관찰하고 unhandled rejection 을 만들지 않는다. |
| `framework runtime state aborts listener tasks before disposing backend context` | runtime state shutdown 이 listener task 에 stop signal 을 먼저 전달하고 backend context 를 마지막에 정리한다. |
| `SerialExecutorTests.SpotSerialExecutor_ExecuteAsync_Propagates_Work_Exception` | Spot queue에서 완료를 기다리는 실행 경로는 handler 예외를 호출자에게 그대로 돌려준다. |
| `SerialExecutorTests.SerialExecutionQueue_RunAsync_Propagates_Work_Exception` | 공통 serial queue의 `run(...)`가 work 예외를 error sink에 기록하면서 호출자에게도 전파한다. |
| `SerialExecutorTests.SerialExecutionQueue_Wait_Cancellation_Does_Not_Remove_Queued_Work` | 공통 serial queue에서 completion wait가 취소되더라도 이미 queue에 들어간 work item은 제거되지 않는다. |
| `SerialExecutorTests.ActorDispatchCancellation_Does_Not_Stop_Current_Or_Later_Dispatch` | actor dispatch 대기를 취소해도 현재 실행 중인 dispatch나 이후 dispatch가 중단되지 않는다. |
| `RegressionTests.NodeSessionActorDispatch_Documents_ExecutionSerialization_Core_Code` | 실행 직렬화 핵심 코드 섹션이 queue, runtime task, error sink, cancellation 의미를 계속 설명한다. |
| `RegressionTests.NodeRegressionMatrix_Includes_ExecutionSerialization_Guards` | 중앙 regression matrix가 실행 직렬화 관련 회귀 항목을 유지한다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어, 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^session-actor-dispatch]: session actor dispatch는 클라이언트 세션으로 들어온 요청을 그 세션과 묶여 있는 actor로 자동 전달해 주는 패턴이다.
[^cross-binding]: cross-binding은 `Node.js`, `.NET`, Java, C++ 등 서로 다른 언어 바인딩에 같은 의미가 동일하게 적용되어야 함을 가리키는 정책 축이다.
[^posd]: POSD(Point Of Significant Decision)는 의미 있는 설계 결정이 내려진 지점을 기록해 두는 표기 방식이다.
[^actor-session-binding]: actor-session binding은 특정 actor가 현재 어떤 client stream session에 연결되어 있는지를 framework/core가 보관하는 상태다.
[^entry-spot]: Entry Spot은 모든 actor가 처음 거치는 공용 입구 역할의 Spot이다. user Spot으로 옮겨 가기 전까지 actor가 머무는 위치다.
[^user-spot]: user Spot은 room이나 game처럼 application 도메인이 정의한 Spot으로, 같은 Spot 안의 actor들이 공유 상태를 두고 상호작용하는 곳이다.
[^topology]: topology는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 표현하는 구성 정보다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework NestJS Actor](./nestjs-actor.ko.md) | [다음: ZLink Framework NestJS Monitoring](./nestjs-monitoring.ko.md)
<!-- framework-adapter-nav:bottom:end -->
