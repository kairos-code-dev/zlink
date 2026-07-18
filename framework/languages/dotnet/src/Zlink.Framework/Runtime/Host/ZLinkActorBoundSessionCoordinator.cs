namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkActorBoundSessionCoordinator
{
    private readonly ZLinkActorBoundSessionRegistry _boundSessions;
    private readonly ZLinkSessionActorBindingTable _sessionBindings = new();
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

    public void BindSessionActor(string actorId, ZLinkSessionContext context, string bindingToken, ZLinkSessionActor actorRef) =>
        _sessionBindings.Bind(actorId, context, bindingToken, actorRef);

    public void UnbindSessionActor(string actorId, ZLinkSessionContext context, string bindingToken) =>
        _sessionBindings.Unbind(actorId, context, bindingToken);

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
        Message message, bool hasMore, SendFlags flags) =>
        RequireNode("Actor session forward requires a router-capable SpotNode.", ZLinkFrameworkErrorKind.ActorRouteNotFound)
            .ForwardActorBoundSessionPart(actorRef, sourceNodeRid, sourceSessionRid, message, hasMore, flags);

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
        if (!node.ForwardActorBoundSessionPart(actorRef, sourceNodeRid, session.SessionRid, headerPart, true, SendFlags.DontWait))
            throw new InvalidOperationException("Actor session disconnect header forward failed.");
        if (!node.ForwardActorBoundSessionPart(actorRef, sourceNodeRid, session.SessionRid, bodyPart, false, SendFlags.DontWait))
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
