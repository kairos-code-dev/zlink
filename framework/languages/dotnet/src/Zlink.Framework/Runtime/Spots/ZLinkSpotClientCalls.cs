namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotClientService(IServiceProvider services) : IZLinkSpotClient
{
    public IZLinkSendCall SendSpot<TMessage>(string spotName, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRouteResolver(),
            ZLinkSpotRouteTarget.ByName(spotName),
            message);
    }

    public IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRouteResolver(),
            ZLinkSpotRouteTarget.ByRoutingId(spotRid),
            message);
    }

    public IZLinkRequestCall RequestSpot<TMessage>(string spotName, TMessage request)
    {
        return new ZLinkRoutedSpotRequestCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRouteResolver(),
            ZLinkSpotRouteTarget.ByName(spotName),
            request);
    }

    public IZLinkRequestCall RequestSpot<TMessage>(RoutingId spotRid, TMessage request)
    {
        return new ZLinkRoutedSpotRequestCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRouteResolver(),
            ZLinkSpotRouteTarget.ByRoutingId(spotRid),
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
            : throw new ZLinkConfigurationException(
                "IZLinkSpotClient spot routing requires AddSpotRouteResolver<TResolver>().");
    }
}

internal sealed class ZLinkRoutedSpotSendCall<TMessage>(
    IZLinkCurrentSpotActivation activation,
    IZLinkSpotRouteResolver resolver,
    ZLinkSpotRouteTarget target,
    TMessage message) : IZLinkSendCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkSendCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public async ValueTask Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var route = await ResolveRouteAsync(cancellationToken).ConfigureAwait(false);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            route.RouterChannelId,
            _messageName ?? throw new InvalidOperationException("Message name is required."));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message);
        await activation.SendSpotAsync(
            route.RouterChannelId,
            route.TargetNodeRid,
            route.SpotRid,
            parts,
            cancellationToken).ConfigureAwait(false);
    }

    private ValueTask<ZLinkSpotRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        return target.ResolveAsync(resolver, cancellationToken);
    }
}

internal sealed class ZLinkRoutedSpotRequestCall<TRequest>(
    IZLinkCurrentSpotActivation activation,
    IZLinkSpotRouteResolver resolver,
    ZLinkSpotRouteTarget target,
    TRequest request) : IZLinkRequestCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var route = await ResolveRouteAsync(cancellationToken).ConfigureAwait(false);
        var timeout = _timeout ?? activation.DefaultTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            route.RouterChannelId,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request);
        var reply = await activation.RequestSpotAsync(
            route.RouterChannelId,
            route.TargetNodeRid,
            route.SpotRid,
            parts,
            timeout,
            cancellationToken).ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT request reply is empty.",
            "SPOT request failed.");
    }

    private ValueTask<ZLinkSpotRoute> ResolveRouteAsync(CancellationToken cancellationToken)
    {
        return target.ResolveAsync(resolver, cancellationToken);
    }
}

internal readonly record struct ZLinkSpotRouteTarget(
    string? SpotName,
    RoutingId? SpotRid)
{
    public static ZLinkSpotRouteTarget ByName(string spotName)
    {
        return new ZLinkSpotRouteTarget(spotName, null);
    }

    public static ZLinkSpotRouteTarget ByRoutingId(RoutingId spotRid)
    {
        return new ZLinkSpotRouteTarget(null, spotRid);
    }

    public ValueTask<ZLinkSpotRoute> ResolveAsync(
        IZLinkSpotRouteResolver resolver,
        CancellationToken cancellationToken)
    {
        return SpotRid is { } rid
            ? resolver.ResolveSpotRouteAsync(rid, cancellationToken)
            : resolver.ResolveSpotRouteAsync(
                SpotName ?? throw new InvalidOperationException("SPOT name is required."),
                cancellationToken);
    }
}

internal sealed class ZLinkCurrentSpotSendCall<TMessage>(
    IZLinkCurrentSpotActivation activation,
    string channelName,
    TMessage message) : IZLinkSendCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkSendCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message);
        return activation.SendChannelAsync(channelName, parts, cancellationToken);
    }
}

internal sealed class ZLinkCurrentSpotRequestCall<TMessage>(
    IZLinkCurrentSpotActivation activation,
    string channelName,
    TMessage request) : IZLinkRequestCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? activation.DefaultTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request);
        var reply = await activation.RequestChannelAsync(channelName, parts, timeout, cancellationToken);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT channel request reply is empty.",
            "SPOT channel request failed.");
    }
}
