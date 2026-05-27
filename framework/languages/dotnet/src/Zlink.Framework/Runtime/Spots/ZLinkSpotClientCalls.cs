namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotClientService(IServiceProvider services) : IZLinkSpotClient
{
    public IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRemoteAddressResolver(),
            ZLinkSpotRemoteAddressTarget.ByRoutingId(spotRid),
            message);
    }

    public IZLinkRequestCall RequestSpot<TMessage>(RoutingId spotRid, TMessage request)
    {
        return new ZLinkRoutedSpotRequestCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRemoteAddressResolver(),
            ZLinkSpotRemoteAddressTarget.ByRoutingId(spotRid),
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

    public IZLinkPublishCall PublishSpot<TEvent>(string topic, TEvent message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().PublishSpot(topic, message);
    }

    private IZLinkSpotRemoteAddressResolver RequireRemoteAddressResolver()
    {
        return services.GetService(typeof(IZLinkSpotRemoteAddressResolver)) is IZLinkSpotRemoteAddressResolver resolver
            ? resolver
            : throw new ZLinkConfigurationException(
                "IZLinkSpotClient remote address lookup requires AddSpotRemoteAddressResolver<TResolver>().");
    }
}

internal sealed class ZLinkRoutedSpotSendCall<TMessage>(
    IZLinkCurrentSpotActivation activation,
    IZLinkSpotRemoteAddressResolver resolver,
    ZLinkSpotRemoteAddressTarget target,
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
        var remoteAddress = await ResolveRemoteAddressAsync(cancellationToken).ConfigureAwait(false);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            remoteAddress.RouterChannelId,
            _messageName ?? throw new InvalidOperationException("Message name is required."));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message);
        await activation.SendSpotAsync(
            remoteAddress.RouterChannelId,
            remoteAddress.TargetNodeRid,
            remoteAddress.SpotRid,
            parts,
            cancellationToken).ConfigureAwait(false);
    }

    private ValueTask<ZLinkSpotRemoteAddress> ResolveRemoteAddressAsync(CancellationToken cancellationToken)
    {
        return target.ResolveAsync(resolver, cancellationToken);
    }
}

internal sealed class ZLinkRoutedSpotRequestCall<TRequest>(
    IZLinkCurrentSpotActivation activation,
    IZLinkSpotRemoteAddressResolver resolver,
    ZLinkSpotRemoteAddressTarget target,
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
        var remoteAddress = await ResolveRemoteAddressAsync(cancellationToken).ConfigureAwait(false);
        var timeout = _timeout ?? activation.DefaultTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            remoteAddress.RouterChannelId,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request);
        var reply = await activation.RequestSpotAsync(
            remoteAddress.RouterChannelId,
            remoteAddress.TargetNodeRid,
            remoteAddress.SpotRid,
            parts,
            timeout,
            cancellationToken).ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT request reply is empty.",
            "SPOT request failed.");
    }

    private ValueTask<ZLinkSpotRemoteAddress> ResolveRemoteAddressAsync(CancellationToken cancellationToken)
    {
        return target.ResolveAsync(resolver, cancellationToken);
    }
}

internal readonly record struct ZLinkSpotRemoteAddressTarget(
    RoutingId SpotRid)
{
    public static ZLinkSpotRemoteAddressTarget ByRoutingId(RoutingId spotRid)
    {
        return new ZLinkSpotRemoteAddressTarget(spotRid);
    }

    public ValueTask<ZLinkSpotRemoteAddress> ResolveAsync(
        IZLinkSpotRemoteAddressResolver resolver,
        CancellationToken cancellationToken)
    {
        return resolver.ResolveSpotRemoteAddressAsync(SpotRid, cancellationToken);
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
