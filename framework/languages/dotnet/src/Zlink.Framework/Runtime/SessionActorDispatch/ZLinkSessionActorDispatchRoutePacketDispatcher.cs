namespace Zlink.Framework.Runtime.SessionActorDispatch;

internal sealed class ZLinkSessionActorDispatchRoutePacketDispatcher(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration)
    : IZLinkRouteInternalPacketDispatcher
{
    public bool CanHandleSend(string packetName)
    {
        return packetName is ZLinkInternalPacketNames.ActorDispatch
            or ZLinkInternalPacketNames.ActorDisconnected
            or ZLinkInternalPacketNames.SessionProxy
            or ZLinkInternalPacketNames.SessionDisconnect;
    }

    public bool CanHandleRequest(string packetName)
    {
        return packetName is ZLinkInternalPacketNames.ActorDispatch
            or ZLinkInternalPacketNames.SessionProxy;
    }

    public async ValueTask DispatchSendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
        switch (header.MessageName)
        {
            case ZLinkInternalPacketNames.ActorDispatch:
                await DispatchActorDispatchSendAsync(received, cancellationToken).ConfigureAwait(false);
                return;
            case ZLinkInternalPacketNames.ActorDisconnected:
                await DispatchActorDisconnectedAsync(received, cancellationToken).ConfigureAwait(false);
                return;
            case ZLinkInternalPacketNames.SessionProxy:
                await DispatchSessionProxySendAsync(received, cancellationToken).ConfigureAwait(false);
                return;
            case ZLinkInternalPacketNames.SessionDisconnect:
                await DispatchSessionDisconnectAsync(received, cancellationToken).ConfigureAwait(false);
                return;
            default:
                throw new InvalidOperationException($"Internal routed send packet '{header.MessageName}' is not supported.");
        }
    }

    public async ValueTask<Message> DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        return routedHeader.MessageName switch
        {
            ZLinkInternalPacketNames.ActorDispatch => await DispatchActorDispatchRequestAsync(received, cancellationToken)
                .ConfigureAwait(false),
            ZLinkInternalPacketNames.SessionProxy => await DispatchSessionProxyRequestAsync(
                    received,
                    routedHeader,
                    cancellationToken)
                .ConfigureAwait(false),
            _ => throw new InvalidOperationException(
                $"Internal routed request packet '{routedHeader.MessageName}' is not supported.")
        };
    }

    private async ValueTask DispatchActorDispatchSendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var metadata = ZLinkInternalMultipartPackets.DecodeActorDispatchMetadata(received);
        var streamHeader = ZLinkInternalMultipartPackets.DecodeActorDispatchStreamHeader(received);
        using var body = ZLinkInternalMultipartPackets.DecodeActorDispatchBody(received);
        await EnsureLocalActorAsync(runtime, metadata, cancellationToken).ConfigureAwait(false);
        await runtime.SubmitActorByIdAsync(
            metadata.ActorId,
            streamHeader,
            body,
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask DispatchActorDisconnectedAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var metadata = ZLinkInternalMultipartPackets.DecodeActorDisconnectedMetadata(received);
        await EnsureLocalActorAsync(runtime, metadata, cancellationToken).ConfigureAwait(false);
        await runtime.NotifyActorDisconnectedByIdAsync(metadata.ActorId, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<Message> DispatchActorDispatchRequestAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var metadata = ZLinkInternalMultipartPackets.DecodeActorDispatchMetadata(received);
        var streamHeader = ZLinkInternalMultipartPackets.DecodeActorDispatchStreamHeader(received);
        using var body = ZLinkInternalMultipartPackets.DecodeActorDispatchBody(received);
        await EnsureLocalActorAsync(runtime, metadata, cancellationToken).ConfigureAwait(false);
        var reply = await runtime.SubmitActorForReplyAsync(
                metadata.ActorId,
                streamHeader,
                body,
                cancellationToken)
            .ConfigureAwait(false);
        return Message.FromBytes(reply);
    }

    private static async ValueTask EnsureLocalActorAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkActorDispatchMetadata metadata,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(metadata.ActorType))
        {
            return;
        }

        await runtime.CreateLocalActorAsync(metadata.ActorId, metadata.ActorType, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchSessionProxySendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var envelope = ZLinkInternalMultipartPackets.DecodeSessionProxyEnvelope(received);
        try
        {
            var sessionContext = ResolveSessionContext(envelope);
            await sessionContext.SendRawAsync(
                envelope.PacketName,
                ZlinkStreamCodec.Json,
                ZLinkInternalMultipartPackets.DecodeSessionProxyBody(received),
                cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (IsClosedSessionWriteFailure(ex))
        {
        }
    }

    private async ValueTask<Message> DispatchSessionProxyRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        var envelope = ZLinkInternalMultipartPackets.DecodeSessionProxyEnvelope(received);
        var sessionContext = ResolveSessionContext(envelope);
        return await sessionContext.RequestRawAsync(
            envelope.PacketName,
            ZlinkStreamCodec.Json,
            ZLinkInternalMultipartPackets.DecodeSessionProxyBody(received),
            ResolveInternalTimeout(routedHeader),
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask DispatchSessionDisconnectAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var envelope = ZLinkInternalMultipartPackets.DecodeSessionDisconnectEnvelope(received);
        var sessionContext = ResolveSessionContext(envelope.ActorId, envelope.BindingToken);
        await sessionContext.CloseByProxyAsync(cancellationToken).ConfigureAwait(false);
    }

    private ZLinkSessionContext ResolveSessionContext(ZLinkSessionProxyEnvelope envelope)
        => ResolveSessionContext(envelope.ActorId, envelope.BindingToken);

    private ZLinkSessionContext ResolveSessionContext(
        string actorId,
        string bindingToken)
    {
        if (runtime.TryGetSessionActorContext(
                actorId,
                bindingToken,
                out var sessionContext))
        {
            return sessionContext;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorSessionNotBound,
            $"No current session binding exists for actor '{actorId}'.");
    }

    private TimeSpan ResolveInternalTimeout(ZLinkEnvelopeHeader header)
    {
        if (header.Deadline is not { } deadline)
        {
            return registration.DefaultTimeout;
        }

        var remaining = deadline - DateTimeOffset.UtcNow;
        return remaining > TimeSpan.Zero
            ? remaining
            : TimeSpan.Zero;
    }

    private static bool IsClosedSessionWriteFailure(Exception exception)
    {
        return exception is ObjectDisposedException
            or ZlinkCloseException
            || exception is ZLinkFrameworkException
            {
                Kind: ZLinkFrameworkErrorKind.ActorSessionNotBound
                    or ZLinkFrameworkErrorKind.SessionRouteNotFound
            }
            || exception is ZlinkSubmitException
            {
                Result: ZlinkSubmitException.ErrorCode.NotConnected
                    or ZlinkSubmitException.ErrorCode.Terminated
                    or ZlinkSubmitException.ErrorCode.InvalidHandle
            };
    }
}
