using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;

namespace Zlink.Framework.Runtime.Host;

internal readonly record struct CreateActorResult(
    IZLinkActor Actor,
    bool Created,
    ZLinkMessage CreateRequest);

internal sealed partial class ZLinkFrameworkRuntime
{
    private readonly IServiceProvider _services;
    private readonly IZLinkBackendAdapterFactory _backendAdapterFactory;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly ZLinkRegistryRuntime? _registryRuntime;
    private readonly ZLinkChannelRuntimeManager _channels;
    private readonly ZLinkStreamRuntimeManager _streams;
    private readonly ZLinkSpotRuntimeManager _spots;
    private readonly ZLinkFrameworkRuntimeStateFactory _stateFactory;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ZLinkActorSessionManager _actorSessionManager;
    private readonly ZLinkFrameworkActorFacade _actors;
    private readonly ZLinkFrameworkChannelFacade _channelFacade;
    private readonly ZLinkFrameworkSpotFacade _spotFacade;
    private readonly ZLinkSpotRouteRouterDispatcher _spotRouteRouter;
    private readonly ZLinkSpotRouteEgressDispatcher _spotRouteEgress;
    private readonly ZLinkFrameworkSessionBindings _sessionBindings = new();
    private readonly object _workerPoolGate = new();
    private ZLinkWorkerPool? _workerPool;
    private ZLinkFrameworkRuntimeState? _state;
    private ZLinkMessageFlowTracer? _flow;

    public ZLinkFrameworkRuntime(
        IServiceProvider services,
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkFrameworkRegistration registration,
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher,
        ZLinkRegistryRuntime? registryRuntime = null)
    {
        _services = services;
        _backendAdapterFactory = backendAdapterFactory;
        _registration = registration;
        _registryRuntime = registryRuntime;
        var components = ZLinkFrameworkRuntimeComponentFactory.Create(
            this,
            services,
            backendAdapterFactory,
            registration,
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
        _channelFacade = components.ChannelFacade;
        _spotFacade = components.SpotFacade;
        _spotRouteRouter = new ZLinkSpotRouteRouterDispatcher(GetOrStartState);
        _spotRouteEgress = new ZLinkSpotRouteEgressDispatcher(
            _registration,
            _channelFacade.GetRouteChannel);
    }

    public IZLinkBackendContext? Context => _state?.Context;

    public ZLinkFrameworkRegistration Registration => _registration;

    // Shared success-path tracer for outbound client calls (channel/route/spot/actor
    // send/request/publish), built once. Inbound surfaces use the reporter's Flow.
    internal ZLinkMessageFlowTracer Flow => _flow ??= new ZLinkMessageFlowTracer(
        _registration.DispatchOptions,
        _services,
        _services.GetService<ILogger<ZLinkFrameworkRuntime>>());

    internal IServiceProvider Services => _services;

    internal ZLinkWorkerPool WorkerPool
    {
        get
        {
            lock (_workerPoolGate)
            {
                return _workerPool ??= _registration.WorkerOptions.CreatePool();
            }
        }
    }

    internal IZLinkRouteClient RouteClient => _services.GetRequiredService<IZLinkRouteClient>();

    internal void DrainSpotRouteBridges()
    {
        var state = _state;
        if (state is null)
        {
            return;
        }

        IZLinkBackendSpotRouteBridge[] bridges;
        lock (state.SyncRoot)
        {
            bridges = state.SpotRouteBridges.ToArray();
        }

        foreach (var bridge in bridges)
        {
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
        var publisher = _services.GetService<IZLinkRuntimeEventPublisher>();
        return publisher is null
            ? ValueTask.CompletedTask
            : publisher.PublishAsync(@event, cancellationToken);
    }

    public bool IsStarted => _state is not null;

    public async ValueTask StartAsync(CancellationToken cancellationToken)
    {
        if (_registryRuntime is not null)
        {
            await _registryRuntime.StartAsync(cancellationToken);
        }

        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (_state is not null)
            {
                return;
            }

            _state = await _stateFactory.CreateAsync().ConfigureAwait(false);
            _state.ListenerTasks.Add(_state.TaskRunner.Run(
                "spot-route-bridge-drain",
                RunSpotRouteBridgeDrainLoopAsync));
        }
        finally
        {
            _gate.Release();
        }
    }

    private async ValueTask RunSpotRouteBridgeDrainLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            DrainSpotRouteBridges();
            await Task.Delay(TimeSpan.FromMilliseconds(10), cancellationToken).ConfigureAwait(false);
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

        if (stateToDispose is not null)
        {
            await stateToDispose.DisposeAsync();
        }

        ZLinkWorkerPool? workerPoolToDispose;
        lock (_workerPoolGate)
        {
            workerPoolToDispose = _workerPool;
            _workerPool = null;
        }

        workerPoolToDispose?.Dispose();

        if (_registryRuntime is not null)
        {
            await _registryRuntime.StopAsync(cancellationToken);
        }
    }

    private async ValueTask<ZLinkFrameworkRuntimeState> GetStartedStateAsync(
        CancellationToken cancellationToken)
    {
        if (_state is null)
        {
            await StartAsync(cancellationToken);
        }

        return _state ?? throw new InvalidOperationException("ZLink framework runtime is not started.");
    }

    private ZLinkFrameworkRuntimeState GetOrStartState()
    {
        if (_state is null)
        {
            throw new InvalidOperationException(
                "ZLink framework runtime is not started. Call StartAsync before using synchronous runtime APIs.");
        }

        return _state ?? throw new InvalidOperationException("ZLink framework runtime is not started.");
    }

}
