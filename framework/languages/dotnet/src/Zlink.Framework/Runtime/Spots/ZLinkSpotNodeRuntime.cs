using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeRuntime : IAsyncDisposable
{
    private readonly IServiceProvider _services;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly ZLinkFrameworkRegistration _frameworkRegistration;
    private readonly ZLinkSpotNodeRegistration _registration;
    private readonly string _spotChannelName;
    private readonly IZLinkBackendContext _context;
    private readonly IZLinkChannelBackendAdapter _channelAdapter;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly CancellationTokenSource _stopSource = new();
    private readonly object _connectionsGate = new();
    private readonly Dictionary<string, ZLinkSpotAttachedChannelBundle> _channelBundles = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkSpotPublisherBundle> _publisherBundles = new(StringComparer.Ordinal);
    private readonly Dictionary<RoutingId, ZLinkSpotActivation> _spots = [];
    private readonly HashSet<string> _routerManualConnections = new(StringComparer.Ordinal);
    private readonly HashSet<string> _pubSubManualConnections = new(StringComparer.Ordinal);
    private readonly HashSet<string> _pubSubDiscoveredConnections = new(StringComparer.Ordinal);
    private Task? _discoveryPeerReconciliationTask;

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
        _context = context;
        _channelAdapter = channelAdapter;
        Node = node;
        _spotChannelName = spotChannelName;
    }

    public string Name => _registration.SpotNodeName;

    public IReadOnlyDictionary<string, Type> SpotFactories => _registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    public IReadOnlyDictionary<string, ZLinkSpotAttachedChannelBundle> AttachedChannelBundles => _channelBundles;

    public IReadOnlyDictionary<string, ZLinkSpotPublisherBundle> PublisherBundles => _publisherBundles;

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Values;

    public IZLinkBackendDiscovery? SpotDiscovery { get; set; }

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
                .ThenBy(static entry => entry.ServiceName, StringComparer.Ordinal)
                .ToArray(),
            Node.SubjectsSnapshot()
                .OrderBy(static entry => entry.Subject, StringComparer.Ordinal)
                .ThenBy(static entry => entry.Role)
                .ToArray());
    }

    public void AddChannelBundle(string channelName, ZLinkSpotAttachedChannelBundle bundle)
    {
        _channelBundles.Add(channelName, bundle);
    }

    public void AddPublisherBundle(string channelName, ZLinkSpotPublisherBundle bundle)
    {
        _publisherBundles.Add(channelName, bundle);
    }

    public ZLinkSpotAttachedChannelBundle GetOrCreateAttachedChannelBundle(string channelName)
    {
        if (_channelBundles.TryGetValue(channelName, out var existing))
        {
            return existing;
        }

        if (!_registration.AttachedChannelClients.TryGetValue(channelName, out var attached))
        {
            throw new InvalidOperationException(
                $"SPOT node '{Name}' attached channel client '{channelName}' is not registered.");
        }

        var dealer = _channelAdapter.CreateDealerSocket(_context);
        dealer.SetChannelName(attached.ChannelName);
        var bundle = new ZLinkSpotAttachedChannelBundle(dealer);

        if (attached.ManualConnections.Count > 0)
        {
            foreach (var endpoint in attached.ManualConnections)
            {
                dealer.Connect(endpoint);
                _ = bundle.TryAddManualConnection(endpoint);
            }

            Node.AttachChannelDealerManual(attached.ChannelName, dealer);
        }
        else
        {
            var discovery = _channelAdapter.CreateDiscovery(
                _context,
                ZLinkAutoConnectType.ClientServer,
                attached.ChannelName);
            foreach (var endpoint in _frameworkRegistration.Discovery?.Endpoints ?? [])
            {
                discovery.ConnectRegistry(endpoint);
            }

            Node.AttachChannelDealer(discovery, dealer);
            bundle.Discovery = discovery;
        }

        _channelBundles.Add(channelName, bundle);
        return bundle;
    }

    public ZLinkSpotPublisherBundle GetOrCreatePublisherBundle(string channelName)
    {
        if (_publisherBundles.TryGetValue(channelName, out var existing))
        {
            return existing;
        }

        if (!_registration.AttachedSpotPublisherClients.TryGetValue(channelName, out var attached))
        {
            throw new InvalidOperationException(
                $"SPOT node '{Name}' publisher client '{channelName}' is not registered.");
        }

        ConnectDiscoveredPubSubPeers();

        var publisher = Node.CreateSpot();
        var bundle = new ZLinkSpotPublisherBundle(
            publisher,
            new ZLinkAsyncSubmitter(
                publisher.OnSendReady,
                attached.SocketOptions.SendTimeout,
                _stopSource.Token));

        if (attached.ManualConnections.Count > 0)
        {
            foreach (var endpoint in attached.ManualConnections)
            {
                if (TryAddPubSubConnection(endpoint))
                {
                    Node.ConnectPeer(endpoint);
                }
            }
        }

        _publisherBundles.Add(channelName, bundle);
        return bundle;
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        RoutingId? requestedSpotRid,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (!_registration.SpotFactories.TryGetValue(spotName, out var spotType))
            {
                throw new InvalidOperationException(
                    $"SPOT factory '{spotName}' is not registered on node '{Name}'.");
            }

            foreach (var channelName in _registration.AttachedChannelClients.Keys)
            {
                GetOrCreateAttachedChannelBundle(channelName);
            }

            if (requestedSpotRid is RoutingId existingRid
                && _spots.TryGetValue(existingRid, out var existing))
            {
                if (!string.Equals(existing.SpotName, spotName, StringComparison.Ordinal))
                {
                    throw new InvalidOperationException(
                        $"SPOT routing id '{existingRid}' already belongs to '{existing.SpotName}'.");
                }

                return new ZLinkSpotCreateResult(existing.SpotRid, existing.SpotName, false);
            }

            var nativeSpot = Node.CreateSpot();
            var spotCreated = false;
            ZLinkSpotActivation? activation = null;
            try
            {
                ConnectDiscoveredPubSubPeers();

                if (requestedSpotRid is RoutingId providedRid)
                {
                    nativeSpot.SetRoutingId(providedRid);
                }

                var spotScope = _services.CreateAsyncScope();
                try
                {
                    activation = new ZLinkSpotActivation(
                        _runtime,
                        spotScope,
                        nativeSpot,
                        Node.RoutingId,
                        spotName,
                        _spotChannelName,
                        _frameworkRegistration.DefaultTimeout,
                        _registration.Router?.SocketOptions.SendTimeout
                            ?? TimeSpan.FromMilliseconds(200));

                    var spot = (IZLinkSpot)ActivatorUtilities.CreateInstance(
                        spotScope.ServiceProvider,
                        spotType,
                        activation);

                    activation.AttachSpot(spot);
                    spot.Configure();
                    activation.BindDescriptors();
                    await activation.InitializeAsync(cancellationToken);
                    _spots.Add(activation.SpotRid, activation);
                    spotCreated = true;

                    return new ZLinkSpotCreateResult(activation.SpotRid, spotName, true);
                }
                catch
                {
                    if (activation is null)
                    {
                        await spotScope.DisposeAsync();
                    }
                    else
                    {
                        await activation.DisposeAsync();
                    }

                    throw;
                }
            }
            finally
            {
                if (!spotCreated && activation is null)
                {
                    await nativeSpot.DisposeAsync();
                }
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (_spots.TryGetValue(spotRid, out var activation))
        {
            return ValueTask.FromResult<ZLinkSpotInfo?>(
                new ZLinkSpotInfo(activation.SpotRid, activation.SpotName));
        }

        return ValueTask.FromResult<ZLinkSpotInfo?>(null);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        IReadOnlyList<ZLinkSpotInfo> items = _spots.Values
            .Select(static activation => new ZLinkSpotInfo(activation.SpotRid, activation.SpotName))
            .OrderBy(static item => item.SpotName, StringComparer.Ordinal)
            .ToArray();
        return ValueTask.FromResult(items);
    }

    public async ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (!_spots.Remove(spotRid, out var activation))
            {
                return false;
            }

            await activation.CloseAsync(cancellationToken);
            await activation.DisposeAsync();
            return true;
        }
        finally
        {
            _gate.Release();
        }
    }

    public ValueTask<bool> ConnectRouterAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!TryAddRouterConnection(endpoint))
        {
            return ValueTask.FromResult(false);
        }

        Node.ConnectPeer(endpoint);
        return ValueTask.FromResult(true);
    }

    public ValueTask<bool> ConnectPubSubAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!TryAddPubSubConnection(endpoint))
        {
            return ValueTask.FromResult(false);
        }

        Node.ConnectPeer(endpoint);
        return ValueTask.FromResult(true);
    }

    public void DisconnectRouter(string endpoint)
    {
        Node.DisconnectPeer(endpoint);
        RemoveRouterConnection(endpoint);
    }

    public void DisconnectPubSub(string endpoint)
    {
        Node.DisconnectPeer(endpoint);
        RemovePubSubConnection(endpoint);
    }

    public IReadOnlyList<string> ListRouterConnections()
    {
        lock (_connectionsGate)
        {
            return _routerManualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
        }
    }

    public IReadOnlyList<string> ListPubSubConnections()
    {
        lock (_connectionsGate)
        {
            return _pubSubManualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
        }
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

            if (TryAddDiscoveredPubSubConnection(peer.Endpoint))
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

        foreach (var activation in _spots.Values.ToArray())
        {
            await activation.DisposeAsync();
        }

        foreach (var publisher in _publisherBundles.Values)
        {
            await publisher.DisposeAsync();
        }

        foreach (var channel in _channelBundles.Values)
        {
            await channel.DisposeAsync();
        }

        await Node.DisposeAsync();
        _stopSource.Dispose();
        _gate.Dispose();
    }

    private bool TryAddRouterConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            return _routerManualConnections.Add(endpoint);
        }
    }

    private bool TryAddPubSubConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            return _pubSubManualConnections.Add(endpoint);
        }
    }

    private void RemoveRouterConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            _routerManualConnections.Remove(endpoint);
        }
    }

    private void RemovePubSubConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            _pubSubManualConnections.Remove(endpoint);
            _pubSubDiscoveredConnections.Remove(endpoint);
        }
    }

    private bool TryAddDiscoveredPubSubConnection(string endpoint)
    {
        lock (_connectionsGate)
        {
            return !_pubSubManualConnections.Contains(endpoint)
                && _pubSubDiscoveredConnections.Add(endpoint);
        }
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
