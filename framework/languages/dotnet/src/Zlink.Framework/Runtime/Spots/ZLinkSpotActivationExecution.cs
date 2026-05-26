
namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
{
    public CancellationToken StopToken => _stopSource.Token;

    public async ValueTask InitializeAsync(
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        RegisterWithoutSynchronizationContext(() =>
        {
            ZLinkSpotNativeDispatchRouter.Attach(
                NativeSpot,
                routeReadable: receivedMessages =>
                {
                    if (receivedMessages.Count == 0)
                    {
                        QueueSerialized(
                            static (activation, ct) => activation._dispatcher.DispatchRouteDrainAsync(ct));
                        return;
                    }

                    foreach (var received in receivedMessages)
                    {
                        QueueSerialized(
                            static (activation, state, ct) => activation._dispatcher.DispatchRouteAsync(state, ct),
                            received);
                    }
                },
                channelReplyReadable: drain => drain?.Invoke(),
                actorJoinReadable: () => QueueSerialized(
                    static (activation, ct) => activation._dispatcher.DispatchActorJoinDrainAsync(ct)),
                actorPartsReadable: actorParts => QueueSerialized(
                    static (activation, state, ct) => activation._dispatcher.DispatchActorPartsAsync(state, ct),
                    actorParts));

            return 0;
        });

        _subscriptionPump.StartIfNeeded(
            _subscriptions.HasSubscriptions,
            StopToken,
            ct => ExecuteSerializedAsync(
                static (activation, innerCt) => activation.DispatchSubscriptionsAsync(innerCt),
                ct));
        await ExecuteSerializedAsync(
            static async (activation, state, ct) =>
            {
                await activation.Spot.OnCreateAsync(state, ct);
                await activation.Spot.OnInitializeAsync(ct);
            },
            createParts,
            cancellationToken);
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

    public async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await _actorDispatchSubmitter.SubmitAsync(
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<byte[]> SubmitActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return await _actorDispatchSubmitter
            .SubmitForReplyAsync(actor, runtimeState, header, body, cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
            static (activation, ct) => activation.Spot.OnClosingAsync(ct),
            cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _stopSource.Cancel();
        await _subscriptionPump.StopAsync();

        await _timers.DisposeAsync();
        await _serial.DisposeAsync();
        await _outbound.DisposeAsync();
        await NativeSpot.DisposeAsync();
        _stopSource.Dispose();
        await _scope.DisposeAsync();
    }

    private async ValueTask ExecuteSerializedAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        await _serial.ExecuteAsync(operation, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask ExecuteSerializedAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        await _serial.ExecuteAsync(operation, state, cancellationToken).ConfigureAwait(false);
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
                isEntrySpot: false,
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
}
