using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Builds one reconcile loop per auto-connect capability from the framework
/// registration and the started runtime state. Dialing capabilities get an
/// executor over the channel's core socket surface; advertise-only
/// capabilities (publishers) run the same loop with a
/// never-called executor so their peer row is published and removed by the
/// same lifecycle. Core discovery is never involved.
/// </summary>
internal sealed class ZLinkLocationAutoConnectHost : IAsyncDisposable, IZLinkAutoConnectTopologyQuery
{
    private readonly ZLinkLocationRuntime _runtime;
    private readonly IZLinkMeshNodeLocationResolver _peers;
    private readonly ZLinkLocationOptions _options;
    private readonly IZLinkClientServerLocationStore? _clientServerStore;
    private readonly IZLinkLocationChangeStampStore? _stampStore;
    private readonly IZLinkLocationWatchStore? _watchStore;
    private readonly ZLinkOwnerLeaseTracker? _leaseTracker;
    private readonly ZLinkLocationEventEmitter _events;
    private readonly TimeProvider _time;
    private readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    private readonly List<ZLinkAutoConnectLoop> _loops = [];
    private readonly List<ZLinkAutoConnectReconciler> _reconcilers = [];
    private readonly System.Collections.Concurrent.ConcurrentDictionary<string, ZLinkAutoConnectReconciler>
        _routeMeshReconcilers = new(StringComparer.Ordinal);
    private readonly System.Collections.Concurrent.ConcurrentDictionary<
        (ZLinkLocationAutoConnectType Type, string MeshName, ZLinkLocationRole Role),
        ZLinkAutoConnectReconciler> _localReconcilers = new();
    private readonly object _disposeGate = new();
    private ZLinkClientServerDiscovery? _clientServerDiscovery;
    private int _disposed;
    private Task? _disposeTask;

    internal ZLinkLocationAutoConnectHost(
        ZLinkLocationRuntime runtime,
        IZLinkMeshNodeLocationResolver peers,
        ZLinkLocationOptions options,
        IZLinkLocationChangeStampStore? stampStore = null,
        IZLinkLocationWatchStore? watchStore = null,
        TimeProvider? timeProvider = null,
        ZLinkLocationEventEmitter? events = null,
        ZLinkOwnerLeaseTracker? leaseTracker = null,
        IZLinkClientServerLocationStore? clientServerStore = null)
    {
        _runtime = runtime;
        _peers = peers;
        _options = options;
        _clientServerStore = clientServerStore;
        _stampStore = stampStore;
        _watchStore = watchStore;
        _leaseTracker = leaseTracker;
        _events = events ?? ZLinkLocationEventEmitter.Disabled;
        _time = timeProvider ?? TimeProvider.System;
    }

    internal async ValueTask StartAsync(
        ZLinkFrameworkComponentState state,
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_loops.Count != 0) return;
            var registration = state.Registration;
            if (registration.Channels.Values.Any(static channel =>
                    channel.ClientServerRole is not null))
            {
                if (_clientServerStore is not null)
                {
                    _clientServerDiscovery = new ZLinkClientServerDiscovery(
                        _clientServerStore,
                        _runtime,
                        _options,
                        _leaseTracker);
                    await _clientServerDiscovery.StartAsync(state, cancellationToken)
                        .ConfigureAwait(false);
                }
            }

        foreach (var (name, channel) in registration.Channels)
        {
            switch (channel.AutoConnectType)
            {
                case ZLinkLocationAutoConnectType.Fanout:
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
                            subscriber.AcquisitionMode == ZLinkPeerAcquisitionMode.AutoConnect
                                ? new ConnectableSocketExecutor(subscriberSocket, subscriberBundle)
                                : NullExecutor.Instance);
                    }

