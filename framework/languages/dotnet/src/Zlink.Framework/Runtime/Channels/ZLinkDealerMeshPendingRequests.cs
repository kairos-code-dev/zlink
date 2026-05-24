using System.Collections.Concurrent;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkDealerMeshPendingRequests
{
    private readonly ConcurrentDictionary<ulong, Action<IReadOnlyList<Message>>> _pending = new();
    private long _nextSeq;

    public ulong NextRequestSeq()
    {
        while (true)
        {
            var next = (ulong)Interlocked.Increment(ref _nextSeq);
            if (next != 0)
            {
                return next;
            }
        }
    }

    public bool TryRegister(
        ulong requestSeq,
        Action<IReadOnlyList<Message>> complete)
    {
        ArgumentNullException.ThrowIfNull(complete);
        return _pending.TryAdd(requestSeq, complete);
    }

    public void Remove(ulong requestSeq)
    {
        _pending.TryRemove(requestSeq, out _);
    }

    public bool TryComplete(
        ulong requestSeq,
        IReadOnlyList<Message> replyParts)
    {
        if (!_pending.TryRemove(requestSeq, out var complete))
        {
            return false;
        }

        complete(replyParts);
        return true;
    }
}
