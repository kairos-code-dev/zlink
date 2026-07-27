using System.Collections.Concurrent;
using System.Security.Cryptography;
using Zlink.Framework.LocationProvider;
using SpotActorTransfer.Shared;

namespace SpotActorTransfer.ActorNode;

internal sealed class RelocationBlobObserver
{
    private readonly ConcurrentQueue<RelocationBlobMeasurement> _measurements = new();

    public void Record(
        string operation,
        ZLinkBlobReference reference,
        ReadOnlyMemory<byte> payload)
    {
        _measurements.Enqueue(new RelocationBlobMeasurement(
            operation,
            payload.Length,
            Convert.ToHexString(SHA256.HashData(payload.Span))
                .ToLowerInvariant(),
            Convert.ToHexString(
                    SHA256.HashData(
                        System.Text.Encoding.UTF8.GetBytes(reference.Value)))
                .ToLowerInvariant()));
    }

    public RelocationBlobMeasurement[] Snapshot() => _measurements.ToArray();

    public void Reset()
    {
        while (_measurements.TryDequeue(out _))
        {
        }
    }
}

/// <summary>
/// Records only opaque blob size and checksum. It does not parse Framework
/// references, envelopes, or provider keys.
/// </summary>
internal sealed class ObservedRelocationStore(
    IZLinkRelocationStore inner,
    RelocationBlobObserver observer) :
    IZLinkRelocationStore,
    IAsyncDisposable
{
    public async ValueTask<ZLinkBlobPutResult> PutAsync(
        ZLinkBlobReference reference,
        ReadOnlyMemory<byte> payload,
        TimeSpan retention,
        CancellationToken cancellationToken = default)
    {
        observer.Record("put", reference, payload);
        return await inner.PutAsync(
                reference,
                payload,
                retention,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkBlobReadResult> ReadAsync(
        ZLinkBlobReference reference,
        CancellationToken cancellationToken = default)
    {
        var result = await inner.ReadAsync(reference, cancellationToken)
            .ConfigureAwait(false);
        if (result is ZLinkBlobReadResult.Found found)
            observer.Record("read", reference, found.Bytes);
        return result;
    }

    public ValueTask<ZLinkBlobRenewResult> RenewAsync(
        ZLinkBlobReference reference,
        TimeSpan retention,
        CancellationToken cancellationToken = default) =>
        inner.RenewAsync(reference, retention, cancellationToken);

    public ValueTask DeleteAsync(
        ZLinkBlobReference reference,
        CancellationToken cancellationToken = default) =>
        inner.DeleteAsync(reference, cancellationToken);

    public async ValueTask DisposeAsync()
    {
        switch (inner)
        {
            case IAsyncDisposable asyncDisposable:
                await asyncDisposable.DisposeAsync().ConfigureAwait(false);
                break;
            case IDisposable disposable:
                disposable.Dispose();
                break;
        }
    }
}
