using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Host;

internal readonly record struct CreateActorResult(
    IZLinkActor Actor,
    bool Created,
    ZLinkMessage CreateRequest);

internal sealed partial class ZLinkFrameworkRuntime
{
    private readonly ZLinkFrameworkActorFacade _actors;
    private readonly ZLinkActorSessionManager _actorSessionManager;
    private readonly IZLinkBackendAdapterFactory _backendAdapterFactory;
    private readonly ZLinkChannelRuntimeManager _channels;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ZLinkSessionActorBindingTable _sessionBindings = new();
    private readonly ZLinkLocationLifecycle? _locationLifecycle;
    private readonly IZLinkAutoConnectTopologyQuery? _topologyQuery;
    private readonly ZLinkSpotRouteRouterDispatcher _spotRouteRouter;
    private readonly ZLinkSpotRuntimeManager _spots;
    private readonly ZLinkFrameworkRuntimeStateFactory _stateFactory;
    private readonly ZLinkStreamRuntimeManager _streams;
    private readonly object _workerPoolGate = new();
    private ZLinkMessageFlowTracer? _flow;
    private ZLinkFrameworkRuntimeState? _state;
    private ZLinkWorkerPool? _workerPool;

    public ZLinkFrameworkRuntime(
        IServiceProvider services,
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkFrameworkRegistration registration,
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher)
    {
        Services = services;
        _backendAdapterFactory = backendAdapterFactory;
        Registration = registration;
        _locationLifecycle = services.GetService<ZLinkLocationLifecycle>();
        _topologyQuery = services.GetService<IZLinkAutoConnectTopologyQuery>();
        var components = ZLinkFrameworkRuntimeComponentFactory.Create(
            this,
            services,
            backendAdapterFactory,
            registration,
            _locationLifecycle,
            handlerRegistry,
            dispatcher,
            GetOrStartState,
            GetActorSpotNode);
        _channels = components.Channels;
        _streams = components.Streams;
        _spots = components.Spots;
        _stateFactory = components.StateFactory;
        _actorSessionManager = components.ActorSessionManager;
        _actors = components.Actors;
        _spotRouteRouter = new ZLinkSpotRouteRouterDispatcher(GetOrStartState);
    }

    public IZLinkBackendContext? Context => _state?.Context;

    public ZLinkFrameworkRegistration Registration { get; }

    // Shared success-path tracer for outbound client calls (channel/route/spot/actor
    // send/request/publish), built once. Inbound surfaces use the reporter's Flow.
    internal ZLinkMessageFlowTracer Flow => _flow ??= new ZLinkMessageFlowTracer(
        Registration.DispatchOptions,
        Services,
        Services.GetService<ILogger<ZLinkFrameworkRuntime>>());

    internal IServiceProvider Services { get; }

    internal ZLinkWorkerPool WorkerPool
    {
        get
        {
            lock (_workerPoolGate)
            {
                return _workerPool ??= Registration.WorkerOptions.CreatePool();
            }
        }
    }

    internal IZLinkRouteClient RouteClient => Services.GetRequiredService<IZLinkRouteClient>();

    public bool IsStarted => _state is not null;

    internal void DrainSpotRouteBridges()
    {
        var state = _state;
        if (state is null) return;

        IZLinkBackendSpotRouteBridge[] bridges;
        lock (state.SyncRoot)
        {
            bridges = state.SpotRouteBridges.ToArray();
        }

        foreach (var bridge in bridges)
            try
            {
                bridge.Drain();
            }
            catch (ObjectDisposedException)
            {
            }
            catch (ZlinkCloseException)
            {
            }
    }

    internal ValueTask<ZLinkFrameworkRuntimeState> GetStartedStateForRoutingAsync(
        CancellationToken cancellationToken)
    {
        return GetStartedStateAsync(cancellationToken);
    }

    internal ValueTask PublishRuntimeEventAsync<TEvent>(
        TEvent @event,
        CancellationToken cancellationToken)
        where TEvent : IZLinkRuntimeEvent
    {
        var publisher = Services.GetService<IZLinkRuntimeEventPublisher>();
        return publisher is null
            ? ValueTask.CompletedTask
            : publisher.PublishAsync(@event, cancellationToken);
    }

    public async ValueTask StartAsync(CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (_state is not null) return;

            _state = await _stateFactory.CreateAsync().ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask StopAsync(CancellationToken cancellationToken)
    {
        ZLinkFrameworkRuntimeState? stateToDispose;

        await _gate.WaitAsync(cancellationToken);
        try
        {
            stateToDispose = _state;
            _state = null;
        }
        finally
        {
            _gate.Release();
        }

        if (stateToDispose is not null) await stateToDispose.DisposeAsync();

        ZLinkWorkerPool? workerPoolToDispose;
        lock (_workerPoolGate)
        {
            workerPoolToDispose = _workerPool;
            _workerPool = null;
        }

        workerPoolToDispose?.Dispose();
    }

    private async ValueTask<ZLinkFrameworkRuntimeState> GetStartedStateAsync(
        CancellationToken cancellationToken)
    {
        if (_state is null) await StartAsync(cancellationToken);

        return _state ?? throw new InvalidOperationException("ZLink framework runtime is not started.");
    }

    private ZLinkFrameworkRuntimeState GetOrStartState()
    {
        if (_state is null)
            throw new InvalidOperationException(
                "ZLink framework runtime is not started. Call StartAsync before using synchronous runtime APIs.");

        return _state;
    }
}
