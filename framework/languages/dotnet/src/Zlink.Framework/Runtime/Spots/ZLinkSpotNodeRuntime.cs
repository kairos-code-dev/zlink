using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeRuntime : IAsyncDisposable
{
    private readonly ZLinkSpotNodeBundleRegistry _bundles;
    private readonly ZLinkFrameworkRegistration _frameworkRegistration;
    private readonly ZLinkSpotMonitoringSnapshotProvider _monitoringSnapshots;
    private readonly ZLinkSpotPeerConnectionSet _peerConnections = new();
    private readonly ZLinkSpotPeerConnector _peerConnector;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly IServiceProvider _services;
    private readonly ZLinkSpotNodeCatalog _spots;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private IZLinkBackendSpot? _entrySpot;
    private ZLinkEntrySpotActivation? _entrySpotActivation;
    private int _entrySpotMetricActive;
    private int _entrySpotLifecycleClosed;

    public ZLinkSpotNodeRuntime(
        IServiceProvider services,
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkSpotNodeRegistration registration,
        IZLinkBackendContext context,
        IZLinkChannelBackendAdapter channelAdapter,
        IZLinkBackendSpotNode node,
        string spotChannelName,
        ZLinkLocationLifecycle? locationLifecycle)
    {
        _services = services;
        _runtime = runtime;
        _frameworkRegistration = frameworkRegistration;
        Registration = registration;
        Node = node;
        _taskRunner = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            _stopSource.Token,
            runtime.ExecutionOwner);
        _monitoringSnapshots = new ZLinkSpotMonitoringSnapshotProvider(node);
        _peerConnector = new ZLinkSpotPeerConnector(node, _peerConnections);
        _bundles = new ZLinkSpotNodeBundleRegistry(
            frameworkRegistration,
            node,
            _stopSource.Token);
        _spots = new ZLinkSpotNodeCatalog(
            services,
            runtime,
            frameworkRegistration,
            registration,
            node,
            spotChannelName,
            locationLifecycle);
    }

    public string Name => Registration.SpotNodeName;

    public IReadOnlySet<Type> SpotFactories => Registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    internal ZLinkSpotNodeRegistration Registration { get; }

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Spots;

    internal ZLinkEntrySpotActivation? EntrySpotActivation => _entrySpotActivation;

    internal void RequestStop()
    {
        _stopSource.Cancel();
        _spots.RequestStop();
        _entrySpotActivation?.RequestStop();
    }

    internal void CancelActiveOperations()
    {
        _spots.CancelActiveOperations();
    }

    internal async ValueTask CloseLifecycleAsync()
    {
        await _spots.CloseLifecycleAsync().ConfigureAwait(false);
        if (_entrySpotActivation is not { } activation
            || Interlocked.Exchange(ref _entrySpotLifecycleClosed, 1) != 0)
            return;

        await activation.CloseAsync(CancellationToken.None).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        var failures = new List<Exception>();
        await CaptureAsync(CloseLifecycleAsync).ConfigureAwait(false);
        Capture(RequestStop);
        await CaptureAsync(_taskRunner.StopAsync).ConfigureAwait(false);
        await CaptureAsync(_spots.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_bundles.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(DisposeEntrySpotAsync).ConfigureAwait(false);
        await CaptureAsync(Node.DisposeAsync).ConfigureAwait(false);
        Capture(_stopSource.Dispose);
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        void Capture(Action cleanup)
        {
            try
            {
                cleanup();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    public void ApplyEntrySpotRoutingIdBeforeBind()
    {
        if (Registration.EntrySpotOptions.RoutingId.Size == 0) return;

        _entrySpot = Node.EntrySpot();
        _entrySpot.SetRoutingId(Registration.EntrySpotOptions.RoutingId);
    }

    public async ValueTask InitializeEntrySpotAsync()
    {
        if (Registration.EntrySpotType is null)
        {
            if (ShouldAttachActorDispatchPump())
            {
                _entrySpot = Node.EntrySpot();
                new ZLinkEntrySpotDispatchPump(_runtime, null, _taskRunner)
                    .Attach(_entrySpot);
                if (Interlocked.Exchange(ref _entrySpotMetricActive, 1) == 0)
                    ZLinkRuntimeMetrics.RecordSpotCreated("entry");
            }

            return;
        }

        _entrySpot ??= Node.EntrySpot();
        var entrySpot = _entrySpot;

        var activation = await CreateEntrySpotActivationAsync(entrySpot)
            .ConfigureAwait(false);
        if (activation is not null)
        {
            if (_entrySpotActivation is not null)
                throw new InvalidOperationException(
                    $"SPOT node '{Registration.SpotNodeName}' already has an Entry Spot activation.");

            _entrySpotActivation = activation;
        }

        new ZLinkEntrySpotDispatchPump(_runtime, activation, _taskRunner)
            .Attach(entrySpot);
        if (Interlocked.Exchange(ref _entrySpotMetricActive, 1) == 0)
            ZLinkRuntimeMetrics.RecordSpotCreated("entry");
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot()
    {
        return _monitoringSnapshots.MonitorStatus();
    }

    public ZLinkSpotPublisherBundle GetOrCreatePublisherBundle(string channelName)
    {
        return _bundles.GetOrCreatePublisherBundle(channelName);
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        Type spotType,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return await _spots.CreateAsync(spotType, request, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        Type spotType,
        RoutingId requestedSpotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return await _spots.GetOrCreateAsync(
            spotType,
            requestedSpotRid,
            request,
            cancellationToken);
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return _spots.GetAsync(spotRid, cancellationToken);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        return _spots.ListAsync(cancellationToken);
    }

    public async ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return await _spots.CloseAsync(spotRid, cancellationToken);
    }

    internal ValueTask<bool> TryDrainSpotsAsync(CancellationToken cancellationToken) =>
        _spots.TryDrainAsync(Registration.DrainPolicy, cancellationToken);

    internal void BeginDrain() => _spots.BeginDrain(Registration.DrainPolicy);

    public ValueTask<bool> ConnectRouterAsync(string endpoint, CancellationToken cancellationToken)
    {
        return _peerConnector.ConnectRouterAsync(endpoint, cancellationToken);
    }

    public ValueTask<bool> ConnectRouterAsync(
        RoutingId peerRid,
        string endpoint,
        CancellationToken cancellationToken)
    {
        return _peerConnector.ConnectRouterAsync(peerRid, endpoint, cancellationToken);
    }

    public ValueTask<bool> ConnectPubSubAsync(string endpoint, CancellationToken cancellationToken)
    {
        return _peerConnector.ConnectPubSubAsync(endpoint, cancellationToken);
    }

    public void DisconnectPeer(string endpoint)
    {
        _peerConnector.Disconnect(endpoint);
    }

    public void DisconnectRouterManual(string endpoint)
        => _peerConnector.DisconnectRouterManual(endpoint);

    public void DisconnectPubSubManual(string endpoint)
        => _peerConnector.DisconnectPubSubManual(endpoint);

    public bool ConnectRouterAuto(RoutingId? peerRid, string endpoint)
        => _peerConnector.ConnectRouterAuto(peerRid, endpoint);

    public bool ConnectPubSubAuto(string endpoint)
        => _peerConnector.ConnectPubSubAuto(endpoint);

    public bool DisconnectRouterAuto(string endpoint)
        => _peerConnector.DisconnectRouterAuto(endpoint);

    public bool DisconnectPubSubAuto(string endpoint)
        => _peerConnector.DisconnectPubSubAuto(endpoint);

    private async ValueTask<ZLinkEntrySpotActivation?> CreateEntrySpotActivationAsync(
        IZLinkBackendSpot entrySpot)
    {
        if (Registration.EntrySpotType is null) return null;

        var scope = _services.CreateAsyncScope();
        ZLinkEntrySpotActivation? activation = null;
        try
        {
            activation = new ZLinkEntrySpotActivation(
                _runtime,
                _services,
                scope,
                entrySpot,
                Registration.EntrySpotType,
                Node.RoutingId,
                Registration.SpotNodeName,
                _frameworkRegistration.SpotDiscovery?.ChannelName ?? Registration.SpotNodeName,
                _frameworkRegistration.DefaultRequestTimeout,
                Registration.Router?.SocketConfig.SendTimeout
                ?? _frameworkRegistration.DefaultSocketSendTimeout);
            activation.InitializeRuntimeResources();
            foreach (var assembly in _frameworkRegistration.EnumerateHandlerScanAssemblies())
            foreach (var handler in ZLinkScannedSpotHandlerScanner.Scan(assembly))
                await activation.ApplyScannedHandlerAsync(handler, _stopSource.Token)
                    .ConfigureAwait(false);

            activation.Configure();
            await activation.InitializeAsync(_stopSource.Token).ConfigureAwait(false);
            return activation;
        }
        catch (Exception initializationFailure)
        {
            try
            {
                if (activation is null)
                    await scope.DisposeAsync().ConfigureAwait(false);
                else
                    await activation.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception cleanupFailure)
            {
                throw new AggregateException(initializationFailure, cleanupFailure);
            }

            throw;
        }
    }

    private bool ShouldAttachActorDispatchPump()
    {
        return Registration.Router is not null
               && Registration.ActorFactories.Count > 0;
    }

    private async ValueTask DisposeEntrySpotAsync()
    {
        if (_entrySpot is null) return;

        var failures = new List<Exception>();
        if (_entrySpotActivation is { } activation)
        {
            if (Volatile.Read(ref _entrySpotLifecycleClosed) == 0)
                await CaptureAsync(() => activation.CloseAsync(CancellationToken.None)).ConfigureAwait(false);
            await CaptureAsync(activation.DisposeAsync).ConfigureAwait(false);
        }

        await CaptureAsync(_entrySpot.DisposeAsync).ConfigureAwait(false);
        if (Interlocked.Exchange(ref _entrySpotMetricActive, 0) != 0)
            ZLinkRuntimeMetrics.RecordSpotClosed("entry");
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }
}

internal sealed record ZLinkSpotMonitoringSnapshot(
    ZLinkSpotNodeStatus Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry> Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects);
