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
    private const int MaxRemoteUserSpotOperations = 4_096;
    private const int MaxRemoteActorCreateOperations = 4_096;
    private static readonly TimeSpan DefaultRemoteUserSpotTerminalRetention =
        TimeSpan.FromMinutes(5);
    private static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(100);
    private static readonly TimeSpan AdmissionRetryInterval = TimeSpan.FromMilliseconds(500);

    private readonly IContext _context;
    private readonly string _meshName;
    private readonly int _maxPendingOperations;
    private readonly TimeSpan _remoteUserSpotTerminalRetention;
    private readonly object _gate = new();
    private readonly object _socketGate = new();
    private readonly object _readyGate = new();
    private readonly object _operationGate = new();
    private readonly object _remoteUserSpotGate = new();
    private readonly object _remoteActorCreateGate = new();
    private readonly Dictionary<string, uint> _channels = new(StringComparer.Ordinal);
    private readonly Dictionary<ulong, Peer> _peersByIntent = new();
    private readonly Dictionary<RoutingId, Peer> _peersByRid = new();
    private readonly ConcurrentDictionary<MailboxKey, OwnedMailbox> _ownedMailboxes = new();
    private readonly ConcurrentDictionary<ulong, PendingOperation> _operations = new();
    private readonly ConcurrentDictionary<string, ZLinkManagedSpot> _spots = new();
    private readonly ConcurrentDictionary<string, ManagedActor> _actors =
        new(StringComparer.Ordinal);
    private readonly ConcurrentDictionary<ActorTransferToken, ManagedTransfer> _transfers = new();
    private readonly ConcurrentDictionary<RemoteUserSpotOperationKey, RemoteUserSpotInvocation>
        _remoteUserSpotOperations = new();
    private readonly ConcurrentDictionary<RemoteActorCreateOperationKey, RemoteActorCreateInvocation>
        _remoteActorCreateOperations = new();
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
    private IUserSpotOperationTarget? _userSpotOperationTarget;
    private IActorCreateOperationTarget? _actorCreateOperationTarget;
    private IInstanceSpotActivationTarget? _instanceSpotActivationTarget;
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
        int maxPendingOperations = DefaultMaxPendingOperations,
        TimeSpan? remoteUserSpotTerminalRetention = null)
    {
        _context = context ?? throw new ArgumentNullException(nameof(context));
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        if (maxPendingOperations <= 0)
            throw new ArgumentOutOfRangeException(nameof(maxPendingOperations));
        if (remoteUserSpotTerminalRetention is { } retention
            && retention < TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(
                nameof(remoteUserSpotTerminalRetention));
        _meshName = meshName;
        _maxPendingOperations = maxPendingOperations;
        _remoteUserSpotTerminalRetention =
            remoteUserSpotTerminalRetention
            ?? DefaultRemoteUserSpotTerminalRetention;
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
        if (weight > ZLinkSocketConfig.MaximumPeerWeight)
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

    public void SetUserSpotOperationTarget(IUserSpotOperationTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_userSpotOperationTarget is not null
                && !ReferenceEquals(_userSpotOperationTarget, target))
                throw new InvalidOperationException(
                    "A User Spot operation target is already registered.");
            _userSpotOperationTarget = target;
        }
    }

    public void SetActorCreateOperationTarget(IActorCreateOperationTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_actorCreateOperationTarget is not null
                && !ReferenceEquals(_actorCreateOperationTarget, target))
                throw new InvalidOperationException(
                    "An Actor create operation target is already registered.");
            _actorCreateOperationTarget = target;
        }
    }

    public void SetInstanceSpotActivationTarget(IInstanceSpotActivationTarget target)
    {
        ArgumentNullException.ThrowIfNull(target);
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_instanceSpotActivationTarget is not null
                && !ReferenceEquals(_instanceSpotActivationTarget, target))
                throw new InvalidOperationException(
                    "An Instance Spot activation target is already registered.");
            _instanceSpotActivationTarget = target;
        }
    }

    public SubmitResult ActivateInstanceSpot(
        InstanceSpotActivationTarget target,
        string sourceSpotId,
        IReadOnlyList<Message> parts,
        bool request,
        out MeshOperationId operationId,
        ulong deadlineUnixMs,
        TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        ArgumentNullException.ThrowIfNull(parts);
        if (parts.Count == 0)
            throw new ArgumentException(
                "The first Instance Spot message is required.",
                nameof(parts));
        if (target.TargetNodeRid == _routingId)
            throw new ArgumentException(
                "Command 39 cold activation is reserved for a remote target.",
                nameof(target));

        Peer? peer;
        lock (_gate)
            _peersByRid.TryGetValue(target.TargetNodeRid, out peer);
        if (peer is null
            || !peer.Admitted
            || peer.LifecycleGeneration != target.TargetNodeGeneration)
        {
            operationId = default;
            return SubmitResult.NotConnected;
        }

        PendingOperation? pending = null;
        ulong replyRouteId = 0;
        if (request)
        {
            if (!TryCreateOperation(
                    MeshOperationKind.InstanceSpotRequest,
                    out replyRouteId,
                    out pending))
            {
                operationId = default;
                Publish(
                    MeshMonitorEventKind.Backpressured,
                    peerRid: target.TargetNodeRid);
                return SubmitResult.Backpressured;
            }
            operationId = pending.OperationId;
        }
        else
        {
            lock (_operationGate)
            {
                var low = ++_nextOperation;
                if (low == 0)
                    throw new InvalidOperationException(
                        "The operation id space was exhausted.");
                operationId = new MeshOperationId(_lifecycleGeneration, low);
            }
        }

        var operation = new InstanceSpotActivationOperation(
            target,
            _routingId,
            _lifecycleGeneration,
            sourceSpotId,
            operationId,
            request,
            replyRouteId,
            deadlineUnixMs);
        var head = ZLinkServiceWireCodec.EncodeInstanceSpotActivation(
            operation,
            !metadata.IsEmpty);
        var wireParts = new List<ReadOnlyMemory<byte>>(parts.Count + 2) { head };
        if (!metadata.IsEmpty)
            wireParts.Add(metadata);
        foreach (var part in parts)
            wireParts.Add(part.ToArray());

        if (!TrySend(peer.PhysicalRoutingId, wireParts, flags))
        {
            if (pending is not null)
            {
                TryRemoveOperation(replyRouteId, out _);
                pending.Cancel();
            }
            operationId = default;
            Publish(
                MeshMonitorEventKind.Backpressured,
                peerRid: target.TargetNodeRid);
            return SubmitResult.Backpressured;
        }

        if (pending is not null)
            _ = ExpireOperationAsync(
                replyRouteId,
                pending,
                timeout <= TimeSpan.Zero ? TimeSpan.FromSeconds(30) : timeout);
        Publish(
            MeshMonitorEventKind.MessageSubmitted,
            peerRid: target.TargetNodeRid);
        return SubmitResult.Ok;
    }

    public SubmitResult CreateUserSpot(
        RoutingId targetNodeRid,
        string spotId,
        string stableType,
        ObjectReservationFence reservation,
        ulong deadlineUnixMs,
        out MeshOperationId operationId,
        TimeSpan timeout = default)
    {
        if (targetNodeRid == _routingId)
            throw new ArgumentException(
                "Command 47 is reserved for a remote User Spot target.",
                nameof(targetNodeRid));
        if (reservation.TargetNodeRid != targetNodeRid)
            throw new ArgumentException(
                "The reservation target must match the command target.",
                nameof(reservation));
        return SubmitReservedCreationOperation(
            targetNodeRid,
            reservation.TargetNodeGeneration,
            MeshOperationKind.UserSpotCreate,
            (correlation, operation) => ZLinkServiceWireCodec.EncodeUserSpotCreate(
                new UserSpotCreateOperation(
                    correlation,
                    operation,
                    _routingId,
                    _lifecycleGeneration,
                    spotId,
                    stableType,
                    reservation,
                    deadlineUnixMs)),
            out operationId,
            timeout);
    }

    public SubmitResult CloseUserSpot(
        RoutingId targetNodeRid,
        UserSpotCloseFence target,
        ulong deadlineUnixMs,
        out MeshOperationId operationId,
        TimeSpan timeout = default)
    {
        if (targetNodeRid == _routingId)
            throw new ArgumentException(
                "Command 48 is reserved for a remote User Spot owner.",
                nameof(targetNodeRid));
        if (target.TargetNodeRid != targetNodeRid)
            throw new ArgumentException(
                "The close fence target must match the command target.",
                nameof(target));
        return SubmitReservedCreationOperation(
            targetNodeRid,
            target.TargetNodeGeneration,
            MeshOperationKind.UserSpotClose,
            (correlation, operation) => ZLinkServiceWireCodec.EncodeUserSpotClose(
                new UserSpotCloseOperation(
                    correlation,
                    operation,
                    _routingId,
                    _lifecycleGeneration,
                    target,
                    deadlineUnixMs)),
            out operationId,
            timeout);
    }

    public SubmitResult CreateActorRemote(
        RoutingId targetNodeRid,
        string actorId,
        string stableType,
        ObjectReservationFence reservation,
        ulong deadlineUnixMs,
        out MeshOperationId operationId,
        TimeSpan timeout = default)
    {
        if (targetNodeRid == _routingId)
            throw new ArgumentException(
                "Command 49 is reserved for a remote Actor target.",
                nameof(targetNodeRid));
        if (reservation.TargetNodeRid != targetNodeRid)
            throw new ArgumentException(
                "The reservation target must match the command target.",
                nameof(reservation));
        return SubmitReservedCreationOperation(
            targetNodeRid,
            reservation.TargetNodeGeneration,
            MeshOperationKind.ActorCreate,
            (correlation, operation) => ZLinkServiceWireCodec.EncodeActorCreate(
                new ActorCreateOperation(
                    correlation,
                    operation,
                    _routingId,
                    _lifecycleGeneration,
                    actorId,
                    stableType,
                    reservation,
                    deadlineUnixMs)),
            out operationId,
            timeout);
    }

    internal SubmitResult ResubmitUserSpotOperation(
        RoutingId targetNodeRid,
        ZLinkServiceWireCodec.UserSpotOperationRecord operation)
    {
        var sourceNodeRid = operation.Command
            == ServiceWireConstants.Command.UserSpotCreate
                ? operation.Create.SourceNodeRid
                : operation.Close.SourceNodeRid;
        var sourceNodeGeneration = operation.Command
            == ServiceWireConstants.Command.UserSpotCreate
                ? operation.Create.SourceNodeGeneration
                : operation.Close.SourceNodeGeneration;
        if (sourceNodeRid != _routingId
            || sourceNodeGeneration != _lifecycleGeneration)
            throw new ArgumentException(
                "The operation source must match this MeshNode.",
                nameof(operation));
        Peer? peer;
        lock (_gate)
            _peersByRid.TryGetValue(targetNodeRid, out peer);
        if (peer is null || !peer.Admitted)
            return SubmitResult.NotConnected;
        var head = operation.Command
            == ServiceWireConstants.Command.UserSpotCreate
                ? ZLinkServiceWireCodec.EncodeUserSpotCreate(operation.Create)
                : ZLinkServiceWireCodec.EncodeUserSpotClose(operation.Close);
        return TrySend(peer.PhysicalRoutingId, [head], SendFlags.None)
            ? SubmitResult.Ok
            : SubmitResult.Backpressured;
    }

    internal int RetainedUserSpotOperationCount =>
        _remoteUserSpotOperations.Count;

    internal SubmitResult ResubmitActorCreateOperation(
        RoutingId targetNodeRid,
        ZLinkServiceWireCodec.ActorCreateOperationRecord record)
    {
        var operation = record.Operation;
        if (operation.SourceNodeRid != _routingId
            || operation.SourceNodeGeneration != _lifecycleGeneration)
            throw new ArgumentException(
                "The operation source must match this MeshNode.",
                nameof(record));
        Peer? peer;
        lock (_gate)
            _peersByRid.TryGetValue(targetNodeRid, out peer);
        if (peer is null || !peer.Admitted)
            return SubmitResult.NotConnected;
        var head = ZLinkServiceWireCodec.EncodeActorCreate(operation);
        return TrySend(peer.PhysicalRoutingId, [head], SendFlags.None)
            ? SubmitResult.Ok
            : SubmitResult.Backpressured;
    }

    internal int RetainedActorCreateOperationCount =>
        _remoteActorCreateOperations.Count;

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
        // Records may be queued while the node starts, before the framework
        // installs its pull-dispatch pump. Such an enqueue marks ready as
        // posted even though no callback existed. Re-arm the edge when the
        // handler is installed so those records and all later completions can
        // be drained.
        Volatile.Write(ref _readyPosted, 0);
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
                        entry.Key.SpotId,
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
        var spotId = Guid.NewGuid().ToString("D");
        return _spots.GetOrAdd(
            spotId,
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
            _routingId.ToString(),
            value => new ZLinkManagedSpot(
                this,
                value,
                Interlocked.Increment(ref _nextSpotGeneration),
                NextAuthorityOwnerGeneration()));
    }

    public ISpot GetOrCreateSpot(string spotId, out bool created)
    {
        ZLinkSpotId.Require(spotId, nameof(spotId));
        if (_spots.TryGetValue(spotId, out var existing))
        {
            created = false;
            return existing;
        }

        var candidate = new ZLinkManagedSpot(
            this,
            spotId,
            Interlocked.Increment(ref _nextSpotGeneration),
            NextAuthorityOwnerGeneration());
        var spot = _spots.GetOrAdd(spotId, candidate);
        created = ReferenceEquals(spot, candidate);
        if (!created)
            candidate.Dispose();
        return spot;
    }

    public ISpot GetOrCreateReservedSpot(
        string spotId,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        out bool created)
    {
        ZLinkSpotId.Require(spotId, nameof(spotId));
        if (objectGeneration == 0 || objectGeneration > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(objectGeneration));
        if (authorityOwnerGeneration == 0 || authorityOwnerGeneration > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(authorityOwnerGeneration));
        if (_spots.TryGetValue(spotId, out var existing))
        {
            created = false;
            return existing;
        }

        ulong observed;
        do
        {
            observed = Volatile.Read(ref _nextSpotGeneration);
            if (observed >= objectGeneration) break;
        } while (Interlocked.CompareExchange(
                     ref _nextSpotGeneration,
                     objectGeneration,
                     observed) != observed);

        var candidate = new ZLinkManagedSpot(
            this,
            spotId,
            objectGeneration,
            authorityOwnerGeneration);
        var spot = _spots.GetOrAdd(spotId, candidate);
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
        var generation = Interlocked.Increment(ref _nextActorGeneration);
        return CreateActorCore(
            actorId,
            checked((ulong)generation),
            creationParts,
            timeout);
    }

    public ActorRef CreateReservedActor(
        string actorId,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message>? creationParts = null,
        TimeSpan timeout = default)
    {
        if (objectGeneration == 0 || objectGeneration > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(objectGeneration));
        if (authorityOwnerGeneration == 0 || authorityOwnerGeneration > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(authorityOwnerGeneration));
        ulong observed;
        do
        {
            observed = Volatile.Read(ref _nextActorGeneration);
            if (observed >= objectGeneration) break;
        } while (Interlocked.CompareExchange(
                     ref _nextActorGeneration,
                     objectGeneration,
                     observed) != observed);
        return CreateActorCore(
            actorId,
            objectGeneration,
            creationParts,
            timeout,
            authorityOwnerGeneration);
    }

    private ActorRef CreateActorCore(
        string actorId,
        ulong generation,
        IReadOnlyList<Message>? creationParts,
        TimeSpan timeout,
        ulong? reservedAuthorityOwnerGeneration = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        if (Encoding.UTF8.GetByteCount(actorId) > byte.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(actorId));
        ThrowIfDisposed();

        var entry = (ZLinkManagedSpot)EntrySpot();
        if (generation == 0 || generation > long.MaxValue)
            throw new InvalidOperationException("The Actor generation space was exhausted.");
        var actorRef = new ActorRef(_routingId, actorId, generation);
        var actor = new ManagedActor(
            actorRef,
            entry.SpotId,
            entry.LifecycleGeneration,
            membershipEpoch: 1,
            reservedAuthorityOwnerGeneration ?? NextAuthorityOwnerGeneration());
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

    public void SetActorAuthority(
        ActorRef actor,
        ulong authorityOwnerGeneration)
    {
        if (authorityOwnerGeneration == 0
            || !TryGetActor(actor, out var current))
            throw new InvalidOperationException(
                $"Actor '{actor.ActorId}' does not match the local authority update.");
        current.SetAuthorityOwnerGeneration(authorityOwnerGeneration);
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
        string targetSpotId,
        ulong objectGeneration,
        ulong authorityOwnerGeneration)
    {
        ValidateObservedAuthority(
            targetNodeRid,
            objectGeneration,
            authorityOwnerGeneration);
        if (string.IsNullOrEmpty(targetSpotId))
            throw new ArgumentException(
                "The observed Spot routing id is required.",
                nameof(targetSpotId));
        _observedSpotAuthorities[
            new ObservedSpotAuthorityKey(
                targetNodeRid,
                targetSpotId,
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
        if (_spots.TryGetValue(current.SpotId, out var spot))
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
        string targetSpotId,
        ulong targetSpotGeneration,
        IReadOnlyList<Message>? creationParts = null,
        TimeSpan timeout = default) =>
        BeginJoin(
            actor,
            targetNodeRid,
            targetSpotId,
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
                ? ((ZLinkManagedSpot)EntrySpot()).SpotId
                : throw new InvalidOperationException(
                    "Remote Entry Spot joins require the descriptor EntrySpotId mapping."),
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

    internal void Publish(
        string sourceSpotId,
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
        foreach (var spot in localTargets)
        {
            var retained = CloneParts(parts);
            EnqueueOwned(
                MailboxKey.ForSpot(spot, MeshReadyDomains.Application),
                new MeshReceiveRecord(
                    MeshRecordKind.SpotMulticast,
                    MeshReadyDomains.Application,
                    _routingId,
                    sourceSpotId,
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
        }

        var targets = SnapshotChannelTargets(channelName)
            .Where(target => target.RoutingId != _routingId)
            .Select(static target => target.RoutingId)
            .ToList();
        foreach (var target in targets)
        {
            _ = SubmitApplication(
                target,
                ServiceWireConstants.Command.ChannelSend,
                0,
                channelName,
                parts,
                flags,
                metadata);
        }
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
        _remoteUserSpotOperations.Clear();
        _remoteActorCreateOperations.Clear();
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
        string sourceSpotId,
        RoutingId targetRid,
        string spotId,
        ulong spotGeneration,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata) =>
        SubmitSpot(
            targetRid,
            sourceSpotId,
            spotId,
            spotGeneration,
            parts,
            request: false,
            default,
            flags,
            metadata);

    internal SubmitResult RequestToSpot(
        string sourceSpotId,
        RoutingId targetRid,
        string spotId,
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
            sourceSpotId,
            spotId,
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
        if (spot.ActorCount != 0)
            return;
        _spots.TryRemove(
            new KeyValuePair<string, ZLinkManagedSpot>(
                spot.SpotId,
                spot));
    }

    internal void RekeySpot(
        ZLinkManagedSpot spot,
        string previousSpotId,
        string currentSpotId)
    {
        if (string.Equals(previousSpotId, currentSpotId, StringComparison.Ordinal))
            return;
        if (!_spots.TryRemove(
                new KeyValuePair<string, ZLinkManagedSpot>(previousSpotId, spot))
            || !_spots.TryAdd(currentSpotId, spot))
            throw new InvalidOperationException(
                $"Managed Spot ID '{currentSpotId}' is already registered.");
    }

    private MeshOperationId BeginJoin(
        ActorRef actorRef,
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        bool entry,
        IReadOnlyList<Message>? requestParts,
        TimeSpan timeout)
    {
        var operation = BeginOperation(MeshOperationKind.ActorJoin, timeout);
        if (targetNodeRid != _routingId
            || !TryGetActor(actorRef, out var actor)
            || !_spots.TryGetValue(targetSpotId, out var targetSpot)
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
            previous.SpotId,
            targetSpot.SpotId,
            previous.SpotGeneration,
            targetSpot.LifecycleGeneration,
            previous.MembershipEpoch,
            checked(previous.MembershipEpoch + 1),
            0);
        var record = new MeshReceiveRecord(
            MeshRecordKind.SpotControl,
            MeshReadyDomains.Application,
            _routingId,
            previous.SpotId,
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
                targetSpot.SpotId,
                targetSpot.LifecycleGeneration))
        {
            CompleteManagedOperation(
                operation,
                RequestResult.Conflict,
                1,
                Array.Empty<Message>());
            return SubmitResult.Ok;
        }

        if (_spots.TryGetValue(previous.SpotId, out var oldSpot)
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
        if (!string.Equals(previous.SpotId, targetSpot.SpotId, StringComparison.Ordinal))
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
        string sourceSpotId,
        string targetSpotId,
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
                        targetSpotId,
                        targetSpotGeneration),
                    out var authorityOwnerGeneration))
                return SubmitResult.NotFound;
            var head = ZLinkServiceWireCodec.EncodeSpot(
                request
                    ? ServiceWireConstants.Command.SpotRequest
                    : ServiceWireConstants.Command.SpotSend,
                operation?.OperationId.Low ?? 0,
                sourceSpotId,
                targetSpotId,
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
        if (!_spots.TryGetValue(targetSpotId, out var spot))
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
                transfer.Actor.SpotId,
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
            previous.SpotId,
            current.SpotId,
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
                previous.SpotId,
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
        if (ZLinkServiceWireCodec.TryDecodeInstanceSpotActivation(
                head,
                out var instanceActivation,
                out _))
        {
            ProcessInstanceSpotActivation(sourceRid, instanceActivation, received);
            return;
        }
        if (ZLinkServiceWireCodec.TryDecodeUserSpotOperation(
                head,
                out var userSpotOperation,
                out _))
        {
            ProcessUserSpotOperation(sourceRid, userSpotOperation);
            return;
        }
        if (ZLinkServiceWireCodec.TryDecodeActorCreateOperation(
                head,
                out var actorCreateOperation,
                out _))
        {
            ProcessActorCreateOperation(sourceRid, actorCreateOperation);
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

    private void ProcessInstanceSpotActivation(
        RoutingId sourceRid,
        ZLinkServiceWireCodec.InstanceSpotActivationRecord record,
        Received received)
    {
        Peer? peer;
        IInstanceSpotActivationTarget? target;
        lock (_gate)
        {
            _peersByRid.TryGetValue(sourceRid, out peer);
            target = _instanceSpotActivationTarget;
        }

        var operation = record.Operation;
        var request = operation.IsRequest;
        if (peer is null
            || !peer.Admitted
            || operation.SourceNodeRid != sourceRid
            || operation.SourceNodeGeneration != peer.LifecycleGeneration
            || operation.Target.TargetNodeRid != _routingId
            || operation.Target.TargetNodeGeneration != _lifecycleGeneration)
        {
            if (request)
                SendTerminalReply(
                    sourceRid,
                    operation.ReplyRouteId,
                    RequestResult.Conflict,
                    (uint)ServiceWireConstants.FrameworkErrorCode.SpotGenerationStale,
                    Array.Empty<Message>(),
                    SendFlags.DontWait);
            return;
        }

        var payloadOffset = record.HasMetadata ? 2 : 1;
        if (received.Parts.Count <= payloadOffset || target is null)
        {
            if (request)
                SendTerminalReply(
                    sourceRid,
                    operation.ReplyRouteId,
                    target is null ? RequestResult.InvalidState : RequestResult.ProtocolError,
                    (uint)(target is null
                        ? ServiceWireConstants.FrameworkErrorCode.RequestFailed
                        : ServiceWireConstants.FrameworkErrorCode.RequestProtocolError),
                    Array.Empty<Message>(),
                    SendFlags.DontWait);
            return;
        }

        var metadata = record.HasMetadata
            ? received.Parts[1].ToArray()
            : (ReadOnlyMemory<byte>?)null;
        var payload = received.Parts
            .Skip(payloadOffset)
            .Select(static part => (ReadOnlyMemory<byte>)part.ToArray())
            .ToArray();
        _ = CompleteInstanceSpotActivationAsync(
            sourceRid, operation, target, metadata, payload);
    }

    private async Task CompleteInstanceSpotActivationAsync(
        RoutingId sourceRid,
        InstanceSpotActivationOperation operation,
        IInstanceSpotActivationTarget target,
        ReadOnlyMemory<byte>? metadata,
        IReadOnlyList<ReadOnlyMemory<byte>> payload)
    {
        var remaining = checked((long)operation.DeadlineUnixMs)
                        - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        InstanceSpotActivationTerminal terminal;
        if (remaining <= 0)
        {
            terminal = new InstanceSpotActivationTerminal(
                RequestResult.TimedOut,
                ServiceWireConstants.FrameworkErrorCode.WorkerTimedOut,
                Array.Empty<ReadOnlyMemory<byte>>());
        }
        else
        {
            using var deadline = CancellationTokenSource.CreateLinkedTokenSource(
                _stop?.Token ?? CancellationToken.None);
            deadline.CancelAfter(TimeSpan.FromMilliseconds(remaining));
            try
            {
                terminal = await target.ActivateAsync(
                        operation, metadata, payload, deadline.Token)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (deadline.IsCancellationRequested)
            {
                terminal = new InstanceSpotActivationTerminal(
                    RequestResult.TimedOut,
                    ServiceWireConstants.FrameworkErrorCode.WorkerTimedOut,
                    Array.Empty<ReadOnlyMemory<byte>>());
            }
            catch
            {
                terminal = new InstanceSpotActivationTerminal(
                    RequestResult.InternalError,
                    ServiceWireConstants.FrameworkErrorCode.RequestFailed,
                    Array.Empty<ReadOnlyMemory<byte>>());
            }
        }

        if (!operation.IsRequest) return;
        var reply = terminal.ReplyParts.Select(Message.From).ToArray();
        try
        {
            SendTerminalReply(
                sourceRid,
                operation.ReplyRouteId,
                terminal.Result,
                (uint)terminal.FailureCode,
                reply,
                SendFlags.DontWait);
        }
        finally
        {
            DisposeParts(reply);
        }
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
        string sourceSpotId = stateful.SourceSpotId;
        if (stateful.Command is ServiceWireConstants.Command.SpotSend
            or ServiceWireConstants.Command.SpotRequest)
        {
            if (!_spots.TryGetValue(stateful.TargetSpotId, out var spot)
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
                sourceSpotId,
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

    private void ProcessUserSpotOperation(
        RoutingId sourceRid,
        ZLinkServiceWireCodec.UserSpotOperationRecord record)
    {
        Peer? peer;
        IUserSpotOperationTarget? target;
        lock (_gate)
        {
            _peersByRid.TryGetValue(sourceRid, out peer);
            target = _userSpotOperationTarget;
        }

        var operation = record.Command == ServiceWireConstants.Command.UserSpotCreate
            ? record.Create.OperationId
            : record.Close.OperationId;
        var correlation = record.Command == ServiceWireConstants.Command.UserSpotCreate
            ? record.Create.Correlation
            : record.Close.Correlation;
        var sourceNodeRid = record.Command == ServiceWireConstants.Command.UserSpotCreate
            ? record.Create.SourceNodeRid
            : record.Close.SourceNodeRid;
        var sourceNodeGeneration =
            record.Command == ServiceWireConstants.Command.UserSpotCreate
                ? record.Create.SourceNodeGeneration
                : record.Close.SourceNodeGeneration;
        var deadlineUnixMs = record.Command == ServiceWireConstants.Command.UserSpotCreate
            ? record.Create.DeadlineUnixMs
            : record.Close.DeadlineUnixMs;
        var targetNodeRid = record.Command == ServiceWireConstants.Command.UserSpotCreate
            ? record.Create.Reservation.TargetNodeRid
            : record.Close.Target.TargetNodeRid;
        var targetNodeGeneration =
            record.Command == ServiceWireConstants.Command.UserSpotCreate
                ? record.Create.Reservation.TargetNodeGeneration
                : record.Close.Target.TargetNodeGeneration;

        if (peer is null
            || !peer.Admitted
            || sourceNodeRid != sourceRid
            || sourceNodeGeneration != peer.LifecycleGeneration)
        {
            SendUserSpotFailure(
                sourceRid,
                correlation,
                record.Command,
                RequestResult.Conflict,
                ServiceWireConstants.FrameworkErrorCode.RequestProtocolError);
            return;
        }
        if (targetNodeRid != _routingId
            || targetNodeGeneration != _lifecycleGeneration)
        {
            SendUserSpotFailure(
                sourceRid,
                correlation,
                record.Command,
                RequestResult.Conflict,
                ServiceWireConstants.FrameworkErrorCode.SpotGenerationStale);
            return;
        }
        var key = new RemoteUserSpotOperationKey(
            sourceRid,
            sourceNodeGeneration,
            operation);
        if (_remoteUserSpotOperations.TryGetValue(key, out var retained))
        {
            if (!SameUserSpotOperation(retained.Record, record))
            {
                SendUserSpotFailure(
                    sourceRid,
                    correlation,
                    record.Command,
                    RequestResult.ProtocolError,
                    ServiceWireConstants.FrameworkErrorCode.RequestProtocolError);
                return;
            }
            _ = ReplyUserSpotOperationAsync(
                sourceNodeRid,
                correlation,
                record.Command,
                key,
                retained);
            return;
        }
        if (deadlineUnixMs <= checked((ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()))
        {
            SendUserSpotFailure(
                sourceRid,
                correlation,
                record.Command,
                RequestResult.TimedOut,
                ServiceWireConstants.FrameworkErrorCode.WorkerTimedOut);
            return;
        }
        if (target is null)
        {
            SendUserSpotFailure(
                sourceRid,
                correlation,
                record.Command,
                RequestResult.InvalidState,
                ServiceWireConstants.FrameworkErrorCode.RequestFailed);
            return;
        }

        RemoteUserSpotInvocation invocation;
        var candidate = new RemoteUserSpotInvocation(
            record,
            () => ExecuteUserSpotOperationAsync(target, record, deadlineUnixMs));
        lock (_remoteUserSpotGate)
        {
            if (!_remoteUserSpotOperations.TryGetValue(key, out invocation!))
            {
                if (_remoteUserSpotOperations.Count
                    >= MaxRemoteUserSpotOperations)
                {
                    SendUserSpotFailure(
                        sourceRid,
                        correlation,
                        record.Command,
                        RequestResult.Busy,
                        ServiceWireConstants.FrameworkErrorCode.WorkerQueueFull);
                    return;
                }
                _remoteUserSpotOperations[key] = candidate;
                invocation = candidate;
            }
        }
        if (!ReferenceEquals(invocation, candidate))
        {
            if (!SameUserSpotOperation(invocation.Record, record))
            {
                SendUserSpotFailure(
                    sourceRid,
                    correlation,
                    record.Command,
                    RequestResult.ProtocolError,
                    ServiceWireConstants.FrameworkErrorCode.RequestProtocolError);
                return;
            }
        }

        _ = ReplyUserSpotOperationAsync(
            sourceNodeRid, correlation, record.Command, key, invocation);
    }

    private async Task<UserSpotOperationTerminal> ExecuteUserSpotOperationAsync(
        IUserSpotOperationTarget target,
        ZLinkServiceWireCodec.UserSpotOperationRecord record,
        ulong deadlineUnixMs)
    {
        var remaining = checked((long)deadlineUnixMs)
                        - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (remaining <= 0)
            return new UserSpotOperationTerminal(
                RequestResult.TimedOut,
                ServiceWireConstants.FrameworkErrorCode.WorkerTimedOut);

        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(
            _stop?.Token ?? CancellationToken.None);
        deadline.CancelAfter(TimeSpan.FromMilliseconds(remaining));
        try
        {
            var terminal = record.Command == ServiceWireConstants.Command.UserSpotCreate
                ? await target.CreateAsync(record.Create, deadline.Token).ConfigureAwait(false)
                : await target.CloseAsync(record.Close, deadline.Token).ConfigureAwait(false);
            return ValidateUserSpotTerminal(record, terminal);
        }
        catch (OperationCanceledException) when (deadline.IsCancellationRequested)
        {
            return new UserSpotOperationTerminal(
                RequestResult.TimedOut,
                ServiceWireConstants.FrameworkErrorCode.WorkerTimedOut);
        }
        catch (ZLinkFrameworkException exception)
        {
            return MapUserSpotException(exception);
        }
        catch
        {
            return new UserSpotOperationTerminal(
                RequestResult.InternalError,
                ServiceWireConstants.FrameworkErrorCode.RequestFailed);
        }
    }

    private static bool SameUserSpotOperation(
        ZLinkServiceWireCodec.UserSpotOperationRecord left,
        ZLinkServiceWireCodec.UserSpotOperationRecord right)
    {
        if (left.Command != right.Command) return false;
        return left.Command == ServiceWireConstants.Command.UserSpotCreate
            ? left.Create with { Correlation = 0 }
                == right.Create with { Correlation = 0 }
            : left.Close with { Correlation = 0 }
                == right.Close with { Correlation = 0 };
    }

    private static UserSpotOperationTerminal ValidateUserSpotTerminal(
        ZLinkServiceWireCodec.UserSpotOperationRecord operation,
        UserSpotOperationTerminal terminal)
    {
        if (terminal.Result != RequestResult.Ok)
        {
            if (terminal.Completion is not null
                || terminal.FailureCode == ServiceWireConstants.FrameworkErrorCode.None)
                throw new InvalidOperationException(
                    "A failed User Spot operation requires one failure code and no success completion.");
            if (terminal.FailureCode is ServiceWireConstants.FrameworkErrorCode.SpotGenerationStale
                or ServiceWireConstants.FrameworkErrorCode.SpotMoving
                && terminal.Result != RequestResult.Conflict)
                throw new InvalidOperationException(
                    "Stale-generation and moving failures map to conflict.");
            return terminal;
        }
        if (terminal.FailureCode != ServiceWireConstants.FrameworkErrorCode.None)
            throw new InvalidOperationException(
                "A successful User Spot operation cannot carry a failure code.");

        if (operation.Command == ServiceWireConstants.Command.UserSpotCreate)
        {
            if (terminal.Completion is not UserSpotCreateCompletion create
                || create.SpotId != operation.Create.SpotId
                || create.ObjectGeneration != operation.Create.Reservation.ObjectGeneration)
                throw new InvalidOperationException(
                    "The User Spot create completion does not match its reservation.");
        }
        else if (terminal.Completion is not UserSpotCloseCompletion)
        {
            throw new InvalidOperationException(
                "The User Spot close completion is missing.");
        }
        return terminal;
    }

    private async Task ReplyUserSpotOperationAsync(
        RoutingId sourceRid,
        ulong correlation,
        ServiceWireConstants.Command command,
        RemoteUserSpotOperationKey key,
        RemoteUserSpotInvocation invocation)
    {
        var terminal = await invocation.Task.ConfigureAwait(false);
        var head = command == ServiceWireConstants.Command.UserSpotCreate
            ? ZLinkServiceWireCodec.EncodeUserSpotCreateReply(
                correlation,
                terminal.Result,
                terminal.FailureCode,
                terminal.Completion as UserSpotCreateCompletion)
            : ZLinkServiceWireCodec.EncodeUserSpotCloseReply(
                correlation,
                terminal.Result,
                terminal.FailureCode,
                terminal.Completion as UserSpotCloseCompletion);
        var replyParts = ReencodeActorCreateReply(
            terminal.ReplyParts,
            correlation);
        var wire = new List<ReadOnlyMemory<byte>>(replyParts.Count + 1) { head };
        wire.AddRange(replyParts);
        await SendServiceTerminalAsync(sourceRid, wire).ConfigureAwait(false);
        _ = ExpireRemoteUserSpotOperationAsync(key, invocation);
    }

    private static IReadOnlyList<ReadOnlyMemory<byte>> ReencodeActorCreateReply(
        IReadOnlyList<ReadOnlyMemory<byte>>? replyParts,
        ulong correlation)
    {
        if (replyParts is not { Count: > 0 })
            return Array.Empty<ReadOnlyMemory<byte>>();
        var messages = replyParts
            .Select(static part => Message.From(part.Span))
            .ToArray();
        try
        {
            ZLinkEnvelopeHeader header;
            try
            {
                header = ZLinkEnvelopeCodec.DecodeHeader(messages);
            }
            catch (ZLinkEnvelopeProtocolException)
            {
                // Raw terminal payloads carry no envelope header, so there is no
                // correlation to rewrite. They travel unchanged.
                return replyParts;
            }
            using var encodedHeader = ZLinkEnvelopeCodec.EncodeHeader(
                header with
                {
                    CorrelationId = correlation.ToString(
                        System.Globalization.CultureInfo.InvariantCulture)
                });
            var result = new ReadOnlyMemory<byte>[replyParts.Count];
            result[0] = encodedHeader.AsReadOnlyMemory().ToArray();
            for (var i = 1; i < replyParts.Count; i++)
                result[i] = replyParts[i];
            return result;
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(messages);
        }
    }

    private async Task SendServiceTerminalAsync(
        RoutingId sourceRid,
        IReadOnlyList<ReadOnlyMemory<byte>> wire)
    {
        var stopToken = _stop?.Token ?? CancellationToken.None;
        for (var attempt = 0;
             attempt < 5000 && !stopToken.IsCancellationRequested;
             attempt++)
        {
            RoutingId target;
            lock (_gate)
                target = _peersByRid.TryGetValue(sourceRid, out var peer)
                    && peer.Admitted
                    ? peer.PhysicalRoutingId
                    : sourceRid;
            if (TrySend(target, wire, SendFlags.DontWait)) return;
            try
            {
                await Task.Delay(TimeSpan.FromMilliseconds(1), stopToken)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }
    }

    private async Task ExpireRemoteUserSpotOperationAsync(
        RemoteUserSpotOperationKey key,
        RemoteUserSpotInvocation invocation)
    {
        try
        {
            var deadline = invocation.Record.Command
                == ServiceWireConstants.Command.UserSpotCreate
                    ? invocation.Record.Create.DeadlineUnixMs
                    : invocation.Record.Close.DeadlineUnixMs;
            var retentionDeadline = checked(
                (long)deadline
                + (long)_remoteUserSpotTerminalRetention.TotalMilliseconds);
            var remaining = Math.Max(
                0,
                retentionDeadline - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
            await Task.Delay(
                    TimeSpan.FromMilliseconds(remaining),
                    _stop?.Token ?? CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        _remoteUserSpotOperations.TryRemove(
            new KeyValuePair<RemoteUserSpotOperationKey, RemoteUserSpotInvocation>(
                key, invocation));
    }

    private void SendUserSpotFailure(
        RoutingId sourceRid,
        ulong correlation,
        ServiceWireConstants.Command command,
        RequestResult result,
        ServiceWireConstants.FrameworkErrorCode failureCode)
    {
        var head = command == ServiceWireConstants.Command.UserSpotCreate
            ? ZLinkServiceWireCodec.EncodeUserSpotCreateReply(
                correlation,
                result,
                failureCode,
                null)
            : ZLinkServiceWireCodec.EncodeUserSpotCloseReply(
                correlation,
                result,
                failureCode,
                null);
        _ = TrySend(sourceRid, [head], SendFlags.DontWait);
    }

    private static UserSpotOperationTerminal MapUserSpotException(
        ZLinkFrameworkException exception)
    {
        return exception.Kind switch
        {
            ZLinkFrameworkErrorKind.SpotGenerationStale =>
                new UserSpotOperationTerminal(
                    RequestResult.Conflict,
                    ServiceWireConstants.FrameworkErrorCode.SpotGenerationStale),
            ZLinkFrameworkErrorKind.SpotMoving =>
                new UserSpotOperationTerminal(
                    RequestResult.Conflict,
                    ServiceWireConstants.FrameworkErrorCode.SpotMoving),
            ZLinkFrameworkErrorKind.SpotTypeMismatch =>
                new UserSpotOperationTerminal(
                    RequestResult.Conflict,
                    ServiceWireConstants.FrameworkErrorCode.SpotTypeMismatch),
            ZLinkFrameworkErrorKind.SpotIdConflict =>
                new UserSpotOperationTerminal(
                    RequestResult.Conflict,
                    ServiceWireConstants.FrameworkErrorCode.SpotCreateFailed),
            ZLinkFrameworkErrorKind.PlacementCapacityExhausted =>
                new UserSpotOperationTerminal(
                    RequestResult.Busy,
                    ServiceWireConstants.FrameworkErrorCode.WorkerQueueFull),
            ZLinkFrameworkErrorKind.RequestProtocolError =>
                new UserSpotOperationTerminal(
                    RequestResult.ProtocolError,
                    ServiceWireConstants.FrameworkErrorCode.RequestProtocolError),
            _ => new UserSpotOperationTerminal(
                exception.IsRetriable ? RequestResult.Busy : RequestResult.InternalError,
                ServiceWireConstants.FrameworkErrorCode.RequestFailed)
        };
    }

    private void ProcessActorCreateOperation(
        RoutingId sourceRid,
        ZLinkServiceWireCodec.ActorCreateOperationRecord record)
    {
        Peer? peer;
        IActorCreateOperationTarget? target;
        lock (_gate)
        {
            _peersByRid.TryGetValue(sourceRid, out peer);
            target = _actorCreateOperationTarget;
        }

        var operation = record.Operation;
        if (peer is null
            || !peer.Admitted
            || operation.SourceNodeRid != sourceRid
            || operation.SourceNodeGeneration != peer.LifecycleGeneration)
        {
            SendActorCreateFailure(
                sourceRid,
                operation.Correlation,
                RequestResult.Conflict,
                ServiceWireConstants.FrameworkErrorCode.RequestProtocolError);
            return;
        }
        if (operation.Reservation.TargetNodeRid != _routingId
            || operation.Reservation.TargetNodeGeneration != _lifecycleGeneration)
        {
            SendActorCreateFailure(
                sourceRid,
                operation.Correlation,
                RequestResult.Conflict,
                ServiceWireConstants.FrameworkErrorCode.ActorLocationStale);
            return;
        }

        var key = new RemoteActorCreateOperationKey(
            sourceRid,
            operation.SourceNodeGeneration,
            operation.OperationId);
        if (_remoteActorCreateOperations.TryGetValue(key, out var retained))
        {
            if (!SameActorCreateOperation(retained.Record, record))
            {
                SendActorCreateFailure(
                    sourceRid,
                    operation.Correlation,
                    RequestResult.ProtocolError,
                    ServiceWireConstants.FrameworkErrorCode.RequestProtocolError);
                return;
            }
            _ = ReplyActorCreateOperationAsync(
                operation.SourceNodeRid,
                operation.Correlation,
                key,
                retained);
            return;
        }
        if (operation.DeadlineUnixMs
            <= checked((ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()))
        {
            SendActorCreateFailure(
                sourceRid,
                operation.Correlation,
                RequestResult.TimedOut,
                ServiceWireConstants.FrameworkErrorCode.WorkerTimedOut);
            return;
        }
        if (target is null)
        {
            SendActorCreateFailure(
                sourceRid,
                operation.Correlation,
                RequestResult.InvalidState,
                ServiceWireConstants.FrameworkErrorCode.RequestFailed);
            return;
        }

        RemoteActorCreateInvocation invocation;
        var candidate = new RemoteActorCreateInvocation(
            record,
            () => ExecuteActorCreateOperationAsync(
                target,
                operation,
                operation.DeadlineUnixMs));
        lock (_remoteActorCreateGate)
        {
            if (!_remoteActorCreateOperations.TryGetValue(key, out invocation!))
            {
                if (_remoteActorCreateOperations.Count
                    >= MaxRemoteActorCreateOperations)
                {
                    SendActorCreateFailure(
                        sourceRid,
                        operation.Correlation,
                        RequestResult.Busy,
                        ServiceWireConstants.FrameworkErrorCode.WorkerQueueFull);
                    return;
                }
                _remoteActorCreateOperations[key] = candidate;
                invocation = candidate;
            }
        }
        if (!ReferenceEquals(invocation, candidate)
            && !SameActorCreateOperation(invocation.Record, record))
        {
            SendActorCreateFailure(
                sourceRid,
                operation.Correlation,
                RequestResult.ProtocolError,
                ServiceWireConstants.FrameworkErrorCode.RequestProtocolError);
            return;
        }

        _ = ReplyActorCreateOperationAsync(
            operation.SourceNodeRid,
            operation.Correlation,
            key,
            invocation);
    }

    private async Task<ActorCreateOperationTerminal> ExecuteActorCreateOperationAsync(
        IActorCreateOperationTarget target,
        ActorCreateOperation operation,
        ulong deadlineUnixMs)
    {
        var remaining = checked((long)deadlineUnixMs)
                        - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (remaining <= 0)
            return new ActorCreateOperationTerminal(
                RequestResult.TimedOut,
                ServiceWireConstants.FrameworkErrorCode.WorkerTimedOut);

        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(
            _stop?.Token ?? CancellationToken.None);
        deadline.CancelAfter(TimeSpan.FromMilliseconds(remaining));
        try
        {
            var terminal = await target.CreateAsync(operation, deadline.Token)
                .ConfigureAwait(false);
            return ValidateActorCreateTerminal(operation, terminal);
        }
        catch (OperationCanceledException) when (deadline.IsCancellationRequested)
        {
            return new ActorCreateOperationTerminal(
                RequestResult.TimedOut,
                ServiceWireConstants.FrameworkErrorCode.WorkerTimedOut);
        }
        catch (ZLinkFrameworkException exception)
        {
            return MapActorCreateException(exception);
        }
        catch
        {
            return new ActorCreateOperationTerminal(
                RequestResult.InternalError,
                ServiceWireConstants.FrameworkErrorCode.ActorCreateFailed);
        }
    }

    private static bool SameActorCreateOperation(
        ZLinkServiceWireCodec.ActorCreateOperationRecord left,
        ZLinkServiceWireCodec.ActorCreateOperationRecord right) =>
        left.Operation with { Correlation = 0 }
        == right.Operation with { Correlation = 0 };

    private static ActorCreateOperationTerminal ValidateActorCreateTerminal(
        ActorCreateOperation operation,
        ActorCreateOperationTerminal terminal)
    {
        if (terminal.Result != RequestResult.Ok)
        {
            if (terminal.Completion is not null
                || terminal.FailureCode == ServiceWireConstants.FrameworkErrorCode.None)
                throw new InvalidOperationException(
                    "A failed Actor create operation requires one failure code and no success completion.");
            return terminal;
        }
        if (terminal.FailureCode != ServiceWireConstants.FrameworkErrorCode.None
            || terminal.Completion is not { } completion)
            throw new InvalidOperationException(
                "A successful Actor create operation requires one completion and no failure code.");

        if (completion.Result is ActorCreateResult.Existing
            or ActorCreateResult.Created)
        {
            if (completion.Actor.ActorId != operation.ActorId
                || completion.Actor.Generation
                    != operation.Reservation.ObjectGeneration
                || completion.Actor.NodeRid
                    != operation.Reservation.TargetNodeRid)
                throw new InvalidOperationException(
                    "The Actor create completion does not match its reservation.");
        }
        else if (!string.IsNullOrEmpty(completion.Actor.ActorId)
                 || completion.Actor.Generation != 0
                 || !completion.Actor.NodeRid.IsEmpty)
        {
            throw new InvalidOperationException(
                "A rejected Actor create completion cannot select an Actor.");
        }
        return terminal;
    }

    private async Task ReplyActorCreateOperationAsync(
        RoutingId sourceRid,
        ulong correlation,
        RemoteActorCreateOperationKey key,
        RemoteActorCreateInvocation invocation)
    {
        var terminal = await invocation.Task.ConfigureAwait(false);
        var head = ZLinkServiceWireCodec.EncodeActorCreateReply(
            correlation,
            terminal.Result,
            terminal.FailureCode,
            terminal.Completion);
        var replyParts = terminal.ReplyParts ?? Array.Empty<ReadOnlyMemory<byte>>();
        var wire = new List<ReadOnlyMemory<byte>>(replyParts.Count + 1) { head };
        wire.AddRange(replyParts);
        await SendServiceTerminalAsync(sourceRid, wire).ConfigureAwait(false);
        _ = ExpireRemoteActorCreateOperationAsync(key, invocation);
    }

    private async Task ExpireRemoteActorCreateOperationAsync(
        RemoteActorCreateOperationKey key,
        RemoteActorCreateInvocation invocation)
    {
        try
        {
            var retentionDeadline = checked(
                (long)invocation.Record.Operation.DeadlineUnixMs
                + (long)_remoteUserSpotTerminalRetention.TotalMilliseconds);
            var remaining = Math.Max(
                0,
                retentionDeadline - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
            await Task.Delay(
                    TimeSpan.FromMilliseconds(remaining),
                    _stop?.Token ?? CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            return;
        }
        _remoteActorCreateOperations.TryRemove(
            new KeyValuePair<RemoteActorCreateOperationKey, RemoteActorCreateInvocation>(
                key,
                invocation));
    }

    private void SendActorCreateFailure(
        RoutingId sourceRid,
        ulong correlation,
        RequestResult result,
        ServiceWireConstants.FrameworkErrorCode failureCode)
    {
        var head = ZLinkServiceWireCodec.EncodeActorCreateReply(
            correlation,
            result,
            failureCode,
            null);
        _ = TrySend(sourceRid, [head], SendFlags.DontWait);
    }

    private static ActorCreateOperationTerminal MapActorCreateException(
        ZLinkFrameworkException exception)
    {
        return exception.Kind switch
        {
            ZLinkFrameworkErrorKind.ActorTypeMismatch =>
                new ActorCreateOperationTerminal(
                    RequestResult.Conflict,
                    ServiceWireConstants.FrameworkErrorCode.ActorTypeMismatch),
            ZLinkFrameworkErrorKind.ActorAlreadyExists =>
                new ActorCreateOperationTerminal(
                    RequestResult.Conflict,
                    ServiceWireConstants.FrameworkErrorCode.ActorAlreadyExists),
            ZLinkFrameworkErrorKind.PlacementCapacityExhausted =>
                new ActorCreateOperationTerminal(
                    RequestResult.Busy,
                    ServiceWireConstants.FrameworkErrorCode.WorkerQueueFull),
            ZLinkFrameworkErrorKind.RequestProtocolError =>
                new ActorCreateOperationTerminal(
                    RequestResult.ProtocolError,
                    ServiceWireConstants.FrameworkErrorCode.RequestProtocolError),
            _ => new ActorCreateOperationTerminal(
                exception.IsRetriable ? RequestResult.Busy : RequestResult.InternalError,
                ServiceWireConstants.FrameworkErrorCode.ActorCreateFailed)
        };
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

    private SubmitResult SubmitReservedCreationOperation(
        RoutingId targetRid,
        ulong targetNodeGeneration,
        MeshOperationKind kind,
        Func<ulong, MeshOperationId, byte[]> encode,
        out MeshOperationId operationId,
        TimeSpan timeout)
    {
        if (kind is not (MeshOperationKind.UserSpotCreate
            or MeshOperationKind.UserSpotClose
            or MeshOperationKind.ActorCreate))
            throw new ArgumentOutOfRangeException(nameof(kind));

        Peer? peer;
        lock (_gate)
            _peersByRid.TryGetValue(targetRid, out peer);
        if (peer is null
            || !peer.Admitted
            || peer.LifecycleGeneration != targetNodeGeneration)
        {
            operationId = default;
            return SubmitResult.NotConnected;
        }

        if (!TryCreateOperation(kind, out var correlation, out var pending))
        {
            operationId = default;
            Publish(MeshMonitorEventKind.Backpressured, peerRid: targetRid);
            return SubmitResult.Backpressured;
        }
        operationId = pending.OperationId;

        byte[] head;
        try
        {
            head = encode(correlation, operationId);
        }
        catch
        {
            TryRemoveOperation(correlation, out _);
            pending.Cancel();
            operationId = default;
            throw;
        }

        if (!TrySend(peer.PhysicalRoutingId, [head], SendFlags.None))
        {
            TryRemoveOperation(correlation, out _);
            pending.Cancel();
            operationId = default;
            return SubmitResult.Backpressured;
        }

        _ = ExpireOperationAsync(
            correlation,
            pending,
            timeout <= TimeSpan.Zero ? TimeSpan.FromSeconds(30) : timeout);
        Publish(MeshMonitorEventKind.MessageSubmitted, peerRid: targetRid);
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
        MeshRecordPayload? completion = null;
        bool decoded;
        if (pending.Kind == MeshOperationKind.ActorCreate)
        {
            decoded = ZLinkServiceWireCodec.TryDecodeActorCreateReply(
                reply,
                out var actorCreateCompletion,
                out _);
            completion = actorCreateCompletion;
        }
        else
        {
            decoded = ZLinkServiceWireCodec.TryDecodeUserSpotReply(
                reply,
                pending.Kind,
                out completion,
                out _);
        }
        if (!decoded)
        {
            EnqueueCompletion(
                pending.OperationId,
                pending.Kind,
                (int)RequestResult.ProtocolError,
                (int)ServiceWireConstants.FrameworkErrorCode.RequestProtocolError,
                Array.Empty<Message>());
            return;
        }
        var parts = received.Parts.Skip(1).Select(Message.From).ToArray();
        EnqueueCompletion(
            pending.OperationId,
            pending.Kind,
            reply.TerminalResult,
            checked((int)reply.FailureCode),
            parts,
            completion);
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
        IReadOnlyList<Message> parts,
        MeshRecordPayload? kindData = null)
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
                kindData),
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
        var target = ZLinkWeightedSelector.Select(
            targets,
            static candidate => candidate.Weight,
            ref _nextChannelSelection);
        targetRid = target!.RoutingId;
        return true;
    }

    private List<WeightedChannelTarget> SnapshotChannelTargets(string channelName)
    {
        lock (_gate)
        {
            var targets = new List<WeightedChannelTarget>();
            if (_channels.TryGetValue(channelName, out var localWeight)
                && localWeight > 0)
                targets.Add(new WeightedChannelTarget(
                    _routingId,
                    checked((int)localWeight)));
            targets.AddRange(_peersByRid.Values
                .Where(peer => peer.Admitted
                               && peer.Channels.TryGetValue(channelName, out var weight)
                               && weight > 0)
                .Select(peer => new WeightedChannelTarget(
                    peer.RoutingId,
                    checked((int)peer.Channels[channelName])))
                .OrderBy(static target => target.RoutingId.ToHex(), StringComparer.Ordinal));
            return targets;
        }
    }

    private sealed record WeightedChannelTarget(
        RoutingId RoutingId,
        int Weight);

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
        string SpotId,
        ActorRef Actor)
    {
        internal static MailboxKey ForNode(MeshReadyDomains domain) =>
            new(MeshOwnerKind.Node, string.Empty, 0, domain, default, default);

        internal static MailboxKey ForSpot(
            ZLinkManagedSpot spot,
            MeshReadyDomains domain) =>
            new(
                MeshOwnerKind.Spot,
                spot.SpotId,
                spot.LifecycleGeneration,
                domain,
                spot.SpotId,
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
        string SpotId,
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
        string SpotId,
        ulong SpotGeneration,
        ulong MembershipEpoch);

    private sealed class ManagedActor(
        ActorRef actorRef,
        string spotId,
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
        internal string SpotId { get; private set; } = spotId;
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

        internal void SetAuthorityOwnerGeneration(ulong value)
        {
            if (value == 0)
                throw new ArgumentOutOfRangeException(nameof(value));
            lock (_gate)
                AuthorityOwnerGeneration = value;
        }
        internal ActorLocation Location =>
            new(Ref, SpotId, SpotGeneration, MembershipEpoch);

        internal ActorSnapshot Snapshot()
        {
            lock (_gate)
                return new ActorSnapshot(
                    Ref,
                    SpotId,
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
            string targetSpotId,
            ulong targetSpotGeneration)
        {
            lock (_gate)
            {
                if (_draining
                    || _sealed
                    || SpotId != expected.SpotId
                    || SpotGeneration != expected.SpotGeneration
                    || MembershipEpoch != expected.MembershipEpoch)
                    return false;
                SpotId = targetSpotId;
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

    private readonly record struct RemoteUserSpotOperationKey(
        RoutingId SourceNodeRid,
        ulong SourceNodeGeneration,
        MeshOperationId OperationId);

    private sealed class RemoteUserSpotInvocation
    {
        private readonly Lazy<Task<UserSpotOperationTerminal>> _task;

        internal RemoteUserSpotInvocation(
            ZLinkServiceWireCodec.UserSpotOperationRecord record,
            Func<Task<UserSpotOperationTerminal>> execute)
        {
            Record = record;
            _task = new Lazy<Task<UserSpotOperationTerminal>>(
                execute,
                LazyThreadSafetyMode.ExecutionAndPublication);
        }

        internal ZLinkServiceWireCodec.UserSpotOperationRecord Record { get; }
        internal Task<UserSpotOperationTerminal> Task => _task.Value;
    }

    private readonly record struct RemoteActorCreateOperationKey(
        RoutingId SourceNodeRid,
        ulong SourceNodeGeneration,
        MeshOperationId OperationId);

    private sealed class RemoteActorCreateInvocation
    {
        private readonly Lazy<Task<ActorCreateOperationTerminal>> _task;

        internal RemoteActorCreateInvocation(
            ZLinkServiceWireCodec.ActorCreateOperationRecord record,
            Func<Task<ActorCreateOperationTerminal>> execute)
        {
            Record = record;
            _task = new Lazy<Task<ActorCreateOperationTerminal>>(
                execute,
                LazyThreadSafetyMode.ExecutionAndPublication);
        }

        internal ZLinkServiceWireCodec.ActorCreateOperationRecord Record { get; }
        internal Task<ActorCreateOperationTerminal> Task => _task.Value;
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
    string spotId,
    ulong lifecycleGeneration,
    ulong authorityOwnerGeneration) : ISpot
{
    private readonly Dictionary<string, HashSet<string>> _subscriptions =
        new(StringComparer.Ordinal);
    private int _disposed;
    private int _actorCount;

    private string _spotId = ZLinkSpotId.Require(spotId, nameof(spotId));
    public RoutingId RoutingId => ZLinkSpotId.ToNativeRoutingId(_spotId);
    internal string SpotId => _spotId;
    public ulong LifecycleGeneration { get; } = lifecycleGeneration;
    internal ulong AuthorityOwnerGeneration { get; } = authorityOwnerGeneration;
    internal int ActorCount => Volatile.Read(ref _actorCount);

    public void SetRoutingId(RoutingId routingId)
    {
        var next = ZLinkSpotId.FromNativeRoutingId(routingId);
        ZLinkSpotId.Require(next, nameof(routingId));
        var previous = _spotId;
        node.RekeySpot(this, previous, next);
        _spotId = next;
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

    public void Publish(
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        node.Publish(SpotId, channelName, topic, parts, flags, metadata);
    }

    public SubmitResult SendToSpot(
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        node.SendToSpot(
            SpotId,
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            parts,
            flags,
            metadata);

    public SubmitResult RequestToSpot(
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default) =>
        node.RequestToSpot(
            SpotId,
            targetNodeRid,
            targetSpotId,
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
