using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActorRelay(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream)
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();

    public ValueTask DispatchAttachedAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var node = runtime.GetActorSpotNode();
        var actorState = runtime.GetOrCreateActorState(actor.ActorId);
        if (node is not null
            && actorState.NativeActorRef is not null
            && stream is ZLinkManagedStream managedStream)
        {
            using var encodedHeader = Message.FromBytes(HeaderCodec.Encode(header).Span);
            using (body)
            {
                managedStream.SendBoundActor(actor.ActorId, [encodedHeader, body]);
            }

            return ValueTask.CompletedTask;
        }

        return runtime.SubmitActorAsync(actor, header, body, cancellationToken);
    }

    public async ValueTask DispatchRemoteAsync(
        ZLinkActorRef actorRef,
        ZlinkStreamHeader header,
        Message body,
        Func<ZlinkStreamHeader, ZlinkStreamCodec, ReadOnlyMemory<byte>, CancellationToken, ValueTask> replyRawAsync,
        CancellationToken cancellationToken)
    {
        var routeClient = runtime.RouteClient as IZLinkMultipartRouteClient
            ?? throw new InvalidOperationException("Route client does not support multipart internal packets.");

        using (body)
        {
            var parts = ZLinkInternalMultipartPackets.CreateActorDispatchParts(
                actorRef.ActorId,
                actorRef.ActorType,
                header,
                body.AsReadOnlySpan());

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

    private async ValueTask<byte[]> RequestActorReplyAsync(
        IZLinkMultipartRouteClient routeClient,
        ZLinkActorRef actorRef,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        try
        {
            return await routeClient.RequestPartsTo<byte[]>(
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
