using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundService(IServiceProvider services) : IZLinkSpotOutbound
{
    public IZLinkSendCall SendToSpot<TMessage>(RoutingId spotRid, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRemoteAddressResolver(),
            ZLinkSpotRemoteAddressTarget.ByRoutingId(spotRid),
            message);
    }

    public IZLinkRequestCall RequestToSpot<TMessage>(RoutingId spotRid, TMessage request)
    {
        return new ZLinkRoutedSpotRequestCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireRemoteAddressResolver(),
            ZLinkSpotRemoteAddressTarget.ByRoutingId(spotRid),
            request);
    }

    public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Publish(topic, message);
    }

    public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().SendToChannel(channelName, message);
    }

    public IZLinkRequestCall RequestToChannel<TMessage>(string channelName, TMessage request)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().RequestToChannel(channelName, request);
    }

    private IZLinkSpotRemoteAddressResolver RequireRemoteAddressResolver()
    {
        return services.GetService(typeof(IZLinkSpotRemoteAddressResolver)) is IZLinkSpotRemoteAddressResolver resolver
            ? resolver
            : throw new ZLinkConfigurationException(
                "IZLinkSpotOutbound remote address lookup requires AddSpotRemoteAddressResolver<TResolver>().");
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

    public void Submit(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(SubmitAsync(cancellationToken), "spot client submit");
    }

    private async ValueTask SubmitAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var remoteAddress = await ResolveRemoteAddressAsync(cancellationToken).ConfigureAwait(false);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            remoteAddress.RouterChannelId,
            _messageName ?? throw new InvalidOperationException("Message name is required."));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, activation.Codecs);
        await activation.SendToSpotAsync(
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
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
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

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var remoteAddress = await ResolveRemoteAddressAsync(cancellationToken).ConfigureAwait(false);
        var timeout = _timeout ?? activation.DefaultRequestTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            remoteAddress.RouterChannelId,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request, activation.Codecs);
        var reply = await activation.RequestToSpotAsync(
            remoteAddress.RouterChannelId,
            remoteAddress.TargetNodeRid,
            remoteAddress.SpotRid,
            parts,
            timeout,
            cancellationToken).ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT request reply is empty.",
            "SPOT request failed.",
            activation.Codecs);
    }

    public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
    {
        return RequireTurn().YieldFrameworkCallAsync(Async<TReply>, cancellationToken);
    }

    private ValueTask<ZLinkSpotRemoteAddress> ResolveRemoteAddressAsync(CancellationToken cancellationToken)
    {
        return target.ResolveAsync(resolver, cancellationToken);
    }

    private ZLinkSerialTurn RequireTurn()
    {
        return _turn
               ?? throw new InvalidOperationException(
                   "Yield requires a framework Spot handler turn captured when the call object was created.");
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

    public void Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, activation.Codecs);
        ZLinkUnawaitedSubmit.Observe(activation.SendToChannelAsync(channelName, parts, cancellationToken), "spot channel submit");
    }
}

internal sealed class ZLinkCurrentSpotRequestCall<TMessage>(
    IZLinkCurrentSpotActivation activation,
    string channelName,
    TMessage request) : IZLinkRequestCall
{
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
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

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? activation.DefaultRequestTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request, activation.Codecs);
        var reply = await activation.RequestToChannelAsync(channelName, parts, timeout, cancellationToken);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT channel request reply is empty.",
            "SPOT channel request failed.",
            activation.Codecs);
    }

    public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
    {
        return RequireTurn().YieldFrameworkCallAsync(Async<TReply>, cancellationToken);
    }

    private ZLinkSerialTurn RequireTurn()
    {
        return _turn
               ?? throw new InvalidOperationException(
                   "Yield requires a framework Spot handler turn captured when the call object was created.");
    }
}
