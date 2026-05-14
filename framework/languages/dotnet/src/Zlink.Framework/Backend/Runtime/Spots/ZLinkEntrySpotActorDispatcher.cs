using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkEntrySpotActorDispatcher
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();
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
                headerPart.Message.Dispose();
                while (i < parts.Count && parts[i - 1].More)
                {
                    parts[i++].Message.Dispose();
                }
                continue;
            }

            if (!headerPart.More)
            {
                var header = HeaderCodec.Decode(headerPart.Message.AsReadOnlyMemory());
                headerPart.Message.Dispose();
                var emptyBody = Message.FromBytes(ReadOnlySpan<byte>.Empty);
                await dispatchTasks.AddAsync(
                        DispatchPacketAndDisposeBodyAsync(
                            runtime,
                            activation,
                            actorState,
                            actor,
                            header,
                            emptyBody,
                            cancellationToken))
                    .ConfigureAwait(false);
                continue;
            }

            if (i >= parts.Count)
            {
                headerPart.Message.Dispose();
                continue;
            }

            var bodyPart = parts[i++];
            var streamHeader = HeaderCodec.Decode(headerPart.Message.AsReadOnlyMemory());
            headerPart.Message.Dispose();
            await dispatchTasks.AddAsync(
                    DispatchPacketAndDisposeBodyAsync(
                        runtime,
                        activation,
                        actorState,
                        actor,
                        streamHeader,
                        bodyPart.Message,
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
