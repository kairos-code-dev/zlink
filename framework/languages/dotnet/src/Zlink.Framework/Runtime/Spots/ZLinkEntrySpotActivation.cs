using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkEntrySpotActivation : IZLinkEntrySpotContext, IZLinkCurrentSpotActivation, IAsyncDisposable
{
    private static readonly AsyncLocal<ZLinkEntrySpotActivation?> Current = new();
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly IZLinkBackendSpot _nativeSpot;
    private readonly ZLinkSpotPacketRegistry _packets = new();
    private readonly ZLinkSpotActorJoinRegistry _actorJoins = new();
    private readonly ZLinkSpotActorMembership _actors = new();
    private readonly ZLinkSpotSubscriptionRegistry _subscriptions = new();
    private readonly ZLinkSpotTimerRegistry _timers = new();
    private readonly ZLinkSpotActorHandlerRegistry _actorHandlers;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly SemaphoreSlim _routeDrainGate = new(1, 1);
    private readonly SemaphoreSlim _actorJoinDrainGate = new(1, 1);
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotOutboundTransport _outbound;
    private readonly ZLinkSpotHandlerInvoker _invoker;
    private readonly ZLinkSpotActivationDispatcher _dispatcher;
    private readonly ZLinkEntrySpotHandlerExecutor _handlerExecutor;
    private readonly ZLinkSpotSubscriptionPump _subscriptionPump = new();
    private readonly TimeSpan _defaultTimeout;
    private bool _configurationOpen = true;
    private int _disposed;

    public ZLinkEntrySpotActivation(
        ZLinkFrameworkRuntime runtime,
        IServiceProvider services,
        IZLinkBackendSpot nativeSpot,
        Type entrySpotType,
        RoutingId nodeRid,
        string spotNodeName,
        string channelName,
        TimeSpan defaultTimeout,
        TimeSpan? sendTimeout,
        Func<string, ZLinkAsyncSubmitter?>? channelSubmitter = null)
    {
        _runtime = runtime;
        _nativeSpot = nativeSpot;
        NodeRid = nodeRid;
        SpotNodeName = spotNodeName;
        ChannelName = channelName;
        _defaultTimeout = defaultTimeout;
        _scope = services.CreateAsyncScope();
        EntrySpot = (IZLinkEntrySpot)ActivatorUtilities.CreateInstance(
            _scope.ServiceProvider,
            entrySpotType,
            this);
        if (!ReferenceEquals(EntrySpot.Context, this))
        {
            throw new InvalidOperationException(
                $"Entry SPOT '{entrySpotType.FullName}' must expose the context provided by the runtime.");
        }

        _invoker = new ZLinkSpotHandlerInvoker(_scope.ServiceProvider, EntrySpot);
        _handlerExecutor = new ZLinkEntrySpotHandlerExecutor(services, EntrySpot);
        _actorHandlers = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.EntrySpot,
            EntrySpot.GetType());
        _outbound = new ZLinkSpotOutboundTransport(
            nativeSpot,
            defaultTimeout,
            sendTimeout,
            _stopSource.Token,
            channelSubmitter);
        _dispatcher = new ZLinkSpotActivationDispatcher(
            runtime,
            nativeSpot,
            channelName,
            _packets,
            _actorJoins,
            _actors,
            _subscriptions,
            () => _actorHandlers,
            () => _invoker);
    }

    public IZLinkEntrySpot EntrySpot { get; }

    public string ChannelName { get; }

    public string SpotNodeName { get; }

    public TimeSpan DefaultTimeout => _defaultTimeout;

    public RoutingId SpotRid => _nativeSpot.RoutingId;

    public RoutingId NodeRid { get; }

    public void Configure()
    {
        EntrySpot.Configure();
        _configurationOpen = false;
        _packets.Bind(EntrySpot);
        _subscriptions.Bind(EntrySpot, _nativeSpot);
        _actorJoins.Bind(EntrySpot);
        _actorHandlers.Bind();
    }

    public async ValueTask InitializeAsync(CancellationToken cancellationToken)
    {
        _subscriptionPump.StartIfNeeded(
            _subscriptions.HasSubscriptions,
            _stopSource.Token,
            DispatchSubscriptionsAsync);

        await ExecuteAsync(
            static (activation, ct) => activation.EntrySpot.OnInitializeAsync(ct),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        await ExecuteAsync(
            static (activation, ct) => activation.EntrySpot.OnClosingAsync(ct),
            cancellationToken).ConfigureAwait(false);
    }

    public bool TryResolveActorPacket(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        return _actorHandlers.TryResolve(actorType, header, out descriptor);
    }

    public bool TryResolvePacket(
        ZLinkEnvelopeHeader header,
        out ZLinkSpotDescriptor? descriptor)
    {
        return _packets.TryResolve(header, out descriptor);
    }

    public async ValueTask InvokePacketAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        using var _ = ZLinkSpotAmbientContext.Push(this);
        await _invoker.InvokePacketAsync(descriptor, message, cancellationToken)
            .ConfigureAwait(false);
    }

    public bool TryResolveActorJoined(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return _actorHandlers.TryResolveJoined(actorType, out descriptor);
    }

    public bool TryResolveActorLeft(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return _actorHandlers.TryResolveLeft(actorType, out descriptor);
    }

    public bool TryResolveActorDisconnected(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return _actorHandlers.TryResolveDisconnected(actorType, out descriptor);
    }

    public ValueTask InvokeActorPacketAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return InvokeActorPacketWithoutLifecycleGateAsync(
            descriptor,
            actor,
            header,
            body,
            cancellationToken);
    }

    public async ValueTask<byte[]> InvokeActorPacketForReplyAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var reply = await InvokeActorPacketForReplyWithoutLifecycleGateAsync(
                descriptor,
                actor,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);

        return reply
            ?? throw new InvalidOperationException(
                $"Entry Spot actor packet reply for '{descriptor.MessageName}' was null.");
    }

    public ValueTask InvokeActorLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        ZLinkSpotActorChangeResult context,
        CancellationToken cancellationToken)
    {
        return InvokeActorLifecycleWithoutGateAsync(
            descriptor,
            actor,
            context,
            cancellationToken);
    }

    public ValueTask InvokeActorDisconnectedAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return InvokeActorDisconnectedWithoutGateAsync(
            descriptor,
            actor,
            cancellationToken);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _stopSource.Cancel();
        await _subscriptionPump.StopAsync();
        await _timers.DisposeAsync().ConfigureAwait(false);
        await _outbound.DisposeAsync().ConfigureAwait(false);
        _gate.Dispose();
        _routeDrainGate.Dispose();
        _actorJoinDrainGate.Dispose();
        _stopSource.Dispose();
        await _scope.DisposeAsync().ConfigureAwait(false);
    }

    private sealed record ActorLifecycleState(
        ZLinkSpotActorLifecycleDescriptor Descriptor,
        IZLinkActor Actor,
        ZLinkSpotActorChangeResult Context);

    private async ValueTask InvokeActorLifecycleWithoutGateAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        ZLinkSpotActorChangeResult context,
        CancellationToken cancellationToken)
    {
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            await _invoker.InvokeActorLifecycleAsync(
                    descriptor,
                    actor,
                    context,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
        }
    }

    private async ValueTask InvokeActorDisconnectedWithoutGateAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        var previous = Current.Value;
        Current.Value = this;
        try
        {
            using var _ = ZLinkSpotAmbientContext.Push(this);
            await _invoker.InvokeActorDisconnectedAsync(
                    descriptor,
                    actor,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            Current.Value = previous;
        }
    }

}
