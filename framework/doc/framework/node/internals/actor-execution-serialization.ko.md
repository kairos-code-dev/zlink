# Actor 실행 직렬화 — 참조 구현 (internals)

[Node 묶음](../README.ko.md)

> **이 문서는 public 계약이 아니다.** 구현자가 실행 의미를 같은 방식으로 이해하도록 돕는
> code-level 설계 기준이다. 실제 class 이름은 달라도 무방하다.
>
> 계약의 정본은
> [session-actor-dispatch 공통 스펙](../../spec/server/31-session-actor-dispatch.ko.md)과
> [stage-wrapper-on-spot §3](../../spec/server/25-stage-wrapper-on-spot.ko.md)이 소유한다.
> **queue의 소유자와 completion 의미는 그 계약을 따라야 한다.**

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
    // completion을 기다리는 호출자가 없어도 unhandled rejection으로 남지 않게
    // rejection을 미리 관찰한다.
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
      () => this.session.onConnected(this.context),
      signal,
    );
  }

  async onTransportFrame(
    dispatch: ZLinkSessionDispatchContext,
    payload: Message,
    signal?: AbortSignal,
  ): Promise<void> {
    this.sessionQueue.post(
      () => this.session.onDispatch(dispatch, payload),
      signal,
    );
  }

  async onTransportDisconnected(signal?: AbortSignal): Promise<void> {
    this.sessionQueue.post(
      () => this.session.onDisconnected(this.context),
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

session callback 으로 들어온 `payload: ZLinkMessage` 는 framework runtime 이
codec registry와 함께 감싼 값이다. session은 인증 packet처럼 직접 처리할 payload만
decode하고, actor로 넘길 packet은 decode하지 않은 채 `relay(...)`에 넘긴다.

```ts
class ZLinkSessionActorImpl implements ZLinkSessionActor {
  constructor(private readonly context: ZLinkSessionContext) {}

  async relay(
    payload: ZLinkMessage,
    signal?: AbortSignal,
  ): Promise<void> {
    await this.context.relayActorRef(this, payload, signal);
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
    dispatch: ZLinkSessionDispatchContext,
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
    dispatch: ZLinkSessionDispatchContext,
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
    return this.lifecycleQueue.run(() => this.entrySpot.onInitialize(), signal);
  }

  closing(signal?: AbortSignal): Promise<void> {
    return this.lifecycleQueue.run(() => this.entrySpot.onClosing(), signal);
  }

  actorJoined(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    return this.lifecycleQueue.run(
      () => this.entrySpot.onJoinedActor?.(actor),
      signal,
    );
  }

  actorLeft(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    return this.lifecycleQueue.run(
      () => this.entrySpot.onLeaveActor?.(actor),
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

## 3. 회귀 테스트

이 문서의 queue 소유권과 실행 순서는 다음 계약 테스트로 확인한다.

- `test/contract/actor-manager.test.js`는 같은 actor의 dispatch는 직렬화하고 서로 다른 actor는
  독립적으로 진행하는지 확인한다.
- `test/contract/entry-spot-serial-dispatch.test.js`는 Entry Spot lifecycle, actor mailbox,
  worker continuation과 automatic turn yield가 각자 정해진 실행 줄을 사용하는지 확인한다.
- `test/contract/stream-session-runtime.test.js`는 한 session의 packet과 disconnect callback이
  입력 순서대로 실행되고 transport callback 안에서 application handler를 직접 호출하지 않는지
  확인한다.
