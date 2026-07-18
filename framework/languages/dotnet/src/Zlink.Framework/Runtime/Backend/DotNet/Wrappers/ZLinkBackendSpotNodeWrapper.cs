using System.Collections.Concurrent;
using Zlink.Framework.Runtime.Backend.DotNet.Mappings;

namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

// RouteMesh 10.0.0 MeshNode-backed implementation of the framework SpotNode seam.
// The 9.x SpotNode fluent+callback surface is bridged onto IMeshNode: requests
// return an out MeshOperationId whose reply is resolved by the node dispatch pump
// through the completion table, and pull dispatch replaces the per-spot receive
// loops.
internal sealed class ZLinkBackendSpotNodeWrapper : IZLinkBackendSpotNode
{
    private readonly IMeshNode _node;
    private readonly ZLinkMeshCompletionTable _completions = new();
    private readonly ZLinkMeshDispatchPump _pump;
    private readonly ConcurrentDictionary<string, ulong> _peerIntents =
        new(StringComparer.Ordinal);
    private readonly ZLinkSpotSubscriptionTracker _subscriptions = new();
    private readonly object _forwardGate = new();
    private readonly Dictionary<ZLinkBackendActorRef, List<Message>> _forwardBuffers = new();
    private readonly object _lifecycleGate = new();
    private readonly object _entrySpotGate = new();
    private IZLinkBackendSpot? _entrySpot;
    private Action? _sendReadyHandler;
    // First registered mesh channel; spot wrappers publish/subscribe on it
    // (spot pub/sub is logical multicast on the router plane).
    private string? _primaryChannelName;
    private bool _bound;
    private bool _started;
    private bool _disposed;

    public ZLinkBackendSpotNodeWrapper(IMeshNode node)
    {
        _node = node;
        _pump = new ZLinkMeshDispatchPump(node, _completions);
    }

    internal IMeshNode NativeNode => _node;

    internal ZLinkMeshDispatchPump Pump => _pump;

    internal ZLinkMeshCompletionTable Completions => _completions;

    public RoutingId RoutingId => _node.RoutingId;

    public void SetRoutingId(RoutingId routingId)
    {
        _node.SetRoutingId(routingId);
    }

    // Pub/sub routing ids and role config have no MeshNode equivalent (publishing
    // is via IMeshNode.CreatePublisher / channels). Preserved as no-ops so the
    // configuration plane keeps compiling; see S8 follow-up.
    public void SetPublisherRoutingId(RoutingId routingId)
    {
    }

    public void SetSubscriberRoutingId(RoutingId routingId)
    {
    }

    public void SetRouterBind(string endpoint)
    {
        BindOnce(endpoint);
    }

    public void SetPubBind(string endpoint)
    {
        BindOnce(endpoint);
    }

    private void BindOnce(string endpoint)
    {
        lock (_lifecycleGate)
        {
            if (_bound) return;
            _bound = true;
            _node.SetBind(endpoint);
        }
    }

    // Startup channel sequencing (spec 21-mesh-node §3): AddChannel/SetChannelWeight
    // are applied before Start. SetChannelWeight also backs the live weight path
    // (spec 21 §4); a positive weight is a runtime descriptor-revision bump.
    public void AddChannel(string channelName)
    {
        _primaryChannelName ??= channelName;
        _node.AddChannel(channelName);
    }

    public void SetChannelWeight(string channelName, uint weight)
    {
        _node.SetChannelWeight(channelName, weight);
    }

    // Explicit host-startup Start (spec 21 §3): the runtime calls this after
    // routing id, bind and channels are configured, so the node is not started
    // lazily on first spot use. Idempotent; EnsureStarted stays as a defensive
    // fallback for paths that reach a spot before explicit startup.
    public void Start()
    {
        EnsureStarted();
    }

    public void ApplyRoleConfig(
        IZLinkSpotPublisherConfig? publisher,
        IZLinkSpotSubscriberConfig? subscriber)
    {
        // MeshNode carries no per-role HWM/linger/timeout knobs; follow-up.
    }

