using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkRoutedSpotClientService(ZLinkFrameworkRuntime runtime)
    : IZLinkRoutedSpotClient
{
    public IZLinkRoutedSpotChannelClient ViaEgressChannel(string localEgressChannelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(localEgressChannelName);
        return new ZLinkRoutedSpotChannelClient(runtime, localEgressChannelName);
    }

    public ValueTask<IZLinkSpotRef> BindSpotHandleAsync(
        ZLinkSpotRemoteAddress remoteAddress,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkSpotRef>(new ZLinkSpotRef(
            remoteAddress.SpotRid,
            remoteAddress.SpotKind,
            isRemote: true,
            remoteAddress));
    }

    public ValueTask<IZLinkSpotRef> BindSpotHandleAsync(
        IZLinkSpotRef spot,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(spot);
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(spot);
    }
}

internal sealed class ZLinkRoutedSpotChannelClient(
    ZLinkFrameworkRuntime runtime,
    string localEgressChannelName) : IZLinkRoutedSpotChannelClient
{
    public IZLinkSendCall SendSpot<TMessage>(
        RoutingId spotRid,
        TMessage message)
    {
        return new ZLinkExplicitRoutedSpotSendCall<TMessage>(
            runtime,
            localEgressChannelName,
            spotRid,
            message);
    }

    public IZLinkSendCall SendSpot<TMessage>(
        IZLinkSpotRef spot,
        TMessage message)
    {
        ArgumentNullException.ThrowIfNull(spot);
        if (!spot.IsRemote)
        {
            throw new InvalidOperationException("Routed SPOT send requires a remote SPOT ref.");
        }

        return SendSpot(spot.RemoteAddress.SpotRid, message);
    }

    public IZLinkRequestCall RequestSpot<TRequest>(
        RoutingId spotRid,
        TRequest request)
    {
        return new ZLinkExplicitRoutedSpotRequestCall<TRequest>(
            runtime,
            localEgressChannelName,
            spotRid,
            request);
    }

    public IZLinkRequestCall RequestSpot<TRequest>(
        IZLinkSpotRef spot,
        TRequest request)
    {
        ArgumentNullException.ThrowIfNull(spot);
        if (!spot.IsRemote)
        {
            throw new InvalidOperationException("Routed SPOT request requires a remote SPOT ref.");
        }

        return RequestSpot(spot.RemoteAddress.SpotRid, request);
    }
}

internal sealed class ZLinkSpotRef(
    RoutingId spotRid,
    ZLinkSpotKind spotKind,
    bool isRemote,
    ZLinkSpotRemoteAddress remoteAddress) : IZLinkSpotRef
{
    public RoutingId SpotRid { get; } = spotRid;

    public ZLinkSpotKind SpotKind { get; } = spotKind;

    public bool IsRemote { get; } = isRemote;

    public ZLinkSpotRemoteAddress RemoteAddress { get; } = remoteAddress;
}

internal sealed class ZLinkExplicitRoutedSpotSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string localEgressChannelName,
    RoutingId spotRid,
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
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            localEgressChannelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message);
        await runtime.SendSpotViaEgressChannelAsync(
                localEgressChannelName,
                spotRid,
                parts,
                cancellationToken)
            .ConfigureAwait(false);
    }
}

internal sealed class ZLinkExplicitRoutedSpotRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    string localEgressChannelName,
    RoutingId spotRid,
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
        var timeout = _timeout ?? runtime.Registration.DefaultTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            localEgressChannelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request);
        var reply = await runtime.RequestSpotViaEgressChannelAsync(
                localEgressChannelName,
                spotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "Routed SPOT request reply is empty.",
            "Routed SPOT request failed.");
    }
}
