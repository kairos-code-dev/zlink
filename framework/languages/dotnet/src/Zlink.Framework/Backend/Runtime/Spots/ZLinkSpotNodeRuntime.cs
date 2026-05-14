using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeRuntime : IAsyncDisposable
{
    private readonly IServiceProvider _services;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly ZLinkFrameworkRegistration _frameworkRegistration;
    private readonly ZLinkSpotNodeRegistration _registration;
    private readonly string _spotChannelName;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSpotPeerConnectionSet _peerConnections = new();
    private readonly ZLinkSpotNodeBundleRegistry _bundles;
    private readonly ZLinkSpotNodeCatalog _spots;
    private Task? _discoveryPeerReconciliationTask;
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
        _spotChannelName = spotChannelName;
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

    public void InitializeEntrySpot()
    {
        _entrySpot = Node.EntrySpot();
        var entrySpot = _entrySpot;
        if (_registration.EntrySpotType is not null)
        {
            _entrySpotActivation = new ZLinkEntrySpotActivation(
                _services,
                _registration.EntrySpotType,
                Node.RoutingId);
            _entrySpotActivation.Configure();
            _entrySpotActivation.InitializeAsync(_stopSource.Token).AsTask().GetAwaiter().GetResult();
        }

        var runtime = _runtime;
        var activation = _entrySpotActivation;
        var stopToken = _stopSource.Token;

        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            entrySpot.OnDispatchEvent(info =>
            {
                if (info.Event != ZLinkBackendSpotDispatchEvent.ActorReadable
                    || info.ActorParts is not { Count: > 0 } actorParts)
                {
                    return;
                }

                _ = Task.Run(
                    () => ZLinkEntrySpotActorDispatcher.DispatchAsync(runtime, activation, actorParts, stopToken),
                    CancellationToken.None);
            });
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
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

    public void StartDiscoveryPeerReconciliation()
    {
        if (SpotDiscovery is null || _discoveryPeerReconciliationTask is not null)
        {
            return;
        }

        _discoveryPeerReconciliationTask = Task.Run(ReconcileDiscoveryPeersAsync);
    }

    public bool HasPublisherClient(string channelName)
    {
        return _registration.AttachedSpotPublisherClients.ContainsKey(channelName);
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot()
    {
        return new ZLinkSpotMonitoringSnapshot(
            Node.StatusSnapshot(),
            Node.PeersSnapshot()
                .OrderBy(static entry => entry.PeerEndpoint, StringComparer.Ordinal)
                .ThenBy(static entry => entry.ChannelName, StringComparer.Ordinal)
                .ToArray(),
            Node.SubjectsSnapshot()
                .OrderBy(static entry => entry.Subject, StringComparer.Ordinal)
                .ThenBy(static entry => entry.Role)
                .ToArray());
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
        RoutingId? requestedSpotRid,
        CancellationToken cancellationToken)
    {
        return await _spots.CreateAsync(spotName, requestedSpotRid, cancellationToken);
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
        cancellationToken.ThrowIfCancellationRequested();
        if (!_peerConnections.TryAddRouterManual(endpoint))
        {
            return ValueTask.FromResult(false);
        }

        try
        {
            Node.ConnectPeer(endpoint);
        }
        catch (ZlinkConnectException error)
            when (error.Result == ZlinkConnectException.ErrorCode.Busy)
        {
        }
        return ValueTask.FromResult(true);
    }

    public ValueTask<bool> ConnectPubSubAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!_peerConnections.TryAddPubSubManual(endpoint))
        {
            return ValueTask.FromResult(false);
        }

        try
        {
            Node.ConnectPeer(endpoint);
        }
        catch (ZlinkConnectException error)
            when (error.Result == ZlinkConnectException.ErrorCode.Busy)
        {
        }
        return ValueTask.FromResult(true);
    }

    public void DisconnectRouter(string endpoint)
    {
        Node.DisconnectPeer(endpoint);
        _peerConnections.RemoveRouterManual(endpoint);
    }

    public void DisconnectPubSub(string endpoint)
    {
        Node.DisconnectPeer(endpoint);
        _peerConnections.RemovePubSub(endpoint);
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
        if (SpotDiscovery is null)
        {
            return;
        }

        var localEndpoint = Node.StatusSnapshot().LocalEndpoint;
        var connectedEndpoints = Node.PeersSnapshot()
            .Select(static peer => peer.PeerEndpoint)
            .ToHashSet(StringComparer.Ordinal);
        foreach (var peer in SpotDiscovery.MemberPeers())
        {
            if (peer.AutoConnectType != ZLinkAutoConnectType.SpotMesh
                || peer.ServiceRole != ZLinkServiceRole.Spot
                || !string.Equals(peer.ChannelName, _spotChannelName, StringComparison.Ordinal)
                || string.IsNullOrWhiteSpace(peer.Endpoint)
                || string.Equals(peer.Endpoint, localEndpoint, StringComparison.Ordinal)
                || connectedEndpoints.Contains(peer.Endpoint))
            {
                continue;
            }

            if (_peerConnections.TryAddPubSubDiscovered(peer.Endpoint))
            {
                try
                {
                    Node.ConnectPeer(peer.Endpoint);
                }
                catch (ZlinkConnectException error)
                    when (error.InternalErrno == 16)
                {
                    // Discovery may connect the same peer between the snapshot
                    // and this manual reconciliation pass.
                }
            }
        }
    }

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();

        if (_discoveryPeerReconciliationTask is not null)
        {
            try
            {
                await _discoveryPeerReconciliationTask;
            }
            catch (OperationCanceledException)
            {
            }
        }

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

    private async Task ReconcileDiscoveryPeersAsync()
    {
        while (!_stopSource.IsCancellationRequested)
        {
            try
            {
                ConnectDiscoveredPubSubPeers();
                await Task.Delay(TimeSpan.FromMilliseconds(100), _stopSource.Token);
            }
            catch (OperationCanceledException) when (_stopSource.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException) when (_stopSource.IsCancellationRequested)
            {
                return;
            }
            catch (ZlinkException)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(100), _stopSource.Token);
            }
        }
    }
}

internal sealed record ZLinkSpotMonitoringSnapshot(
    ZLinkSpotNodeStatus Status,
    IReadOnlyList<ZLinkSpotNodePeerEntry> Peers,
    IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects);
