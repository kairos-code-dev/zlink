namespace Zlink.Framework.Runtime.Core;

internal sealed partial class ZLinkFrameworkRuntime
{
    private static readonly ZLinkActorBoundSessionIndex BoundSessionIndex = new();

    internal async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        return await _actors.JoinActorAsync<TRequest, TReply>(
            spotRid,
            actor,
            request,
            cancellationToken);
    }

    internal async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
        => await _actors.JoinActorToSpotAsync(activation, actor, cancellationToken);

    internal async ValueTask LeaveActorFromSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
        => await _actors.LeaveActorFromSpotAsync(activation, actor, cancellationToken);

    internal async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
        => await _actors.AttachActorAsync(actor, stream, cancellationToken);

    internal async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
        => await _actors.DisconnectActorAsync(actor, stream, cancellationToken);

    internal async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorAsync(actor, header, payload, cancellationToken);

    internal async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
        => await _actors.CreateLocalActorAsync(actorId, actorType, cancellationToken);

    internal async ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
        => await _actors.CreateActorAsync(actorId, actorType, cancellationToken);

    internal async ValueTask<IZLinkActor?> FindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
        => await _actors.FindActorAsync(actorId, cancellationToken);

    internal bool TryGetCreatedActor(
        string actorId,
        string actorType,
        out IZLinkActor actor)
        => _actors.TryGetCreatedActor(actorId, actorType, out actor);

    internal bool TryGetCreatedActorState(
        string actorId,
        string actorType,
        out ZLinkActorRuntimeState state)
        => _actors.TryGetCreatedActorState(actorId, actorType, out state);

    internal async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken);

    internal async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorByIdAsync(actorId, header, payload, cancellationToken);

    internal async ValueTask NotifyActorDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
        => await _actors.NotifyDisconnectedByIdAsync(actorId, cancellationToken);

    internal ZLinkActorRuntimeState GetOrCreateActorState(string actorId)
        => _actors.GetOrCreateActorState(actorId);

    internal ZLinkActorRemoteAddress ResolveLocalActorRemoteAddress(
        string actorId,
        string actorType)
    {
        if (!TryGetCreatedActorState(actorId, actorType, out var state)
            || state.NativeActorRef is not { } actorRef)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' does not have a native Actor ref.");
        }

        return new ZLinkActorRemoteAddress(
            string.Empty,
            actorRef.NodeRid,
            actorRef.Generation);
    }

    internal void BindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkActorRef actorRef)
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

    internal void BindActorSession(
        string actorId,
        RoutingId sessionRid,
        string bindingToken)
    {
        GetOrCreateActorState(actorId).BindSession(sessionRid, bindingToken);
        BoundSessionIndex.Register(this, actorId, sessionRid, bindingToken);
    }

    internal void UnbindActorSession(
        string actorId,
        string bindingToken)
    {
        GetOrCreateActorState(actorId).UnbindSession(bindingToken);
        BoundSessionIndex.Unregister(this, actorId, bindingToken);
    }

    internal void CleanupActorSessionsForSession(RoutingId sessionRid)
    {
        BoundSessionIndex.Cleanup(sessionRid);
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
                "Actor bound session send requires an ActorGateway SpotNode.",
                isRetriable: false);
        var actorRef = state.NativeActorRef
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' does not have a native Actor ref.",
                isRetriable: false);

        return node.SendActorBoundSession(actorRef, parts, flags);
    }

    internal async ValueTask CloseActorBoundSessionAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var state = GetOrCreateActorState(actorId);
        var node = GetActorSpotNode()
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                "Actor bound session close requires an ActorGateway SpotNode.",
                isRetriable: false);
        var actorRef = state.NativeActorRef
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' does not have a native Actor ref.",
                isRetriable: false);

        await node.CloseActorBoundSessionAsync(
                actorRef,
                Registration.DefaultTimeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

}

internal sealed class ZLinkActorBoundSessionIndex
{
    private const string NativeBindingTokenPrefix = "native:";
    private readonly object _gate = new();
    private readonly Dictionary<string, List<Entry>> _entries = new(StringComparer.Ordinal);

    public void Register(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        RoutingId sessionRid,
        string bindingToken)
    {
        if (!IsNativeBindingToken(bindingToken))
        {
            return;
        }

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
        if (!IsNativeBindingToken(bindingToken))
        {
            return;
        }

        lock (_gate)
        {
            foreach (var key in _entries.Keys.ToArray())
            {
                var entries = _entries[key];
                entries.RemoveAll(entry => entry.Matches(runtime, actorId, bindingToken) || !entry.IsAlive);
                if (entries.Count == 0)
                {
                    _entries.Remove(key);
                }
            }
        }
    }

    public void Cleanup(RoutingId sessionRid)
    {
        Entry[] entries;
        var key = sessionRid.ToHex();
        lock (_gate)
        {
            if (!_entries.Remove(key, out var registered))
            {
                return;
            }

            entries = registered.ToArray();
        }

        foreach (var entry in entries)
        {
            if (entry.Runtime.TryGetTarget(out var runtime))
            {
                runtime.UnbindActorSession(entry.ActorId, entry.BindingToken);
            }
        }
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
