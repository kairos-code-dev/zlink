<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework ASP.NET Core Actor](aspnet-core-actor.ko.md) | [다음: ZLink Stream Connector For .NET](streaming-client.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [Actor](./aspnet-core-actor.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [policy/Session Actor Dispatch 사용성](../../policy/session-gateway-usability.ko.md)

# Draft -- ZLink Framework .NET Session Actor Dispatch

> 이 문서는 **릴리스 전 초안**이다.
> 아직 공개된 계약[^public-contract]이 아니며, `.NET` `ZLink Framework`에서
> session actor dispatch[^session-actor-dispatch] 표면을 어떤 시그니처와 등록
> 코드로 노출할지 정리해 둔 문서다.
>
> cross-binding[^cross-binding] 정책에 해당하는 부분, 즉 의미·계약·실패
> 의미·테스트 항목·POSD[^posd] 결론과 error kind 매트릭스, 회귀 테스트 목록은
> [policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md)
> 를 본다. 이 문서는 그 정책을 `.NET` 표면으로만 옮겨 내려 정리한다.

## 1. 목적

이 문서가 다루는 범위는 다음과 같다.

- session 서버와 play 서버를 분리하는 구조를 `.NET` 사용자가 실제 시그니처와
  DI 등록 코드 모양으로 살펴 볼 수 있도록 정리한다.
- cross-binding 의미 자체는
  [policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md)
  에서 다룬다.
- 따라서 여기서는 `.NET` 표면만 다룬다.

## 2. 핵심 표면 요약

이 절은 session actor dispatch 가 `.NET` 에서 어떤 형태로 노출되는지를 한눈에
정리한다. 핵심 표면은 다음 네 축이다.

| 축 | `.NET` 표면 |
|----|-------------|
| session → actor dispatch | `IZLinkSessionContext.CreateAndBindActorAsync(...)`, `BindActorHandleAsync(...)`, `DispatchToActorAsync(...)` |
| actor handler | `IZLinkEntrySpotActorSendHandler<TActor, TMessage>`, `IZLinkEntrySpotActorRequestHandler<TActor, TRequest, TReply>`, `IZLinkSpotActorSendHandler<TSpot, TActor, TMessage>`, `IZLinkSpotActorRequestHandler<TSpot, TActor, TRequest, TReply>` |
| actor → own client push | `context.SessionProxy.Send(msg).Submit(...)` / `context.SessionProxy.Request(req).SubmitAsync<TReply>(...)` |
| actor id → client push | `IZLinkActorSessionClient.Send(actorId, msg).Submit(...)` / `IZLinkActorSessionClient.Request(actorId, req).SubmitAsync<TReply>(...)` |
| route 해석 | `IZLinkActorPlayRouteResolver`. actor → client push 방향은 framework/core가 가진 actor-session binding[^actor-session-binding]을 사용한다 |

인터페이스 전체 정의는 [handler-interfaces.ko.md](./handler-interfaces.ko.md)
§4.4, §5.5, §5.6, §5.7 에 모여 있다. 이 문서에서는 사용 모양과 등록 코드
예시만 모아 둔다.

## 2.1 내부 routed wire 계약

이 절에서는 session 서버와 play 서버 사이의 wire 단계 규약을 정리한다.

session actor dispatch 의 public API 는 typed object 중심이다. 다만 서버
사이를 잇는 내부 route transport 단계에서는 공통
[message-model.ko.md](../../policy/message-model.ko.md) 가 정한 multipart 계약을
그대로 따른다.

Session 서버에서 Play 서버 actor 로 보내는 actor dispatch request / send 는
아래와 같은 part 구성을 사용한다.

| part | 내용 |
|------|------|
| `parts[0]` | routed framework header. packet name은 internal actor dispatch packet 이름을 사용한다 |
| `parts[1]` | actor dispatch metadata. actor route와 함께, local actor를 새로 만들어야 하는 경우를 위한 `ActorId`, `ActorType`만 둔다 |
| `parts[2]` | encoded stream header bytes. stream packet kind, codec, request sequence, packet name, metadata snapshot은 모두 이 part에 둔다 |
| `parts[3]` | application payload bytes. framework codec이나 stream packet codec이 만든 payload를 그대로 둔다 |

반대 방향도 같은 원칙을 따른다. Play 서버 actor 에서 Session 서버의 client
stream 으로 보내는 session proxy send / request 는 다음 part 구성을 사용한다.

| part | 내용 |
|------|------|
| `parts[0]` | routed framework header. packet name은 internal session proxy packet 이름을 사용한다 |
| `parts[1]` | session proxy metadata. `ActorId`, `BindingToken`, client packet name, reply 필요 여부, metadata snapshot을 함께 담는다 |
| `parts[2]` | application payload bytes |

reply 도 같은 원칙을 따른다. routed reply header 는 `parts[0]` 에 두고, reply
payload 는 별도 part 로 둔다. payload 가 없으면 빈 payload part 를 그대로 남긴다.

다음과 같은 형태는 이 초안의 내부 routed wire 계약이 아니다.

- `ZLinkActorDispatchPacket` 같은 단일 DTO 안에 `StreamHeader` 와 `byte[] Payload`
  를 같이 넣고, 그 DTO 전체를 다시 JSON 으로 직렬화하는 방식
- `ZLinkSessionProxyPacket` 같은 단일 DTO 안에 proxy metadata 와 payload bytes 를
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

이를 정리하면 `.NET` runtime 의 실행 규칙은 아래와 같다.

| 입력 경로 | 실행 위치 |
| --- | --- |
| stream session → Entry/local actor | actor별로 순서를 보존한 뒤 현재 actor 위치로 dispatch |
| Entry Spot actor packet | actor별 mailbox |
| stream session → user Spot actor | user Spot 실행 queue |
| user Spot actor packet | user Spot 실행 queue |
| user Spot packet / timer / subscription | user Spot 실행 queue |
| Entry Spot initialize / closing / lifecycle callback | Entry Spot 실행 문맥 |

## 2.3 실행 직렬화 핵심 코드

이 절의 코드는 public API 계약이 아니다. 구현자가 실행 의미를 같은 방식으로
이해할 수 있도록 돕는 code-level 설계 기준이다. 실제 class 이름은 달라도
무방하다. 다만 queue 의 소유자와 completion 의미는 이 구조를 따라야 한다.

### 2.3.1 work item과 completion 의미

이 절은 실행 queue 에 들어가는 단위(`work item`)와 그 단위의 끝(`completion`)
이 무엇을 뜻하는지 정리한다.

실행 queue 에는 다음 두 가지를 함께 넣는다.

- 실행할 일(callback)
- 그 일이 끝났음을 알리는 completion

다만 모든 호출자가 이 completion 을 끝까지 기다리는 것은 아니다.

```csharp
internal sealed class ZLinkSerialWorkItem
{
    private readonly Func<CancellationToken, ValueTask> _callback;
    private readonly TaskCompletionSource _completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public ZLinkSerialWorkItem(Func<CancellationToken, ValueTask> callback)
    {
        _callback = callback;
    }

    public Task Completion => _completion.Task;

    public async ValueTask InvokeAsync(
        Action<Exception> onUnhandledException,
        CancellationToken cancellationToken)
    {
        try
        {
            await _callback(cancellationToken).ConfigureAwait(false);
            _completion.TrySetResult();
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            _completion.TrySetCanceled(cancellationToken);
        }
        catch (Exception ex)
        {
            _completion.TrySetException(ex);
            _ = _completion.Task.Exception;
            onUnhandledException(ex);
        }
    }
}
```

completion 이 어떤 의미인지는 호출 종류에 따라 달라진다.

- send 나 fire-and-forget relay 는 target queue 에 work item 을 넣는 시점까지만
  기다리면 충분하다.
- request / reply relay 는 handler 가 reply 를 만들어 내거나 오류를 낼 때까지
  기다린다.
- lifecycle callback 은 runtime shutdown 이나 remove 흐름에서 completion 을
  기다릴 수 있다.
- fire-and-forget handler 예외는 completion 을 기다리는 호출자가 없더라도
  runtime error sink 에 반드시 기록해야 한다.
- `TaskCompletionSource` 에 저장한 예외는 fire-and-forget 경로에서도
  unobserved task exception 으로 남지 않도록 관찰 처리해야 한다.
- 어떤 경우에도 transport callback thread 에서 application handler 를 직접
  호출하지 않는다.

### 2.3.2 단일 실행 queue

이 절은 한 session 또는 한 actor 의 실행 줄을 어떻게 한 줄로 묶는지를 다룬다.

먼저 헷갈리기 쉬운 부분을 짚어 둔다. `SemaphoreSlim` 은 handler 하나하나를
감싸는 lock 이 아니다. 아래 코드의 `_drainGate` 는 queue 를 비우는 drain loop
가 동시에 두 개 이상 실행되지 않도록 막는 용도다.

실제 실행 순서는 `Channel` 에 들어간 work item 의 입력 순서가 만든다.

```csharp
internal sealed class ZLinkSerialExecutionQueue : IAsyncDisposable
{
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly IZLinkRuntimeErrorSink _errorSink;
    private readonly CancellationToken _executionToken;
    private readonly Channel<ZLinkSerialWorkItem> _queue =
        Channel.CreateUnbounded<ZLinkSerialWorkItem>(
            new UnboundedChannelOptions
            {
                SingleReader = true,
                SingleWriter = false,
                AllowSynchronousContinuations = false,
            });
    private readonly SemaphoreSlim _drainGate = new(1, 1);
    private readonly TaskCompletionSource _drained =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int _pendingCount;
    private int _completed;

    public ZLinkSerialExecutionQueue(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken executionToken)
    {
        _taskRunner = taskRunner;
        _errorSink = errorSink;
        _executionToken = executionToken;
    }

    public async ValueTask<ZLinkSerialWorkItem> PostAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken)
    {
        var item = new ZLinkSerialWorkItem(callback);
        Interlocked.Increment(ref _pendingCount);
        try
        {
            await _queue.Writer.WriteAsync(item, cancellationToken).ConfigureAwait(false);
        }
        catch
        {
            CompletePendingItem();
            throw;
        }

        ScheduleDrain();
        return item;
    }

    public bool TryPost(
        Func<CancellationToken, ValueTask> callback,
        out ZLinkSerialWorkItem item)
    {
        item = new ZLinkSerialWorkItem(callback);
        Interlocked.Increment(ref _pendingCount);
        if (_queue.Writer.TryWrite(item))
        {
            ScheduleDrain();
            return true;
        }

        CompletePendingItem();
        return false;
    }

    public async ValueTask RunAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken)
    {
        var item = await PostAsync(callback, cancellationToken).ConfigureAwait(false);
        await item.Completion.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0)
        {
            return;
        }

        _queue.Writer.TryComplete();
        if (Volatile.Read(ref _pendingCount) == 0)
        {
            _drained.TrySetResult();
        }

        try
        {
            await _drained.Task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException)
        {
        }

        _drainGate.Dispose();
    }

    private void ScheduleDrain()
    {
        _taskRunner.RunDetached(
            "serial-queue-drain",
            DrainAsync);
    }

    private async ValueTask DrainAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (!await _drainGate.WaitAsync(0, CancellationToken.None).ConfigureAwait(false))
        {
            return;
        }

        try
        {
            while (_queue.Reader.TryRead(out var item))
            {
                await item.InvokeAsync(
                    ReportHandlerException,
                    _executionToken).ConfigureAwait(false);
                CompletePendingItem();
            }
        }
        finally
        {
            _drainGate.Release();
        }

        if (_queue.Reader.TryPeek(out _))
        {
            ScheduleDrain();
        }
    }

    private void ReportHandlerException(Exception exception)
    {
        try
        {
            _errorSink.ReportHandlerException(exception);
        }
        catch (Exception reportException)
        {
            _taskRunner.ReportErrorSinkFailure(
                "handler-exception-report",
                reportException);
        }
    }

    private void CompletePendingItem()
    {
        if (Interlocked.Decrement(ref _pendingCount) == 0
            && Volatile.Read(ref _completed) != 0)
        {
            _drained.TrySetResult();
        }
    }
}
```

이 queue 의 동작은 다음과 같이 읽으면 된다.

- `PostAsync(...)` 는 work item 을 queue 에 넣고 drain task 를 깨운다.
- 이미 drain 중이라면 `_drainGate.WaitAsync(0)` 이 실패한다. 그래서 새로
  만들어진 drain task 는 곧바로 끝난다.
- 진행 중이던 drain task 가 queue 를 계속 비우므로, handler 가 하나씩 차례로
  실행된다.
- 서로 다른 `ZLinkSerialExecutionQueue` 인스턴스는 서로 다른 실행 줄이다.
  따라서 인스턴스가 다르면 병렬로 실행될 수 있다.

`PostAsync(...)` 와 `RunAsync(...)` 에 넘긴 `cancellationToken` 은 queue 에
들어가기 이전 단계의 대기나 completion 대기를 취소하기 위한 값이다. 즉 이미
queue 에 들어간 work item 을 중간에서 빼낸다는 뜻은 아니다.

handler 실행 자체를 멈추는 값은 따로 있다. runtime shutdown token 이나
handler 가 별도로 받은 operation token 으로 분리해야 한다. 이렇게 분리해
두어야 request timeout 이 같은 queue 안에 줄 서 있는 다음 work item 의
순서를 깨지 않는다.

### 2.3.3 runtime task runner

이 절은 queue drain task 를 누가 만들고, 그 task 의 예외를 어떻게 처리할지
다룬다.

queue drain 은 transport callback thread 에서 직접 실행하지 않는다. framework
runtime 은 분리된 task runner 를 통해 drain task 를 만든다. 이 runner 는
fire-and-forget task 의 예외를 반드시 관찰해 monitoring 이나 runtime error
sink 로 넘겨야 한다.

```csharp
internal interface IZLinkRuntimeErrorSink
{
    void ReportHandlerException(Exception exception);

    void ReportRuntimeTaskException(
        string taskName,
        Exception exception);
}

internal sealed class ZLinkRuntimeTaskRunner
{
    private readonly IZLinkRuntimeErrorSink _errorSink;
    private readonly CancellationToken _shutdownToken;

    public ZLinkRuntimeTaskRunner(
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken shutdownToken)
    {
        _errorSink = errorSink;
        _shutdownToken = shutdownToken;
    }

    public void RunDetached(
        string name,
        Func<CancellationToken, ValueTask> callback)
    {
        _ = Task.Factory.StartNew(
            static state => RunDetachedCoreAsync((TaskState)state!),
            new TaskState(name, callback, _errorSink, _shutdownToken),
            CancellationToken.None,
            TaskCreationOptions.DenyChildAttach,
            TaskScheduler.Default).Unwrap();
    }

    private static async Task RunDetachedCoreAsync(TaskState state)
    {
        try
        {
            await state.Callback(state.ShutdownToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (state.ShutdownToken.IsCancellationRequested)
        {
        }
        catch (Exception ex)
        {
            try
            {
                state.ErrorSink.ReportRuntimeTaskException(
                    state.Name,
                    ex);
            }
            catch
            {
            }
        }
    }

    public void ReportErrorSinkFailure(
        string name,
        Exception exception)
    {
        _ = name;
        _ = exception;
    }

    private sealed record TaskState(
        string Name,
        Func<CancellationToken, ValueTask> Callback,
        IZLinkRuntimeErrorSink ErrorSink,
        CancellationToken ShutdownToken);
}
```

이런 runner 를 따로 두는 이유는 아래와 같다.

- transport callback 은 queue 에 item 을 넣은 뒤 곧장 빠져나와야 한다.
- application handler 는 반드시 runtime 이 만든 Task 안에서 실행되어야 한다.
- fire-and-forget task 의 예외와 fire-and-forget handler 의 예외가 unobserved
  exception 으로 남으면 안 된다.
- `TaskScheduler.Default` 를 명시해 ASP.NET request context 나 임의
  synchronization context 에 묶이지 않도록 한다.

`Task.Factory.StartNew(...).Unwrap()` 대신 `Task.Run(...)` 을 써도 같은 의미를
만들 수 있다. 중요한 것은 다음 두 가지다.

- task 생성 위치를 runtime 한곳에 모은다.
- 예외 관찰과 shutdown token 처리를 동일한 규칙으로 적용한다.

### 2.3.4 stream session runtime

이 절은 stream transport 에서 들어온 frame 이 어떻게 session 별 실행 줄로
이어지는지를 다룬다.

stream transport 가 frame 을 읽으면, session runtime 은 packet 을 만든 뒤
session 별 queue 에 넣는다. 같은 session 에서 들어온 frame 순서는 stream
socket 이 이미 보존한다. 따라서 framework 는 그 순서를 session callback 순서로
이어 주면 된다.

```csharp
internal sealed class ZLinkStreamSessionRuntime
{
    private readonly ZLinkSerialExecutionQueue _sessionQueue;
    private readonly IZLinkSession _session;
    private readonly ZLinkSessionContext _context;

    public ZLinkStreamSessionRuntime(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken executionToken,
        IZLinkSession session,
        ZLinkSessionContext context)
    {
        _sessionQueue = new ZLinkSerialExecutionQueue(
            taskRunner,
            errorSink,
            executionToken);
        _session = session;
        _context = context;
    }

    public async ValueTask OnTransportConnectedAsync(CancellationToken cancellationToken)
    {
        await _sessionQueue.PostAsync(
            ct => _session.OnConnectedAsync(_context, ct),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask OnTransportFrameAsync(
        ZLinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        await _sessionQueue.PostAsync(
            ct => _session.OnDispatchAsync(header, payload, ct),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask OnTransportDisconnectedAsync(CancellationToken cancellationToken)
    {
        await _sessionQueue.PostAsync(
            ct => _session.OnDisconnectedAsync(_context, ct),
            cancellationToken).ConfigureAwait(false);
    }
}
```

위 코드에서 transport 진입점은 `PostAsync(...)` 가 끝나기만 기다린다. 즉
work item 을 queue 에 넣는 시점까지만 기다리고, handler 가 실제로 처리되는
것까지는 기다리지 않는다는 뜻이다. 이렇게 둔 이유는 두 가지다.

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
`DispatchToActorAsync(...)` 같은 helper 다. 이 helper 는 session queue 를 actor
실행 queue 로 이어 주는 bridge 역할을 한다.

```csharp
internal sealed class ZLinkSessionContext : IZLinkSessionContext
{
    private readonly ZLinkActorDispatchRuntime _actorDispatch;
    private readonly ZLinkActorBindingTable _bindings;

    public async ValueTask DispatchToActorAsync(
        IZLinkActorRef actorRef,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var binding = _bindings.GetCurrentBinding(actorRef.ActorId);

        await _actorDispatch.PostFromSessionAsync(
            actorRef,
            binding,
            header,
            payload,
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<TReply> RequestActorAsync<TReply>(
        IZLinkActorRef actorRef,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var binding = _bindings.GetCurrentBinding(actorRef.ActorId);

        return await _actorDispatch.InvokeFromSessionAsync<TReply>(
            actorRef,
            binding,
            header,
            payload,
            cancellationToken).ConfigureAwait(false);
    }
}
```

두 helper 는 끝나는 시점이 다르다.

- send 성격의 `DispatchToActorAsync(...)` 는 actor 의 실행 줄에 packet 을 넣는
  데 성공한 시점에 끝난다.
- 반면 request 성격의 `RequestActorAsync<TReply>(...)` 는 actor handler 가 만든
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
없다.

```csharp
internal sealed class ZLinkActorDispatchRuntime
{
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly IZLinkRuntimeErrorSink _errorSink;
    private readonly CancellationToken _executionToken;

    private readonly ConcurrentDictionary<string, ZLinkSerialExecutionQueue>
        _entryActorQueues = new();

    public ZLinkActorDispatchRuntime(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken executionToken)
    {
        _taskRunner = taskRunner;
        _errorSink = errorSink;
        _executionToken = executionToken;
    }

    public async ValueTask PostFromSessionAsync(
        IZLinkActorRef actorRef,
        ZLinkActorSessionBinding binding,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var item = CreateActorWorkItem<object?>(
            actorRef,
            binding,
            header,
            payload,
            expectReply: false);

        await EnqueueByCurrentLocationAsync(
            actorRef,
            item,
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<TReply> InvokeFromSessionAsync<TReply>(
        IZLinkActorRef actorRef,
        ZLinkActorSessionBinding binding,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var item = CreateActorWorkItem<TReply>(
            actorRef,
            binding,
            header,
            payload,
            expectReply: true);

        var queued = await EnqueueByCurrentLocationAsync(
            actorRef,
            item,
            cancellationToken).ConfigureAwait(false);

        await queued.Completion.WaitAsync(cancellationToken).ConfigureAwait(false);
        return item.GetReply();
    }

    private ValueTask<ZLinkSerialWorkItem> EnqueueByCurrentLocationAsync(
        IZLinkActorRef actorRef,
        ZLinkActorDispatchWorkItem item,
        CancellationToken cancellationToken)
    {
        var location = actorRef.ReadCurrentLocation();

        if (location.IsEntrySpot)
        {
            var actorQueue = _entryActorQueues.GetOrAdd(
                actorRef.ActorId,
                _ => new ZLinkSerialExecutionQueue(
                    _taskRunner,
                    _errorSink,
                    _executionToken));

            return actorQueue.PostAsync(
                ct => InvokeEntrySpotHandlerAsync(item, ct),
                cancellationToken);
        }

        return location.UserSpot.ExecutionQueue.PostAsync(
            ct => InvokeUserSpotHandlerAsync(location.UserSpot, item, ct),
            cancellationToken);
    }
}
```

이 코드를 보면 Entry Spot 에는 actor 별 queue 만 둔다. 즉 Entry Spot 전체
queue 에 actor packet 을 넣지 않는다. 그래서 `actor A` 의 handler 가 아무리
오래 걸려도, `actor B` 의 Entry Spot packet 은 같은 actor queue 에 끌려
들어가지 않는다.

user Spot 은 그 반대다. actor 별 queue 로만 끝내 버리면 같은 room state 를
handler 두 개가 동시에 건드릴 수 있다. 그래서 actor 가 user Spot 에 있으면
최종 handler 호출은 반드시 `location.UserSpot.ExecutionQueue` 안에서 한다.

### 2.3.7 user Spot queue

이 절은 user Spot 의 실행 queue 가 어떤 입력을 한 줄로 묶는지를 다룬다.

user Spot queue 는 actor packet 만 처리하는 곳이 아니다. 다음 항목 모두 같은
queue 로 들어와야 한다.

- Spot packet
- timer
- subscription
- channel reply continuation

```csharp
internal sealed class ZLinkUserSpotRuntime
{
    public ZLinkSerialExecutionQueue ExecutionQueue { get; }

    public ZLinkUserSpotRuntime(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken executionToken)
    {
        ExecutionQueue = new ZLinkSerialExecutionQueue(
            taskRunner,
            errorSink,
            executionToken);
    }

    public ValueTask<ZLinkSerialWorkItem> EnqueueSpotPacketAsync(
        ZLinkSpotPacket packet,
        CancellationToken cancellationToken)
    {
        return ExecutionQueue.PostAsync(
            ct => InvokeSpotPacketHandlerAsync(packet, ct),
            cancellationToken);
    }

    public ValueTask<ZLinkSerialWorkItem> EnqueueActorPacketAsync(
        ZLinkActorDispatchWorkItem item,
        CancellationToken cancellationToken)
    {
        return ExecutionQueue.PostAsync(
            ct => InvokeUserSpotActorHandlerAsync(item, ct),
            cancellationToken);
    }

    public ValueTask<ZLinkSerialWorkItem> EnqueueTimerTickAsync(
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        return ExecutionQueue.PostAsync(
            ct => InvokeTimerHandlerAsync(tick, ct),
            cancellationToken);
    }

    public ValueTask<ZLinkSerialWorkItem> EnqueueChannelReplyAsync(
        ZLinkChannelReply reply,
        CancellationToken cancellationToken)
    {
        return ExecutionQueue.PostAsync(
            ct => CompleteChannelRequestAsync(reply, ct),
            cancellationToken);
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

```csharp
internal sealed class ZLinkEntrySpotRuntime
{
    private readonly ZLinkSerialExecutionQueue _lifecycleQueue;
    private readonly IZLinkEntrySpot _entrySpot;

    public ZLinkEntrySpotRuntime(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken executionToken,
        IZLinkEntrySpot entrySpot)
    {
        _lifecycleQueue = new ZLinkSerialExecutionQueue(
            taskRunner,
            errorSink,
            executionToken);
        _entrySpot = entrySpot;
    }

    public ValueTask InitializeAsync(CancellationToken cancellationToken)
    {
        return _lifecycleQueue.RunAsync(
            ct => _entrySpot.OnInitializeAsync(ct),
            cancellationToken);
    }

    public ValueTask ClosingAsync(CancellationToken cancellationToken)
    {
        return _lifecycleQueue.RunAsync(
            ct => _entrySpot.OnClosingAsync(ct),
            cancellationToken);
    }

    public ValueTask ActorJoinedAsync(
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        return _lifecycleQueue.RunAsync(
            ct => InvokeEntrySpotActorJoinedHandlerAsync(info, ct),
            cancellationToken);
    }
}
```

이 queue 에는 Entry Spot actor packet 을 넣지 않는다. Entry Spot actor packet
은 `ZLinkActorDispatchRuntime` 의 actor 별 queue 를 사용한다.

### 2.3.9 독립 node message task

이 절은 어떤 Spot 이나 actor 에도 묶이지 않는 node 단위 message 를 어떻게
실행하는지 다룬다.

특정 Spot 이나 actor 의 상태를 보호할 필요가 없는 node-level message 는
message 하나를 runtime task 하나로 실행한다. 이 경로에는 전역 node queue 를
두지 않는다. 전역 queue 를 두면 서로 무관한 node message 들이 한 줄로 묶여
막혀 버리기 때문이다.

```csharp
internal sealed class ZLinkNodeMessageRuntime
{
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly ZLinkNodeMessageHandlerInvoker _invoker;

    public void OnNodeMessage(ZLinkNodeMessage message)
    {
        _taskRunner.RunDetached(
            "node-message",
            ct => _invoker.InvokeAsync(message, ct));
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

```csharp
public interface IZLinkEntrySpotActorRequestHandler<TActor, in TRequest, TReply>
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorRequestHandler<TSpot, TActor, in TRequest, TReply>
    where TSpot : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkEntrySpotActorSendHandler<TActor, in TMessage>
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TActor actor,
        TMessage message,
        CancellationToken cancellationToken);
}
```

handler 는 transport raw header 를 직접 받지 않는다. Session route, stream
sequence, binding token 같은 값은 framework runtime 의 metadata 쪽에 남는다.

typed actor context 는 source session 의 `RoutingId` 를 노출하지 않는다.
handler 가 즉시 자기 client 로 push 를 보내야 하는 경우에도 마찬가지다. 이때도
`context.SessionProxy.Send(message)` 처럼 현재 actor 에 묶인 표면을 사용해야
한다.

### 3.2 metadata snapshot

이 절은 application metadata 를 어디까지 actor 쪽에 흘려 보낼지 정하는 표면을
다룬다.

```csharp
public sealed class ZLinkMessageMetadata
{
    public static ZLinkMessageMetadata Empty { get; }

    public IReadOnlyDictionary<string, string> Application { get; }
    public IReadOnlyDictionary<string, string> Codec { get; }

    public bool TryGetApplicationValue(
        string key,
        out string? value);

    public bool TryGetCodecValue(
        string key,
        out string? value);
}

public interface IZLinkMessageMetadataPolicy
{
    bool CanForwardApplicationKey(string key);
}
```

기본 `IZLinkMessageMetadataPolicy` 는 application metadata 를 전달하지 않는다.
trace id 같은 값을 actor handler 까지 함께 흘려 보내려면, framework 등록
단계에서 명시적으로 허용해야 한다.

```csharp
options.ConfigureMetadata(metadata =>
{
    metadata.ForwardApplicationKey("trace-id");
    metadata.ForwardApplicationKey("tenant-id");
});
```

## 4. SessionProxy 호출 표면

이 절은 actor 가 client session 쪽으로 push 나 request 를 보낼 때 사용하는
public 표면을 정리한다.

```csharp
public interface IZLinkSessionProxy
{
    IZLinkSessionProxySendCall Send<TMessage>(
        TMessage message);

    IZLinkSessionProxyRequestCall Request<TRequest>(
        TRequest request);

    ValueTask DisconnectAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorSessionClient
{
    IZLinkSessionProxySendCall Send<TMessage>(
        string actorId,
        TMessage message);

    IZLinkSessionProxyRequestCall Request<TRequest>(
        string actorId,
        TRequest request);

    ValueTask DisconnectAsync(
        string actorId,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionProxySendCall
{
    IZLinkSessionProxySendCall PacketName(string packetName);

    IZLinkSessionProxySendCall Metadata(
        string key,
        string value);

    ValueTask Submit(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionProxyRequestCall
{
    IZLinkSessionProxyRequestCall PacketName(string packetName);

    IZLinkSessionProxyRequestCall Metadata(
        string key,
        string value);

    IZLinkSessionProxyRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(
        CancellationToken cancellationToken = default);
}
```

호출 모양은 아래와 같다.

```csharp
await sessionProxy
    .Send(new GameStateChangedMsg(gameId, board))
    .Submit(cancellationToken);

GamePromptRep prompt = await sessionProxy
    .Request(new ChooseMoveReq(gameId, board))
    .SubmitAsync<GamePromptRep>(cancellationToken);
```

기존에 사용하던 `SessionGateway` 라는 이름은 새 public API 에서 제거한다.
이름이 두 갈래로 정리된다.

- session → actor 방향: `CreateAndBindActorAsync(...)`,
  `BindActorHandleAsync(...)`, `DispatchToActorAsync(...)`,
  `IZLinkActorRef.NotifyDisconnectedAsync(...)` 를 사용한다.
- actor → 자기 client 방향: actor context 의 `IZLinkSessionProxy` 를 사용한다.
- actor id 를 지정해서 다른 actor 의 client session 에 보내야 하는 application
  service 는 `IZLinkActorSessionClient` 를 사용한다.

`IZLinkSessionProxy.Send(...).Submit(...)` 은 one-way push 다. 이 호출은
framework route send 제출이 끝났다는 의미일 뿐이다. 즉 client application
handler 가 메시지를 처리 완료했다는 ack 는 아니다.

`IZLinkSessionProxy.DisconnectAsync(...)` 는 actor 가 현재 actor id 에 묶인
client stream 을 끊어야 한다고 판단했을 때 호출한다. 이 close 는
application 이 의도한 동작이므로 session 의 `OnDisconnectedAsync(...)` callback
을 다시 올리지 않는다. framework 는 stream close 와 actor-session binding
정리만 수행한다.

stale binding token, 이미 닫힌 stream, 늦게 도착한 push 는 해당 push 하나만
실패해야 한다. 즉 route receive loop 나 host shutdown 자체를 실패시켜서는 안
된다.

만약 client 처리 완료가 계약상 필요하다면, one-way push 가 아니라
`IZLinkSessionProxy.Request(...).SubmitAsync<TReply>(...)` 같은 명시적인
request / reply 표면을 써야 한다.

재접속은 actor id 기준으로 idempotent 해야 한다. 같은 actor id 가 새 stream
session 에서 `BindActorHandleAsync(...)` 로 다시 들어오면, framework 는 다음과
같이 동작한다.

- 기존 actor instance 와 spot membership 은 그대로 유지한다.
- session binding 만 새 stream 으로 옮긴다.

이 규칙이 있어야 client reconnect 가 "새 게임에 참여"가 아니라 "기존 actor 의
새 연결"로 동작한다.

## 5. Session에서 actor로 relay

session actor dispatch 에서 session 은 actor runtime 을 직접 호출하는 범용
public client 를 사용하지 않는다. client stream 에서 받은 packet 은
`IZLinkSession.OnDispatchAsync(...)` 로 올라오고, session 구현은 actor handle 을
만든 뒤 `DispatchToActorAsync(...)` 로 전달한다.

```csharp
public interface IZLinkSessionActorDispatchContext
{
    ValueTask<IZLinkActorRef> CreateAndBindActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask DispatchToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);
}
```

이 표면만 남기면 session 코드의 의도가 분명해진다. session 은 "받은 client
packet 을 어떤 actor 에 relay 할지"만 결정하고, remote actor route 와 multipart
전송은 framework 내부가 처리한다.

## 6. Actor/Spot route resolver 등록

이 절은 framework 가 route 결정을 위해 외부에서 받는 resolver 표면을 정리한다.

공개 resolver 는 actor 와 spot 두 축으로 한정한다.

- session actor dispatch 에 필요한 public resolver 는 하나뿐이다. actor id 에서
  actor runtime route 를 찾는 resolver 다.
- actor 가 현재 연결된 client session 으로 push 나 request 를 보낼 때는,
  framework / core 가 가진 actor-session binding 상태를 사용한다.
- actor 가 `JoinSpot(spotName, ...)` 로 user Spot 에 들어가는 경로가 node
  경계를 넘을 수 있다면, spot route resolver 도 함께 등록한다.

```csharp
namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorPlayRouteResolver
{
    ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid);
```

```csharp
namespace Zlink.Framework.Contracts.Spots;

public interface IZLinkSpotRouteResolver
{
    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        ZLinkSpotId spotId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkSpotRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    ZLinkSpotId SpotId);
```

DI 등록 (Session 서버):

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddActorPlayRouteResolver<RegistryPlayRouteStore>();
    options.AddSpotRouteResolver<RegistrySpotRouteStore>();
    // STREAM session 등록 + routed channel 등록 (별도 문서 참고)
});
```

DI 등록 (Play 서버):

```csharp
builder.Services.AddScoped<PlayerActorFactory>();
builder.Services.AddSingleton<RegistryPlayRouteStore>();
builder.Services.AddSingleton<RegistrySpotRouteStore>();
builder.Services.AddZLinkFramework(options =>
{
    options.AddActorFactory<PlayerActorFactory>("player");
    options.AddActorPlayRouteResolver<RegistryPlayRouteStore>();
    options.AddSpotRouteResolver<RegistrySpotRouteStore>();
    // routed channel 등록 + spot mesh 등록 (별도 문서 참고)
});
```

### 6.1 Actor-session binding 상태

이 절은 actor 와 stream session 의 연결 정보를 누가 들고 있는지를 정리한다.

framework 는 session route resolver 를 public 기본 표면으로 제공하지 않는다.
session binding 은 다음 흐름에서 framework / core 가 갱신해 두는 내부
상태이기 때문이다.

- actor handle 생성
- stream attach
- stream disconnect

분산 배포에서 이 상태를 외부 저장소에 두어야 한다면, `.NET` adapter 는
`IZLinkActorSessionBindingStore` 를 등록한다. 이 store 는 resolver 가 아니다.
bind / unbind 동작과 `SessionProxy` 조회를 함께 가진 저장소 계약이다.

`IRegistryDiscoveryMetadata` 같은 registry / discovery metadata adapter 는
sample code 에서 쓸 수 있다. 다만 framework public contract 는 아니다.
session binding 을 registry 에 저장하는 샘플을 만들더라도, 그 adapter 는
`IZLinkActorSessionBindingStore` 구현 내부에 머물러야 한다. 별도의 public
session route resolver 를 도입하지 않는다.

`DeleteIfAsync(...)` 는 저장된 `sessionId` 와 `bindingToken` 이 모두 일치할
때만 key 를 삭제해야 한다. registry metadata API 가 조건부 삭제나
compare-and-swap 을 제공하지 않는다면, sample adapter 는 read 후 delete 로
흉내 내서는 안 된다.

## 7. 등록 표면 (host 측)

이 절은 host 가 framework 를 띄울 때 작성하는 등록 코드 모양을 보여 준다.

```csharp
options.UseDiscovery(discovery => discovery.Add(registryEndpoint));

options.AddRoutedChannel("backend", routed =>
{
    routed.Bind(playEndpoint);
});

options.AddActorFactory<TicTacToeActorFactory>("player");

public sealed class TicTacToeActor(string actorId)
    : IZLinkActor
{
    public string ActorId { get; } = actorId;
    public IZLinkActorContext Context { get; set; } = default!;

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}
```

spot handler는 spot 객체 안에서 등록한다.

```csharp
options.AddSpotNode("play", spot =>
{
    spot.Bind(spotEndpoint);
    spot.AddEntrySpot<TicTacToeEntrySpot>();
    spot.AddSpotFactory<TicTacToeGame>("game");
});

public sealed class TicTacToeEntrySpot : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; }

    public void Configure()
    {
        Context.AddActorPacket<JoinMatchEntryHandler, TicTacToeActor>();
        Context.AddActorJoined<TicTacToeEntryJoinedHandler, TicTacToeActor>();
        Context.AddActorLeft<TicTacToeEntryLeftHandler, TicTacToeActor>();
    }
}