                    break;
            }
        }

        foreach (var (name, spot) in registration.SpotNodes)
        {
            if (!state.SpotNodes.TryGetValue(name, out var node) || spot.Router is null) continue;

            var meshName = spot.SpotMeshChannelName ?? spot.SpotNodeName;
            // An ephemeral bind (port 0) must advertise the endpoint Core
            // actually bound — peers dial the descriptor row verbatim.
            var endpoint = spot.Router.BindEndpoint is { } configured
                           && !configured.EndsWith(":0", StringComparison.Ordinal)
                ? configured
                : node.Node.Status().LocalEndpoint;
            AddLoop(
                ZLinkLocationAutoConnectType.SpotMesh,
                meshName,
                ZLinkLocationRole.Spot,
                RidOrNull(spot.RoutingId),
                endpoint ?? string.Empty,
                (uint)spot.Router.SocketConfig.Weight,
                new SpotRouterExecutor(
                    node,
                    spot.Router.AcquisitionMode == ZLinkPeerAcquisitionMode.AutoConnect),
                retainRemovedMembers:
                    spot.Router.AcquisitionMode == ZLinkPeerAcquisitionMode.Manual,
                // The row carries Core's nonzero lifecycle token verbatim.
                // Admission compares this opaque token for exact equality.
                lifecycleGeneration: node.Node.MeshStatus().LifecycleGeneration,
                objectRole: spot.ObjectRole,
                objectCapabilities: BuildObjectCapabilities(spot),
                applicationVersion: registration.ApplicationVersion,
                maintenanceWave: registration.MaintenanceWave,
                placementWeight: spot.PlacementWeight,
                capacity: new ZLinkPlacementCapacity(
                    0,
                    0,
                    spot.MaxActiveObjects,
                    spot.MaxPendingActivations));
        }

            try
            {
                foreach (var loop in _loops)
                    await loop.StartAsync(cancellationToken).ConfigureAwait(false);
            }
            catch (Exception startFailure)
            {
                try
                {
                    await DisposeGenerationAsync().ConfigureAwait(false);
                }
                catch (Exception cleanupFailure)
                {
                    throw new AggregateException(startFailure, cleanupFailure);
                }

                throw;
            }
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    internal async ValueTask StopAsync(CancellationToken cancellationToken = default)
    {
        if (Volatile.Read(ref _disposed) != 0) return;
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await DisposeGenerationAsync().ConfigureAwait(false);
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    internal async ValueTask<bool> MarkDrainingAsync(
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var published = true;
            if (_clientServerDiscovery is not null)
                published &= await _clientServerDiscovery
                    .MarkDrainingAsync(cancellationToken)
                    .ConfigureAwait(false);
            foreach (var reconciler in _reconcilers)
                published &= await reconciler.MarkDrainingAsync(cancellationToken).ConfigureAwait(false);
            return published;
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    internal async ValueTask FreezeOwnerWritesAsync(
        CancellationToken cancellationToken = default)
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            foreach (var reconciler in _reconcilers)
                await reconciler.FreezeOwnerWritesAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    public ValueTask DisposeAsync()
    {
        Task task;
        TaskCompletionSource? start = null;
        lock (_disposeGate)
        {
            if (_disposeTask is null)
            {
                Volatile.Write(ref _disposed, 1);
                start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _disposeTask = DisposeCoreAsync(start.Task);
            }
            task = _disposeTask;
        }
        start?.TrySetResult();
        return new ValueTask(task);
    }

    private async Task DisposeCoreAsync(Task started)
    {
        await started.ConfigureAwait(false);
        await _lifecycleGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
        try
        {
            await DisposeGenerationAsync().ConfigureAwait(false);
        }
        finally
        {
            _lifecycleGate.Release();
            _lifecycleGate.Dispose();
        }
    }

    private async ValueTask DisposeGenerationAsync()
    {
        var clientServerDiscovery = _clientServerDiscovery;
        _clientServerDiscovery = null;
        var loops = _loops.ToArray();
        var reconcilers = _reconcilers.ToArray();
        _loops.Clear();
        _reconcilers.Clear();
        _routeMeshReconcilers.Clear();
        List<Exception>? failures = null;
        if (clientServerDiscovery is not null)
        {
            try
            {
                await clientServerDiscovery.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }
        foreach (var loop in loops)
        {
            try
            {
                await loop.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }
        foreach (var reconciler in reconcilers) reconciler.RemovePeerMetric();

        if (failures is { Count: 1 })
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures is { Count: > 1 }) throw new AggregateException(failures);
    }

    private void AddLoop(
        ZLinkLocationAutoConnectType type,
        string meshName,
        ZLinkLocationRole role,
        RoutingId? nodeRid,
        string endpoint,
        uint weight,
        IZLinkAutoConnectExecutor executor,
        bool retainRemovedMembers = false,
        ulong lifecycleGeneration = 0,
        ZLinkMeshNodeObjectRole objectRole = ZLinkMeshNodeObjectRole.None,
        IReadOnlyList<ZLinkObjectCapability>? objectCapabilities = null,
        long applicationVersion = 0,
        string? maintenanceWave = null,
        int placementWeight = 100,
        ZLinkPlacementCapacity? capacity = null)
    {
        // A descriptor is keyed by (MeshName, Rid), so a capability without
        // an identity cannot be advertised (an endpoint-less member still
        // publishes for mesh-membership classification; planners skip its
        // empty endpoint as a dial target). When it also never dials
        // (advertise-only server roles) there is nothing to reconcile; a
        // dialing capability (a client dealer or subscriber without a
        // configured identity) still gets a dial-only loop that connects to
        // remote descriptors.
        var advertisable = nodeRid is { Size: > 0 };
        if (!advertisable && ReferenceEquals(executor, NullExecutor.Instance)) return;

        var local = new ZLinkAutoConnectLocal(type, meshName, role, nodeRid, endpoint);
        var effectiveLifecycleGeneration = lifecycleGeneration == 0
            ? CreateLifecycleNonce()
            : lifecycleGeneration;
        var row = advertisable
            ? new ZLinkMeshNodeDescriptor(
                meshName, nodeRid!.Value,
                effectiveLifecycleGeneration, DescriptorRevision: 1,
                endpoint,
                new Dictionary<string, int>(StringComparer.Ordinal) { [meshName] = (int)weight },
                SecurityIdentity: ZLinkTransportSecurityIdentity.Plaintext,
                OwnerId: string.Empty,
                LeaseGeneration: 0,
                UpdatedAt: default)
            {
                ApplicationVersion = applicationVersion,
                ObjectRole = objectRole,
                ObjectCapabilities =
                    objectCapabilities ?? Array.Empty<ZLinkObjectCapability>(),
                MaintenanceWave = maintenanceWave,
                State = ZLinkFrameworkRuntimeState.Serving,
                PlacementWeight = placementWeight,
                Capacity = capacity ?? new ZLinkPlacementCapacity(
                    0,
                    0,
                    10_000,
                    128)
            }
            : null;
        var reconciler = new ZLinkAutoConnectReconciler(
            local, row, _runtime, _peers, executor, _options, _time, _events,
            retainRemovedMembers);
        reconciler.RegisterPeerMetric();
        _reconcilers.Add(reconciler);
        _localReconcilers[(type, meshName, role)] = reconciler;
        if (type is ZLinkLocationAutoConnectType.RouteMesh
            or ZLinkLocationAutoConnectType.SpotMesh)
            _routeMeshReconcilers[meshName] = reconciler;
        _loops.Add(new ZLinkAutoConnectLoop(
            reconciler, local, _options, _stampStore, _watchStore, _time, _leaseTracker));
    }

    public bool? IsKnownRouteMeshPeer(string meshName, RoutingId nodeRid)
    {
        return _routeMeshReconcilers.TryGetValue(meshName, out var reconciler)
            ? reconciler.KnowsPeer(nodeRid)
            : null;
    }

    internal void SetLocalWeight(
        ZLinkLocationAutoConnectType type,
        string meshName,
        ZLinkLocationRole role,
        uint weight)
    {
        if (_localReconcilers.TryGetValue((type, meshName, role), out var reconciler))
            reconciler.SetLocalWeight(weight);
    }

    internal ValueTask<bool> SetLocalWeightAsync(
        ZLinkLocationAutoConnectType type,
        string meshName,
        ZLinkLocationRole role,
        uint weight,
        CancellationToken cancellationToken)
    {
        return _localReconcilers.TryGetValue((type, meshName, role), out var reconciler)
            ? reconciler.SetLocalWeightAsync(weight, cancellationToken)
            : ValueTask.FromResult(true);
    }

    private static RoutingId? RidOrNull(RoutingId routingId) =>
        routingId.Size > 0 ? routingId : null;

    private static ulong CreateLifecycleNonce()
    {
        Span<byte> bytes = stackalloc byte[sizeof(ulong)];
        ulong value;
        do
        {
            System.Security.Cryptography.RandomNumberGenerator.Fill(bytes);
            value = System.Buffers.Binary.BinaryPrimitives
                .ReadUInt64BigEndian(bytes);
        } while (value == 0);
        return value;
    }

    private static IReadOnlyList<ZLinkObjectCapability> BuildObjectCapabilities(
        ZLinkSpotNodeRegistration registration)
    {
        var capabilities = new List<ZLinkObjectCapability>(
            registration.SpotRelocations.Count
            + registration.InstanceSpotRelocations.Count
            + registration.ActorRelocations.Count);
        AddCapabilities(
            capabilities,
            ZLinkPlacementObjectKind.UserSpot,
            registration.SpotRelocations);
        AddCapabilities(
            capabilities,
            ZLinkPlacementObjectKind.InstanceSpot,
            registration.InstanceSpotRelocations);
        AddCapabilities(
            capabilities,
            ZLinkPlacementObjectKind.Actor,
            registration.ActorRelocations);
        return capabilities
            .OrderBy(static capability => capability.ObjectKind)
            .ThenBy(static capability => capability.StableType, StringComparer.Ordinal)
            .ToArray();
    }

    private static void AddCapabilities(
        ICollection<ZLinkObjectCapability> capabilities,
        ZLinkPlacementObjectKind objectKind,
        IReadOnlyDictionary<string, ZLinkObjectRelocationRegistration>
            registrations)
    {
        foreach (var (stableType, registration) in registrations)
        {
            capabilities.Add(new ZLinkObjectCapability(
                objectKind,
                stableType,
                registration.PolicyKind switch
                {
                    0 => ZLinkObjectMaintenancePolicyKind.Disabled,
                    1 => ZLinkObjectMaintenancePolicyKind.Recreate,
                    2 => ZLinkObjectMaintenancePolicyKind.Snapshot,
                    _ => throw new ZLinkConfigurationException(
                        $"Unknown relocation policy kind '{registration.PolicyKind}'.")
                },
                registration.AdapterType is not null,
                registration.Placement.PlacementProfiles.ToHashSet(
                    StringComparer.Ordinal),
                registration.Placement.MaxActiveObjects,
                registration.Placement.MaxPendingActivations));
        }
    }

    private static RoutingId? BundleRid(string? bundleRid, RoutingId fallback) =>
        bundleRid is { Length: > 0 } value ? RoutingId.From(value) : RidOrNull(fallback);

    private sealed class NullExecutor : IZLinkAutoConnectExecutor
    {
        internal static readonly NullExecutor Instance = new();

        public bool Connect(ZLinkAutoConnectTarget target) => true;

        public bool Disconnect(ZLinkAutoConnectTarget target) => true;
    }

    private sealed class ConnectableSocketExecutor(
        IZLinkBackendConnectableSocket socket,
        ZLinkChannelRuntimeBundle bundle) : IZLinkAutoConnectExecutor
    {
        public bool Connect(ZLinkAutoConnectTarget target)
            => bundle.ConnectAuto(socket, target.Endpoint);

        public bool Disconnect(ZLinkAutoConnectTarget target)
            => bundle.DisconnectAuto(socket, target.Endpoint);
    }

    private sealed class SpotRouterExecutor(
        ZLinkSpotNodeRuntime node,
        bool connectRouter) : IZLinkAutoConnectExecutor
    {
        public bool Connect(ZLinkAutoConnectTarget target)
        {
            if (!connectRouter || !target.InitiatesSpotRouterLink) return true;
            return node.ConnectPeerAuto(target.NodeRid, target.Endpoint);
        }

        public bool Disconnect(ZLinkAutoConnectTarget target)
        {
            // Row absence alone must not tear down a live admitted transport
            // (store outages and lease expiry windows ride established
            // connections, SF-B2); only the dialing side retires its own
            // connect intent here.
            if (!connectRouter || !target.InitiatesSpotRouterLink) return true;
            return node.DisconnectPeerAuto(target.Endpoint);
        }
    }

}
