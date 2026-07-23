using System.Collections.Concurrent;
using System.Buffers.Binary;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using Systems.Zlink.Framework.Runtime.Protocol;

namespace Zlink.Framework.Runtime.Service;

internal sealed class ZLinkManagedMeshNode : IMeshNode
{
    private const int ReceiveBatchSize = 64;
    private const int DefaultMaxPendingOperations = 65_536;
    private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(100);
    private static readonly TimeSpan AdmissionRetryInterval = TimeSpan.FromMilliseconds(500);

    private readonly IContext _context;
    private readonly string _meshName;
    private readonly int _maxPendingOperations;
    private readonly object _gate = new();
    private readonly object _socketGate = new();
    private readonly object _readyGate = new();
    private readonly object _operationGate = new();
    private readonly Dictionary<string, uint> _channels = new(StringComparer.Ordinal);
    private readonly Dictionary<ulong, Peer> _peersByIntent = new();
    private readonly Dictionary<RoutingId, Peer> _peersByRid = new();
    private readonly ConcurrentDictionary<MailboxKey, OwnedMailbox> _ownedMailboxes = new();
    private readonly ConcurrentDictionary<ulong, PendingOperation> _operations = new();
    private readonly ConcurrentDictionary<RoutingId, ZLinkManagedSpot> _spots = new();
    private readonly ConcurrentDictionary<string, ManagedActor> _actors =
        new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<ActorTransferToken, ManagedTransfer> _transfers = new();
    private readonly ConcurrentDictionary<ObservedSpotAuthorityKey, ulong>
        _observedSpotAuthorities = new();
    private readonly ConcurrentDictionary<ObservedActorAuthorityKey, ulong>
        _observedActorAuthorities = new();
    private readonly List<RawMeshMonitor> _monitors = new();
    private readonly ulong _lifecycleGeneration = NewNonZeroToken();

    private IRouterSocket? _socket;
    private IPoller? _poller;
    private CancellationTokenSource? _stop;
    private Task? _receiveLoop;
    private Func<MeshReadyDomains, MeshReadyDomains>? _readyHandler;
    private RoutingId _routingId;
    private string _bindEndpoint = string.Empty;
    private MeshNodeState _state = MeshNodeState.Created;
    private ulong _descriptorRevision = 1;
    private ulong _nextIntent;
    private ulong _nextOperation;
    private ulong _nextActorGeneration;
    private ulong _nextSpotGeneration;
    private ulong _nextAuthorityOwnerGeneration;
    private long _nextChannelSelection;
    private long _queuedMessages;
    private long _queuedBytes;
    private int _readyPosted;
    private int _disposed;

