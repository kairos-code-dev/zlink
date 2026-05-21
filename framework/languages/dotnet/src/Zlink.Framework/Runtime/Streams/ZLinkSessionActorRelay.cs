
namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorRelay(
    ZLinkFrameworkRuntime runtime)
{
    public async ValueTask DispatchRemoteAsync(
        ZLinkActorRef actorRef,
        ZLinkActorRoute route,
        ZlinkStreamHeader header,
        Message payload,
        Func<ZlinkStreamHeader, ZlinkStreamCodec, ReadOnlyMemory<byte>, CancellationToken, ValueTask> replyRawAsync,
        CancellationToken cancellationToken)
    {
        var routeClient = runtime.RouteClient as IZLinkMultipartRouteClient
            ?? throw new InvalidOperationException("Route client does not support multipart internal packets.");

        using (payload)
        {
            var parts = ZLinkInternalMultipartPackets.CreateActorDispatchParts(
                actorRef.ActorId,
                actorRef.ActorType,
                actorRef.SessionRouterId,
                actorRef.BindingToken,
                header,
                payload.AsReadOnlySpan());

            if (header.RequestSeq is not null)
            {
                var reply = await RequestActorReplyAsync(
                        routeClient,
                        actorRef,
                        route,
                        parts,
                        cancellationToken)
                    .ConfigureAwait(false);
                await replyRawAsync(header, header.Codec, reply, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await routeClient.SendPartsTo(
                    route.RouterChannelId,
                    route.TargetNodeRid,
                    ZLinkInternalPacketNames.ActorDispatch,
                    parts,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public async ValueTask NotifyDisconnectedAsync(
        ZLinkActorRef actorRef,
        CancellationToken cancellationToken)
    {
        var routeClient = runtime.RouteClient as IZLinkMultipartRouteClient
            ?? throw new InvalidOperationException("Route client does not support multipart internal packets.");

        var parts = ZLinkInternalMultipartPackets.CreateActorDisconnectedParts(
            actorRef.ActorId,
            actorRef.ActorType,
            actorRef.SessionRouterId,
            actorRef.BindingToken);
        var route = actorRef.RouteSnapshot;

        try
        {
            await routeClient.SendPartsTo(
                    route.RouterChannelId,
                    route.TargetNodeRid,
                    ZLinkInternalPacketNames.ActorDisconnected,
                    parts,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (InvalidOperationException ex)
        {
            if (!cancellationToken.IsCancellationRequested
                && !ex.Message.Contains("runtime is not started", StringComparison.Ordinal))
            {
                throw;
            }
        }
    }

    private async ValueTask<ReadOnlyMemory<byte>> RequestActorReplyAsync(
        IZLinkMultipartRouteClient routeClient,
        ZLinkActorRef actorRef,
        ZLinkActorRoute route,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        try
        {
            return await routeClient.RequestPartsTo<ReadOnlyMemory<byte>>(
                    route.RouterChannelId,
                    route.TargetNodeRid,
                    ZLinkInternalPacketNames.ActorDispatch,
                    parts,
                    runtime.Registration.DefaultTimeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorDispatchTimeout,
                $"Actor dispatch request for '{actorRef.ActorId}' timed out.",
                innerException: ex);
        }
        catch (ZLinkFrameworkException)
        {
            throw;
        }
        catch (Exception ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorDispatchHandlerFailed,
                $"Actor dispatch request for '{actorRef.ActorId}' failed: {ex.Message}",
                innerException: ex);
        }
    }
}