public sealed class TicTacToeGame : IZLinkSpot
{
    public IZLinkSpotContext Context { get; }

    public void Configure()
    {
        Context.AddActorJoin<JoinMatchHandler, TicTacToeActor, JoinMatchReq, JoinMatchRes>();
        Context.AddActorPacket<PlaceMarkHandler, TicTacToeActor>();
        Context.AddActorPacket<MoveHandler, TicTacToeActor>();
        Context.AddActorJoined<TicTacToeGameJoinedHandler, TicTacToeActor>();
        Context.AddActorLeft<TicTacToeGameLeftHandler, TicTacToeActor>();
    }
}
```

actor resolver 는 transport builder 가 아니라 framework service 설정 쪽에
등록한다.

```csharp
options.AddActorPlayRouteResolver<TicTacToePlayRouteResolver>();
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

```csharp
public sealed class TicTacToeSession : IZLinkSession
{
    public IZLinkSessionContext Context { get; set; } = default!;

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (header.Name == "auth")
        {
            AuthReq request = payload.FromJson<AuthReq>();

            IZLinkActorRef actor = await Context.BindActorHandleAsync(
                request.ActorId,
                request.ActorType,
                cancellationToken);

            authenticatedActors.Remember(request.ActorId, actor);

            await Context.Reply(new AuthRep(ok: true))
                .Submit(cancellationToken);
            return;
        }

        if (authenticatedActors.TryGet(header, out IZLinkActorRef actor))
        {
            await Context.DispatchToActorAsync(
                actor,
                header,
                payload,
                cancellationToken);
            return;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorNotAuthenticated,
            "Actor is not bound to this session.");
    }

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        => ValueTask.CompletedTask;

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        foreach (IZLinkActorRef actor in authenticatedActors.Values)
        {
            await actor.NotifyDisconnectedAsync(cancellationToken);
        }

        authenticatedActors.Clear();
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
        => ValueTask.CompletedTask;
}

public sealed class JoinMatchHandler
    : IZLinkEntrySpotActorRequestHandler<PlayerActor, JoinMatchReq, JoinMatchRes>
{
    public async ValueTask<JoinMatchRes> HandleAsync(
        PlayerActor actor,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        // request.MatchId는 application 도메인이 정한 spot 이름이다.
        // RoutingId 변환은 framework 내부의 spot route resolver가 풀어 준다.
        var joined = await actor.Context
            .JoinSpot<JoinMatchSpotResult, JoinMatchReq>(request.MatchId, request)
            .Submit(cancellationToken);
        return joined.ToReply();
    }
}

public sealed class PlaceMarkHandler
    : IZLinkSpotActorRequestHandler<TicTacToeGameSpot, PlayerActor, PlaceMarkReq, PlaceMarkRes>
{
    public ValueTask<PlaceMarkRes> HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        PlaceMarkReq request,
        CancellationToken cancellationToken)
    {
        var room = spot;
        return ValueTask.FromResult(room.PlaceMark(actor.ActorId, request.Cell));
    }
}
```

