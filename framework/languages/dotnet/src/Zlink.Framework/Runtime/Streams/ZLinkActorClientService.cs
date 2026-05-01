namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorClientService(
    IZLinkActorPlayRouteResolver routeResolver,
    IZLinkRoutedClient routedClient) : IZLinkActorClient
{
    public IZLinkActorClientSendCall Send<TMessage>(
        string actorId,
        TMessage message)
    {
        return new ZLinkActorClientSendCall<TMessage>(
            routeResolver,
            routedClient,
            actorId,
            message);
    }

    public IZLinkActorClientRequestCall Request<TRequest>(
        string actorId,
        TRequest request)
    {
        return new ZLinkActorClientRequestCall<TRequest>(
            routeResolver,
            routedClient,
            actorId,
            request);
    }
}

internal sealed class ZLinkActorClientSendCall<TMessage>(
    IZLinkActorPlayRouteResolver routeResolver,
    IZLinkRoutedClient routedClient,
    string actorId,
    TMessage message) : IZLinkActorClientSendCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkActorClientSendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkActorClientSendCall WithMetadata(
        string key,
        string value)
    {
        _ = key;
        _ = value;
        return this;
    }

    public async ValueTask Async(CancellationToken cancellationToken = default)
    {
        var route = await routeResolver.ResolvePlayRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        await routedClient.SendTo(route.RouterChannelId, route.TargetNodeRid, message)
            .WithPacketName(_packetName ?? throw new InvalidOperationException("Packet name is required."))
            .Async(cancellationToken)
            .ConfigureAwait(false);
    }
}

internal sealed class ZLinkActorClientRequestCall<TRequest>(
    IZLinkActorPlayRouteResolver routeResolver,
    IZLinkRoutedClient routedClient,
    string actorId,
    TRequest request) : IZLinkActorClientRequestCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan _timeout = TimeSpan.FromSeconds(30);

    public IZLinkActorClientRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkActorClientRequestCall WithMetadata(
        string key,
        string value)
    {
        _ = key;
        _ = value;
        return this;
    }

    public IZLinkActorClientRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var route = await routeResolver.ResolvePlayRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        return await routedClient.RequestTo(route.RouterChannelId, route.TargetNodeRid, request)
            .WithPacketName(_packetName ?? throw new InvalidOperationException("Packet name is required."))
            .WithTimeout(_timeout)
            .Async<TReply>(cancellationToken)
            .ConfigureAwait(false);
    }
}
