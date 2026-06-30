using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkEntrySpotActivation :
    IZLinkEntrySpotContext,
    IZLinkCurrentSpotActivation,
    IZLinkSpotHandlerRegistrySink,
    IZLinkSpotOutboundSink,
    IAsyncDisposable
{
    private static readonly AsyncLocal<ZLinkEntrySpotActivation?> Current = new();
    private readonly ZLinkSpotActorHandlerRegistry _actorHandlers;
    private readonly SemaphoreSlim _actorJoinDrainGate = new(1, 1);
    private readonly ZLinkSpotActorJoinRegistry _actorJoins = new();
    private readonly ZLinkSpotActorMembership _actors = new();
    private readonly ZLinkSpotActivationDispatcher _dispatcher;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ZLinkEntrySpotHandlerExecutor _handlerExecutor;
    private readonly ZLinkSpotHandlerInvoker _invoker;
    private readonly IZLinkBackendSpot _nativeSpot;
    private readonly ZLinkSpotOutboundTransport _outbound;
    private readonly ZLinkSpotOutboundEndpoint _outboundEndpoint;
    private readonly ZLinkSpotPacketRegistry _packets = new();
    private readonly SemaphoreSlim _routeDrainGate = new(1, 1);
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly AsyncServiceScope _scope;
    private readonly ZLinkSerialExecutionQueue _serial;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotSubscriptionPump _subscriptionPump = new();
    private readonly ZLinkSpotSubscriptionRegistry _subscriptions = new();
    private readonly ZLinkSpotTimerRegistry _timers = new();
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
        TimeSpan defaultRequestTimeout,
        TimeSpan? sendTimeout)
    {
        _runtime = runtime;
        _nativeSpot = nativeSpot;
        NodeRid = nodeRid;
        SpotNodeName = spotNodeName;
        ChannelName = channelName;
        DefaultRequestTimeout = defaultRequestTimeout;
        var errorSink = new ZLinkRuntimeErrorSink();
        _serial = new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(errorSink, _stopSource.Token),
            errorSink,
            _stopSource.Token);
        _scope = services.CreateAsyncScope();
        EntrySpot = (IZLinkEntrySpot)ActivatorUtilities.CreateInstance(
            _scope.ServiceProvider,
            entrySpotType,
            this);
        if (!ReferenceEquals(EntrySpot.Context, this))
            throw new InvalidOperationException(
                $"Entry SPOT '{entrySpotType.FullName}' must expose the context provided by the runtime.");

        _invoker = new ZLinkSpotHandlerInvoker(
            _scope.ServiceProvider,
            EntrySpot,
            _runtime.Registration.Codecs,
            _runtime.Registration.StreamCompressionCodec);
        _handlerExecutor = new ZLinkEntrySpotHandlerExecutor(
            services,
            EntrySpot,
            _runtime.Registration.Codecs,
            _runtime.Registration.StreamCompressionCodec);
        _actorHandlers = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.EntrySpot,
            EntrySpot.GetType());
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
            "IZLinkEntrySpotContext spot routing requires AddSpotRemoteAddressResolver<TResolver>().");
        Handlers = new ZLinkSpotHandlerRegistrySurface(this);
        Outbound = new ZLinkSpotOutboundSurface(this);
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

    public string SpotNodeName { get; }

    private bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;

        _stopSource.Cancel();
        await _subscriptionPump.StopAsync();
        await _timers.DisposeAsync().ConfigureAwait(false);
        await _serial.DisposeAsync().ConfigureAwait(false);
        await _outbound.DisposeAsync().ConfigureAwait(false);
        _stopSource.Dispose();
        await _scope.DisposeAsync().ConfigureAwait(false);
    }

    public string ChannelName { get; }

    public TimeSpan DefaultRequestTimeout { get; }

    public ZLinkCodecRegistryBuilder Codecs => _runtime.Registration.Codecs;

    public RoutingId SpotRid => _nativeSpot.RoutingId;

    public RoutingId NodeRid { get; }

    public async ValueTask DestroyActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        await _runtime.DestroyActorAsync(
                NodeRid,
                actor,
                cancellationToken)
            .ConfigureAwait(false);
        _actors.RemoveIfCurrent(actor);
    }

    public IZLinkWorkerCall<TResult> RunWorker<TResult>(
        Func<CancellationToken, TResult> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        return new ZLinkWorkerCall<TResult>(
            _runtime.WorkerPool,
            work,
            callback => QueueSerialized((_, ct) => callback(ct)));
    }

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

    public bool TryResolveActorJoin(
        out ZLinkSpotActorJoinDescriptor? descriptor)
    {
        return _actorJoins.TryResolve(out descriptor);
    }

    public async ValueTask<ZLinkSpotActorJoinResult> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var call = new ActorJoinCallState(descriptor, actor, request);
        await ExecuteAsync(
            static async (activation, state, ct) =>
            {
                state.Result = await activation._invoker.InvokeActorJoinAsync(
                        state.Descriptor,
                        state.Actor,
                        state.Request,
                        ct)
                    .ConfigureAwait(false);
            },
            call,
            cancellationToken).ConfigureAwait(false);

        return call.Result;
    }

    public async ValueTask InvokePacketAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await ExecuteAsync(
            static (activation, state, ct) => activation._invoker.InvokePacketAsync(
                state.Descriptor,
                state.Message,
                ct),
            (Descriptor: descriptor, Message: message),
            cancellationToken).ConfigureAwait(false);
    }

    public bool TryResolveActorJoined(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return _actorHandlers.TryResolveJoined(actorType, out descriptor);
    }

    public bool TryResolveActorCreated(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        return _actorHandlers.TryResolveCreated(actorType, out descriptor);
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

    public async ValueTask InvokeActorPacketAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await ExecuteActorPacketAsync(
            static (activation, state, ct) => activation._handlerExecutor.InvokeActorPacketAsync(
                state.Descriptor,
                state.Actor,
                state.Header,
                state.Body,
                ct),
            (Descriptor: descriptor, Actor: actor, Header: header, Body: body),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<ZLinkActorReply> InvokeActorPacketForReplyAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var call = new ActorPacketReplyCallState(descriptor, actor, header, body);
        await ExecuteActorPacketAsync(
            static async (activation, state, ct) =>
            {
                state.Reply = await activation._handlerExecutor.InvokeActorPacketForReplyAsync(
                        state.Descriptor,
                        state.Actor,
                        state.Header,
                        state.Body,
                        ct)
                    .ConfigureAwait(false);
            },
            call,
            cancellationToken).ConfigureAwait(false);

        return call.Reply
               ?? throw new InvalidOperationException(
                   $"Entry Spot actor packet reply for '{descriptor.MessageName}' was null.");
    }

    public async ValueTask InvokeActorLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        ZLinkMessage? request,
        CancellationToken cancellationToken)
    {
        await ExecuteAsync(
            static (activation, state, ct) => activation._invoker.InvokeActorLifecycleAsync(
                state.Descriptor,
                state.Actor,
                state.Request,
                ct),
            (Descriptor: descriptor, Actor: actor, Request: request),
            cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask InvokeActorDisconnectedAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await ExecuteAsync(
            static (activation, state, ct) => activation._invoker.InvokeActorLifecycleAsync(
                state.Descriptor,
                state.Actor,
                ct),
            (Descriptor: descriptor, Actor: actor),
            cancellationToken).ConfigureAwait(false);
    }

    private sealed class ActorPacketReplyCallState(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body)
    {
        public ZLinkSpotActorPacketDescriptor Descriptor { get; } = descriptor;

        public IZLinkActor Actor { get; } = actor;

        public ZlinkStreamHeader Header { get; } = header;

        public Message Body { get; } = body;

        public ZLinkActorReply? Reply { get; set; }
    }

    private sealed class ActorJoinCallState(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        ZLinkMessage request)
    {
        public ZLinkSpotActorJoinDescriptor Descriptor { get; } = descriptor;

        public IZLinkActor Actor { get; } = actor;

        public ZLinkMessage Request { get; } = request;

        public ZLinkSpotActorJoinResult Result { get; set; }
    }
}