    public void OnSendReady(Action handler)
    {
        _sendReadyHandler = handler;
    }

    public void ConnectPeer(string endpoint)
    {
        _peerIntents[endpoint] = _node.ConnectPeer(endpoint);
    }

    public void ConnectPeer(RoutingId peerRid, string endpoint)
    {
        _peerIntents[endpoint] = _node.ConnectPeer(endpoint, peerRid);
    }

    public void DisconnectPeer(string endpoint)
    {
        if (_peerIntents.TryRemove(endpoint, out var intent))
        {
            try
            {
                _node.RemovePeerConnection(intent);
                return;
            }
            catch (ZlinkException)
            {
                // Intent removal covers unadmitted intents only; an admitted
                // lifetime takes the exact RID+generation disconnect below.
            }
        }

        foreach (var peer in _node.Peers())
        {
            if (!string.Equals(peer.Endpoint, endpoint, StringComparison.Ordinal)
                || peer.State is not (MeshPeerState.Admitted or MeshPeerState.Draining))
                continue;
            try
            {
                _node.DisconnectPeer(peer.RoutingId, peer.LifecycleGeneration);
            }
            catch (ZlinkException)
            {
                // Core may have retired the lifetime with the transport.
            }
        }
    }

    public void DisconnectPeerLifetime(RoutingId peerRid, ulong lifecycleGeneration)
    {
        try
        {
            _node.DisconnectPeer(peerRid, lifecycleGeneration);
        }
        catch (ZlinkException)
        {
            // The lifetime may already be gone (never admitted, or Core
            // retired it with the transport); retirement is idempotent.
        }
    }

    public IZLinkBackendSpot CreateSpot()
    {
        EnsureStarted();
        return new ZLinkBackendSpotWrapper(
            _node, _node.CreateSpot(), _pump, _completions,
            () => _primaryChannelName, _subscriptions);
    }