## 9. Error 표현 (`.NET` exception)

이 절은 framework 가 던지는 오류가 `.NET` 표면에서 어떤 모양으로 보이는지를
정리한다.

public `.NET` API 에서는 framework error 를 하나의 exception family 로
모은다.

```csharp
public sealed class ZLinkFrameworkException : Exception
{
    public ZLinkFrameworkErrorKind Kind { get; }
    public bool IsRetriable { get; }
}

public enum ZLinkFrameworkErrorKind
{
    ActorNotAuthenticated,
    ActorRouteNotFound,
    ActorCreateFailed,
    ActorAlreadyExists,
    ActorSessionNotBound,
    SessionProxyTimeout,
    ActorDispatchTimeout,
    ActorDispatchHandlerFailed,
    CodecFailed,
}
```

각 kind 의 발생 조건과 cross-binding 의미는
[policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md)
§17 error-kind 매트릭스에서 다룬다.

`ActorCreateFailed` 와 `ActorAlreadyExists` 는 local `SpotNode` 의 actor
runtime 이 actor 를 새로 만들거나 handle 을 준비할 때 사용한다.
`BindActorHandleAsync(...)` 는 remote node 를 직접 지정하지 않는다. 현재
actor 에 bound 된 session 이 없어서 client push 를 보낼 수 없으면
`ActorSessionNotBound` 로 분류한다.

