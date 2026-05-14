
namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
{
    public CancellationToken StopToken => _stopSource.Token;

    public async ValueTask InitializeAsync(CancellationToken cancellationToken)
    {
        RegisterWithoutSynchronizationContext(() =>
        {
            ZLinkSpotNativeDispatchRouter.Attach(
                NativeSpot,
                routeReadable: () => QueueSerialized(
                    static (activation, ct) => activation._dispatcher.DispatchRouteDrainAsync(ct)),
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
            static (activation, ct) => activation.Spot.OnInitializeAsync(ct),
            cancellationToken);
    }

    public ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken)
        where THandler : class
    {
        return _timers.AddAsync(
            name,
            period,
            typeof(THandler),
            Spot.GetType(),
            StopToken,
            (descriptor, ct) => ExecuteSerializedAsync(
                async static (activation, state, innerCt) =>
                {
                    await activation.InvokeTimerAsync(state, innerCt);
                },
                descriptor,
                ct),
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

        await _serial.DisposeAsync();
        await _timers.DisposeAsync();
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
        CancellationToken cancellationToken)
    {
        await HandlerInvoker.InvokeTimerAsync(descriptor, cancellationToken).ConfigureAwait(false);
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
