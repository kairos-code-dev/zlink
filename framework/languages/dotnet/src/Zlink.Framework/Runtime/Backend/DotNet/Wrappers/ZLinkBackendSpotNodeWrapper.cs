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
    private readonly object _lifecycleGate = new();
    private readonly object _entrySpotGate = new();
    private IZLinkBackendSpot? _entrySpot;
    private Action? _sendReadyHandler;
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
            _node.RemovePeerConnection(intent);
    }

    public IZLinkBackendSpot CreateSpot()
    {
        EnsureStarted();
        return new ZLinkBackendSpotWrapper(_node, _node.CreateSpot(), _pump, _completions);
    }

    public IZLinkBackendSpot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        EnsureStarted();
        return new ZLinkBackendSpotWrapper(
            _node, _node.GetOrCreateSpot(spotRid, out created), _pump, _completions);
    }

    public ZLinkSpotNodeStatus Status()
    {
        return _node.Status().ToFramework();
    }

    public IReadOnlyList<ZLinkSpotNodePeerEntry> Peers()
    {
        return _node.Peers().Select(static peer => peer.ToFramework()).ToArray();
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects()
    {
        // MeshNode does not surface a subject table; empty preserves callers.
        return Array.Empty<ZLinkSpotNodeSubjectEntry>();
    }

    public IZLinkBackendSpot EntrySpot()
    {
        if (_entrySpot is { } entrySpot) return entrySpot;
        lock (_entrySpotGate)
        {
            EnsureStarted();
            return _entrySpot ??= new ZLinkBackendSpotWrapper(
                _node, _node.EntrySpot(), _pump, _completions);
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

    // Straggler bound-session relay primitives (ReplyActorNoBind / forward / bind
    // remote) have no direct MeshNode surface; preserved as best-effort no-ops so
    // the actor bound-session plane compiles. See S8 bound-session follow-up.
    public void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        IReadOnlyList<Message> parts)
    {
        ZLinkMessageParts.DisposeAll(parts);
    }

    public bool ForwardActorBoundSessionPart(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags)
    {
        return _node.SendBoundSession(actor.ToNative(), new[] { message }, flags)
            == SubmitResult.Ok;
    }

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

    public async ValueTask DisposeAsync()
    {
        lock (_lifecycleGate)
        {
            if (_disposed) return;
            _disposed = true;
        }

        await _pump.DisposeAsync().ConfigureAwait(false);
        await _node.DisposeAsync().ConfigureAwait(false);
    }
}