`IsRetriable` 은 framework 가 자동으로 retry 해 준다는 의미가 아니다. caller
가 retry policy 를 만들 때 참고할 수 있는 분류일 뿐이다. sample 코드에서도 이
값을 이용해 retry loop 를 만들지 않는다.

## 10. Diagnostic helper

이 절은 `.NET` 사용자가 connection 이나 topology[^topology] 상태를 점검할 때
쓸 수 있는 helper 초안이다. session actor dispatch 의 필수 API 는 아니며,
운영 점검용으로만 둔다.

```csharp
public interface IZLinkTopologyDiagnostics
{
    ValueTask<ZLinkRoutedChannelSnapshot> GetRoutedChannelAsync(
        string routerChannelId,
        CancellationToken cancellationToken = default);
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
  [policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md)
- 인터페이스 전체 정의 → [handler-interfaces.ko.md](./handler-interfaces.ko.md)
  §4.4 (session), §5.5 (session relay), §5.6 (`IZLinkSessionProxy`), §5.7
  (actor route resolver)
- actor 라이프사이클과 actor handler 모델 →
  [aspnet-core-actor.ko.md](./aspnet-core-actor.ko.md)
- TicTacToe sample contract →
  [tictactoe-game-sample.ko.md](./tictactoe-game-sample.ko.md)
- STREAM session 라이프사이클 →
  [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md)

## 12. 회귀 테스트

이 절은 session actor dispatch 표면을 지키기 위해 어느 통합 테스트를 함께
유지해야 하는지를 정리한다.

session actor dispatch 항목은 다음 요소가 하나의 흐름으로 맞물려 동작하는지를
확인한다.

- stream session
- actor factory
- route resolver
- actor-session binding

또한 이전 stream 에서 늦게 도착한 disconnect 가 현재 actor-session 연결을
끊지 않는지도 함께 확인한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `StreamIntegrationTests.SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence` | session callback에서 actor request를 relay하고, request sequence를 통해 reply를 되돌린다. |
| `StreamIntegrationTests.ActorRefNotifyDisconnected_Notifies_Local_Bound_Actor` | `CreateAndBindActorAsync(...)` 로 만든 local actor ref의 disconnect 알림이 actor `OnDisconnectedAsync(...)` 로 전달된다. |
| `StreamIntegrationTests.SessionProxyDisconnect_FromLocalActor_Closes_Client_Without_Session_Disconnect_Callback` | local actor 가 `IZLinkSessionProxy.DisconnectAsync(...)` 를 호출하면 session binding 이 정리되고 session disconnect callback 은 다시 호출되지 않는다. |
| `StreamIntegrationTests.SessionProxyDisconnect_FromRemoteActor_Closes_Client_Without_Session_Disconnect_Callback` | remote actor 가 routed `IZLinkSessionProxy.DisconnectAsync(...)` 를 호출해도 session host 에서 같은 close 의미가 유지된다. |
| `StreamIntegrationTests.SessionActorDispatch_Uses_Multipart_Routed_Actor_Dispatch` | Session 서버와 Play 서버 사이의 actor dispatch가 route header, actor metadata, stream header, body를 각각 별도 part로 유지한다. |
| `StreamIntegrationTests.SessionProxy_Uses_Multipart_Routed_Client_Push` | Play 서버에서 Session 서버로 가는 `SessionProxy` send/request가 proxy metadata와 body를 별도 part로 유지하고, client에게는 단일 STREAM packet으로 보낸다. |
| `SpotIntegrationTests.EntrySpot_ActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors` | Entry Spot actor packet이 actor별 순서를 지키되, 서로 다른 actor를 전역으로 막지 않는다. |
| `SpotIntegrationTests.EntrySpot_NativeActorReadableBatch_Dispatches_Actors_In_Parallel` | native Entry Spot actor batch도 actor별 순서 규칙을 거쳐 dispatch된다. |
| `SpotIntegrationTests.LocalActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors` | user Spot에 들어가지 않은 actor packet도 actor별 순서를 지키되 서로 다른 actor 사이에서는 병렬로 실행될 수 있다. |
| `SpotIntegrationTests.ActorDispatch_Rechecks_CurrentLocation_After_Waiting_For_ActorMailbox` | 같은 actor의 앞 packet이 join을 마치고 나면, 대기 중이던 다음 packet이 새 user Spot 위치로 dispatch된다. |
| `SpotIntegrationTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | actor join 이후의 dispatch가 현재 spot 실행 문맥에서 실행된다. |
| `SpotIntegrationTests.ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream` | 이전 stream의 늦은 disconnect가 현재 actor-session 연결을 끊지 않는다. |
| `StreamIntegrationTests.HeaderStreamSession_Can_Close_Current_Client_Stream` | session context가 현재 client stream을 닫고 disconnect callback으로 자연스럽게 이어진다. |
| `SerialExecutorTests.StreamSessionSerialExecutor_Continues_After_Work_Exception` | session queue의 fire-and-forget work 예외가 error sink에 기록되고, 다음 work 실행을 막지 않는다. |
| `SerialExecutorTests.SpotSerialExecutor_Continues_After_Queued_Work_Exception` | Spot queue의 fire-and-forget work 예외가 error sink에 기록되고, 다음 work 실행을 막지 않는다. |
| `SerialExecutorTests.SpotSerialExecutor_ExecuteAsync_Propagates_Work_Exception` | Spot queue에서 완료를 기다리는 실행 경로는 handler 예외를 호출자에게 그대로 돌려준다. |
| `SerialExecutorTests.SerialExecutionQueue_RunAsync_Propagates_Work_Exception` | 공통 serial queue의 `RunAsync(...)`가 work 예외를 error sink에 기록하면서 호출자에게도 전파한다. |
| `SerialExecutorTests.SerialExecutionQueue_Wait_Cancellation_Does_Not_Remove_Queued_Work` | 공통 serial queue에서 completion wait가 취소되더라도 이미 queue에 들어간 work item은 제거되지 않는다. |
| `SerialExecutorTests.ActorDispatchCancellation_Does_Not_Stop_Current_Or_Later_Dispatch` | actor dispatch 대기를 취소해도 현재 실행 중인 dispatch나 이후 dispatch가 중단되지 않는다. |
| `DocumentationRegressionTests.DotNetSessionActorDispatch_Documents_ExecutionSerialization_Core_Code` | 실행 직렬화 핵심 코드 섹션이 queue, runtime task, error sink, cancellation 의미를 계속 설명한다. |
| `DocumentationRegressionTests.DotNetRegressionMatrix_Includes_ExecutionSerialization_Guards` | 중앙 regression matrix가 실행 직렬화 관련 회귀 항목을 유지한다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어, 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^session-actor-dispatch]: session actor dispatch는 클라이언트 세션으로 들어온 요청을 그 세션과 묶여 있는 actor로 자동 전달해 주는 패턴이다.
[^cross-binding]: cross-binding은 `.NET`, Java, C++ 등 서로 다른 언어 바인딩에 같은 의미가 동일하게 적용되어야 함을 가리키는 정책 축이다.
[^posd]: POSD(Point Of Significant Decision)는 의미 있는 설계 결정이 내려진 지점을 기록해 두는 표기 방식이다.
[^actor-session-binding]: actor-session binding은 특정 actor가 현재 어떤 client stream session에 연결되어 있는지를 framework/core가 보관하는 상태다.
[^entry-spot]: Entry Spot은 모든 actor가 처음 거치는 공용 입구 역할의 Spot이다. user Spot으로 옮겨 가기 전까지 actor가 머무는 위치다.
[^user-spot]: user Spot은 room이나 game처럼 application 도메인이 정의한 Spot으로, 같은 Spot 안의 actor들이 공유 상태를 두고 상호작용하는 곳이다.
[^topology]: topology는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 표현하는 구성 정보다.
