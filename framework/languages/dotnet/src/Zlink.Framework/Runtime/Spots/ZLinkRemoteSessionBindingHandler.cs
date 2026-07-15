using System.Text.Json;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkRemoteSessionBindingHandler
{
    public static async ValueTask<bool> TryHandleAsync(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZLinkSpotActorFrame frame,
        ZLinkActorBoundSessionDispatch boundSession,
        Action? acknowledgeHandledFrame,
        CancellationToken cancellationToken)
    {
        if (!string.Equals(
                frame.Header.Name,
                ZLinkRemoteSessionBindingProtocol.PacketName,
                StringComparison.Ordinal)) return false;

        if (frame.Header.Kind != ZlinkStreamMessageKind.Request
            || frame.Header.RequestSeq is null
            || state.NativeActorRef is not { } nativeActorRef)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                "Remote actor session binding requires a request and a concrete actor ref.");

        var request = JsonSerializer.Deserialize<ZLinkRemoteSessionBindRequest>(
                          frame.Body.AsReadOnlySpan(),
                          ZLinkJsonSerializerOptions.Default)
                      ?? throw new ZLinkFrameworkException(
                          ZLinkFrameworkErrorKind.PayloadDecodeFailed,
                          "Remote actor session binding payload was empty.");
        var sessionNodeRid = RoutingId.From(request.SessionNodeRid);
        if (frame.SourceNodeRid != sessionNodeRid)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                "Remote actor session binding source did not match the declared session node.");

        await runtime.BindRemoteBoundSessionRouteAsync(
                actor.ActorId,
                nativeActorRef,
                sessionNodeRid,
                RoutingId.From(request.SessionRid),
                cancellationToken)
            .ConfigureAwait(false);
        acknowledgeHandledFrame?.Invoke();
        await ZLinkActorBoundSessionRelay.SendReplyAsync(
                runtime,
                actor.ActorId,
                frame.ReplyActor,
                frame.SourceNodeRid,
                frame.SourceSessionRid,
                frame.RequestId,
                frame.Flags,
                boundSession.IsNoBind,
                frame.Header,
                AcknowledgedReply(),
                cancellationToken)
            .ConfigureAwait(false);
        await boundSession.DrainAsync(cancellationToken).ConfigureAwait(false);
        return true;
    }

    private static ZLinkActorReply AcknowledgedReply() => new(
        ZlinkStreamMessageKind.Response,
        ZlinkStreamCodec.Json,
        ZLinkEnvelopeCodec.EncodeJsonBytes(new ZLinkRemoteSessionBindResponse(true)),
        ZlinkStreamHeaderFlags.None,
        ZlinkStreamMetadata.Empty);
}
