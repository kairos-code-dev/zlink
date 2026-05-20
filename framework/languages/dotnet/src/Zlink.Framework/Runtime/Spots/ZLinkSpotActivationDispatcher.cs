using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActivationDispatcher(
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendSpot nativeSpot,
    string channelName,
    ZLinkSpotPacketRegistry packets,
    ZLinkSpotActorJoinRegistry actorJoins,
    ZLinkSpotActorMembership actors,
    ZLinkSpotSubscriptionRegistry subscriptions,
    Func<ZLinkSpotActorHandlerRegistry?> actorHandlers,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker)
{
    private readonly ZLinkSpotActorPacketDispatcher _actorPacketDispatcher =
        new(runtime, actorHandlers, handlerInvoker);
    private readonly ZLinkSpotActorJoinDispatcher _actorJoinDispatcher =
        new(runtime, nativeSpot, channelName, actorJoins, actors, handlerInvoker);
    private readonly ZLinkSpotRouteDispatcher _routeDispatcher =
        new(channelName, packets, handlerInvoker);

    public async ValueTask DispatchActorJoinDrainAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            ZLinkBackendActorJoinRequest? request;
            try
            {
                request = nativeSpot.RecvActorJoin(RecvFlags.DontWait);
            }
            catch (ZlinkRecvException ex)
                when (ex.Result == ZlinkRecvException.ErrorCode.NoData)
            {
                return;
            }

            if (request is null)
            {
                return;
            }

            try
            {
                await _actorJoinDispatcher.DispatchAsync(request, cancellationToken).ConfigureAwait(false);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(request.Parts);
            }
        }
    }

    public async ValueTask DispatchActorPartsAsync(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        CancellationToken cancellationToken)
    {
        int i = 0;
        while (i < parts.Count)
        {
            var headerPart = parts[i++];
            if (!actors.TryGetActor(headerPart.Actor.ActorId, out var actor) || actor is null)
            {
                headerPart.Message.Dispose();
                DisposeContinuationParts(parts, ref i, headerPart.More);
                continue;
            }

            var streamHeader = ZLinkStreamProtocolDefaults.DecodeHeader(headerPart.Message.AsReadOnlyMemory());
            headerPart.Message.Dispose();

            var body = TakeBodyPart(parts, ref i, headerPart.More);
            if (body is null)
            {
                continue;
            }

            using (body)
            {
                await DispatchActorStreamPartAsync(
                        actor,
                        headerPart.Actor.ActorId,
                        streamHeader,
                        body,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
        }
    }

    public async ValueTask DispatchActorPacketAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await _actorPacketDispatcher.DispatchAsync(
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<byte[]?> DispatchActorPacketForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return await _actorPacketDispatcher.DispatchForReplyAsync(
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask DispatchRouteDrainAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            var received = new Received();
            if (!nativeSpot.RecvRoute(received, RecvFlags.DontWait))
            {
                received.Dispose();
                return;
            }

            await _routeDispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
        }
    }

    public async ValueTask DispatchRouteAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        await _routeDispatcher.DispatchAsync(received, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        await subscriptions
            .DrainAsync(nativeSpot, InvokeSubscriptionAsync, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchActorStreamPartAsync(
        IZLinkActor actor,
        string actorId,
        ZlinkStreamHeader streamHeader,
        Message body,
        CancellationToken cancellationToken)
    {
        var runtimeState = runtime.GetOrCreateActorState(actorId);
        await _actorPacketDispatcher.DispatchAsync(
                actor,
                runtimeState,
                streamHeader,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await handlerInvoker().InvokeSubscriptionAsync(descriptor, message, cancellationToken).ConfigureAwait(false);
    }

    private static Message? TakeBodyPart(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        ref int index,
        bool hasBody)
    {
        if (!hasBody)
        {
            return Message.FromBytes(ReadOnlySpan<byte>.Empty);
        }

        if (index >= parts.Count)
        {
            return null;
        }

        return parts[index++].Message;
    }

    private static void DisposeContinuationParts(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        ref int index,
        bool hasMore)
    {
        while (hasMore && index < parts.Count)
        {
            var part = parts[index++];
            hasMore = part.More;
            part.Message.Dispose();
        }
    }

}
