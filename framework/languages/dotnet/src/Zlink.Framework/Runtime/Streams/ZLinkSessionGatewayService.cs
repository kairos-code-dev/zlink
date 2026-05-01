namespace Zlink.Framework.Runtime.Streams;

using System.Text.Json;

internal sealed class ZLinkSessionGatewayService(IZLinkRoutedClient routedClient) : IZLinkSessionGateway
{
    public IZLinkSessionGatewaySendCall SendToActor<TMessage>(
        string routerChannelId,
        RoutingId targetSessionNodeRid,
        string actorId,
        TMessage message)
    {
        return new ZLinkSessionGatewaySendCall<TMessage>(
            routedClient,
            routerChannelId,
            targetSessionNodeRid,
            actorId,
            message);
    }

    public IZLinkSessionGatewayRequestCall RequestActor<TRequest>(
        string routerChannelId,
        RoutingId targetSessionNodeRid,
        string actorId,
        TRequest request)
    {
        return new ZLinkSessionGatewayRequestCall<TRequest>(
            routedClient,
            routerChannelId,
            targetSessionNodeRid,
            actorId,
            request);
    }
}

internal sealed class ZLinkSessionGatewaySendCall<TMessage>(
    IZLinkRoutedClient routedClient,
    string routerChannelId,
    RoutingId targetSessionNodeRid,
    string actorId,
    TMessage message) : IZLinkSessionGatewaySendCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkSessionGatewaySendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        var packet = new ZLinkSessionGatewayPacket(
            new ZLinkSessionGatewayEnvelope(
                actorId,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                false),
            JsonSerializer.SerializeToUtf8Bytes(message, message?.GetType() ?? typeof(TMessage), JsonOptions));
        return routedClient.SendTo(routerChannelId, targetSessionNodeRid, packet)
            .WithPacketName("ZLink.SessionGateway")
            .Async(cancellationToken);
    }
}

internal sealed class ZLinkSessionGatewayRequestCall<TRequest>(
    IZLinkRoutedClient routedClient,
    string routerChannelId,
    RoutingId targetSessionNodeRid,
    string actorId,
    TRequest request) : IZLinkSessionGatewayRequestCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan _timeout = TimeSpan.FromSeconds(30);

    public IZLinkSessionGatewayRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSessionGatewayRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var packet = new ZLinkSessionGatewayPacket(
            new ZLinkSessionGatewayEnvelope(
                actorId,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                true),
            JsonSerializer.SerializeToUtf8Bytes(request, request?.GetType() ?? typeof(TRequest), JsonOptions));
        var reply = await routedClient.RequestTo(routerChannelId, targetSessionNodeRid, packet)
            .WithPacketName("ZLink.SessionGateway")
            .WithTimeout(_timeout)
            .Async<byte[]>(cancellationToken)
            .ConfigureAwait(false);
        return JsonSerializer.Deserialize<TReply>(reply, JsonOptions)
            ?? throw new InvalidOperationException("Session gateway reply body is null.");
    }
}
