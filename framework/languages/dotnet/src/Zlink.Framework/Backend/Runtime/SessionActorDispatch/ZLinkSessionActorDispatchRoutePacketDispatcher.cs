using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.SessionActorDispatch;

internal sealed class ZLinkSessionActorDispatchRoutePacketDispatcher(IServiceProvider services)
    : IZLinkRouteInternalPacketDispatcher
{
    private static readonly ZlinkStreamHeaderCodec HeaderCodec = new();

    public bool CanHandleSend(string packetName)
    {
        return packetName is ZLinkInternalPacketNames.ActorDispatch
            or ZLinkInternalPacketNames.SessionProxy;
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
            case ZLinkInternalPacketNames.SessionProxy:
                await DispatchSessionProxySendAsync(received, cancellationToken).ConfigureAwait(false);
                return;
            default:
                throw new InvalidOperationException($"Internal routed send packet '{header.MessageName}' is not supported.");
        }
    }

    public async ValueTask<byte[]> DispatchRequestAsync(
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
        var metadata = DecodeActorDispatchMetadata(received);
        var streamHeader = DecodeActorDispatchStreamHeader(received);
        using var body = DecodeActorDispatchBody(received);
        var runtime = services.GetRequiredService<ZLinkFrameworkRuntime>();
        await EnsureLocalActorAsync(runtime, metadata, cancellationToken).ConfigureAwait(false);
        await runtime.SubmitActorByIdAsync(
            metadata.ActorId,
            streamHeader,
            body,
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<byte[]> DispatchActorDispatchRequestAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var metadata = DecodeActorDispatchMetadata(received);
        var streamHeader = DecodeActorDispatchStreamHeader(received);
        using var body = DecodeActorDispatchBody(received);
        var runtime = services.GetRequiredService<ZLinkFrameworkRuntime>();
        await EnsureLocalActorAsync(runtime, metadata, cancellationToken).ConfigureAwait(false);
        return await runtime.SubmitActorForReplyAsync(
            metadata.ActorId,
            streamHeader,
            body,
            cancellationToken).ConfigureAwait(false);
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
        var envelope = DecodeSessionProxyEnvelope(received);
        try
        {
            var sessionContext = ResolveSessionContext(envelope);
            await sessionContext.SendRawAsync(
                envelope.PacketName,
                ZlinkStreamCodec.Json,
                DecodeSessionProxyBody(received),
                cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (IsClosedSessionWriteFailure(ex))
        {
        }
    }

    private async ValueTask<byte[]> DispatchSessionProxyRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        var envelope = DecodeSessionProxyEnvelope(received);
        var sessionContext = ResolveSessionContext(envelope);
        using var reply = await sessionContext.RequestRawAsync(
            envelope.PacketName,
            ZlinkStreamCodec.Json,
            DecodeSessionProxyBody(received),
            ResolveInternalTimeout(routedHeader),
            cancellationToken).ConfigureAwait(false);
        return reply.AsReadOnlyMemory().ToArray();
    }

    private ZLinkSessionContext ResolveSessionContext(ZLinkSessionProxyEnvelope envelope)
    {
        var runtime = services.GetRequiredService<ZLinkFrameworkRuntime>();
        if (runtime.TryGetSessionActorContext(
                envelope.ActorId,
                envelope.BindingToken,
                out var sessionContext))
        {
            return sessionContext;
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorSessionNotBound,
            $"No current session binding exists for actor '{envelope.ActorId}'.");
    }

    private static ZLinkActorDispatchMetadata DecodeActorDispatchMetadata(Received received)
    {
        EnsurePartCount(received, 4, "actor dispatch");
        return ZLinkEnvelopeCodec.DecodePart<ZLinkActorDispatchMetadata>(received.Parts[1]);
    }

    private static ZlinkStreamHeader DecodeActorDispatchStreamHeader(Received received)
    {
        EnsurePartCount(received, 4, "actor dispatch");
        return HeaderCodec.Decode(received.Parts[2].AsReadOnlyMemory());
    }

    private static Message DecodeActorDispatchBody(Received received)
    {
        EnsurePartCount(received, 4, "actor dispatch");
        return Message.FromBytes(received.Parts[3].AsReadOnlySpan());
    }

    private static ZLinkSessionProxyEnvelope DecodeSessionProxyEnvelope(Received received)
    {
        EnsurePartCount(received, 3, "session proxy");
        return ZLinkEnvelopeCodec.DecodePart<ZLinkSessionProxyEnvelope>(received.Parts[1]);
    }

    private static ReadOnlyMemory<byte> DecodeSessionProxyBody(Received received)
    {
        EnsurePartCount(received, 3, "session proxy");
        return received.Parts[2].AsReadOnlyMemory();
    }

    private static void EnsurePartCount(Received received, int minimumCount, string packetName)
    {
        if (received.Parts.Count < minimumCount)
        {
            throw new InvalidOperationException(
                $"Internal routed {packetName} packet requires at least {minimumCount} message parts.");
        }
    }

    private TimeSpan ResolveInternalTimeout(ZLinkEnvelopeHeader header)
    {
        if (header.Deadline is not { } deadline)
        {
            return services.GetRequiredService<ZLinkFrameworkRegistration>().DefaultTimeout;
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
