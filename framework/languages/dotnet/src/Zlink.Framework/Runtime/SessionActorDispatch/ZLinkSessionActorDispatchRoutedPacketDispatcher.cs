using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.SessionActorDispatch;

internal sealed class ZLinkSessionActorDispatchRoutedPacketDispatcher(IServiceProvider services)
    : IZLinkRoutedInternalPacketDispatcher
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public bool CanHandleSend(string packetName)
    {
        return packetName is ZLinkInternalPacketNames.ActorDispatch
            or ZLinkInternalPacketNames.SessionProxy;
    }

    public bool CanHandleRequest(string packetName)
    {
        return packetName is ZLinkInternalPacketNames.ActorCreate
            or ZLinkInternalPacketNames.ActorDispatch
            or ZLinkInternalPacketNames.SessionProxy;
    }

    public async ValueTask DispatchSendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts[0]);
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
            ZLinkInternalPacketNames.ActorCreate => await DispatchActorCreateAsync(received, cancellationToken)
                .ConfigureAwait(false),
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

    private async ValueTask<byte[]> DispatchActorCreateAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var packet = DecodeRequired<ZLinkActorCreatePacket>(received, "Actor create packet body is null.");
        var runtime = services.GetRequiredService<ZLinkFrameworkRuntime>();
        var result = await runtime.CreateLocalActorAsync(packet.ActorId, packet.ActorType, cancellationToken)
            .ConfigureAwait(false);
        var reply = new ZLinkActorCreateResult(result.Actor.ActorId, packet.ActorType);
        return JsonSerializer.SerializeToUtf8Bytes(reply, JsonOptions);
    }

    private async ValueTask DispatchActorDispatchSendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var packet = DecodeRequired<ZLinkActorDispatchPacket>(received, "Actor dispatch packet body is null.");
        using var body = Message.FromBytes(packet.Body);
        var runtime = services.GetRequiredService<ZLinkFrameworkRuntime>();
        await runtime.SubmitActorByIdAsync(
            packet.ActorId,
            packet.StreamHeader.ToHeader(),
            body,
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<byte[]> DispatchActorDispatchRequestAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var packet = DecodeRequired<ZLinkActorDispatchPacket>(received, "Actor dispatch packet body is null.");
        using var body = Message.FromBytes(packet.Body);
        var runtime = services.GetRequiredService<ZLinkFrameworkRuntime>();
        return await runtime.SubmitActorForReplyAsync(
            packet.ActorId,
            packet.StreamHeader.ToHeader(),
            body,
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask DispatchSessionProxySendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var packet = DecodeRequired<ZLinkSessionProxyPacket>(received, "Session proxy packet body is null.");
        var sessionContext = ResolveSessionContext(packet.Envelope);
        await sessionContext.SendRawAsync(
            packet.Envelope.PacketName,
            ZlinkStreamCodec.Json,
            packet.Body,
            cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<byte[]> DispatchSessionProxyRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        var packet = DecodeRequired<ZLinkSessionProxyPacket>(received, "Session proxy packet body is null.");
        var sessionContext = ResolveSessionContext(packet.Envelope);
        using var reply = await sessionContext.RequestRawAsync(
            packet.Envelope.PacketName,
            ZlinkStreamCodec.Json,
            packet.Body,
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

    private static TPacket DecodeRequired<TPacket>(
        Received received,
        string failureMessage)
        where TPacket : class
    {
        return (TPacket?)ZLinkEnvelopeCodec.DecodeBody(received.Parts[0], typeof(TPacket))
            ?? throw new InvalidOperationException(failureMessage);
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
}
