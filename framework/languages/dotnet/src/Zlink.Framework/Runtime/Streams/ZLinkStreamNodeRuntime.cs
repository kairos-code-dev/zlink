using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamNodeRuntime : IAsyncDisposable
{
    internal static readonly TimeSpan SessionShutdownUpperBound = TimeSpan.FromMilliseconds(900);
    internal static readonly TimeSpan SessionForceCleanupUpperBound = TimeSpan.FromMilliseconds(100);
    private const int ControlIngressCapacity = 256;
    private const int ReceiveBatchSize = 64;
    private static readonly TimeSpan ReceivePollInterval =
        TimeSpan.FromMilliseconds(100);
    private readonly ZLinkStreamSessionTable _sessions;
    private readonly ZLinkAsyncSubmitter _sendSubmitter;
    private readonly ZLinkStreamSessionSerialExecutor _sessionIngress;
    private readonly ZLinkStreamSessionSerialExecutor _controlIngress;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly IZLinkRuntimeFailureReporter _errorSink;
    private readonly TimeProvider _timeProvider;
    private readonly string _transport;
    private readonly long _maxMessageSize;
    private readonly ZLinkInboundDispatchBudget? _inboundDispatchBudget;
    private readonly object _receiveStateGate = new();
    private readonly Dictionary<RoutingId, ZLinkStreamReceiveState> _receiveStates = [];
    private readonly Dictionary<RoutingId, LinkedListNode<RoutingId>>
        _disconnectedRoutingIds = [];
    private readonly LinkedList<RoutingId> _disconnectedRoutingIdOrder = [];
    private readonly HashSet<ZLinkStreamReceiveState> _blockedReceiveStates = [];
    private readonly List<ZLinkStreamReceiveState> _receiveStateSnapshot = [];
    private const int DisconnectedRoutingIdLimit = 4096;
    private readonly object _disposeGate = new();
    private Task? _disposeTask;
    private Task? _receiveLoop;
    private Task? _livenessLoop;
    private Task? _monitorLoop;
    private IZLinkBackendSocketPoller? _receivePoller;
    // The binding keeps a multipart receive sequence on the same thread until
    // the part flag is final. This loop is the sole RecvPart caller, so the
    // boundary can remain private to the receive owner.
    private RoutingId? _multipartRoutingId;

    public ZLinkStreamNodeRuntime(
        string nodeName,
        IServiceProvider services,
        IZLinkBackendStreamSocket socket,
        IZLinkBackendSocketMonitor monitor,
        Type? headerSessionType,
        ZLinkRuntimeTaskRunner taskRunner,
        string transport,
        TimeProvider? timeProvider = null,
        bool actorDispatchEnabled = false,
        string? boundEndpoint = null,
        string? advertisedEndpoint = null,
        ZLinkInboundDispatchBudget? inboundDispatchBudget = null,
        ZLinkCompletionAdmissionOwner? completionAdmission = null,
        long maxMessageSize = 16L * 1024L * 1024L)
    {
        NodeName = nodeName;
        Socket = socket;
        Monitor = monitor;
        BoundEndpoint = boundEndpoint;
        AdvertisedEndpoint = advertisedEndpoint;
        _taskRunner = taskRunner;
        _timeProvider = timeProvider ?? TimeProvider.System;
        _transport = transport;
        _maxMessageSize = maxMessageSize;
        _inboundDispatchBudget = inboundDispatchBudget;
        var runtime = services.GetRequiredService<ZLinkFrameworkRuntime>();
        _errorSink = runtime.ErrorSink;
        _sessionIngress = new ZLinkStreamSessionSerialExecutor(runtime.ExecutionOwner, runtime.ErrorSink);
        _controlIngress = new ZLinkStreamSessionSerialExecutor(
            runtime.ExecutionOwner,
            runtime.ErrorSink,
            ControlIngressCapacity);
        _sendSubmitter = new ZLinkAsyncSubmitter(
            socket.OnSendReady,
            runtime.Registration.DefaultSocketSendTimeout,
            _stopSource.Token);
        _sessions = new ZLinkStreamSessionTable(
            services,
            socket,
            headerSessionType,
            runtime.DrainAdmission,
            transport,
            _timeProvider,
            actorDispatchEnabled,
            _sendSubmitter,
            completionAdmission);
    }

    public string NodeName { get; }

    public IZLinkBackendStreamSocket Socket { get; }

    public IZLinkBackendSocketMonitor Monitor { get; }

    internal string? BoundEndpoint { get; }

    internal string? AdvertisedEndpoint { get; }

    internal int SessionCount => _sessions.Count;

    internal void RequestStop()
    {
        _stopSource.Cancel();
        _sessionIngress.RequestStop();
        _controlIngress.RequestStop();
        _sessions.RequestStop();
    }

    internal ValueTask<bool> DrainSessionsAsync(CancellationToken cancellationToken) =>
        _sessions.DrainSessionsAsync(cancellationToken);

    internal void ForceStopSessions() =>
        _sessions.ForceStopSessions();

    public ValueTask DisposeAsync()
    {
        lock (_disposeGate)
            return new ValueTask(_disposeTask ??= DisposeCoreAsync());
    }

    private async Task DisposeCoreAsync()
    {
        var sessions = _sessions.Stop();
        var failures = new List<Exception>();
        Capture(RequestStop);
        await CaptureAsync(_sessionIngress.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(_controlIngress.DisposeAsync).ConfigureAwait(false);
        await CaptureAsync(Monitor.DisposeAsync).ConfigureAwait(false);

        if (_monitorLoop is not null)
            try
            {
                await _monitorLoop;
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }

        if (_livenessLoop is not null)
            try
            {
                await _livenessLoop;
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }

        if (_receiveLoop is not null)
            try
            {
                await _receiveLoop;
            }
            catch (OperationCanceledException)
            {
            }
            catch (ObjectDisposedException)
            {
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }

        Capture(DisposeReceiveStates);

        await CaptureAsync(() => DisposeSessionsAsync(sessions)).ConfigureAwait(false);
        await CaptureAsync(_sendSubmitter.DisposeAsync).ConfigureAwait(false);
        Capture(() => _receivePoller?.Dispose());

        var socketDisposed = false;
        try
        {
            await Socket.DisposeAsync().ConfigureAwait(false);
            socketDisposed = true;
        }
        catch (Exception exception)
        {
            failures.Add(exception);
        }
        if (socketDisposed)
            foreach (var session in sessions) session.ConfirmNodeTransportDisposed();
        Capture(_stopSource.Dispose);
        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
        return;

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

    private async ValueTask DisposeSessionsAsync(
        IReadOnlyCollection<ZLinkStreamSessionRuntime> sessions)
    {
        if (sessions.Count == 0) return;

        var disposals = sessions.Select(static session => session.DisposeAsync().AsTask()).ToArray();
        try
        {
            await Task.WhenAll(disposals)
                .WaitAsync(SessionShutdownUpperBound)
                .ConfigureAwait(false);
            return;
        }
        catch (TimeoutException)
        {
        }

        var forcedCloses = sessions
            .Select(static session => session.ForceCloseForShutdownAsync().AsTask())
            .ToArray();
        try
        {
            await Task.WhenAll(disposals.Concat(forcedCloses))
                .WaitAsync(SessionForceCleanupUpperBound)
                .ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            for (var index = 0; index < disposals.Length; index++)
                ZLinkUnawaitedSubmit.Observe(
                    new ValueTask(disposals[index]),
                    $"stream-session-late-dispose:{index}",
                    _errorSink);
            for (var index = 0; index < forcedCloses.Length; index++)
                ZLinkUnawaitedSubmit.Observe(
                    new ValueTask(forcedCloses[index]),
                    $"stream-session-late-force-close:{index}",
                    _errorSink);
        }
    }

    public void Start()
    {
        _receivePoller ??= Socket.CreateReceivePoller();
        _receiveLoop = _taskRunner.RunLongRunning(
            $"stream-recv:{NodeName}",
            runtimeToken => new ValueTask(RunReceiveLoopUntilStoppedAsync(runtimeToken)));
        _monitorLoop = _taskRunner.RunLongRunning(
            $"stream-monitor:{NodeName}",
            runtimeToken => new ValueTask(RunMonitorLoopUntilStoppedAsync(runtimeToken)));
        _livenessLoop = _taskRunner.Run(
            $"stream-liveness:{NodeName}",
            runtimeToken => new ValueTask(RunLivenessLoopUntilStoppedAsync(runtimeToken)));
    }

    private async Task RunMonitorLoopUntilStoppedAsync(CancellationToken runtimeToken)
    {
        using var stop = CancellationTokenSource.CreateLinkedTokenSource(
            _stopSource.Token,
            runtimeToken);
        await RunMonitorLoopAsync(stop.Token).ConfigureAwait(false);
    }

    private async Task RunLivenessLoopUntilStoppedAsync(CancellationToken runtimeToken)
    {
        using var stop = CancellationTokenSource.CreateLinkedTokenSource(
            _stopSource.Token,
            runtimeToken);
        while (!stop.IsCancellationRequested)
        {
            await Task.Delay(
                ZLinkStreamSessionLiveness.SweepInterval,
                    _timeProvider,
                    stop.Token)
                .ConfigureAwait(false);
            foreach (var session in _sessions.Snapshot()) session.CheckLiveness();
        }
    }

    private async Task RunReceiveLoopUntilStoppedAsync(CancellationToken runtimeToken)
    {
        using var stop = CancellationTokenSource.CreateLinkedTokenSource(
            _stopSource.Token,
            runtimeToken);
        var backoff = new ZLinkPollingBackoff();
        var failureBackoff = new ZLinkPollingBackoff();
        while (!stop.IsCancellationRequested)
        {
            try
            {
                if (_multipartRoutingId is null && !FlushReceiveStates())
                {
                    if (_inboundDispatchBudget is { } budget
                        && !budget.CanStartApplicationReceive)
                    {
                        var ownsResumePermit =
                            await budget.WaitForReceiveCapacityAsync(stop.Token)
                                .ConfigureAwait(false);
                        budget.CompleteReceiveAttempt(ownsResumePermit);
                    }
                    else
                    {
                        await backoff.NoDataAsync(stop.Token).ConfigureAwait(false);
                    }
                    continue;
                }

                backoff.Reset();
                if (_multipartRoutingId is null
                    && _inboundDispatchBudget is { } receiveBudget
                    && !receiveBudget.CanStartApplicationReceive)
                {
                    var ownsResumePermit =
                        await receiveBudget.WaitForReceiveCapacityAsync(stop.Token)
                            .ConfigureAwait(false);
                    receiveBudget.CompleteReceiveAttempt(ownsResumePermit);
                    continue;
                }

                var readiness = _receivePoller!.Wait(ReceivePollInterval);
                if ((readiness & (PollEventFlags.PollIn
                                  | PollEventFlags.PollErr
                                  | PollEventFlags.PollPri)) == 0)
                    continue;

                for (var receivedCount = 0;
                     receivedCount < ReceiveBatchSize
                     && !stop.IsCancellationRequested;
                     receivedCount++)
                {
                    // A transport multipart already started before the HWM
                    // became full must be consumed to preserve Core's part
                    // boundary.  Otherwise, flush blocked complete frames
                    // and stop before issuing the next raw receive.
                    if (_multipartRoutingId is null
                        && !FlushReceiveStates())
                        break;
                    if (_multipartRoutingId is null
                        && _inboundDispatchBudget is { } batchBudget
                        && !batchBudget.CanStartApplicationReceive)
                        break;
                    if (!Socket.RecvPart(
                            out var routingId,
                            out var part,
                            out var hasMore,
                            RecvFlags.DontWait))
                        break;

                    using (part)
                    try
                    {
                        if (routingId is not { } sourceRoutingId || part is null)
                        {
                            _multipartRoutingId = null;
                            _errorSink.ReportRuntimeTaskException(
                                $"stream-recv:{NodeName}",
                                new InvalidDataException(
                                    "STREAM receive did not provide a source routing id or part."));
                            continue;
                        }
                        if (_multipartRoutingId is { } expectedRoutingId
                            && expectedRoutingId != sourceRoutingId)
                        {
                            _multipartRoutingId = null;
                            throw new InvalidDataException(
                                "STREAM multipart receive changed its source routing id.");
                        }
                        _multipartRoutingId = hasMore ? sourceRoutingId : null;
                        ProcessReceived(sourceRoutingId, part);
                    }
                    catch (Exception exception)
                    {
                        if (routingId is { } sourceRoutingId)
                        {
                            if (_multipartRoutingId == sourceRoutingId)
                                _multipartRoutingId = null;
                            HandlePeerReceiveFailure(sourceRoutingId, exception);
                        }
                        else
                            throw;
                    }
                }
                failureBackoff.Reset();
            }
            catch (OperationCanceledException) when (stop.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkRecvException exception)
                when (stop.IsCancellationRequested
                      || exception.Result is ZlinkRecvException.ErrorCode.InternalError
                          or ZlinkRecvException.ErrorCode.InvalidHandle)
            {
                return;
            }
            catch (ZlinkException) when (stop.IsCancellationRequested)
            {
                return;
            }
            catch (Exception exception)
            {
                _errorSink.ReportRuntimeTaskException(
                    $"stream-recv:{NodeName}", exception);
                try
                {
                    await failureBackoff.NoDataAsync(stop.Token).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                    when (stop.IsCancellationRequested)
                {
                    return;
                }
            }
        }
    }

    private void ProcessReceived(RoutingId routingId, Message receivedPart)
    {
        var state = GetOrCreateReceiveState(routingId);
        if (state is null) return;
        var blocked = false;
        lock (state.Gate)
        {
            if (state.Removed) return;
            state.Buffer.Append(receivedPart.AsReadOnlySpan());
            blocked = !DrainReceiveState(state);
        }
        if (blocked) MarkReceiveStateBlocked(state);
    }

    private void HandlePeerReceiveFailure(RoutingId routingId, Exception exception)
    {
        MarkDisconnectedRoutingId(routingId);
        RemoveReceiveState(routingId);
        try
        {
            Socket.DisconnectPeer(routingId);
        }
        catch (Exception disconnectFailure)
        {
            _errorSink.ReportRuntimeTaskException(
                $"stream-recv-disconnect:{NodeName}", disconnectFailure);
        }

        _errorSink.ReportRuntimeTaskException(
            $"stream-recv-peer:{NodeName}", exception);
    }

    private bool FlushReceiveStates()
    {
        lock (_receiveStateGate)
        {
            _receiveStateSnapshot.Clear();
            _receiveStateSnapshot.AddRange(_blockedReceiveStates);
            _blockedReceiveStates.Clear();
        }

        var anyBlocked = false;
        foreach (var state in _receiveStateSnapshot)
        {
            Exception? failure = null;
            var canReceive = true;
            lock (state.Gate)
            {
                if (state.Removed) continue;
                try
                {
                    canReceive = DrainReceiveState(state);
                }
                catch (Exception exception)
                {
                    failure = exception;
                }
            }

            if (failure is not null)
            {
                HandlePeerReceiveFailure(state.RoutingId, failure);
                continue;
            }

            if (!canReceive)
            {
                anyBlocked = true;
                MarkReceiveStateBlocked(state);
            }
        }

        return !anyBlocked;
    }

    private bool DrainReceiveState(ZLinkStreamReceiveState state)
    {
        while (true)
        {
            if (state.Pending is { } pending)
            {
                if (!TryAdmitFrame(state.RoutingId, pending)) return false;
                state.Pending = null;
            }

            if (!state.Buffer.TryTakeFrame(out var frame)) return true;
            if (!TryAdmitFrame(state.RoutingId, frame!))
            {
                state.Pending = frame;
                return false;
            }
        }
    }

    private bool TryAdmitFrame(
        RoutingId routingId,
        ZLinkStreamInboundFrame frame)
    {
        var header = frame.Header
            ?? throw new InvalidOperationException("STREAM frame header ownership was lost.");
        var payload = frame.Payload
            ?? throw new InvalidOperationException("STREAM frame payload ownership was lost.");

        if (_stopSource.IsCancellationRequested || _sessions.IsStopping)
        {
            frame.Dispose();
            return true;
        }

        try
        {
            var admitted = IsApplicationPacket(header)
                ? AdmitApplicationPacket(routingId, header, payload)
                : AdmitControlPacket(routingId, header, payload);
            if (admitted) frame.Detach();
            return admitted;
        }
        catch (ZLinkStreamPeerAdmissionException)
        {
            frame.Dispose();
            throw;
        }
        catch (Exception exception)
        {
            _errorSink.ReportHandlerException(exception);
            frame.Dispose();
            throw;
        }
    }

    private bool AdmitApplicationPacket(
        RoutingId routingId,
        Message header,
        Message payload)
    {
        ZLinkInboundDispatchLease? lease = null;
        if (_inboundDispatchBudget is { } budget
            && !budget.TryTrack(checked((ulong)payload.Size), out lease))
            return false;

        try
        {
            if (_sessions.TryGet(routingId, out var existing))
            {
                var admission = existing.TryEnqueuePacket(header, payload, lease);
                if (admission == ZLinkSerialPostAdmission.Accepted) return true;
                lease?.Dispose();
                if (admission == ZLinkSerialPostAdmission.QueueFull)
                    return false;
                throw new ZLinkStreamPeerAdmissionException(
                    "STREAM peer session queue is closed.");
            }

            if (_sessionIngress.EnqueueApplication(async cancellationToken =>
            {
                var ownershipTransferred = false;
                try
                {
                    var session = await _sessions.GetOrCreateAsync(routingId)
                        .ConfigureAwait(false);
                    if (session is not null)
                        ownershipTransferred = session.TryEnqueuePacket(header, payload, lease)
                            == ZLinkSerialPostAdmission.Accepted;
                }
                finally
                {
                    if (!ownershipTransferred)
                        DisposeRejectedPacket(header, payload, lease);
                }
            }) == ZLinkSerialPostAdmission.Accepted)
                return true;

            lease?.Dispose();
            return false;
        }
        catch
        {
            lease?.Dispose();
            throw;
        }
    }

    private bool AdmitControlPacket(
        RoutingId routingId,
        Message header,
        Message payload)
    {
        if (_sessions.TryGet(routingId, out var existing))
        {
            var admission = existing.TryEnqueueControlPacket(header, payload);
            if (admission == ZLinkSerialPostAdmission.Accepted) return true;
            if (admission == ZLinkSerialPostAdmission.QueueFull) return false;
            throw new ZLinkStreamPeerAdmissionException(
                "STREAM peer control queue is closed.");
        }

        if (_controlIngress.EnqueueControl(async cancellationToken =>
        {
            var ownershipTransferred = false;
            try
            {
                var session = await _sessions.GetOrCreateAsync(routingId)
                    .ConfigureAwait(false);
                if (session is not null)
                    ownershipTransferred = session.TryEnqueueControlPacket(header, payload)
                        == ZLinkSerialPostAdmission.Accepted;
            }
            finally
            {
                if (!ownershipTransferred)
                    DisposeRejectedPacket(header, payload, null);
            }
        }) == ZLinkSerialPostAdmission.Accepted)
            return true;

        return false;
    }

    private static bool IsApplicationPacket(Message header)
    {
        try
        {
            var bytes = header.AsReadOnlySpan();
            if (bytes.Length < 2) return true;
            return (ZlinkStreamMessageKind)bytes[1]
                != ZlinkStreamMessageKind.Control;
        }
        catch
        {
            // A malformed header still consumes application ingress and must
            // be released through the same terminal path as a decoded packet.
            return true;
        }
    }

    private void OnMonitorEvent(ZLinkBackendSocketMonitorEvent monitorEvent)
    {
        if (_sessions.IsStopping) return;

        switch (monitorEvent.NativeEvent)
        {
            case ZLinkSocketNativeEventType.ConnectionReady:
                if (monitorEvent.RoutingId is RoutingId readyRoutingId)
                {
                    ClearDisconnectedRoutingId(readyRoutingId);
                    _ = _controlIngress.EnqueueControl(async cancellationToken =>
                    {
                        var session = await _sessions.GetOrCreateAsync(readyRoutingId).ConfigureAwait(false);
                        session?.EnqueueConnected(monitorEvent.LocalAddr, monitorEvent.RemoteAddr);
                        await ValueTask.CompletedTask;
                    });
                }
                break;
            case ZLinkSocketNativeEventType.Accepted:
                break;
            case ZLinkSocketNativeEventType.Disconnected:
                if (monitorEvent.RoutingId is { } disconnectedRoutingId)
                {
                    MarkDisconnectedRoutingId(disconnectedRoutingId);
                    RemoveReceiveState(disconnectedRoutingId);
                }
                _ = _controlIngress.EnqueueControl(_ =>
                {
                    if (_sessions.TryResolveMonitorSession(monitorEvent.RoutingId, out var disconnectedSession))
                        disconnectedSession.EnqueueDisconnected(
                            new ZLinkStreamError(
                                ZLinkStreamSessionError.TransportError,
                                monitorEvent.NativeEvent.ToString()));
                    return ValueTask.CompletedTask;
                });
                break;
        }
    }

    private ZLinkStreamReceiveState? GetOrCreateReceiveState(RoutingId routingId)
    {
        lock (_receiveStateGate)
        {
            if (_disconnectedRoutingIds.ContainsKey(routingId)) return null;
            if (_receiveStates.TryGetValue(routingId, out var existing))
                return existing;

            var created = new ZLinkStreamReceiveState(routingId, _maxMessageSize);
            _receiveStates.Add(routingId, created);
            return created;
        }
    }

    private void RemoveReceiveState(RoutingId routingId)
    {
        ZLinkStreamReceiveState? state;
        lock (_receiveStateGate)
        {
            if (!_receiveStates.Remove(routingId, out state)) return;
            _blockedReceiveStates.Remove(state);
        }

        state.Dispose();
    }

    private void DisposeReceiveStates()
    {
        ZLinkStreamReceiveState[] states;
        lock (_receiveStateGate)
        {
            states = _receiveStates.Values.ToArray();
            _receiveStates.Clear();
            _blockedReceiveStates.Clear();
            _receiveStateSnapshot.Clear();
        }

        foreach (var state in states) state.Dispose();
    }

    private void MarkReceiveStateBlocked(ZLinkStreamReceiveState state)
    {
        lock (_receiveStateGate)
            _blockedReceiveStates.Add(state);
    }

    private void MarkDisconnectedRoutingId(RoutingId routingId)
    {
        lock (_receiveStateGate)
        {
            if (_disconnectedRoutingIds.ContainsKey(routingId)) return;
            while (_disconnectedRoutingIdOrder.Count >= DisconnectedRoutingIdLimit)
            {
                var oldest = _disconnectedRoutingIdOrder.First;
                if (oldest is null) break;
                _disconnectedRoutingIdOrder.RemoveFirst();
                _disconnectedRoutingIds.Remove(oldest.Value);
            }

            var node = _disconnectedRoutingIdOrder.AddLast(routingId);
            _disconnectedRoutingIds.Add(routingId, node);
        }
    }

    private void ClearDisconnectedRoutingId(RoutingId routingId)
    {
        lock (_receiveStateGate)
        {
            if (!_disconnectedRoutingIds.Remove(routingId, out var node)) return;
            _disconnectedRoutingIdOrder.Remove(node);
        }
    }

    private sealed class ZLinkStreamPeerAdmissionException(string message)
        : InvalidOperationException(message);

    private async Task RunMonitorLoopAsync(CancellationToken cancellationToken)
    {
        var backoff = new ZLinkPollingBackoff();
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                if (!Monitor.TryRecv(out var monitorEvent))
                {
                    await backoff.NoDataAsync(cancellationToken).ConfigureAwait(false);
                    continue;
                }

                backoff.Reset();
                OnMonitorEvent(monitorEvent);
            }
            catch (Exception) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkRecvException ex)
                when (cancellationToken.IsCancellationRequested
                      || ex.Result is ZlinkRecvException.ErrorCode.InternalError
                          or ZlinkRecvException.ErrorCode.InvalidHandle)
            {
                return;
            }
            await Task.Yield();
        }
    }

    private static void DisposeRejectedPacket(
        Message header,
        Message payload,
        ZLinkInboundDispatchLease? inboundDispatchLease)
    {
        try
        {
            header.Dispose();
            payload.Dispose();
        }
        finally
        {
            inboundDispatchLease?.Dispose();
        }
    }
}
