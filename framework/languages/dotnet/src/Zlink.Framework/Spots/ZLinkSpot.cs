namespace Zlink.Framework;

public abstract class ZLinkSpot
{
    private readonly List<ZLinkSpotPacketRegistration> _packets = [];
    private readonly List<ZLinkSpotSubscriptionRegistration> _subscriptions = [];
    private ZLinkSpotRuntimeContext? _runtimeContext;

    protected ZLinkSpot(
        global::Zlink.RoutingId spotRid,
        global::Zlink.RoutingId nodeRid)
    {
        SpotRid = spotRid;
        NodeRid = nodeRid;
    }

    public global::Zlink.RoutingId SpotRid { get; }

    public global::Zlink.RoutingId NodeRid { get; }

    internal IReadOnlyList<ZLinkSpotPacketRegistration> Packets => _packets;

    internal IReadOnlyList<ZLinkSpotSubscriptionRegistration> Subscriptions => _subscriptions;

    internal void AttachRuntime(ZLinkSpotRuntimeContext runtimeContext)
    {
        _runtimeContext = runtimeContext;
    }

    protected void AddPacket<THandler>()
        where THandler : class
    {
        _packets.Add(new ZLinkSpotPacketRegistration(typeof(THandler)));
    }

    protected void AddSubscribe<THandler>(string topic)
        where THandler : class
    {
        if (string.IsNullOrWhiteSpace(topic))
        {
            throw new ZLinkConfigurationException("SPOT subscription topic must not be empty.");
        }

        _subscriptions.Add(new ZLinkSpotSubscriptionRegistration(topic, typeof(THandler)));
    }

    protected IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
    {
        if (_runtimeContext is null)
        {
            throw new InvalidOperationException("SPOT runtime is not attached yet.");
        }

        return _runtimeContext.Publish(topic, message);
    }

    protected ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken = default)
        where THandler : class
    {
        if (_runtimeContext is null)
        {
            throw new InvalidOperationException("SPOT runtime is not attached yet.");
        }

        return _runtimeContext.AddTimerAsync<THandler>(name, period, cancellationToken);
    }

    public virtual ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }
}

internal sealed record ZLinkSpotPacketRegistration(Type HandlerType);

internal sealed record ZLinkSpotSubscriptionRegistration(string Topic, Type HandlerType);

internal interface ZLinkSpotRuntimeContext
{
    IZLinkPublishCall Publish<TEvent>(string topic, TEvent message);

    ValueTask<IZLinkTimer> AddTimerAsync<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken)
        where THandler : class;
}
