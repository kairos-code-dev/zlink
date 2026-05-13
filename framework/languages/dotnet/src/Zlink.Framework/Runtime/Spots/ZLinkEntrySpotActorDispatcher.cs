using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkEntrySpotActorDispatcher
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();

    public static async Task DispatchAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        IReadOnlyList<ZLinkBackendActorPart> parts,
        CancellationToken cancellationToken)
    {
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
                using var emptyBody = Message.FromBytes(ReadOnlySpan<byte>.Empty);
                await DispatchPacketAsync(
                        runtime,
                        activation,
                        actor,
                        header,
                        emptyBody,
                        cancellationToken)
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
            using var body = bodyPart.Message;
            await DispatchPacketAsync(
                    runtime,
                    activation,
                    actor,
                    streamHeader,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static async ValueTask DispatchPacketAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (activation is not null
            && activation.TryResolveActorPacket(actor.GetType(), header, out var descriptor)
            && descriptor is not null)
        {
            var actorState = runtime.GetOrCreateActorState(actor.ActorId);
            var previousDispatch = actorState.CurrentDispatch;
            actorState.CurrentDispatch = new ZLinkActorDispatchState(header);
            try
            {
                await activation.InvokeActorPacketAsync(descriptor, actor, header, body, cancellationToken)
                    .ConfigureAwait(false);
            }
            finally
            {
                actorState.CurrentDispatch = previousDispatch;
            }

            return;
        }

        await runtime.SubmitActorAsync(actor, header, body, cancellationToken)
            .ConfigureAwait(false);
    }
}
