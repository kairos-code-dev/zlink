using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Builds one reconcile loop per auto-connect capability from the framework
/// registration and the started runtime state. Dialing capabilities get an
/// executor over the channel's core socket surface; advertise-only
/// capabilities (server routers, publishers) run the same loop with a
/// never-called executor so their peer row is published and removed by the
/// same lifecycle. Core discovery is never involved.
/// </summary>
internal sealed class ZLinkLocationAutoConnectHost : IAsyncDisposable
{
    private readonly ZLinkLocationRuntime _runtime;
    private readonly IZLinkPeerLocationResolver _peers;
    private readonly ZLinkLocationOptions _options;
    private readonly IZLinkLocationChangeStampStore? _stampStore;
    private readonly IZLinkLocationWatchStore? _watchStore;
    private readonly ZLinkOwnerLeaseTracker? _leaseTracker;
    private readonly ZLinkLocationEventEmitter _events;
    private readonly TimeProvider _time;
    private readonly List<ZLinkAutoConnectLoop> _loops = [];

    internal ZLinkLocationAutoConnectHost(
        ZLinkLocationRuntime runtime,
        IZLinkPeerLocationResolver peers,
        ZLinkLocationOptions options,
        IZLinkLocationChangeStampStore? stampStore = null,
        IZLinkLocationWatchStore? watchStore = null,
        TimeProvider? timeProvider = null,
        ZLinkLocationEventEmitter? events = null,
        ZLinkOwnerLeaseTracker? leaseTracker = null)
    {
        _runtime = runtime;
        _peers = peers;
        _options = options;
        _stampStore = stampStore;
        _watchStore = watchStore;
        _leaseTracker = leaseTracker;
        _events = events ?? ZLinkLocationEventEmitter.Disabled;
        _time = timeProvider ?? TimeProvider.System;
    }

