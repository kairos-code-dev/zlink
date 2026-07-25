namespace Zlink.Framework.Runtime.Spots;

internal sealed record ZLinkSpotRelocationSeal(
    ZLinkSpotExecutionRelocationSeal QueueSeal,
    IReadOnlyList<ZLinkRelocationLogicalTimer> LogicalTimers);

internal sealed record ZLinkSpotRelocationApplicationState(
    ReadOnlyMemory<byte> SpotState,
    IReadOnlyDictionary<string, ReadOnlyMemory<byte>> ActorStates);

internal sealed partial class ZLinkSpotActivation
{
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
        ArgumentNullException.ThrowIfNull(work);
        return new ZLinkWorkerCall<TResult>(
            _runtime.WorkerPool,
            work,
            _runtime.ErrorSink);
    }

    public IZLinkWorkerCall<TResult> RunIoWorker<TResult>(
        Func<CancellationToken, ValueTask<TResult>> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        return new ZLinkIoWorkerCall<TResult>(
            _runtime.WorkerPool.ShutdownToken,
            work,
            _runtime.ErrorSink);
    }

    ValueTask<bool> IZLinkSpotContext.CloseAsync(CancellationToken cancellationToken)
    {
        return _runtime.CloseCurrentSpotAsync(SpotId, cancellationToken);
    }

    ValueTask<bool> IZLinkInstanceSpotContext.CloseAsync(CancellationToken cancellationToken)
    {
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

                        QueueApplicationRouteSerialized(received);
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
            IReadOnlyList<ReadOnlyMemory<byte>> payload,
            ReadOnlyMemory<byte>? metadata,
            bool request,
            CancellationToken cancellationToken)
    {
        _ = operationId;
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
            NodeRid,
            SpotId,
            null,
            replyCallback,
            metadata: decodedMetadata);
        byte[] accepted;
        try
        {
            accepted = ZLinkSpotAcceptedJournal.Encode(received);
        }
        catch
        {
            received.Dispose();
            throw;
        }

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

        var queued = _serial.QueueAccepted(
            acceptedJournalRecord,
            async (activation, ct) =>
            {
                using (lease)
                    await capturedOperation(activation, capturedState, ct).ConfigureAwait(false);
            },
            ReleaseForRelocation,
            out _);
        if (queued) return true;

        lease.Dispose();
        onRejected();
        return false;
    }

    private bool QueueApplicationRouteSerialized(
        ZLinkBackendRouteReceived received)
    {
        byte[] acceptedJournalRecord;
        try
        {
            acceptedJournalRecord = ZLinkSpotAcceptedJournal.Encode(received);
        }
        catch
        {
            received.Dispose();
            throw;
        }

        return QueueApplicationSerialized(
            static (activation, state, ct) =>
                activation._dispatcher.DispatchRouteAsync(state, ct),
            received,
            acceptedJournalRecord,
            received.CanReply,
            () => ZLinkSpotActivationDispatcher.RejectApplicationRouteForDrain(
                received,
                ChannelName),
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

    internal void RestoreLogicalTimers(
        IReadOnlyList<ZLinkRelocationLogicalTimer> logicalTimers)
    {
        _timers.RestoreRelocation(
            logicalTimers,
            StopToken,
            DispatchTimerAsync,
            PublishTimerFailureAsync);
    }

    internal void ResumeRestoredLogicalTimers()
    {
        _timers.Resume();
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
                            actor.ActorId,
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
                                actor.ActorId,
                                out var actorState))
                            throw new InvalidDataException(
                                $"Relocation state for Actor '{actor.ActorId}' is missing.");
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

    private ZLinkObjectRelocationRegistration ResolveActorRelocationRegistration(
        IZLinkActor actor)
    {
        var node = _runtime.Registration.SpotNodes[SpotNodeName];
        var actorType = _runtime.GetOrCreateActorState(actor.ActorId).ActorType;
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
