using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeRuntime : IAsyncDisposable
{
    private readonly ZLinkSpotNodeBundleRegistry _bundles;
    private readonly ZLinkFrameworkRegistration _frameworkRegistration;
    private readonly ZLinkSpotMonitoringSnapshotProvider _monitoringSnapshots;
    private readonly ZLinkSpotPeerConnectionSet _peerConnections = new();
    private readonly ZLinkSpotPeerConnector _peerConnector;
    private readonly ZLinkAsyncSubmitter _nodeSubmitter;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly IServiceProvider _services;
    private readonly ZLinkSpotNodeCatalog _spots;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly object _disposeGate = new();
    private Task? _disposeTask;
    private bool _stopSourceDisposed;
    private IActorCreateOperationTarget? _actorCreateOperationTarget;
    private IActorDestroyOperationTarget? _actorDestroyOperationTarget;
    private ZLinkInstanceSpotActivationTarget? _instanceSpotActivationTarget;
    private IUserSpotOperationTarget? _userSpotOperationTarget;
    private IZLinkBackendSpot? _entrySpot;
    private ZLinkEntrySpotDispatchPump? _entryDispatchPump;
    private ZLinkSpotOutboundTransport? _entryOutbound;
    private ZLinkEntrySpotActivation? _entrySpotActivation;
    private ZLinkMeshNodeRouteDispatcher? _nodeRouteDispatcher;
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
        ZLinkLocationLifecycle? locationLifecycle,
        ZLinkMeshNodeStartupState? startupState = null,
        string? entrySpotId = null)
    {
        _services = services;
        _runtime = runtime;
        _frameworkRegistration = frameworkRegistration;
        Registration = registration;
        Node = node;
        if (locationLifecycle is not null)
        {
            if (node is not IZLinkBackendAuthorityObserver authorityObserver)
                throw new InvalidOperationException(
                    "The MeshNode backend does not support authority fencing.");
            authorityObserver.SetLocalOwnerLeaseGeneration(
                checked((ulong)locationLifecycle.OwnerToken.LeaseGeneration));
        }
        StartupState = startupState;
        EntrySpotId = entrySpotId ?? startupState?.EntrySpotId
            ?? registration.EntrySpotId;
        _taskRunner = new ZLinkRuntimeTaskRunner(
            runtime.ErrorSink,
            _stopSource.Token,
            runtime.ExecutionOwner);
        _monitoringSnapshots = new ZLinkSpotMonitoringSnapshotProvider(node);
        _peerConnector = new ZLinkSpotPeerConnector(node, _peerConnections);
        _nodeSubmitter = new ZLinkAsyncSubmitter(
            node.OnSendReady,
            frameworkRegistration.DefaultSocketSendTimeout,
            _stopSource.Token,
            ZLinkAsyncSubmitter.ResolvePendingCapacity(
                registration.Router?.SocketConfig.SendHighWaterMark ?? 0));
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
        if (registration.SpotRelocations.Count > 0
            && frameworkRegistration.Locations.ResolveStore() is { } authorityStore)
        {
            _userSpotOperationTarget = new ZLinkUserSpotOperationTarget(
                authorityStore,
                _spots,
                node,
                registration,
                frameworkRegistration.Codecs);
            node.SetUserSpotOperationTarget(_userSpotOperationTarget);
        }
        if (registration.ActorFactories.Count > 0
            && frameworkRegistration.Locations.ResolveStore() is { } actorAuthorityStore)
        {
            var actorOperationTarget = new ZLinkActorOperationTarget(
                actorAuthorityStore,
                runtime,
                node,
                spotChannelName,
                frameworkRegistration.Codecs);
            _actorCreateOperationTarget = actorOperationTarget;
            _actorDestroyOperationTarget = actorOperationTarget;
            node.SetActorCreateOperationTarget(_actorCreateOperationTarget);
            node.SetActorDestroyOperationTarget(_actorDestroyOperationTarget);
        }
        if (registration.InstanceSpotFactories.Count > 0
            && frameworkRegistration.Locations.ResolveStore()
                is IZLinkAuthorityStore instanceAuthorityStore
            && frameworkRegistration.Locations.RelocationStoreInstance
                is { } relocationStore
            && locationLifecycle is not null)
        {
            _instanceSpotActivationTarget = new ZLinkInstanceSpotActivationTarget(
                instanceAuthorityStore,
                relocationStore,
                _spots,
                node,
                registration,
                locationLifecycle.OwnerToken);
            node.SetInstanceSpotActivationTarget(_instanceSpotActivationTarget);
        }
    }

    internal ZLinkMeshNodeStartupState? StartupState { get; }

    internal string EntrySpotId { get; }

    public string Name => Registration.SpotNodeName;

    internal ZLinkSpotNodeCatalog Catalog => _spots;

    public IReadOnlySet<Type> SpotFactories => Registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    internal ZLinkSpotNodeRegistration Registration { get; }

    internal async ValueTask<(
        UserSpotCreateCompletion Completion,
        IReadOnlyList<Message> Reply)> CreateUserSpotLocalAsync(
        string spotId,
        string stableType,
        ObjectReservationFence reservation,
        ulong deadlineUnixMs,
        CancellationToken cancellationToken)
    {
        var target = _userSpotOperationTarget
                     ?? throw new ZLinkFrameworkException(
                         ZLinkFrameworkErrorKind.InvalidConfiguration,
                         $"MeshNode '{Name}' does not host User Spot factories.");
        var remaining = checked((long)deadlineUnixMs)
                        - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (remaining <= 0)
            throw new TimeoutException("The User Spot create deadline elapsed.");

        var operationId = Node.AllocateOperationId();
        var correlation = operationId.Low;
        var status = Node.MeshStatus();
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            _stopSource.Token);
        deadline.CancelAfter(TimeSpan.FromMilliseconds(remaining));
        var terminal = await target.CreateAsync(
                new UserSpotCreateOperation(
                    correlation,
                    operationId,
                    Node.RoutingId,
                    status.LifecycleGeneration,
                    spotId,
                    stableType,
                    reservation,
                    deadlineUnixMs),
                deadline.Token)
            .ConfigureAwait(false);
        if (terminal.Result != RequestResult.Ok
            || terminal.Completion is not UserSpotCreateCompletion completion)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotCreateFailed,
                "Local User Spot create failed.");

        var reply = terminal.ReplyParts is null
            ? Array.Empty<Message>()
            : terminal.ReplyParts.Select(static part => Message.From(part.Span)).ToArray();
        return (completion, reply);
    }

    internal ValueTask<InstanceSpotActivationTerminal> ActivateInstanceSpotLocalAsync(
        InstanceSpotActivationTarget target,
        RoutingId sourceNodeRid,
        ulong sourceNodeGeneration,
        MeshOperationId operationId,
        string sourceSpotId,
        IReadOnlyList<ReadOnlyMemory<byte>> payload,
        bool request,
        ulong deadlineUnixMs,
        ReadOnlyMemory<byte>? metadata,
        CancellationToken cancellationToken)
    {
        var activationTarget = _instanceSpotActivationTarget
                               ?? throw new ZLinkFrameworkException(
                                   ZLinkFrameworkErrorKind.InvalidConfiguration,
                                   $"MeshNode '{Name}' does not host Instance Spot factories.");
        return activationTarget.ActivateAsync(
            new InstanceSpotActivationOperation(
                target,
                sourceNodeRid,
                sourceNodeGeneration,
                sourceSpotId,
                operationId,
                request,
                request ? operationId.Low : 0,
                deadlineUnixMs),
            metadata,
            payload,
            cancellationToken);
    }

    internal async ValueTask<(
        ActorCreateCompletion Completion,
        IReadOnlyList<Message> Reply)> CreateActorLocalAsync(
        string actorId,
        string stableType,
        ObjectReservationFence reservation,
        ulong deadlineUnixMs,
        CancellationToken cancellationToken)
    {
        var target = _actorCreateOperationTarget
                     ?? throw new ZLinkFrameworkException(
                         ZLinkFrameworkErrorKind.InvalidConfiguration,
                         $"MeshNode '{Name}' does not host Actor factories.");
        var remaining = checked((long)deadlineUnixMs)
                        - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (remaining <= 0)
            throw new TimeoutException("The Actor create deadline elapsed.");

        var operationId = Node.AllocateOperationId();
        var correlation = operationId.Low;
        var status = Node.MeshStatus();
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken,
            _stopSource.Token);
        deadline.CancelAfter(TimeSpan.FromMilliseconds(remaining));
        var terminal = await target.CreateAsync(
                new ActorCreateOperation(
                    correlation,
                    operationId,
                    Node.RoutingId,
                    status.LifecycleGeneration,
                    actorId,
                    stableType,
                    reservation,
                    deadlineUnixMs),
                deadline.Token)
            .ConfigureAwait(false);
        if (terminal.Result != RequestResult.Ok || terminal.Completion is null)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                $"Local Actor create failed with '{terminal.Result}'/"
                + $"'{terminal.FailureCode}'.");

        var reply = terminal.ReplyParts is null
            ? Array.Empty<Message>()
            : terminal.ReplyParts.Select(static part => Message.From(part.Span)).ToArray();
        return (terminal.Completion, reply);
    }

    internal ValueTask<ActorDestroyOperationTerminal> DestroyActorLocalAsync(
        ActorDestroyOperation operation,
        CancellationToken cancellationToken)
    {
        var target = _actorDestroyOperationTarget
                     ?? throw new ZLinkFrameworkException(
                         ZLinkFrameworkErrorKind.InvalidConfiguration,
                         $"MeshNode '{Name}' does not host Actor factories.");
        return target.DestroyAsync(operation, cancellationToken);
    }

    internal bool UsesManualRouterAcquisition =>
        Registration.Router?.AcquisitionMode == ZLinkPeerAcquisitionMode.Manual;

    internal bool IsExplicitManualRouterRouteDisconnected(RoutingId targetNodeRid)
    {
        if (Registration.Router is not
            {
                AcquisitionMode: ZLinkPeerAcquisitionMode.Manual
            } router)
            return false;

        var targetEndpoints = router.PeerRoutingIds
            .Where(pair => pair.Value == targetNodeRid)
            .Select(pair => pair.Key)
            .ToArray();
        if (targetEndpoints.Length == 0) return false;

        var configuredEndpoints = router.ManualConnections.ListConnections();
        return targetEndpoints.All(endpoint => !configuredEndpoints.Contains(endpoint, StringComparer.Ordinal));
    }

    internal bool TryClassifyManualRouterTarget(
        RoutingId targetNodeRid,
        out bool connected)
    {
        connected = false;
        if (Registration.Router is not
            {
                AcquisitionMode: ZLinkPeerAcquisitionMode.Manual
            } router)
            return false;

        var matchingEndpoints = router.PeerRoutingIds
            .Where(pair => pair.Value == targetNodeRid)
            .Select(pair => pair.Key)
            .ToArray();
        if (matchingEndpoints.Length == 0)
        {
            if (_peerConnections.HasRetainedManualPeer(targetNodeRid)) return true;
            var peer = Node.MeshPeers().FirstOrDefault(candidate =>
                candidate.RoutingId == targetNodeRid);
            if (peer is null) return false;
            connected = peer.State == MeshPeerState.Admitted;
            return true;
        }

        var configuredEndpoints = router.ManualConnections.ListConnections();
        connected = matchingEndpoints.Any(endpoint =>
            configuredEndpoints.Contains(endpoint, StringComparer.Ordinal));
        return true;
    }

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => _spots.Spots;

    internal ZLinkEntrySpotActivation? EntrySpotActivation => _entrySpotActivation;

    internal ZLinkSpotOutboundTransport EntryOutbound => _entryOutbound
        ?? throw new InvalidOperationException($"SPOT node '{Name}' entry outbound transport is not initialized.");

    internal bool TrySendToNodeOnce(
        RoutingId targetNodeRid,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        return ZLinkSubmitFailureMapper.AcceptOrThrow(
            Node.SendToNode(targetNodeRid, parts, SendFlags.DontWait, metadata),
            $"node '{targetNodeRid}'");
    }

    internal ValueTask<ZLinkOneWaySubmitResult> SendToNodeAsync(
        RoutingId targetNodeRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        if (targetNodeRid == Node.RoutingId)
            return SubmitToLocalNodeAsync(parts, cancellationToken, metadata);

        return _nodeSubmitter.SubmitAsync(
            parts,
            pending => ZLinkSubmitFailureMapper.AcceptOrThrow(
                Node.SendToNode(targetNodeRid, pending, SendFlags.DontWait, metadata),
                $"node '{targetNodeRid}'"),
            cancellationToken);
    }

    private ValueTask<ZLinkOneWaySubmitResult> SubmitToLocalNodeAsync(
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata)
    {
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_stopSource.IsCancellationRequested)
                return ValueTask.FromResult(new ZLinkOneWaySubmitResult(ZLinkOneWaySubmitStatus.Shutdown));
            if (_nodeRouteDispatcher is null)
                return ValueTask.FromResult(new ZLinkOneWaySubmitResult(ZLinkOneWaySubmitStatus.TargetNotFound));
            if (!ZLinkMeshMetadataCodec.TryDecode(metadata.Span, out var decodedMetadata))
                throw new ArgumentException("Application metadata is malformed.", nameof(metadata));

            var received = new ZLinkBackendRouteReceived(
                parts,
                Node.RoutingId,
                spotId: null,
                requestSeq: null,
                reply: null,
                metadata: decodedMetadata);
            if (_nodeRouteDispatcher.TryDispatch(received))
                return ValueTask.FromResult(new ZLinkOneWaySubmitResult(ZLinkOneWaySubmitStatus.Submitted));

            received.Dispose();
            return ValueTask.FromResult(new ZLinkOneWaySubmitResult(ZLinkOneWaySubmitStatus.Shutdown));
        }
        catch
        {
            ZLinkMessageParts.DisposeAll(parts);
            throw;
        }
    }

    internal ValueTask<ZLinkOneWaySubmitResult> SendToActorAsync(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _nodeSubmitter.SubmitAsync(
            parts,
            pending => ZLinkSubmitFailureMapper.AcceptOrThrow(
                Node.SendToActor(actor, pending, SendFlags.DontWait),
                $"actor '{actor.ActorId}'"),
            cancellationToken);
    }

    internal void ObserveActorAuthority(
        ZLinkBackendActorRef actor,
        ulong targetNodeGeneration,
        ulong authorityOwnerGeneration,
        ulong ownerLeaseGeneration)
    {
        if (Node is not IZLinkBackendAuthorityObserver observer)
            throw new InvalidOperationException(
                "The MeshNode backend does not support authority fencing.");
        observer.ObserveActorAuthority(
            actor,
            targetNodeGeneration,
            authorityOwnerGeneration,
            ownerLeaseGeneration);
    }

    internal ValueTask<IReadOnlyList<Message>> RequestToNodeAsync(
        RoutingId targetNodeRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return _nodeSubmitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            parts,
            (pending, complete, fail) => Node.RequestToNode(
                targetNodeRid,
                pending,
                (result, reply) =>
                {
                    if (result == RequestResult.Ok)
                    {
                        complete(reply);
                        return;
                    }

                    fail(ZLinkRequestFailureMapper.CreateCompletionException(
                        result,
                        $"Node request to '{targetNodeRid}' failed with result '{result}'."));
                    ZLinkMessageParts.DisposeAll(reply);
                },
                SendFlags.DontWait,
                timeout,
                metadata),
            cancellationToken,
            ZLinkMessageParts.DisposeAll);
    }

    internal void RequestStop()
    {
        lock (_disposeGate)
        {
            if (_stopSourceDisposed) return;
            _stopSource.Cancel();
        }
        _spots.RequestStop();
        _entryDispatchPump?.RequestStop();
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

    public ValueTask DisposeAsync()
    {
        lock (_disposeGate)
            return new ValueTask(_disposeTask ??= DisposeCoreAsync());
    }

    private async Task DisposeCoreAsync()
    {
        var failures = new List<Exception>();
        await CaptureAsync(CloseLifecycleAsync).ConfigureAwait(false);
        Capture(RequestStop);
        if (_entryDispatchPump is { } entryDispatchPump)
            await CaptureAsync(entryDispatchPump.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_taskRunner.StopAsync).ConfigureAwait(false);
        await CaptureAsync(_spots.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_bundles.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(DisposeEntrySpotAsync).ConfigureAwait(false);
        await CaptureAsync(_nodeSubmitter.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(Node.DisposeAsync).ConfigureAwait(false);
        Capture(() =>
        {
            lock (_disposeGate)
            {
                if (_stopSourceDisposed) return;
                _stopSource.Dispose();
                _stopSourceDisposed = true;
            }
        });
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

    public void ApplyEntrySpotIdBeforeBind()
    {
        if (string.IsNullOrEmpty(EntrySpotId)) return;

        _entrySpot = Node.EntrySpot();
        _entrySpot.SetRoutingId(
            ZLinkSpotId.ToNativeRoutingId(EntrySpotId));
    }

    public async ValueTask InitializeEntrySpotAsync()
    {
        WireNodeRouteDispatch();
        if (_instanceSpotActivationTarget is not null)
            await _instanceSpotActivationTarget.RecoverAsync(_stopSource.Token)
                .ConfigureAwait(false);
        _entrySpot ??= Node.EntrySpot();
        _entryOutbound ??= new ZLinkSpotOutboundTransport(
            _entrySpot,
            Registration.Router?.SocketConfig.SendTimeout
            ?? _frameworkRegistration.DefaultSocketSendTimeout,
            _stopSource.Token);
        if (Registration.EntrySpotType is null)
        {
            if (ShouldAttachActorDispatchPump())
            {
                _entryDispatchPump = new ZLinkEntrySpotDispatchPump(_runtime, null, _taskRunner);
                _entryDispatchPump.Attach(_entrySpot);
                if (Interlocked.Exchange(ref _entrySpotMetricActive, 1) == 0)
                    ZLinkRuntimeMetrics.RecordSpotCreated("entry");
            }

            return;
        }

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

        _entryDispatchPump = new ZLinkEntrySpotDispatchPump(_runtime, activation, _taskRunner);
        _entryDispatchPump.Attach(entrySpot);
        if (Interlocked.Exchange(ref _entrySpotMetricActive, 1) == 0)
            ZLinkRuntimeMetrics.RecordSpotCreated("entry");
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot()
    {
        return _monitoringSnapshots.MonitorStatus();
    }

    internal IReadOnlyList<ZLinkInstanceSpotTypeSnapshot>
        GetInstanceSpotMonitoringSnapshots()
    {
        if (Registration.InstanceSpotFactories.Count == 0)
            return Array.Empty<ZLinkInstanceSpotTypeSnapshot>();

        var activationTarget = _instanceSpotActivationTarget
            ?? throw new InvalidOperationException(
                $"MeshNode '{Name}' has Instance Spot factories without an activation target.");
        return Registration.InstanceSpotFactories.Keys
            .Order(StringComparer.Ordinal)
            .Select(stableType =>
            {
                var catalog = _spots.InstanceSpotSnapshot(stableType);
                var operations = activationTarget.MonitoringSnapshot(stableType);
                return new ZLinkInstanceSpotTypeSnapshot(
                    stableType,
                    catalog.ActiveCount,
                    catalog.ActivatingCount,
                    catalog.ClosingCount,
                    operations.PendingMessageCount,
                    operations.PendingByteCount,
                    operations.LastActivationOutcome);
            })
            .ToArray();
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
        string requestedSpotId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return await _spots.GetOrCreateAsync(
            spotType,
            requestedSpotId,
            request,
            cancellationToken);
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        string spotId,
        CancellationToken cancellationToken)
    {
        return _spots.GetAsync(spotId, cancellationToken);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        return _spots.ListAsync(cancellationToken);
    }

    public async ValueTask<bool> CloseAsync(
        string spotId,
        CancellationToken cancellationToken)
    {
        return await _spots.CloseAsync(spotId, cancellationToken);
    }

    internal ValueTask<bool> TryDrainSpotsAsync(
        bool relocate,
        CancellationToken cancellationToken) =>
        relocate
            ? _spots.TryRelocateForRetireAsync(cancellationToken)
            : _spots.TryDrainAsync(cancellationToken);

    public ValueTask<bool> ConnectPeerAsync(string endpoint, CancellationToken cancellationToken)
    {
        return _peerConnector.ConnectPeerAsync(endpoint, cancellationToken);
    }

    public ValueTask<bool> ConnectPeerAsync(
        RoutingId peerRid,
        string endpoint,
        CancellationToken cancellationToken)
    {
        _peerConnections.RetainManualPeerRid(endpoint, peerRid);
        return _peerConnector.ConnectPeerAsync(peerRid, endpoint, cancellationToken);
    }

    public void DisconnectPeer(string endpoint)
    {
        _peerConnector.Disconnect(endpoint);
    }

    public void DisconnectPeerManual(string endpoint)
        => _peerConnector.DisconnectPeerManual(endpoint);

    public void DisconnectPeerManual(string endpoint, RoutingId peerRid)
    {
        _peerConnections.RetainManualPeerRid(endpoint, peerRid);
        _peerConnector.DisconnectPeerManual(endpoint);
    }

    public bool ConnectPeerAuto(RoutingId? peerRid, string endpoint)
        => _peerConnector.ConnectPeerAuto(peerRid, endpoint);

    public void DisconnectPeerLifetime(RoutingId peerRid, ulong lifecycleGeneration)
        => Node.DisconnectPeerLifetime(peerRid, lifecycleGeneration);

    public bool DisconnectPeerAuto(string endpoint)
        => _peerConnector.DisconnectPeerAuto(endpoint);

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
                EntrySpotId,
                Registration.EntrySpotType,
                Node.RoutingId,
                Registration.SpotNodeName,
                Registration.SpotMeshChannelName ?? Registration.SpotNodeName,
                _frameworkRegistration.DefaultRequestTimeout,
                EntryOutbound);
            activation.InitializeRuntimeResources();
            foreach (var handler in _frameworkRegistration.ScannedHandlerCatalog.SpotHandlers)
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

    // Wires inbound node-route (NodeSend/NodeRequest) and channel-membership
    // (ChannelSend/ChannelRequest) dispatch to the MeshNode builder's registered
    // handlers. Idempotent; a node with no such handlers registers no sink and its
    // node/channel records are released by the pump.
    private void WireNodeRouteDispatch()
    {
        if (_nodeRouteDispatcher is not null) return;

        _nodeRouteDispatcher = ZLinkMeshNodeRouteDispatcher.Create(
            _services,
            _frameworkRegistration,
            Registration,
            _runtime,
            _taskRunner);
        if (_nodeRouteDispatcher is not null)
            Node.OnNodeRoute(_nodeRouteDispatcher.Dispatch);
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

        if (_entryOutbound is { } outbound)
        {
            await CaptureAsync(outbound.DisposeAsync).ConfigureAwait(false);
            _entryOutbound = null;
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
