namespace Zlink.Framework.Runtime.Spots;

internal sealed record ZLinkSpotRelocationSeal(
    ZLinkSpotExecutionRelocationSeal QueueSeal,
    IReadOnlyList<ZLinkRelocationLogicalTimer> LogicalTimers);

internal sealed record ZLinkSpotRelocationApplicationState(
    ReadOnlyMemory<byte> SpotState,
    IReadOnlyDictionary<string, ReadOnlyMemory<byte>> ActorStates);

internal sealed partial class ZLinkSpotActivation
{
    private ZLinkSpotMessageFollow? _messageFollow;

    internal object RuntimeExecutionOwner => _runtime.ExecutionOwner;

    public CancellationToken StopToken => _stopSource.Token;

    internal void CancelActiveOperations()
    {
        _stopSource.Cancel();
    }

    internal void RequestStop()
    {
        _serial.RequestStop();
        _stopSource.Cancel();
    }

    public ValueTask DisposeAsync()
    {
        TaskCompletionSource completion;
        lock (_lifecycleGate)
        {
            if (_finalization is not null) return new ValueTask(_finalization);

            Volatile.Write(ref _disposed, 1);
            completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _finalization = completion.Task;
        }

        _ = CompleteFinalizationAsync(completion);
        return new ValueTask(completion.Task);
    }

    private async Task CompleteFinalizationAsync(TaskCompletionSource completion)
    {
        try
        {
            await FinalizeAsync().ConfigureAwait(false);
            completion.TrySetResult();
        }
        catch (Exception exception)
        {
            completion.TrySetException(exception);
        }
    }

