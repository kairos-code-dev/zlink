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
        ZLinkLocationLifecycle? locationLifecycle)
    {
        _services = services;
        _runtime = runtime;
        _frameworkRegistration = frameworkRegistration;
        Registration = registration;
        Node = node;
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
    }

    public string Name => Registration.SpotNodeName;

    public IReadOnlySet<Type> SpotFactories => Registration.SpotFactories;

    public IZLinkBackendSpotNode Node { get; }

    internal ZLinkSpotNodeRegistration Registration { get; }

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

    internal ValueTask<ZLinkSubmitResult> SendToNodeAsync(
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

    private ValueTask<ZLinkSubmitResult> SubmitToLocalNodeAsync(
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata)
    {
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_stopSource.IsCancellationRequested)
                return ValueTask.FromResult(new ZLinkSubmitResult(ZLinkSubmitStatus.Shutdown));
            if (_nodeRouteDispatcher is null)
                return ValueTask.FromResult(new ZLinkSubmitResult(ZLinkSubmitStatus.TargetNotFound));
            if (!ZLinkMeshMetadataCodec.TryDecode(metadata.Span, out var decodedMetadata))
                throw new ArgumentException("Application metadata is malformed.", nameof(metadata));

            var received = new ZLinkBackendRouteReceived(
                parts,
                Node.RoutingId,
                spotRid: null,
                requestSeq: null,
                reply: null,
                metadata: decodedMetadata);
            if (_nodeRouteDispatcher.TryDispatch(received))
                return ValueTask.FromResult(new ZLinkSubmitResult(ZLinkSubmitStatus.Submitted));

            received.Dispose();
            return ValueTask.FromResult(new ZLinkSubmitResult(ZLinkSubmitStatus.Shutdown));
        }
        catch
        {
            ZLinkMessageParts.DisposeAll(parts);
            throw;
        }
    }

    internal ValueTask<ZLinkSubmitResult> SendToActorAsync(
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

    public void ApplyEntrySpotRoutingIdBeforeBind()
    {
        if (Registration.EntrySpotOptions.RoutingId.Size == 0) return;

        _entrySpot = Node.EntrySpot();
        _entrySpot.SetRoutingId(Registration.EntrySpotOptions.RoutingId);
    }

    public async ValueTask InitializeEntrySpotAsync()
    {
        WireNodeRouteDispatch();
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
        _spots.TryDrainAsync(cancellationToken);

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