    internal ZLinkManagedMeshNode(
        IContext context,
        string meshName,
        int maxPendingOperations = DefaultMaxPendingOperations)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        if (maxPendingOperations <= 0)
            throw new ArgumentOutOfRangeException(nameof(maxPendingOperations));
        _meshName = meshName;
        _maxPendingOperations = maxPendingOperations;
    }

    public RoutingId RoutingId => _routingId;
    public long MaxMessageSize { get; set; } = -1;
    public int RouterHighWaterMark { get; set; } = 1_000;
    public ulong MailboxMessageBudget { get; set; } = 10_000;
    public ulong MailboxByteBudget { get; set; } = 64 * 1024 * 1024;
    public TimeSpan? SendTimeout { get; set; }

    public void SetRoutingId(RoutingId routingId)
    {
        ThrowIfStarted();
        if (routingId.IsEmpty)
            throw new ArgumentException("Routing id is required.", nameof(routingId));
        _routingId = routingId;
    }

    public void SetBind(string endpoint)
    {
        ThrowIfStarted();
        ArgumentException.ThrowIfNullOrWhiteSpace(endpoint);
        _bindEndpoint = endpoint;
    }

    public void Start()
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_state != MeshNodeState.Created)
                return;
            if (_routingId.IsEmpty)
                throw new InvalidOperationException("A MeshNode routing id is required before Start.");
            if (_bindEndpoint.Length == 0)
                _bindEndpoint = $"inproc://zlink-framework-{Guid.NewGuid():N}";

            var socket = _context.CreateRouterSocket();
            try
            {
                socket.Options.Mandatory = true;
                socket.Options.Handover = true;
                socket.Options.Linger = TimeSpan.Zero;
                socket.Options.MaxMessageSize = MaxMessageSize;
                socket.Options.SendHighWaterMark = RouterHighWaterMark;
                socket.Options.ReceiveHighWaterMark = RouterHighWaterMark;
                if (SendTimeout is { } timeout)
                    socket.Options.SendTimeout = timeout;
                socket.SetRoutingId(_routingId);
                socket.OnSendReady(EnqueueSendReady);
                socket.Bind(_bindEndpoint);

                var poller = Systems.Zlink.Zlink.CreatePoller();
                poller.Add(socket, PollEventFlags.PollIn, 1);
                _socket = socket;
                _poller = poller;
                _stop = new CancellationTokenSource();
                _state = MeshNodeState.Started;
                Publish(MeshMonitorEventKind.StateChanged);
                foreach (var peer in _peersByIntent.Values)
                    ConnectPeerCore(peer);
                _receiveLoop = Task.Factory.StartNew(
                        () => ReceiveLoop(_stop.Token),
                        CancellationToken.None,
                        TaskCreationOptions.LongRunning,
                        TaskScheduler.Default)
                    .Unwrap();
            }
            catch
            {
                socket.Dispose();
                _socket = null;
                _poller?.Dispose();
                _poller = null;
                _state = MeshNodeState.Error;
                throw;
            }
        }
    }

    public ulong ConnectPeer(string endpoint, RoutingId? expectedRid = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(endpoint);
        lock (_gate)
        {
            ThrowIfDisposed();
            var intent = checked(++_nextIntent);
            var peer = new Peer(intent, endpoint, expectedRid);
            _peersByIntent.Add(intent, peer);
            if (_state != MeshNodeState.Created)
                ConnectPeerCore(peer);
            return intent;
        }
    }

    public void RemovePeerConnection(ulong connectionIntentId)
    {
        lock (_gate)
        {
            if (!_peersByIntent.Remove(connectionIntentId, out var peer))
                return;
            RemovePeer(peer, disconnect: true);
        }
    }

    public void DisconnectPeer(RoutingId peerRid, ulong lifecycleGeneration = 0)
    {
        lock (_gate)
        {
            if (!_peersByRid.TryGetValue(peerRid, out var peer))
                return;
            if (lifecycleGeneration != 0
                && lifecycleGeneration != peer.LifecycleGeneration)
                return;
            RemovePeer(peer, disconnect: true);
        }
    }

    public void AddChannel(string channelName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        if (Encoding.UTF8.GetByteCount(channelName) > byte.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(channelName));
        Peer[] peers;
        lock (_gate)
        {
            ThrowIfDisposed();
            if (!_channels.TryAdd(channelName, 100))
                return;
            _descriptorRevision = checked(_descriptorRevision + 1);
            peers = _peersByRid.Values
                .Where(static peer => peer.Admitted)
                .ToArray();
        }
        foreach (var peer in peers)
            SendAdmission(peer, ServiceWireConstants.Command.Update);
        Publish(MeshMonitorEventKind.ChannelChanged, channelName: channelName);
    }

    public void SetChannelWeight(string channelName, uint weight)
    {
        if (weight > 100)
            throw new ArgumentOutOfRangeException(nameof(weight));
        Peer[] peers;
        lock (_gate)
        {
            if (!_channels.ContainsKey(channelName))
                throw new InvalidOperationException($"Channel '{channelName}' is not registered.");
            _channels[channelName] = weight;
            _descriptorRevision = checked(_descriptorRevision + 1);
            peers = _peersByRid.Values
                .Where(static peer => peer.Admitted)
                .ToArray();
        }
        foreach (var peer in peers)
            SendAdmission(peer, ServiceWireConstants.Command.Update);
        Publish(MeshMonitorEventKind.ChannelChanged, channelName: channelName);
    }

    public MeshNodeStatus Status()
    {
        lock (_gate)
        {
            var admitted = _peersByRid.Values.Count(static peer => peer.Admitted);
            var draining = _peersByRid.Values.Count(
                static peer => peer.State == MeshPeerState.Draining);
            var pendingApplication = _ownedMailboxes
                .Where(static entry =>
                    entry.Key.Domain == MeshReadyDomains.Application)
                .Sum(static entry => entry.Value.Count);
            var pendingInfrastructure = _ownedMailboxes
                .Where(static entry =>
                    entry.Key.Domain == MeshReadyDomains.Infrastructure)
                .Sum(static entry => entry.Value.Count);
            return new MeshNodeStatus(
                _state,
                _routingId,
                _meshName,
                _bindEndpoint,
                _lifecycleGeneration,
                _descriptorRevision,
                checked((uint)_channels.Count),
                checked((uint)_peersByIntent.Count),
                checked((uint)admitted),
                checked((uint)draining),
                checked((ulong)pendingApplication),
                checked((ulong)pendingInfrastructure),
                checked((ulong)Volatile.Read(ref _queuedBytes)),
                0,
                0,
                0,
                checked((ulong)Environment.TickCount64));
        }
    }

    public MeshNodePeer[] Peers()
    {
        lock (_gate)
            return _peersByIntent.Values
                .Select(static peer => peer.Snapshot())
                .ToArray();
    }

    public MeshPeerChannel[] PeerChannels(
        RoutingId peerRid,
        ulong lifecycleGeneration)
    {
        lock (_gate)
        {
            if (!_peersByRid.TryGetValue(peerRid, out var peer)
                || peer.LifecycleGeneration != lifecycleGeneration)
                return Array.Empty<MeshPeerChannel>();
            return peer.Channels.Select(static channel =>
                    new MeshPeerChannel(channel.Key, channel.Value))
                .ToArray();
        }
    }

    public IMeshNodeMonitor OpenMonitor(
        MeshMonitorEventMask events = MeshMonitorEventMask.All)
    {
        var monitor = new RawMeshMonitor(events);
        lock (_gate)
            _monitors.Add(monitor);
        return monitor;
    }

    public void SetReadyHandler(Func<MeshReadyDomains, MeshReadyDomains> handler)
    {
        _readyHandler = handler ?? throw new ArgumentNullException(nameof(handler));
        SignalReadyIfNeeded();
    }

    public bool DrainReady(
        MeshReadyDomains domains,
        MeshReadyBatch batch,
        RecvFlags flags = RecvFlags.None)
    {
        ArgumentNullException.ThrowIfNull(batch);
        if ((domains & MeshReadyDomains.All) == 0)
            return false;

        lock (_readyGate)
        {
            Volatile.Write(ref _readyPosted, 0);
            foreach (var entry in _ownedMailboxes
                         .Where(entry => (entry.Key.Domain & domains) != 0)
                         .OrderBy(static entry =>
                             entry.Key.Domain == MeshReadyDomains.Infrastructure ? 0 : 1)
                         .ThenBy(static entry => entry.Key.OwnerKind)
                         .ThenBy(static entry => entry.Key.Identity, StringComparer.Ordinal))
            {
                if (!entry.Value.TryClaim())
                    continue;
                var mailbox = entry.Value;
                batch.Add(
                    new MeshReadyRecord(
                        entry.Key.OwnerKind,
                        entry.Key.Domain,
                        entry.Key.SpotRid,
                        entry.Key.Actor),
                    new MeshClaim
                    {
                        Receiver = (receiveBatch, receiveFlags) =>
                            DrainOwnedQueue(mailbox, receiveBatch, receiveFlags),
                        Releaser = () => ReleaseOwnedMailbox(mailbox)
                    });
            }
            return false;
        }
    }

    public ISpot CreateSpot()
    {
        var rid = RoutingId.From(Guid.NewGuid());
        return _spots.GetOrAdd(
            rid,
            value => new ZLinkManagedSpot(
                this,
                value,
                Interlocked.Increment(ref _nextSpotGeneration),
                NextAuthorityOwnerGeneration()));
    }

    public ISpot EntrySpot()
    {
        if (_routingId.IsEmpty)
            throw new InvalidOperationException(
                "The MeshNode routing id must be configured before its Entry Spot is used.");
        return _spots.GetOrAdd(
            _routingId,
            value => new ZLinkManagedSpot(
                this,
                value,
                Interlocked.Increment(ref _nextSpotGeneration),
                NextAuthorityOwnerGeneration()));
    }

    public ISpot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        if (spotRid.IsEmpty)
            throw new ArgumentException("Spot routing id is required.", nameof(spotRid));
        if (_spots.TryGetValue(spotRid, out var existing))
        {
            created = false;
            return existing;
        }

        var candidate = new ZLinkManagedSpot(
            this,
            spotRid,
            Interlocked.Increment(ref _nextSpotGeneration),
            NextAuthorityOwnerGeneration());
        var spot = _spots.GetOrAdd(spotRid, candidate);
        created = ReferenceEquals(spot, candidate);
        if (!created)
            candidate.Dispose();
        return spot;
    }

    public ActorRef CreateActor(
        string actorId,
        IReadOnlyList<Message>? creationParts = null,
        TimeSpan timeout = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        if (Encoding.UTF8.GetByteCount(actorId) > byte.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(actorId));
        ThrowIfDisposed();

        var entry = (ZLinkManagedSpot)EntrySpot();
        var generation = Interlocked.Increment(ref _nextActorGeneration);
        if (generation == 0 || generation > long.MaxValue)
            throw new InvalidOperationException("The Actor generation space was exhausted.");
        var actorRef = new ActorRef(_routingId, actorId, generation);
        var actor = new ManagedActor(
            actorRef,
            entry.RoutingId,
            entry.LifecycleGeneration,
            membershipEpoch: 1,
            NextAuthorityOwnerGeneration());
        if (!_actors.TryAdd(actorId, actor))
            throw new InvalidOperationException($"Actor '{actorId}' already exists.");
        entry.AddActor();
        EnqueueActorLifecycle(
            entry,
            ActorLifecycleKind.Created,
            actor,
            actor,
            creationParts ?? Array.Empty<Message>());
        return actorRef;
    }

    public bool ActorLookup(string actorId, out ActorLocation location)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        if (_actors.TryGetValue(actorId, out var actor)
            && !actor.Draining)
        {
            location = actor.Location;
            return true;
        }
        location = default!;
        return false;
    }

    internal void ObserveSpotAuthority(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong objectGeneration,
        ulong authorityOwnerGeneration)
    {
        ValidateObservedAuthority(
            targetNodeRid,
            objectGeneration,
            authorityOwnerGeneration);
        if (targetSpotRid.IsEmpty)
            throw new ArgumentException(
                "The observed Spot routing id is required.",
                nameof(targetSpotRid));
        _observedSpotAuthorities[
            new ObservedSpotAuthorityKey(
                targetNodeRid,
                targetSpotRid,
                objectGeneration)] = authorityOwnerGeneration;
    }

    internal void ObserveActorAuthority(
        ActorRef actor,
        ulong authorityOwnerGeneration)
    {
        ValidateObservedAuthority(
            actor.NodeRid,
            actor.Generation,
            authorityOwnerGeneration);
        ArgumentException.ThrowIfNullOrWhiteSpace(actor.ActorId);
        _observedActorAuthorities[
            new ObservedActorAuthorityKey(
                actor.NodeRid,
                actor.ActorId,
                actor.Generation)] = authorityOwnerGeneration;
    }

    internal bool TryGetActorAuthority(
        ActorRef actor,
        out ulong authorityOwnerGeneration)
    {
        if (TryGetActor(actor, out var current))
        {
            authorityOwnerGeneration = current.AuthorityOwnerGeneration;
            return true;
        }
        authorityOwnerGeneration = 0;
        return false;
    }

    public MeshOperationId DestroyActor(ActorRef actor, TimeSpan timeout = default)
    {
        var operation = BeginOperation(MeshOperationKind.ActorDestroy, timeout);
        if (!_actors.TryGetValue(actor.ActorId, out var current)
            || current.Ref.Generation != actor.Generation
            || current.Ref.NodeRid != actor.NodeRid
            || !current.TryDrain())
        {
            CompleteManagedOperation(
                operation,
                RequestResult.Conflict,
                current is null ? 2 : 1,
                Array.Empty<Message>());
            return operation.OperationId;
        }

        _actors.TryRemove(new KeyValuePair<string, ManagedActor>(actor.ActorId, current));
        if (_spots.TryGetValue(current.SpotRid, out var spot))
        {
            spot.RemoveActor();
            EnqueueActorLifecycle(
                spot,
                ActorLifecycleKind.Destroyed,
                current,
                current,
                Array.Empty<Message>());
        }
        CompleteManagedOperation(operation, RequestResult.Ok, 0, Array.Empty<Message>());
        return operation.OperationId;
    }

    public MeshOperationId JoinSpot(
        ActorRef actor,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message>? creationParts = null,
        TimeSpan timeout = default) =>
        BeginJoin(
            actor,
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            entry: false,
            creationParts,
            timeout);

    public MeshOperationId JoinEntrySpot(
        ActorRef actor,
        RoutingId targetNodeRid,
        IReadOnlyList<Message>? creationParts = null,
        TimeSpan timeout = default) =>
        BeginJoin(
            actor,
            targetNodeRid,
            targetNodeRid == _routingId
                ? EntrySpot().RoutingId
                : targetNodeRid,
            0,
            entry: true,
            creationParts,
            timeout);

    public SubmitResult SendToNode(
        RoutingId targetRid,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        SubmitApplication(
            targetRid,
            ServiceWireConstants.Command.NodeSend,
            0,
            null,
            parts,
            flags,
            metadata);

    public SubmitResult RequestToNode(
        RoutingId targetRid,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        SubmitRequest(
            targetRid,
            ServiceWireConstants.Command.NodeRequest,
            null,
            parts,
            timeout,
            flags,
            metadata,
            out operationId);

    internal SubmitResult SendToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        if (!TrySelectChannelTarget(channelName, out var targetRid))
            return SubmitResult.NotConnected;
        return SubmitApplication(
            targetRid,
            ServiceWireConstants.Command.ChannelSend,
            0,
            channelName,
            parts,
            flags,
            metadata);
    }

    internal SubmitResult RequestToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        if (!TrySelectChannelTarget(channelName, out var targetRid))
        {
            operationId = default;
            return SubmitResult.NotConnected;
        }
        return SubmitRequest(
            targetRid,
            ServiceWireConstants.Command.ChannelRequest,
            channelName,
            parts,
            timeout,
            flags,
            metadata,
            out operationId);
    }

    internal MeshPublishResult Publish(
        RoutingId sourceSpotRid,
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        var localTargets = _spots.Values
            .Where(spot => spot.Matches(channelName, topic))
            .OrderBy(static spot => spot.RoutingId.ToHex(), StringComparer.Ordinal)
            .ToArray();
        ulong localAdmitted = 0;
        ulong localDropped = 0;
        foreach (var spot in localTargets)
        {
            var retained = CloneParts(parts);
            var before = Volatile.Read(ref _queuedMessages);
            EnqueueOwned(
                MailboxKey.ForSpot(spot, MeshReadyDomains.Application),
                new MeshReceiveRecord(
                    MeshRecordKind.SpotMulticast,
                    MeshReadyDomains.Application,
                    _routingId,
                    sourceSpotRid,
                    _lifecycleGeneration,
                    default,
                    default,
                    default,
                    channelName,
                    topic,
                    metadata.IsEmpty ? null : metadata.ToArray(),
                    0,
                    retained.Count,
                    0,
                    0,
                    null),
                retained);
            if (Volatile.Read(ref _queuedMessages) > before)
                localAdmitted++;
            else
                localDropped++;
        }

        var targets = SnapshotChannelTargets(channelName)
            .Where(target => target != _routingId)
            .ToList();
        ulong admitted = 0;
        ulong dropped = 0;
        foreach (var target in targets)
        {
            var result = SubmitApplication(
                target,
                ServiceWireConstants.Command.ChannelSend,
                0,
                channelName,
                parts,
                flags,
                metadata);
            if (result == SubmitResult.Ok) admitted++;
            else dropped++;
        }
        var submit = admitted > 0 || localAdmitted > 0
            ? SubmitResult.Ok
            : dropped > 0 || localDropped > 0
                ? SubmitResult.Backpressured
                : SubmitResult.NotFound;
        return new MeshPublishResult(
            submit,
            new MeshPublishDetail(
                checked((ulong)targets.Count),
                admitted,
                dropped,
                dropped,
                checked((ulong)localTargets.Length),
                localAdmitted,
                localDropped));
    }

    public SubmitResult SendToActor(
        ActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None) =>
        SubmitActor(actor, parts, request: false, default, flags, out _);

    public SubmitResult RequestToActor(
        ActorRef actor,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout = default)
    {
        if (!TryBeginOperation(
                MeshOperationKind.ActorRequest,
                timeout,
                out var operation))
        {
            operationId = default;
            Publish(MeshMonitorEventKind.Backpressured);
            return SubmitResult.Backpressured;
        }
        operationId = operation.OperationId;
        var result = SubmitActor(
            actor,
            parts,
            request: true,
            operation,
            SendFlags.None,
            out _);
        if (result != SubmitResult.Ok)
        {
            RemoveManagedOperation(operation);
            operationId = default;
        }
        return result;
    }

    public SubmitResult SendBoundSession(
        ActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        if (!TryGetActor(actor, out var current))
            return SubmitResult.NotFound;
        var binding = current.Binding;
        if (binding is null)
            return SubmitResult.NotConnected;
        return binding.Service.SendToSession(binding.SessionRid, parts, flags);
    }

    public MeshOperationId CloseBoundSession(
        ActorRef actor,
        ulong expectedBindingGeneration,
        TimeSpan timeout = default)
    {
        var operation = BeginOperation(MeshOperationKind.StreamUnbind, timeout);
        if (!TryGetActor(actor, out var current)
            || !current.TryClearBinding(expectedBindingGeneration))
        {
            CompleteManagedOperation(
                operation,
                RequestResult.NotFound,
                0,
                Array.Empty<Message>());
            return operation.OperationId;
        }
        CompleteManagedOperation(operation, RequestResult.Ok, 0, Array.Empty<Message>());
        return operation.OperationId;
    }

    public ActorTransferToken PrepareActorTransfer(
        ActorTransferPrepare prepare,
        out ActorTransferPrepareResult result,
        TimeSpan timeout = default)
    {
        if (!TryGetActor(prepare.Actor, out var actor)
            || actor.MembershipEpoch != prepare.ExpectedMembershipEpoch
            || !actor.TrySeal())
        {
            result = default;
            throw new InvalidOperationException(
                "The Actor transfer fence is stale or already active.");
        }
        var token = new ActorTransferToken(Guid.NewGuid());
        var transfer = new ManagedTransfer(token, prepare, actor);
        if (!_transfers.TryAdd(token, transfer))
            throw new InvalidOperationException("The Actor transfer token collided.");
        result = new ActorTransferPrepareResult(
            prepare.Role,
            prepare.TransferId,
            prepare.Actor,
            prepare.FinalSequence,
            prepare.ReserveMessageCount,
            prepare.ReserveByteCount);
        EnqueueTransferControl(transfer, ActorTransferPhase.Preparing, 0);
        return token;
    }

    public void CommitActorTransfer(
        ActorTransferToken token,
        ulong newMembershipEpoch)
    {
        var transfer = RequireTransfer(token);
        transfer.Commit(newMembershipEpoch);
        EnqueueTransferControl(transfer, ActorTransferPhase.Committed, 0);
    }

    public void ActivateActorTransfer(ActorTransferToken token)
    {
        var transfer = RequireTransfer(token);
        transfer.Activate();
        transfer.Actor.Unseal();
        _transfers.TryRemove(token, out _);
        EnqueueTransferControl(transfer, ActorTransferPhase.Activated, 0);
    }

    public void AbortActorTransfer(ActorTransferToken token)
    {
        var transfer = RequireTransfer(token);
        transfer.Abort();
        transfer.Actor.Unseal();
        _transfers.TryRemove(token, out _);
        EnqueueTransferControl(transfer, ActorTransferPhase.Aborted, 0);
    }

    public IStreamSessionService CreateStreamSessionService(IStreamSocket stream) =>
        new ZLinkManagedStreamSessionService(this, stream);

    public void Dispose()
    {
        DisposeAsync().AsTask().GetAwaiter().GetResult();
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
            return;

        Task? receiveLoop;
        lock (_gate)
        {
            _state = MeshNodeState.Stopped;
            _stop?.Cancel();
            receiveLoop = _receiveLoop;
        }
        if (receiveLoop is not null)
            try
            {
                await receiveLoop.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }

        PendingOperation[] pendingOperations;
        lock (_operationGate)
        {
            pendingOperations = _operations.Values.ToArray();
            _operations.Clear();
        }
        foreach (var pending in pendingOperations)
            pending.Cancel();
        foreach (var mailbox in _ownedMailboxes.Values)
            mailbox.Dispose();
        _ownedMailboxes.Clear();
        foreach (var spot in _spots.Values)
            await spot.DisposeAsync().ConfigureAwait(false);
        _spots.Clear();

        _poller?.Dispose();
        _socket?.Dispose();
        _stop?.Dispose();
        Publish(MeshMonitorEventKind.StateChanged);
        lock (_gate)
        {
            foreach (var monitor in _monitors)
                monitor.Dispose();
            _monitors.Clear();
        }
    }

    internal SubmitResult SendToSpot(
        RoutingId sourceSpotRid,
        RoutingId targetRid,
        RoutingId spotRid,
        ulong spotGeneration,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata) =>
        SubmitSpot(
            targetRid,
            sourceSpotRid,
            spotRid,
            spotGeneration,
            parts,
            request: false,
            default,
            flags,
            metadata);

    internal SubmitResult RequestToSpot(
        RoutingId sourceSpotRid,
        RoutingId targetRid,
        RoutingId spotRid,
        ulong spotGeneration,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        if (!TryBeginOperation(
                MeshOperationKind.SpotRequest,
                timeout,
                out var operation))
        {
            operationId = default;
            Publish(MeshMonitorEventKind.Backpressured);
            return SubmitResult.Backpressured;
        }
        operationId = operation.OperationId;
        var result = SubmitSpot(
            targetRid,
            sourceSpotRid,
            spotRid,
            spotGeneration,
            parts,
            request: true,
            operation,
            flags,
            metadata);
        if (result != SubmitResult.Ok)
        {
            RemoveManagedOperation(operation);
            operationId = default;
        }
        return result;
    }

    internal void ReleaseSpot(ZLinkManagedSpot spot)
    {
        if (spot.RoutingId == _routingId || spot.ActorCount != 0)
            return;
        _spots.TryRemove(
            new KeyValuePair<RoutingId, ZLinkManagedSpot>(
                spot.RoutingId,
                spot));
    }

    private MeshOperationId BeginJoin(
        ActorRef actorRef,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        bool entry,
        IReadOnlyList<Message>? requestParts,
        TimeSpan timeout)
    {
        var operation = BeginOperation(MeshOperationKind.ActorJoin, timeout);
        if (targetNodeRid != _routingId
            || !TryGetActor(actorRef, out var actor)
            || !_spots.TryGetValue(targetSpotRid, out var targetSpot)
            || (targetSpotGeneration != 0
                && targetSpot.LifecycleGeneration != targetSpotGeneration))
        {
            CompleteManagedOperation(
                operation,
                targetNodeRid == _routingId
                    ? RequestResult.NotFound
                    : RequestResult.NotConnected,
                0,
                Array.Empty<Message>());
            return operation.OperationId;
        }

        var previous = actor.Snapshot();
        var parts = CloneParts(requestParts ?? Array.Empty<Message>());
        var control = new ActorControlRecord(
            ActorLifecycleKind.Joined,
            previous.Ref,
            previous.Ref,
            previous.SpotRid,
            targetSpot.RoutingId,
            previous.SpotGeneration,
            targetSpot.LifecycleGeneration,
            previous.MembershipEpoch,
            checked(previous.MembershipEpoch + 1),
            0);
        var record = new MeshReceiveRecord(
            MeshRecordKind.SpotControl,
            MeshReadyDomains.Application,
            _routingId,
            previous.SpotRid,
            _lifecycleGeneration,
            previous.Ref,
            operation.OperationId,
            MeshOperationKind.ActorJoin,
            null,
            null,
            null,
            0,
            parts.Count,
            0,
            0,
            control,
            joinReply: (result, reply, _) =>
                CompleteJoin(
                    operation,
                    actor,
                    previous,
                    targetSpot,
                    result,
                    reply,
                    entry));
        EnqueueOwned(
            MailboxKey.ForSpot(targetSpot, MeshReadyDomains.Application),
            record,
            parts);
        return operation.OperationId;
    }

    internal SubmitResult BindSessionActor(
        ZLinkManagedStreamSessionService service,
        RoutingId sessionRid,
        ActorRef actorRef,
        out MeshOperationId operationId,
        TimeSpan timeout)
    {
        var operation = BeginOperation(MeshOperationKind.StreamBind, timeout);
        operationId = operation.OperationId;
        var found = TryGetActor(actorRef, out var actor);
        if (!found || actor.IsSealed)
        {
            CompleteManagedOperation(
                operation,
                found && actor.IsSealed
                    ? RequestResult.Busy
                    : RequestResult.NotFound,
                0,
                Array.Empty<Message>());
            return SubmitResult.Ok;
        }
        var bindingGeneration = actor.Bind(service, sessionRid);
        service.RecordBinding(
            sessionRid,
            new StreamSessionBinding(
                sessionRid,
                actor.Ref,
                bindingGeneration,
                actor.MembershipEpoch));
        CompleteManagedOperation(operation, RequestResult.Ok, 0, Array.Empty<Message>());
        return SubmitResult.Ok;
    }

    internal SubmitResult UnbindSessionActor(
        ZLinkManagedStreamSessionService service,
        RoutingId sessionRid,
        ActorRef actorRef,
        ulong expectedBindingGeneration,
        out MeshOperationId operationId,
        TimeSpan timeout)
    {
        var operation = BeginOperation(MeshOperationKind.StreamUnbind, timeout);
        operationId = operation.OperationId;
        if (!TryGetActor(actorRef, out var actor)
            || !actor.TryClearBinding(expectedBindingGeneration))
        {
            CompleteManagedOperation(
                operation,
                RequestResult.NotFound,
                0,
                Array.Empty<Message>());
            return SubmitResult.Ok;
        }
        service.RemoveBinding(
            sessionRid,
            actorRef.ActorId,
            expectedBindingGeneration);
        CompleteManagedOperation(operation, RequestResult.Ok, 0, Array.Empty<Message>());
        return SubmitResult.Ok;
    }

    internal SubmitResult RelaySessionToActor(
        ZLinkManagedStreamSessionService service,
        RoutingId sessionRid,
        ActorRef actorRef,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        if (!TryGetActor(actorRef, out var actor)
            || actor.Binding is not { } binding
            || !ReferenceEquals(binding.Service, service)
            || binding.SessionRid != sessionRid)
            return SubmitResult.NotFound;
        return SubmitActor(
            actorRef,
            parts,
            request: false,
            default,
            flags,
            out _);
    }

    private SubmitResult CompleteJoin(
        PendingOperation operation,
        ManagedActor actor,
        ActorSnapshot previous,
        ZLinkManagedSpot targetSpot,
        ActorJoinResult result,
        IReadOnlyList<Message> reply,
        bool entry)
    {
        if (!_operations.TryGetValue(operation.OperationId.Low, out var active)
            || !ReferenceEquals(active, operation))
            return SubmitResult.Terminated;

        if (result == ActorJoinResult.Rejected)
        {
            CompleteManagedOperation(
                operation,
                RequestResult.Rejected,
                0,
                CloneParts(reply),
                new ActorJoinCompletion(
                    result,
                    actor.Ref,
                    actor.Location));
            return SubmitResult.Ok;
        }

        if (!actor.TryMove(
                previous,
                targetSpot.RoutingId,
                targetSpot.LifecycleGeneration))
        {
            CompleteManagedOperation(
                operation,
                RequestResult.Conflict,
                1,
                Array.Empty<Message>());
            return SubmitResult.Ok;
        }

        if (_spots.TryGetValue(previous.SpotRid, out var oldSpot)
            && !ReferenceEquals(oldSpot, targetSpot))
        {
            oldSpot.RemoveActor();
            EnqueueActorLifecycle(
                oldSpot,
                ActorLifecycleKind.Left,
                previous,
                actor,
                Array.Empty<Message>());
        }
        if (previous.SpotRid != targetSpot.RoutingId)
            targetSpot.AddActor();
        EnqueueActorLifecycle(
            targetSpot,
            entry ? ActorLifecycleKind.Joined : ActorLifecycleKind.Joined,
            previous,
            actor,
            Array.Empty<Message>());
        CompleteManagedOperation(
            operation,
            RequestResult.Ok,
            0,
            CloneParts(reply),
            new ActorJoinCompletion(
                result,
                actor.Ref,
                actor.Location));
        return SubmitResult.Ok;
    }

    private SubmitResult SubmitSpot(
        RoutingId targetNodeRid,
        RoutingId sourceSpotRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        bool request,
        PendingOperation? operation,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        ArgumentNullException.ThrowIfNull(parts);
        if (parts.Count == 0)
            throw new ArgumentException("Application payload is required.", nameof(parts));
        if (targetNodeRid != _routingId)
        {
            Peer? peer;
            lock (_gate)
                _peersByRid.TryGetValue(targetNodeRid, out peer);
            if (peer is null || !peer.Admitted)
                return SubmitResult.NotConnected;
            if (targetSpotGeneration == 0
                || !_observedSpotAuthorities.TryGetValue(
                    new ObservedSpotAuthorityKey(
                        targetNodeRid,
                        targetSpotRid,
                        targetSpotGeneration),
                    out var authorityOwnerGeneration))
                return SubmitResult.NotFound;
            var head = ZLinkServiceWireCodec.EncodeSpot(
                request
                    ? ServiceWireConstants.Command.SpotRequest
                    : ServiceWireConstants.Command.SpotSend,
                operation?.OperationId.Low ?? 0,
                sourceSpotRid,
                targetSpotRid,
                targetSpotGeneration,
                targetNodeRid,
                peer.LifecycleGeneration,
                authorityOwnerGeneration,
                !metadata.IsEmpty);
            return SubmitStatefulWire(
                peer,
                head,
                parts,
                flags,
                metadata);
        }
        if (!_spots.TryGetValue(targetSpotRid, out var spot))
            return SubmitResult.NotFound;
        if (targetSpotGeneration != 0
            && spot.LifecycleGeneration != targetSpotGeneration)
            return SubmitResult.InvalidState;

        Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? reply = null;
        if (request && operation is not null)
            reply = (replyParts, _) =>
            {
                CompleteManagedOperation(
                    operation,
                    RequestResult.Ok,
                    0,
                    CloneParts(replyParts));
                return SubmitResult.Ok;
            };
        var retained = CloneParts(parts);
        var record = new MeshReceiveRecord(
            request ? MeshRecordKind.SpotRequest : MeshRecordKind.SpotSend,
            MeshReadyDomains.Application,
            _routingId,
            default,
            _lifecycleGeneration,
            default,
            operation?.OperationId ?? default,
            request ? MeshOperationKind.SpotRequest : default,
            null,
            null,
            metadata.IsEmpty ? null : metadata.ToArray(),
            0,
            retained.Count,
            0,
            0,
            null,
            reply);
        EnqueueOwned(
            MailboxKey.ForSpot(spot, MeshReadyDomains.Application),
            record,
            retained);
        return SubmitResult.Ok;
    }

    private SubmitResult SubmitActor(
        ActorRef actorRef,
        IReadOnlyList<Message> parts,
        bool request,
        PendingOperation? operation,
        SendFlags flags,
        out ulong acceptedSequence)
    {
        ArgumentNullException.ThrowIfNull(parts);
        if (parts.Count == 0)
            throw new ArgumentException("Application payload is required.", nameof(parts));
        acceptedSequence = 0;
        if (actorRef.NodeRid != _routingId)
        {
            Peer? peer;
            lock (_gate)
                _peersByRid.TryGetValue(actorRef.NodeRid, out peer);
            if (peer is null || !peer.Admitted)
                return SubmitResult.NotConnected;
            if (!_observedActorAuthorities.TryGetValue(
                    new ObservedActorAuthorityKey(
                        actorRef.NodeRid,
                        actorRef.ActorId,
                        actorRef.Generation),
                    out var authorityOwnerGeneration))
                return SubmitResult.NotFound;
            var head = ZLinkServiceWireCodec.EncodeActor(
                request
                    ? ServiceWireConstants.Command.ActorRequest
                    : ServiceWireConstants.Command.ActorSend,
                operation?.OperationId.Low ?? 0,
                actorRef,
                actorRef.NodeRid,
                peer.LifecycleGeneration,
                authorityOwnerGeneration,
                hasMetadata: false);
            return SubmitStatefulWire(
                peer,
                head,
                parts,
                flags,
                default);
        }
        if (!TryGetActor(actorRef, out var actor))
            return SubmitResult.NotFound;
        if (actor.IsSealed)
            return SubmitResult.Backpressured;

        acceptedSequence = actor.NextSequence();
        Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? reply = null;
        if (request && operation is not null)
            reply = (replyParts, _) =>
            {
                CompleteManagedOperation(
                    operation,
                    RequestResult.Ok,
                    0,
                    CloneParts(replyParts));
                return SubmitResult.Ok;
            };
        var retained = CloneParts(parts);
        var record = new MeshReceiveRecord(
            request ? MeshRecordKind.ActorRequest : MeshRecordKind.ActorSend,
            MeshReadyDomains.Application,
            _routingId,
            default,
            _lifecycleGeneration,
            default,
            operation?.OperationId ?? default,
            request ? MeshOperationKind.ActorRequest : default,
            null,
            null,
            null,
            0,
            retained.Count,
            0,
            0,
            null,
            reply);
        EnqueueOwned(
            MailboxKey.ForActor(actor, MeshReadyDomains.Application),
            record,
            retained);
        return SubmitResult.Ok;
    }

    private SubmitResult SubmitStatefulWire(
        Peer peer,
        byte[] head,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        var wireParts = new List<ReadOnlyMemory<byte>>(parts.Count + 2) { head };
        if (!metadata.IsEmpty)
            wireParts.Add(metadata);
        foreach (var part in parts)
            wireParts.Add(part.ToArray());
        if (!TrySend(peer.PhysicalRoutingId, wireParts, flags))
        {
            Publish(MeshMonitorEventKind.Backpressured, peerRid: peer.RoutingId);
            return SubmitResult.Backpressured;
        }
        Publish(MeshMonitorEventKind.MessageSubmitted, peerRid: peer.RoutingId);
        return SubmitResult.Ok;
    }

    private PendingOperation BeginOperation(
        MeshOperationKind kind,
        TimeSpan timeout)
    {
        if (!TryBeginOperation(kind, timeout, out var operation))
            throw new ZlinkSubmitException(
                ZlinkSubmitException.ErrorCode.Backpressured);
        return operation;
    }

    private bool TryBeginOperation(
        MeshOperationKind kind,
        TimeSpan timeout,
        out PendingOperation operation)
    {
        if (!TryCreateOperation(kind, out var correlation, out operation))
            return false;
        _ = ExpireOperationAsync(
            correlation,
            operation,
            timeout <= TimeSpan.Zero ? TimeSpan.FromSeconds(30) : timeout);
        return true;
    }

    private bool TryCreateOperation(
        MeshOperationKind kind,
        out ulong correlation,
        out PendingOperation operation)
    {
        lock (_operationGate)
        {
            if (_operations.Count >= _maxPendingOperations)
            {
                correlation = 0;
                operation = null!;
                return false;
            }
            correlation = ++_nextOperation;
            if (correlation == 0)
                throw new InvalidOperationException(
                    "The operation id space was exhausted.");
            operation = new PendingOperation(
                new MeshOperationId(_lifecycleGeneration, correlation),
                kind);
            if (!_operations.TryAdd(correlation, operation))
                throw new InvalidOperationException(
                    "The operation id was reused.");
            return true;
        }
    }

    private bool TryRemoveOperation(
        ulong correlation,
        out PendingOperation operation)
    {
        lock (_operationGate)
            return _operations.TryRemove(correlation, out operation!);
    }

    private bool TryRemoveOperation(
        KeyValuePair<ulong, PendingOperation> operation)
    {
        lock (_operationGate)
            return _operations.TryRemove(operation);
    }

    private void RemoveManagedOperation(PendingOperation operation)
    {
        TryRemoveOperation(
            new KeyValuePair<ulong, PendingOperation>(
                operation.OperationId.Low,
                operation));
        operation.Cancel();
    }

    private void CompleteManagedOperation(
        PendingOperation operation,
        RequestResult result,
        int failure,
        IReadOnlyList<Message> parts,
        MeshRecordPayload? kindData = null)
    {
        if (!TryRemoveOperation(
                new KeyValuePair<ulong, PendingOperation>(
                    operation.OperationId.Low,
                    operation))
            || !operation.TryComplete())
        {
            DisposeParts(parts);
            return;
        }
        EnqueueOwned(
            MailboxKey.ForNode(MeshReadyDomains.Infrastructure),
            new MeshReceiveRecord(
                MeshRecordKind.Completion,
                MeshReadyDomains.Infrastructure,
                default,
                default,
                0,
                default,
                operation.OperationId,
                operation.Kind,
                null,
                null,
                null,
                0,
                parts.Count,
                (int)result,
                failure,
                kindData),
            parts);
    }

    private bool TryGetActor(ActorRef actorRef, out ManagedActor actor) =>
        _actors.TryGetValue(actorRef.ActorId, out actor!)
        && actor.Ref.Generation == actorRef.Generation
        && actor.Ref.NodeRid == actorRef.NodeRid
        && !actor.Draining;

    private ManagedTransfer RequireTransfer(ActorTransferToken token) =>
        _transfers.TryGetValue(token, out var transfer)
            ? transfer
            : throw new InvalidOperationException("The Actor transfer token is stale.");

    private void EnqueueTransferControl(
        ManagedTransfer transfer,
        ActorTransferPhase phase,
        int resultCode)
    {
        var control = new ActorTransferControl(
            phase,
            transfer.Prepare.Role,
            transfer.Prepare.TransferId,
            transfer.Actor.Ref,
            transfer.Actor.MembershipEpoch,
            transfer.Prepare.FinalSequence,
            resultCode,
            0);
        EnqueueOwned(
            MailboxKey.ForActor(transfer.Actor, MeshReadyDomains.Infrastructure),
            new MeshReceiveRecord(
                MeshRecordKind.TransferControl,
                MeshReadyDomains.Infrastructure,
                _routingId,
                transfer.Actor.SpotRid,
                _lifecycleGeneration,
                transfer.Actor.Ref,
                default,
                default,
                null,
                null,
                null,
                0,
                0,
                resultCode,
                0,
                new ActorTransferControlRecord(control)),
            Array.Empty<Message>());
    }

    private void EnqueueActorLifecycle(
        ZLinkManagedSpot spot,
        ActorLifecycleKind kind,
        ManagedActor previous,
        ManagedActor current,
        IReadOnlyList<Message> parts) =>
        EnqueueActorLifecycle(
            spot,
            kind,
            previous.Snapshot(),
            current,
            parts);

    private void EnqueueActorLifecycle(
        ZLinkManagedSpot spot,
        ActorLifecycleKind kind,
        ActorSnapshot previous,
        ManagedActor current,
        IReadOnlyList<Message> parts)
    {
        var control = new ActorControlRecord(
            kind,
            previous.Ref,
            current.Ref,
            previous.SpotRid,
            current.SpotRid,
            previous.SpotGeneration,
            current.SpotGeneration,
            previous.MembershipEpoch,
            current.MembershipEpoch,
            0);
        var retained = CloneParts(parts);
        EnqueueOwned(
            MailboxKey.ForSpot(spot, MeshReadyDomains.Infrastructure),
            new MeshReceiveRecord(
                MeshRecordKind.SpotControl,
                MeshReadyDomains.Infrastructure,
                _routingId,
                previous.SpotRid,
                _lifecycleGeneration,
                previous.Ref,
                default,
                default,
                null,
                null,
                null,
                0,
                retained.Count,
                0,
                0,
                control),
            retained);
    }

    private static IReadOnlyList<Message> CloneParts(IReadOnlyList<Message> parts) =>
        parts.Select(Message.From).ToArray();

    private static void DisposeParts(IReadOnlyList<Message> parts)
    {
        foreach (var part in parts)
            part.Dispose();
    }

    private async Task ReceiveLoop(CancellationToken cancellationToken)
    {
        var events = new PollEvent[1];
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var count = _poller!.Wait(events, PollInterval);
                if (count > 0)
                    DrainRawSocket();
                ProcessInfrastructure(Stopwatch.GetTimestamp());
            }
            catch (ObjectDisposedException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ZlinkException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (Exception)
            {
                lock (_gate)
                    _state = MeshNodeState.Error;
                Publish(MeshMonitorEventKind.ProtocolError);
            }
            await Task.Yield();
        }
    }

    private void DrainRawSocket()
    {
        for (var index = 0; index < ReceiveBatchSize; index++)
        {
            using var received = Received.Create();
            bool available;
            lock (_socketGate)
                available = _socket!.Recv(received, RecvFlags.DontWait);
            if (!available)
                return;
            ProcessReceived(received);
        }
    }

    private void ProcessReceived(Received received)
    {
        if (received.RoutingId is not { } sourceRid || received.Parts.Count == 0)
        {
            Publish(MeshMonitorEventKind.ProtocolError);
            return;
        }

        var head = received.Parts[0].ToArray();
        if (ZLinkServiceWireCodec.TryDecodeLiveness(
                head,
                out var liveness,
                out _))
        {
            ProcessLiveness(sourceRid, liveness);
            return;
        }
        if (ZLinkServiceWireCodec.TryDecodeRouteAdmission(
                head,
                out var admissionCommand,
                out var admission,
                out _))
        {
            ProcessAdmission(sourceRid, admissionCommand, admission);
            return;
        }
        if (ZLinkServiceWireCodec.TryDecodeReply(head, out var reply, out _))
        {
            CompleteOperation(reply, received);
            return;
        }
        if (ZLinkServiceWireCodec.TryDecodeStateful(
                head,
                out var stateful,
                out _))
        {
            ProcessStateful(sourceRid, stateful, received);
            return;
        }
        if (!ZLinkServiceWireCodec.TryDecodeApplication(
                head,
                out var application,
                out _))
        {
            Publish(MeshMonitorEventKind.ProtocolError, peerRid: sourceRid);
            return;
        }

        var payloadOffset = application.HasMetadata ? 2 : 1;
        if (received.Parts.Count <= payloadOffset)
        {
            Publish(MeshMonitorEventKind.ProtocolError, peerRid: sourceRid);
            return;
        }
        var metadata = application.HasMetadata
            ? received.Parts[1].ToArray()
            : null;
        var parts = received.Parts.Skip(payloadOffset).Select(Message.From).ToArray();
        var request = application.Command is ServiceWireConstants.Command.NodeRequest
            or ServiceWireConstants.Command.ChannelRequest;
        var kind = application.Command switch
        {
            ServiceWireConstants.Command.NodeSend => MeshRecordKind.NodeSend,
            ServiceWireConstants.Command.NodeRequest => MeshRecordKind.NodeRequest,
            ServiceWireConstants.Command.ChannelSend => MeshRecordKind.ChannelSend,
            ServiceWireConstants.Command.ChannelRequest => MeshRecordKind.ChannelRequest,
            _ => throw new InvalidOperationException()
        };
        Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? replyHandler = null;
        if (request)
            replyHandler = (replyParts, flags) => SendReply(
                sourceRid,
                application.Correlation,
                replyParts,
                flags);
        EnqueueOwned(
            MailboxKey.ForNode(MeshReadyDomains.Application),
            new MeshReceiveRecord(
                kind,
                MeshReadyDomains.Application,
                sourceRid,
                default,
                ResolvePeerGeneration(sourceRid),
                default,
                request ? new MeshOperationId(0, application.Correlation) : default,
                application.Command == ServiceWireConstants.Command.NodeRequest
                    ? MeshOperationKind.NodeRequest
                    : application.Command == ServiceWireConstants.Command.ChannelRequest
                        ? MeshOperationKind.ChannelRequest
                        : default,
                application.ChannelName,
                null,
                metadata,
                0,
                parts.Length,
                0,
                0,
                null,
                replyHandler),
            parts);
    }

    private void ProcessStateful(
        RoutingId sourceRid,
        ZLinkServiceWireCodec.StatefulRecord stateful,
        Received received)
    {
        lock (_gate)
            if (!_peersByRid.TryGetValue(sourceRid, out var peer)
                || !peer.Admitted)
                return;
        var request = stateful.Command is ServiceWireConstants.Command.SpotRequest
            or ServiceWireConstants.Command.ActorRequest;
        if (stateful.TargetNodeRid != _routingId
            || stateful.TargetNodeGeneration != _lifecycleGeneration)
        {
            if (request)
                SendTerminalReply(
                    sourceRid,
                    stateful.Correlation,
                    RequestResult.Conflict,
                    (uint)ServiceWireConstants.FrameworkErrorCode.ActorLocationStale,
                    Array.Empty<Message>(),
                    SendFlags.DontWait);
            return;
        }
        var payloadOffset = stateful.HasMetadata ? 2 : 1;
        if (received.Parts.Count <= payloadOffset)
        {
            if (request)
                SendTerminalReply(
                    sourceRid,
                    stateful.Correlation,
                    RequestResult.ProtocolError,
                    (uint)ServiceWireConstants.FrameworkErrorCode.RequestProtocolError,
                    Array.Empty<Message>(),
                    SendFlags.DontWait);
            return;
        }
        var metadata = stateful.HasMetadata
            ? received.Parts[1].ToArray()
            : null;
        var parts = received.Parts.Skip(payloadOffset).Select(Message.From).ToArray();
        MailboxKey owner;
        MeshRecordKind kind;
        MeshOperationKind operationKind;
        ActorRef sourceActor = default;
        RoutingId sourceSpotRid = stateful.SourceSpotRid;
        if (stateful.Command is ServiceWireConstants.Command.SpotSend
            or ServiceWireConstants.Command.SpotRequest)
        {
            if (!_spots.TryGetValue(stateful.TargetSpotRid, out var spot)
                || spot.LifecycleGeneration != stateful.TargetSpotGeneration
                || spot.AuthorityOwnerGeneration
                    != stateful.AuthorityOwnerGeneration)
            {
                DisposeParts(parts);
                if (request)
                    SendTerminalReply(
                        sourceRid,
                        stateful.Correlation,
                        RequestResult.Conflict,
                        (uint)ServiceWireConstants.FrameworkErrorCode.SpotRouteNotFound,
                        Array.Empty<Message>(),
                        SendFlags.DontWait);
                return;
            }
            owner = MailboxKey.ForSpot(spot, MeshReadyDomains.Application);
            kind = request ? MeshRecordKind.SpotRequest : MeshRecordKind.SpotSend;
            operationKind = MeshOperationKind.SpotRequest;
        }
        else
        {
            if (!TryGetActor(stateful.TargetActor, out var actor)
                || actor.AuthorityOwnerGeneration
                    != stateful.AuthorityOwnerGeneration
                || actor.IsSealed)
            {
                DisposeParts(parts);
                if (request)
                    SendTerminalReply(
                        sourceRid,
                        stateful.Correlation,
                        actor is not null && actor.IsSealed
                            ? RequestResult.Busy
                            : RequestResult.Conflict,
                        (uint)ServiceWireConstants.FrameworkErrorCode.ActorLocationStale,
                        Array.Empty<Message>(),
                        SendFlags.DontWait);
                return;
            }
            owner = MailboxKey.ForActor(actor, MeshReadyDomains.Application);
            kind = request ? MeshRecordKind.ActorRequest : MeshRecordKind.ActorSend;
            operationKind = MeshOperationKind.ActorRequest;
            actor.NextSequence();
        }

        Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? reply = null;
        if (request)
            reply = (replyParts, flags) =>
                SendTerminalReply(
                    sourceRid,
                    stateful.Correlation,
                    RequestResult.Ok,
                    0,
                    replyParts,
                    flags);
        EnqueueOwned(
            owner,
            new MeshReceiveRecord(
                kind,
                MeshReadyDomains.Application,
                sourceRid,
                sourceSpotRid,
                ResolvePeerGeneration(sourceRid),
                sourceActor,
                request
                    ? new MeshOperationId(0, stateful.Correlation)
                    : default,
                request ? operationKind : default,
                null,
                null,
                metadata,
                0,
                parts.Length,
                0,
                0,
                null,
                reply),
            parts);
    }

    private void ProcessAdmission(
        RoutingId sourceRid,
        ServiceWireConstants.Command command,
        ZLinkServiceWireCodec.AdmissionRecord admission)
    {
        if (!string.Equals(admission.MeshName, _meshName, StringComparison.Ordinal))
        {
            Publish(MeshMonitorEventKind.PeerRejected, peerRid: sourceRid);
            return;
        }

        Peer peer;
        ZLinkServiceAdmissionDecision decision;
        lock (_gate)
        {
            peer = FindPeerForAdmission(sourceRid) ??
                   new Peer(checked(++_nextIntent), admission.AdvertisedEndpoint, sourceRid);
            if (peer.ExpectedRid is { } expected && expected != sourceRid)
            {
                peer.State = MeshPeerState.Error;
                Publish(MeshMonitorEventKind.PeerRejected, peerRid: sourceRid);
                return;
            }
            decision = ZLinkServiceAdmissionGuard.Evaluate(
                peer.Admission,
                command,
                admission);
            if (decision == ZLinkServiceAdmissionDecision.Reject)
            {
                if (_peersByIntent.ContainsKey(peer.Intent))
                {
                    peer.State = MeshPeerState.Error;
                    peer.Admitted = false;
                    _peersByRid.Remove(sourceRid);
                }
                Publish(MeshMonitorEventKind.ProtocolError, peerRid: sourceRid);
                Publish(MeshMonitorEventKind.PeerRejected, peerRid: sourceRid);
                return;
            }
            if (decision == ZLinkServiceAdmissionDecision.Idempotent)
            {
                if (command == ServiceWireConstants.Command.Hello)
                    SendAdmission(peer, ServiceWireConstants.Command.Admit);
                return;
            }
            if (!_peersByIntent.ContainsKey(peer.Intent))
                _peersByIntent.Add(peer.Intent, peer);
            peer.RoutingId = sourceRid;
            peer.PhysicalRoutingId = sourceRid;
            peer.LifecycleGeneration = admission.LifecycleGeneration;
            peer.DescriptorRevision = admission.DescriptorRevision;
            peer.Channels = admission.Channels;
            peer.Admission = admission;
            peer.State = MeshPeerState.Admitted;
            peer.Admitted = true;
            peer.Liveness = new ZLinkServiceLiveness(Stopwatch.GetTimestamp());
            peer.LastChangedMs = checked((ulong)Environment.TickCount64);
            _peersByRid[sourceRid] = peer;
            _state = MeshNodeState.Ready;
        }

        if (command == ServiceWireConstants.Command.Hello)
            SendAdmission(peer, ServiceWireConstants.Command.Admit);
        Publish(MeshMonitorEventKind.PeerAdmitted, peerRid: sourceRid);
        Publish(MeshMonitorEventKind.StateChanged);
    }

    private void ProcessLiveness(
        RoutingId sourceRid,
        ZLinkServiceWireCodec.LivenessRecord record)
    {
        if (record.Command == ServiceWireConstants.Command.LivenessProbe)
        {
            SendControl(
                sourceRid,
                ZLinkServiceWireCodec.EncodeLiveness(
                    ServiceWireConstants.Command.LivenessAck,
                    record.ProbeId));
            return;
        }

        lock (_gate)
            if (_peersByRid.TryGetValue(sourceRid, out var peer))
                peer.Liveness?.Acknowledge(
                    record.ProbeId,
                    Stopwatch.GetTimestamp());
    }

    private void ProcessInfrastructure(long now)
    {
        Peer[] peers;
        lock (_gate)
            peers = _peersByIntent.Values.ToArray();

        foreach (var peer in peers)
        {
            if (!peer.Admitted)
            {
                if (now >= peer.NextAdmissionTimestamp)
                {
                    peer.NextAdmissionTimestamp = Add(now, AdmissionRetryInterval);
                    SendAdmission(peer, ServiceWireConstants.Command.Hello);
                }
                continue;
            }
            var liveness = peer.Liveness;
            if (liveness is null)
                continue;
            if (liveness.IsExpired(now))
            {
                lock (_gate)
                {
                    peer.Admitted = false;
                    peer.State = MeshPeerState.Connecting;
                    _peersByRid.Remove(peer.RoutingId);
                    peer.NextAdmissionTimestamp = now;
                    _state = _peersByRid.Count == 0
                        ? MeshNodeState.Started
                        : MeshNodeState.PartialReady;
                }
                Publish(MeshMonitorEventKind.PeerClosed, peerRid: peer.RoutingId);
                continue;
            }
            if (liveness.TryGetProbe(now, out var probeId))
                SendControl(
                    peer.PhysicalRoutingId,
                    ZLinkServiceWireCodec.EncodeLiveness(
                        ServiceWireConstants.Command.LivenessProbe,
                        probeId));
        }
    }

    private SubmitResult SubmitRequest(
        RoutingId targetRid,
        ServiceWireConstants.Command command,
        string? channelName,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata,
        out MeshOperationId operationId)
    {
        var effectiveTimeout = timeout <= TimeSpan.Zero
            ? TimeSpan.FromSeconds(30)
            : timeout;
        var kind = command == ServiceWireConstants.Command.NodeRequest
            ? MeshOperationKind.NodeRequest
            : MeshOperationKind.ChannelRequest;
        if (!TryCreateOperation(kind, out var correlation, out var pending))
        {
            operationId = default;
            Publish(MeshMonitorEventKind.Backpressured, peerRid: targetRid);
            return SubmitResult.Backpressured;
        }
        operationId = pending.OperationId;

        var result = SubmitApplication(
            targetRid,
            command,
            correlation,
            channelName,
            parts,
            flags,
            metadata);
        if (result != SubmitResult.Ok)
        {
            TryRemoveOperation(correlation, out _);
            pending.Cancel();
            operationId = default;
            return result;
        }

        _ = ExpireOperationAsync(correlation, pending, effectiveTimeout);
        return SubmitResult.Ok;
    }

    private SubmitResult SubmitApplication(
        RoutingId targetRid,
        ServiceWireConstants.Command command,
        ulong correlation,
        string? channelName,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        ArgumentNullException.ThrowIfNull(parts);
        if (parts.Count == 0)
            throw new ArgumentException("Application payload is required.", nameof(parts));

        var head = ZLinkServiceWireCodec.EncodeApplication(
            command,
            correlation,
            channelName,
            !metadata.IsEmpty);
        if (targetRid == _routingId)
        {
            var recordKind = command switch
            {
                ServiceWireConstants.Command.NodeSend => MeshRecordKind.NodeSend,
                ServiceWireConstants.Command.NodeRequest => MeshRecordKind.NodeRequest,
                ServiceWireConstants.Command.ChannelSend => MeshRecordKind.ChannelSend,
                ServiceWireConstants.Command.ChannelRequest => MeshRecordKind.ChannelRequest,
                _ => throw new ArgumentOutOfRangeException(nameof(command))
            };
            var retained = parts.Select(Message.From).ToArray();
            Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? reply = null;
            if (correlation != 0)
                reply = (replyParts, _) =>
                {
                    CompleteLocalOperation(correlation, replyParts);
                    return SubmitResult.Ok;
                };
            EnqueueOwned(
                MailboxKey.ForNode(MeshReadyDomains.Application),
                new MeshReceiveRecord(
                    recordKind,
                    MeshReadyDomains.Application,
                    _routingId,
                    default,
                    _lifecycleGeneration,
                    default,
                    correlation == 0 ? default : new MeshOperationId(0, correlation),
                    command == ServiceWireConstants.Command.NodeRequest
                        ? MeshOperationKind.NodeRequest
                        : command == ServiceWireConstants.Command.ChannelRequest
                            ? MeshOperationKind.ChannelRequest
                            : default,
                    channelName,
                    null,
                    metadata.IsEmpty ? null : metadata.ToArray(),
                    0,
                    retained.Length,
                    0,
                    0,
                    null,
                    reply),
                retained);
            return SubmitResult.Ok;
        }

        Peer? peer;
        lock (_gate)
            _peersByRid.TryGetValue(targetRid, out peer);
        if (peer is null || !peer.Admitted)
            return SubmitResult.NotConnected;

        var wireParts = new List<ReadOnlyMemory<byte>>(parts.Count + 2) { head };
        if (!metadata.IsEmpty)
            wireParts.Add(metadata);
        foreach (var part in parts)
            wireParts.Add(part.ToArray());
        var sent = TrySend(peer.PhysicalRoutingId, wireParts, flags);
        if (!sent)
        {
            Publish(MeshMonitorEventKind.Backpressured, peerRid: targetRid);
            return SubmitResult.Backpressured;
        }
        Publish(
            MeshMonitorEventKind.MessageSubmitted,
            peerRid: targetRid,
            channelName: channelName ?? string.Empty);
        return SubmitResult.Ok;
    }

    private SubmitResult SendReply(
        RoutingId targetRid,
        ulong correlation,
        IReadOnlyList<Message> parts,
        SendFlags flags) =>
        SendTerminalReply(
            targetRid,
            correlation,
            RequestResult.Ok,
            0,
            parts,
            flags);

    private SubmitResult SendTerminalReply(
        RoutingId targetRid,
        ulong correlation,
        RequestResult result,
        uint failureCode,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        var wireParts = new List<ReadOnlyMemory<byte>>(parts.Count + 1)
        {
            ZLinkServiceWireCodec.EncodeReply(correlation, (int)result, failureCode)
        };
        foreach (var part in parts)
            wireParts.Add(part.ToArray());
        return TrySend(targetRid, wireParts, flags)
            ? SubmitResult.Ok
            : SubmitResult.Backpressured;
    }

    private void CompleteOperation(
        ZLinkServiceWireCodec.ReplyRecord reply,
        Received received)
    {
        if (!TryRemoveOperation(reply.Correlation, out var pending)
            || !pending.TryComplete())
            return;
        var parts = received.Parts.Skip(1).Select(Message.From).ToArray();
        EnqueueCompletion(
            pending.OperationId,
            pending.Kind,
            reply.TerminalResult,
            checked((int)reply.FailureCode),
            parts);
    }

    private void CompleteLocalOperation(
        ulong correlation,
        IReadOnlyList<Message> replyParts)
    {
        if (!TryRemoveOperation(correlation, out var pending)
            || !pending.TryComplete())
            return;
        EnqueueCompletion(
            pending.OperationId,
            pending.Kind,
            (int)RequestResult.Ok,
            0,
            replyParts.Select(Message.From).ToArray());
    }

    private async Task ExpireOperationAsync(
        ulong correlation,
        PendingOperation pending,
        TimeSpan timeout)
    {
        try
        {
            await Task.Delay(timeout, pending.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        if (TryRemoveOperation(correlation, out var current)
            && ReferenceEquals(current, pending)
            && pending.TryComplete())
            EnqueueCompletion(
                pending.OperationId,
                pending.Kind,
                (int)RequestResult.TimedOut,
                0,
                Array.Empty<Message>());
    }

    private void EnqueueCompletion(
        MeshOperationId operationId,
        MeshOperationKind operationKind,
        int result,
        int failure,
        IReadOnlyList<Message> parts)
    {
        EnqueueOwned(
            MailboxKey.ForNode(MeshReadyDomains.Infrastructure),
            new MeshReceiveRecord(
                MeshRecordKind.Completion,
                MeshReadyDomains.Infrastructure,
                default,
                default,
                0,
                default,
                operationId,
                operationKind,
                null,
                null,
                null,
                0,
                parts.Count,
                result,
                failure,
                null),
            parts);
        Publish(
            MeshMonitorEventKind.OperationCompleted,
            operationId: operationId,
            resultCode: result,
            failureErrno: failure);
    }

    private void EnqueueSendReady()
    {
        EnqueueOwned(
            MailboxKey.ForNode(MeshReadyDomains.Infrastructure),
            new MeshReceiveRecord(
                MeshRecordKind.SendReady,
                MeshReadyDomains.Infrastructure,
                default,
                default,
                0,
                default,
                default,
                default,
                null,
                null,
                null,
                0,
                0,
                0,
                0,
                new MeshSendReadyData(
                    MeshDestinationKind.Node,
                    default,
                    default,
                    default,
                    null)),
            Array.Empty<Message>());
    }

    private void EnqueueOwned(
        MailboxKey key,
        MeshReceiveRecord record,
        IReadOnlyList<Message> parts)
    {
        var queued = new QueuedRecord(record, parts);
        var mailbox = _ownedMailboxes.GetOrAdd(key, static _ => new OwnedMailbox());
        if (!mailbox.TryEnqueue(
                queued,
                MailboxMessageBudget,
                MailboxByteBudget))
        {
            queued.Dispose();
            Publish(MeshMonitorEventKind.Backpressured);
            return;
        }
        Interlocked.Increment(ref _queuedMessages);
        Interlocked.Add(ref _queuedBytes, checked((long)queued.PendingBytes));
        SignalReadyIfNeeded();
    }

    private bool DrainOwnedQueue(
        OwnedMailbox mailbox,
        MeshReceiveBatch batch,
        RecvFlags flags)
    {
        var count = 0;
        while (count < ReceiveBatchSize && mailbox.TryDequeue(out var queued))
        {
            Interlocked.Decrement(ref _queuedMessages);
            Interlocked.Add(ref _queuedBytes, -checked((long)queued.PendingBytes));
            batch.Add(queued.Record, queued.TakeParts());
            count++;
        }
        return count > 0;
    }

    private void ReleaseOwnedMailbox(OwnedMailbox mailbox)
    {
        mailbox.Release();
        if (mailbox.HasRecords)
            SignalReadyIfNeeded();
    }

    private void SignalReadyIfNeeded()
    {
        if (!_ownedMailboxes.Values.Any(static mailbox => mailbox.HasRecords)
            || Interlocked.CompareExchange(ref _readyPosted, 1, 0) != 0)
            return;
        _readyHandler?.Invoke(MeshReadyDomains.All);
    }

    private bool TrySelectChannelTarget(string channelName, out RoutingId targetRid)
    {
        var targets = SnapshotChannelTargets(channelName);
        if (targets.Count == 0)
        {
            targetRid = default;
            return false;
        }
        var cursor = Interlocked.Increment(ref _nextChannelSelection);
        targetRid = targets[(int)((ulong)cursor % (ulong)targets.Count)];
        return true;
    }

    private List<RoutingId> SnapshotChannelTargets(string channelName)
    {
        lock (_gate)
        {
            var targets = new List<RoutingId>();
            if (_channels.TryGetValue(channelName, out var localWeight)
                && localWeight > 0)
                targets.Add(_routingId);
            targets.AddRange(_peersByRid.Values
                .Where(peer => peer.Admitted
                               && peer.Channels.TryGetValue(channelName, out var weight)
                               && weight > 0)
                .Select(static peer => peer.RoutingId)
                .OrderBy(static rid => rid.ToHex(), StringComparer.Ordinal));
            return targets;
        }
    }

    private void ConnectPeerCore(Peer peer)
    {
        peer.State = MeshPeerState.Connecting;
        peer.PhysicalRoutingId = peer.ExpectedRid
            ?? RoutingId.From($"zlink-intent-{peer.Intent:x16}");
        lock (_socketGate)
        {
            _socket!.Options.SetConnectRoutingId(peer.PhysicalRoutingId);
            _socket.Connect(peer.Endpoint);
        }
        peer.NextAdmissionTimestamp = 0;
        Publish(MeshMonitorEventKind.PeerConnecting, peerRid: peer.ExpectedRid ?? default);
    }

    private void SendAdmission(
        Peer peer,
        ServiceWireConstants.Command command)
    {
        if (_socket is null)
            return;
        var target = peer.PhysicalRoutingId;
        if (target.IsEmpty)
            return;
        ulong descriptorRevision;
        Dictionary<string, uint> channels;
        lock (_gate)
        {
            descriptorRevision = _descriptorRevision;
            channels = new Dictionary<string, uint>(_channels, StringComparer.Ordinal);
        }
        var descriptor = ZLinkServiceWireCodec.EncodeRouteAdmission(
            command,
            _meshName,
            _bindEndpoint,
            _lifecycleGeneration,
            descriptorRevision,
            channels);
        SendControl(target, descriptor);
    }

    private void SendControl(RoutingId target, byte[] head)
    {
        _ = TrySend(target, [head], SendFlags.DontWait);
    }

    private bool TrySend(
        RoutingId target,
        IReadOnlyList<ReadOnlyMemory<byte>> parts,
        SendFlags flags)
    {
        if (_socket is null)
            return false;
        var messages = new Message[parts.Count];
        var created = 0;
        try
        {
            for (; created < messages.Length; created++)
                messages[created] = Message.From(parts[created]);
            lock (_socketGate)
                return _socket.Send(target)
                    .Messages(messages)
                    .Flags(flags)
                    .Submit();
        }
        catch (ZlinkException)
        {
            return false;
        }
        finally
        {
            for (var index = 0; index < created; index++)
                messages[index].Dispose();
        }
    }

    private Peer? FindPeerForAdmission(RoutingId sourceRid)
    {
        if (_peersByRid.TryGetValue(sourceRid, out var exact))
            return exact;
        return _peersByIntent.Values.FirstOrDefault(peer =>
            peer.ExpectedRid == sourceRid
            || peer.PhysicalRoutingId == sourceRid
            || (!peer.Admitted && peer.ExpectedRid is null));
    }

    private ulong ResolvePeerGeneration(RoutingId sourceRid)
    {
        lock (_gate)
            return _peersByRid.TryGetValue(sourceRid, out var peer)
                ? peer.LifecycleGeneration
                : 0;
    }

    private void RemovePeer(Peer peer, bool disconnect)
    {
        _peersByIntent.Remove(peer.Intent);
        if (!peer.RoutingId.IsEmpty)
            _peersByRid.Remove(peer.RoutingId);
        peer.Admitted = false;
        peer.State = MeshPeerState.Closed;
        if (disconnect && _socket is not null)
            try
            {
                lock (_socketGate)
                    _socket.Disconnect(peer.Endpoint);
            }
            catch (ZlinkException)
            {
            }
        Publish(MeshMonitorEventKind.PeerClosed, peerRid: peer.RoutingId);
    }

    private void Publish(
        MeshMonitorEventKind kind,
        RoutingId peerRid = default,
        string channelName = "",
        MeshOperationId operationId = default,
        int resultCode = 0,
        int failureErrno = 0)
    {
        RawMeshMonitor[] monitors;
        MeshNodeState state;
        lock (_gate)
        {
            monitors = _monitors.ToArray();
            state = _state;
        }
        foreach (var monitor in monitors)
            monitor.Publish(
                kind,
                state,
                peerRid,
                channelName,
                operationId,
                resultCode,
                failureErrno);
    }

    private void ThrowIfStarted()
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_state != MeshNodeState.Created)
                throw new InvalidOperationException("The MeshNode has already started.");
        }
    }

    private void ThrowIfDisposed() =>
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);

    private static NotSupportedException StatefulSlicePending() =>
        new("The stateful object runtime is implemented by the M6B slice.");

    private static ulong NewNonZeroToken()
    {
        Span<byte> bytes = stackalloc byte[sizeof(ulong)];
        do
        {
            RandomNumberGenerator.Fill(bytes);
        } while (BinaryPrimitives.ReadUInt64BigEndian(bytes) == 0);
        return BinaryPrimitives.ReadUInt64BigEndian(bytes);
    }

    private ulong NextAuthorityOwnerGeneration()
    {
        var generation = Interlocked.Increment(ref _nextAuthorityOwnerGeneration);
        if (generation == 0 || generation > long.MaxValue)
            throw new InvalidOperationException(
                "The authority owner generation space was exhausted.");
        return generation;
    }

    private static void ValidateObservedAuthority(
        RoutingId targetNodeRid,
        ulong objectGeneration,
        ulong authorityOwnerGeneration)
    {
        if (targetNodeRid.IsEmpty)
            throw new ArgumentException(
                "The observed owner node routing id is required.",
                nameof(targetNodeRid));
        if (objectGeneration is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(objectGeneration));
        if (authorityOwnerGeneration is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(authorityOwnerGeneration));
    }

    private static long Add(long timestamp, TimeSpan duration)
    {
        var delta = (long)Math.Ceiling(duration.TotalSeconds * Stopwatch.Frequency);
        return checked(timestamp + delta);
    }

    private sealed class Peer(
        ulong intent,
        string endpoint,
        RoutingId? expectedRid)
    {
        internal ulong Intent { get; } = intent;
        internal string Endpoint { get; } = endpoint;
        internal RoutingId? ExpectedRid { get; } = expectedRid;
        internal RoutingId RoutingId { get; set; }
        internal RoutingId PhysicalRoutingId { get; set; }
        internal ulong LifecycleGeneration { get; set; }
        internal ulong DescriptorRevision { get; set; }
        internal IReadOnlyDictionary<string, uint> Channels { get; set; } =
            new Dictionary<string, uint>(StringComparer.Ordinal);
        internal ZLinkServiceWireCodec.AdmissionRecord? Admission { get; set; }
        internal MeshPeerState State { get; set; } = MeshPeerState.Configured;
        internal bool Admitted { get; set; }
        internal ZLinkServiceLiveness? Liveness { get; set; }
        internal long NextAdmissionTimestamp { get; set; }
        internal ulong LastChangedMs { get; set; } =
            checked((ulong)Environment.TickCount64);

        internal MeshNodePeer Snapshot() =>
            new(
                Intent,
                MeshPeerSource.Manual,
                State,
                RoutingId,
                LifecycleGeneration,
                DescriptorRevision,
                Endpoint,
                checked((uint)Channels.Count),
                0,
                LastChangedMs);
    }

    private readonly record struct MailboxKey(
        MeshOwnerKind OwnerKind,
        string Identity,
        ulong Generation,
        MeshReadyDomains Domain,
        RoutingId SpotRid,
        ActorRef Actor)
    {
        internal static MailboxKey ForNode(MeshReadyDomains domain) =>
            new(MeshOwnerKind.Node, string.Empty, 0, domain, default, default);

        internal static MailboxKey ForSpot(
            ZLinkManagedSpot spot,
            MeshReadyDomains domain) =>
            new(
                MeshOwnerKind.Spot,
                spot.RoutingId.ToHex(),
                spot.LifecycleGeneration,
                domain,
                spot.RoutingId,
                default);

        internal static MailboxKey ForActor(
            ManagedActor actor,
            MeshReadyDomains domain) =>
            new(
                MeshOwnerKind.Actor,
                actor.Ref.ActorId,
                actor.Ref.Generation,
                domain,
                default,
                actor.Ref);
    }

    private readonly record struct ObservedSpotAuthorityKey(
        RoutingId NodeRid,
        RoutingId SpotRid,
        ulong ObjectGeneration);

    private readonly record struct ObservedActorAuthorityKey(
        RoutingId NodeRid,
        string ActorId,
        ulong ObjectGeneration);

    private sealed class OwnedMailbox : IDisposable
    {
        private readonly Queue<QueuedRecord> _records = new();
        private readonly object _gate = new();
        private ulong _pendingBytes;
        private bool _claimed;

        internal bool HasRecords
        {
            get
            {
                lock (_gate)
                    return _records.Count != 0;
            }
        }

        internal int Count
        {
            get
            {
                lock (_gate)
                    return _records.Count;
            }
        }

        internal bool IsClaimed
        {
            get
            {
                lock (_gate)
                    return _claimed;
            }
        }

        internal bool TryEnqueue(
            QueuedRecord record,
            ulong messageBudget,
            ulong byteBudget)
        {
            lock (_gate)
            {
                if ((ulong)_records.Count >= messageBudget
                    || record.PendingBytes > byteBudget - Math.Min(
                        _pendingBytes,
                        byteBudget))
                    return false;
                _records.Enqueue(record);
                _pendingBytes = checked(_pendingBytes + record.PendingBytes);
                return true;
            }
        }

        internal bool TryClaim()
        {
            lock (_gate)
            {
                if (_claimed || _records.Count == 0)
                    return false;
                _claimed = true;
                return true;
            }
        }

        internal bool TryDequeue(out QueuedRecord record)
        {
            lock (_gate)
            {
                if (_records.Count == 0)
                {
                    record = null!;
                    return false;
                }
                record = _records.Dequeue();
                _pendingBytes -= record.PendingBytes;
                return true;
            }
        }

        internal void Release()
        {
            lock (_gate)
                _claimed = false;
        }

        public void Dispose()
        {
            lock (_gate)
            {
                while (_records.Count != 0)
                    _records.Dequeue().Dispose();
                _pendingBytes = 0;
                _claimed = false;
            }
        }
    }

    private readonly record struct ActorSnapshot(
        ActorRef Ref,
        RoutingId SpotRid,
        ulong SpotGeneration,
        ulong MembershipEpoch);

    private sealed class ManagedActor(
        ActorRef actorRef,
        RoutingId spotRid,
        ulong spotGeneration,
        ulong membershipEpoch,
        ulong authorityOwnerGeneration)
    {
        private readonly object _gate = new();
        private long _sequence;
        private bool _sealed;
        private bool _draining;
        private ActorBinding? _binding;

        internal ActorRef Ref { get; } = actorRef;
        internal RoutingId SpotRid { get; private set; } = spotRid;
        internal ulong SpotGeneration { get; private set; } = spotGeneration;
        internal ulong MembershipEpoch { get; private set; } = membershipEpoch;
        internal ulong AuthorityOwnerGeneration { get; private set; } =
            authorityOwnerGeneration;
        internal bool Draining
        {
            get
            {
                lock (_gate)
                    return _draining;
            }
        }
        internal bool IsSealed
        {
            get
            {
                lock (_gate)
                    return _sealed;
            }
        }
        internal ActorBinding? Binding
        {
            get
            {
                lock (_gate)
                    return _binding;
            }
        }
        internal ActorLocation Location =>
            new(Ref, SpotRid, SpotGeneration, MembershipEpoch);

        internal ActorSnapshot Snapshot()
        {
            lock (_gate)
                return new ActorSnapshot(
                    Ref,
                    SpotRid,
                    SpotGeneration,
                    MembershipEpoch);
        }

        internal ulong NextSequence() =>
            checked((ulong)Interlocked.Increment(ref _sequence));

        internal bool TryDrain()
        {
            lock (_gate)
            {
                if (_draining || _sealed)
                    return false;
                _draining = true;
                _binding = null;
                return true;
            }
        }

        internal bool TryMove(
            ActorSnapshot expected,
            RoutingId targetSpotRid,
            ulong targetSpotGeneration)
        {
            lock (_gate)
            {
                if (_draining
                    || _sealed
                    || SpotRid != expected.SpotRid
                    || SpotGeneration != expected.SpotGeneration
                    || MembershipEpoch != expected.MembershipEpoch)
                    return false;
                SpotRid = targetSpotRid;
                SpotGeneration = targetSpotGeneration;
                MembershipEpoch = checked(MembershipEpoch + 1);
                return true;
            }
        }

        internal bool TrySeal()
        {
            lock (_gate)
            {
                if (_draining || _sealed)
                    return false;
                _sealed = true;
                return true;
            }
        }

        internal void Unseal()
        {
            lock (_gate)
                _sealed = false;
        }

        internal void CommitMembershipEpoch(ulong epoch)
        {
            lock (_gate)
            {
                if (!_sealed || epoch <= MembershipEpoch)
                    throw new InvalidOperationException(
                        "The transfer membership epoch is stale.");
                MembershipEpoch = epoch;
            }
        }

        internal ulong Bind(
            ZLinkManagedStreamSessionService service,
            RoutingId sessionRid)
        {
            lock (_gate)
            {
                var generation = checked((_binding?.Generation ?? 0) + 1);
                _binding = new ActorBinding(service, sessionRid, generation);
                return generation;
            }
        }

        internal bool TryClearBinding(ulong expectedGeneration)
        {
            lock (_gate)
            {
                if (_binding is null
                    || (expectedGeneration != 0
                        && _binding.Generation != expectedGeneration))
                    return false;
                _binding = null;
                return true;
            }
        }
    }

    private sealed record ActorBinding(
        ZLinkManagedStreamSessionService Service,
        RoutingId SessionRid,
        ulong Generation);

    private sealed class ManagedTransfer(
        ActorTransferToken token,
        ActorTransferPrepare prepare,
        ManagedActor actor)
    {
        private int _phase = (int)ActorTransferPhase.Preparing;
        internal ActorTransferToken Token { get; } = token;
        internal ActorTransferPrepare Prepare { get; } = prepare;
        internal ManagedActor Actor { get; } = actor;

        internal void Commit(ulong membershipEpoch)
        {
            if (Interlocked.CompareExchange(
                    ref _phase,
                    (int)ActorTransferPhase.Committed,
                    (int)ActorTransferPhase.Preparing)
                != (int)ActorTransferPhase.Preparing)
                throw new InvalidOperationException("The transfer is not preparing.");
            Actor.CommitMembershipEpoch(membershipEpoch);
        }

        internal void Activate()
        {
            if (Interlocked.CompareExchange(
                    ref _phase,
                    (int)ActorTransferPhase.Activated,
                    (int)ActorTransferPhase.Committed)
                != (int)ActorTransferPhase.Committed)
                throw new InvalidOperationException("The transfer is not committed.");
        }

        internal void Abort()
        {
            if (Interlocked.CompareExchange(
                    ref _phase,
                    (int)ActorTransferPhase.Aborted,
                    (int)ActorTransferPhase.Preparing)
                != (int)ActorTransferPhase.Preparing)
                throw new InvalidOperationException(
                    "Only an uncommitted transfer can be aborted.");
        }
    }

    private sealed class PendingOperation(
        MeshOperationId operationId,
        MeshOperationKind kind)
    {
        private readonly CancellationTokenSource _timeout = new();
        private int _terminal;
        internal MeshOperationId OperationId { get; } = operationId;
        internal MeshOperationKind Kind { get; } = kind;
        internal CancellationToken Token => _timeout.Token;
        internal bool TryComplete()
        {
            if (Interlocked.CompareExchange(ref _terminal, 1, 0) != 0)
                return false;
            _timeout.Cancel();
            _timeout.Dispose();
            return true;
        }
        internal void Cancel()
        {
            if (Interlocked.Exchange(ref _terminal, 1) == 0)
                _timeout.Cancel();
            _timeout.Dispose();
        }
    }

    private sealed class QueuedRecord(
        MeshReceiveRecord record,
        IReadOnlyList<Message> parts) : IDisposable
    {
        private IReadOnlyList<Message>? _parts = parts;
        internal MeshReceiveRecord Record { get; } = record;
        internal ulong PendingBytes =>
            checked((ulong)(_parts?.Sum(static part => part.Size) ?? 0));
        internal IReadOnlyList<Message> TakeParts() =>
            Interlocked.Exchange(ref _parts, null) ?? Array.Empty<Message>();
        public void Dispose()
        {
            var owned = Interlocked.Exchange(ref _parts, null);
            if (owned is null)
                return;
            foreach (var part in owned)
                part.Dispose();
        }
    }
}

