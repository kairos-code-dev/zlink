namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkActorCaptureDebug
{
    public static bool PipelineCapture(ZLinkSpotActorFrame frame)
    {
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine(
                $"[pipeline] captured actor={frame.Actor.ActorId} name={frame.Header.Name}");
        return true;
    }
}

internal interface IZLinkActorInboundEndpoint
{
    IZLinkActor? ResolveActor(ZLinkActorRuntimeState state);

    ValueTask NotifyDisconnectedAsync(
        string actorId,
        CancellationToken cancellationToken);

    ValueTask DispatchAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken);

    ValueTask<ZLinkActorReply?> DispatchForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken);
}

internal sealed class ZLinkActorInboundPipeline(
    ZLinkFrameworkRuntime runtime,
    IZLinkActorInboundEndpoint endpoint)
{
    public async ValueTask DispatchAsync(
        ZLinkSpotActorFrameBatch frames,
        CancellationToken cancellationToken)
    {
        Exception? dispatchFailure = null;
        try
        {
            for (var i = 0; i < frames.Count; i++)
            {
                using var frame = frames[i];
                try
                {
                    await DispatchFrameAsync(frame, cancellationToken, allowCapture: true)
                        .ConfigureAwait(false);
                }
                catch (Exception exception)
                {
                    dispatchFailure = dispatchFailure is null
                        ? exception
                        : new AggregateException(dispatchFailure, exception);
                }
            }

            if (dispatchFailure is not null) throw dispatchFailure;
        }
        finally
        {
            frames.Dispose();
        }
    }

    public async ValueTask DispatchReplayAsync(
        ZLinkSpotActorFrameBatch frames,
        Action acknowledgeFrame,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(acknowledgeFrame);
        try
        {
            for (var i = 0; i < frames.Count; i++)
            {
                using var frame = frames[i];
                await DispatchFrameAsync(
                        frame,
                        cancellationToken,
                        allowCapture: false,
                        acknowledgeHandledFrame: acknowledgeFrame)
                    .ConfigureAwait(false);
            }
        }
        finally
        {
            frames.Dispose();
        }
    }

    private async ValueTask DispatchFrameAsync(
        ZLinkSpotActorFrame frame,
        CancellationToken cancellationToken,
        bool allowCapture,
        Action? acknowledgeHandledFrame = null)
    {
        using var flow = ZLinkFlowContext.Enter(
            frame.Header.FlowId,
            frame.Header.FlowOrigin is { } streamOrigin
                ? (ZLinkFlowOrigin)(byte)streamOrigin
                : null,
            runtime.Flow.CaptureEnabled,
            ZLinkFlowOrigin.Inbound);
        var state = runtime.GetOrCreateActorState(frame.Actor.ActorId);
        var debugPump = Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1";
        if (allowCapture && state.Handoff.TryCapture(frame) && ZLinkActorCaptureDebug.PipelineCapture(frame))
        {
            if (debugPump)
                Console.Error.WriteLine($"[dispatch] captured actor={frame.Actor.ActorId} name={frame.Header.Name}");
            return;
        }
        if (debugPump)
            Console.Error.WriteLine(
                $"[dispatch] frame actor={frame.Actor.ActorId} name={frame.Header.Name} blocked={state.IsDispatchBlocked}");
        if (state.IsDispatchBlocked)
        {
            await ZLinkActorBoundSessionRelay.ReplyStaleActorAsync(
                    runtime,
                    frame.Actor,
                    frame.SourceNodeRid,
                    frame.SourceSessionRid,
                    frame.RequestId,
                    frame.Flags,
                    frame.Header,
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Actor '{frame.Actor.ActorId}' is being destroyed."),
                    cancellationToken)
                .ConfigureAwait(false);
            acknowledgeHandledFrame?.Invoke();
            return;
        }
        if (await RouteAwayFromCurrentActorAsync(state, frame, cancellationToken)
                .ConfigureAwait(false))
        {
            if (debugPump)
                Console.Error.WriteLine($"[dispatch] routed-away actor={frame.Actor.ActorId} name={frame.Header.Name}");
            acknowledgeHandledFrame?.Invoke();
            return;
        }

        var actor = endpoint.ResolveActor(state);
        if (debugPump && actor is null)
            Console.Error.WriteLine($"[dispatch] no-actor actor={frame.Actor.ActorId}");
        if (actor is null)
        {
            ZLinkActorBoundSessionRelay.TryReplyMissingNoBindActor(
                runtime,
                frame.Actor,
                frame.SourceNodeRid,
                frame.SourceSessionRid,
                frame.RequestId,
                frame.Flags,
                frame.Header);
            acknowledgeHandledFrame?.Invoke();
            return;
        }

        try
        {
            await DispatchCurrentActorAsync(
                    actor,
                    state,
                    frame,
                    acknowledgeHandledFrame,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException exception)
            when (allowCapture
                  && exception.Kind == ZLinkFrameworkErrorKind.ActorRouteNotFound
                  && state.Handoff.TryCapture(frame) && ZLinkActorCaptureDebug.PipelineCapture(frame))
        {
            // Capture may begin after frame decoding but before the actor
            // mailbox admits the handler. The handoff transition shares that
            // mailbox, so a rejected live dispatch is reclassified exactly
            // once as backlog instead of being dropped.
        }
    }

    private async ValueTask<bool> RouteAwayFromCurrentActorAsync(
        ZLinkActorRuntimeState state,
        ZLinkSpotActorFrame frame,
        CancellationToken cancellationToken)
    {
        try
        {
            if (!ZLinkActorSessionForwarder.TryForward(
                    runtime,
                    state,
                    frame.Actor,
                    frame.SourceNodeRid,
                    frame.SourceSessionRid,
                    frame.RequestId,
                    frame.Flags,
                    frame.Header,
                    frame.Body))
                return false;
        }
        catch (ZLinkFrameworkException exception)
            when (exception.Kind == ZLinkFrameworkErrorKind.ActorLocationStale)
        {
            await ZLinkActorBoundSessionRelay.ReplyStaleActorAsync(
                    runtime,
                    frame.Actor,
                    frame.SourceNodeRid,
                    frame.SourceSessionRid,
                    frame.RequestId,
                    frame.Flags,
                    frame.Header,
                    exception,
                    cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        return true;
    }

    private async ValueTask DispatchCurrentActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZLinkSpotActorFrame frame,
        Action? acknowledgeHandledFrame,
        CancellationToken cancellationToken)
    {
        if (ZLinkActorBoundSessionRelay.IsSessionDisconnectedPacket(frame.Header))
        {
            ZLinkActorBoundSessionRelay.RemoveNativeBinding(
                runtime,
                actor.ActorId,
                frame.SourceSessionRid);
            await endpoint.NotifyDisconnectedAsync(actor.ActorId, cancellationToken)
                .ConfigureAwait(false);
            acknowledgeHandledFrame?.Invoke();
            return;
        }

        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine(
                $"[dispatch] enter actor={actor.ActorId} name={frame.Header.Name} req={frame.RequestId} flags={frame.Flags}");
        var boundSession = ZLinkActorBoundSessionRelay.EnterDispatch(
            runtime,
            actor.ActorId,
            frame.SourceNodeRid,
            frame.SourceSessionRid,
            frame.RequestId,
            frame.Flags);

        try
        {
            if (await ZLinkRemoteSessionBindingHandler.TryHandleAsync(
                    runtime,
                    actor,
                    state,
                    frame,
                    boundSession,
                    acknowledgeHandledFrame,
                    cancellationToken).ConfigureAwait(false))
            {
                if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                    Console.Error.WriteLine(
                        $"[dispatch] session-bind handled actor={frame.Actor.ActorId} nobind={boundSession.IsNoBind}");
                return;
            }

            if (frame.Header.Kind == ZlinkStreamMessageKind.Request
                && frame.Header.RequestSeq is not null)
            {
                var reply = await endpoint.DispatchForReplyAsync(
                        actor,
                        state,
                        frame.Header,
                        frame.Body,
                        cancellationToken)
                    .ConfigureAwait(false);
                acknowledgeHandledFrame?.Invoke();
                if (reply is not null)
                {
                    if (acknowledgeHandledFrame is null)
                        await ZLinkActorBoundSessionRelay.SendReplyAsync(
                                runtime,
                                actor.ActorId,
                                frame.ReplyActor,
                                frame.SourceNodeRid,
                                frame.SourceSessionRid,
                                frame.RequestId,
                                frame.Flags,
                                boundSession.IsNoBind,
                                frame.Header,
                                reply,
                                cancellationToken)
                            .ConfigureAwait(false);
                    else
                        await ReconcileReplayFinalizationAsync(
                                "actor handoff reply",
                                ct => ZLinkActorBoundSessionRelay.SendReplyAsync(
                                    runtime,
                                    actor.ActorId,
                                    frame.ReplyActor,
                                    frame.SourceNodeRid,
                                    frame.SourceSessionRid,
                                    frame.RequestId,
                                    frame.Flags,
                                    boundSession.IsNoBind,
                                    frame.Header,
                                    reply,
                                    ct))
                            .ConfigureAwait(false);
                }

                if (acknowledgeHandledFrame is null)
                    await boundSession.DrainAsync(cancellationToken).ConfigureAwait(false);
                else
                    await ReconcileReplayFinalizationAsync(
                            "actor handoff deferred operation",
                            boundSession.DrainAsync)
                        .ConfigureAwait(false);
                return;
            }

            await endpoint.DispatchAsync(
                    actor,
                    state,
                    frame.Header,
                frame.Body,
                cancellationToken)
                .ConfigureAwait(false);
            acknowledgeHandledFrame?.Invoke();
            if (acknowledgeHandledFrame is not null)
                await ReconcileReplayFinalizationAsync(
                        "actor handoff deferred operation",
                        boundSession.DrainAsync)
                    .ConfigureAwait(false);
        }
        finally
        {
            if (acknowledgeHandledFrame is null)
                await boundSession.DisposeAsync().ConfigureAwait(false);
            else
                await ReconcileReplayFinalizationAsync(
                        "actor handoff dispatch scope disposal",
                        _ => boundSession.DisposeAsync())
                    .ConfigureAwait(false);
        }
    }

    private async ValueTask ReconcileReplayFinalizationAsync(
        string operation,
        Func<CancellationToken, ValueTask> finalize)
    {
        await ZLinkReconciliationRunner.RunAsync(
                finalize,
                exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"{operation} retry: {exception.Message}"),
                runtime.ShutdownToken)
            .ConfigureAwait(false);
    }
}

internal sealed class ZLinkEntrySpotActorInboundEndpoint(
    ZLinkFrameworkRuntime runtime) : IZLinkActorInboundEndpoint
{
    public IZLinkActor? ResolveActor(ZLinkActorRuntimeState state) => state.Actor;

    public async ValueTask NotifyDisconnectedAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        if (!await runtime.TryNotifyJoinedSpotActorDisconnectedAsync(actorId, cancellationToken)
                .ConfigureAwait(false))
            await runtime.NotifyActorDisconnectedByIdAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
    }

    public ValueTask DispatchAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return runtime.SubmitActorAsync(actor, header, body, cancellationToken);
    }

    public async ValueTask<ZLinkActorReply?> DispatchForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (state.LiveActivation is not null)
            return await runtime.SubmitActorForReplyAsync(
                    actor.ActorId,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);

        var result = await runtime.TrySubmitEntrySpotActorForReplyAsync(
                actor,
                state,
                header,
                body,
                callerOwnsDispatchTurn: false,
                cancellationToken)
            .ConfigureAwait(false);
        return result.Handled
            ? result.Reply
            : await runtime.SubmitActorForReplyAsync(
                    actor.ActorId,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
    }
}

internal sealed class ZLinkUserSpotActorInboundEndpoint(
    ZLinkFrameworkRuntime runtime,
    ZLinkSpotActorMembership actors,
    ZLinkSpotActorPacketDispatcher dispatcher) : IZLinkActorInboundEndpoint
{
    public IZLinkActor? ResolveActor(ZLinkActorRuntimeState state)
    {
        return actors.TryGetActor(state.ActorId, out var actor) && actor is not null
            ? actor
            : state.Actor;
    }

    public ValueTask NotifyDisconnectedAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        return runtime.NotifyActorDisconnectedByIdAsync(actorId, cancellationToken);
    }

    public ValueTask DispatchAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return dispatcher.DispatchAsync(actor, state, header, body, cancellationToken);
    }

    public ValueTask<ZLinkActorReply?> DispatchForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState state,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return dispatcher.DispatchForReplyAsync(actor, state, header, body, cancellationToken);
    }
}
