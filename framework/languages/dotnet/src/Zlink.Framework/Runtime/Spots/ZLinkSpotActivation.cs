using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation :
    IZLinkSpotContext,
    IZLinkCurrentSpotActivation,
    IZLinkSpotHandlerRegistrySink,
    IAsyncDisposable
{
    private readonly ZLinkSpotActorDispatchSubmitter _actorDispatchSubmitter;
    private readonly ZLinkSpotActorJoinRegistry _actorJoins = new();
    private readonly ZLinkSpotActorMembership _actors = new();
    private readonly ZLinkSpotActivationDispatcher _dispatcher;
    private readonly ZLinkSpotOutboundTransport _outbound;
    private readonly ZLinkSpotOutboundEndpoint _outboundEndpoint;
    private readonly ZLinkSpotPacketRegistry _packets = new();
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly ZLinkSpotSerialExecutor _serial;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotSubscriptionRegistry _subscriptions = new();
    private readonly ZLinkSpotTimerRegistry _timers = new();
    private ZLinkSpotActorHandlerRegistry? _actorHandlers;
    private bool _configurationOpen = true;
    private int _disposed;
    private ZLinkSpotHandlerInvoker? _handlerInvoker;
    private IZLinkSpot? _spot;

    public ZLinkSpotActivation(
        ZLinkFrameworkRuntime runtime,
        AsyncServiceScope scope,
        IZLinkBackendSpot nativeSpot,
        RoutingId nodeRid,
        string spotNodeName,
        string channelName,
        TimeSpan defaultRequestTimeout,
        TimeSpan? sendTimeout)
    {
        _runtime = runtime;
        _scope = scope;
        NativeSpot = nativeSpot;
        NodeRid = nodeRid;
        SpotNodeName = spotNodeName;
        ChannelName = channelName;
        DefaultRequestTimeout = defaultRequestTimeout;
        _outbound = new ZLinkSpotOutboundTransport(
            nativeSpot,
            sendTimeout,
            _stopSource.Token);
        _outboundEndpoint = new ZLinkSpotOutboundEndpoint(
            this,
            _outbound,
            _runtime);
        Handlers = new ZLinkSpotHandlerRegistrySurface(this);
        Outbound = _outboundEndpoint;
        _serial = new ZLinkSpotSerialExecutor(this, () => IsDisposed, _stopSource.Token);
        _dispatcher = new ZLinkSpotActivationDispatcher(
            runtime,
            nativeSpot,
            channelName,
            _packets,
            _actorJoins,
            _actors,
            _subscriptions,
            () => _actorHandlers,
            () => HandlerInvoker,
            CommitNativeActorJoinAsync);
        _actorDispatchSubmitter = new ZLinkSpotActorDispatchSubmitter(_serial, _dispatcher.ActorPackets);
    }

    public IZLinkSpot Spot => _spot
                              ?? throw new InvalidOperationException("SPOT has not been attached to this context.");

    private ZLinkSpotHandlerInvoker HandlerInvoker => _handlerInvoker
                                                      ?? throw new InvalidOperationException(
                                                          "SPOT has not been attached to this context.");

    public IZLinkBackendSpot NativeSpot { get; }

    public string SpotNodeName { get; }

    public int JoinedActorCount => _actors.Count;

    public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    public string ChannelName { get; }

    public TimeSpan DefaultRequestTimeout { get; }

    public ZLinkCodecRegistryBuilder Codecs => _runtime.Registration.Codecs;

    public IZLinkSpotHandlerRegistry Handlers { get; }

    public IZLinkSpotOutbound Outbound { get; }

    ZLinkSpotOutboundEndpoint IZLinkCurrentSpotActivation.OutboundEndpoint => _outboundEndpoint;

    public RoutingId SpotRid => NativeSpot.RoutingId;

    public RoutingId NodeRid { get; }

    private void EnsureConfigurationOpen()
    {
        if (!_configurationOpen)
            throw new InvalidOperationException(
                "SPOT handler registration is only allowed while IZLinkSpot.Configure is running.");
    }

    private ValueTask CommitNativeActorJoinAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return JoinActorToSpotCoreAsync(actor, cancellationToken);
    }
}
