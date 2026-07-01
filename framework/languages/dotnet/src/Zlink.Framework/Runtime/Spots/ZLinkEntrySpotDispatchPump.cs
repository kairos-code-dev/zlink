namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkEntrySpotDispatchPump(
    ZLinkFrameworkRuntime runtime,
    ZLinkEntrySpotActivation? activation,
    ZLinkRuntimeTaskRunner taskRunner)
{
    private IZLinkBackendSpot? _entrySpot;

    public void Attach(IZLinkBackendSpot entrySpot)
    {
        _entrySpot = entrySpot;
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            entrySpot.OnDispatchEvent(OnDispatchEvent);
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    private void OnDispatchEvent(ZLinkBackendSpotDispatchInfo info)
    {
        if (activation is not null)
            switch (info.Event)
            {
                case ZLinkBackendSpotDispatchEvent.RouteReadable:
                    taskRunner.RunDetached(
                        "entry-spot-route-dispatch",
                        ct => activation.DispatchRouteDrainAsync(ct));
                    return;
                case ZLinkBackendSpotDispatchEvent.ChannelReplyReadable:
                    info.DrainChannelReply?.Invoke();
                    return;
                case ZLinkBackendSpotDispatchEvent.SubscribeReadable:
                    taskRunner.RunDetached(
                        "entry-spot-subscription-dispatch",
                        ct => activation.DispatchSubscriptionsAsync(ct));
                    return;
                case ZLinkBackendSpotDispatchEvent.ActorJoinReadable:
                    taskRunner.RunDetached(
                        "entry-spot-actor-join-dispatch",
                        ct => activation.DispatchActorJoinDrainAsync(ct));
                    return;
                case ZLinkBackendSpotDispatchEvent.ActorLifecycleReadable:
                    taskRunner.RunDetached(
                        "entry-spot-actor-lifecycle-dispatch",
                        DispatchActorLifecycleDrainAsync);
                    return;
            }

        if (info.Event != ZLinkBackendSpotDispatchEvent.ActorReadable
            || info.ActorParts is not { Count: > 0 } actorParts)
            return;

        taskRunner.RunDetached(
            "entry-spot-actor-dispatch",
            ct => new ValueTask(ZLinkEntrySpotActorDispatcher.DispatchAsync(
                runtime,
                activation,
                actorParts,
                ct)));
    }

    private async ValueTask DispatchActorLifecycleDrainAsync(CancellationToken cancellationToken)
    {
        if (_entrySpot is not { } entrySpot) return;

        while (true)
        {
            var lifecycle = entrySpot.RecvActorLifecycle(RecvFlags.DontWait);
            if (lifecycle is null) return;
            if (lifecycle.Value.Kind != ZLinkBackendActorLifecycleEventKind.Disconnected)
                continue;

            var actorId = lifecycle.Value.Info.CurrentActor?.ActorId;
            if (actorId is null) continue;

            if (!await runtime.TryNotifyJoinedSpotActorDisconnectedAsync(actorId, cancellationToken)
                    .ConfigureAwait(false))
                await runtime.NotifyActorDisconnectedByIdAsync(actorId, cancellationToken)
                    .ConfigureAwait(false);
        }
    }
}
