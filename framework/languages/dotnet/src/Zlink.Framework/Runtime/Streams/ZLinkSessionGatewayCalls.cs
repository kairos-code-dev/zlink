namespace Zlink.Framework.Runtime.Streams;

using System.Text.Json;

internal sealed class ZLinkSessionRequestCall<TRequest>(
    ZLinkSessionContext context,
    TRequest request) : IZLinkSessionRequestCall
{
    private static readonly ZlinkStreamPacketNameResolver PacketNameResolver = new();

    private string? _packetName = PacketNameResolver.Resolve(typeof(TRequest));
    private TimeSpan _timeout = TimeSpan.FromSeconds(30);

    public IZLinkSessionRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSessionRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        return context.RequestClientAsync<TRequest, TReply>(
            request,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            _timeout,
            cancellationToken);
    }
}

internal sealed class ZLinkActorRelay(
    IZLinkRoutedClient routedClient,
    string routerChannelId,
    RoutingId targetPlayNodeRid,
    string actorId,
    ZLinkSessionContext sessionContext) : IZLinkActorRelay
{
    public IZLinkActorRelaySendCall Send<TMessage>(TMessage message)
    {
        return new ZLinkActorRelaySendCall<TMessage>(
            routedClient,
            routerChannelId,
            targetPlayNodeRid,
            actorId,
            message);
    }

    public IZLinkActorRelayRequestCall Request<TRequest>(TRequest request)
    {
        return new ZLinkActorRelayRequestCall<TRequest>(
            routedClient,
            routerChannelId,
            targetPlayNodeRid,
            actorId,
            request);
    }

    public async ValueTask DispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        using (body)
        {
            var packet = ZLinkActorRelayPacket.Create(
                new ZLinkActorRelayEnvelope(actorId, header, header.RequestSeq is not null),
                body.AsReadOnlyMemory().ToArray());

            if (header.RequestSeq is not null)
            {
                var reply = await routedClient.RequestTo(routerChannelId, targetPlayNodeRid, packet)
                    .WithPacketName("ZLink.ActorRelay")
                    .WithTimeout(TimeSpan.FromSeconds(30))
                    .Async<byte[]>(cancellationToken)
                    .ConfigureAwait(false);

                await sessionContext.ReplyRawAsync(header, header.Codec, reply, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await routedClient.SendTo(routerChannelId, targetPlayNodeRid, packet)
                .WithPacketName("ZLink.ActorRelay")
                .Async(cancellationToken)
                .ConfigureAwait(false);
        }
    }
}

internal sealed class ZLinkActorRelaySendCall<TMessage>(
    IZLinkRoutedClient routedClient,
    string routerChannelId,
    RoutingId targetPlayNodeRid,
    string actorId,
    TMessage message) : IZLinkActorRelaySendCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkActorRelaySendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        var packetName = _packetName ?? throw new InvalidOperationException("Packet name is required.");
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            packetName,
            ZlinkStreamMetadata.Empty);
        var packet = ZLinkActorRelayPacket.Create(
            new ZLinkActorRelayEnvelope(actorId, header, false),
            JsonSerializer.SerializeToUtf8Bytes(message, message?.GetType() ?? typeof(TMessage), JsonOptions));
        return routedClient.SendTo(routerChannelId, targetPlayNodeRid, packet)
            .WithPacketName("ZLink.ActorRelay")
            .Async(cancellationToken);
    }
}

internal sealed class ZLinkActorRelayRequestCall<TRequest>(
    IZLinkRoutedClient routedClient,
    string routerChannelId,
    RoutingId targetPlayNodeRid,
    string actorId,
    TRequest request) : IZLinkActorRelayRequestCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan _timeout = TimeSpan.FromSeconds(30);

    public IZLinkActorRelayRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkActorRelayRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<Message> Async(CancellationToken cancellationToken = default)
    {
        var packetName = _packetName ?? throw new InvalidOperationException("Packet name is required.");
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            packetName,
            ZlinkStreamMetadata.Empty);
        var packet = ZLinkActorRelayPacket.Create(
            new ZLinkActorRelayEnvelope(actorId, header, true),
            JsonSerializer.SerializeToUtf8Bytes(request, request?.GetType() ?? typeof(TRequest), JsonOptions));
        var reply = await routedClient.RequestTo(routerChannelId, targetPlayNodeRid, packet)
            .WithPacketName("ZLink.ActorRelay")
            .WithTimeout(_timeout)
            .Async<byte[]>(cancellationToken)
            .ConfigureAwait(false);
        return Message.FromBytes(reply);
    }
}
