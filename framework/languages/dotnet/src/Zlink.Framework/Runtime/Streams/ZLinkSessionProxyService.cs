namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionProxyService(
    IZLinkMultipartRouteClient routedClient,
    IZLinkActorSessionBindingStore bindingStore,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkSessionProxy
{
    public IZLinkSessionProxySendCall Send<TMessage>(
        string actorId,
        TMessage message)
    {
        return new ZLinkSessionProxySendCall<TMessage>(
            routedClient,
            bindingStore,
            runtime,
            actorId,
            message);
    }

    public IZLinkSessionProxyRequestCall Request<TRequest>(
        string actorId,
        TRequest request)
    {
        return new ZLinkSessionProxyRequestCall<TRequest>(
            routedClient,
            bindingStore,
            runtime,
            registration,
            actorId,
            request);
    }
}

internal sealed class ZLinkSessionProxySendCall<TMessage>(
    IZLinkMultipartRouteClient routedClient,
    IZLinkActorSessionBindingStore bindingStore,
    ZLinkFrameworkRuntime runtime,
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
        var route = await ResolveRouteAsync(cancellationToken)
            .ConfigureAwait(false);
        var parts = ZLinkInternalMultipartPackets.CreateSessionProxyParts(
            new ZLinkSessionProxyEnvelope(
                actorId,
                route.BindingToken,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                false,
                ZLinkClientCallCodec.CopyMetadata(_metadata)),
            message,
            message?.GetType() ?? typeof(TMessage));

        await routedClient.SendPartsTo(
                runtime.ResolveDefaultRouterChannelId(),
                route.SessionRouterId,
                ZLinkInternalPacketNames.SessionProxy,
                parts,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorSessionRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        try
        {
            return await bindingStore.FindSessionAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException)
        {
            throw;
        }
        catch (Exception ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SessionRouteNotFound,
                $"Session route for actor '{actorId}' was not found.",
                innerException: ex);
        }
    }
}

internal sealed class ZLinkSessionProxyRequestCall<TRequest>(
    IZLinkMultipartRouteClient routedClient,
    IZLinkActorSessionBindingStore bindingStore,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
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
        var route = await ResolveRouteAsync(cancellationToken)
            .ConfigureAwait(false);
        var timeout = _timeout ?? registration.DefaultTimeout;
        var parts = ZLinkInternalMultipartPackets.CreateSessionProxyParts(
            new ZLinkSessionProxyEnvelope(
                actorId,
                route.BindingToken,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                true,
                ZLinkClientCallCodec.CopyMetadata(_metadata)),
            request,
            request?.GetType() ?? typeof(TRequest));

        ReadOnlyMemory<byte> reply;
        try
        {
            reply = await routedClient.RequestPartsTo<ReadOnlyMemory<byte>>(
                    runtime.ResolveDefaultRouterChannelId(),
                    route.SessionRouterId,
                    ZLinkInternalPacketNames.SessionProxy,
                    parts,
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

    private async ValueTask<ZLinkActorSessionRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        try
        {
            return await bindingStore.FindSessionAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException)
        {
            throw;
        }
        catch (Exception ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SessionRouteNotFound,
                $"Session route for actor '{actorId}' was not found.",
                innerException: ex);
        }
    }
}

internal sealed class ZLinkMissingSessionProxy : IZLinkSessionProxy
{
    public IZLinkSessionProxySendCall Send<TMessage>(
        string actorId,
        TMessage message)
    {
        _ = actorId;
        _ = message;
        throw new ZLinkConfigurationException(
            "IZLinkSessionProxy requires AddActorSessionBindingStore<TStore>().");
    }

    public IZLinkSessionProxyRequestCall Request<TRequest>(
        string actorId,
        TRequest request)
    {
        _ = actorId;
        _ = request;
        throw new ZLinkConfigurationException(
            "IZLinkSessionProxy requires AddActorSessionBindingStore<TStore>().");
    }
}
