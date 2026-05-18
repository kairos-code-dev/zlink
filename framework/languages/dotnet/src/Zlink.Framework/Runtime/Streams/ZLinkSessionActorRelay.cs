
namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorRelay(
    ZLinkFrameworkRuntime runtime)
{
    public async ValueTask DispatchRemoteAsync(
        ZLinkActorRef actorRef,
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
                header,
                payload.AsReadOnlySpan());

            if (header.RequestSeq is not null)
            {
                var reply = await RequestActorReplyAsync(
                        routeClient,
                        actorRef,
                        parts,
                        cancellationToken)
                    .ConfigureAwait(false);
                await replyRawAsync(header, header.Codec, reply, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await routeClient.SendPartsTo(
                    actorRef.RouterChannelId,
                    actorRef.TargetNodeRid,
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
            actorRef.ActorType);

        await routeClient.SendPartsTo(
                actorRef.RouterChannelId,
                actorRef.TargetNodeRid,
                ZLinkInternalPacketNames.ActorDisconnected,
                parts,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ReadOnlyMemory<byte>> RequestActorReplyAsync(
        IZLinkMultipartRouteClient routeClient,
        ZLinkActorRef actorRef,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        try
        {
            return await routeClient.RequestPartsTo<ReadOnlyMemory<byte>>(
                    actorRef.RouterChannelId,
                    actorRef.TargetNodeRid,
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
