namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkCommittedSpotForwarding(
    RoutingId targetNodeRid,
    ulong objectGeneration,
    ulong sourceNodeGeneration,
    ulong targetNodeGeneration,
    ulong sourceAuthorityOwnerGeneration,
    ulong targetAuthorityOwnerGeneration,
    ZLinkLocationOwnerToken sourceOwner,
    ZLinkLocationOwnerToken targetOwner,
    DateTimeOffset expiresAt)
{
    private const int MessageCapacity = 1_024;
    private const long ByteCapacity = 16L * 1024 * 1024;
    private readonly object _gate = new();
    private int _messages;
    private long _bytes;

    internal RoutingId TargetNodeRid { get; } = targetNodeRid;
    internal ulong ObjectGeneration { get; } = objectGeneration;
    internal ulong SourceNodeGeneration { get; } = sourceNodeGeneration;
    internal ulong TargetNodeGeneration { get; } = targetNodeGeneration;
    internal ulong SourceAuthorityOwnerGeneration { get; } =
        sourceAuthorityOwnerGeneration;
    internal ulong TargetAuthorityOwnerGeneration { get; } =
        targetAuthorityOwnerGeneration;
    internal ZLinkLocationOwnerToken SourceOwner { get; } = sourceOwner;
    internal ZLinkLocationOwnerToken TargetOwner { get; } = targetOwner;
    internal DateTimeOffset ExpiresAt { get; } = expiresAt;

    internal bool TryAcquire(long bytes)
    {
        if (bytes < 0)
            return false;
        lock (_gate)
        {
            if (_messages >= MessageCapacity
                || bytes > ByteCapacity - _bytes)
                return false;
            _messages++;
            _bytes += bytes;
            return true;
        }
    }

    internal void Release(long bytes)
    {
        lock (_gate)
        {
            _messages--;
            _bytes -= bytes;
            if (_messages < 0 || _bytes < 0)
                throw new InvalidOperationException(
                    "SPOT forwarding admission became negative.");
        }
    }
}
