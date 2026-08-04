using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.Runtime.Service;

/// <summary>
/// Bounded mailbox for records owned by one node, Spot or Actor.
/// </summary>
internal sealed class ZLinkMeshNodeOwnedMailbox(
    Action<ulong> onRecordEnqueued,
    Action<ulong> onRecordDequeued)
{
    private readonly Queue<ZLinkMeshQueuedRecord> _records = new();
    private readonly object _gate = new();
    private ulong _pendingBytes;
    private bool _claimed;

    internal bool HasRecords
    {
        get
        {
            lock (_gate)
                return _records.Count != 0;
        }
    }

    internal int Count
    {
        get
        {
            lock (_gate)
                return _records.Count;
        }
    }

    internal bool IsClaimed
    {
        get
        {
            lock (_gate)
                return _claimed;
        }
    }

    internal bool TryEnqueue(
        ZLinkMeshQueuedRecord record,
        ulong messageBudget,
        ulong byteBudget)
    {
        lock (_gate)
        {
            if ((ulong)_records.Count >= messageBudget
                || record.PendingBytes > byteBudget - Math.Min(
                    _pendingBytes,
                    byteBudget))
                return false;
            _records.Enqueue(record);
            _pendingBytes = checked(_pendingBytes + record.PendingBytes);
            onRecordEnqueued(record.PendingBytes);
            return true;
        }
    }

    internal bool TryClaim(ZLinkInboundDispatchBudget? inboundDispatchBudget)
    {
        ZLinkMeshQueuedRecord? candidate;
        lock (_gate)
        {
            if (_claimed || _records.Count == 0)
                return false;
            _claimed = true;
            candidate = _records.Peek();
            if (!candidate.Record.RequiresApplicationDispatchLease
                || candidate.Record.InboundDispatchLease is not null
                || inboundDispatchBudget is null)
                return true;
        }

        if (!inboundDispatchBudget.TryTrack(
                candidate.PendingBytes,
                out var lease))
        {
            lock (_gate)
            {
                if (_records.Count != 0
                    && ReferenceEquals(_records.Peek(), candidate))
                    _claimed = false;
            }
            return false;
        }

        lock (_gate)
        {
            if (_records.Count == 0
                || !ReferenceEquals(_records.Peek(), candidate))
            {
                _claimed = false;
                lease!.Dispose();
                return false;
            }

            candidate.AttachLease(lease!);
            return true;
        }
    }

    internal bool TryDequeue(
        MeshReceiveBatch batch,
        out ZLinkMeshQueuedRecord record)
    {
        lock (_gate)
        {
            if (_records.Count == 0)
            {
                record = null!;
                return false;
            }
            var candidate = _records.Peek();
            if (!batch.CanAdd(checked((long)candidate.PendingBytes)))
            {
                record = null!;
                return false;
            }
            record = _records.Dequeue();
            _pendingBytes -= record.PendingBytes;
            onRecordDequeued(record.PendingBytes);
            return true;
        }
    }

    internal void Release()
    {
        lock (_gate)
            _claimed = false;
    }

    internal void Dispose()
    {
        lock (_gate)
        {
            while (_records.Count != 0)
            {
                var record = _records.Dequeue();
                onRecordDequeued(record.PendingBytes);
                record.Dispose();
            }
            _pendingBytes = 0;
            _claimed = false;
        }
    }
}

internal sealed class ZLinkMeshQueuedRecord(
    MeshReceiveRecord record,
    IReadOnlyList<Message> parts) : IDisposable
{
    private IReadOnlyList<Message>? _parts = parts;
    internal MeshReceiveRecord Record { get; private set; } = record;
    internal ulong PendingBytes =>
        checked((ulong)(_parts?.Sum(static part => part.Size) ?? 0));

    internal IReadOnlyList<Message> TakeParts() =>
        Interlocked.Exchange(ref _parts, null) ?? Array.Empty<Message>();

    internal void AttachLease(ZLinkInboundDispatchLease lease)
    {
        var current = Record;
        current.InboundDispatchLease = lease;
        Record = current;
    }

    public void Dispose()
    {
        var owned = Interlocked.Exchange(ref _parts, null);
        if (owned is not null)
            foreach (var part in owned)
                part.Dispose();
        Record.InboundDispatchLease?.Dispose();
    }
}
