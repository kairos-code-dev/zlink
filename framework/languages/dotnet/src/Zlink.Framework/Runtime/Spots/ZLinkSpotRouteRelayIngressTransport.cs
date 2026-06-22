using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRouteRelayIngressTransport(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration)
{
    public async Task HandleSendAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        CancellationToken cancellationToken)
    {
        var metadata = ZLinkRoutedSpotRelayPackets.DecodeMetadata(received);
        var targetNodeRid = runtime.ResolveAcceptedSpotRouteNodeRid(channelName);
        var targetSpotRid = RoutingId.From(metadata.TargetSpotRid);
        var spotParts = ZLinkRoutedSpotRelayPackets.CopySpotPayloadParts(received);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!router.SendToSpot(targetNodeRid, targetSpotRid, spotParts, SendFlags.None))
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Routed SPOT ingress channel '{channelName}' is not ready for SPOT send.");
            }
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(spotParts);
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    public async Task HandleRequestAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Routed SPOT relay request requires a source routing id.");
        try
        {
            var metadata = ZLinkRoutedSpotRelayPackets.DecodeMetadata(received);
            var targetNodeRid = runtime.ResolveAcceptedSpotRouteNodeRid(channelName);
            var targetSpotRid = RoutingId.From(metadata.TargetSpotRid);
            var timeout = header.Deadline is { } deadline
                ? deadline - DateTimeOffset.UtcNow
                : registration.DefaultRequestTimeout;
            if (timeout <= TimeSpan.Zero)
            {
                timeout = TimeSpan.FromMilliseconds(1);
            }

            var spotParts = ZLinkRoutedSpotRelayPackets.CopySpotPayloadParts(received);
            IReadOnlyList<Message> reply;
            try
            {
                reply = await RequestToSpotFromIngressRouterAsync(
                        channelName,
                        router,
                        targetNodeRid,
                        targetSpotRid,
                        spotParts,
                        timeout,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(spotParts);
            }

            ZLinkChannelReplyWriter.ReplyRawParts(
                router,
                sourceRid,
                received.RequestSeq,
                reply);
        }
        catch (Exception ex)
        {
            ZLinkChannelReplyWriter.ReplyEnvelope(
                router,
                sourceRid,
                received.RequestSeq,
                ZLinkChannelReplyWriter.CreateErrorHeader(channelName, header, ex),
                null,
                null);
        }
    }

    private static async ValueTask<IReadOnlyList<Message>> RequestToSpotFromIngressRouterAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        if (!router.RequestToSpot(
                targetNodeRid,
                targetSpotRid,
                parts,
                (result, reply) => ZLinkRawReplyCompletion.Complete(
                    result,
                    reply,
                    completion,
                    $"Routed SPOT ingress channel '{channelName}' request failed with result '{result}'."),
                SendFlags.None,
                timeout))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Routed SPOT ingress channel '{channelName}' is not ready for SPOT request.");
        }

        using var _ = cancellationToken.Register(
            static state => ((TaskCompletionSource<IReadOnlyList<Message>>)state!)
                .TrySetCanceled(),
            completion);

        return await completion.Task.ConfigureAwait(false);
    }
}
