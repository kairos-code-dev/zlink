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
