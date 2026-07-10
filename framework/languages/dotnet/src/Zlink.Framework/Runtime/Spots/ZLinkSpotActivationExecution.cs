namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
{
    public CancellationToken StopToken => _stopSource.Token;

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;

        _stopSource.Cancel();

        await _timers.DisposeAsync();
        await _serial.DisposeAsync();
        await _outbound.DisposeAsync();
        await NativeSpot.DisposeAsync();
        _stopSource.Dispose();
        await _scope.DisposeAsync();
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

    public IZLinkWorkerCall<TResult> RunWorker<TResult>(
        Func<CancellationToken, TResult> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        return new ZLinkWorkerCall<TResult>(
            _runtime.WorkerPool,
            work,
            callback => QueueSerialized((_, ct) => callback(ct)));
    }

    ValueTask<bool> IZLinkSpotContext.CloseAsync(CancellationToken cancellationToken)
    {
        return _runtime.CloseSpotAsync(SpotRid, cancellationToken);
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
                    if (dispatchable.Count == 0) return;

                    QueueSerialized(
                        static (activation, state, ct) => activation._dispatcher.DispatchActorPartsAsync(state, ct),
                        dispatchable);
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
        return ExecuteSerializedAsync(
            static (activation, ct) => activation.Spot.OnClosingAsync(ct),
            cancellationToken);
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

    private void QueueSerialized(Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation)
    {
        _serial.Queue(operation);
    }

    private void QueueSerialized<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state)
    {
        var capturedOp = operation;
        var capturedState = state;
        _serial.Queue((activation, ct) => capturedOp(activation, capturedState, ct));
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
