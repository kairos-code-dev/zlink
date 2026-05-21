namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionProxyService(
    IZLinkMultipartRouteClient routedClient,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkSessionProxyFactory
{
    public IZLinkSessionProxy Create(string actorId)
    {
        return new ZLinkBoundSessionProxy(this, actorId);
    }

    internal IZLinkSessionProxySendCall Send<TMessage>(
        string actorId,
        TMessage message)
    {
        return new ZLinkSessionProxySendCall<TMessage>(
            this,
            actorId,
            message);
    }

    internal IZLinkSessionProxyRequestCall Request<TRequest>(
        string actorId,
        TRequest request)
    {
        return new ZLinkSessionProxyRequestCall<TRequest>(
            this,
            actorId,
            request);
    }

    public async ValueTask DisconnectAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        var route = await ResolveSessionRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        if (runtime.TryGetSessionActorContext(actorId, route.BindingToken, out var localContext))
        {
            await localContext.CloseByProxyAsync(cancellationToken).ConfigureAwait(false);
            runtime.UnbindActorSession(actorId, route.BindingToken);
            return;
        }

        var parts = ZLinkInternalMultipartPackets.CreateSessionDisconnectParts(
            new ZLinkSessionDisconnectEnvelope(actorId, route.BindingToken));

        await routedClient.SendPartsTo(
                runtime.ResolveDefaultRouterChannelId(),
                route.SessionRouterId,
                ZLinkInternalPacketNames.SessionDisconnect,
                parts,
                cancellationToken)
            .ConfigureAwait(false);
        runtime.UnbindActorSession(actorId, route.BindingToken);
    }

    internal async ValueTask SendProxyAsync<TMessage>(
        string actorId,
        string? packetName,
        IReadOnlyDictionary<string, string> metadata,
        TMessage message,
        CancellationToken cancellationToken)
    {
        var route = await ResolveSessionRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        if (runtime.TryGetSessionActorContext(actorId, route.BindingToken, out var localContext))
        {
            await localContext.SendRawAsync(
                    packetName ?? throw new InvalidOperationException("Packet name is required."),
                    ZlinkStreamCodec.Json,
                    ZLinkEnvelopeCodec.EncodeJsonBytes(message, message?.GetType() ?? typeof(TMessage)),
                    cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var packet = CreateProxyPacket(
                actorId,
                packetName,
                metadata,
                expectsReply: false,
                message,
                route);

        await routedClient.SendPartsTo(
                runtime.ResolveDefaultRouterChannelId(),
                packet.SessionRouterId,
                ZLinkInternalPacketNames.SessionProxy,
                packet.Parts,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<TReply> RequestProxyAsync<TRequest, TReply>(
        string actorId,
        string? packetName,
        IReadOnlyDictionary<string, string> metadata,
        TimeSpan? timeoutOverride,
        TRequest request,
        CancellationToken cancellationToken)
    {
        var route = await ResolveSessionRouteAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
        var timeout = timeoutOverride ?? registration.DefaultTimeout;
        if (runtime.TryGetSessionActorContext(actorId, route.BindingToken, out var localContext))
        {
            var localReply = await localContext.RequestRawAsync(
                    packetName ?? throw new InvalidOperationException("Packet name is required."),
                    ZlinkStreamCodec.Json,
                    ZLinkEnvelopeCodec.EncodeJsonBytes(request, request?.GetType() ?? typeof(TRequest)),
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
            return ZLinkClientCallCodec.DecodeJsonReply<TReply>(
                localReply.AsReadOnlySpan(),
                "Session proxy reply payload is null.");
        }

        var packet = CreateProxyPacket(
                actorId,
                packetName,
                metadata,
                expectsReply: true,
                request,
                route);

        ReadOnlyMemory<byte> reply;
        try
        {
            reply = await routedClient.RequestPartsTo<ReadOnlyMemory<byte>>(
                    runtime.ResolveDefaultRouterChannelId(),
                    packet.SessionRouterId,
                    ZLinkInternalPacketNames.SessionProxy,
                    packet.Parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SessionProxyTimeout,
                $"Session proxy request for actor '{actorId}' timed out.",
                innerException: ex);
        }

        return ZLinkClientCallCodec.DecodeJsonReply<TReply>(
            reply.Span,
            "Session proxy reply payload is null.");
    }

    private ValueTask<ZLinkActorBoundSession> ResolveSessionRouteAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (runtime.TryGetActorBoundSession(actorId, out var route))
        {
            return ValueTask.FromResult(route);
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorSessionNotBound,
            $"No current session binding exists for actor '{actorId}'.",
            isRetriable: true);
    }

    private static ZLinkSessionProxyPacket CreateProxyPacket<TPayload>(
        string actorId,
        string? packetName,
        IReadOnlyDictionary<string, string> metadata,
        bool expectsReply,
        TPayload payload,
        ZLinkActorBoundSession route)
    {
        var envelope = new ZLinkSessionProxyEnvelope(
            actorId,
            route.BindingToken,
            packetName ?? throw new InvalidOperationException("Packet name is required."),
            expectsReply,
            ZLinkClientCallCodec.CopyMetadata(metadata));
        var parts = ZLinkInternalMultipartPackets.CreateSessionProxyParts(
            envelope,
            payload,
            payload?.GetType() ?? typeof(TPayload));

        return new ZLinkSessionProxyPacket(route.SessionRouterId, parts);
    }
}

internal readonly record struct ZLinkSessionProxyPacket(
    RoutingId SessionRouterId,
    IReadOnlyList<Message> Parts);

internal sealed class ZLinkBoundSessionProxy(
    ZLinkSessionProxyService service,
    string actorId) : IZLinkSessionProxy
{
    public IZLinkSessionProxySendCall Send<TMessage>(
        TMessage message)
    {
        return service.Send(actorId, message);
    }

    public IZLinkSessionProxyRequestCall Request<TRequest>(
        TRequest request)
    {
        return service.Request(actorId, request);
    }

    public ValueTask DisconnectAsync(
        CancellationToken cancellationToken = default)
    {
        return service.DisconnectAsync(actorId, cancellationToken);
    }
}

internal sealed class ZLinkSessionProxySendCall<TMessage>(
    ZLinkSessionProxyService service,
    string actorId,
    TMessage message) : IZLinkSessionProxySendCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);

    public IZLinkSessionProxySendCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSessionProxySendCall Metadata(
        string key,
        string value)
    {
        _metadata[key] = value;
        return this;
    }

    public async ValueTask Submit(CancellationToken cancellationToken = default)
    {
        await service.SendProxyAsync(
                actorId,
                _packetName,
                _metadata,
                message,
                cancellationToken)
            .ConfigureAwait(false);
    }
}

internal sealed class ZLinkSessionProxyRequestCall<TRequest>(
    ZLinkSessionProxyService service,
    string actorId,
    TRequest request) : IZLinkSessionProxyRequestCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);
    private TimeSpan? _timeout;

    public IZLinkSessionProxyRequestCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSessionProxyRequestCall Metadata(
        string key,
        string value)
    {
        _metadata[key] = value;
        return this;
    }

    public IZLinkSessionProxyRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        return await service.RequestProxyAsync<TRequest, TReply>(
                actorId,
                _packetName,
                _metadata,
                _timeout,
                request,
                cancellationToken)
            .ConfigureAwait(false);
    }
}
