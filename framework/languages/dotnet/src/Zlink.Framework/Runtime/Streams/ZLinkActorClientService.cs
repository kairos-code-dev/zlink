using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorClientService(
    IZLinkActorPlayRouteResolver routeResolver,
    IZLinkRouteClient routedClient,
    ZLinkFrameworkRegistration registration) : IZLinkActorClient
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
            registration,
            actorId,
            request);
    }
}

internal sealed class ZLinkActorClientSendCall<TMessage>(
    IZLinkActorPlayRouteResolver routeResolver,
    IZLinkRouteClient routedClient,
    string actorId,
    TMessage message) : IZLinkActorClientSendCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);

    public IZLinkActorClientSendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkActorClientSendCall WithMetadata(
        string key,
        string value)
    {
        _metadata[key] = value;
        return this;
    }

    public async ValueTask Submit(CancellationToken cancellationToken = default)
    {
        var route = await ResolveRouteAsync(cancellationToken)
            .ConfigureAwait(false);
        var packetName = _packetName ?? throw new InvalidOperationException("Packet name is required.");
        var packet = new ZLinkActorDispatchPacket(
            actorId,
            string.Empty,
            CreateHeaderSnapshot(ZlinkStreamMessageKind.Send, packetName, _metadata),
            JsonSerializer.SerializeToUtf8Bytes(message, message?.GetType() ?? typeof(TMessage), JsonOptions));

        await routedClient.SendTo(route.RouterChannelId, route.TargetNodeRid, packet)
            .WithPacketName(ZLinkInternalPacketNames.ActorDispatch)
            .Submit(cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        try
        {
            return await routeResolver.ResolvePlayRouteAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException)
        {
            throw;
        }
        catch (Exception ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor route for '{actorId}' was not found.",
                innerException: ex);
        }
    }

    private static ZLinkStreamHeaderSnapshot CreateHeaderSnapshot(
        ZlinkStreamMessageKind kind,
        string packetName,
        IReadOnlyDictionary<string, string> metadata)
    {
        return new ZLinkStreamHeaderSnapshot(
            kind,
            ZlinkStreamCodec.Json,
            metadata.Count == 0 ? ZlinkStreamHeaderFlags.None : ZlinkStreamHeaderFlags.HasMetadata,
            null,
            packetName,
            new Dictionary<string, string>(metadata, StringComparer.Ordinal));
    }
}

internal sealed class ZLinkActorClientRequestCall<TRequest>(
    IZLinkActorPlayRouteResolver routeResolver,
    IZLinkRouteClient routedClient,
    ZLinkFrameworkRegistration registration,
    string actorId,
    TRequest request) : IZLinkActorClientRequestCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);

    public IZLinkActorClientRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkActorClientRequestCall WithMetadata(
        string key,
        string value)
    {
        _metadata[key] = value;
        return this;
    }

    public IZLinkActorClientRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var route = await ResolveRouteAsync(cancellationToken)
            .ConfigureAwait(false);
        var packetName = _packetName ?? throw new InvalidOperationException("Packet name is required.");
        var packet = new ZLinkActorDispatchPacket(
            actorId,
            string.Empty,
            CreateHeaderSnapshot(packetName, _metadata),
            JsonSerializer.SerializeToUtf8Bytes(request, request?.GetType() ?? typeof(TRequest), JsonOptions));

        try
        {
            var reply = await routedClient.RequestTo(route.RouterChannelId, route.TargetNodeRid, packet)
                .WithPacketName(ZLinkInternalPacketNames.ActorDispatch)
                .WithTimeout(_timeout ?? registration.DefaultTimeout)
                .SubmitAsync<byte[]>(cancellationToken)
                .ConfigureAwait(false);
            return JsonSerializer.Deserialize<TReply>(reply, JsonOptions)
                ?? throw new InvalidOperationException("Actor client reply body is null.");
        }
        catch (TimeoutException ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorDispatchTimeout,
                $"Actor request for '{actorId}' timed out.",
                innerException: ex);
        }
    }

    private async ValueTask<ZLinkActorRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        try
        {
            return await routeResolver.ResolvePlayRouteAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException)
        {
            throw;
        }
        catch (Exception ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor route for '{actorId}' was not found.",
                innerException: ex);
        }
    }

    private static ZLinkStreamHeaderSnapshot CreateHeaderSnapshot(
        string packetName,
        IReadOnlyDictionary<string, string> metadata)
    {
        return new ZLinkStreamHeaderSnapshot(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            metadata.Count == 0 ? ZlinkStreamHeaderFlags.HasRequestSeq : ZlinkStreamHeaderFlags.HasRequestSeq | ZlinkStreamHeaderFlags.HasMetadata,
            new ZlinkStreamRequestSeq(1).Value,
            packetName,
            new Dictionary<string, string>(metadata, StringComparer.Ordinal));
    }
}

internal sealed class ZLinkMissingActorClient : IZLinkActorClient
{
    public IZLinkActorClientSendCall Send<TMessage>(
        string actorId,
        TMessage message)
    {
        _ = actorId;
        _ = message;
        throw new ZLinkConfigurationException(
            "IZLinkActorClient requires AddActorPlayRouteResolver<TResolver>().");
    }

    public IZLinkActorClientRequestCall Request<TRequest>(
        string actorId,
        TRequest request)
    {
        _ = actorId;
        _ = request;
        throw new ZLinkConfigurationException(
            "IZLinkActorClient requires AddActorPlayRouteResolver<TResolver>().");
    }
}
