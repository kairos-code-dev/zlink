namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
{
    internal object RuntimeExecutionOwner => _runtime.ExecutionOwner;

    public CancellationToken StopToken => _stopSource.Token;

    internal void CancelActiveOperations()
    {
        _stopSource.Cancel();
    }

    internal void RequestStop()
    {
        _serial.RequestStop();
        _stopSource.Cancel();
    }

    public ValueTask DisposeAsync()
    {
        TaskCompletionSource completion;
        lock (_lifecycleGate)
        {
            if (_finalization is not null) return new ValueTask(_finalization);

            Volatile.Write(ref _disposed, 1);
            completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _finalization = completion.Task;
        }

        _ = CompleteFinalizationAsync(completion);
        return new ValueTask(completion.Task);
    }

    private async Task CompleteFinalizationAsync(TaskCompletionSource completion)
    {
        try
        {
            await FinalizeAsync().ConfigureAwait(false);
            completion.TrySetResult();
        }
        catch (Exception exception)
        {
            completion.TrySetException(exception);
        }
    }

    private async Task FinalizeAsync()
    {
        var failures = new List<Exception>();
        Capture(RequestStop);
        await CaptureAsync(_timers.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_serial.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_outbound.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(NativeSpot.DisposeAsync).ConfigureAwait(false);
        Capture(_stopSource.Dispose);
        await CaptureAsync(_handlerInstances.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_scope.DisposeAsync).ConfigureAwait(false);
        ThrowFailures(failures);

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        void Capture(Action cleanup)
        {
            try
            {
                cleanup();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    private static void ThrowFailures(IReadOnlyList<Exception> failures)
    {
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
    }

    public ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class
    {
        return _timers.AddAsync(
            name,
            period,
            options,
            typeof(THandler),
            Spot.GetType(),
            StopToken,
            (descriptor, tick, ct) => ExecuteSerializedAsync(
                async static (activation, state, innerCt) =>
                {
                    await activation.InvokeTimerAsync(state.Descriptor, state.Tick, innerCt);
                },
                (Descriptor: descriptor, Tick: tick),
                ct),
            PublishTimerFailureAsync,
            cancellationToken);
    }

    public IZLinkWorkerCall<TResult> RunCpuWorker<TResult>(
        Func<CancellationToken, TResult> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        return new ZLinkWorkerCall<TResult>(
            _runtime.WorkerPool,
            work,
            _runtime.ErrorSink);
    }

    public IZLinkWorkerCall<TResult> RunIoWorker<TResult>(
        Func<CancellationToken, ValueTask<TResult>> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        return new ZLinkIoWorkerCall<TResult>(
            _runtime.WorkerPool.ShutdownToken,
            work,
            _runtime.ErrorSink);
    }

    ValueTask<bool> IZLinkSpotContext.CloseAsync(CancellationToken cancellationToken)
    {
        return _runtime.CloseAsync(SpotRid, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResponse> InitializeAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        RegisterWithoutSynchronizationContext(() =>
        {
            ZLinkSpotNativeDispatchRouter.Attach(
                NativeSpot,
                receivedMessages =>
                {
                    if (receivedMessages.Count == 0)
                    {
                        QueueSerialized(static (activation, ct) => activation._dispatcher.DispatchRouteDrainAsync(ct));
                        return;
                    }

                    foreach (var received in receivedMessages)
                        QueueSerialized(
                            static (activation, state, ct) => activation._dispatcher.DispatchRouteAsync(state, ct),
                            received);
                },
                drain => drain?.Invoke(),
                () => QueueSerialized(static (activation, ct) => activation.DispatchSubscriptionsAsync(ct)),
                () => QueueSerialized(static (activation, ct) =>
                    activation._dispatcher.DispatchActorJoinDrainAsync(ct)),
                () => QueueSerialized(static (activation, ct) =>
                    activation.DispatchActorLifecycleDrainAsync(ct)),
                actorParts =>
                {
                    var dispatchable = ZLinkActorHandoffIngress.CaptureMovingFrames(_runtime, actorParts);
                    if (dispatchable.Count == 0)
                    {
                        dispatchable.Dispose();
                        return;
                    }

                    if (!QueueSerialized(
                        static (activation, state, ct) => activation._dispatcher.DispatchActorFramesAsync(state, ct),
                        dispatchable,
                        dispatchable.Dispose))
                        dispatchable.Dispose();
                });

            return 0;
        });

        var create = new SpotCreateCallState(request);
        await ExecuteSerializedAsync(
            static async (activation, state, ct) =>
            {
                state.Response = await activation.Spot.OnCreateAsync(state.Request, ct);
                if (!state.Response.Accepted) return;

                await activation.Spot.OnInitializeAsync(ct);
            },
            create,
            cancellationToken);
        return create.Response;
    }

    public ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return _actorDispatchSubmitter.Async(actor, runtimeState, header, body, cancellationToken);
    }

    public ValueTask<ZLinkActorReply> SubmitActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return _actorDispatchSubmitter.SubmitForReplyAsync(
            actor,
            runtimeState,
            header,
            body,
            cancellationToken);
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        if (Interlocked.Exchange(ref _closingInvoked, 1) != 0)
            return ValueTask.CompletedTask;
        return _serial.ExecuteLifecycleAsync(
            static (activation, ct) => activation.Spot.OnClosingAsync(ct),
            cancellationToken);
    }

    internal async ValueTask<bool> TryCloseIfNoActorsAsync(
        CancellationToken cancellationToken)
    {
        var accepted = false;
        await _serial.ExecuteLifecycleAsync(
                async (activation, ct) =>
                {
                    if (activation._actors.Count > 0) return;

                    accepted = true;
                    if (Interlocked.Exchange(ref activation._closingInvoked, 1) == 0)
                        await activation.Spot.OnClosingAsync(ct).ConfigureAwait(false);
                },
                cancellationToken)
            .ConfigureAwait(false);
        return accepted;
    }

    private ValueTask ExecuteSerializedAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        return _serial.ExecuteAsync(operation, cancellationToken);
    }

    private ValueTask ExecuteSerializedAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        return _serial.ExecuteAsync(operation, state, cancellationToken);
    }

    private bool QueueSerialized(Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation)
    {
        return _serial.Queue(operation);
    }

    private bool QueueSerialized<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        Action? onSkipped = null)
    {
        var capturedOp = operation;
        var capturedState = state;
        return _serial.Queue(
            (activation, ct) => capturedOp(activation, capturedState, ct),
            onSkipped);
    }

    private async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        await _dispatcher
            .DispatchSubscriptionsAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchActorLifecycleDrainAsync(CancellationToken cancellationToken)
    {
        while (true)
        {
            var lifecycle = NativeSpot.RecvActorLifecycle(RecvFlags.DontWait);
            if (lifecycle is null) return;
            if (lifecycle.Value.Kind != ZLinkBackendActorLifecycleEventKind.Disconnected)
                continue;

            var actorId = lifecycle.Value.Info.CurrentActor?.ActorId;
            if (actorId is null) continue;

            if (_actors.TryGetActor(actorId, out var actor) && actor is not null)
            {
                await NotifyActorDisconnectedCoreAsync(actor, cancellationToken)
                    .ConfigureAwait(false);
                continue;
            }

            await _runtime.NotifyActorDisconnectedByIdAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        await HandlerInvoker.InvokeTimerAsync(descriptor, tick, cancellationToken).ConfigureAwait(false);
    }

    private ValueTask PublishTimerFailureAsync(
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick,
        Exception exception,
        bool stopped,
        CancellationToken cancellationToken)
    {
        return _runtime.PublishRuntimeEventAsync(
            ZLinkSpotTimerFailureEventFactory.Create(
                SpotNodeName,
                SpotRid,
                false,
                descriptor,
                tick,
                exception,
                stopped),
            cancellationToken);
    }

    private static T RegisterWithoutSynchronizationContext<T>(Func<T> action)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            return action();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    private sealed class SpotCreateCallState(ZLinkMessage request)
    {
        public ZLinkMessage Request { get; } = request;

        public ZLinkSpotCreateResponse Response { get; set; }
    }
}