internal sealed class ZLinkManagedSpot(
    ZLinkManagedMeshNode node,
    RoutingId routingId,
    ulong lifecycleGeneration,
    ulong authorityOwnerGeneration) : ISpot
{
    private readonly Dictionary<string, HashSet<string>> _subscriptions =
        new(StringComparer.Ordinal);
    private int _disposed;
    private int _actorCount;

    public RoutingId RoutingId { get; } = routingId;
    public ulong LifecycleGeneration { get; } = lifecycleGeneration;
    internal ulong AuthorityOwnerGeneration { get; } = authorityOwnerGeneration;
    internal int ActorCount => Volatile.Read(ref _actorCount);

    public void SetRoutingId(RoutingId routingId)
    {
        if (routingId != RoutingId)
            throw new InvalidOperationException("A managed Spot routing id is immutable.");
    }

    public SpotStatus Status() => new(LifecycleGeneration);

    public void SetSubscription(string channelName, string topic)
    {
        lock (_subscriptions)
        {
            if (!_subscriptions.TryGetValue(channelName, out var topics))
            {
                topics = new HashSet<string>(StringComparer.Ordinal);
                _subscriptions.Add(channelName, topics);
            }
            topics.Add(topic);
        }
    }

    internal bool Matches(string channelName, string topic)
    {
        lock (_subscriptions)
            return _subscriptions.TryGetValue(channelName, out var topics)
                   && topics.Contains(topic);
    }

    internal void AddActor() => Interlocked.Increment(ref _actorCount);

    internal void RemoveActor()
    {
        if (Interlocked.Decrement(ref _actorCount) < 0)
            Interlocked.Exchange(ref _actorCount, 0);
    }

    public SubmitResult SendToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        node.SendToChannel(channelName, parts, flags, metadata);

    public SubmitResult RequestToChannel(
        string channelName,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        node.RequestToChannel(
            channelName,
            parts,
            out operationId,
            timeout,
            flags,
            metadata);

    public MeshPublishResult Publish(
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        node.Publish(RoutingId, channelName, topic, parts, flags, metadata);

    public SubmitResult SendToSpot(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        node.SendToSpot(
            RoutingId,
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            parts,
            flags,
            metadata);

    public SubmitResult RequestToSpot(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        node.RequestToSpot(
            RoutingId,
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            parts,
            out operationId,
            timeout,
            flags,
            metadata);

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) == 0)
            node.ReleaseSpot(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }
}

internal sealed class ZLinkManagedStreamSessionService(
    ZLinkManagedMeshNode node,
    IStreamSocket stream) : IStreamSessionService
{
    private readonly ConcurrentDictionary<
        RoutingId,
        ConcurrentDictionary<string, StreamSessionBinding>> _bindings = new();
    private int _started;
    private int _disposed;

    public void Start()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
        Interlocked.Exchange(ref _started, 1);
    }

    public SubmitResult BindActor(
        RoutingId sessionRid,
        ActorRef actor,
        out MeshOperationId operationId,
        TimeSpan timeout = default)
    {
        EnsureStarted();
        return node.BindSessionActor(
            this,
            sessionRid,
            actor,
            out operationId,
            timeout);
    }

    public SubmitResult UnbindActor(
        RoutingId sessionRid,
        ActorRef actor,
        ulong expectedBindingGeneration,
        out MeshOperationId operationId,
        TimeSpan timeout = default)
    {
        EnsureStarted();
        return node.UnbindSessionActor(
            this,
            sessionRid,
            actor,
            expectedBindingGeneration,
            out operationId,
            timeout);
    }

    public StreamSessionBinding[] Bindings(RoutingId sessionRid) =>
        _bindings.TryGetValue(sessionRid, out var bindings)
            ? bindings.Values
                .OrderBy(static binding => binding.Actor.ActorId, StringComparer.Ordinal)
                .ToArray()
            : Array.Empty<StreamSessionBinding>();

    public SubmitResult SendToActor(
        RoutingId sessionRid,
        ActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        EnsureStarted();
        if (!_bindings.TryGetValue(sessionRid, out var bindings)
            || !bindings.TryGetValue(actor.ActorId, out var binding)
            || binding.Actor != actor)
            return SubmitResult.NotFound;
        return node.RelaySessionToActor(this, sessionRid, actor, parts, flags);
    }

    internal void RecordBinding(
        RoutingId sessionRid,
        StreamSessionBinding binding)
    {
        var session = _bindings.GetOrAdd(
            sessionRid,
            static _ => new ConcurrentDictionary<string, StreamSessionBinding>(
                StringComparer.Ordinal));
        session[binding.Actor.ActorId] = binding;
    }

    internal void RemoveBinding(
        RoutingId sessionRid,
        string actorId,
        ulong expectedBindingGeneration)
    {
        if (!_bindings.TryGetValue(sessionRid, out var bindings)
            || !bindings.TryGetValue(actorId, out var binding)
            || (expectedBindingGeneration != 0
                && binding.BindingGeneration != expectedBindingGeneration))
            return;
        bindings.TryRemove(
            new KeyValuePair<string, StreamSessionBinding>(
                actorId,
                binding));
        if (bindings.IsEmpty)
            _bindings.TryRemove(sessionRid, out _);
    }

    internal SubmitResult SendToSession(
        RoutingId sessionRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        EnsureStarted();
        var retained = parts.Select(Message.From).ToArray();
        try
        {
            return stream.Send(sessionRid)
                .Messages(retained)
                .Flags(flags)
                .Submit()
                ? SubmitResult.Ok
                : SubmitResult.Backpressured;
        }
        catch (ZlinkException)
        {
            return SubmitResult.NotConnected;
        }
        finally
        {
            foreach (var part in retained)
                part.Dispose();
        }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
            return;
        _bindings.Clear();
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    private void EnsureStarted()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
        if (Volatile.Read(ref _started) == 0)
            throw new InvalidOperationException(
                "The STREAM session service has not started.");
    }
}
