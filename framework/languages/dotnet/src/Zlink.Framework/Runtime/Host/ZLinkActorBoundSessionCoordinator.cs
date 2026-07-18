namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkActorBoundSessionCoordinator
{
    private readonly ZLinkActorBoundSessionRegistry _boundSessions;
    private readonly ZLinkSessionActorBindingTable _sessionBindings = new();
    private readonly object _remoteForwardGate = new();
    private readonly Dictionary<string, List<byte[]>> _remoteForwardBuffers = new(StringComparer.Ordinal);
    private readonly Func<string, ZLinkActorRuntimeState> _getState;
    private readonly Func<IZLinkBackendSpotNode?> _getNode;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly Func<CancellationToken> _getShutdownToken;

    public ZLinkActorBoundSessionCoordinator(
        Func<string, ZLinkActorRuntimeState> getState,
        Func<IZLinkBackendSpotNode?> getNode,
        ZLinkFrameworkRegistration registration,
        Func<CancellationToken> getShutdownToken)
    {
        _getState = getState;
        _getNode = getNode;
        _registration = registration;
        _getShutdownToken = getShutdownToken;
        _boundSessions = new ZLinkActorBoundSessionRegistry(UnbindActorSession);
    }

    /// <summary>Set by the runtime after construction: relays an encoded push
    /// frame to a remote session node as (actorId, sessionNodeRid, sessionRid,
    /// frame). Core's bound-session send is node-local, so a session bound on
    /// another node is reached through this framework-internal route packet
    /// (spec 31 §6 keeps the session route inside the framework).</summary>
    public Func<string, RoutingId, RoutingId, byte[], bool>? RemotePushRelay { get; set; }

    /// <summary>Set by the runtime after construction: relays a session frame
    /// to a bound actor that migrated to another node as (actorRef,
    /// sessionNodeRid, sessionRid, headerBytes, bodyBytes).</summary>
    public Func<ZLinkBackendActorRef, RoutingId, RoutingId, byte[], byte[], bool>? RemoteFrameRelay { get; set; }

    public enum RemotePushDelivery
    {
        Delivered,
        Backpressured,
        // No session-actor binding right now — transient during a rebind
        // (release→bind gap), so callers may retry briefly.
        NoBinding,
        // Bound to a different session: a definite new binding; late pushes
        // must not apply to it (spec 31 §6) — dropped.
        WrongSession
    }

    /// <summary>Session-node delivery for a relayed remote push: writes the
    /// frame to the still-bound local session. A binding or session-rid miss
    /// is a stale push racing rebind/disconnect and is dropped (spec 31 §6);
    /// a failed write is backpressure the caller may retry.</summary>
    public RemotePushDelivery DeliverLocalSessionFrame(
        string actorId, RoutingId sessionRid, byte[] frame)
    {
        if (!_sessionBindings.TryGetByActorId(actorId, out var context))
        {
            return RemotePushDelivery.NoBinding;
        }
        if (context.RoutingId is not { } boundRid || !boundRid.Equals(sessionRid))
        {
            return RemotePushDelivery.WrongSession;
        }
        using var message = Message.From(frame);
        var written = context.Write(message);
        return written ? RemotePushDelivery.Delivered : RemotePushDelivery.Backpressured;
    }

    public void BindSessionActor(string actorId, ZLinkSessionContext context, string bindingToken, ZLinkSessionActor actorRef) =>
        _sessionBindings.Bind(actorId, context, bindingToken, actorRef);

    public void UnbindSessionActor(string actorId, ZLinkSessionContext context, string bindingToken)
    {
        _sessionBindings.Unbind(actorId, context, bindingToken);
    }

    public bool TryGetSessionActorContext(string actorId, string bindingToken, out ZLinkSessionContext context) =>
        _sessionBindings.TryGet(actorId, bindingToken, out context);

    public bool TryGetSessionActorContext(string actorId, out ZLinkSessionContext context) =>
        _sessionBindings.TryGetByActorId(actorId, out context);

    public void BindActorSession(string actorId, RoutingId? sessionNodeRid, RoutingId sessionRid, string bindingToken)
    {
        _getState(actorId).BindSession(sessionNodeRid, sessionRid, bindingToken);
        _boundSessions.Register(actorId, sessionRid, bindingToken);
    }

    public void UnbindActorSession(string actorId, string bindingToken)
    {
        _getState(actorId).UnbindSession(bindingToken);
        _boundSessions.Unregister(actorId, bindingToken);
    }

    public void RemoveActorSessionBinding(string actorId, string bindingToken)
    {
        if (TryGetSessionActorContext(actorId, bindingToken, out var context))
            UnbindSessionActor(actorId, context, bindingToken);
        UnbindActorSession(actorId, bindingToken);
    }

    public void CleanupActorSessionsForSession(RoutingId sessionRid) => _boundSessions.Cleanup(sessionRid);

    public bool TryGetActorBoundSession(string actorId, out ZLinkActorBoundSession session) =>
        _getState(actorId).TryGetBoundSession(out session);

    public void ResetGeneration()
    {
        _sessionBindings.ResetGeneration();
        _boundSessions.Clear();
    }

    public bool Send(string actorId, IReadOnlyList<Message> parts, SendFlags flags)
    {
        var state = _getState(actorId);
        if (state.TryGetBoundSession(out var session))
        {
            if (TryGetSessionActorContext(actorId, session.BindingToken, out var context))
            {
                if (parts.Count != 1)
                    throw new InvalidOperationException("A local actor bound-session send requires one encoded stream frame.");
                return context.Write(parts[0]);
            }
            if (TryRelayRemotePush(actorId, session, ConcatParts(parts)))
                return true;
            if (!ZLinkActorBoundSessionBindingToken.IsNative(session.BindingToken))
                throw Error(ZLinkFrameworkErrorKind.ActorSessionNotBound,
                    $"Actor '{actorId}' no longer has the selected local session binding.", true);
        }

        var actorRef = state.NativeActorRef
                       ?? throw Error(ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' does not have a native Actor ref.", false);
        return RequireNode("Actor bound session send requires a router-capable SpotNode.")
            .SendActorBoundSession(actorRef, parts, flags);
    }

    public bool SendIfBoundTo(
        string actorId,
        string expectedBindingToken,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        var state = _getState(actorId);
        return state.TryUseBoundSession(
            expectedBindingToken,
            _ => Send(actorId, parts, flags));
    }

    public ZLinkAsyncSubmitter CreateSubmitter()
    {
        var node = RequireNode("Actor bound session send requires a router-capable SpotNode.");
        return new ZLinkAsyncSubmitter(node.OnSendReady, _registration.DefaultSocketSendTimeout, _getShutdownToken());
    }

    public void ReplyNoBind(ZLinkBackendActorRef actor, RoutingId sourceNodeRid, RoutingId sourceSessionRid,
        ulong requestId, uint flags, IReadOnlyList<Message> parts) =>
        RequireNode("Actor no-bind reply requires a router-capable SpotNode.")
            .ReplyActorNoBind(actor, sourceNodeRid, sourceSessionRid, requestId, flags, parts);

    public bool ForwardPart(ZLinkBackendActorRef actorRef, RoutingId sourceNodeRid, RoutingId sourceSessionRid,
        Message message, bool hasMore, SendFlags flags)
    {

        // A bound actor that migrated to another node cannot be reached
        // through the local bound-session send; buffer the parts and relay
        // the frame to the actor's owner node, which dispatches it through
        // its actor pipeline (replies come back on the push relay).
        if (RemoteFrameRelay is { } frameRelay
            && !actorRef.NodeRid.IsEmpty
            && _getNode() is { } frameLocalNode
            && !actorRef.NodeRid.Equals(frameLocalNode.RoutingId))
        {
            // Locally relayed frames carry no session identity; the actor's
            // bound-session state (or the local session-side binding registry
            // after a source-side migration cleared the actor-side state)
            // names the session this frame belongs to.
            if (sourceSessionRid.IsEmpty)
            {
                if (_getState(actorRef.ActorId).TryGetBoundSession(out var forwardSession)
                    && !forwardSession.SessionRid.IsEmpty)
                {
                    sourceSessionRid = forwardSession.SessionRid;
                    if (sourceNodeRid.IsEmpty && forwardSession.SessionNodeRid is { } forwardNode)
                        sourceNodeRid = forwardNode;
                }
                else if (_sessionBindings.TryGetByActorId(actorRef.ActorId, out var forwardContext)
                         && forwardContext.RoutingId is { } forwardRid)
                {
                    sourceSessionRid = forwardRid;
                }
            }

            lock (_remoteForwardGate)
            {
                var key = "frame:" + actorRef.ActorId;
                if (!_remoteForwardBuffers.TryGetValue(key, out var framePending))
                {
                    framePending = [];
                    _remoteForwardBuffers[key] = framePending;
                }

                framePending.Add(message.ToArray());
                if (hasMore) return true;
                var header = framePending[0];
                var bodyLength = 0;
                for (var i = 1; i < framePending.Count; i++) bodyLength += framePending[i].Length;
                var frameBody = new byte[bodyLength];
                var bodyOffset = 0;
                for (var i = 1; i < framePending.Count; i++)
                {
                    framePending[i].CopyTo(frameBody, bodyOffset);
                    bodyOffset += framePending[i].Length;
                }

                if (!frameRelay(actorRef, sourceNodeRid, sourceSessionRid, header, frameBody))
                {
                    // Retries resubmit only the terminal part; keep the
                    // buffered prefix so the frame stays whole.
                    framePending.RemoveAt(framePending.Count - 1);
                    return false;
                }

                _remoteForwardBuffers.Remove(key);
                return true;
            }
        }

        // A session on another node cannot be reached through the local
        // bound-session send; buffer the parts and relay the coalesced frame
        // to the session's node instead.
        if (RemotePushRelay is { } relay
            && !sourceNodeRid.IsEmpty
            && _getNode() is { } localNode
            && !sourceNodeRid.Equals(localNode.RoutingId))
        {
            lock (_remoteForwardGate)
            {
                if (!_remoteForwardBuffers.TryGetValue(actorRef.ActorId, out var pending))
                {
                    pending = [];
                    _remoteForwardBuffers[actorRef.ActorId] = pending;
                }

                pending.Add(message.ToArray());
                if (hasMore) return true;
                _remoteForwardBuffers.Remove(actorRef.ActorId);
                var total = 0;
                foreach (var part in pending) total += part.Length;
                var frame = new byte[total];
                var offset = 0;
                foreach (var part in pending)
                {
                    part.CopyTo(frame, offset);
                    offset += part.Length;
                }

                return relay(actorRef.ActorId, sourceNodeRid, sourceSessionRid, frame);
            }
        }

        return RequireNode("Actor session forward requires a router-capable SpotNode.", ZLinkFrameworkErrorKind.ActorRouteNotFound)
            .ForwardActorBoundSessionPart(actorRef, sourceNodeRid, sourceSessionRid, message, hasMore, flags);
    }

    private bool TryRelayRemotePush(string actorId, ZLinkActorBoundSession session, byte[] frame)
    {
        if (RemotePushRelay is not { } relay
            || session.SessionNodeRid is not { } sessionNodeRid
            || sessionNodeRid.IsEmpty)
        {
            return false;
        }
        if (_getNode() is not { } localNode || sessionNodeRid.Equals(localNode.RoutingId))
        {
            return false;
        }
        return relay(actorId, sessionNodeRid, session.SessionRid, frame);
    }

    private static byte[] ConcatParts(IReadOnlyList<Message> parts)
    {
        if (parts.Count == 1) return parts[0].ToArray();
        var buffers = new byte[parts.Count][];
        var total = 0;
        for (var i = 0; i < parts.Count; i++)
        {
            buffers[i] = parts[i].ToArray();
            total += buffers[i].Length;
        }

        var frame = new byte[total];
        var offset = 0;
        foreach (var buffer in buffers)
        {
            buffer.CopyTo(frame, offset);
            offset += buffer.Length;
        }

        return frame;
    }

    public ValueTask NotifyRemoteDisconnectedAsync(ZLinkActorRuntimeState state, ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!state.TryGetBoundSession(out var session) || session.SessionNodeRid is not { } sourceNodeRid)
        {
            node.CloseActorBoundSession(actorRef, _registration.DefaultRequestTimeout, cancellationToken);
            return ValueTask.CompletedTask;
        }

        var header = new ZlinkStreamHeader(ZlinkStreamMessageKind.Send, ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None, null, ZLinkRemoteActorJoinPackets.SessionDisconnectedPacketName,
            ZlinkStreamMetadata.Empty);
        using var headerPart = Message.From(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span);
        using var bodyPart = Message.From(Array.Empty<byte>());
        // The disconnect frame takes the same route as any session frame to
        // this actor: ForwardPart relays it to the actor's owner node when the
        // actor is remote and writes the native bound session when it is local.
        if (!ForwardPart(actorRef, sourceNodeRid, session.SessionRid, headerPart, true, SendFlags.DontWait))
            throw new InvalidOperationException("Actor session disconnect header forward failed.");
        if (!ForwardPart(actorRef, sourceNodeRid, session.SessionRid, bodyPart, false, SendFlags.DontWait))
            throw new InvalidOperationException("Actor session disconnect body forward failed.");
        return ValueTask.CompletedTask;
    }

    public ValueTask CloseAsync(string actorId, CancellationToken cancellationToken)
    {
        var actorRef = _getState(actorId).NativeActorRef
                       ?? throw Error(ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' does not have a native Actor ref.", false);
        RequireNode("Actor bound session close requires a router-capable SpotNode.")
            .CloseActorBoundSession(actorRef, _registration.DefaultRequestTimeout, cancellationToken);
        return ValueTask.CompletedTask;
    }

    private IZLinkBackendSpotNode RequireNode(string message,
        ZLinkFrameworkErrorKind kind = ZLinkFrameworkErrorKind.ActorSessionNotBound) =>
        _getNode() ?? throw Error(kind, message, false);

    private static ZLinkFrameworkException Error(ZLinkFrameworkErrorKind kind, string message, bool retryable) =>
        new(kind, message, retryable);
}
