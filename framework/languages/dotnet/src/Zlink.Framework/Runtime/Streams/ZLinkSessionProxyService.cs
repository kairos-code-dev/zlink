using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionProxyService(
    IZLinkRouteClient routedClient,
    IZLinkActorSessionRouteResolver routeResolver,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkSessionProxy
{
    public IZLinkSessionProxySendCall Send<TMessage>(
        string actorId,
        TMessage message)
    {
        return new ZLinkSessionProxySendCall<TMessage>(
            routedClient,
            routeResolver,
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
            routeResolver,
            runtime,
            registration,
            actorId,
            request);
    }
}

internal sealed class ZLinkSessionProxySendCall<TMessage>(
    IZLinkRouteClient routedClient,
    IZLinkActorSessionRouteResolver routeResolver,
    ZLinkFrameworkRuntime runtime,
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
            JsonSerializer.SerializeToUtf8Bytes(message, message?.GetType() ?? typeof(TMessage), JsonOptions));

        await routedClient.SendTo(runtime.ResolveDefaultRouterChannelId(), route.SessionRouterId, packet)
            .WithPacketName(ZLinkInternalPacketNames.SessionProxy)
            .Submit(cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorSessionRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        try
        {
            return await routeResolver.ResolveSessionRouteAsync(actorId, cancellationToken)
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
    IZLinkActorSessionRouteResolver routeResolver,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    string actorId,
    TRequest request) : IZLinkSessionProxyRequestCall
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
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

    public async ValueTask<TReply> Submit<TReply>(CancellationToken cancellationToken = default)
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
            JsonSerializer.SerializeToUtf8Bytes(request, request?.GetType() ?? typeof(TRequest), JsonOptions));

        byte[] reply;
        try
        {
            reply = await routedClient.RequestTo(runtime.ResolveDefaultRouterChannelId(), route.SessionRouterId, packet)
                .WithPacketName(ZLinkInternalPacketNames.SessionProxy)
                .WithTimeout(_timeout ?? registration.DefaultTimeout)
                .Submit<byte[]>(cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException ex)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SessionProxyTimeout,
                $"Session proxy request for actor '{actorId}' timed out.",
                innerException: ex);
        }

        return JsonSerializer.Deserialize<TReply>(reply, JsonOptions)
            ?? throw new InvalidOperationException("Session proxy reply body is null.");
    }

    private async ValueTask<ZLinkActorSessionRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        try
        {
            return await routeResolver.ResolveSessionRouteAsync(actorId, cancellationToken)
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
            "IZLinkSessionProxy requires AddActorSessionRouteResolver<TResolver>().");
    }

    public IZLinkSessionProxyRequestCall Request<TRequest>(
        string actorId,
        TRequest request)
    {
        _ = actorId;
        _ = request;
        throw new ZLinkConfigurationException(
            "IZLinkSessionProxy requires AddActorSessionRouteResolver<TResolver>().");
    }
}
