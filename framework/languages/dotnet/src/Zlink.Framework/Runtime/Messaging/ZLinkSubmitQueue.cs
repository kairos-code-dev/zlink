namespace Zlink.Framework.Runtime.Messaging;

internal sealed class ZLinkSubmitQueue
{
    private readonly object _gate = new();
    private readonly Queue<PendingSubmit> _pending = new();
    private readonly int _capacity;
    private bool _disposed;

    public ZLinkSubmitQueue(int capacity)
    {
        _capacity = capacity > 0
            ? capacity
            : throw new ArgumentOutOfRangeException(nameof(capacity));
    }

    public void Enqueue(PendingSubmit pending)
    {
        lock (_gate)
        {
            ThrowIfDisposed();
            if (_pending.Count >= _capacity)
            {
                throw new InvalidOperationException("ZLink async submit queue is full.");
            }

            _pending.Enqueue(pending);
        }
    }

    public bool TryPeek(out PendingSubmit? pending)
    {
        lock (_gate)
        {
            pending = _pending.Count > 0 ? _pending.Peek() : null;
            return pending is not null;
        }
    }

    public bool TryDequeue(
        PendingSubmit expected,
        out PendingSubmit? pending)
    {
        lock (_gate)
        {
            if (_pending.Count > 0 && ReferenceEquals(_pending.Peek(), expected))
            {
                pending = _pending.Dequeue();
                return true;
            }
        }

        pending = null;
        return false;
    }

    public IReadOnlyList<PendingSubmit> DisposeAll()
    {
        PendingSubmit[] remaining;
        lock (_gate)
        {
            if (_disposed)
            {
                return Array.Empty<PendingSubmit>();
            }

            _disposed = true;
            remaining = _pending.ToArray();
            _pending.Clear();
        }

        return remaining;
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(ZLinkAsyncSubmitter));
        }
    }
}
