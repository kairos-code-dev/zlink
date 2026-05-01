using System.Collections.Concurrent;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionRequestTracker
{
    private readonly ConcurrentDictionary<ulong, ZLinkPendingSessionRequest> _pendingRequests = new();
    private long _nextRequestSeq;

    public ZLinkPendingSessionRequest Start()
    {
        var requestSeq = NextRequestSeq();
        var pending = new ZLinkPendingSessionRequest(this, requestSeq);
        if (!_pendingRequests.TryAdd(requestSeq.Value, pending))
        {
            throw new InvalidOperationException("Duplicate stream request sequence.");
        }

        return pending;
    }

    public bool TryCompleteResponse(
        ZlinkStreamHeader header,
        Message body)
    {
        if (header.Kind != ZlinkStreamMessageKind.Response
            || header.RequestSeq is not { } requestSeq
            || !_pendingRequests.TryRemove(requestSeq.Value, out var pending))
        {
            return false;
        }

        pending.Complete(body.Move());
        return true;
    }

    internal void Remove(ZlinkStreamRequestSeq requestSeq)
    {
        _pendingRequests.TryRemove(requestSeq.Value, out _);
    }

    private ZlinkStreamRequestSeq NextRequestSeq()
    {
        while (true)
        {
            var value = unchecked((ulong)Interlocked.Increment(ref _nextRequestSeq));
            if (value != 0)
            {
                return new ZlinkStreamRequestSeq(value);
            }
        }
    }
}

internal sealed class ZLinkPendingSessionRequest(
    ZLinkSessionRequestTracker tracker,
    ZlinkStreamRequestSeq requestSeq) : IDisposable
{
    private readonly TaskCompletionSource<Message> _completion =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public ZlinkStreamRequestSeq RequestSeq { get; } = requestSeq;

    public Task<Message> Task => _completion.Task;

    public void Complete(Message body)
    {
        _completion.TrySetResult(body);
    }

    public void Cancel()
    {
        _completion.TrySetException(new TimeoutException("Client stream request timed out."));
    }

    public void Dispose()
    {
        tracker.Remove(RequestSeq);
    }
}