    public IZLinkBackendSpot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        EnsureStarted();
        return new ZLinkBackendSpotWrapper(
            _node, _node.GetOrCreateSpot(spotRid, out created),
            _pump, _completions, () => _primaryChannelName, _subscriptions);
    }

    public ZLinkSpotNodeStatus Status()
    {
        return _node.Status().ToFramework();
    }

    public IReadOnlyList<ZLinkSpotNodePeerEntry> Peers()
    {
        return _node.Peers().Select(static peer => peer.ToFramework()).ToArray();
    }

    public MeshNodeStatus MeshStatus()
    {
        return _node.Status();
    }

    public IReadOnlyList<MeshNodePeer> MeshPeers()
    {
        return _node.Peers();
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects()
    {
        // MeshNode surfaces no subject table; the framework owns the spots it
        // created and their logical-multicast subscriptions, so the subject
        // view (spec 50 snapshot) derives from that tracking.
        return _subscriptions.Snapshot();
    }

    public IZLinkBackendSpot EntrySpot()
    {
        if (_entrySpot is { } entrySpot) return entrySpot;
        lock (_entrySpotGate)
        {
            EnsureStarted();
            return _entrySpot ??= new ZLinkBackendSpotWrapper(
                _node, _node.EntrySpot(), _pump, _completions,
                () => _primaryChannelName, _subscriptions);
        }
    }

    private void EnsureStarted()
    {
        lock (_lifecycleGate)
        {
            if (!_started && !_disposed)
            {
                _started = true;
                _node.Start();
            }
        }

        _pump.EnsureStarted();
    }

    public ZLinkBackendActorRef CreateActor(string actorId, Message createRequest)
    {
        EnsureStarted();
        var actorRef = _node.CreateActor(actorId, new[] { createRequest });
        return EnsureConcreteActorRef(actorRef.ToBackend(), actorId);
    }

    public ZLinkBackendActorRef? ActorLookup(string actorId)
    {
        return _node.ActorLookup(actorId, out var location)
            ? EnsureConcreteActorRef(location.Actor.ToBackend(), actorId)
            : null;
    }

    private ZLinkBackendActorRef EnsureConcreteActorRef(
        ZLinkBackendActorRef actorRef, string actorId)
    {
        if (!actorRef.NodeRid.IsEmpty) return actorRef;

        var nodeRid = _node.RoutingId;
        if (nodeRid.IsEmpty)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                $"Actor '{actorId}' was created on a node without a concrete routing id.");

        return actorRef with { NodeRid = nodeRid };
    }

    public bool JoinActor(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        RoutingId destSpotRid,
        Message message,
        RequestCallback callback,
        TimeSpan? timeout)
    {
        var operationId = _node.JoinSpot(
            actor.ToNative(), destNodeRid, destSpotRid, 0, new[] { message },
            timeout ?? default);
        return _completions.RegisterRequest(operationId, callback);
    }

    public bool JoinActor(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        ActorJoinCallback callback,
        TimeSpan? timeout)
    {
        var operationId = _node.JoinSpot(
            actor.ToNative(), destNodeRid, destSpotRid, 0, parts, timeout ?? default);
        return _completions.Register(operationId, (record, replyParts) =>
            callback(BuildJoinResult(record, actor), replyParts));
    }

    public bool JoinActorEntrySpot(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        Message request,
        ActorJoinEntrySpotCallback callback,
        TimeSpan? timeout)
    {
        var operationId = _node.JoinEntrySpot(
            actor.ToNative(), destNodeRid, new[] { request }, timeout ?? default);
        return _completions.Register(operationId, (record, replyParts) =>
            callback(BuildEntrySpotJoinResult(record, actor, destNodeRid), replyParts));
    }

    private static ZLinkBackendActorJoinResult BuildJoinResult(
        MeshReceiveRecord record, ZLinkBackendActorRef fallback)
    {
        var completion = record.JoinCompletion;
        return new ZLinkBackendActorJoinResult(
            ZLinkMeshCompletionTable.MapResult(record.TerminalResult, record.FailureErrno),
            completion is { JoinResult: ActorJoinResult.Accepted } ? 0 : 1,
            completion?.Actor.ToBackend() ?? fallback,
            completion?.Location.SpotRid ?? default,
            completion?.Location.MembershipEpoch ?? 0,
            0);
    }

    private static ZLinkBackendActorJoinEntrySpotResult BuildEntrySpotJoinResult(
        MeshReceiveRecord record, ZLinkBackendActorRef fallback, RoutingId targetNodeRid)
    {
        var completion = record.JoinCompletion;
        return new ZLinkBackendActorJoinEntrySpotResult(
            ZLinkMeshCompletionTable.MapResult(record.TerminalResult, record.FailureErrno),
            completion is { JoinResult: ActorJoinResult.Accepted } ? 0 : 1,
            completion?.Actor.ToBackend() ?? fallback,
            targetNodeRid,
            completion?.Location.SpotRid ?? default,
            completion?.Location.MembershipEpoch ?? 0,
            0);
    }

    public async ValueTask DestroyActorAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var operationId = _node.DestroyActor(actor.ToNative(), timeout);
        if (operationId == default) return;

        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        _completions.Register(operationId, (_, parts) =>
        {
            ZLinkMessageParts.DisposeAll(parts);
            completion.TrySetResult();
        });
        await using (cancellationToken.Register(() => completion.TrySetCanceled())
                         .ConfigureAwait(false))
            await completion.Task.ConfigureAwait(false);
    }

    public bool SendActorBoundSession(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return _node.SendBoundSession(actor.ToNative(), parts, flags) == SubmitResult.Ok;
    }

    public SubmitResult SendToNode(
        RoutingId targetNodeRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return _node.SendToNode(targetNodeRid, parts, flags);
    }

    public bool SendToActor(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return _node.SendToActor(actor.ToNative(), parts, flags) == SubmitResult.Ok;
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToActorAsync(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var submit = _node.RequestToActor(
            actor.ToNative(), parts, out var operationId, timeout ?? default);
        if (submit != SubmitResult.Ok)
            throw new ZlinkSubmitException((ZlinkSubmitException.ErrorCode)(int)submit);

        var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        _completions.Register(operationId, (record, replyParts) =>
        {
            var result = ZLinkMeshCompletionTable.MapResult(
                record.TerminalResult, record.FailureErrno);
            if (result == RequestResult.Ok)
            {
                completion.TrySetResult(replyParts);
                return;
            }

            ZLinkMessageParts.DisposeAll(replyParts);
            completion.TrySetException(
                new ZlinkRequestException((ZlinkRequestException.ErrorCode)(int)result));
        });
        await using (cancellationToken.Register(() => completion.TrySetCanceled())
                         .ConfigureAwait(false))
            return await completion.Task.ConfigureAwait(false);
    }

    // Straggler bound-session reply with no active binding. Spec 31 §5: once a
    // session closes, a late Actor reply is not delivered to a new session or
    // binding, and the 10.0.0 bound-session surface (spec 31 §6) provides only a
    // one-way push to the *current* binding plus close — there is no MeshNode
    // primitive that replies to an arbitrary (sourceSessionRid, requestId). In
    // 10.0.0 request replies flow through MeshOperationId completion correlation,
    // not a no-bind reply channel, so a no-bind reply is intentionally dropped.
    // Documented deviation, not a stub gap.
    public void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        IReadOnlyList<Message> parts)
    {
        // A no-bind actor request replies through the inbound record's core
        // reply token (registered by the dispatch pump); there is no session
        // binding to carry the reply. A missing token means the requester is
        // gone or the record was evicted — the reply is dropped by contract.
        var found = _pump.TryTakeActorReply(requestId, out var reply);
        var submit = found ? reply(parts, SendFlags.DontWait) : default;
        ZLinkMessageParts.DisposeAll(parts);
    }

    // Forwards a straggler bound-session frame to the actor's currently bound
    // STREAM session via IMeshNode.SendBoundSession (spec 31 §6 one-way push). The
    // 9.x fine-grained (sourceNodeRid, sourceSessionRid) SNDMORE targeting has no
    // MeshNode equivalent — the target is the actor's current binding, resolved by
    // Core, not an arbitrary source session. Parts marked hasMore are buffered per
    // actor and flushed as one multipart SendBoundSession when the terminal part
    // (hasMore == false) arrives, so header+body framing is preserved. On a failed
    // flush the buffered prefix is retained so the caller's retry re-submits the
    // same multipart message without duplicating parts. Forwarding for a given
    // actor is serial (the straggler forwarder submits header then body in order).
    public bool ForwardActorBoundSessionPart(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags)
    {
        if (hasMore)
        {
            lock (_forwardGate)
            {
                if (!_forwardBuffers.TryGetValue(actor, out var pending))
                {
                    pending = new List<Message>();
                    _forwardBuffers[actor] = pending;
                }

                pending.Add(Message.From(message));
            }

            return true;
        }

        List<Message>? buffered;
        lock (_forwardGate)
            _forwardBuffers.Remove(actor, out buffered);

        var terminal = Message.From(message);
        var parts = new List<Message>((buffered?.Count ?? 0) + 1);
        if (buffered is not null) parts.AddRange(buffered);
        parts.Add(terminal);

        // SendBoundSession clones the parts (the caller keeps ownership), so this
        // wrapper disposes every clone it owns on success.
        if (_node.SendBoundSession(actor.ToNative(), parts, flags) == SubmitResult.Ok)
        {
            foreach (var part in parts) part.Dispose();
            return true;
        }

        terminal.Dispose();
        if (buffered is not null)
            lock (_forwardGate)
                _forwardBuffers[actor] = buffered;
        return false;
    }

    // 10.0.0 binds a STREAM session to an actor through the owning STREAM node's
    // IStreamSessionService.BindActor (see ZLinkBackendStreamSocketWrapper); the
    // MeshNode/actor plane exposes no bind-remote-session primitive. The framework
    // records the remote binding via ZLinkActorBoundSessionCoordinator.BindActorSession
    // (the caller invokes it immediately after this), so this mesh-plane hook has
    // no MeshNode action. Documented deviation, not a stub gap.
    public void BindRemoteActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid)
    {
    }

    public void CloseActorBoundSession(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _ = _node.CloseBoundSession(actor.ToNative(), 0, timeout);
    }

    // Native actor-transfer fence wiring: the framework-owned transfer records map
    // onto the bindings IMeshNode ActorTransfer API. The fence token is opaque and
    // round-trips through commit/activate/abort. The distributed authority that
    // decides when to prepare/commit/activate/abort (participant-set CAS, lease,
    // crash recovery) is S8-04A and lives outside this seam.
    public ZLinkBackendActorTransferToken PrepareActorTransfer(
        ZLinkBackendActorTransferPrepare prepare,
        out ZLinkBackendActorTransferPrepareResult result,
        TimeSpan timeout)
    {
        var nativePrepare = new ActorTransferPrepare(
            (ActorTransferRole)prepare.Role,
            new ActorTransferId(prepare.TransferId.High, prepare.TransferId.Low),
            prepare.Actor.ToNative(),
            prepare.ExpectedMembershipEpoch,
            prepare.PeerNodeRid,
            prepare.FinalSequence,
            prepare.ReserveMessageCount,
            prepare.ReserveByteCount);
        var token = _node.PrepareActorTransfer(nativePrepare, out var nativeResult, timeout);
        result = new ZLinkBackendActorTransferPrepareResult(
            (ZLinkBackendActorTransferRole)nativeResult.Role,
            new ZLinkBackendActorTransferId(
                nativeResult.TransferId.High, nativeResult.TransferId.Low),
            nativeResult.Actor.ToBackend(),
            nativeResult.FinalSequence,
            nativeResult.ReserveMessageCount,
            nativeResult.ReserveByteCount);
        return new ZLinkBackendActorTransferToken(token);
    }

    public void CommitActorTransfer(
        ZLinkBackendActorTransferToken token, ulong newMembershipEpoch)
    {
        _node.CommitActorTransfer(token.Native, newMembershipEpoch);
    }

    public void ActivateActorTransfer(ZLinkBackendActorTransferToken token)
    {
        _node.ActivateActorTransfer(token.Native);
    }

    public void AbortActorTransfer(ZLinkBackendActorTransferToken token)
    {
        _node.AbortActorTransfer(token.Native);
    }

    public void OnNodeRoute(Action<ZLinkBackendRouteReceived> handler)
    {
        _pump.SetNodeRouteHandler(handler);
    }

    public void OnTransferControl(Action<ZLinkBackendActorTransferControl> handler)
    {
        _pump.SetTransferControlHandler(control => handler(
            new ZLinkBackendActorTransferControl(
                (ZLinkBackendActorTransferPhase)control.Phase,
                (ZLinkBackendActorTransferRole)control.Role,
                new ZLinkBackendActorTransferId(
                    control.TransferId.High, control.TransferId.Low),
                control.Actor.ToBackend(),
                control.MembershipEpoch,
                control.FinalSequence,
                control.ResultCode,
                control.FailureErrno)));
    }

    public async ValueTask DisposeAsync()
    {
        lock (_lifecycleGate)
        {
            if (_disposed) return;
            _disposed = true;
        }

        lock (_forwardGate)
        {
            foreach (var pending in _forwardBuffers.Values)
                foreach (var part in pending)
                    part.Dispose();
            _forwardBuffers.Clear();
        }

        await _pump.DisposeAsync().ConfigureAwait(false);
        await _node.DisposeAsync().ConfigureAwait(false);
    }
}
