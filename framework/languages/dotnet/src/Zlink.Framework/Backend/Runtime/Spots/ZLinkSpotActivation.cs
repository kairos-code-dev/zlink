using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Protocol;

namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation : IZLinkSpotContext, IAsyncDisposable
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotSerialExecutor _serial;
    private readonly ZLinkSpotPacketRegistry _packets = new();
    private readonly ZLinkSpotActorJoinRegistry _actorJoins = new();
    private ZLinkSpotActorHandlerRegistry? _actorHandlers;
    private readonly ZLinkSpotActorMembership _actors = new();
    private readonly ZLinkSpotSubscriptionRegistry _subscriptions = new();
    private ZLinkSpotHandlerInvoker? _handlerInvoker;
    private IZLinkSpot? _spot;
    private readonly ZLinkSpotTimerRegistry _timers = new();
    private readonly ZLinkSpotActivationDispatcher _dispatcher;
    private readonly ZLinkSpotActorDispatchSubmitter _actorDispatchSubmitter;
    private readonly ZLinkSpotActorLifecycleCoordinator _actorLifecycle;
    private readonly ZLinkSpotOutboundTransport _outbound;
    private readonly ZLinkSpotSubscriptionPump _subscriptionPump = new();
    private readonly TimeSpan _defaultTimeout;
    private int _disposed;
    private bool _configurationOpen = true;

    public ZLinkSpotActivation(
        ZLinkFrameworkRuntime runtime,
        AsyncServiceScope scope,
        IZLinkBackendSpot nativeSpot,
        RoutingId nodeRid,
        string spotName,
        string channelName,
        TimeSpan defaultTimeout,
        TimeSpan? sendTimeout)
    {
        _runtime = runtime;
        _scope = scope;
        NativeSpot = nativeSpot;
        NodeRid = nodeRid;
        SpotName = spotName;
        ChannelName = channelName;
        _defaultTimeout = defaultTimeout;
        _outbound = new ZLinkSpotOutboundTransport(
            nativeSpot,
            defaultTimeout,
            sendTimeout,
            _stopSource.Token);
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
            () => HandlerInvoker,
            () => Spot);
    }

    public IZLinkSpot Spot => _spot
        ?? throw new InvalidOperationException("SPOT has not been attached to this context.");

    private ZLinkSpotHandlerInvoker HandlerInvoker => _handlerInvoker
        ?? throw new InvalidOperationException("SPOT has not been attached to this context.");

    public IZLinkBackendSpot NativeSpot { get; }

    public string SpotName { get; }

    public string ChannelName { get; }

    public TimeSpan DefaultTimeout => _defaultTimeout;

    public RoutingId SpotRid => NativeSpot.RoutingId;

    public ZLinkSpotId SpotId => ZLinkSpotId.FromRoutingId(SpotRid);

    public RoutingId NodeRid { get; }

    public int SubscriptionMessageCount => _subscriptions.MessageCount;

    public int SubscriptionDispatchCount => _subscriptions.DispatchCount;

    public int SubscriptionIgnoreCount => _subscriptions.IgnoreCount;

    public string? LastSubscriptionTopic => _subscriptions.LastTopic;

    public string? LastSubscriptionMessageName => _subscriptions.LastMessageName;

    public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    internal string SubscriptionPumpState => _subscriptionPump.State;

    public ValueTask JoinActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? JoinActorCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) => activation.JoinActorCoreAsync(state, ct),
                actor,
                cancellationToken);
    }

    public ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? LeaveActorCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) => activation.LeaveActorCoreAsync(state, ct),
                actor,
                cancellationToken);
    }

    public async ValueTask InitializeAsync(CancellationToken cancellationToken)
    {
        RegisterWithoutSynchronizationContext(() =>
        {
            ZLinkSpotNativeDispatchRouter.Attach(
                NativeSpot,
                routeReadable: () => QueueSerialized(
                    static (activation, ct) => activation._dispatcher.DispatchRouteDrainAsync(ct)),
                channelReplyReadable: drain => drain?.Invoke(),
                actorJoinReadable: () => QueueSerialized(
                    static (activation, ct) => activation._dispatcher.DispatchActorJoinDrainAsync(ct)),
                actorPartsReadable: actorParts => QueueSerialized(
                    static (activation, state, ct) => activation._dispatcher.DispatchActorPartsAsync(state, ct),
                    actorParts));

            return 0;
        });

        _subscriptionPump.StartIfNeeded(
            _subscriptions.HasSubscriptions,
            StopToken,
            ct => ExecuteSerializedAsync(
                static (activation, innerCt) => activation.DispatchSubscriptionsAsync(innerCt),
                ct));

        await ExecuteSerializedAsync(
            static (activation, ct) => activation.Spot.OnInitializeAsync(ct),
            cancellationToken);
    }

    public ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken)
        where THandler : class
    {
        return _timers.AddAsync(
            name,
            period,
            typeof(THandler),
            Spot.GetType(),
            StopToken,
            (descriptor, ct) => ExecuteSerializedAsync(
                async static (activation, state, innerCt) =>
                {
                    await activation.InvokeTimerAsync(state, innerCt);
                },
                descriptor,
                ct),
            cancellationToken);
    }

    public CancellationToken StopToken => _stopSource.Token;

    public bool TryResolveActorJoinDescriptor(
        Type requestType,
        out ZLinkSpotActorJoinDescriptor? descriptor)
    {
        return _actorJoins.TryResolve(requestType, out descriptor);
    }

    public bool TryResolveActorPacketDescriptor(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        descriptor = null;
        return _actorHandlers is not null
            && _actorHandlers.TryResolve(actorType, header, out descriptor);
    }

    public async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!_actorJoins.TryResolve(typeof(TRequest), out var descriptor)
            || descriptor is null)
        {
            throw new InvalidOperationException(
                $"SPOT '{Spot.GetType()}' does not register an actor join handler for '{typeof(TRequest)}'.");
        }

        var state = new ActorJoinCallState(actor, request, descriptor);
        await ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                state.Reply = await activation.InvokeActorJoinAsync(
                    state.Descriptor,
                    state.Actor,
                    state.Request,
                    ct);
            },
            state,
            cancellationToken);

        return (TReply?)state.Reply
            ?? throw new InvalidOperationException(
                $"SPOT actor join reply for '{typeof(TRequest)}' was null.");
    }

    public async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await _actorDispatchSubmitter.SubmitAsync(
                actor,
                runtimeState,
                header,
                body,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<byte[]> SubmitActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return await _actorDispatchSubmitter
            .SubmitForReplyAsync(actor, runtimeState, header, body, cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
            static (activation, ct) => activation.Spot.OnClosingAsync(ct),
            cancellationToken);
    }

    public ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                await activation.LeaveActorCoreAsync(state, ct);
                await state.OnDisconnectedAsync(ct);
            },
            actor,
            cancellationToken);
    }

    private async ValueTask JoinActorCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await _actorLifecycle.JoinAsync(actor, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask LeaveActorCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await _actorLifecycle.LeaveAsync(actor, cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _stopSource.Cancel();
        await _subscriptionPump.StopAsync();

        await _serial.DisposeAsync();
        await _timers.DisposeAsync();
        await _outbound.DisposeAsync();
        await NativeSpot.DisposeAsync();
        _stopSource.Dispose();
        await _scope.DisposeAsync();
    }

    private async ValueTask ExecuteSerializedAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        await _serial.ExecuteAsync(operation, cancellationToken).ConfigureAwait(false);
    }

    private void EnsureConfigurationOpen()
    {
        if (!_configurationOpen)
        {
            throw new InvalidOperationException(
                "SPOT handler registration is only allowed while IZLinkSpot.Configure is running.");
        }
    }

    private async ValueTask ExecuteSerializedAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        await _serial.ExecuteAsync(operation, state, cancellationToken).ConfigureAwait(false);
    }

    private void QueueSerialized(Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation)
    {
        _serial.Queue(operation);
    }

    private void QueueSerialized<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state)
    {
        var capturedOp = operation;
        var capturedState = state;
        _serial.Queue((activation, ct) => capturedOp(activation, capturedState, ct));
    }

    private async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        await _dispatcher
            .DispatchSubscriptionsAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        await HandlerInvoker.InvokeTimerAsync(descriptor, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        object request,
        CancellationToken cancellationToken)
    {
        return await HandlerInvoker.InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
            .ConfigureAwait(false);
    }

    private static T RegisterWithoutSynchronizationContext<T>(Func<T> action)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            return action();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    private sealed class ActorJoinCallState(
        IZLinkActor actor,
        object request,
        ZLinkSpotActorJoinDescriptor descriptor)
    {
        public IZLinkActor Actor { get; } = actor;

        public object Request { get; } = request;

        public ZLinkSpotActorJoinDescriptor Descriptor { get; } = descriptor;

        public object? Reply { get; set; }
    }

}
