namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotClientService(IServiceProvider services) : IZLinkSpotClient
{
    public IZLinkSendCall SendSpot<TMessage>(string spotName, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRouteResolver(),
            spotName,
            null,
            message);
    }

    public IZLinkSendCall SendSpot<TMessage>(ZLinkSpotId spotId, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRouteResolver(),
            null,
            spotId,
            message);
    }

    public IZLinkRequestCall RequestSpot<TMessage>(string spotName, TMessage request)
    {
        return new ZLinkRoutedSpotRequestCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRouteResolver(),
            spotName,
            null,
            request);
    }

    public IZLinkRequestCall RequestSpot<TMessage>(ZLinkSpotId spotId, TMessage request)
    {
        return new ZLinkRoutedSpotRequestCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRouteResolver(),
            null,
            spotId,
            request);
    }

    public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkCurrentSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            channelName,
            message);
    }

    public IZLinkRequestCall RequestChannel<TMessage>(
        string channelName,
        TMessage request)
    {
        return new ZLinkCurrentSpotRequestCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            channelName,
            request);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Publish(topic, message);
    }

    private IZLinkSpotRouteResolver RequireRouteResolver()
    {
        return services.GetService(typeof(IZLinkSpotRouteResolver)) is IZLinkSpotRouteResolver resolver
            ? resolver
            : throw new InvalidOperationException(
                "IZLinkSpotClient spot routing requires AddSpotRouteResolver<TResolver>().");
    }
}

internal sealed class ZLinkRoutedSpotSendCall<TMessage>(
    ZLinkSpotActivation activation,
    IZLinkSpotRouteResolver resolver,
    string? spotName,
    ZLinkSpotId? spotId,
    TMessage message) : IZLinkSendCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkSendCall WithPacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public async ValueTask Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var route = await ResolveRouteAsync(cancellationToken).ConfigureAwait(false);
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            route.RouterChannelId,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TMessage));
        if (!activation.SendToSpot(
                route.TargetNodeRid,
                route.SpotId.ToRoutingId(),
                envelope,
                SendFlags.DontWait))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"SPOT send target route was not available for '{route.SpotId}'.");
        }
    }

    private ValueTask<ZLinkSpotRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        return spotId is { } id
            ? resolver.ResolveSpotRouteAsync(id, cancellationToken)
            : resolver.ResolveSpotRouteAsync(
                spotName ?? throw new InvalidOperationException("SPOT name is required."),
                cancellationToken);
    }
}

internal sealed class ZLinkRoutedSpotRequestCall<TRequest>(
    ZLinkSpotActivation activation,
    IZLinkSpotRouteResolver resolver,
    string? spotName,
    ZLinkSpotId? spotId,
    TRequest request) : IZLinkRequestCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall WithPacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var route = await ResolveRouteAsync(cancellationToken).ConfigureAwait(false);
        var timeout = _timeout ?? activation.DefaultTimeout;
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            route.RouterChannelId,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            DateTimeOffset.UtcNow.Add(timeout),
            null,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, request, request?.GetType() ?? typeof(TRequest));
        var reply = await activation.RequestSpotAsync(
            route.TargetNodeRid,
            route.SpotId.ToRoutingId(),
            envelope,
            timeout,
            cancellationToken).ConfigureAwait(false);
        try
        {
            if (reply.Count == 0)
            {
                throw new InvalidOperationException("SPOT request reply is empty.");
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply[0]);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                throw new InvalidOperationException(replyHeader.ErrorMessage ?? "SPOT request failed.");
            }

            return (TReply?)ZLinkEnvelopeCodec.DecodeBody(reply[0], typeof(TReply))
                ?? throw new InvalidOperationException("SPOT reply body is null.");
        }
        finally
        {
            foreach (var item in reply)
            {
                item.Dispose();
            }
        }
    }

    private ValueTask<ZLinkSpotRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        return spotId is { } id
            ? resolver.ResolveSpotRouteAsync(id, cancellationToken)
            : resolver.ResolveSpotRouteAsync(
                spotName ?? throw new InvalidOperationException("SPOT name is required."),
                cancellationToken);
    }
}

internal sealed class ZLinkCurrentSpotSendCall<TMessage>(
    ZLinkSpotActivation activation,
    string channelName,
    TMessage message) : IZLinkSendCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkSendCall WithPacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);
        var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TMessage));
        return activation.SendChannelAsync(channelName, envelope, cancellationToken);
    }
}

internal sealed class ZLinkCurrentSpotRequestCall<TMessage>(
    ZLinkSpotActivation activation,
    string channelName,
    TMessage request) : IZLinkRequestCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall WithPacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? activation.DefaultTimeout;
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            DateTimeOffset.UtcNow.Add(timeout),
            null,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, request, request?.GetType() ?? typeof(TMessage));
        var reply = await activation.RequestChannelAsync(channelName, envelope, timeout, cancellationToken);
        try
        {
            if (reply.Count == 0)
            {
                throw new InvalidOperationException("SPOT channel request reply is empty.");
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply[0]);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                throw new InvalidOperationException(replyHeader.ErrorMessage ?? "SPOT channel request failed.");
            }

            return (TReply?)ZLinkEnvelopeCodec.DecodeBody(reply[0], typeof(TReply))
                ?? throw new InvalidOperationException("SPOT channel reply body is null.");
        }
        finally
        {
            foreach (var item in reply)
            {
                item.Dispose();
            }
        }
    }
}
