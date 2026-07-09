namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    private static readonly ZLinkActorBoundSessionIndex ActorBoundSessions = new();

    internal async ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        return await _actors.JoinActorAsync(
            spotRid,
            actor,
            request,
            cancellationToken);
    }

    internal async ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        RoutingId spotRid,
        ActorRef actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        var managedActor = ResolveOwnedActorRef(actor);
        return await JoinActorAsync(
                spotRid,
                managedActor,
                request,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkActorJoinResult> JoinActorEntrySpotAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        return await _actors.JoinActorEntrySpotAsync(
            spotNodeRid,
            actor,
            request,
            cancellationToken);
    }

    internal async ValueTask<ZLinkActorJoinResult> JoinActorEntrySpotAsync(
        RoutingId spotNodeRid,
        ActorRef actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        var managedActor = ResolveOwnedActorRef(actor);
        return await JoinActorEntrySpotAsync(
                spotNodeRid,
                managedActor,
                request,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private IZLinkActor ResolveOwnedActorRef(ActorRef actor)
    {
        if (!TryGetCreatedActorState(actor.ActorId, out var state)
            || state.Actor is not { } managedActor
            || state.NativeActorRef is not { } nativeRef
            || nativeRef.NodeRid != actor.NodeRid
            || nativeRef.Generation != actor.Generation)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor ref '{actor.ActorId}' is not owned by this runtime.");

        return managedActor;
    }


    internal async ValueTask DestroyActorAsync(
        RoutingId entrySpotNodeRid,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        await _actorSessionManager.DestroyActorAsync(entrySpotNodeRid, actor, cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkRemoteActorJoinReply> JoinRoutedActorAsync(
        RoutingId spotRid,
        ZLinkRemoteActorJoinRequest request,
        CancellationToken cancellationToken = default)
    {
        var activation = await GetSpotActivationByRidAsync(spotRid, cancellationToken)
                             .ConfigureAwait(false)
                         ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        // Hosting handoff: the source node still owns the location row, so
        // the local claim may fence it out with Takeover (draft section 9).
        var creation = await CreateLocalActorForHandoffAsync(
                request.ActorId,
                request.ActorType,
                cancellationToken)
            .ConfigureAwait(false);
        var actorId = request.ActorId;
        var actorState = GetOrCreateActorState(actorId);
        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' does not have a native Actor ref.");
        var boundRoute = ZLinkRemoteActorJoinPackets.DecodeBoundSessionRoute(request);
        BindRemoteBoundSessionRoute(actorId, actorRef, boundRoute.NodeRid, boundRoute.SessionRid);

        var result = await activation.JoinActorAsync(
                creation.Actor,
                ZLinkRemoteActorJoinPackets.DecodeJoinRequestPayload(
                    request,
                    Registration.Codecs),
                cancellationToken)
            .ConfigureAwait(false);

        return ZLinkRemoteActorJoinPackets.CreateJoinReply(
            result.Accepted,
            actorRef,
            result.Reply,
            Registration.Codecs);
    }

    private void BindRemoteBoundSessionRoute(
        string actorId,
        ZLinkBackendActorRef actorRef,
        RoutingId? boundSessionNodeRid,
        RoutingId? boundSessionRid)
    {
        if (boundSessionNodeRid is not { } sourceNodeRid
            || boundSessionRid is not { } sourceSessionRid)
            return;

        BindActorSession(
            actorId,
            sourceNodeRid,
            sourceSessionRid,
            BuildNativeBoundSessionToken(sourceSessionRid));
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Remote actor session binding requires a router-capable SpotNode.");
        node.BindRemoteActorBoundSession(actorRef, sourceNodeRid, sourceSessionRid);
    }

    private static string BuildNativeBoundSessionToken(RoutingId sourceSessionRid)
    {
        return $"native:{sourceSessionRid.ToHex()}";
    }

    internal async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        await _actorSessionManager.JoinActorToSpotAsync(activation, actor, cancellationToken);
    }

    internal async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        await _actorSessionManager.AttachActorAsync(actor, stream, cancellationToken);
    }

    internal async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        await _actorSessionManager.DisconnectActorAsync(actor, stream, cancellationToken);
    }

    internal async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await _actorSessionManager.SubmitActorAsync(actor, header, payload, cancellationToken);
    }

    internal async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await CreateLocalActorAsync(
                actorId,
                actorType,
                ZLinkMessage.Empty,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default)
    {
        return await CreateLocalActorAsync(
                actorId,
                actorType,
                createRequest,
                ZLinkActorClaimMode.NewOwner,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<CreateActorResult> CreateLocalActorForHandoffAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await CreateLocalActorAsync(
                actorId,
                actorType,
                ZLinkMessage.Empty,
                ZLinkActorClaimMode.TakeoverExistingOwner,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        var result = await _actorSessionManager.CreateAndBindActorAsync(
                actorId,
                actorType,
                createRequest,
                claimMode,
                cancellationToken)
            .ConfigureAwait(false);
        if (result.Created)
        {
            var state = GetOrCreateActorState(result.Actor.ActorId);
            var nativeRef = state.NativeActorRef
                            ?? throw new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                $"Actor '{result.Actor.ActorId}' does not have a native Actor ref after creation.");
            await NotifyEntrySpotActorCreatedAsync(
                    result.Actor,
                    result.CreateRequest,
                    nativeRef.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return result;
    }

    internal async ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await CreateActorAsync(actorId, actorType, ZLinkMessage.Empty, cancellationToken);
    }

    internal async ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default)
    {
        return await _actorSessionManager.CreateActorAsync(actorId, actorType, createRequest, cancellationToken);
    }

    internal async ValueTask<IZLinkActor?> FindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return await _actorSessionManager.FindActorAsync(actorId, cancellationToken);
    }

    internal bool TryGetCreatedActor(
        string actorId,
        string actorType,
        out IZLinkActor actor)
    {
        return _actorSessionManager.TryGetCreatedActor(actorId, actorType, out actor);
    }

    internal bool TryGetCreatedActorState(
        string actorId,
        out ZLinkActorRuntimeState state)
    {
        return _actorSessionManager.TryGetCreatedActorState(actorId, out state);
    }

    internal bool TryGetCreatedActorState(
        string actorId,
        string actorType,
        out ZLinkActorRuntimeState state)
    {
        return _actorSessionManager.TryGetCreatedActorState(actorId, actorType, out state);
    }

    internal async ValueTask<ZLinkActorReply> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return await _actorSessionManager.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken);
    }

    internal async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await _actorSessionManager.SubmitActorByIdAsync(actorId, header, payload, cancellationToken);
    }

    internal async ValueTask NotifyActorDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        await _actorSessionManager.NotifyDisconnectedByIdAsync(actorId, cancellationToken);
    }

    internal async ValueTask NotifyActorDisconnectedAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrCreateActorState(actor.ActorId);
        if (state.Actor is not null
            && state.NativeActorRef is { } localActor
            && localActor.NodeRid == actor.NodeRid
            && localActor.Generation == actor.Generation)
        {
            await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        if (GetActorSpotNode() is not { } node)
        {
            await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await NotifyRemoteActorDisconnectedAsync(
                state,
                actor.ToBackend(),
                node,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ZLinkActorRuntimeState GetOrCreateActorState(string actorId)
    {
        return _actorSessionManager.GetOrCreateState(actorId);
    }

    internal ValueTask DeactivateActorOnOwnershipLossAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.DeactivateActorOnOwnershipLossAsync(actorId, cancellationToken);
    }

    internal ZLinkLocationLifecycle? LocationLifecycle =>
        Services.GetService(typeof(ZLinkLocationLifecycle)) as ZLinkLocationLifecycle;

    internal void BindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkSessionActor actorRef)
    {
        _sessionBindings.Bind(actorId, context, bindingToken, actorRef);
    }

    internal void UnbindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        _sessionBindings.Unbind(actorId, context, bindingToken);
    }

    internal bool TryGetSessionActorContext(
        string actorId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        return _sessionBindings.TryGet(actorId, bindingToken, out context);
    }

    internal bool TryGetSessionActorContext(
        string actorId,
        out ZLinkSessionContext context)
    {
        return _sessionBindings.TryGetByActorId(actorId, out context);
    }

    internal void BindActorSession(
        string actorId,
        RoutingId? sessionNodeRid,
        RoutingId sessionRid,
        string bindingToken)
    {
        GetOrCreateActorState(actorId).BindSession(sessionNodeRid, sessionRid, bindingToken);
        ActorBoundSessions.Register(this, actorId, sessionRid, bindingToken);
        LocationLifecycle?.ActorSessionRoutes.OnActorSessionBound(
            sessionRid,
            actorId,
            GetActorSpotNode()?.RoutingId ?? default);
    }

    internal void UnbindActorSession(
        string actorId,
        string bindingToken)
    {
        NotifyActorSessionRouteUnbound(actorId, bindingToken);
        GetOrCreateActorState(actorId).UnbindSession(bindingToken);
        ActorBoundSessions.Unregister(this, actorId, bindingToken);
    }

    internal void RemoveActorSessionBinding(
        string actorId,
        string bindingToken)
    {
        if (TryGetSessionActorContext(actorId, bindingToken, out var context))
            UnbindSessionActor(actorId, context, bindingToken);

        NotifyActorSessionRouteUnbound(actorId, bindingToken);
        GetOrCreateActorState(actorId).UnbindSession(bindingToken);
        ActorBoundSessions.Unregister(this, actorId, bindingToken);
    }

    private void NotifyActorSessionRouteUnbound(string actorId, string bindingToken)
    {
        if (LocationLifecycle is not { } lifecycle) return;

        if (GetOrCreateActorState(actorId).TryGetBoundSession(out var session)
            && string.Equals(session.BindingToken, bindingToken, StringComparison.Ordinal))
            lifecycle.ActorSessionRoutes.OnActorSessionUnbound(session.SessionRid);
    }

    internal void CleanupActorSessionsForSession(RoutingId sessionRid)
    {
        ActorBoundSessions.Cleanup(sessionRid);
    }

    internal bool TryGetActorBoundSession(
        string actorId,
        out ZLinkActorBoundSession session)
    {
        return GetOrCreateActorState(actorId).TryGetBoundSession(out session);
    }

    internal bool SendActorBoundSession(
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        var state = GetOrCreateActorState(actorId);
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Actor bound session send requires a router-capable SpotNode.",
                       false);
        var actorRef = state.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' does not have a native Actor ref.",
                           false);

        return node.SendActorBoundSession(actorRef, parts, flags);
    }

    internal void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        IReadOnlyList<Message> parts)
    {
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Actor no-bind reply requires a router-capable SpotNode.",
                       false);
        node.ReplyActorNoBind(actor, sourceNodeRid, sourceSessionRid, requestId, flags, parts);
    }

    internal bool ForwardActorBoundSessionPart(
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags)
    {
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorRouteNotFound,
                       "Actor session forward requires a router-capable SpotNode.",
                       false);

        return node.ForwardActorBoundSessionPart(
            actorRef,
            sourceNodeRid,
            sourceSessionRid,
            message,
            hasMore,
            flags);
    }

    private ValueTask NotifyRemoteActorDisconnectedAsync(
        ZLinkActorRuntimeState state,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!state.TryGetBoundSession(out var session)
            || session.SessionNodeRid is not { } sourceNodeRid)
        {
            node.CloseActorBoundSession(
                actorRef,
                Registration.DefaultRequestTimeout,
                cancellationToken);
            return ValueTask.CompletedTask;
        }

        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            ZLinkRemoteActorJoinPackets.SessionDisconnectedPacketName,
            ZlinkStreamMetadata.Empty);
        var headerBytes = ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray();
        using var headerPart = Message.From(headerBytes);
        using var bodyPart = Message.From(Array.Empty<byte>());

        if (!node.ForwardActorBoundSessionPart(
                actorRef,
                sourceNodeRid,
                session.SessionRid,
                headerPart,
                true,
                SendFlags.DontWait))
            throw new InvalidOperationException("Actor session disconnect header forward failed.");

        if (!node.ForwardActorBoundSessionPart(
                actorRef,
                sourceNodeRid,
                session.SessionRid,
                bodyPart,
                false,
                SendFlags.DontWait))
            throw new InvalidOperationException("Actor session disconnect body forward failed.");

        return ValueTask.CompletedTask;
    }

    internal ValueTask CloseActorBoundSessionAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var state = GetOrCreateActorState(actorId);
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Actor bound session close requires a router-capable SpotNode.",
                       false);
        var actorRef = state.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' does not have a native Actor ref.",
                           false);

        node.CloseActorBoundSession(
            actorRef,
            Registration.DefaultRequestTimeout,
            cancellationToken);
        return ValueTask.CompletedTask;
    }
}

