using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionProxyService(
    IZLinkRoutedClient routedClient,
    IZLinkActorSessionRouteResolver routeResolver) : IZLinkSessionProxy
{
    public IZLinkSessionProxySendCall Send<TMessage>(
        string actorId,
        TMessage message)
    {
        return new ZLinkSessionProxySendCall<TMessage>(
            routedClient,
            routeResolver,
            actorId,
            message);
    }

    public IZLinkSessionProxyRequestCall Request<TRequest>(
        string actorId,
        TRequest request)
    {
        return new ZLinkSessionProxyRequestCall<TRequest>(
            routedClient,
            routeResolver,
            actorId,
            request);
    }
}

internal sealed class ZLinkSessionProxySendCall<TMessage>(
    IZLinkRoutedClient routedClient,
    IZLinkActorSessionRouteResolver routeResolver,
    string actorId,
    TMessage message) : IZLinkSessionProxySendCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);

    public IZLinkSessionProxySendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSessionProxySendCall WithMetadata(
        string key,
        string value)
    {
        _metadata[key] = value;
        return this;
    }

    public async ValueTask Async(CancellationToken cancellationToken = default)
    {
        var route = await routeResolver.ResolveSessionRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        var packet = new ZLinkSessionProxyPacket(
            new ZLinkSessionProxyEnvelope(
                actorId,
                route.SessionId,
                route.BindingToken,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                false,
                new Dictionary<string, string>(_metadata, StringComparer.Ordinal)),
            JsonSerializer.SerializeToUtf8Bytes(message, message?.GetType() ?? typeof(TMessage), JsonOptions));

        await routedClient.SendTo(route.RouterChannelId, route.SessionRouterId, packet)
            .WithPacketName(ZLinkInternalPacketNames.SessionProxy)
            .Async(cancellationToken)
            .ConfigureAwait(false);
    }
}

internal sealed class ZLinkSessionProxyRequestCall<TRequest>(
    IZLinkRoutedClient routedClient,
    IZLinkActorSessionRouteResolver routeResolver,
    string actorId,
    TRequest request) : IZLinkSessionProxyRequestCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);
    private TimeSpan _timeout = TimeSpan.FromSeconds(30);

    public IZLinkSessionProxyRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSessionProxyRequestCall WithMetadata(
        string key,
        string value)
    {
        _metadata[key] = value;
        return this;
    }

    public IZLinkSessionProxyRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var route = await routeResolver.ResolveSessionRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        var packet = new ZLinkSessionProxyPacket(
            new ZLinkSessionProxyEnvelope(
                actorId,
                route.SessionId,
                route.BindingToken,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                true,
                new Dictionary<string, string>(_metadata, StringComparer.Ordinal)),
            JsonSerializer.SerializeToUtf8Bytes(request, request?.GetType() ?? typeof(TRequest), JsonOptions));

        var reply = await routedClient.RequestTo(route.RouterChannelId, route.SessionRouterId, packet)
            .WithPacketName(ZLinkInternalPacketNames.SessionProxy)
            .WithTimeout(_timeout)
            .Async<byte[]>(cancellationToken)
            .ConfigureAwait(false);
        return JsonSerializer.Deserialize<TReply>(reply, JsonOptions)
            ?? throw new InvalidOperationException("Session proxy reply body is null.");
    }
}