    private async Task FinalizeAsync()
    {
        var failures = new List<Exception>();
        Capture(RequestStop);
        await CaptureAsync(_timers.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_serial.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_outbound.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(NativeSpot.DisposeAsync).ConfigureAwait(false);
        Capture(_stopSource.Dispose);
        await CaptureAsync(_handlerInstances.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_scope.DisposeAsync).ConfigureAwait(false);
        ThrowFailures(failures);

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        void Capture(Action cleanup)
        {
            try
            {
                cleanup();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    private static void ThrowFailures(IReadOnlyList<Exception> failures)
    {
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
    }

    public ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class
    {
        EnsureContextOperationAllowed();
        return _timers.AddAsync(
            name,
            period,
            options,
            typeof(THandler),
            Spot.GetType(),
            StopToken,
            DispatchTimerAsync,
            PublishTimerFailureAsync,
            cancellationToken);
    }

    public IZLinkWorkerCall<TResult> RunCpuWorker<TResult>(
        Func<CancellationToken, TResult> work)
    {
        EnsureContextOperationAllowed();
        ArgumentNullException.ThrowIfNull(work);
        return new ZLinkWorkerCall<TResult>(
            _runtime.WorkerPool,
            work,
            _runtime.ErrorSink);
    }

    public IZLinkWorkerCall<TResult> RunIoWorker<TResult>(
        Func<CancellationToken, ValueTask<TResult>> work)
    {
        EnsureContextOperationAllowed();
        ArgumentNullException.ThrowIfNull(work);
        return new ZLinkIoWorkerCall<TResult>(
            _runtime.WorkerPool.ShutdownToken,
            work,
            _runtime.ErrorSink);
    }

    ValueTask<bool> IZLinkSpotContext.CloseAsync(CancellationToken cancellationToken)
    {
        EnsureContextOperationAllowed();
        return _runtime.CloseCurrentSpotAsync(SpotId, cancellationToken);
    }

    ValueTask<bool> IZLinkInstanceSpotContext.CloseAsync(CancellationToken cancellationToken)
    {
        EnsureContextOperationAllowed();
        return _runtime.CloseCurrentSpotAsync(SpotId, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResponse> InitializeAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        RegisterWithoutSynchronizationContext(() =>
        {
            ZLinkSpotNativeDispatchRouter.Attach(
                NativeSpot,
                receivedMessages =>
                {
                    if (receivedMessages.Count == 0)
                    {
                        QueueSerialized(static (activation, ct) => activation._dispatcher.DispatchRouteDrainAsync(ct));
                        return;
                    }

                    foreach (var received in receivedMessages)
                    {
                        if (ZLinkSpotActivationDispatcher.IsInfrastructureRoute(received))
                        {
                            QueueSerialized(
                                static (activation, state, ct) => activation._dispatcher.DispatchRouteAsync(state, ct),
                                received,
                                received.Dispose);
                            continue;
                        }

                        switch (TryMessageFollow(received))
                        {
                            case ZLinkSpotMessageFollowResult.Followed:
                                continue;
                            case ZLinkSpotMessageFollowResult.StaleRejected:
                                ZLinkSpotActivationDispatcher
                                    .RejectApplicationRouteForStaleMessageFollow(
                                        received,
                                        ChannelName);
                                continue;
                            case ZLinkSpotMessageFollowResult.Full:
                                ZLinkSpotActivationDispatcher
                                    .RejectApplicationRouteForRelocation(
                                        received,
                                        ChannelName);
                                continue;
                            case ZLinkSpotMessageFollowResult.NotApplicable:
                                QueueApplicationRouteSerialized(received);
                                break;
                            default:
                                throw new InvalidOperationException(
                                    "Unknown Spot Message Follow result.");
                        }
                    }
                },
                drain => drain?.Invoke(),
                () => QueueApplicationSerialized(
                    static (activation, ct) => activation.DispatchSubscriptionsAsync(ct),
                    countAsRequest: false,
                    () => QueueSerialized(
                        static (activation, ct) => activation._dispatcher.DiscardSubscriptionsAsync(ct))),
                () => QueueSerialized(static (activation, ct) =>
                    activation._dispatcher.DispatchActorJoinDrainAsync(ct)),
                () => QueueSerialized(static (activation, ct) =>
                    activation.DispatchActorLifecycleDrainAsync(ct)),
                actorParts =>
                {
                    var dispatchable = ZLinkActorHandoffIngress.CaptureMovingFrames(_runtime, actorParts);
                    if (dispatchable.Count == 0)
                    {
                        dispatchable.Dispose();
                        return;
                    }

                    if (!QueueActorFrames(dispatchable))
                        dispatchable.Dispose();
                });

            return 0;
        });

        var create = new SpotCreateCallState(request);
        await ExecuteSerializedAsync(
            static async (activation, state, ct) =>
            {
                state.Response = await activation.UserSpot.OnCreateAsync(state.Request, ct);
                if (!state.Response.Accepted) return;

                await activation.UserSpot.OnInitializeAsync(ct);
            },
            create,
            cancellationToken);
        return create.Response;
    }

    internal ValueTask InitializeInstanceAsync(CancellationToken cancellationToken)
    {
        if (Spot is not IZLinkInstanceSpot instance)
            throw new InvalidOperationException("The current activation is not an Instance Spot.");
        return ExecuteSerializedAsync(
            static (activation, state, ct) => state.OnInitializeAsync(ct),
            instance,
            cancellationToken);
    }

    internal async ValueTask<InstanceSpotActivationTerminal>
        DispatchDurableActivationAsync(
            MeshOperationId operationId,
            RoutingId sourceNodeRid,
            string sourceSpotId,
            ZLinkServiceWireCodec.RequestSourceFence requestSource,
            ulong targetNodeGeneration,
            ulong authorityOwnerGeneration,
            ulong ownerLeaseGeneration,
            IReadOnlyList<ReadOnlyMemory<byte>> payload,
            ReadOnlyMemory<byte>? metadata,
            bool request,
            CancellationToken cancellationToken)
    {
        if (operationId == default
            || targetNodeGeneration == 0
            || authorityOwnerGeneration == 0
            || ownerLeaseGeneration == 0)
            throw new ArgumentOutOfRangeException(
                nameof(operationId),
                "Durable Instance Spot dispatch requires an exact operation and authority fence.");
        if (requestSource.NodeRid != sourceNodeRid
            || requestSource.NodeGeneration == 0
            || string.IsNullOrWhiteSpace(requestSource.OwnerId)
            || requestSource.LeaseGeneration == 0)
            throw new ArgumentException(
                "The durable Instance Spot request-source fence does not match its source node.",
                nameof(requestSource));
        var parts = payload.Select(Message.From).ToArray();
        var completion = new TaskCompletionSource<InstanceSpotActivationTerminal>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var metadataBytes = metadata.HasValue
            ? metadata.Value
            : ReadOnlyMemory<byte>.Empty;
        if (!ZLinkMeshMetadataCodec.TryDecode(
                metadataBytes.Span,
                out var decodedMetadata))
            throw new ArgumentException(
                "Application metadata is malformed.",
                nameof(metadata));
        Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? replyCallback = null;
        if (request)
            replyCallback = (reply, _) =>
            {
                completion.TrySetResult(new InstanceSpotActivationTerminal(
                    RequestResult.Ok,
                    Systems.Zlink.Framework.Runtime.Protocol.ServiceWireConstants
                        .FrameworkErrorCode.None,
                    reply.Select(static item =>
                            (ReadOnlyMemory<byte>)item.ToArray())
                        .ToArray()));
                return SubmitResult.Ok;
            };
        var received = new ZLinkBackendRouteReceived(
            parts,
            sourceNodeRid,
            sourceSpotId,
            request ? operationId.Low : null,
            replyCallback,
            metadata: decodedMetadata,
            operationId: operationId,
            targetNodeGeneration: targetNodeGeneration,
            authorityOwnerGeneration: authorityOwnerGeneration,
            ownerLeaseGeneration: ownerLeaseGeneration,
            sourceNodeGeneration: requestSource.NodeGeneration,
            requestSource: requestSource);
        var accepted = ZLinkSpotAcceptedJournal.CaptureOrDispose(
            received,
            request ? operationId.Low : 0);

        var queued = QueueApplicationSerialized(
            static async (activation, state, ct) =>
            {
                try
                {
                    await activation._dispatcher.DispatchRouteAsync(
                            state.Received,
                            ct)
                        .ConfigureAwait(false);
                    if (!state.Request)
                        state.Completion.TrySetResult(new InstanceSpotActivationTerminal(
                            RequestResult.Ok,
                            Systems.Zlink.Framework.Runtime.Protocol.ServiceWireConstants
                                .FrameworkErrorCode.None,
                            []));
                    else if (!state.Completion.Task.IsCompleted)
                        state.Completion.TrySetResult(new InstanceSpotActivationTerminal(
                            RequestResult.InternalError,
                            Systems.Zlink.Framework.Runtime.Protocol.ServiceWireConstants
                                .FrameworkErrorCode.RequestFailed,
                            []));
                }
                catch (Exception error)
                {
                    state.Completion.TrySetException(error);
                }
            },
            new DurableActivationDispatch(received, completion, request),
            accepted,
            request,
            () =>
            {
                received.Dispose();
                completion.TrySetException(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RuntimeShutdown,
                    "The Instance Spot activation queue stopped before admission."));
            },
            () =>
            {
                received.Dispose();
                completion.TrySetException(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    "The Instance Spot activation queue is relocating.",
                    true));
            },
            received.Dispose);
        if (!queued)
            return await completion.Task.ConfigureAwait(false);
        // Admission is durable at this point. Caller cancellation no longer
        // removes the accepted queue record or prevents terminal publication.
        return await completion.Task.ConfigureAwait(false);
    }

    public ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return _actorDispatchSubmitter.Async(actor, runtimeState, header, body, cancellationToken);
    }

    private sealed record DurableActivationDispatch(
        ZLinkBackendRouteReceived Received,
        TaskCompletionSource<InstanceSpotActivationTerminal> Completion,
        bool Request);

    public ValueTask<ZLinkActorReply> SubmitActorForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        return _actorDispatchSubmitter.SubmitForReplyAsync(
            actor,
            runtimeState,
            header,
            body,
            cancellationToken);
    }

    public async ValueTask CloseAsync(CancellationToken cancellationToken)
    {
        await CloseAsync(
                ZLinkSpotCloseReason.ExplicitClose,
                DateTimeOffset.UtcNow + DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask CloseAsync(
        ZLinkSpotCloseReason reason,
        DateTimeOffset deadline,
        CancellationToken cancellationToken)
    {
        if (Interlocked.Exchange(ref _closingInvoked, 1) != 0)
            return;
        _ = await _serial.ExecuteQuiescentLifecycleAsync(
                async (activation, ct) =>
                {
                    await activation.InvokeClosingAsync(reason, deadline)
                        .ConfigureAwait(false);
                    return true;
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ValueTask InvokeRelocationClosingAfterCommitAsync(
        DateTimeOffset deadline)
    {
        if (Interlocked.Exchange(ref _closingInvoked, 1) != 0)
            return ValueTask.CompletedTask;
        return InvokeClosingAsync(
            ZLinkSpotCloseReason.RelocationOut,
            deadline);
    }

    internal async ValueTask<bool> TryCloseIfNoActorsAsync(
        ZLinkSpotCloseReason reason,
        DateTimeOffset deadline,
        CancellationToken cancellationToken)
    {
        return await _serial.ExecuteQuiescentLifecycleAsync(
                async (activation, ct) =>
                {
                    if (activation._actors.Count > 0) return false;

                    if (Interlocked.Exchange(ref activation._closingInvoked, 1) == 0)
                        await activation.InvokeClosingAsync(reason, deadline)
                            .ConfigureAwait(false);
                    return true;
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    private ValueTask InvokeClosingAsync(
        ZLinkSpotCloseReason reason,
        DateTimeOffset deadline)
    {
        return Spot switch
        {
            IZLinkSpot user => ZLinkSpotClosingInvocation.InvokeAsync(
                user.OnClosingAsync,
                reason,
                deadline),
            IZLinkInstanceSpot instance => ZLinkSpotClosingInvocation.InvokeAsync(
                instance.OnClosingAsync,
                reason,
                deadline),
            _ => throw new InvalidOperationException("The SPOT lifecycle is not attached.")
        };
    }

    private ValueTask ExecuteSerializedAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        return _serial.ExecuteAsync(operation, cancellationToken);
    }

    private ValueTask ExecuteSerializedAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        return _serial.ExecuteAsync(operation, state, cancellationToken);
    }

    private async ValueTask ExecuteApplicationSerializedAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (!_runtime.TryEnterInboundOperation(countAsRequest: false, out var lease))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                "SPOT application admission is sealed for drain.");
        using (lease)
            await _serial.ExecuteAsync(operation, state, cancellationToken).ConfigureAwait(false);
    }

    private bool QueueSerialized(Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation)
    {
        return _serial.Queue(operation);
    }

    private bool QueueSerialized<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        Action? onSkipped = null)
    {
        var capturedOp = operation;
        var capturedState = state;
        return _serial.Queue(
            (activation, ct) => capturedOp(activation, capturedState, ct),
            onSkipped);
    }

    private bool QueueApplicationSerialized(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        bool countAsRequest,
        Action? onRejected = null)
    {
        if (!_runtime.TryEnterInboundOperation(countAsRequest, out var lease))
        {
            onRejected?.Invoke();
            return false;
        }

        var queued = _serial.Queue(
            async (activation, ct) =>
            {
                using (lease)
                    await operation(activation, ct).ConfigureAwait(false);
            },
            () =>
            {
                lease.Dispose();
                onRejected?.Invoke();
            });
        if (!queued) lease.Dispose();
        return queued;
    }

    private bool QueueApplicationSerialized<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        bool countAsRequest,
        Action? onRejected = null)
    {
        if (!_runtime.TryEnterInboundOperation(countAsRequest, out var lease))
        {
            onRejected?.Invoke();
            return false;
        }

        var queued = QueueSerialized(
            async (activation, captured, ct) =>
            {
                using (lease)
                    await operation(activation, captured, ct).ConfigureAwait(false);
            },
            state,
            () =>
            {
                lease.Dispose();
                onRejected?.Invoke();
            });
        if (!queued) lease.Dispose();
        return queued;
    }

    private bool QueueApplicationSerialized<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        ReadOnlyMemory<byte> acceptedJournalRecord,
        bool countAsRequest,
        Action onRejected,
        Action onMoving,
        Action relocationRelease)
    {
        if (!_runtime.TryEnterInboundOperation(countAsRequest, out var lease))
        {
            onRejected();
            return false;
        }

        var capturedOperation = operation;
        var capturedState = state;
        var released = 0;
        void ReleaseForRelocation()
        {
            if (Interlocked.Exchange(ref released, 1) != 0) return;
            lease.Dispose();
            relocationRelease();
        }

        var admission = _serial.QueueAccepted(
            acceptedJournalRecord,
            async (activation, ct) =>
            {
                using (lease)
                    await capturedOperation(activation, capturedState, ct).ConfigureAwait(false);
            },
            ReleaseForRelocation,
            out _);
        if (admission == ZLinkAcceptedWorkAdmission.Accepted)
            return true;

        lease.Dispose();
        if (admission == ZLinkAcceptedWorkAdmission.RelocationMoving)
            onMoving();
        else
            onRejected();
        return false;
    }

    private bool QueueApplicationRouteSerialized(
        ZLinkBackendRouteReceived received)
    {
        var replyRouteId = 0UL;
        if (received.CanReply)
        {
            if (received.OperationId == default
                || received.RequestSeq is not { } correlation
                || correlation == 0
                || correlation != received.OperationId.Low)
            {
                received.Dispose();
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestRejected,
                    "A Spot request has no source-owned reply correlation.");
            }
            replyRouteId = correlation;
        }
        var acceptedJournalRecord = ZLinkSpotAcceptedJournal.CaptureOrDispose(
            received,
            replyRouteId);

        return QueueApplicationSerialized(
            static (activation, state, ct) => activation._dispatcher
                .DispatchRouteAsync(state, ct),
            received,
            acceptedJournalRecord,
            received.CanReply,
            () =>
            {
                ZLinkSpotActivationDispatcher.RejectApplicationRouteForDrain(
                    received,
                    ChannelName);
            },
            () =>
            {
                ZLinkSpotActivationDispatcher
                    .RejectApplicationRouteForRelocation(
                        received,
                        ChannelName);
            },
            received.Dispose);
    }

    private bool QueueActorFrames(ZLinkSpotActorFrameBatch frames)
    {
        if (!_runtime.TryEnterInboundOperation(countAsRequest: false, out var lease))
            return false;

        if (_serial.TryRunDetached(
                "user-spot-actor-frames",
                async ct =>
                {
                    using (lease)
                        await _dispatcher.DispatchActorFramesAsync(frames, _serial, ct)
                            .ConfigureAwait(false);
                }))
            return true;

        lease.Dispose();
        return false;
    }


    private async ValueTask DispatchSubscriptionsAsync(CancellationToken cancellationToken)
    {
        await _dispatcher
            .DispatchSubscriptionsAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchActorLifecycleDrainAsync(CancellationToken cancellationToken)
    {
        while (true)
        {
            var lifecycle = NativeSpot.RecvActorLifecycle(RecvFlags.DontWait);
            if (lifecycle is null) return;
            if (lifecycle.Value.Kind != ZLinkBackendActorLifecycleEventKind.Disconnected)
                continue;

            var actorId = lifecycle.Value.Info.CurrentActor?.ActorId;
            if (actorId is null) continue;

            if (_actors.TryGetActor(actorId, out var actor) && actor is not null)
            {
                await NotifyActorDisconnectedCoreAsync(actor, cancellationToken)
                    .ConfigureAwait(false);
                continue;
            }

            await _runtime.NotifyActorDisconnectedByIdAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask<bool> InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        if (_timers.IsFrozen) return false;
        await HandlerInvoker.InvokeTimerAsync(descriptor, tick, cancellationToken).ConfigureAwait(false);
        return true;
    }

    internal async ValueTask<ZLinkSpotRelocationSeal> SealRelocationAsync(
        CancellationToken cancellationToken)
    {
        var logicalTimers = _timers.FreezeRelocation();
        try
        {
            var queueSeal = await _serial
                .SealRelocationAsync(cancellationToken)
                .ConfigureAwait(false);
            return new ZLinkSpotRelocationSeal(queueSeal, logicalTimers);
        }
        catch
        {
            _timers.Resume();
            throw;
        }
    }

    internal bool IsRelocationReady => _serial.IsRelocationReady;

    internal ulong SourceNodeLifecycleGeneration =>
        _runtime.GetSpotNodeRuntime(NodeRid).Node.MeshStatus()
            .LifecycleGeneration;

    internal ZLinkLocationOwnerToken SourceOwnerToken =>
        _runtime.LocationLifecycle?.OwnerToken
        ?? throw new ZLinkConfigurationException(
            "Location runtime is required for SPOT relocation.");

    internal ZLinkRemoteActorBoundSessionRoute
        CaptureActorBoundSessionRouteForRetire(string actorId)
    {
        var actorState = _runtime.GetOrCreateActorState(actorId);
        if (!actorState.TryGetBoundSession(out var session)
            || session.SessionNodeRid is not { } sessionNodeRid)
            return default;
        return new ZLinkRemoteActorBoundSessionRoute(
            NodeRid: sessionNodeRid,
            SessionRid: session.SessionRid,
            BindingToken: session.BindingToken,
            BindingGeneration: session.BindingGeneration,
            ObjectGeneration: session.ObjectGeneration,
            AuthorityOwnerGeneration:
                session.AuthorityOwnerGeneration,
            MeshName: session.MeshName,
            TargetNodeGeneration: session.TargetNodeGeneration,
            OwnerLeaseGeneration: session.OwnerLeaseGeneration,
            SessionOwnerNodeGeneration:
                session.SessionOwnerNodeGeneration,
            AcceptedHighWater: session.AcceptedHighWater);
    }

    internal async ValueTask<ZLinkRemoteActorBoundSessionRoute>
        SealActorBoundSessionRouteForRetireAsync(
            string actorId,
            string handoffId,
            CancellationToken cancellationToken)
    {
        var route = CaptureActorBoundSessionRouteForRetire(actorId);
        if (!route.IsBound)
            return route;
        var seal = new ZLinkSessionRouteSeal(
            actorId,
            route.BindingToken!,
            route.BindingGeneration,
            route.ObjectGeneration,
            route.AuthorityOwnerGeneration,
            route.MeshName!,
            route.TargetNodeGeneration,
            route.OwnerLeaseGeneration,
            route.SessionOwnerNodeGeneration,
            handoffId);
        ZLinkSessionRouteSealResult result;
        var routeNodeRid = route.NodeRid
                           ?? throw new InvalidOperationException(
                               "A bound Session route requires a target NodeRid.");
        if (routeNodeRid
            == _runtime.GetMeshNodeRuntime(route.MeshName!).Node.RoutingId)
        {
            result = await _runtime.SealSessionActorRouteAsync(
                    seal,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        else
        {
            var reply = await _runtime.RouteClient
                .RequestToNode(
                    route.MeshName!,
                    routeNodeRid,
                    new ZLinkSessionRouteSealRequest(
                        actorId,
                        route.BindingToken!,
                        route.BindingGeneration,
                        route.ObjectGeneration,
                        route.AuthorityOwnerGeneration,
                        route.MeshName!,
                        route.TargetNodeGeneration,
                        route.OwnerLeaseGeneration,
                        route.SessionOwnerNodeGeneration,
                        handoffId))
                .Timeout(DefaultRequestTimeout)
                .Async<ZLinkSessionRouteSealReply>(cancellationToken)
                .ConfigureAwait(false);
            result = new ZLinkSessionRouteSealResult(
                reply.Acknowledged,
                reply.AcceptedHighWater);
        }
        if (!result.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{actorId}' session ingress seal was fenced.");
        return route with { AcceptedHighWater = result.AcceptedHighWater };
    }

    internal async ValueTask AbortActorBoundSessionRouteSealForRetireAsync(
        string actorId,
        ZLinkRemoteActorBoundSessionRoute route,
        string handoffId,
        CancellationToken cancellationToken)
    {
        if (!route.IsBound)
            return;
        var seal = new ZLinkSessionRouteSeal(
            actorId,
            route.BindingToken!,
            route.BindingGeneration,
            route.ObjectGeneration,
            route.AuthorityOwnerGeneration,
            route.MeshName!,
            route.TargetNodeGeneration,
            route.OwnerLeaseGeneration,
            route.SessionOwnerNodeGeneration,
            handoffId);
        bool acknowledged;
        var routeNodeRid = route.NodeRid
                           ?? throw new InvalidOperationException(
                               "A bound Session route requires a target NodeRid.");
        if (routeNodeRid
            == _runtime.GetMeshNodeRuntime(route.MeshName!).Node.RoutingId)
        {
            acknowledged = _runtime.AbortSessionActorRouteSeal(seal);
        }
        else
        {
            var reply = await _runtime.RouteClient
                .RequestToNode(
                    route.MeshName!,
                    routeNodeRid,
                    new ZLinkSessionRouteAbortRequest(
                        actorId,
                        route.BindingToken!,
                        route.BindingGeneration,
                        route.ObjectGeneration,
                        route.AuthorityOwnerGeneration,
                        route.MeshName!,
                        route.TargetNodeGeneration,
                        route.OwnerLeaseGeneration,
                        route.SessionOwnerNodeGeneration,
                        handoffId))
                .Timeout(DefaultRequestTimeout)
                .Async<ZLinkSessionRouteSealReply>(cancellationToken)
                .ConfigureAwait(false);
            acknowledged = reply.Acknowledged;
        }
        if (!acknowledged)
            throw new ZLinkRelocationDataLostException(
                $"Actor '{actorId}' source session route seal was not restored.");
    }

    internal void BeginMessageFollow(
        RoutingId targetNodeRid,
        ulong targetNodeGeneration,
        ulong sourceAuthorityOwnerGeneration,
        ulong targetAuthorityOwnerGeneration,
        ZLinkLocationOwnerToken targetOwner)
    {
        var duration = _runtime.Registration.Locations.Options
            .MessageFollowDuration;
        if (duration <= TimeSpan.Zero)
            return;
        Volatile.Write(
            ref _messageFollow,
            new ZLinkSpotMessageFollow(
                targetNodeRid,
                ObjectGeneration,
                SourceNodeLifecycleGeneration,
                targetNodeGeneration,
                sourceAuthorityOwnerGeneration,
                targetAuthorityOwnerGeneration,
                SourceOwnerToken,
                targetOwner,
                DateTimeOffset.UtcNow + duration));
        ZLinkFrameworkDebugLog.SpotDiscovery(
            $"message_follow_registered target_rid={targetNodeRid}");
    }

    internal TimeSpan MessageFollowRemaining
    {
        get
        {
            var messageFollow = Volatile.Read(ref _messageFollow);
            return messageFollow is null
                ? TimeSpan.Zero
                : messageFollow.ExpiresAt - DateTimeOffset.UtcNow;
        }
    }

    private ZLinkSpotMessageFollowResult TryMessageFollow(
        ZLinkBackendRouteReceived received)
    {
        var messageFollow = Volatile.Read(ref _messageFollow);
        if (messageFollow is null)
            return ZLinkSpotMessageFollowResult.NotApplicable;
        var now = DateTimeOffset.UtcNow;
        var currentSourceOwner =
            _runtime.LocationLifecycle?.OwnerToken;
        if (!messageFollow.MatchesSourceRoute(
                received,
                ObjectGeneration,
                currentSourceOwner,
                now))
        {
            _ = Interlocked.CompareExchange(
                ref _messageFollow,
                null,
                messageFollow);
            ZLinkFrameworkDebugLog.SpotDiscovery(
                messageFollow.ExpiresAt <= now
                    ? "message_follow_expired"
                    : "message_follow_rejected");
            return ZLinkSpotMessageFollowResult.StaleRejected;
        }
        ReadOnlyMemory<byte> metadata;
        long bytes;
        try
        {
            metadata = ZLinkMeshMetadataCodec.Encode(received.Metadata);
            bytes = ZLinkServiceWireCodec.MeasureSpotMessageFollowEncodedBytes(
                received.CanReply,
                received.OperationId,
                SpotId,
                SpotId,
                ObjectGeneration,
                messageFollow.TargetNodeRid,
                messageFollow.TargetNodeGeneration,
                messageFollow.TargetAuthorityOwnerGeneration,
                checked((ulong)messageFollow.TargetOwner.LeaseGeneration),
                checked((byte)(received.MessageFollowHopCount + 1)),
                received.Parts,
                metadata);
        }
        catch
        {
            received.Dispose();
            throw;
        }
        if (!messageFollow.TryAcquire(bytes, out var lease))
            return ZLinkSpotMessageFollowResult.Full;
        lease = lease
                ?? throw new InvalidOperationException(
                    "Spot Message Follow admission did not return a lease.");
        if (!received.CanReply)
        {
            try
            {
                if (NativeSpot is not IZLinkBackendSpotMessageFollower relay)
                    throw new InvalidOperationException(
                        "The Spot backend does not support Message Follow.");
                _ = relay.MessageFollowSendToSpot(
                    messageFollow.TargetNodeRid,
                    SpotId,
                    ObjectGeneration,
                    received.OperationId,
                    messageFollow.TargetNodeGeneration,
                    messageFollow.TargetAuthorityOwnerGeneration,
                    checked((ulong)messageFollow.TargetOwner.LeaseGeneration),
                    checked((byte)(received.MessageFollowHopCount + 1)),
                    received.Parts,
                    SendFlags.DontWait,
                    metadata);
                received.Dispose();
                ZLinkFrameworkDebugLog.SpotDiscovery("message_follow_relay");
            }
            finally
            {
                lease.Dispose();
            }
            return ZLinkSpotMessageFollowResult.Followed;
        }

        if (NativeSpot is not IZLinkBackendSpotMessageFollower requestRelay)
        {
            lease.Dispose();
            return ZLinkSpotMessageFollowResult.StaleRejected;
        }
        var remainingTimeout = RemainingRequestTimeout(received, DateTimeOffset.UtcNow);
        if (remainingTimeout == TimeSpan.Zero)
        {
            lease.Dispose();
            received.Dispose();
            return ZLinkSpotMessageFollowResult.Followed;
        }
        var followed = SubmitSpotMessageFollowRequest(
            received,
            lease,
            callback => requestRelay.MessageFollowRequestToSpot(
                messageFollow.TargetNodeRid,
                SpotId,
                ObjectGeneration,
                received.OperationId,
                messageFollow.TargetNodeGeneration,
                messageFollow.TargetAuthorityOwnerGeneration,
                checked((ulong)messageFollow.TargetOwner.LeaseGeneration),
                checked((byte)(received.MessageFollowHopCount + 1)),
                received.DeadlineUnixMs,
                received.Parts,
                callback,
                SendFlags.DontWait,
                timeout: remainingTimeout,
                metadata: metadata))
            ? ZLinkSpotMessageFollowResult.Followed
            : ZLinkSpotMessageFollowResult.Full;
        if (followed == ZLinkSpotMessageFollowResult.Followed)
            ZLinkFrameworkDebugLog.SpotDiscovery("message_follow_relay");
        return followed;
    }

    internal static bool SubmitSpotMessageFollowRequest(
        ZLinkBackendRouteReceived received,
        ZLinkSpotMessageFollow.AdmissionLease admission,
        Func<RequestCallback, bool> submit)
    {
        ArgumentNullException.ThrowIfNull(received);
        ArgumentNullException.ThrowIfNull(admission);
        ArgumentNullException.ThrowIfNull(submit);
        try
        {
            var accepted = submit((result, parts) =>
            {
                try
                {
                    if (result == RequestResult.Ok
                        && RemainingRequestTimeout(
                            received,
                            DateTimeOffset.UtcNow) != TimeSpan.Zero)
                        _ = received.Reply(parts, SendFlags.None);
                }
                finally
                {
                    ZLinkMessageParts.DisposeAll(parts);
                    received.Dispose();
                    admission.Dispose();
                }
            });
            if (accepted) return true;
            admission.Dispose();
            return false;
        }
        catch
        {
            received.Dispose();
            admission.Dispose();
            throw;
        }
    }

    internal static TimeSpan? RemainingRequestTimeout(
        ZLinkBackendRouteReceived received,
        DateTimeOffset now)
    {
        ArgumentNullException.ThrowIfNull(received);
        if (received.DeadlineUnixMs == 0)
            return null;
        var remainingMilliseconds = checked((long)received.DeadlineUnixMs)
                                    - now.ToUnixTimeMilliseconds();
        return remainingMilliseconds <= 0
            ? TimeSpan.Zero
            : TimeSpan.FromMilliseconds(remainingMilliseconds);
    }

    internal bool TrySealRelocation(out ZLinkSpotRelocationSeal seal)
    {
        var logicalTimers = _timers.FreezeRelocation();
        if (_serial.TrySealRelocation(out var queueSeal))
        {
            seal = new ZLinkSpotRelocationSeal(queueSeal, logicalTimers);
            return true;
        }
        _timers.Resume();
        seal = null!;
        return false;
    }

    internal bool TrySealRelocation(
        Func<
            IReadOnlyList<ZLinkAcceptedWorkRecord>,
            IReadOnlyList<ZLinkRelocationLogicalTimer>,
            bool> admit,
        out ZLinkSpotRelocationSeal seal)
    {
        ArgumentNullException.ThrowIfNull(admit);
        var logicalTimers = _timers.FreezeRelocation();
        var pendingTimerCount = logicalTimers.Count(static timer =>
            ZLinkSpotTimerRelocationCodec.Decode(timer).Timer.PendingTick.HasValue);
        if (_serial.TrySealRelocation(
                pendingTimerCount,
                captured => admit(captured, logicalTimers),
                out var queueSeal,
                out var firstPendingSequence))
        {
            var nextPendingSequence = firstPendingSequence;
            logicalTimers = logicalTimers.Select(timer =>
            {
                var snapshot = ZLinkSpotTimerRelocationCodec.Decode(timer);
                return snapshot.Timer.PendingTick.HasValue
                    ? timer with
                    {
                        PendingAcceptedSequence = nextPendingSequence++
                    }
                    : timer;
            }).ToArray();
            seal = new ZLinkSpotRelocationSeal(queueSeal, logicalTimers);
            return true;
        }
        _timers.Resume();
        seal = null!;
        return false;
    }

    internal bool AbortRelocation(ZLinkSpotRelocationSeal seal)
    {
        ArgumentNullException.ThrowIfNull(seal);
        if (!_serial.TryAbortRelocation(seal.QueueSeal))
            return false;
        _timers.Resume();
        return true;
    }

    internal bool CommitRelocation(
        ZLinkSpotRelocationSeal seal,
        out IReadOnlyList<ZLinkAcceptedWorkRecord> held)
    {
        ArgumentNullException.ThrowIfNull(seal);
        return _serial.TryCommitRelocation(seal.QueueSeal, out held);
    }

    internal bool FreezeRelocationIngress(
        ZLinkSpotRelocationSeal seal,
        out IReadOnlyList<ZLinkAcceptedWorkRecord> held)
    {
        ArgumentNullException.ThrowIfNull(seal);
        return _serial.TryFreezeRelocationIngress(
            seal.QueueSeal,
            out held);
    }

    internal void RestoreLogicalTimers(
        IReadOnlyList<ZLinkRelocationLogicalTimer> logicalTimers)
    {
        _timers.RestoreRelocation(
            logicalTimers,
            Spot.GetType(),
            StopToken,
            DispatchTimerAsync,
            PublishTimerFailureAsync);
    }

    internal async ValueTask ReplayAcceptedJobsAsync(
        IReadOnlyList<ZLinkRelocationQueuedJob> jobs,
        string sourceMeshName,
        ZLinkSpotRelocationSeal admissionSeal,
        int completedCount,
        Func<
            ZLinkRelocationQueuedJob,
            ZLinkSpotAcceptedJournalRecord,
            byte[][]?,
            CancellationToken,
            ValueTask> replayCompleted,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(admissionSeal);
        ArgumentNullException.ThrowIfNull(replayCompleted);
        var ordered = jobs.OrderBy(static job => job.AcceptedSequence)
            .ToArray();
        if (completedCount < 0 || completedCount > ordered.Length)
            throw new ArgumentOutOfRangeException(nameof(completedCount));
        foreach (var job in ordered.Skip(completedCount))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var journal = DecodeRelocationReplayRecord(job, sourceMeshName);
            var parts = journal.Parts
                .Select(static part => Message.From(part.Span))
                .ToArray();
            byte[][]? capturedReply = null;
            Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? reply = null;
            if (journal.ReplyRouteId != 0
                && journal.SourceNodeRid is { } sourceNodeRid)
            {
                reply = (replyParts, _) =>
                {
                    capturedReply = replyParts
                        .Select(static part => part.ToArray())
                        .ToArray();
                    return SubmitResult.Ok;
                };
            }
            var received = new ZLinkBackendRouteReceived(
                parts,
                journal.SourceNodeRid,
                journal.SpotId,
                journal.RequestSequence,
                reply,
                metadata: journal.Metadata,
                operationId: journal.OperationId,
                targetNodeGeneration: journal.TargetNodeGeneration,
                authorityOwnerGeneration:
                    journal.AuthorityOwnerGeneration,
                ownerLeaseGeneration: journal.OwnerLeaseGeneration,
                messageFollowHopCount: journal.MessageFollowHopCount,
                sourceNodeGeneration: journal.SourceNodeGeneration);
            try
            {
                await _serial.ExecuteSealedRelocationAsync(
                        admissionSeal.QueueSeal,
                        (activation, ct) =>
                            activation._dispatcher.DispatchRouteAsync(
                                received,
                                ct),
                        cancellationToken)
                    .ConfigureAwait(false);
                await replayCompleted(
                        job,
                        journal,
                        capturedReply,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            finally
            {
                received.Dispose();
            }
        }
    }

    private static ZLinkSpotAcceptedJournalRecord DecodeRelocationReplayRecord(
        ZLinkRelocationQueuedJob job,
        string channelName)
    {
        if (job.CanonicalRequest is not { } request)
            return ZLinkSpotAcceptedJournal.Decode(job.Payload.Span);
        var kind = request.ReplyRouteId == 0
            ? ZLinkMessageKind.Command
            : ZLinkMessageKind.Request;
        using var header = ZLinkEnvelopeCodec.EncodeHeader(
            new ZLinkEnvelopeHeader(
                kind,
                channelName,
                request.ApplicationPayload.PacketName,
                request.ApplicationPayload.ContentType,
                request.ReplyRouteId == 0
                    ? null
                    : request.ReplyRouteId.ToString(
                        System.Globalization.CultureInfo.InvariantCulture),
                null,
                null,
                null,
                null,
                request.SourceSpotId));
        return new ZLinkSpotAcceptedJournalRecord(
            RoutingId.FromHex(request.Source.NodeRid),
            request.Source.NodeGeneration,
            new ZLinkServiceWireCodec.RequestSourceFence(
                request.Source.OwnerId,
                request.Source.OwnerLeaseGeneration,
                RoutingId.FromHex(request.Source.NodeRid),
                request.Source.NodeGeneration),
            request.SourceSpotId,
            null,
            request.ReplyRouteId,
            new MeshOperationId(request.OperationHigh, request.OperationLow),
            request.TargetNodeGeneration,
            request.TargetAuthorityOwnerGeneration,
            request.TargetOwnerLeaseGeneration,
            0,
            request.Metadata,
            [header.ToArray(), request.ApplicationPayload.Payload.ToArray()]);
    }

    private async ValueTask<bool> DispatchTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        var state = new TimerDispatchState(descriptor, tick);
        if (!_runtime.TryEnterInboundOperation(countAsRequest: false, out var lease))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                "SPOT application admission is sealed for drain.");
        using (lease)
            await _serial.ExecuteTimerAsync(
                descriptor.Name,
                async static (activation, state, innerCt) =>
                {
                    state.Delivered = await activation
                        .InvokeTimerAsync(
                            state.Descriptor,
                            state.Tick,
                            innerCt)
                        .ConfigureAwait(false);
                },
                state,
                cancellationToken)
                .ConfigureAwait(false);
        return state.Delivered;
    }

    internal async ValueTask<ZLinkSpotRelocationApplicationState>
        CaptureRelocationApplicationStateAsync(CancellationToken cancellationToken)
    {
        ZLinkSpotRelocationApplicationState? captured = null;
        await _serial.ExecuteLifecycleAsync(
                async (activation, ct) =>
                {
                    var spotRegistration = activation.ResolveSpotRelocationRegistration();
                    var spotState = await activation.CaptureInstanceAsync(
                            spotRegistration,
                            activation.Spot,
                            ct)
                        .ConfigureAwait(false);
                    var actorStates =
                        new Dictionary<string, ReadOnlyMemory<byte>>(StringComparer.Ordinal);
                    foreach (var actor in activation._actors.Snapshot())
                    {
                        var actorRegistration =
                            activation.ResolveActorRelocationRegistration(actor);
                        actorStates.Add(
                            actor.Context.ActorId,
                            await activation.CaptureInstanceAsync(
                                    actorRegistration,
                                    actor,
                                    ct)
                                .ConfigureAwait(false));
                    }
                    captured = new ZLinkSpotRelocationApplicationState(
                        spotState,
                        actorStates);
                },
                cancellationToken)
            .ConfigureAwait(false);
        return captured
               ?? throw new InvalidOperationException(
                   "SPOT relocation capture did not complete.");
    }

    internal async ValueTask<ZLinkSpotRelocationApplicationState>
        CaptureSealedRelocationApplicationStateAsync(
            ZLinkSpotRelocationSeal seal,
            CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(seal);
        ZLinkSpotRelocationApplicationState? captured = null;
        await _serial.ExecuteSealedRelocationAsync(
                seal.QueueSeal,
                async (activation, ct) =>
                {
                    var spotRegistration =
                        activation.ResolveSpotRelocationRegistration();
                    var spotState = await activation.CaptureInstanceAsync(
                            spotRegistration,
                            activation.Spot,
                            ct)
                        .ConfigureAwait(false);
                    var actorStates =
                        new Dictionary<string, ReadOnlyMemory<byte>>(
                            StringComparer.Ordinal);
                    foreach (var actor in activation._actors.Snapshot())
                    {
                        actorStates.Add(
                            actor.Context.ActorId,
                            await activation.CaptureInstanceAsync(
                                    activation.ResolveActorRelocationRegistration(
                                        actor),
                                    actor,
                                    ct)
                                .ConfigureAwait(false));
                    }
                    captured = new ZLinkSpotRelocationApplicationState(
                        spotState,
                        actorStates);
                },
                cancellationToken)
            .ConfigureAwait(false);
        return captured
               ?? throw new InvalidOperationException(
                   "SPOT relocation capture did not complete.");
    }

    internal async ValueTask RestoreRelocationApplicationStateAsync(
        ZLinkSpotRelocationApplicationState state,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(state);
        await _serial.ExecuteLifecycleAsync(
                async (activation, ct) =>
                {
                    await activation.RestoreInstanceAsync(
                            activation.ResolveSpotRelocationRegistration(),
                            activation.Spot,
                            state.SpotState,
                            ct)
                        .ConfigureAwait(false);
                    foreach (var actor in activation._actors.Snapshot())
                    {
                        if (!state.ActorStates.TryGetValue(
                                actor.Context.ActorId,
                                out var actorState))
                            throw new InvalidDataException(
                                $"Relocation state for Actor '{actor.Context.ActorId}' is missing.");
                        await activation.RestoreInstanceAsync(
                                activation.ResolveActorRelocationRegistration(actor),
                                actor,
                                actorState,
                                ct)
                            .ConfigureAwait(false);
                    }
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ValueTask RestoreSpotRelocationStateAsync(
        ReadOnlyMemory<byte> state,
        CancellationToken cancellationToken) =>
        _serial.ExecuteLifecycleAsync(
            (activation, ct) => activation.RestoreInstanceAsync(
                activation.ResolveSpotRelocationRegistration(),
                activation.Spot,
                state,
                ct),
            cancellationToken);

    private ZLinkObjectRelocationRegistration ResolveSpotRelocationRegistration()
    {
        var node = _runtime.Registration.SpotNodes[SpotNodeName];
        var matches = node.SpotRelocations.Values
            .Concat(node.InstanceSpotRelocations.Values)
            .Where(registration => registration.InstanceType == Spot.GetType())
            .Distinct()
            .ToArray();
        return matches.Length switch
        {
            1 => matches[0],
            0 => throw new ZLinkConfigurationException(
                $"Relocation policy for SPOT '{Spot.GetType()}' is not registered."),
            _ => throw new ZLinkConfigurationException(
                $"Relocation policy for SPOT '{Spot.GetType()}' is ambiguous.")
        };
    }

    internal ZLinkObjectRelocationRegistration
        ResolveSpotRelocationRegistrationForRetire() =>
        ResolveSpotRelocationRegistration();

    internal string ResolveStableTypeForRetire()
    {
        var node = _runtime.Registration.SpotNodes[SpotNodeName];
        var matches = node.SpotRelocations
            .Concat(node.InstanceSpotRelocations)
            .Where(entry => entry.Value.InstanceType == Spot.GetType())
            .Select(static entry => entry.Key)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
        return matches.Length == 1
            ? matches[0]
            : throw new ZLinkConfigurationException(
                $"Relocation stable type for SPOT '{Spot.GetType()}' is not unique.");
    }

    internal IReadOnlyList<string> SnapshotActorIds() =>
        _actors.Snapshot()
            .Select(static actor => actor.Context.ActorId)
            .OrderBy(static actorId => actorId, StringComparer.Ordinal)
            .ToArray();

    internal IReadOnlyList<ZLinkObjectCapability> ResolveRetireCapabilities(
        bool instanceSpot)
    {
        var capabilities = new List<ZLinkObjectCapability>();
        var spot = ResolveSpotRelocationRegistration();
        capabilities.Add(CreateRetireCapability(
            instanceSpot
                ? ZLinkPlacementObjectKind.InstanceSpot
                : ZLinkPlacementObjectKind.UserSpot,
            ResolveStableTypeForRetire(),
            spot));
        foreach (var actor in _actors.Snapshot())
        {
            var actorType = _runtime.GetOrCreateActorState(
                    actor.Context.ActorId)
                .ActorType
                            ?? throw new ZLinkConfigurationException(
                                $"Relocation stable type for Actor '{actor.Context.ActorId}' is not registered.");
            capabilities.Add(CreateRetireCapability(
                ZLinkPlacementObjectKind.Actor,
                actorType,
                ResolveActorRelocationRegistration(actor)));
        }
        return capabilities;
    }

    private static ZLinkObjectCapability CreateRetireCapability(
        ZLinkPlacementObjectKind kind,
        string stableType,
        ZLinkObjectRelocationRegistration registration) =>
        new(
            kind,
            stableType,
            registration.PolicyKind switch
            {
                0 => ZLinkObjectMaintenancePolicyKind.Disabled,
                1 => ZLinkObjectMaintenancePolicyKind.Recreate,
                2 => ZLinkObjectMaintenancePolicyKind.Snapshot,
                _ => throw new ZLinkConfigurationException(
                    $"Unknown relocation policy kind '{registration.PolicyKind}'.")
            },
            registration.AdapterType is not null,
            0);

    internal ZLinkObjectRelocationRegistration
        ResolveActorRelocationRegistrationForRetire(string actorId)
    {
        if (!_actors.TryGetActor(actorId, out var actor) || actor is null)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' left SPOT '{SpotId}' before relocation sealed.");
        return ResolveActorRelocationRegistration(actor);
    }

    private ZLinkObjectRelocationRegistration ResolveActorRelocationRegistration(
        IZLinkActor actor)
    {
        var node = _runtime.Registration.SpotNodes[SpotNodeName];
        var actorType = _runtime.GetOrCreateActorState(actor.Context.ActorId).ActorType;
        if (actorType is not null
            && node.ActorRelocations.TryGetValue(actorType, out var registered))
            return registered;
        var matches = node.ActorRelocations.Values
            .Where(registration => registration.InstanceType == actor.GetType())
            .Distinct()
            .ToArray();
        return matches.Length switch
        {
            1 => matches[0],
            0 => throw new ZLinkConfigurationException(
                $"Relocation policy for Actor '{actor.GetType()}' is not registered."),
            _ => throw new ZLinkConfigurationException(
                $"Relocation policy for Actor '{actor.GetType()}' is ambiguous.")
        };
    }

    private async ValueTask<byte[]> CaptureInstanceAsync(
        ZLinkObjectRelocationRegistration registration,
        object instance,
        CancellationToken cancellationToken)
    {
        return registration.PolicyKind switch
        {
            0 => throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                $"Relocation is disabled for '{registration.InstanceType}'."),
            1 => [],
            2 when registration.AdapterInvoker is { } invoker =>
                await invoker.CaptureAsync(
                        _scope.ServiceProvider,
                        instance,
                        cancellationToken)
                    .ConfigureAwait(false),
            _ => throw new ZLinkConfigurationException(
                $"Relocation adapter for '{registration.InstanceType}' is not registered.")
        };
    }

    private async ValueTask RestoreInstanceAsync(
        ZLinkObjectRelocationRegistration registration,
        object instance,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        switch (registration.PolicyKind)
        {
            case 1:
                if (!payload.IsEmpty)
                    throw new InvalidDataException(
                        $"Recreate relocation state for '{registration.InstanceType}' must be empty.");
                return;
            case 2 when registration.AdapterInvoker is { } invoker:
                await invoker.RestoreAsync(
                        _scope.ServiceProvider,
                        instance,
                        payload,
                        cancellationToken)
                    .ConfigureAwait(false);
                return;
            case 0:
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestRejected,
                    $"Relocation is disabled for '{registration.InstanceType}'.");
            default:
                throw new ZLinkConfigurationException(
                    $"Relocation adapter for '{registration.InstanceType}' is not registered.");
        }
    }

    private ValueTask PublishTimerFailureAsync(
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick,
        Exception exception,
        bool stopped,
        CancellationToken cancellationToken)
    {
        return _runtime.PublishRuntimeEventAsync(
            ZLinkSpotTimerFailureEventFactory.Create(
                SpotNodeName,
                SpotId,
                false,
                descriptor,
                tick,
                exception,
                stopped),
            cancellationToken);
    }

    private static T RegisterWithoutSynchronizationContext<T>(Func<T> action)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(null);
        try
        {
            return action();
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    private sealed class SpotCreateCallState(ZLinkMessage request)
    {
        public ZLinkMessage Request { get; } = request;

        public ZLinkSpotCreateResponse Response { get; set; }
    }

    private sealed class TimerDispatchState(
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick)
    {
        public ZLinkSpotTimerDescriptor Descriptor { get; } = descriptor;

        public ZLinkTimerTick Tick { get; } = tick;

        public bool Delivered { get; set; }
    }
}
