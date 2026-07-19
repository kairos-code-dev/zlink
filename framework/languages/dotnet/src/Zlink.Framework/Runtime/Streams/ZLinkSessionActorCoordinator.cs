using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Streams;

using Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkSessionActorCoordinator(
    ZLinkFrameworkRuntime runtime,
    IZLinkStream stream)
{
    private readonly ZLinkSessionActorBindingRegistry _bindings = new(runtime);
    private readonly ZLinkBoundActorRelaySender _relaySender = new(runtime.Registration.DefaultRequestTimeout);

    public IReadOnlyCollection<IZLinkSessionActor> BoundActors => _bindings.BoundActors;

    public async ValueTask<IZLinkSessionActor> BindActorAsync(
        ZLinkSessionContext context,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        var metricStarted = ZLinkRuntimeMetrics.StartStreamSessionBind();
        try
        {
            var actorRef = ResolveActorRefForBinding(actor);
            await BindNativeActorAsync(actorRef, cancellationToken).ConfigureAwait(false);
            return await _bindings.BindAsync(
                context,
                actorRef.ToNative(),
                cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            ZLinkRuntimeMetrics.CompleteStreamSessionBind(metricStarted);
        }
    }

    public ValueTask<IZLinkSessionActor> BindActorAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        return BindActorCoreAsync(context, actor, cancellationToken);
    }

    public async ValueTask<IZLinkSessionActor> BindOrGetActorAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        if (_bindings.FindActor(actor.ActorId) is { } existing)
        {
            if (ActorRefsEqual(existing.Ref, actor)) return existing;

            if (existing is not ZLinkSessionActor sessionActor)
                throw new InvalidOperationException("Actor ref was not created by this framework runtime.");

            if (IsLocalActorRef(existing.Ref))
                await UnbindNativeActorAsync(existing.ActorId, cancellationToken).ConfigureAwait(false);
            await _bindings.ReleaseAsync(context, sessionActor, cancellationToken).ConfigureAwait(false);
        }

        return await BindActorCoreAsync(context, actor, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<IZLinkSessionActor> BindActorCoreAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        var metricStarted = ZLinkRuntimeMetrics.StartStreamSessionBind();
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
                await ConfirmRemoteBindingAsync(context, actor, cancellationToken).ConfigureAwait(false);
                return await _bindings.BindAsync(
                    context,
                    actor,
                    cancellationToken).ConfigureAwait(false);
            }
            catch (Exception bindingFailure)
            {
                try
                {
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
        return actor.NodeRid == runtime.GetActorClientSpotNode().RoutingId;
    }

    private async ValueTask ConfirmRemoteBindingAsync(
        ZLinkSessionContext context,
        ActorRef actor,
        CancellationToken cancellationToken)
    {
        // Contexts constructed before runtime startup are used only by the
        // transport-level unit boundary; no remote route exists to confirm.
        if (!runtime.IsStarted) return;
        if (context.RoutingId is not { } sessionRid)
            throw new InvalidOperationException("Actor session binding requires a stream routing id.");
        var sessionNodeRid = runtime.GetActorClientSpotNode().RoutingId;
        if (actor.NodeRid == sessionNodeRid) return;
        // The bind confirm can race auto-discovery admission of the actor's
        // node at startup; retriable route failures retry within the request
        // timeout instead of failing the session's first authenticate.
        var deadline = DateTime.UtcNow + runtime.Registration.DefaultRequestTimeout;
        ZLinkRemoteSessionBindResponse response;
        while (true)
        {
            try
            {
                response = await new ZLinkActorClient(runtime)
                    .RequestToActor(actor, new ZLinkRemoteSessionBindRequest(
                        sessionNodeRid.ToBytes().ToArray(),
                        sessionRid.ToBytes().ToArray()))
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

        if (runtime.Services.GetService<IZLinkActorDirectory>() is { } directory)
        {
            // A resolve miss with the row still present is the
            // transfer-commit window (a claimed-but-unpublished
            // generation-0 row while the actor moves nodes). The session
            // already holds a concrete bound ref: proceed with it — the
            // source incarnation's capture pipeline preserves in-flight
            // order — instead of stalling the frame behind the window.
            // A confirmed store miss is terminal: the actor was destroyed.
            var (current, rowPresent) = directory is ZLinkActorDirectory concreteDirectory
                ? await concreteDirectory.FindWithPresenceAsync(actorRef.ActorId, cancellationToken)
                    .ConfigureAwait(false)
                : (await directory.FindAsync(actorRef.ActorId, cancellationToken)
                    .ConfigureAwait(false), false);

            if (current is null && !rowPresent)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorRef.ActorId}' is no longer available.");

            if (current is { } resolved && !ActorRefsEqual(resolved, actorRef.Ref))
            {
                actorRef = (ZLinkSessionActor)await BindOrGetActorAsync(
                        actorRef.Context,
                        resolved,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
        }

        if (stream is ZLinkManagedStream managedStream)
        {
            if (!IsLocalActorRef(actorRef.Ref))
            {
                await ForwardToRemoteActorAsync(actorRef, header, payload, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await _relaySender.SendAsync(managedStream, actorRef, header, payload, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await DispatchLocalAsync(actorRef, header, payload, replyRawAsync, cancellationToken)
            .ConfigureAwait(false);
    }

    // Session frame for an actor hosted on another node: relayed to the
    // actor's owner node on the actor-frame plane (spec 31 §6). The relay
    // carries this session's identity, so the target binds the remote route
    // and replies/pushes travel back on the push relay.
    private async ValueTask ForwardToRemoteActorAsync(
        ZLinkSessionActor actorRef,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (actorRef.Context.RoutingId is not { } sessionRid)
            throw new InvalidOperationException("Actor session relay requires a stream routing id.");
        var sessionNodeRid = runtime.GetActorClientSpotNode().RoutingId;
        var headerBytes = ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray();
        var bodyBytes = payload.ToArray();
        var target = actorRef.Ref.ToBackend();
        await ZLinkRetryingSubmitter.Async(
                () =>
                {
                    using var headerPart = Message.From(headerBytes);
                    if (!runtime.ForwardActorBoundSessionPart(
                            target, sessionNodeRid, sessionRid, headerPart, true, SendFlags.DontWait))
                        return false;
                    using var bodyPart = Message.From(bodyBytes);
                    return runtime.ForwardActorBoundSessionPart(
                        target, sessionNodeRid, sessionRid, bodyPart, false, SendFlags.DontWait);
                },
                runtime.Registration.DefaultRequestTimeout,
                "Remote actor session relay failed because the relay route was not ready before timeout.",
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask NotifyActorDisconnectedAsync(
        IZLinkSessionActor actor,
        CancellationToken cancellationToken)
    {
        if (actor is not ZLinkSessionActor actorRef)
            throw new InvalidOperationException("Actor ref was not created by this framework runtime.");

        return runtime.NotifyActorDisconnectedAsync(
            actorRef.Ref,
            actorRef.BindingToken,
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

    private ZLinkBackendActorRef ResolveActorRefForBinding(IZLinkActor actor)
    {
        var actorId = actor.ActorId;
        if (runtime.TryGetCreatedActorState(actorId, out var state))
        {
            if (state.Actor is not null && !ReferenceEquals(state.Actor, actor))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorId}' is already created with a different actor instance.");

            return state.NativeActorRef
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorRouteNotFound,
                       $"Actor '{actorId}' does not have a native Actor ref.");
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor '{actorId}' is not created on the local actor runtime.");
    }

    private static bool ActorRefsEqual(ActorRef left, ActorRef right)
    {
        return left.ActorId == right.ActorId
               && left.NodeRid == right.NodeRid
               && left.Generation == right.Generation;
    }

    private static void EnsureConcreteActorRef(ActorRef actor)
    {
        if (actor.NodeRid.IsEmpty || actor.Generation == 0)
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
