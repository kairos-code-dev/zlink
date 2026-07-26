namespace Zlink.Framework.Runtime.Streams;

using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkSessionActorCoordinator(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream,
    string? actorDispatchMeshName)
{
    private readonly ZLinkSessionActorBindingRegistry _bindings =
        new(runtime);
    private readonly object _actorOperationGatesLock = new();
    private readonly Dictionary<string, ActorOperationGate> _actorOperationGates =
        new(StringComparer.Ordinal);

    private string ActorDispatchMeshName => actorDispatchMeshName
        ?? throw new ZLinkConfigurationException(
            "STREAM Actor dispatch requires EnableActorDispatch(meshName).");

    public IReadOnlyCollection<IZLinkSessionActor> BoundActors => _bindings.BoundActors;

    public ValueTask<IZLinkSessionActor> BindActorAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        return BindActorSerializedAsync(
            context,
            actor,
            allowExisting: false,
            cancellationToken: cancellationToken);
    }

    public ValueTask<IZLinkSessionActor> BindOrGetActorAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        return BindActorSerializedAsync(
            context,
            actor,
            allowExisting: true,
            cancellationToken: cancellationToken);
    }

    private async ValueTask<IZLinkSessionActor> BindActorSerializedAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        bool allowExisting,
        CancellationToken cancellationToken)
    {
        ActorOperationGate operation;
        lock (_actorOperationGatesLock)
        {
            if (!_actorOperationGates.TryGetValue(actor.ActorId, out operation!))
            {
                operation = new ActorOperationGate();
                _actorOperationGates.Add(actor.ActorId, operation);
            }
            operation.Users++;
        }
        var acquired = false;
        try
        {
            await operation.Gate.WaitAsync(cancellationToken).ConfigureAwait(false);
            acquired = true;
            return await BindActorWithinGateAsync(
                    context,
                    actor,
                    allowExisting,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            if (acquired)
                operation.Gate.Release();
            lock (_actorOperationGatesLock)
            {
                operation.Users--;
                if (operation.Users == 0
                    && _actorOperationGates.Remove(actor.ActorId, out var removed)
                    && ReferenceEquals(removed, operation))
                    operation.Gate.Dispose();
            }
        }
    }

    private sealed class ActorOperationGate
    {
        internal SemaphoreSlim Gate { get; } = new(1, 1);
        internal int Users { get; set; }
    }

    private async ValueTask<IZLinkSessionActor> BindActorWithinGateAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        bool allowExisting,
        CancellationToken cancellationToken)
    {
        if (_bindings.FindActor(actor.ActorId) is { } existing)
        {
            if (allowExisting && ActorRefsEqual(existing.Ref, actor))
                return existing;

            if (existing is not ZLinkSessionActor existingActor)
                throw new InvalidOperationException("Actor ref was not created by this framework runtime.");
            if (!runtime.TryGetSessionActorBinding(
                    existing.ActorId,
                    existingActor.BindingToken,
                    out var previousEntry))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorSessionNotBound,
                    $"Actor '{existing.ActorId}' replacement lost its previous binding identity.");
            var previousIdentity = new ZLinkActorBoundSession(
                null,
                existingActor.SessionRid,
                previousEntry.BindingToken,
                previousEntry.BindingGeneration,
                previousEntry.ObjectGeneration,
                previousEntry.AuthorityOwnerGeneration,
                previousEntry.MeshName,
                previousEntry.TargetNodeGeneration,
                previousEntry.OwnerLeaseGeneration,
                previousEntry.SessionOwnerNodeGeneration,
                previousEntry.AcceptedHighWater);
            IZLinkSessionActor replacement;
            try
            {
                // BindAsync replaces the previous table entry only after the
                // new exact owner has acknowledged. Until then the previous
                // binding remains the terminal route for this session.
                replacement = await BindActorCoreAsync(
                        context,
                        actor,
                        new ZLinkRemoteSessionPreviousBinding(
                            existing.Ref.NodeRid.ToBytes().ToArray(),
                            previousIdentity.BindingToken,
                            previousIdentity.BindingGeneration,
                            previousIdentity.ObjectGeneration,
                            previousIdentity.MeshName,
                            previousIdentity.TargetNodeGeneration,
                            previousIdentity.AuthorityOwnerGeneration,
                            previousIdentity.OwnerLeaseGeneration,
                            previousIdentity.SessionOwnerNodeGeneration),
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception replacementFailure)
            {
                if (!IsLocalActorRef(existing.Ref)) throw;
                try
                {
                    await BindNativeActorAsync(
                            existing.Ref.ToBackend(),
                            CancellationToken.None)
                        .ConfigureAwait(false);
                }
                catch (Exception rollbackFailure)
                {
                    throw new AggregateException(
                        replacementFailure,
                        rollbackFailure);
                }

                throw;
            }
            return replacement;
        }

        return await BindActorCoreAsync(context, actor, null, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<IZLinkSessionActor> BindActorCoreAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        ZLinkRemoteSessionPreviousBinding? previousBinding,
        CancellationToken cancellationToken)
    {
        var metricStarted = ZLinkRuntimeMetrics.StartStreamSessionBind();
        ZLinkSessionBindingIdentity? confirmedIdentity = null;
        try
        {
            EnsureConcreteActorRef(actor);
            var actorRef = actor.ToBackend();
            // The native session binding requires a node-local actor; a remote
            // actor binds through the framework route instead (the remote node
            // acknowledges via ConfirmRemoteBindingAsync and session frames
            // travel on the actor-frame relay plane).
            var nativeBound = IsLocalActorRef(actor);
            if (nativeBound)
                await BindNativeActorAsync(actorRef, cancellationToken).ConfigureAwait(false);
            try
            {
                var identity = await ConfirmBindingAsync(
                        context,
                        actor,
                        previousBinding,
                        cancellationToken)
                    .ConfigureAwait(false);
                confirmedIdentity = identity;
                return await _bindings.BindAsync(
                    context,
                    actor,
                    identity.BindingToken,
                    identity.BindingGeneration,
                    identity.AuthorityOwnerGeneration,
                    identity.MeshName,
                    identity.TargetNodeGeneration,
                    identity.OwnerLeaseGeneration,
                    identity.SessionOwnerNodeGeneration,
                    cancellationToken).ConfigureAwait(false);
            }
            catch (Exception bindingFailure)
            {
                try
                {
                    if (runtime.IsStarted
                        && confirmedIdentity is { } identity)
                        await RevokeBindingAsync(
                                actor,
                                new ZLinkActorBoundSession(
                                    null,
                                    context.RoutingId
                                    ?? throw new InvalidOperationException(
                                        "Actor session binding requires a stream routing id."),
                                    identity.BindingToken,
                                    identity.BindingGeneration,
                                    actor.ObjectGeneration,
                                    identity.AuthorityOwnerGeneration,
                                    identity.MeshName,
                                    identity.TargetNodeGeneration,
                                    identity.OwnerLeaseGeneration,
                                    identity.SessionOwnerNodeGeneration,
                                    AcceptedHighWater: 0),
                                CancellationToken.None)
                            .ConfigureAwait(false);
                    else if (confirmedIdentity is { } localIdentity)
                        runtime.UnbindActorSession(
                            actor.ActorId,
                            localIdentity.BindingToken);
                    if (nativeBound)
                        await UnbindNativeActorAsync(actor.ActorId, CancellationToken.None)
                            .ConfigureAwait(false);
                }
                catch (Exception rollbackFailure)
                {
                    throw new AggregateException(bindingFailure, rollbackFailure);
                }

                throw;
            }
        }
        finally
        {
            ZLinkRuntimeMetrics.CompleteStreamSessionBind(metricStarted);
        }
    }

    private bool IsLocalActorRef(ActorRef actor)
    {
        if (!runtime.IsStarted) return true;
        return actor.NodeRid == runtime.GetMeshNodeRuntime(actor.MeshName).Node.RoutingId;
    }

    private async ValueTask<ZLinkSessionBindingIdentity> ConfirmBindingAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        ZLinkRemoteSessionPreviousBinding? previousBinding,
        CancellationToken cancellationToken)
    {
        // Contexts constructed before runtime startup are used only by the
        // transport-level unit boundary; no remote route exists to confirm.
        var bindingGeneration = runtime.NextSessionBindingGeneration();
        if (!runtime.IsStarted)
        {
            if (context.RoutingId is not { } localSessionRid)
                throw new InvalidOperationException(
                    "Actor session binding requires a stream routing id.");
            var localBindingToken = Guid.NewGuid().ToString("N");
            const ulong localGeneration = 1;
            runtime.BindActorSession(
                actor.ActorId,
                null,
                localSessionRid,
                localBindingToken,
                bindingGeneration,
                actor.ObjectGeneration,
                localGeneration,
                actor.MeshName,
                localGeneration,
                localGeneration,
                localGeneration);
            return new ZLinkSessionBindingIdentity(
                localBindingToken,
                bindingGeneration,
                actor.MeshName,
                TargetNodeGeneration: localGeneration,
                OwnerLeaseGeneration: localGeneration,
                AuthorityOwnerGeneration: localGeneration,
                SessionOwnerNodeGeneration: localGeneration);
        }
        if (context.RoutingId is not { } sessionRid)
            throw new InvalidOperationException("Actor session binding requires a stream routing id.");
        var sessionNode = runtime.GetMeshNodeRuntime(actor.MeshName).Node;
        var sessionNodeRid = sessionNode.RoutingId;
        var sessionOwnerNodeGeneration = sessionNode.MeshStatus().LifecycleGeneration;
        if (sessionOwnerNodeGeneration == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                "Session owner lifecycle generation is unavailable.");
        var bindingToken = Guid.NewGuid().ToString("N");
        var request = new ZLinkRemoteSessionBindRequest(
            actor.ActorId,
            actor.NodeRid.ToBytes().ToArray(),
            sessionNodeRid.ToBytes().ToArray(),
            sessionRid.ToBytes().ToArray(),
            bindingToken,
            bindingGeneration,
            actor.ObjectGeneration,
            actor.MeshName,
            sessionOwnerNodeGeneration,
            AcceptedHighWater: 0,
            PreviousBinding: previousBinding);
        // The bind confirm can race auto-discovery admission of the actor's
        // node at startup; retriable route failures retry within the request
        // timeout instead of failing the session's first authenticate.
        var deadline = DateTime.UtcNow + runtime.Registration.DefaultRequestTimeout;
        ZLinkRemoteSessionBindResponse response;
        while (true)
        {
            try
            {
                response = actor.NodeRid == sessionNodeRid
                    ? await runtime.BindRemoteBoundSessionRouteAsync(
                            request,
                            sessionNodeRid,
                            cancellationToken)
                        .ConfigureAwait(false)
                    : await runtime.Services.GetRequiredService<IZLinkRouteClient>()
                        .RequestToNode(actor.MeshName, actor.NodeRid, request)
                        .Timeout(runtime.Registration.DefaultRequestTimeout)
                        .Async<ZLinkRemoteSessionBindResponse>(cancellationToken)
                        .ConfigureAwait(false);
                break;
            }
            catch (ZLinkFrameworkException failure)
                when ((failure.IsRetriable
                       || failure.Kind == ZLinkFrameworkErrorKind.ActorRouteNotFound)
                      && DateTime.UtcNow < deadline)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(50), cancellationToken)
                    .ConfigureAwait(false);
            }
        }

        if (!response.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{actor.ActorId}' did not acknowledge its remote session binding.");
        var responseTargetNodeRid = RoutingId.From(response.TargetNodeRid);
        if (response.ObjectGeneration != actor.ObjectGeneration
            || responseTargetNodeRid != actor.NodeRid
            || !string.Equals(response.MeshName, actor.MeshName, StringComparison.Ordinal)
            || response.TargetNodeGeneration == 0
            || response.AuthorityOwnerGeneration == 0
            || response.OwnerLeaseGeneration == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{actor.ActorId}' returned a different route during session bind.");
        return new ZLinkSessionBindingIdentity(
            bindingToken,
            bindingGeneration,
            response.MeshName,
            response.TargetNodeGeneration,
            response.OwnerLeaseGeneration,
            response.AuthorityOwnerGeneration,
            sessionOwnerNodeGeneration);
    }

    private async ValueTask RevokeBindingAsync(
        ActorRef actor,
        ZLinkActorBoundSession identity,
        CancellationToken cancellationToken)
    {
        var request = new ZLinkRemoteSessionUnbindRequest(
            actor.ActorId,
            actor.NodeRid.ToBytes().ToArray(),
            identity.BindingToken,
            identity.BindingGeneration,
            actor.ObjectGeneration,
            actor.MeshName,
            identity.TargetNodeGeneration,
            identity.AuthorityOwnerGeneration,
            identity.OwnerLeaseGeneration,
            identity.SessionOwnerNodeGeneration);
        var localNodeRid = runtime.GetMeshNodeRuntime(actor.MeshName).Node.RoutingId;
        var response = actor.NodeRid == localNodeRid
            ? await runtime.UnbindRemoteBoundSessionRouteAsync(
                    request,
                    cancellationToken)
                .ConfigureAwait(false)
            : await runtime.Services.GetRequiredService<IZLinkRouteClient>()
                .RequestToNode(actor.MeshName, actor.NodeRid, request)
                .Timeout(runtime.Registration.DefaultRequestTimeout)
                .Async<ZLinkRemoteSessionUnbindResponse>(cancellationToken)
                .ConfigureAwait(false);
        if (!response.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{actor.ActorId}' did not invalidate its previous session binding.");
    }

    public IZLinkSessionActor? FindActor(string actorId)
    {
        return _bindings.FindActor(actorId);
    }

    public async ValueTask RelayToActorAsync(
        IZLinkSessionActor actor,
        ZlinkStreamHeader header,
        Message payload,
        Func<ZlinkStreamHeader, ZLinkActorReply, CancellationToken, ValueTask> replyRawAsync,
        CancellationToken cancellationToken)
    {
        if (actor is not ZLinkSessionActor actorRef)
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");

        if (!runtime.TryGetSessionActorContext(
                actorRef.ActorId,
                actorRef.BindingToken,
                out var currentContext)
            || !ReferenceEquals(currentContext, actorRef.Context))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{actorRef.ActorId}' session binding is stale.",
                true);
        var isBindingControlFrame = string.Equals(
            header.Name,
            ZLinkRemoteSessionBindingProtocol.PacketName,
            StringComparison.Ordinal);
        var acceptedFrame = false;
        if (!isBindingControlFrame
            && !runtime.TryAcceptSessionActorFrame(
                actorRef.ActorId,
                actorRef.BindingToken,
                out _))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{actorRef.ActorId}' session binding changed before frame admission.",
                true);
        if (!isBindingControlFrame)
            acceptedFrame = true;

        try
        {
            if (stream is ZLinkManagedStream)
            {
                var route = actorRef.Route;
                if (!IsLocalActorRef(route))
                {
                    var requestId = header.Kind == ZlinkStreamMessageKind.Request
                        ? header.RequestSeq
                        : null;
                    string? replyCapability = null;
                    if (requestId is { } pendingRequestId)
                    {
                        replyCapability = runtime.TrackRemoteSessionActorRequest(
                            actorRef.ActorId,
                            pendingRequestId.Value,
                            actorRef.BindingToken);
                        acceptedFrame = false;
                    }
                    try
                    {
                        await ForwardToRemoteActorAsync(
                                actorRef,
                                header,
                                payload,
                                replyCapability,
                                cancellationToken)
                            .ConfigureAwait(false);
                    }
                    catch
                    {
                        if (requestId is { } failedRequestId)
                            runtime.CompleteRemoteSessionActorRequest(
                                actorRef.ActorId,
                                failedRequestId.Value);
                        throw;
                    }
                    return;
                }
            }

            if (!isBindingControlFrame)
                runtime.GetOrCreateActorState(actorRef.ActorId)
                    .RecordBoundSessionAccepted(actorRef.BindingToken);
            await DispatchLocalAsync(actorRef, header, payload, replyRawAsync, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            if (acceptedFrame)
                runtime.CompleteAcceptedSessionActorFrame(
                    actorRef.ActorId,
                    actorRef.BindingToken);
        }
    }

    // Session frame for an actor hosted on another node: relayed to the
    // actor's owner node on the actor-frame plane (spec 31 §6). The relay
    // carries this session's identity, so the target binds the remote route
    // and replies/pushes travel back on the push relay.
    private async ValueTask ForwardToRemoteActorAsync(
        ZLinkSessionActor actorRef,
        ZlinkStreamHeader header,
        Message payload,
        string? replyCapability,
        CancellationToken cancellationToken)
    {
        if (actorRef.Context.RoutingId is not { } sessionRid)
            throw new InvalidOperationException("Actor session relay requires a stream routing id.");
        var route = actorRef.Route;
        var sessionNodeRid = runtime.GetMeshNodeRuntime(route.MeshName).Node.RoutingId;
        var headerBytes = ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray();
        var bodyBytes = payload.ToArray();
        var target = route.Ref.ToBackend();
        var replyRoute = header.Kind == ZlinkStreamMessageKind.Request
                         && header.RequestSeq is { } requestSeq
            ? new ZLinkBackendActorRouteContext(
                OperationId: default,
                ForwardingHopCount: 0,
                TargetNodeGeneration: route.TargetNodeGeneration,
                AuthorityOwnerGeneration: route.AuthorityOwnerGeneration,
                OwnerLeaseGeneration: route.OwnerLeaseGeneration,
                ReplyRequestId: requestSeq.Value,
                ReplyFlags: ZLinkActorBoundSessionRelay.ActorRecvInfoNoBind,
                ReplyCapability: replyCapability)
            : default;
        await ZLinkRetryingSubmitter.Async(
                () =>
                {
                    using var headerPart = Message.From(headerBytes);
                    if (!runtime.ForwardActorBoundSessionPart(
                            route.MeshName,
                            target,
                            route.TargetNodeGeneration,
                            route.AuthorityOwnerGeneration,
                            route.OwnerLeaseGeneration,
                            sessionNodeRid,
                            sessionRid,
                            headerPart,
                            true,
                            SendFlags.DontWait,
                            replyRoute))
                        return false;
                    using var bodyPart = Message.From(bodyBytes);
                    return runtime.ForwardActorBoundSessionPart(
                        route.MeshName,
                        target,
                        route.TargetNodeGeneration,
                        route.AuthorityOwnerGeneration,
                        route.OwnerLeaseGeneration,
                        sessionNodeRid,
                        sessionRid,
                        bodyPart,
                        false,
                        SendFlags.DontWait,
                        replyRoute);
                },
                runtime.Registration.DefaultRequestTimeout,
                "Remote actor session relay failed because the relay route was not ready before timeout.",
                cancellationToken)
            .ConfigureAwait(false);
    }

    private bool IsLocalActorRef(ZLinkSessionBindingRoute route)
    {
        if (!runtime.IsStarted) return true;
        var local = runtime.GetMeshNodeRuntime(route.MeshName).Node;
        return route.Ref.NodeRid == local.RoutingId
               && route.TargetNodeGeneration
               == local.MeshStatus().LifecycleGeneration;
    }

    public ValueTask NotifyActorDisconnectedAsync(
        IZLinkSessionActor actor,
        CancellationToken cancellationToken)
    {
        if (actor is not ZLinkSessionActor actorRef)
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");

        if (!runtime.TryGetSessionActorBinding(
                actorRef.ActorId,
                actorRef.BindingToken,
                out var binding)
            || !ReferenceEquals(binding.ActorRef, actorRef)
            || binding.Context.RoutingId is null)
            return ValueTask.CompletedTask;
        return runtime.NotifyActorDisconnectedAsync(
            binding,
            cancellationToken);
    }

    public ValueTask CleanupAsync(
        ZLinkSessionContext context,
        CancellationToken cancellationToken)
    {
        return _bindings.CleanupAsync(context, cancellationToken);
    }

    private async ValueTask DispatchLocalAsync(
        ZLinkSessionActor actorRef,
        ZlinkStreamHeader header,
        Message payload,
        Func<ZlinkStreamHeader, ZLinkActorReply, CancellationToken, ValueTask> replyRawAsync,
        CancellationToken cancellationToken)
    {
        using var actorPayload = payload.Copy();
        // Only genuine requests take the reply path (a relayed Send can
        // carry a request seq from stream-level bookkeeping).
        if (header.Kind == ZlinkStreamMessageKind.Request && header.RequestSeq is not null)
        {
            await using var boundSessionScope = ZLinkBoundSessionDispatchScope.Enter(actorRef.ActorId);
            try
            {
                var reply = await runtime.SubmitActorForReplyAsync(
                        actorRef.ActorId,
                        header,
                        actorPayload,
                        cancellationToken)
                    .ConfigureAwait(false);
                await replyRawAsync(header, reply, cancellationToken)
                    .ConfigureAwait(false);
            }
            finally
            {
                await boundSessionScope.DrainAsync(cancellationToken)
                    .ConfigureAwait(false);
            }
            return;
        }

        await runtime.SubmitActorByIdAsync(
                actorRef.ActorId,
                header,
                actorPayload,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static bool ActorRefsEqual(ActorRef left, ActorRef right)
    {
        return left.ActorId == right.ActorId
               && left.NodeRid == right.NodeRid
               && left.ObjectGeneration == right.ObjectGeneration
               && string.Equals(left.MeshName, right.MeshName, StringComparison.Ordinal);
    }

    private static void EnsureConcreteActorRef(ActorRef actor)
    {
        if (actor.NodeRid.IsEmpty || actor.ObjectGeneration == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                "Actor ref requires a target SpotNode routing id and concrete actor generation.",
                false);
    }

    private async ValueTask BindNativeActorAsync(
        ZLinkBackendActorRef actorRef,
        CancellationToken cancellationToken)
    {
        await ZLinkNativeActorStreamBinding.BindAsync(
                stream,
                actorRef,
                runtime.Registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask UnbindNativeActorAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        await ZLinkNativeActorStreamBinding.UnbindAsync(
                stream,
                actorId,
                runtime.Registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);
    }
}

internal readonly record struct ZLinkSessionBindingIdentity(
    string BindingToken,
    ulong BindingGeneration,
    string MeshName,
    ulong TargetNodeGeneration,
    ulong OwnerLeaseGeneration,
    ulong AuthorityOwnerGeneration,
    ulong SessionOwnerNodeGeneration);
