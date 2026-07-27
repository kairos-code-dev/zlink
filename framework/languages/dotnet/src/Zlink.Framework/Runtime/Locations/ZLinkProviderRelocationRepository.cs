namespace Zlink.Framework.Runtime.Locations;

internal sealed class ZLinkProviderRelocationRepository(
    IZLinkRelocationStore provider) : IZLinkRelocationRepository
{
    public async ValueTask<ZLinkRelocationStored> PutRelocationAsync(
        ReadOnlyMemory<byte> payload,
        TimeSpan retention,
        CancellationToken cancellationToken = default)
    {
        var reference = new ZLinkBlobReference(
            Guid.NewGuid().ToString("N"));
        var result = await provider.PutAsync(
                reference,
                payload,
                retention,
                cancellationToken)
            .ConfigureAwait(false);
        var (expiresAt, storeNow) = result switch
        {
            ZLinkBlobPutResult.Stored stored =>
                (stored.ExpiresAt, stored.StoreNow),
            ZLinkBlobPutResult.AlreadyStored stored =>
                (stored.ExpiresAt, stored.StoreNow),
            ZLinkBlobPutResult.Conflict =>
                throw new InvalidDataException(
                    "A Framework-issued relocation reference collided."),
            _ => throw new InvalidOperationException()
        };
        return new ZLinkRelocationStored(
            reference.Value,
            ComputeCrc32C(payload.Span),
            expiresAt,
            storeNow);
    }

    public async ValueTask<ZLinkRelocationReadResult> GetRelocationAsync(
        string reference,
        CancellationToken cancellationToken = default)
    {
        var result = await provider.ReadAsync(
                new ZLinkBlobReference(reference),
                cancellationToken)
            .ConfigureAwait(false);
        return result switch
        {
            ZLinkBlobReadResult.Found found =>
                new ZLinkRelocationReadResult.Found(found.Bytes),
            ZLinkBlobReadResult.Missing =>
                new ZLinkRelocationReadResult.Missing(),
            _ => throw new InvalidOperationException()
        };
    }

    public async ValueTask<ZLinkRelocationRenewResult> RenewRelocationAsync(
        string reference,
        TimeSpan retention,
        CancellationToken cancellationToken = default)
    {
        var result = await provider.RenewAsync(
                new ZLinkBlobReference(reference),
                retention,
                cancellationToken)
            .ConfigureAwait(false);
        return result switch
        {
            ZLinkBlobRenewResult.Renewed renewed =>
                new ZLinkRelocationRenewResult.Renewed(
                    renewed.ExpiresAt,
                    renewed.StoreNow),
            ZLinkBlobRenewResult.Missing =>
                new ZLinkRelocationRenewResult.Missing(),
            _ => throw new InvalidOperationException()
        };
    }

    public async ValueTask<ZLinkRelocationDeleteResult> DeleteRelocationAsync(
        string reference,
        CancellationToken cancellationToken = default)
    {
        await provider.DeleteAsync(
                new ZLinkBlobReference(reference),
                cancellationToken)
            .ConfigureAwait(false);
        return ZLinkRelocationDeleteResult.Deleted;
    }

    private static uint ComputeCrc32C(ReadOnlySpan<byte> payload)
    {
        var crc = uint.MaxValue;
        foreach (var value in payload)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc >> 1)
                      ^ (0x82f63b78U & (uint)-(int)(crc & 1));
            }
        }
        return ~crc;
    }
}
