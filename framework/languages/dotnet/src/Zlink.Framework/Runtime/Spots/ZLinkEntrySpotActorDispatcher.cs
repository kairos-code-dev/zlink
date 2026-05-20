
namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkEntrySpotActorDispatcher
{
    private static readonly int MaxConcurrentDispatches = Math.Max(1, Environment.ProcessorCount);

    public static async Task DispatchAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        IReadOnlyList<ZLinkBackendActorPart> parts,
        CancellationToken cancellationToken)
    {
        var dispatchTasks = new ZLinkBoundedTaskSet(MaxConcurrentDispatches);
        int i = 0;
        while (i < parts.Count)
        {
            var headerPart = parts[i++];
            var actorState = runtime.GetOrCreateActorState(headerPart.Actor.ActorId);
            var actor = actorState.Actor;
            if (actor is null)
            {
                ZLinkSpotActorFrameReader.DisposeFrame(parts, ref i, headerPart);
                continue;
            }

            if (!ZLinkSpotActorFrameReader.TryRead(parts, ref i, headerPart, out var frame))
            {
                continue;
            }

            await dispatchTasks.AddAsync(
                    DispatchPacketAndDisposeBodyAsync(
                        runtime,
                        activation,
                        actorState,
                        actor,
                        frame.Header,
                        frame.Body,
                        cancellationToken))
                .ConfigureAwait(false);
        }

        await dispatchTasks.DrainAsync().ConfigureAwait(false);
    }

    private static async Task DispatchPacketAndDisposeBodyAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        ZLinkActorRuntimeState actorState,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        try
        {
            await DispatchPacketAsync(
                    runtime,
                    activation,
                    actorState,
                    actor,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            body.Dispose();
        }
    }

    private static async ValueTask DispatchPacketAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        ZLinkActorRuntimeState actorState,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (activation is not null
            && activation.TryResolveActorPacket(actor.GetType(), header, out var descriptor)
            && descriptor is not null)
        {
            await runtime.SubmitResolvedEntrySpotActorAsync(
                    actor,
                    actorState,
                    header,
                    ct => activation.InvokeActorPacketAsync(
                        descriptor,
                        actor,
                        header,
                        body,
                        ct),
                    cancellationToken)
                .ConfigureAwait(false);

            return;
        }

        await runtime.SubmitActorAsync(actor, header, body, cancellationToken)
            .ConfigureAwait(false);
    }
}
