using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Backend.DotNet;

// RouteMesh 10.0.0 node-level pull-dispatch pump (Option B, S8-06).
//
// A single background loop drains the node ready index (SetReadyHandler signals →
// DrainReady(All) until residue is exhausted, infrastructure domain first). Each
// MeshReadyRecord is claimed, its messages pulled into a receive batch, and each
// receive record dispatched by Kind. Claims are always released in finally so a
// dropped claim cannot pin an owner. Records are fanned out to per-owner state
// (keyed by spot rid) that the framework's existing per-spot pull-drain consumers
// (RecvRoute/Subscribe/RecvActorJoin/RecvActorLifecycle) read, and the per-spot
// dispatch-event handler registered via IZLinkBackendSpot.OnDispatchEvent is
// invoked so the framework schedules its drains. Completion records resolve the
// request/reply completion table.
internal sealed class ZLinkMeshDispatchPump : IAsyncDisposable
{
    private readonly IMeshNode _node;
    private readonly ZLinkMeshCompletionTable _completions;
    private readonly ConcurrentDictionary<RoutingId, SpotDispatchState> _spots = new();

    // Core reply tokens of inbound ActorRequest records, keyed by request id
    // (operation id low). No-bind replies (ReplyActorNoBind) redeem the token
    // to answer a mesh actor request without a session binding; bound requests
    // reply through the session plane and leave their token to be evicted by
    // the bound cap.
    private const int MaxPendingActorReplies = 8192;
    private long _nextActorRequestId;
    private readonly ConcurrentDictionary<
        ulong, Func<IReadOnlyList<Message>, SendFlags, SubmitResult>> _actorReplies = new();

    public bool TryTakeActorReply(
        ulong requestId,
        out Func<IReadOnlyList<Message>, SendFlags, SubmitResult> reply)
    {
        return _actorReplies.TryRemove(requestId, out reply!);
    }
    private Action<ActorTransferControl>? _transferControlHandler;
    private Action<ZLinkBackendRouteReceived>? _nodeRouteHandler;
    private readonly object _lifecycleGate = new();
    private readonly SemaphoreSlim _signal = new(0);
    private CancellationTokenSource? _stop;
    private Task? _loop;
    private bool _started;
    private bool _disposed;

    public ZLinkMeshDispatchPump(IMeshNode node, ZLinkMeshCompletionTable completions)
    {
        _node = node;
        _completions = completions;
    }

    public void EnsureStarted()
    {
        lock (_lifecycleGate)
        {
            if (_started || _disposed) return;
            _started = true;
            if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                Console.Error.WriteLine("[pump] started");
            _stop = new CancellationTokenSource();
            _node.SetReadyHandler(OnReady);
            _loop = Task.Run(() => RunAsync(_stop.Token));
        }
    }

    // Registers (or replaces) the per-spot dispatch-event handler and returns the
    // spot's dispatch state so the spot wrapper can pull decoded records.
    public SpotDispatchState RegisterSpot(RoutingId spotRid)
    {
        return _spots.GetOrAdd(spotRid, static _ => new SpotDispatchState());
    }

    public void SetDispatchHandler(
        RoutingId spotRid,
        Action<ZLinkBackendSpotDispatchInfo> handler)
    {
        RegisterSpot(spotRid).DispatchHandler = handler;
        EnsureStarted();
    }

    // Registers the node-level route/channel dispatch sink. Node-addressed
    // (NodeSend/NodeRequest) and channel-addressed (ChannelSend/ChannelRequest)
    // records are owned by the node (ready-record OwnerKind == Node) — their
    // source spot rid is the remote sender's, so they cannot key a local per-spot
    // queue. They are delivered to this single node-level consumer, which routes
    // them to the MeshNode builder's registered route/channel handlers.
    public void SetNodeRouteHandler(Action<ZLinkBackendRouteReceived> handler)
    {
        _nodeRouteHandler = handler;
        EnsureStarted();
    }

    // Registers the node-level transfer-control sink (S8-04A authority consumer).
    public void SetTransferControlHandler(Action<ActorTransferControl> handler)
    {
        _transferControlHandler = handler;
        EnsureStarted();
    }

    private MeshReadyDomains OnReady(MeshReadyDomains readyDomains)
    {
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine($"[pump] ready mask={readyDomains}");
        _signal.Release();
        return readyDomains;
    }

    private async Task RunAsync(CancellationToken cancellationToken)
    {
        using var readyBatch = new MeshReadyBatch();
        using var receiveBatch = new MeshReceiveBatch();
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await _signal.WaitAsync(cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return;
            }

            DrainResidue(readyBatch, receiveBatch);
            if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                Console.Error.WriteLine("[pump] idle");
        }
    }

    private void DrainResidue(MeshReadyBatch readyBatch, MeshReceiveBatch receiveBatch)
    {
        var more = true;
        while (more)
        {
            readyBatch.Reset();
            bool residue;
            try
            {
                // Non-blocking: the native drain/claim receives block indefinitely
                // by default, which would park this pump thread inside one claim
                // and starve every other owner. The signal semaphore provides the
                // wakeups; the pump itself must never wait inside the native API.
                residue = _node.DrainReady(
                    MeshReadyDomains.All, readyBatch, RecvFlags.DontWait);
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkException ex)
            {
                if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                    Console.Error.WriteLine($"[pump] drain error: {ex.Message}");
                return;
            }

            if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                Console.Error.WriteLine(
                    $"[pump] drained ready={readyBatch.Count} residue={residue} tid={Environment.CurrentManagedThreadId}");
            for (var i = 0; i < readyBatch.Count; i++)
                DrainClaim(readyBatch, i, receiveBatch);

            more = residue;
        }
    }

    private void DrainClaim(MeshReadyBatch readyBatch, int index, MeshReceiveBatch receiveBatch)
    {
        var debug = Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1";
        // The claim owner identifies the local consumer the records belong to.
        // Spot owners carry the hosting spot's rid directly; actor owners carry
        // only the actor identity (core leaves their spot_rid empty), so the
        // hosting spot is resolved through the node's actor table. Receive
        // records key per-spot dispatch by this owner rid: their own
        // SourceSpotRid is the remote sender's spot (or empty for
        // session-relayed actor sends), so it cannot address the local consumer.
        var readyRecord = readyBatch[index];
        var ownerSpotRid = readyRecord.SpotRid;
        if (ownerSpotRid.IsEmpty
            && readyRecord.OwnerKind == MeshOwnerKind.Actor
            && readyRecord.Actor.ActorId is { Length: > 0 } ownerActorId)
            try
            {
                if (_node.ActorLookup(ownerActorId, out var ownerLocation))
                    ownerSpotRid = ownerLocation.SpotRid;
            }
            catch (ZlinkException)
            {
            }
        MeshClaim claim;
        try
        {
            claim = readyBatch.TakeClaim(index);
        }
        catch (ZlinkException ex)
        {
            if (debug) Console.Error.WriteLine($"[pump] take-claim error: {ex.Message}");
            return;
        }

        try
        {
            while (true)
            {
                receiveBatch.Reset();
                if (debug) Console.Error.WriteLine("[pump] claim recv…");
                var got = claim.Receive(receiveBatch, RecvFlags.DontWait);
                if (debug) Console.Error.WriteLine($"[pump] claim recv={got} count={(got ? receiveBatch.Count : 0)}");
                if (!got)
                    return;

                var count = receiveBatch.Count;
                for (var record = 0; record < count; record++)
                    DispatchRecord(receiveBatch, record, ownerSpotRid, readyRecord.Actor);
            }
        }
        catch (Exception ex)
        {
            // A failed pull or a poison record must not kill the pump loop: the
            // pump is the node's only dispatch thread, so surviving and moving to
            // the next claim keeps every other owner (and the completion table)
            // alive.
            if (debug) Console.Error.WriteLine($"[pump] claim error: {ex}");
        }
        finally
        {
            claim.Dispose();
        }
    }

    private void DispatchRecord(
        MeshReceiveBatch batch, int index, RoutingId ownerSpotRid, ActorRef ownerActor)
    {
        var record = batch[index];
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine(
                $"[pump] record kind={record.Kind} op={record.OperationKind} tid={Environment.CurrentManagedThreadId}");
        switch (record.Kind)
        {
            case MeshRecordKind.Completion:
                ResolveCompletion(batch, index, record);
                return;
            case MeshRecordKind.NodeSend:
            case MeshRecordKind.NodeRequest:
            case MeshRecordKind.ChannelSend:
            case MeshRecordKind.ChannelRequest:
                EnqueueNodeRoute(batch, index, record);
                return;
            case MeshRecordKind.SpotSend:
            case MeshRecordKind.SpotRequest:
                EnqueueRoute(batch, index, record, ownerSpotRid);
                return;
            case MeshRecordKind.SpotMulticast:
                EnqueueSubscribe(batch, index, record, ownerSpotRid);
                return;
            case MeshRecordKind.SpotControl:
                EnqueueSpotControl(batch, index, record, ownerSpotRid);
                return;
            case MeshRecordKind.ActorSend:
            case MeshRecordKind.ActorRequest:
                EnqueueActor(batch, index, record, ownerSpotRid, ownerActor);
                return;
            case MeshRecordKind.SendReady:
                RaiseSendReady(record);
                return;
            case MeshRecordKind.TransferControl:
                // Deliver the transfer-control phase to the registered framework
                // sink so it drives the transfer state machine. The orchestrating
                // authority (S8-04A) registers the consumer.
                if (record.TransferControl is { } control)
                    _transferControlHandler?.Invoke(control);
                return;
        }
    }

    private void ResolveCompletion(MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine(
                $"[pump] completion op={record.OperationId} kind={record.OperationKind} terminal={record.TerminalResult}");
        var parts = record.PartCount > 0
            ? batch.RetainMessage(index)
            : Array.Empty<Message>();
        _completions.Complete(record, parts);
    }

    private void EnqueueRoute(MeshReceiveBatch batch, int index, MeshReceiveRecord record, RoutingId ownerSpotRid)
    {
        // Malformed application metadata is a protocol error: reject the ingress
        // and do not deliver it to a handler (spec 03 §3). The batch reset
        // releases the Core-owned parts we never retained.
        if (!TryDecodeMetadata(record, out var metadata))
            return;

        var state = ResolveSpotState(
            ownerSpotRid.IsEmpty ? record.SourceSpotRid : ownerSpotRid,
            targetOwner: true, record);
        var replyRecord = record;
        var reply = record.Kind is MeshRecordKind.NodeRequest
            or MeshRecordKind.ChannelRequest or MeshRecordKind.SpotRequest
            ? new Func<IReadOnlyList<Message>, SendFlags, SubmitResult>(
                (parts, flags) => replyRecord.Reply(parts, flags))
            : null;
        var route = new ZLinkBackendRouteReceived(
            RetainParts(batch, index),
            record.SourceNodeRid,
            record.SourceSpotRid,
            record.OperationId == default ? null : record.OperationId.Low,
            reply,
            metadata: metadata);
        state.Routes.Enqueue(route);
        state.Raise(ZLinkBackendSpotDispatchEvent.RouteReadable);
    }

    // Node/channel-addressed records (owned by the node). Requests carry the reply
    // token exactly like the per-spot route plane; channel records also carry the
    // addressed channel name so the node dispatcher can select the channel
    // membership's handler set. Delivered to the node-level route consumer; if none
    // is registered (no MeshNode route/channel handlers), the retained parts are
    // released so a dropped record cannot leak.
    private void EnqueueNodeRoute(MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        // Malformed application metadata is a protocol error: reject the ingress
        // (spec 03 §3). No parts are retained before this point.
        if (!TryDecodeMetadata(record, out var metadata))
            return;

        var replyRecord = record;
        var reply = record.Kind is MeshRecordKind.NodeRequest or MeshRecordKind.ChannelRequest
            ? new Func<IReadOnlyList<Message>, SendFlags, SubmitResult>(
                (parts, flags) => replyRecord.Reply(parts, flags))
            : null;
        var received = new ZLinkBackendRouteReceived(
            RetainParts(batch, index),
            record.SourceNodeRid,
            record.SourceSpotRid,
            record.OperationId == default ? null : record.OperationId.Low,
            reply,
            record.Kind is MeshRecordKind.ChannelSend or MeshRecordKind.ChannelRequest
                ? record.ChannelName
                : null,
            metadata);
        var handler = _nodeRouteHandler;
        if (handler is null)
        {
            received.Dispose();
            return;
        }

        handler(received);
    }

    private void EnqueueSubscribe(MeshReceiveBatch batch, int index, MeshReceiveRecord record, RoutingId ownerSpotRid)
    {
        // Malformed application metadata is a protocol error: reject the ingress
        // (spec 03 §3). The same publish snapshot is delivered to every matching
        // Spot handler, so the decoded view is immutable and shared.
        if (!TryDecodeMetadata(record, out var metadata))
            return;

        var state = ResolveSpotState(
            ownerSpotRid.IsEmpty ? record.SourceSpotRid : ownerSpotRid,
            targetOwner: true, record);
        var message = new ZLinkBackendSubscribeMessage(
            record.Topic ?? string.Empty, RetainParts(batch, index), metadata);
        state.Subscriptions.Enqueue(message);
        state.Raise(ZLinkBackendSpotDispatchEvent.SubscribeReadable);
    }

    // Decodes the record's application-metadata frame into an immutable snapshot.
    // Returns false only when the frame is present but malformed, so callers
    // drop the record as a protocol error rather than deliver it.
    private static bool TryDecodeMetadata(
        MeshReceiveRecord record, out ZLinkMessageMetadata metadata)
    {
        var frame = record.ApplicationMetadata;
        if (frame is null || frame.Length == 0)
        {
            metadata = ZLinkMessageMetadata.Empty;
            return true;
        }

        return ZLinkMeshMetadataCodec.TryDecode(frame, out metadata);
    }

    private void EnqueueSpotControl(MeshReceiveBatch batch, int index, MeshReceiveRecord record, RoutingId ownerSpotRid)
    {
        var state = ResolveSpotState(
            ownerSpotRid.IsEmpty ? record.SourceSpotRid : ownerSpotRid,
            targetOwner: true, record);
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine(
                $"[pump] spot-control owner={ownerSpotRid} src={record.SourceSpotRid} op={record.OperationKind} handler={state.DispatchHandler is not null}");
        if (record.OperationKind == MeshOperationKind.ActorJoin)
        {
            // Actor-join admission record: build a framework join request.
            var join = ZLinkMeshRecordAdapters.ToActorJoinRequest(batch, index, record);
            state.ActorJoins.Enqueue(join);
            state.Raise(ZLinkBackendSpotDispatchEvent.ActorJoinReadable);
            return;
        }

        if (record.ActorControl is { } control)
        {
            var lifecycle = ZLinkMeshRecordAdapters.ToLifecycleEvent(control);
            if (lifecycle is { } value)
            {
                state.Lifecycles.Enqueue(value);
                state.Raise(ZLinkBackendSpotDispatchEvent.ActorLifecycleReadable);
            }
        }
    }

    private void EnqueueActor(
        MeshReceiveBatch batch, int index, MeshReceiveRecord record,
        RoutingId ownerSpotRid, ActorRef ownerActor)
    {
        ulong requestId = 0;
        if (record.Kind == MeshRecordKind.ActorRequest)
        {
            requestId = (ulong)Interlocked.Increment(ref _nextActorRequestId);
            if (_actorReplies.Count < MaxPendingActorReplies)
            {
                var replyRecord = record;
                _actorReplies[requestId] =
                    (parts, flags) => replyRecord.Reply(parts, flags);
            }
        }


        var state = ResolveSpotState(
            ownerSpotRid.IsEmpty ? record.SourceSpotRid : ownerSpotRid,
            targetOwner: true, record);
        var parts = ZLinkMeshRecordAdapters.ToActorParts(batch, index, record, ownerActor, requestId);
        if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
            Console.Error.WriteLine(
                $"[pump] actor owner={ownerSpotRid} src={record.SourceSpotRid} parts={parts.Count} handler={state.DispatchHandler is not null} keys={string.Join(",", _spots.Keys)}");
        if (parts.Count == 0) return;
        state.RaiseActor(parts);
    }

    private void RaiseSendReady(MeshReceiveRecord record)
    {
        if (record.SendReady is not { } ready) return;
        var state = _spots.GetValueOrDefault(ready.TargetSpotRid);
        state?.RaiseSendReady();
    }

    private SpotDispatchState ResolveSpotState(
        RoutingId spotRid, bool targetOwner, MeshReceiveRecord record)
    {
        return _spots.GetOrAdd(spotRid, static _ => new SpotDispatchState());
    }

    private static IReadOnlyList<Message> RetainParts(MeshReceiveBatch batch, int index)
    {
        return batch.RetainMessage(index);
    }

    public async ValueTask DisposeAsync()
    {
        Task? loop;
        lock (_lifecycleGate)
        {
            if (_disposed) return;
            _disposed = true;
            _stop?.Cancel();
            loop = _loop;
        }

        if (loop is not null)
            try
            {
                await loop.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }

        _completions.FailAll(RequestResult.Terminated);
        _stop?.Dispose();
        _signal.Dispose();
    }

    // Per-spot decoded-record queues plus the registered dispatch-event handler.
    internal sealed class SpotDispatchState
    {
        public Action<ZLinkBackendSpotDispatchInfo>? DispatchHandler { get; set; }

        public Action? SendReadyHandler { get; set; }

        public ConcurrentQueue<ZLinkBackendRouteReceived> Routes { get; } = new();

        public ConcurrentQueue<ZLinkBackendSubscribeMessage> Subscriptions { get; } = new();

        public ConcurrentQueue<ZLinkBackendActorJoinRequest> ActorJoins { get; } = new();

        public ConcurrentQueue<ZLinkBackendSpotActorLifecycleEvent> Lifecycles { get; } = new();

        public void Raise(ZLinkBackendSpotDispatchEvent kind)
        {
            DispatchHandler?.Invoke(new ZLinkBackendSpotDispatchInfo(kind));
        }

        public void RaiseActor(IReadOnlyList<ZLinkBackendActorPart> parts)
        {
            DispatchHandler?.Invoke(new ZLinkBackendSpotDispatchInfo(
                ZLinkBackendSpotDispatchEvent.ActorReadable, ActorParts: parts));
        }

        public void RaiseSendReady()
        {
            SendReadyHandler?.Invoke();
        }
    }
}
