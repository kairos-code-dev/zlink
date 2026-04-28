using System.Collections.Concurrent;

namespace Systems.Zlink.Stream.Connector.Connector;

internal sealed class ZlinkStreamPendingRequests
{
    private readonly ConcurrentDictionary<ulong, PendingRequest> _pending = new();
    private long _nextRequestId;

    public PendingRequest Create(string packetName)
    {
        var requestId = NextRequestId();
        var pending = new PendingRequest(requestId, packetName);
        if (!_pending.TryAdd(requestId.Value, pending))
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.ValidationFailed,
                "Duplicate request id.");
        }

        return pending;
    }

    public bool TryComplete(
        ZlinkStreamHeader header,
        ZlinkStreamPacket packet,
        Func<ReadOnlyMemory<byte>, ZlinkStreamError> parseError)
    {
        if (header.RequestId is not { } requestId
            || (header.Kind != ZlinkStreamMessageKind.Response && header.Kind != ZlinkStreamMessageKind.Error)
            || !_pending.TryRemove(requestId.Value, out var pending))
        {
            return false;
        }

        if (!string.Equals(pending.Name, header.Name, StringComparison.Ordinal))
        {
            pending.Fail(new ZlinkStreamError(
                ZlinkStreamErrorCode.FrameDecodeFailed,
                "Response packet name does not match the request packet name."));
            return true;
        }

        if (header.Kind == ZlinkStreamMessageKind.Error)
        {
            pending.Fail(parseError(packet.Body));
            return true;
        }

        pending.Complete(packet);
        return true;
    }

    public async ValueTask<ZlinkStreamPacket> WaitAsync(
        PendingRequest pending,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutCts.CancelAfter(timeout);

        try
        {
            return await pending.Task.WaitAsync(timeoutCts.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException ex) when (!cancellationToken.IsCancellationRequested)
        {
            _pending.TryRemove(pending.RequestId.Value, out _);
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.RequestTimeout,
                "Request timed out.",
                ex);
        }
    }

    public void Remove(ZlinkStreamRequestId requestId)
    {
        _pending.TryRemove(requestId.Value, out _);
    }

    public void FailAll(ZlinkStreamError error)
    {
        foreach (var (requestId, pending) in _pending)
        {
            if (_pending.TryRemove(requestId, out _))
            {
                pending.Fail(error);
            }
        }
    }

    private ZlinkStreamRequestId NextRequestId()
    {
        while (true)
        {
            var value = unchecked((ulong)Interlocked.Increment(ref _nextRequestId));
            if (value != 0)
            {
                return new ZlinkStreamRequestId(value);
            }
        }
    }

    internal sealed class PendingRequest(ZlinkStreamRequestId requestId, string name)
    {
        private readonly TaskCompletionSource<ZlinkStreamPacket> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ZlinkStreamRequestId RequestId { get; } = requestId;

        public string Name { get; } = name;

        public Task<ZlinkStreamPacket> Task => _completion.Task;

        public void Complete(ZlinkStreamPacket packet) => _completion.TrySetResult(packet);

        public void Fail(ZlinkStreamError error) => _completion.TrySetException(new ZlinkStreamException(error));
    }
}
