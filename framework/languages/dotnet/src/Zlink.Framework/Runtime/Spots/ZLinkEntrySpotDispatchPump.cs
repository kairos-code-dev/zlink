using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkEntrySpotDispatchPump(
    ZLinkFrameworkRuntime runtime,
    ZLinkEntrySpotActivation? activation,
    ZLinkRuntimeTaskRunner taskRunner)
{
    public void Attach(IZLinkBackendSpot entrySpot)
    {
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
        {
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
                case ZLinkBackendSpotDispatchEvent.ActorJoinReadable:
                    taskRunner.RunDetached(
                        "entry-spot-actor-join-dispatch",
                        ct => activation.DispatchActorJoinDrainAsync(ct));
                    return;
            }
        }

        if (info.Event != ZLinkBackendSpotDispatchEvent.ActorReadable
            || info.ActorParts is not { Count: > 0 } actorParts)
        {
            return;
        }

        taskRunner.RunDetached(
            "entry-spot-actor-dispatch",
            ct => new ValueTask(ZLinkEntrySpotActorDispatcher.DispatchAsync(
                runtime,
                activation,
                actorParts,
                ct)));
    }
}
