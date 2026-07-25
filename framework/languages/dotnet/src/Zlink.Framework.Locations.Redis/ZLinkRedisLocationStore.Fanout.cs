using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

public sealed partial class ZLinkRedisLocationStore
{
    public ValueTask<ZLinkLocationWriteResult> UpdateFanoutPublisherAsync(
        ZLinkFanoutPublisherDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        ValidateFanoutDescriptor(descriptor);
        return ExecuteAsync(
            database => _commands.WriteFanoutAsync(
                database,
                descriptor,
                intent),
            cancellationToken);
    }

    public ValueTask<ZLinkLocationWriteStatus> RemoveFanoutPublisherAsync(
        ZLinkFanoutPublisherDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key.ChannelName);
        if (key.PublisherRid.IsEmpty)
            throw new ArgumentOutOfRangeException(nameof(key));
        return ExecuteAsync(
            async database =>
            {
                var result = await _commands.RemoveFanoutAsync(
                        database,
                        ZLinkRedisLocationKeyCodec.EncodeFanoutKey(key),
                        key.ChannelName,
                        owner)
                    .ConfigureAwait(false);
                return result.Status;
            },
            cancellationToken);
    }

    public ValueTask<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>>
        ListFanoutPublishersAsync(
            string channelName,
            ZLinkPageRequest page,
            CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(channelName);
        var pageSize = page.PageSize <= 0 ? 100 : page.PageSize;
        if (pageSize > 1000)
            throw new ArgumentOutOfRangeException(nameof(page));
        var offset = ParseFanoutContinuationToken(page.ContinuationToken);

        return ExecuteAsync(
            async database =>
            {
                var members = await database.SortedSetRangeByRankAsync(
                        _keys.FanoutChannelIndexKey(channelName),
                        offset,
                        checked(offset + pageSize))
                    .ConfigureAwait(false);
                var rows = new List<ZLinkFanoutPublisherDescriptor>(
                    Math.Min(pageSize, members.Length));
                var encodedBytes = 0;
                var consumedMembers = 0;
                foreach (var member in members)
                {
                    if (rows.Count == pageSize)
                        break;

                    var fields = await database.HashGetAsync(
                            _keys.RowHashKey(
                                ZLinkRedisLocationKinds.Fanout.Tag,
                                (string)member!),
                            ZLinkRedisLocationRows.Fields)
                        .ConfigureAwait(false);
                    if (fields[0].IsNull)
                    {
                        consumedMembers++;
                        continue;
                    }

                    var rowBytes = System.Text.Encoding.UTF8.GetByteCount(
                        (string)fields[0]!);
                    if (rows.Count != 0
                        && encodedBytes + rowBytes > 4 * 1024 * 1024)
                    {
                        break;
                    }

                    if (ZLinkRedisLocationRows.Materialize(
                            ZLinkRedisLocationKinds.Fanout,
                            fields) is { } row)
                    {
                        rows.Add(row);
                        encodedBytes += rowBytes;
                    }
                    consumedMembers++;
                }

                var next = consumedMembers < members.Length
                           || members.Length > pageSize
                    ? checked(offset + consumedMembers).ToString(
                        System.Globalization.CultureInfo.InvariantCulture)
                    : null;
                return new ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>(
                    rows,
                    next);
            },
            cancellationToken);
    }

    private static long ParseFanoutContinuationToken(string? token)
    {
        if (token is null)
            return 0;
        if (long.TryParse(
                token,
                System.Globalization.NumberStyles.None,
                System.Globalization.CultureInfo.InvariantCulture,
                out var offset)
            && offset >= 0)
        {
            return offset;
        }

        throw new ArgumentException(
            "The Fanout continuation token is invalid.",
            nameof(token));
    }

    private static void ValidateFanoutDescriptor(
        ZLinkFanoutPublisherDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.ChannelName);
        if (descriptor.PublisherRid.IsEmpty)
            throw new ArgumentOutOfRangeException(nameof(descriptor));
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.Endpoint);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.SecurityIdentity);
        ArgumentException.ThrowIfNullOrWhiteSpace(descriptor.OwnerId);
        if (descriptor.LifecycleGeneration == 0
            || descriptor.DescriptorRevision == 0
            || descriptor.LeaseGeneration <= 0
            || !Enum.IsDefined(descriptor.State))
            throw new ArgumentOutOfRangeException(nameof(descriptor));
    }
}
