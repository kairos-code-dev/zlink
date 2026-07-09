using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Reads Redis row hashes and rebuilds public location rows. Store-issued
/// generation and UpdatedAt fields always replace the serialized JSON fields.
/// </summary>
internal static class ZLinkRedisLocationRows
{
    internal static readonly RedisValue[] Fields = ["json", "gen", "updatedAtMs"];

    internal static async Task<List<TRow>> LoadAsync<TRow>(
        IDatabase database,
        ZLinkRedisLocationKeys keys,
        ZLinkRedisLocationKind<TRow> kind,
        RedisValue[] rowKeys)
        where TRow : class
    {
        if (rowKeys.Length == 0)
        {
            return [];
        }

        var batch = database.CreateBatch();
        var reads = new Task<RedisValue[]>[rowKeys.Length];
        for (var i = 0; i < rowKeys.Length; i++)
        {
            reads[i] = batch.HashGetAsync(keys.RowHashKey(kind.Tag, (string)rowKeys[i]!), Fields);
        }

        batch.Execute();
        await Task.WhenAll(reads).ConfigureAwait(false);

        var rows = new List<TRow>(rowKeys.Length);
        foreach (var read in reads)
        {
            // A row listed by the index may have been removed concurrently;
            // skipping it matches a snapshot taken a moment later.
            if (Materialize(kind, read.Result) is { } row)
            {
                rows.Add(row);
            }
        }

        return rows;
    }

    internal static TRow? Materialize<TRow>(
        ZLinkRedisLocationKind<TRow> kind,
        RedisValue[] fields)
        where TRow : class
    {
        if (fields[0].IsNull)
        {
            return null;
        }

        var row = ZLinkRedisLocationRowJson.Deserialize<TRow>((string)fields[0]!);
        return kind.Finalize(
            row,
            (long)fields[1],
            DateTimeOffset.FromUnixTimeMilliseconds((long)fields[2]));
    }
}
