namespace Zlink.Framework.Runtime;

/// <summary>
/// Atomically admits records against both count and encoded-byte limits.
/// </summary>
internal sealed class ZLinkBoundedIngressAdmission
{
    internal const int SourceIngressHoldRecordCapacity = 1_024;
    internal const long SourceIngressHoldByteCapacity = 16L * 1024 * 1024;

    private readonly object _gate = new();
    private readonly int _recordCapacity;
    private readonly long _byteCapacity;
    private int _records;
    private long _bytes;

    internal ZLinkBoundedIngressAdmission(
        int recordCapacity = SourceIngressHoldRecordCapacity,
        long byteCapacity = SourceIngressHoldByteCapacity)
    {
        if (recordCapacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(recordCapacity));
        if (byteCapacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(byteCapacity));
        _recordCapacity = recordCapacity;
        _byteCapacity = byteCapacity;
    }

    internal bool TryAcquire(long encodedBytes)
    {
        if (encodedBytes < 0)
            return false;
        lock (_gate)
        {
            if (_records >= _recordCapacity
                || encodedBytes > _byteCapacity - _bytes)
                return false;
            _records++;
            _bytes += encodedBytes;
            return true;
        }
    }

    internal void Release(long encodedBytes)
    {
        if (encodedBytes < 0)
            throw new ArgumentOutOfRangeException(nameof(encodedBytes));
        lock (_gate)
        {
            if (_records == 0 || encodedBytes > _bytes)
                throw new InvalidOperationException(
                    "Ingress admission release does not match an acquired record.");
            _records--;
            _bytes -= encodedBytes;
        }
    }

    internal void ReleaseAll()
    {
        lock (_gate)
        {
            _records = 0;
            _bytes = 0;
        }
    }

    internal (int Records, long Bytes) Snapshot()
    {
        lock (_gate) return (_records, _bytes);
    }

    internal long RemainingByteCapacity
    {
        get
        {
            lock (_gate) return _byteCapacity - _bytes;
        }
    }

    internal int RemainingRecordCapacity
    {
        get
        {
            lock (_gate) return _recordCapacity - _records;
        }
    }
}
