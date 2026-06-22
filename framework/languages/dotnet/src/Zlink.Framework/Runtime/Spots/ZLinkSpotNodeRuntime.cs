using System.Diagnostics.CodeAnalysis;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotNodeRuntime : IAsyncDisposable
{
    private readonly IServiceProvider _services;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly ZLinkFrameworkRegistration _frameworkRegistration;
    private readonly ZLinkSpotNodeRegistration _registration;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotPeerConnectionSet _peerConnections = new();
    private readonly ZLinkSpotNodeBundleRegistry _bundles;
    private readonly ZLinkSpotNodeCatalog _spots;
    private readonly ZLinkSpotMonitoringSnapshotProvider _monitoringSnapshots;
    private readonly ZLinkSpotDiscoveryReconciler _discoveryReconciler;
    private readonly ZLinkSpotDiscoveryLoop _discoveryLoop;
    private readonly ZLinkSpotPeerConnector _peerConnector;
    private readonly ZLinkEntrySpotActorDispatch _entryActorDispatch;
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
        _registration = registration;
        Node = node;
        _taskRunner = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            _stopSource.Token);
        _monitoringSnapshots = new ZLinkSpotMonitoringSnapshotProvider(node);
        _entryActorDispatch = new ZLinkEntrySpotActorDispatch(registration.SpotNodeName);
        _discoveryReconciler = new ZLinkSpotDiscoveryReconciler(
            spotChannelName,
            node,
            _peerConnections,
            () => SpotDiscovery,
            registration.Router is not null,
            registration.PubSub is not null);
        _discoveryLoop = new ZLinkSpotDiscoveryLoop(
            registration.SpotNodeName,
            _taskRunner,
            _stopSource.Token,
            ConnectDiscoveredPubSubPeers);
        _peerConnector = new ZLinkSpotPeerConnector(node, _peerConnections);
        _bundles = new ZLinkSpotNodeBundleRegistry(
            registration.SpotNodeName,
            frameworkRegistration,
            registration,
            node,
            _peerConnections,
            _stopSource.Token,
            ConnectDiscoveredPubSubPeers);
        _spots = new ZLinkSpotNodeCatalog(
            services,
            runtime,
            frameworkRegistration,
            registration,
            node,
            spotChannelName,
            ConnectDiscoveredPubSubPeers);
    }

    public string Name => _registration.SpotNodeName;

    public IReadOnlySet<Type> SpotFactories => _registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    internal ZLinkSpotNodeRegistration Registration => _registration;

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Spots;

    internal ZLinkEntrySpotActivation? EntrySpotActivation => _entryActorDispatch.Activation;

    internal ZLinkEntrySpotActorDispatch EntrySpotActorDispatch => _entryActorDispatch;

    public IZLinkBackendDiscovery? SpotDiscovery { get; set; }

    public void ApplyEntrySpotRoutingIdBeforeBind()
    {
        if (_registration.EntrySpotOptions.RoutingId.Size == 0)
        {
            return;
        }

        _entrySpot = Node.EntrySpot();
        _entrySpot.SetRoutingId(_registration.EntrySpotOptions.RoutingId);
    }

    public async ValueTask InitializeEntrySpotAsync()
    {
        if (_registration.EntrySpotType is null)
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
        if (activation is not null)
        {
            _entryActorDispatch.Attach(activation);
        }

        new ZLinkEntrySpotDispatchPump(_runtime, activation, _taskRunner)
            .Attach(entrySpot);
    }

    public void StartDiscoveryPeerReconciliation()
    {
        _discoveryLoop.StartIfNeeded(() => SpotDiscovery is not null);
    }

    public bool HasPublisherClient(string channelName)
    {
        return _registration.AttachedSpotPublisherClients.ContainsKey(channelName);
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
        Message request,
        CancellationToken cancellationToken)
    {
        return await _spots.CreateAsync(spotType, request, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        Type spotType,
        RoutingId requestedSpotRid,
        Message request,
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

    public void ConnectDiscoveredPubSubPeers()
    {
        if (SpotDiscovery is not null
            && _registration.Router?.BindEndpoint is { Length: > 0 } routerEndpoint)
        {
            var bound = ZLinkSpotRouterEndpointDiscovery.TryBindLocalEndpoint(
                SpotDiscovery,
                Node.RoutingId,
                routerEndpoint);
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"bind-local rid={Node.RoutingId.ToHex()} endpoint={routerEndpoint} bound={bound}");
        }

        _discoveryReconciler.ConnectDiscoveredPubSubPeers();
    }

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();

        await _discoveryLoop.StopAsync().ConfigureAwait(false);

        await _spots.DisposeAsync();

        await _bundles.DisposeAsync();

        await DisposeEntrySpotAsync().ConfigureAwait(false);

        await Node.DisposeAsync();
        _stopSource.Dispose();
    }

    private async ValueTask<ZLinkEntrySpotActivation?> CreateEntrySpotActivationAsync(
        IZLinkBackendSpot entrySpot)
    {
        if (_registration.EntrySpotType is null)
        {
            return null;
        }

        var activation = new ZLinkEntrySpotActivation(
            _runtime,
            _services,
            entrySpot,
            _registration.EntrySpotType,
            Node.RoutingId,
            _registration.SpotNodeName,
            _frameworkRegistration.SpotDiscovery?.ChannelName ?? _registration.SpotNodeName,
            _frameworkRegistration.DefaultRequestTimeout,
            _registration.Router?.SocketConfig.SendTimeout
                ?? _frameworkRegistration.DefaultSocketSendTimeout);
        foreach (var assembly in _frameworkRegistration.HandlerAssemblies)
        {
            foreach (var handler in ZLinkScannedSpotHandlerScanner.Scan(assembly))
            {
                await activation.ApplyScannedHandlerAsync(handler, _stopSource.Token)
                    .ConfigureAwait(false);
            }
        }

        activation.Configure();
        await activation.InitializeAsync(_stopSource.Token)
            .ConfigureAwait(false);
        return activation;
    }

    private bool ShouldAttachActorDispatchPump()
    {
        return _registration.Router is not null
            && _frameworkRegistration.ActorFactories.Count > 0;
    }

    private async ValueTask DisposeEntrySpotAsync()
    {
        if (_entrySpot is null)
        {
            return;
        }

        if (_entryActorDispatch.Activation is { } activation)
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
