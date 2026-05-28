namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkEntrySpotActivation
{
    private async ValueTask ExecuteAsync(
        Func<ZLinkEntrySpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (ReferenceEquals(Current.Value, this))
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            await operation(this, cancellationToken).ConfigureAwait(false);
            return;
        }

        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            await operation(this, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
            _gate.Release();
        }
    }

    private async ValueTask ExecuteAsync<TState>(
        Func<ZLinkEntrySpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (ReferenceEquals(Current.Value, this))
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            await operation(this, state, cancellationToken).ConfigureAwait(false);
            return;
        }

        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            await operation(this, state, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
            _gate.Release();
        }
    }

    private async ValueTask InvokeActorPacketWithoutLifecycleGateAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            await _handlerExecutor.InvokeActorPacketAsync(
                    descriptor,
                    actor,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
        }
    }

    private async ValueTask<ZLinkActorReply> InvokeActorPacketForReplyWithoutLifecycleGateAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            return await _handlerExecutor.InvokeActorPacketForReplyAsync(
                    descriptor,
                    actor,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
        }
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
            EntrySpot.GetType(),
            _stopSource.Token,
            async (descriptor, tick, ct) =>
            {
                using var _ = ZLinkSpotAmbientContext.Push(this);
                await _invoker.InvokeTimerAsync(descriptor, tick, ct).ConfigureAwait(false);
            },
            PublishTimerFailureAsync,
            cancellationToken);
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
                isEntrySpot: true,
                descriptor,
                tick,
                exception,
                stopped),
            cancellationToken);
    }

    public async ValueTask DispatchRouteDrainAsync(CancellationToken cancellationToken)
    {
        if (!await _routeDrainGate.WaitAsync(0, cancellationToken).ConfigureAwait(false))
        {
            return;
        }

        var dispatchTasks = new List<Task>();
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var received = Received.Create();
                if (!_nativeSpot.RecvRoute(received, RecvFlags.DontWait))
                {
                    received.Dispose();
                    break;
                }

                dispatchTasks.Add(DispatchRouteAsync(received, cancellationToken).AsTask());
            }

        }
        finally
        {
            _routeDrainGate.Release();
        }

        await Task.WhenAll(dispatchTasks).ConfigureAwait(false);
    }

    public async ValueTask DispatchActorJoinDrainAsync(CancellationToken cancellationToken)
    {
        if (!await _actorJoinDrainGate.WaitAsync(0, cancellationToken).ConfigureAwait(false))
        {
            return;
        }

        try
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            await _dispatcher.DispatchActorJoinDrainAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _actorJoinDrainGate.Release();
        }
    }

    public async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        using var _ = ZLinkSpotAmbientContext.Push(this);
        await _dispatcher
            .DispatchSubscriptionsAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchRouteAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        using (ZLinkSpotAmbientContext.Push(this))
        {
            await _dispatcher.DispatchRouteAsync(received, cancellationToken).ConfigureAwait(false);
        }
    }
}
