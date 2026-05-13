using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionProxyService(
    IZLinkRouteClient routedClient,
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
    IZLinkRouteClient routedClient,
    IZLinkActorSessionBindingStore bindingStore,
    ZLinkFrameworkRuntime runtime,
    string actorId,
    TMessage message) : IZLinkSessionProxySendCall
{
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

    public async ValueTask Submit(CancellationToken cancellationToken = default)
    {
        var route = await ResolveRouteAsync(cancellationToken)
            .ConfigureAwait(false);
        var packet = new ZLinkSessionProxyPacket(
            new ZLinkSessionProxyEnvelope(
                actorId,
                route.BindingToken,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                false,
                new Dictionary<string, string>(_metadata, StringComparer.Ordinal)),
            JsonSerializer.SerializeToUtf8Bytes(
                message,
                message?.GetType() ?? typeof(TMessage),
                ZLinkJsonSerializerOptions.Default));

        await routedClient.SendTo(runtime.ResolveDefaultRouterChannelId(), route.SessionRouterId, packet)
            .WithPacketName(ZLinkInternalPacketNames.SessionProxy)
            .Submit(cancellationToken)
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
    IZLinkRouteClient routedClient,
    IZLinkActorSessionBindingStore bindingStore,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    string actorId,
    TRequest request) : IZLinkSessionProxyRequestCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);
    private TimeSpan? _timeout;

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

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var route = await ResolveRouteAsync(cancellationToken)
            .ConfigureAwait(false);
        var packet = new ZLinkSessionProxyPacket(
            new ZLinkSessionProxyEnvelope(
                actorId,
                route.BindingToken,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                true,
                new Dictionary<string, string>(_metadata, StringComparer.Ordinal)),
            JsonSerializer.SerializeToUtf8Bytes(
                request,
                request?.GetType() ?? typeof(TRequest),
                ZLinkJsonSerializerOptions.Default));

        byte[] reply;
        try
        {
            reply = await routedClient.RequestTo(runtime.ResolveDefaultRouterChannelId(), route.SessionRouterId, packet)
                .WithPacketName(ZLinkInternalPacketNames.SessionProxy)
                .WithTimeout(_timeout ?? registration.DefaultTimeout)
                .SubmitAsync<byte[]>(cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SessionProxyTimeout,
                $"Session proxy request for actor '{actorId}' timed out.",
                innerException: ex);
        }

            return JsonSerializer.Deserialize<TReply>(reply, ZLinkJsonSerializerOptions.Default)
                ?? throw new InvalidOperationException("Session proxy reply body is null.");
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
