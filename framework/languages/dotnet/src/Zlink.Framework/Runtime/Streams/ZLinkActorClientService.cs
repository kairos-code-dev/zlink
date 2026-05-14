namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorClientService(
    IZLinkActorPlayRouteResolver routeResolver,
    IZLinkMultipartRouteClient routedClient,
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
    IZLinkMultipartRouteClient routedClient,
    string actorId,
    TMessage message) : IZLinkActorClientSendCall
{
    private static readonly IZlinkStreamHeaderCodec HeaderCodec = ZlinkStreamDefaultCodecs.Header();
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);

    public IZLinkActorClientSendCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkActorClientSendCall Metadata(
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
        var streamHeader = CreateHeader(ZlinkStreamMessageKind.Send, packetName, _metadata);
        var parts = ZLinkInternalMultipartPackets.CreateActorDispatchParts(
            actorId,
            string.Empty,
            streamHeader,
            message,
            message?.GetType() ?? typeof(TMessage));

        await routedClient.SendPartsTo(
                route.RouterChannelId,
                route.TargetNodeRid,
                ZLinkInternalPacketNames.ActorDispatch,
                parts,
                cancellationToken)
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

    private static ZlinkStreamHeader CreateHeader(
        ZlinkStreamMessageKind kind,
        string packetName,
        IReadOnlyDictionary<string, string> metadata)
    {
        return new ZlinkStreamHeader(
            kind,
            ZlinkStreamCodec.Json,
            metadata.Count == 0 ? ZlinkStreamHeaderFlags.None : ZlinkStreamHeaderFlags.HasMetadata,
            null,
            packetName,
            ZlinkStreamMetadata.Empty.WithMany(metadata));
    }
}

internal sealed class ZLinkActorClientRequestCall<TRequest>(
    IZLinkActorPlayRouteResolver routeResolver,
    IZLinkMultipartRouteClient routedClient,
    ZLinkFrameworkRegistration registration,
    string actorId,
    TRequest request) : IZLinkActorClientRequestCall
{
    private static readonly IZlinkStreamHeaderCodec HeaderCodec = ZlinkStreamDefaultCodecs.Header();
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);

    public IZLinkActorClientRequestCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkActorClientRequestCall Metadata(
        string key,
        string value)
    {
        _metadata[key] = value;
        return this;
    }

    public IZLinkActorClientRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var route = await ResolveRouteAsync(cancellationToken)
            .ConfigureAwait(false);
        var packetName = _packetName ?? throw new InvalidOperationException("Packet name is required.");
        var timeout = _timeout ?? registration.DefaultTimeout;
        var streamHeader = CreateHeader(packetName, _metadata);
        var parts = ZLinkInternalMultipartPackets.CreateActorDispatchParts(
            actorId,
            string.Empty,
            streamHeader,
            request,
            request?.GetType() ?? typeof(TRequest));

        try
        {
            var reply = await routedClient.RequestPartsTo<ReadOnlyMemory<byte>>(
                    route.RouterChannelId,
                    route.TargetNodeRid,
                    ZLinkInternalPacketNames.ActorDispatch,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
            return ZLinkClientCallCodec.DecodeJsonReply<TReply>(
                reply.Span,
                "Actor client reply body is null.");
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

    private static ZlinkStreamHeader CreateHeader(
        string packetName,
        IReadOnlyDictionary<string, string> metadata)
    {
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            metadata.Count == 0 ? ZlinkStreamHeaderFlags.HasRequestSeq : ZlinkStreamHeaderFlags.HasRequestSeq | ZlinkStreamHeaderFlags.HasMetadata,
            new ZlinkStreamRequestSeq(1),
            packetName,
            ZlinkStreamMetadata.Empty.WithMany(metadata));
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
