using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

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
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private IZLinkBackendSpot? _entrySpot;
    private ZLinkEntrySpotActivation? _entrySpotActivation;

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
        _discoveryReconciler = new ZLinkSpotDiscoveryReconciler(
            spotChannelName,
            node,
            _peerConnections,
            () => SpotDiscovery);
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
            context,
            channelAdapter,
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
            GetOrCreateAttachedChannelBundle,
            ConnectDiscoveredPubSubPeers);
    }

    public string Name => _registration.SpotNodeName;

    public IReadOnlyDictionary<string, Type> SpotFactories => _registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    public IReadOnlyDictionary<string, ZLinkSpotAttachedChannelBundle> AttachedChannelBundles => _bundles.AttachedChannelBundles;

    public IReadOnlyDictionary<string, ZLinkSpotPublisherBundle> PublisherBundles => _bundles.PublisherBundles;

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Spots;

    internal ZLinkEntrySpotActivation? EntrySpotActivation => _entrySpotActivation;

    public IZLinkBackendDiscovery? SpotDiscovery { get; set; }

    public async ValueTask InitializeEntrySpotAsync()
    {
        _entrySpot = Node.EntrySpot();
        var entrySpot = _entrySpot;
        if (_registration.EntrySpotType is not null)
        {
            _entrySpotActivation = new ZLinkEntrySpotActivation(
                _runtime,
                _services,
                entrySpot,
                _registration.EntrySpotType,
                Node.RoutingId,
                _frameworkRegistration.SpotDiscovery?.ChannelName ?? _registration.SpotNodeName,
                _frameworkRegistration.DefaultTimeout,
                _registration.Router?.SocketConfig.SendTimeout
                    ?? TimeSpan.FromMilliseconds(200));
            _entrySpotActivation.Configure();
            await _entrySpotActivation.InitializeAsync(_stopSource.Token)
                .ConfigureAwait(false);
        }

        var runtime = _runtime;
        var activation = _entrySpotActivation;
        new ZLinkEntrySpotDispatchPump(runtime, activation, _taskRunner)
            .Attach(entrySpot);
    }

    public bool TryResolveEntrySpotActorPacket(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        descriptor = null;
        return _entrySpotActivation is not null
            && _entrySpotActivation.TryResolveActorPacket(actorType, header, out descriptor);
    }

    public ValueTask InvokeEntrySpotActorPacketAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (_entrySpotActivation is null)
        {
            throw new InvalidOperationException($"SPOT node '{Name}' does not have an Entry Spot.");
        }

        return _entrySpotActivation.InvokeActorPacketAsync(
            descriptor,
            actor,
            header,
            body,
            cancellationToken);
    }

    public ValueTask<byte[]> InvokeEntrySpotActorPacketForReplyAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (_entrySpotActivation is null)
        {
            throw new InvalidOperationException($"SPOT node '{Name}' does not have an Entry Spot.");
        }

        return _entrySpotActivation.InvokeActorPacketForReplyAsync(
            descriptor,
            actor,
            header,
            body,
            cancellationToken);
    }

    public bool TryResolveEntrySpotActorJoined(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        descriptor = null;
        return _entrySpotActivation is not null
            && _entrySpotActivation.TryResolveActorJoined(actorType, out descriptor);
    }

    public bool TryResolveEntrySpotActorLeft(
        Type actorType,
        out ZLinkSpotActorLifecycleDescriptor? descriptor)
    {
        descriptor = null;
        return _entrySpotActivation is not null
            && _entrySpotActivation.TryResolveActorLeft(actorType, out descriptor);
    }

    public ValueTask InvokeEntrySpotActorLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        if (_entrySpotActivation is null)
        {
            throw new InvalidOperationException($"SPOT node '{Name}' does not have an Entry Spot.");
        }

        return _entrySpotActivation.InvokeActorLifecycleAsync(
            descriptor,
            actor,
            info,
            cancellationToken);
    }

    public ValueTask InvokeEntrySpotActorJoinedCallbackAsync(
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        if (_entrySpotActivation is null)
        {
            return ValueTask.CompletedTask;
        }

        return _entrySpotActivation.InvokeActorJoinedCallbackAsync(actor, info, cancellationToken);
    }

    public ValueTask InvokeEntrySpotActorLeftCallbackAsync(
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        if (_entrySpotActivation is null)
        {
            return ValueTask.CompletedTask;
        }

        return _entrySpotActivation.InvokeActorLeftCallbackAsync(actor, info, cancellationToken);
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
        return _monitoringSnapshots.Snapshot();
    }

    public void AddChannelBundle(string channelName, ZLinkSpotAttachedChannelBundle bundle)
    {
        _bundles.AddChannelBundle(channelName, bundle);
    }

    public void AddPublisherBundle(string channelName, ZLinkSpotPublisherBundle bundle)
    {
        _bundles.AddPublisherBundle(channelName, bundle);
    }

    public ZLinkSpotAttachedChannelBundle GetOrCreateAttachedChannelBundle(string channelName)
    {
        return _bundles.GetOrCreateAttachedChannelBundle(channelName);
    }

    public ZLinkSpotPublisherBundle GetOrCreatePublisherBundle(string channelName)
    {
        return _bundles.GetOrCreatePublisherBundle(channelName);
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        return await _spots.CreateAsync(spotName, createParts, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        string spotName,
        RoutingId requestedSpotRid,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        return await _spots.GetOrCreateAsync(
            spotName,
            requestedSpotRid,
            createParts,
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

    public async ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return await _spots.RemoveAsync(spotRid, cancellationToken);
    }

    public ValueTask<bool> ConnectRouterAsync(string endpoint, CancellationToken cancellationToken)
    {
        return _peerConnector.ConnectRouterAsync(endpoint, cancellationToken);
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
        _discoveryReconciler.ConnectDiscoveredPubSubPeers();
    }

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();

        await _discoveryLoop.StopAsync().ConfigureAwait(false);

        await _spots.DisposeAsync();

        await _bundles.DisposeAsync();

        if (_entrySpot is not null)
        {
            if (_entrySpotActivation is not null)
            {
                await _entrySpotActivation.CloseAsync(CancellationToken.None).ConfigureAwait(false);
                await _entrySpotActivation.DisposeAsync().ConfigureAwait(false);
            }

            await _entrySpot.DisposeAsync();
        }

        await Node.DisposeAsync();
        _stopSource.Dispose();
    }

}

internal sealed record ZLinkSpotMonitoringSnapshot(
    ZLinkSpotNodeStatus Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry> Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects);
