using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation :
    IZLinkSpotContext,
    IZLinkCurrentSpotActivation,
    IZLinkSpotHandlerRegistrySink,
    IZLinkSpotOutboundSink,
    IAsyncDisposable
{
    private readonly ZLinkSpotActorDispatchSubmitter _actorDispatchSubmitter;
    private readonly ZLinkSpotActorJoinRegistry _actorJoins = new();
    private readonly ZLinkSpotActorLifecycleCoordinator _actorLifecycle;
    private readonly ZLinkSpotActorMembership _actors = new();
    private readonly ZLinkSpotActivationDispatcher _dispatcher;
    private readonly ZLinkSpotOutboundTransport _outbound;
    private readonly ZLinkSpotOutboundEndpoint _outboundEndpoint;
    private readonly ZLinkSpotPacketRegistry _packets = new();
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly ZLinkSpotSerialExecutor _serial;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotSubscriptionPump _subscriptionPump = new();
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
            defaultRequestTimeout,
            sendTimeout,
            _stopSource.Token);
        _outboundEndpoint = new ZLinkSpotOutboundEndpoint(
            this,
            _scope.ServiceProvider,
            _outbound,
            _runtime,
            "IZLinkSpotContext spot routing requires AddSpotRemoteAddressResolver<TResolver>().");
        Handlers = new ZLinkSpotHandlerRegistrySurface(this);
        Outbound = new ZLinkSpotOutboundSurface(this);
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
            () => HandlerInvoker);
        _actorDispatchSubmitter = new ZLinkSpotActorDispatchSubmitter(_serial, _dispatcher);
        _actorLifecycle = new ZLinkSpotActorLifecycleCoordinator(
            runtime,
            this,
            _actors,
            () => _actorHandlers,
            () => HandlerInvoker);
    }

    public IZLinkSpot Spot => _spot
                              ?? throw new InvalidOperationException("SPOT has not been attached to this context.");

    private ZLinkSpotHandlerInvoker HandlerInvoker => _handlerInvoker
                                                      ?? throw new InvalidOperationException(
                                                          "SPOT has not been attached to this context.");

    public IZLinkBackendSpot NativeSpot { get; }

    public string SpotNodeName { get; }

    public int SubscriptionMessageCount => _subscriptions.MessageCount;

    public int SubscriptionDispatchCount => _subscriptions.DispatchCount;

    public int SubscriptionIgnoreCount => _subscriptions.IgnoreCount;

    public int JoinedActorCount => _actors.Count;

    public string? LastSubscriptionTopic => _subscriptions.LastTopic;

    public string? LastSubscriptionMessageName => _subscriptions.LastMessageName;

    public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    internal string SubscriptionPumpState => _subscriptionPump.State;

    public string ChannelName { get; }

    public TimeSpan DefaultRequestTimeout { get; }

    public ZLinkCodecRegistryBuilder Codecs => _runtime.Registration.Codecs;

    public RoutingId SpotRid => NativeSpot.RoutingId;

    public RoutingId NodeRid { get; }

    private void EnsureConfigurationOpen()
    {
        if (!_configurationOpen)
            throw new InvalidOperationException(
                "SPOT handler registration is only allowed while IZLinkSpot.Configure is running.");
    }
}