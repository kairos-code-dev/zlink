using System.Diagnostics.CodeAnalysis;

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

    public ZLinkSpotNodeRuntime(
        IServiceProvider services,
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration frameworkRegistration,
        ZLinkSpotNodeRegistration registration,
        IZLinkBackendContext context,
        IZLinkChannelBackendAdapter channelAdapter,
        IZLinkBackendSpotNode node,
        string spotChannelName)
    {
        _services = services;
        _runtime = runtime;
        _frameworkRegistration = frameworkRegistration;
        Registration = registration;
        Node = node;
        _taskRunner = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            _stopSource.Token);
        _monitoringSnapshots = new ZLinkSpotMonitoringSnapshotProvider(node);
        EntrySpotActorDispatch = new ZLinkEntrySpotActorDispatch(registration.SpotNodeName);
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
            spotChannelName);
    }

    public string Name => Registration.SpotNodeName;

    public IReadOnlySet<Type> SpotFactories => Registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    internal ZLinkSpotNodeRegistration Registration { get; }

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Spots;

    internal ZLinkEntrySpotActivation? EntrySpotActivation => EntrySpotActorDispatch.Activation;

    internal ZLinkEntrySpotActorDispatch EntrySpotActorDispatch { get; }

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();

        await _spots.DisposeAsync();

        await _bundles.DisposeAsync();

        await DisposeEntrySpotAsync().ConfigureAwait(false);

        await Node.DisposeAsync();
        _stopSource.Dispose();
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
            }

            return;
        }

        _entrySpot ??= Node.EntrySpot();
        var entrySpot = _entrySpot;

        var activation = await CreateEntrySpotActivationAsync(entrySpot)
            .ConfigureAwait(false);
        if (activation is not null) EntrySpotActorDispatch.Attach(activation);

        new ZLinkEntrySpotDispatchPump(_runtime, activation, _taskRunner)
            .Attach(entrySpot);
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot()
    {
        return _monitoringSnapshots.MonitorStatus();
    }

    public void AddPublisherBundle(string channelName, ZLinkSpotPublisherBundle bundle)
    {
        _bundles.AddPublisherBundle(channelName, bundle);
    }

    public ZLinkSpotPublisherBundle GetOrCreatePublisherBundle(string channelName)
    {
        return _bundles.GetOrCreatePublisherBundle(channelName);
    }

    public bool TryGetPublisherBundle(
        string channelName,
        [NotNullWhen(true)] out ZLinkSpotPublisherBundle? bundle)
    {
        return _bundles.TryGetPublisherBundle(channelName, out bundle);
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

    public void DisconnectRouter(string endpoint)
    {
        _peerConnector.DisconnectRouter(endpoint);
    }

    public void DisconnectPubSub(string endpoint)
    {
        _peerConnector.DisconnectPubSub(endpoint);
    }

    public IReadOnlyList<string> ListRouterConnections()
    {
        return _peerConnections.ListRouterManual();
    }

    public IReadOnlyList<string> ListPubSubConnections()
    {
        return _peerConnections.ListPubSubManual();
    }

    private async ValueTask<ZLinkEntrySpotActivation?> CreateEntrySpotActivationAsync(
        IZLinkBackendSpot entrySpot)
    {
        if (Registration.EntrySpotType is null) return null;

        var activation = new ZLinkEntrySpotActivation(
            _runtime,
            _services,
            entrySpot,
            Registration.EntrySpotType,
            Node.RoutingId,
            Registration.SpotNodeName,
            _frameworkRegistration.SpotDiscovery?.ChannelName ?? Registration.SpotNodeName,
            _frameworkRegistration.DefaultRequestTimeout,
            Registration.Router?.SocketConfig.SendTimeout
            ?? _frameworkRegistration.DefaultSocketSendTimeout);
        foreach (var assembly in _frameworkRegistration.HandlerAssemblies)
        foreach (var handler in ZLinkScannedSpotHandlerScanner.Scan(assembly))
            await activation.ApplyScannedHandlerAsync(handler, _stopSource.Token)
                .ConfigureAwait(false);

        activation.Configure();
        await activation.InitializeAsync(_stopSource.Token)
            .ConfigureAwait(false);
        return activation;
    }

    private bool ShouldAttachActorDispatchPump()
    {
        return Registration.Router is not null
               && Registration.ActorFactories.Count > 0;
    }

    private async ValueTask DisposeEntrySpotAsync()
    {
        if (_entrySpot is null) return;

        if (EntrySpotActorDispatch.Activation is { } activation)
        {
            await activation.CloseAsync(CancellationToken.None).ConfigureAwait(false);
            await activation.DisposeAsync().ConfigureAwait(false);
        }

        await _entrySpot.DisposeAsync();
    }
}

internal sealed record ZLinkSpotMonitoringSnapshot(
    ZLinkSpotNodeStatus Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry> Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects);