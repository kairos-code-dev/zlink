using System.Collections.Concurrent;
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

    private MeshReadyDomains OnReady(MeshReadyDomains readyDomains)
    {
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
                // Infrastructure plane first so control traffic drains ahead of app.
                residue = _node.DrainReady(MeshReadyDomains.All, readyBatch);
            }
            catch (ObjectDisposedException)
            {
                return;
            }
            catch (ZlinkException)
            {
                return;
            }

            for (var i = 0; i < readyBatch.Count; i++)
                DrainClaim(readyBatch, i, receiveBatch);

            more = residue;
        }
    }

    private void DrainClaim(MeshReadyBatch readyBatch, int index, MeshReceiveBatch receiveBatch)
    {
        MeshClaim claim;
        try
        {
            claim = readyBatch.TakeClaim(index);
        }
        catch (ZlinkException)
        {
            return;
        }

        try
        {
            while (true)
            {
                receiveBatch.Reset();
                if (!claim.Receive(receiveBatch))
                    return;

                var count = receiveBatch.Count;
                for (var record = 0; record < count; record++)
                    DispatchRecord(receiveBatch, record);
            }
        }
        catch (ZlinkException)
        {
        }
        finally
        {
            claim.Dispose();
        }
    }

    private void DispatchRecord(MeshReceiveBatch batch, int index)
    {
        var record = batch[index];
        switch (record.Kind)
        {
            case MeshRecordKind.Completion:
                ResolveCompletion(batch, index, record);
                return;
            case MeshRecordKind.NodeSend:
            case MeshRecordKind.NodeRequest:
            case MeshRecordKind.ChannelSend:
            case MeshRecordKind.ChannelRequest:
            case MeshRecordKind.SpotSend:
            case MeshRecordKind.SpotRequest:
                EnqueueRoute(batch, index, record);
                return;
            case MeshRecordKind.SpotMulticast:
                EnqueueSubscribe(batch, index, record);
                return;
            case MeshRecordKind.SpotControl:
                EnqueueSpotControl(batch, index, record);
                return;
            case MeshRecordKind.ActorSend:
            case MeshRecordKind.ActorRequest:
                EnqueueActor(batch, index, record);
                return;
            case MeshRecordKind.SendReady:
                RaiseSendReady(record);
                return;
            case MeshRecordKind.TransferControl:
                // Transfer-control drives the actor-transfer state machine
                // (S8-04A follow-up). No framework consumer yet at this seam.
                return;
        }
    }

    private void ResolveCompletion(MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        var parts = record.PartCount > 0
            ? batch.RetainMessage(index)
            : Array.Empty<Message>();
        _completions.Complete(record, parts);
    }

    private void EnqueueRoute(MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        var state = ResolveSpotState(record.SourceSpotRid, targetOwner: true, record);
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
            reply);
        state.Routes.Enqueue(route);
        state.Raise(ZLinkBackendSpotDispatchEvent.RouteReadable);
    }

    private void EnqueueSubscribe(MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        var state = ResolveSpotState(record.SourceSpotRid, targetOwner: true, record);
        var message = new ZLinkBackendSubscribeMessage(
            record.Topic ?? string.Empty, RetainParts(batch, index));
        state.Subscriptions.Enqueue(message);
        state.Raise(ZLinkBackendSpotDispatchEvent.SubscribeReadable);
    }

    private void EnqueueSpotControl(MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        var state = ResolveSpotState(record.SourceSpotRid, targetOwner: true, record);
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

    private void EnqueueActor(MeshReceiveBatch batch, int index, MeshReceiveRecord record)
    {
        var state = ResolveSpotState(record.SourceSpotRid, targetOwner: true, record);
        var parts = ZLinkMeshRecordAdapters.ToActorParts(batch, index, record);
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
