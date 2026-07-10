using System.Collections.Concurrent;

namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed class ZlinkStreamPendingRequests
{
    private readonly ConcurrentDictionary<ulong, PendingRequest> _pending = new();
    private long _nextRequestSeq;

    public PendingRequest Create()
    {
        var requestSeq = NextRequestSeq();
        var pending = new PendingRequest(requestSeq);
        if (!_pending.TryAdd(requestSeq.Value, pending))
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                "Duplicate request sequence.");

        return pending;
    }

    public bool TryComplete(
        ZlinkStreamHeader header,
        ZlinkStreamFrame frame,
        Func<ReadOnlyMemory<byte>, ZlinkStreamError> parseError)
    {
        if (header.RequestSeq is not { } requestSeq
            || (header.Kind != ZlinkStreamMessageKind.Response && header.Kind != ZlinkStreamMessageKind.Error)
            || !_pending.TryRemove(requestSeq.Value, out var pending))
            return false;

        if (header.Kind == ZlinkStreamMessageKind.Error)
        {
            pending.Fail(parseError(frame.Payload));
            return true;
        }

        pending.Complete(frame);
        return true;
    }

    public ValueTask<ZlinkStreamFrame> WaitAsync(
        PendingRequest pending,
        CancellationToken cancellationToken)
    {
        return new ValueTask<ZlinkStreamFrame>(pending.Task.WaitAsync(cancellationToken));
    }

    public void Remove(ZlinkStreamRequestSeq requestSeq)
    {
        _pending.TryRemove(requestSeq.Value, out _);
    }

    public void FailAll(ZlinkStreamError error)
    {
        foreach (var (requestSeq, pending) in _pending)
            if (_pending.TryRemove(requestSeq, out _))
                pending.Fail(error);
    }

    private ZlinkStreamRequestSeq NextRequestSeq()
    {
        while (true)
        {
            var value = unchecked((ulong)Interlocked.Increment(ref _nextRequestSeq));
            if (value != 0) return new ZlinkStreamRequestSeq(value);
        }
    }

    internal sealed class PendingRequest(ZlinkStreamRequestSeq requestSeq)
    {
        private readonly TaskCompletionSource<ZlinkStreamFrame> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ZlinkStreamRequestSeq RequestSeq { get; } = requestSeq;

        public Task<ZlinkStreamFrame> Task => _completion.Task;

        public void Complete(ZlinkStreamFrame frame)
        {
            _completion.TrySetResult(frame);
        }

        public void Fail(ZlinkStreamError error)
        {
            _completion.TrySetException(new ZlinkStreamException(error));
        }
    }
}
