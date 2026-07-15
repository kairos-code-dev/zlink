namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendSpotNodeWrapper(ISpotNode nativeSpotNode) : IZLinkBackendSpotNode
{
    private readonly SemaphoreSlim _actorLifecycleGate = new(1, 1);
    private readonly Dictionary<ZLinkBackendActorRef, IActor> _ownedActors = [];
    private readonly object _disposeGate = new();
    private Task? _disposeTask;
    private bool _disposed;

    public RoutingId RoutingId => nativeSpotNode.RoutingId;

    public void SetRoutingId(RoutingId routingId)
    {
        nativeSpotNode.SetRoutingId(routingId);
    }

    public void SetPublisherRoutingId(RoutingId routingId)
    {
        nativeSpotNode.SetPublisherRoutingId(routingId);
    }

    public void SetSubscriberRoutingId(RoutingId routingId)
    {
        nativeSpotNode.SetSubscriberRoutingId(routingId);
    }

    public void SetRouterBind(string endpoint)
    {
        nativeSpotNode.SetRouterBind(endpoint);
    }

    public void SetPubBind(string endpoint)
    {
        nativeSpotNode.SetPubBind(endpoint);
    }

    public void ApplyRoleConfig(
        IZLinkSpotPublisherConfig? publisher,
        IZLinkSpotSubscriberConfig? subscriber)
    {
        if (publisher is not null)
        {
            if (publisher.SendHighWaterMark > 0)
                nativeSpotNode.PubSubHighWaterMark = publisher.SendHighWaterMark;
            nativeSpotNode.PublisherSendTimeout = publisher.SendTimeout;
            nativeSpotNode.PublisherLinger = publisher.Linger;
            nativeSpotNode.PublisherNoDrop = publisher.NoDrop;
        }

        if (subscriber is not null)
        {
            if (subscriber.ReceiveHighWaterMark > 0)
                nativeSpotNode.PubSubHighWaterMark = subscriber.ReceiveHighWaterMark;
            nativeSpotNode.SubscriberReceiveTimeout = subscriber.ReceiveTimeout;
            nativeSpotNode.SubscriberLinger = subscriber.Linger;
        }
    }

    public void OnSendReady(Action handler)
    {
        nativeSpotNode.SetSendReadyHandler(() => handler());
    }

    public void ConnectPeer(string endpoint)
    {
        nativeSpotNode.ConnectPeer(endpoint);
    }

    public void ConnectPeer(RoutingId peerRid, string endpoint)
    {
        nativeSpotNode.ConnectPeerRid(peerRid, endpoint);
    }

    public void DisconnectPeer(string endpoint)
    {
        nativeSpotNode.DisconnectPeer(endpoint);
    }

    public IZLinkBackendSpot CreateSpot()
    {
        return new ZLinkBackendSpotWrapper(nativeSpotNode.CreateSpot());
    }

    public IZLinkBackendSpot GetOrCreateSpot(RoutingId spotRid, out bool created)
    {
        return new ZLinkBackendSpotWrapper(
            nativeSpotNode.GetOrCreateSpot(spotRid, out created));
    }

    public ZLinkSpotNodeStatus Status()
    {
        return nativeSpotNode.Status().ToFramework();
    }

    public IReadOnlyList<ZLinkSpotNodePeerEntry> Peers()
    {
        return nativeSpotNode.Peers()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> Subjects()
    {
        return nativeSpotNode.Subjects()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    private IZLinkBackendSpot? _entrySpot;
    private readonly object _entrySpotGate = new();

    public IZLinkBackendSpotRouteBridge CreateRouteBridge()
    {
        return new ZLinkBackendSpotRouteBridgeWrapper(nativeSpotNode.CreateRouteBridge());
    }

    public IZLinkBackendSpot EntrySpot()
    {
        // One facade per node: every native EntrySpot() call creates and
        // registers a NEW facade, and actor-readable notifications go to
        // one arbitrary registered facade — which must be the one the
        // dispatch pump listens on. Handing out duplicates silently
        // black-holes bound-actor session relays for entry-resident actors.
        if (_entrySpot is { } entrySpot) return entrySpot;

        lock (_entrySpotGate)
        {
            return _entrySpot ??= new ZLinkBackendSpotWrapper(nativeSpotNode.EntrySpot());
        }
    }

    public ZLinkBackendActorRef CreateActor(string actorId, Message createRequest)
    {
        _actorLifecycleGate.Wait();
        try
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            IActor? actor = null;
            try
            {
                actor = nativeSpotNode.CreateActor(actorId, createRequest);
                var actorRef = EnsureConcreteActorRef(actor.Ref.ToBackend(), actorId);
                if (!_ownedActors.TryAdd(actorRef, actor))
                    throw new InvalidOperationException(
                        $"Actor handle '{actorRef.ActorId}' generation '{actorRef.Generation}' is already owned.");

                actor = null;
                return actorRef;
            }
            catch (Exception createFailure)
            {
                if (actor is null) throw;

                try
                {
                    actor.Dispose();
                }
                catch (Exception cleanupFailure)
                {
                    throw new AggregateException(createFailure, cleanupFailure);
                }

                throw;
            }
        }
        finally
        {
            _actorLifecycleGate.Release();
        }
    }

    public ZLinkBackendActorRef? ActorLookup(string actorId)
    {
        try
        {
            var actorRef = nativeSpotNode.ActorLookup(actorId);
            return EnsureConcreteActorRef(actorRef.ToBackend(), actorId);
        }
        catch (ZlinkConfigException ex) when (ex.Result == ZlinkConfigException.ErrorCode.NotFound)
        {
            return null;
        }
    }

    private ZLinkBackendActorRef EnsureConcreteActorRef(
        ZLinkBackendActorRef actorRef,
        string actorId)
    {
        if (!actorRef.NodeRid.IsEmpty) return actorRef;

        var nodeRid = nativeSpotNode.RoutingId;
        if (nodeRid.IsEmpty)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                $"Actor '{actorId}' was created on a SpotNode without a concrete routing id.");

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
        var operation = nativeSpotNode.JoinActor(actor.ToNative(), destNodeRid, destSpotRid)
            .Message(message)
            .Flags(SendFlags.DontWait);
        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Submit((result, parts) => callback(result.Result, parts));
    }

    public bool JoinActor(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        RoutingId destSpotRid,
        IReadOnlyList<Message> parts,
        ActorJoinCallback callback,
        TimeSpan? timeout)
    {
        var operation = nativeSpotNode.JoinActor(actor.ToNative(), destNodeRid, destSpotRid)
            .Messages(parts)
            .Flags(SendFlags.DontWait);

        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Submit((result, replyParts) => callback(
            new ZLinkBackendActorJoinResult(
                result.Result,
                result.JoinResultCode,
                result.Actor.ToBackend(),
                result.JoinedSpotRid,
                result.JoinEpoch,
                result.Flags),
            replyParts));
    }

    public bool JoinActorEntrySpot(
        ZLinkBackendActorRef actor,
        RoutingId destNodeRid,
        Message request,
        ActorJoinEntrySpotCallback callback,
        TimeSpan? timeout)
    {
        var operation = nativeSpotNode.JoinActorEntrySpot(actor.ToNative(), destNodeRid, request);
        if (timeout is { } value) operation = operation.Timeout(value);

        return operation.Submit((result, replyParts) => callback(
            new ZLinkBackendActorJoinEntrySpotResult(
                result.Result,
                result.JoinResultCode,
                result.Actor.ToBackend(),
                result.TargetNodeRid,
                result.JoinedSpotRid,
                result.JoinEpoch,
                result.Flags),
            replyParts));
    }

    public async ValueTask DestroyActorAsync(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await _actorLifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (_ownedActors.TryGetValue(actor, out var ownedActor))
            {
                cancellationToken.ThrowIfCancellationRequested();
                ownedActor.Close(timeout);
                _ownedActors.Remove(actor);
                return;
            }

            await nativeSpotNode.DestroyActor(actor.ToNative())
                .Timeout(timeout)
                .Async(cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            _actorLifecycleGate.Release();
        }
    }

    public bool SendActorBoundSession(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpotNode.SendActorBoundSession(actor.ToNative())
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public bool SendToActor(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpotNode.SendToActor(actor.ToNative())
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToActorAsync(
        ZLinkBackendActorRef actor,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var operation = nativeSpotNode.RequestToActor(actor.ToNative())
            .Messages(parts);
        if (timeout is { } value) operation = operation.Timeout(value);

        return await operation.Async(cancellationToken).ConfigureAwait(false);
    }

    public void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        IReadOnlyList<Message> parts)
    {
        var info = new ActorRecvInfo(
            actor.ToNative(),
            sourceNodeRid,
            sourceSessionRid,
            requestId,
            flags);
        nativeSpotNode.ReplyActorNoBind(info, parts);
    }

    public bool ForwardActorBoundSessionPart(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags)
    {
        return nativeSpotNode.ForwardActorBoundSessionPart(
            actor.ToNative(),
            sourceNodeRid,
            sourceSessionRid,
            message,
            hasMore,
            flags);
    }

    public void BindRemoteActorBoundSession(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid)
    {
        nativeSpotNode.BindRemoteActorBoundSession(
            actor.ToNative(),
            sourceNodeRid,
            sourceSessionRid);
    }

    public void CloseActorBoundSession(
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        nativeSpotNode.CloseActorBoundSession(actor.ToNative(), timeout);
    }

    public ValueTask DisposeAsync()
    {
        lock (_disposeGate)
            return new ValueTask(_disposeTask ??= DisposeCoreAsync());
    }

    private async Task DisposeCoreAsync()
    {
        await _actorLifecycleGate.WaitAsync().ConfigureAwait(false);
        try
        {
            if (_disposed) return;
            _disposed = true;

            var failures = new List<Exception>();
            foreach (var actor in _ownedActors.Values)
                try
                {
                    await actor.DisposeAsync().ConfigureAwait(false);
                }
                catch (Exception exception)
                {
                    failures.Add(exception);
                }
            _ownedActors.Clear();

            try
            {
                await nativeSpotNode.DisposeAsync().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }

            if (failures.Count == 1)
                System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
            if (failures.Count > 1) throw new AggregateException(failures);
        }
        finally
        {
            _actorLifecycleGate.Release();
        }
    }
}
