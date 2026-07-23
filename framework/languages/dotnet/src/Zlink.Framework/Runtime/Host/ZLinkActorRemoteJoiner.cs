namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkActorRemoteJoiner(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    IServiceProvider services,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager)
{
    public async ValueTask<ZLinkActorJoinResult> JoinAsync(
        ZLinkFrameworkComponentState state,
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        var activation = spots.GetActivationBySpotRid(state, spotRid);
        if (activation is not null)
        {
            if (!activation.TryResolveActorJoinDescriptor(out var descriptor) || descriptor is null)
                throw new InvalidOperationException(
                    $"SPOT '{activation.SpotRid}' does not declare an actor join callback.");

            return await SubmitNativeJoinActorAsync(
                    actor,
                    actorRef,
                    node,
                    activation.NodeRid,
                    activation.SpotRid,
                    activation.ChannelName,
                    request,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var remoteAddress = await ResolveRemoteActorJoinTargetAsync(
                actor.Context.MeshName,
                spotRid,
                cancellationToken)
            .ConfigureAwait(false);
        return await SubmitRoutedJoinActorAsync(
                actor,
                actorRef,
                actorSessionManager.GetOrCreateState(actor.ActorId),
                remoteAddress,
                request,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorJoinResult> SubmitRoutedJoinActorAsync(
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        ZLinkActorRuntimeState actorState,
        ZLinkResolvedSpotHandle target,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorState.ActorType))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actor.ActorId}' does not have an actor type for remote SPOT join.");

        var actorType = actorState.ActorType;
        var handoffId = Guid.NewGuid().ToString("N");
        ZLinkActorTransferRegistry.TryResolve(registration, actorType, out var transfer);
        if (transfer is null)
            return await SubmitRoutedJoinActorCoreAsync(
                    actor,
                    actorRef,
                    actorState,
                    target,
                    request,
                    actorType,
                    handoffId,
                    null,
                    cancellationToken)
                .ConfigureAwait(false);

        var timeout = registration.ActorTransferTimeout
                      ?? throw new ZLinkConfigurationException(
                          "ActorTransferTimeout is required for remote actor transfer.");
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);
        try
        {
            return await SubmitRoutedJoinActorCoreAsync(
                    actor,
                    actorRef,
                    actorState,
                    target,
                    request,
                    actorType,
                    handoffId,
                    transfer,
                    timeoutSource.Token)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (
            !cancellationToken.IsCancellationRequested
            && timeoutSource.IsCancellationRequested)
        {
            throw new TimeoutException($"Actor transfer timed out after {timeout}.");
        }
    }

    private async ValueTask<ZLinkActorJoinResult> SubmitRoutedJoinActorCoreAsync(
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        ZLinkActorRuntimeState actorState,
        ZLinkResolvedSpotHandle target,
        ZLinkMessage request,
        string actorType,
        string handoffId,
        ZLinkActorTransferRegistration? transfer,
        CancellationToken cancellationToken)
    {
        var targetAccepted = false;
        var sourceActivation = actorState.LiveActivation;
        var sourceLeft = false;
        var sourceCaptureStarted = false;
        try
        {
            var result = await SubmitRoutedJoinActorTransactionAsync(
                    actor,
                    actorRef,
                    actorState,
                    target,
                    request,
                    actorType,
                    handoffId,
                    transfer,
                    accepted => targetAccepted = accepted,
                    () => sourceCaptureStarted = true,
                    () => sourceLeft = true,
                    cancellationToken)
                .ConfigureAwait(false);
            if (result is not ZLinkActorJoinResult.Accepted)
            {
                await RollbackSourceHandoffAsync(
                        actor,
                        actorState,
                        sourceActivation,
                        sourceLeft,
                        sourceCaptureStarted)
                    .ConfigureAwait(false);
            }

            return result;
        }
        catch (Exception transactionFailure)
        {
            if (!targetAccepted)
            {
                try
                {
                    await RollbackSourceHandoffAsync(
                            actor,
                            actorState,
                            sourceActivation,
                            sourceLeft,
                            sourceCaptureStarted)
                        .ConfigureAwait(false);
                }
                catch (Exception rollbackFailure)
                {
                    throw new AggregateException(transactionFailure, rollbackFailure);
                }
            }
            throw;
        }
    }

    private async ValueTask RollbackSourceHandoffAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkSpotActivation? sourceActivation,
        bool sourceLeft,
        bool sourceCaptureStarted)
    {
        List<Exception>? failures = null;
        if (sourceLeft && sourceActivation is not null)
        {
            try
            {
                await sourceActivation.RestoreActorAfterFailedHandoffAsync(
                        actor,
                        CancellationToken.None)
                    .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }

        if (sourceCaptureStarted)
        {
            try
            {
                await ReplayAbortedSourceHandoffAsync(actorState).ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }

        if (failures is { Count: > 0 })
            throw new AggregateException("Actor handoff source rollback failed.", failures);
    }

    private async ValueTask<ZLinkActorJoinResult> SubmitRoutedJoinActorTransactionAsync(
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        ZLinkActorRuntimeState actorState,
        ZLinkResolvedSpotHandle target,
        ZLinkMessage request,
        string actorType,
        string handoffId,
        ZLinkActorTransferRegistration? transfer,
        Action<bool> setTargetAccepted,
        Action markSourceCaptureStarted,
        Action markSourceLeft,
        CancellationToken cancellationToken)
    {
        var sourceSpotRid = ResolveSourceSpotRid(actorState);

        var admissionDeadline = DateTimeOffset.UtcNow + registration.DefaultRequestTimeout;
        var admission = await ZLinkSpotHandleRequestExecution.ExecuteAsync(
                target,
                async snapshot =>
                {
                    var admissionHeader = ZLinkClientCallCodec.CreateEnvelope(
                        ZLinkMessageKind.Request,
                        snapshot.RouterChannelId,
                        ZLinkRemoteActorJoinPackets.AdmissionPacketName,
                        registration.DefaultRequestTimeout);
                    var admissionParts = ZLinkRemoteActorJoinPackets.EncodeAdmissionRequest(
                        admissionHeader,
                        actor.ActorId,
                        actorType,
                        handoffId,
                        admissionDeadline,
                        sourceSpotRid,
                        actorRef.NodeRid,
                        request,
                        registration.Codecs);
                    var replyParts = await runtime.RequestToSpotViaRouterChannelAsync(
                            snapshot.RouterChannelId,
                            snapshot.NodeRid,
                            snapshot.SpotRid,
                            (ulong)snapshot.Generation,
                            snapshot.AuthorityOwnerGeneration,
                            admissionParts,
                            registration.DefaultRequestTimeout,
                            cancellationToken)
                        .ConfigureAwait(false);
                    var reply = ZLinkRemoteActorJoinPackets.DecodeAdmissionReplyAndDispose(
                        replyParts,
                        actor.ActorId,
                        snapshot.SpotRid);
                    return (Snapshot: snapshot, Reply: reply);
                },
                cancellationToken)
            .ConfigureAwait(false);
        var targetNodeRid = admission.Snapshot.NodeRid;
        var targetSpotRid = admission.Snapshot.SpotRid;
        var routerChannelId = admission.Snapshot.RouterChannelId;
        var admissionReply = admission.Reply;
        var admissionReplyMessage = ZLinkRemoteActorJoinPackets.DecodeAdmissionReplyPayload(
            admissionReply,
            registration.Codecs);
        if (!admissionReply.Accepted)
        {
            return new ZLinkActorJoinResult.Rejected(admissionReplyMessage);
        }

        var pendingRequests = await actorState.BeginHandoffCaptureAsync(cancellationToken)
            .ConfigureAwait(false);
        var transferMetricStarted = ZLinkRuntimeMetrics.StartActorTransfer(pendingRequests);
        markSourceCaptureStarted();

        var transferState = await CaptureTransferStateAsync(
                services,
                transfer,
                actor,
                cancellationToken)
            .ConfigureAwait(false);

        markSourceLeft();
        await NotifySourceActorLeftAsync(actor, actorState, cancellationToken)
            .ConfigureAwait(false);

        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            ZLinkRemoteActorJoinPackets.CommitPacketName,
            registration.DefaultRequestTimeout);
        var hasBoundSession = actorState.TryGetBoundSession(out var boundSession);
        // A locally bound session records no SessionNodeRid (null means "this
        // node"); the join commit crosses nodes, so the target must receive
        // the concrete session node rid — the actor's current owner node — or
        // its pushes can never route back to the session.
        if (hasBoundSession && boundSession.SessionNodeRid is null)
            boundSession = boundSession with { SessionNodeRid = actorRef.NodeRid };
        var committedFrames = actorState.Handoff.SnapshotFrames();
        var reply = await ReconcileTargetJoinCommitAsync(
                actor.ActorId,
                handoffId,
                targetNodeRid,
                targetSpotRid,
                (ulong)admission.Snapshot.Generation,
                admission.Snapshot.AuthorityOwnerGeneration,
                routerChannelId,
                () => ZLinkRemoteActorJoinPackets.EncodeJoinRequest(
                    header,
                    actor.ActorId,
                    actorType,
                    handoffId,
                    sourceSpotRid,
                    actorRef.NodeRid,
                    boundSession.SessionNodeRid,
                    boundSession.SessionRid,
                    transferState,
                    request,
                    committedFrames,
                    registration.Codecs))
            .ConfigureAwait(false);
        if (!reply.Accepted)
            return new ZLinkActorJoinResult.Rejected(admissionReplyMessage);

        var resultActorRef = ZLinkRemoteActorJoinPackets.ToActorRef(reply);
        var trailingFrames = actorState.Handoff.CutoverCaptureToForwarding(
            committedFrames.Count,
            actorRef,
            resultActorRef);
        await ReconcileTargetHandoffCompletionAsync(
                    actor.ActorId,
                    handoffId,
                    trailingFrames,
                    sourceSpotRid,
                    actorRef.NodeRid,
                    targetNodeRid,
                    targetSpotRid,
                    (ulong)admission.Snapshot.Generation,
                    admission.Snapshot.AuthorityOwnerGeneration,
                    routerChannelId,
                CancellationToken.None)
                .ConfigureAwait(false);
        ZLinkRuntimeMetrics.CompleteActorTransfer(transferMetricStarted);
        setTargetAccepted(true);
        actorState.Handoff.CommitForwardingCutover(
            registration.ActorTransferForwardWindow ?? TimeSpan.Zero);
        runtime.RunDetached(
            "actor-source-handoff-cleanup",
            ct => ReconcileCommittedSourceHandoffAsync(
                actorState,
                actorRef,
                resultActorRef,
                ct));
        return new ZLinkActorJoinResult.Accepted(
            resultActorRef.ToNative(),
            admissionReplyMessage);
    }

    internal static ValueTask<ZLinkMessage> CaptureTransferStateAsync(
        IServiceProvider services,
        ZLinkActorTransferRegistration? transfer,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return transfer is null
            ? ValueTask.FromResult(ZLinkMessage.Empty)
            : ZLinkActorTransferRegistry.TransferOutAsync(
                services,
                transfer,
                actor,
                cancellationToken);
    }

    private async ValueTask ReconcileCommittedSourceHandoffAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        if (ZLinkBoundSessionDispatchScope.TryDefer(
                actorState.ActorId,
                ct => ReconcileCommittedSourceHandoffCoreAsync(
                    actorState,
                    sourceActorRef,
                    targetActorRef,
                    ct)))
            return;

        await ReconcileCommittedSourceHandoffCoreAsync(
                actorState,
                sourceActorRef,
                targetActorRef,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ReconcileCommittedSourceHandoffCoreAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        var migrationApplied = false;
        await ZLinkReconciliationRunner.RunAsync(
                async token =>
                {
                    if (!migrationApplied)
                    {
                        await ApplyRemoteActorMigrationCoreAsync(
                                actorState,
                                targetActorRef,
                                token)
                            .ConfigureAwait(false);
                        migrationApplied = true;
                    }

                    await actorSessionManager.FinalizeMigratedSourceAsync(actorState, sourceActorRef)
                        .ConfigureAwait(false);
                },
                exception => ReportCommittedHandoffFailure(
                    "actor-source-handoff-cleanup",
                    exception),
                cancellationToken,
                static exception => exception is OperationCanceledException)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkRemoteActorJoinReply> ReconcileTargetJoinCommitAsync(
        string actorId,
        string handoffId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        string routerChannelId,
        Func<IReadOnlyList<Message>> createParts)
    {
        return await ZLinkReconciliationRunner.RunAsync(
                async token =>
                {
                    var replyParts = await runtime.RequestToSpotViaRouterChannelAsync(
                            routerChannelId,
                            targetNodeRid,
                            targetSpotRid,
                            targetSpotGeneration,
                            authorityOwnerGeneration,
                            createParts(),
                            registration.DefaultRequestTimeout,
                            token)
                        .ConfigureAwait(false);
                    return ZLinkRemoteActorJoinPackets.DecodeJoinReplyAndDispose(
                        replyParts,
                        actorId,
                        targetSpotRid);
                },
                exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"handoff commit retry actor={actorId} id={handoffId}: {exception.Message}"),
                runtime.ShutdownToken,
                static exception => exception is ZLinkActorHandoffRejectedException)
            .ConfigureAwait(false);
    }

    private async ValueTask ReconcileTargetHandoffCompletionAsync(
        string actorId,
        string handoffId,
        IReadOnlyList<ZLinkActorHandoffFrame> frames,
        RoutingId sourceSpotRid,
        RoutingId sourceNodeRid,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        string routerChannelId,
        CancellationToken cancellationToken)
    {
        await ZLinkReconciliationRunner.RunAsync(
                _ =>
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    return CompleteTargetHandoffAsync(
                        actorId,
                        handoffId,
                        frames,
                        sourceSpotRid,
                        sourceNodeRid,
                        targetNodeRid,
                        targetSpotRid,
                        targetSpotGeneration,
                        authorityOwnerGeneration,
                        routerChannelId,
                        runtime.ShutdownToken);
                },
                exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"handoff completion retry actor={actorId} id={handoffId}: {exception.Message}"),
                runtime.ShutdownToken,
                // Terminal: an explicit rejection (the target's joined
                // callback refused the handoff) or a target that no longer
                // hosts the actor (it already rolled the transfer back) —
                // retrying either would spin for the whole request window.
                exception => exception is ZLinkActorHandoffRejectedException
                             || exception is ZLinkFrameworkException
                             {
                                 Kind: ZLinkFrameworkErrorKind.RequestRejected
                                     or ZLinkFrameworkErrorKind.ActorRouteNotFound
                             }
                             || (exception is OperationCanceledException
                                 && cancellationToken.IsCancellationRequested))
            .ConfigureAwait(false);
    }

    private async ValueTask ReplayAbortedSourceHandoffAsync(ZLinkActorRuntimeState actorState)
    {
        var frames = actorState.Handoff.AbortCapture();
        if (frames.Count == 0) return;

        if (actorState.LiveActivation is { } activation)
        {
            await activation.ReplayAbortedActorHandoffAsync(
                    actorState,
                    frames,
                    CancellationToken.None)
                .ConfigureAwait(false);
            return;
        }

        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref during handoff rollback.");
        var pipeline = new ZLinkActorInboundPipeline(
            runtime,
            new ZLinkEntrySpotActorInboundEndpoint(runtime));
        await pipeline.DispatchAsync(
                ZLinkActorHandoffFrames.Restore(actorRef, frames),
                CancellationToken.None)
            .ConfigureAwait(false);
    }

    private async ValueTask CompleteTargetHandoffAsync(
        string actorId,
        string handoffId,
        IReadOnlyList<ZLinkActorHandoffFrame> frames,
        RoutingId sourceSpotRid,
        RoutingId sourceNodeRid,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        string routerChannelId,
        CancellationToken cancellationToken)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            ZLinkRemoteActorJoinPackets.HandoffCompletionPacketName,
            registration.DefaultRequestTimeout);
        var parts = ZLinkRemoteActorJoinPackets.EncodeHandoffCompletionRequest(
            header,
            actorId,
            handoffId,
            sourceSpotRid,
            sourceNodeRid,
            targetSpotRid,
            frames);
        var replyParts = await runtime.RequestToSpotViaRouterChannelAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                targetSpotGeneration,
                authorityOwnerGeneration,
                parts,
                registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);
        _ = ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<ZLinkRemoteActorHandoffCompletionRequest>(
            replyParts,
            "Remote actor handoff completion reply was empty.",
            $"Remote actor handoff completion failed for '{actorId}'.",
            null);
    }

    private void ReportCommittedHandoffFailure(string operation, Exception exception)
    {
        ZLinkFrameworkDebugLog.TaskFailure(operation, exception);
        runtime.ErrorSink.ReportUnhandledCallbackException(exception);
    }

    private RoutingId ResolveSourceSpotRid(ZLinkActorRuntimeState actorState)
    {
        if (actorState.LiveActivation is { } activation) return activation.SpotRid;

        foreach (var spotNode in registration.SpotNodes.Values)
            if (spotNode.EntrySpotOptions.RoutingId.Size > 0)
                return spotNode.EntrySpotOptions.RoutingId;

        return actorState.NativeActorRef?.NodeRid ?? default;
    }

    private async ValueTask ApplyRemoteActorMigrationCoreAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        actorState.BindNativeActorRef(targetActorRef);
        await RebindRemoteSessionActorAsync(actorState, targetActorRef, cancellationToken)
            .ConfigureAwait(false);
        actorState.InvalidateContext();
        await ReconcileActorLocationAfterMoveAsync(actorState, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask NotifySourceActorLeftAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        if (actorState.LiveActivation is { } previousActivation)
            await previousActivation.NotifyActorLeftAfterManagedJoinSpotAsync(actor, cancellationToken)
                .ConfigureAwait(false);
        else
            await runtime.NotifyEntrySpotActorLeftAsync(actor, cancellationToken: cancellationToken)
                .ConfigureAwait(false);
    }

    private async ValueTask ReconcileActorLocationAfterMoveAsync(
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        await ZLinkReconciliationRunner.RunAsync(
                token => actorSessionManager.ReleaseActorLocationAfterMoveAsync(actorState, token),
                exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"remote actor move cleanup retry for '{actorState.ActorId}': {exception.Message}"),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkResolvedSpotHandle> ResolveRemoteActorJoinTargetAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var resolver = services.GetService(typeof(IZLinkSpotHandleResolver))
            as IZLinkSpotHandleResolver;
        if (resolver is null) throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        var handle = await resolver.ResolveSpotHandleAsync(meshName, spotRid, cancellationToken)
            .ConfigureAwait(false) as ZLinkResolvedSpotHandle;
        if (handle is null)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotRouteNotFound,
                $"SPOT '{spotRid}' has no live location row.");
        var snapshot = handle.Snapshot;
        if (services.GetService(typeof(IZLinkMeshNodeLocationResolver))
                is IZLinkMeshNodeLocationResolver peerResolver)
        {
            var peers = await peerResolver.ListLiveMeshNodesAsync(
                    snapshot.RouterChannelId, cancellationToken)
                .ConfigureAwait(false);
            if (peers.Any(descriptor =>
                    descriptor.Rid.Equals(snapshot.NodeRid)
                    && descriptor.State == ZLinkFrameworkRuntimeState.Draining))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestRejected,
                    $"SPOT '{spotRid}' is hosted by a draining node.",
                    false);
        }
        return handle;
    }

    private async ValueTask<ZLinkActorJoinResult> SubmitNativeJoinActorAsync(
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        string channelName,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var encodedRequest = request.Encode(registration.Codecs);
        var joinHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            channelName,
            typeof(ZLinkMessage).Name,
            encodedRequest.ContentType,
            null, null, null, null, null);
        IReadOnlyList<Message> joinParts;
        joinParts = ZLinkMessageParts.Create(
            ZLinkEnvelopeCodec.EncodeHeader(joinHeader),
            Message.From(encodedRequest.Payload.Bytes.Span));

        using var completion = new ZLinkNativeReplyCompletion<ZLinkBackendActorJoinResult>(cancellationToken);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.ActorRequest,
                "JoinSpot",
                channelName,
                SourceRid: targetNodeRid.ToString(),
                SpotRid: targetSpotRid.ToString(),
                ActorId: actor.ActorId));

        bool submitted;
        try
        {
            submitted = node.JoinActor(
                actorRef,
                targetNodeRid,
                targetSpotRid,
                joinParts,
                completion.Complete,
                registration.DefaultRequestTimeout);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(joinParts);
        }

        if (!submitted)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor join submit failed for '{actor.ActorId}' to SPOT '{targetSpotRid}'.");

        var (joinResult, replyParts) = await completion.Task.ConfigureAwait(false);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived))
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.ReplyReceived,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.Response,
                "JoinSpot",
                channelName,
                SourceRid: targetNodeRid.ToString(),
                SpotRid: targetSpotRid.ToString(),
                ActorId: actor.ActorId));
        var reply = DecodeNativeJoinReply(
            joinResult.Result,
            replyParts,
            actor.ActorId,
            targetSpotRid);
        var accepted = joinResult.JoinResultCode == 0;
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        if (accepted)
        {
            actorState.BindNativeActorRef(joinResult.Actor);
            if (joinResult.Actor.NodeRid != actorRef.NodeRid) actorState.InvalidateContext();
        }

        return accepted
            ? new ZLinkActorJoinResult.Accepted(joinResult.Actor.ToNative(), reply)
            : new ZLinkActorJoinResult.Rejected(reply);
    }

    private async ValueTask RebindRemoteSessionActorAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        if (!actorState.TryGetBoundSession(out var session)) return;

        if (!runtime.TryGetSessionActorContext(
                actorState.ActorId,
                session.BindingToken,
                out var context)
            && !runtime.TryGetSessionActorContext(actorState.ActorId, out context))
            return;

        await context.ActorCoordinator.BindActorAsync(
                context,
                targetActorRef.ToNative(),
                cancellationToken)
            .ConfigureAwait(false);
        runtime.UnbindSessionActor(actorState.ActorId, context, session.BindingToken);
        runtime.UnbindActorSession(actorState.ActorId, session.BindingToken);
    }

    private ZLinkMessage DecodeNativeJoinReply(
        RequestResult result,
        IReadOnlyList<Message> replyParts,
        string actorId,
        RoutingId spotRid)
    {
        try
        {
            if (result != RequestResult.Ok)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor join was rejected for '{actorId}' to SPOT '{spotRid}'.");

            if (replyParts.Count == 0)
                throw new InvalidOperationException(
                    "Actor join reply was empty.");

            var header = ZLinkEnvelopeCodec.DecodeHeader(replyParts);
            var reply = (Message)ZLinkEnvelopeCodec.DecodeBody(replyParts, typeof(Message))!;
            using var ownedReply = Message.From(reply);
            return ZLinkMessage.FromEnvelopePayload(header.ContentType, ownedReply, registration.Codecs);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}
