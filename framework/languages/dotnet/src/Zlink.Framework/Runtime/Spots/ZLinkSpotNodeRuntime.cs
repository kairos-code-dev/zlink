using System.Diagnostics.CodeAnalysis;
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
            () => SpotDiscovery,
            registration.Router is not null,
            registration.PubSub is not null);
        _discoveryLoop = new ZLinkSpotDiscoveryLoop(
            registration.SpotNodeName,
            _taskRunner,
            _stopSource.Token,
            ConnectDiscoveredPubSubPeers);
        _peerConnector = new ZLinkSpotPeerConnector(node, _peerConnections, spotChannelName);
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
            () => SpotDiscovery,
            GetOrCreateAttachedChannelBundle,
            ConnectDiscoveredPubSubPeers);
    }

    public string Name => _registration.SpotNodeName;

    public IReadOnlyDictionary<string, Type> SpotFactories => _registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Spots;

    internal ZLinkEntrySpotActivation? EntrySpotActivation => _entrySpotActivation;

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

        EnsureAttachedChannelBundles();
        _entrySpot ??= Node.EntrySpot();
        var entrySpot = _entrySpot;

        _entrySpotActivation = await CreateEntrySpotActivationAsync(entrySpot)
            .ConfigureAwait(false);

        new ZLinkEntrySpotDispatchPump(_runtime, _entrySpotActivation, _taskRunner)
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
        return RequireEntrySpotActivation().InvokeActorPacketAsync(
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
        return RequireEntrySpotActivation().InvokeActorPacketForReplyAsync(
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
        return RequireEntrySpotActivation().InvokeActorLifecycleAsync(
            descriptor,
            actor,
            info,
            cancellationToken);
    }

    public ValueTask InvokeEntrySpotActorJoinedCallbackAsync(
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        if (_entrySpotActivation is null)
        {
            return ValueTask.CompletedTask;
        }

        return _entrySpotActivation.InvokeActorJoinedCallbackAsync(info, cancellationToken);
    }

    public ValueTask InvokeEntrySpotActorLeftCallbackAsync(
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        if (_entrySpotActivation is null)
        {
            return ValueTask.CompletedTask;
        }

        return _entrySpotActivation.InvokeActorLeftCallbackAsync(info, cancellationToken);
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

    private void EnsureAttachedChannelBundles()
    {
        foreach (var channelName in _registration.AttachedChannelClients.Keys)
        {
            _bundles.GetOrCreateAttachedChannelBundle(channelName);
        }
    }

    private ZLinkAsyncSubmitter? ResolveAttachedChannelSubmitter(string channelName)
    {
        return _registration.AttachedChannelClients.ContainsKey(channelName)
            ? _bundles.GetOrCreateAttachedChannelBundle(channelName).Submitter
            : null;
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
        if (SpotDiscovery is not null
            && _registration.Router?.BindEndpoint is { Length: > 0 } routerEndpoint)
        {
            var bound = ZLinkSpotRouterEndpointDiscovery.TryBindLocalEndpoint(
                SpotDiscovery,
                Node.RoutingId,
                routerEndpoint);
            if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY") == "1")
            {
                Console.Error.WriteLine(
                    $"[zlink-framework-spot-discovery] bind-local rid={Node.RoutingId.ToHex()} endpoint={routerEndpoint} bound={bound}");
            }
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
            _frameworkRegistration.DefaultTimeout,
            _registration.Router?.SocketConfig.SendTimeout
                ?? TimeSpan.FromMilliseconds(200),
            ResolveAttachedChannelSubmitter);
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

    private ZLinkEntrySpotActivation RequireEntrySpotActivation()
    {
        return _entrySpotActivation
            ?? throw new InvalidOperationException($"SPOT node '{Name}' does not have an Entry Spot.");
    }

    private async ValueTask DisposeEntrySpotAsync()
    {
        if (_entrySpot is null)
        {
            return;
        }

        if (_entrySpotActivation is not null)
        {
            await _entrySpotActivation.CloseAsync(CancellationToken.None).ConfigureAwait(false);
            await _entrySpotActivation.DisposeAsync().ConfigureAwait(false);
        }

        await _entrySpot.DisposeAsync();
    }

}

internal sealed record ZLinkSpotMonitoringSnapshot(
    ZLinkSpotNodeStatus Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry> Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects);