    internal async ValueTask StartAsync(
        ZLinkFrameworkRuntimeState state,
        CancellationToken cancellationToken = default)
    {
        var registration = state.Registration;

        foreach (var (name, route) in registration.RouteChannels)
        {
            if (!state.RouteChannels.TryGetValue(name, out var runtime)) continue;

            var manual = new HashSet<string>(route.ManualConnections, StringComparer.Ordinal);
            AddLoop(
                ZLinkLocationAutoConnectType.RouteMesh,
                route.RouterChannelId,
                ZLinkLocationRole.Router,
                RidOrNull(route.RoutingId),
                route.BindEndpoint ?? string.Empty,
                (uint)route.SocketConfig.Weight,
                new RouteChannelExecutor(runtime, manual));
        }

        foreach (var (name, channel) in registration.Channels)
        {
            switch (channel.AutoConnectType)
            {
                case ZLinkAutoConnectType.ClientServer:
                    if (channel.Server is { } server
                        && state.ServerBundles.TryGetValue(name, out var serverBundle))
                    {
                        AddLoop(
                            ZLinkLocationAutoConnectType.ClientServer,
                            channel.ChannelName,
                            ZLinkLocationRole.Router,
                            BundleRid(serverBundle.LocalRid, channel.RoutingId),
                            server.BindEndpoint ?? string.Empty,
                            (uint)server.SocketConfig.Weight,
                            NullExecutor.Instance);
                    }

                    if (channel.Client is { } client
                        && state.ClientBundles.TryGetValue(name, out var clientBundle)
                        && clientBundle.Socket is IZLinkBackendConnectableSocket dealerSocket)
                    {
                        AddLoop(
                            ZLinkLocationAutoConnectType.ClientServer,
                            channel.ChannelName,
                            ZLinkLocationRole.Dealer,
                            BundleRid(clientBundle.LocalRid, channel.RoutingId),
                            client.BindEndpoint ?? string.Empty,
                            (uint)client.SocketConfig.Weight,
                            new ConnectableSocketExecutor(dealerSocket, clientBundle));
                    }

                    break;

                case ZLinkAutoConnectType.Fanout:
                    if (channel.Publisher is { } publisher
                        && state.PublisherBundles.TryGetValue(name, out var publisherBundle))
                    {
                        AddLoop(
                            ZLinkLocationAutoConnectType.Fanout,
                            channel.ChannelName,
                            ZLinkLocationRole.Pub,
                            BundleRid(publisherBundle.LocalRid, channel.RoutingId),
                            publisher.BindEndpoint ?? string.Empty,
                            (uint)publisher.SocketConfig.Weight,
                            NullExecutor.Instance);
                    }

                    if (channel.Subscriber is { } subscriber
                        && state.SubscriberBundles.TryGetValue(name, out var subscriberBundle)
                        && subscriberBundle.Socket is IZLinkBackendConnectableSocket subscriberSocket)
                    {
                        AddLoop(
                            ZLinkLocationAutoConnectType.Fanout,
                            channel.ChannelName,
                            ZLinkLocationRole.Sub,
                            BundleRid(subscriberBundle.LocalRid, channel.RoutingId),
                            string.Empty,
                            (uint)subscriber.SocketConfig.Weight,
                            new ConnectableSocketExecutor(subscriberSocket, subscriberBundle));
                    }

                    break;
            }
        }

        foreach (var (name, spot) in registration.SpotNodes)
        {
            if (!state.SpotNodes.TryGetValue(name, out var node) || spot.Router is null) continue;

            var meshName = spot.SpotDiscoveryChannelName ?? spot.SpotNodeName;
            var endpoint = spot.Router.BindEndpoint ?? node.Node.Status().LocalEndpoint;
            var manual = new HashSet<string>(
                spot.Router.ManualConnections.Select(static connection => connection.Endpoint),
                StringComparer.Ordinal);
            AddLoop(
                ZLinkLocationAutoConnectType.SpotMesh,
                meshName,
                ZLinkLocationRole.Spot,
                RidOrNull(spot.RoutingId),
                endpoint ?? string.Empty,
                (uint)spot.Router.SocketConfig.Weight,
                new SpotNodeExecutor(node.Node, manual));
        }

        foreach (var loop in _loops)
        {
            await loop.StartAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    internal async ValueTask StopAsync(CancellationToken cancellationToken = default)
    {
        foreach (var loop in _loops)
        {
            await loop.StopAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    public async ValueTask DisposeAsync()
    {
        foreach (var loop in _loops)
        {
            await loop.DisposeAsync().ConfigureAwait(false);
        }

        _loops.Clear();
    }

    private void AddLoop(
        ZLinkLocationAutoConnectType type,
        string meshName,
        ZLinkLocationRole role,
        RoutingId? nodeRid,
        string endpoint,
        uint weight,
        IZLinkAutoConnectExecutor executor)
    {
        // A capability with neither identity nor endpoint cannot be keyed
        // or advertised. When it also never dials (advertise-only server
        // roles) there is nothing to reconcile; a dialing capability (a
        // client dealer or subscriber without a configured identity) still
        // gets a dial-only loop that connects to remote rows.
        var advertisable = nodeRid is not null || !string.IsNullOrEmpty(endpoint);
        if (!advertisable && ReferenceEquals(executor, NullExecutor.Instance)) return;

        var local = new ZLinkAutoConnectLocal(type, meshName, role, nodeRid, endpoint);
        var row = advertisable
            ? new ZLinkPeerLocation(
                type, meshName, nodeRid, role, endpoint, weight, 0,
                Metadata: null, Capabilities: null,
                OwnerId: string.Empty, Generation: 0, UpdatedAt: default)
            : null;
        var reconciler = new ZLinkAutoConnectReconciler(
            local, row, _runtime, _peers, executor, _options, _time, _events);
        _loops.Add(new ZLinkAutoConnectLoop(
            reconciler, local, _options, _stampStore, _watchStore, _time, _leaseTracker));
    }

    private static RoutingId? RidOrNull(RoutingId routingId) =>
        routingId.Size > 0 ? routingId : null;

    private static RoutingId? BundleRid(string? bundleRid, RoutingId fallback) =>
        bundleRid is { Length: > 0 } value ? RoutingId.From(value) : RidOrNull(fallback);

    private sealed class NullExecutor : IZLinkAutoConnectExecutor
    {
        internal static readonly NullExecutor Instance = new();

        public void Connect(ZLinkAutoConnectTarget target)
        {
        }

        public void Disconnect(ZLinkAutoConnectTarget target)
        {
        }
    }

    private sealed class RouteChannelExecutor(
        ZLinkRouteChannelRuntime runtime,
        HashSet<string> manualEndpoints) : IZLinkAutoConnectExecutor
    {
        public void Connect(ZLinkAutoConnectTarget target) =>
            Guard(() => runtime.Connect(target.Endpoint));

        public void Disconnect(ZLinkAutoConnectTarget target)
        {
            // Manual connections always win: auto reconcile never cuts an
            // endpoint the user configured explicitly.
            if (manualEndpoints.Contains(target.Endpoint)) return;

            Guard(() => runtime.Disconnect(target.Endpoint));
        }
    }

    private sealed class ConnectableSocketExecutor(
        IZLinkBackendConnectableSocket socket,
        ZLinkChannelRuntimeBundle bundle) : IZLinkAutoConnectExecutor
    {
        public void Connect(ZLinkAutoConnectTarget target)
        {
            if (bundle.ContainsManualConnection(target.Endpoint)) return;

            Guard(() => socket.Connect(target.Endpoint));
        }

        public void Disconnect(ZLinkAutoConnectTarget target)
        {
            if (bundle.ContainsManualConnection(target.Endpoint)) return;

            Guard(() => socket.Disconnect(target.Endpoint));
        }
    }

    private sealed class SpotNodeExecutor(
        IZLinkBackendSpotNode node,
        HashSet<string> manualEndpoints) : IZLinkAutoConnectExecutor
    {
        public void Connect(ZLinkAutoConnectTarget target)
        {
            if (manualEndpoints.Contains(target.Endpoint)) return;

            Guard(() =>
            {
                if (target.NodeRid is { Size: > 0 } rid)
                {
                    node.ConnectPeer(rid, target.Endpoint);
                    return;
                }

                node.ConnectPeer(target.Endpoint);
            });
        }

        public void Disconnect(ZLinkAutoConnectTarget target)
        {
            if (manualEndpoints.Contains(target.Endpoint)) return;

            Guard(() => node.DisconnectPeer(target.Endpoint));
        }
    }

    /// <summary>Connect failures never abort a reconcile tick and never
    /// remove a location row; the next tick retries naturally.</summary>
    private static void Guard(Action action)
    {
        try
        {
            action();
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery($"auto-connect executor error: {exception.Message}");
        }
    }
}