internal sealed class ZLinkActorBoundSessionIndex
{
    private const string NativeBindingTokenPrefix = "native:";
    private readonly Dictionary<string, List<Entry>> _entries = new(StringComparer.Ordinal);
    private readonly object _gate = new();

    public void Register(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        RoutingId sessionRid,
        string bindingToken)
    {
        if (!IsNativeBindingToken(bindingToken)) return;

        var key = sessionRid.ToHex();
        lock (_gate)
        {
            if (!_entries.TryGetValue(key, out var entries))
            {
                entries = new List<Entry>();
                _entries[key] = entries;
            }

            entries.RemoveAll(entry => entry.Matches(runtime, actorId, bindingToken) || !entry.IsAlive);
            entries.Add(new Entry(new WeakReference<ZLinkFrameworkRuntime>(runtime), actorId, bindingToken));
        }
    }

    public void Unregister(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        string bindingToken)
    {
        if (!IsNativeBindingToken(bindingToken)) return;

        lock (_gate)
        {
            foreach (var key in _entries.Keys.ToArray())
            {
                var entries = _entries[key];
                entries.RemoveAll(entry => entry.Matches(runtime, actorId, bindingToken) || !entry.IsAlive);
                if (entries.Count == 0) _entries.Remove(key);
            }
        }
    }

    public void Cleanup(RoutingId sessionRid)
    {
        Entry[] entries;
        var key = sessionRid.ToHex();
        lock (_gate)
        {
            if (!_entries.Remove(key, out var registered)) return;

            entries = registered.ToArray();
        }

        foreach (var entry in entries)
            if (entry.Runtime.TryGetTarget(out var runtime))
                runtime.UnbindActorSession(entry.ActorId, entry.BindingToken);
    }

    private static bool IsNativeBindingToken(string bindingToken)
    {
        return bindingToken.StartsWith(NativeBindingTokenPrefix, StringComparison.Ordinal);
    }

    private sealed record Entry(
        WeakReference<ZLinkFrameworkRuntime> Runtime,
        string ActorId,
        string BindingToken)
    {
        public bool IsAlive => Runtime.TryGetTarget(out _);

        public bool Matches(
            ZLinkFrameworkRuntime runtime,
            string actorId,
            string bindingToken)
        {
            return Runtime.TryGetTarget(out var current)
                   && ReferenceEquals(current, runtime)
                   && string.Equals(ActorId, actorId, StringComparison.Ordinal)
                   && string.Equals(BindingToken, bindingToken, StringComparison.Ordinal);
        }
    }
}
