using System.Globalization;
using System.Text;
using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

public sealed partial class ZLinkRedisLocationStore
{
    private const int MaximumKeyBytes = 1024;
    private const int MaximumVersionBytes = 4096;
    private const int MaximumValueBytes = 1024 * 1024;
    private const int MaximumBatchKeys = 2048;
    private const int MaximumEncodedBatchBytes = 4 * 1024 * 1024;

    private const string OpaqueReadScript = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        local values = redis.call('HMGET', KEYS[1], 'key', 'value', 'version')
        if not values[1] then
            return { 'missing', nowMs }
        end
        local ttl = redis.call('PTTL', KEYS[1])
        return { 'found', nowMs, values[1], values[2], values[3], ttl }
        """;

    private const string OpaqueWriteScript = """
        if redis.replicate_commands then redis.replicate_commands() end
        local conditionCount = tonumber(ARGV[1])
        local mutationCount = tonumber(ARGV[2])
        local arg = 3
        for i = 1, conditionCount do
            local kind = ARGV[arg]
            local expected = ARGV[arg + 1]
            local current = redis.call('HGET', KEYS[i], 'version')
            if (kind == 'missing' and current)
                or (kind == 'version' and current ~= expected) then
                local time = redis.call('TIME')
                local nowMs = tonumber(time[1]) * 1000
                    + math.floor(tonumber(time[2]) / 1000)
                return { 'conflict', nowMs }
            end
            arg = arg + 2
        end
        local putVersions = {}
        for i = 1, mutationCount do
            local keyIndex = tonumber(ARGV[arg])
            local kind = ARGV[arg + 1]
            local originalKey = ARGV[arg + 2]
            local value = ARGV[arg + 3]
            local version = ARGV[arg + 4]
            local retention = tonumber(ARGV[arg + 5])
            local redisKey = KEYS[keyIndex]
            if kind == 'put' then
                redis.call('HSET', redisKey,
                    'key', originalKey, 'value', value, 'version', version)
                if retention >= 0 then
                    redis.call('PEXPIRE', redisKey, retention)
                else
                    redis.call('PERSIST', redisKey)
                end
                redis.call('ZADD', KEYS[#KEYS - 1], 0, originalKey)
                redis.call('HSET', KEYS[#KEYS], originalKey, redisKey)
                table.insert(putVersions, originalKey)
                table.insert(putVersions, version)
            else
                redis.call('DEL', redisKey)
                redis.call('ZREM', KEYS[#KEYS - 1], originalKey)
                redis.call('HDEL', KEYS[#KEYS], originalKey)
            end
            arg = arg + 6
        end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000
            + math.floor(tonumber(time[2]) / 1000)
        local result = { 'applied', nowMs }
        for _, item in ipairs(putVersions) do table.insert(result, item) end
        return result
        """;

    private const string OpaqueScanScript = """
        if redis.replicate_commands then redis.replicate_commands() end
        local prefix = ARGV[1]
        local offset = tonumber(ARGV[2])
        local limit = tonumber(ARGV[3])
        local create = ARGV[4] == '1'
        local snapshot = KEYS[3]
        if create then
            redis.call('DEL', snapshot)
            local time = redis.call('TIME')
            local nowMs = tonumber(time[1]) * 1000
                + math.floor(tonumber(time[2]) / 1000)
            redis.call('RPUSH', snapshot, tostring(nowMs))
            local originals = redis.call('ZRANGE', KEYS[1], 0, -1)
            for _, original in ipairs(originals) do
                if string.sub(original, 1, string.len(prefix)) == prefix then
                    local recordKey = redis.call('HGET', KEYS[2], original)
                    if recordKey then
                        local values = redis.call(
                            'HMGET', recordKey, 'key', 'value', 'version')
                        if values[1] == original then
                            local ttl = redis.call('PTTL', recordKey)
                            local expiresAt = -1
                            if ttl >= 0 then expiresAt = nowMs + ttl end
                            redis.call(
                                'RPUSH',
                                snapshot,
                                original,
                                values[2],
                                values[3],
                                tostring(expiresAt))
                        end
                    end
                end
            end
            redis.call('PEXPIRE', snapshot, 60000)
        elseif redis.call('EXISTS', snapshot) == 0 then
            return { 'expired' }
        end

        local snapshotNow = redis.call('LINDEX', snapshot, 0)
        local total = math.floor((redis.call('LLEN', snapshot) - 1) / 4)
        local emitted = 0
        local encodedBytes = 0
        local result = { 'page', snapshotNow, total }
        while offset + emitted < total and emitted < limit do
            local index = 1 + (offset + emitted) * 4
            local values = redis.call('LRANGE', snapshot, index, index + 3)
            local itemBytes = string.len(values[1])
                + string.len(values[2])
                + string.len(values[3])
            if emitted > 0 and encodedBytes + itemBytes > 4194304 then
                break
            end
            table.insert(result, values[1])
            table.insert(result, values[2])
            table.insert(result, values[3])
            table.insert(result, values[4])
            encodedBytes = encodedBytes + itemBytes
            emitted = emitted + 1
        end
        local nextOffset = offset + emitted
        if nextOffset >= total then
            nextOffset = -1
            redis.call('DEL', snapshot)
        end
        table.insert(result, 4, nextOffset)
        return result
        """;

    public async ValueTask<ZLinkStoreReadResult> ReadAsync(
        ZLinkStoreKey key,
        CancellationToken cancellationToken = default)
    {
        ValidateOpaqueKey(key, nameof(key));
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database
                    .ScriptEvaluateAsync(
                        OpaqueReadScript,
                        [_keys.OpaqueRecordKey(key.Value)],
                        [])
                    .ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds(
            (long)result[1]);
        if ((string)result[0]! == "missing")
            return new ZLinkStoreReadResult.Missing(storeNow);
        if (!string.Equals((string)result[2]!, key.Value, StringComparison.Ordinal))
        {
            throw new InvalidDataException(
                "The Redis opaque key digest resolved to a different key.");
        }
        var ttl = (long)result[5];
        return new ZLinkStoreReadResult.Found(
            new ZLinkStoreValue(
                (byte[])result[3]!,
                new ZLinkStoreVersion((string)result[4]!),
                ttl >= 0 ? storeNow + TimeSpan.FromMilliseconds(ttl) : null,
                storeNow));
    }

    public async ValueTask<ZLinkStoreWriteResult> WriteAsync(
        ZLinkStoreWriteRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        ValidateWriteRequest(request);

        var uniqueKeys = request.Conditions
            .Select(static condition => condition switch
            {
                ZLinkStoreCondition.Missing missing => missing.Key,
                ZLinkStoreCondition.Version version => version.Key,
                _ => throw new ArgumentException(
                    "Unknown Location Store condition.",
                    nameof(request))
            })
            .Concat(request.Mutations.Select(static mutation => mutation switch
            {
                ZLinkStoreMutation.Put put => put.Key,
                ZLinkStoreMutation.Delete delete => delete.Key,
                _ => throw new ArgumentException(
                    "Unknown Location Store mutation.",
                    nameof(request))
            }))
            .Distinct()
            .ToArray();
        var keyIndex = uniqueKeys
            .Select((key, index) => (key, index: index + 1))
            .ToDictionary(static item => item.key, static item => item.index);
        var redisKeys = uniqueKeys
            .Select(key => _keys.OpaqueRecordKey(key.Value))
            .Append(_keys.OpaqueIndexKey())
            .Append(_keys.OpaqueMapKey())
            .ToArray();
        var args = new List<RedisValue>
        {
            request.Conditions.Count,
            request.Mutations.Count
        };
        foreach (var condition in request.Conditions)
        {
            switch (condition)
            {
                case ZLinkStoreCondition.Missing:
                    args.Add("missing");
                    args.Add(string.Empty);
                    break;
                case ZLinkStoreCondition.Version version:
                    args.Add("version");
                    args.Add(version.Expected.Value);
                    break;
            }
        }
        foreach (var mutation in request.Mutations)
        {
            switch (mutation)
            {
                case ZLinkStoreMutation.Put put:
                    args.Add(keyIndex[put.Key]);
                    args.Add("put");
                    args.Add(put.Key.Value);
                    args.Add(put.Bytes.ToArray());
                    args.Add(Guid.NewGuid().ToString("N"));
                    args.Add(put.Retention is { } retention
                        ? checked((long)retention.TotalMilliseconds)
                        : -1);
                    break;
                case ZLinkStoreMutation.Delete delete:
                    args.Add(keyIndex[delete.Key]);
                    args.Add("delete");
                    args.Add(delete.Key.Value);
                    args.Add(Array.Empty<byte>());
                    args.Add(string.Empty);
                    args.Add(-1);
                    break;
            }
        }

        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database
                    .ScriptEvaluateAsync(
                        OpaqueWriteScript,
                        redisKeys,
                        args.ToArray())
                    .ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds(
            (long)result[1]);
        if ((string)result[0]! == "conflict")
            return new ZLinkStoreWriteResult.Conflict(storeNow);
        var versions = new Dictionary<ZLinkStoreKey, ZLinkStoreVersion>();
        for (var index = 2; index < result.Length; index += 2)
        {
            versions[new ZLinkStoreKey((string)result[index]!)] =
                new ZLinkStoreVersion((string)result[index + 1]!);
        }
        return new ZLinkStoreWriteResult.Applied(versions, storeNow);
    }

    public async ValueTask<ZLinkStoreScanResult> ScanAsync(
        ZLinkStoreScanRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (request.Limit is < 1 or > 1000)
            throw new ArgumentOutOfRangeException(nameof(request));
        var prefix = request.Prefix
                     ?? throw new ArgumentException(
                         "The scan prefix cannot be null.",
                         nameof(request));
        _ = Encoding.UTF8.GetByteCount(prefix) is <= MaximumKeyBytes
            ? 0
            : throw new ArgumentException(
                "The scan prefix exceeds 1024 UTF-8 bytes.",
                nameof(request));
        string scanId;
        var offset = 0;
        var create = request.Cursor is null;
        if (request.Cursor is { } cursor)
        {
            var cursorValue = cursor.Value ?? string.Empty;
            var cursorBytes = Encoding.UTF8.GetByteCount(cursorValue);
            if (cursorBytes is < 1 or > MaximumVersionBytes)
                throw new ArgumentException(
                    "Store scan cursors must contain 1..4096 UTF-8 bytes.",
                    nameof(request));
            var separator = cursorValue.LastIndexOf(':');
            if (separator != 32
                || !Guid.TryParseExact(cursorValue[..separator], "N", out _)
                || !int.TryParse(
                    cursorValue[(separator + 1)..],
                    NumberStyles.None,
                    CultureInfo.InvariantCulture,
                    out offset)
                || offset < 0)
            {
                return new ZLinkStoreScanResult.Expired();
            }
            scanId = cursorValue[..separator];
        }
        else
        {
            scanId = Guid.NewGuid().ToString("N");
        }
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database
                    .ScriptEvaluateAsync(
                        OpaqueScanScript,
                        [
                            _keys.OpaqueIndexKey(),
                            _keys.OpaqueMapKey(),
                            _keys.OpaqueScanKey(scanId)
                        ],
                        [
                            prefix,
                            offset,
                            request.Limit,
                            create ? 1 : 0
                        ])
                    .ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        if ((string)result[0]! == "expired")
            return new ZLinkStoreScanResult.Expired();

        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds(
            long.Parse(
                (string)result[1]!,
                NumberStyles.None,
                CultureInfo.InvariantCulture));
        var nextOffset = (long)result[3];
        var items = new List<KeyValuePair<ZLinkStoreKey, ZLinkStoreValue>>();
        for (var index = 4; index < result.Length; index += 4)
        {
            var expiresAtMs = long.Parse(
                (string)result[index + 3]!,
                NumberStyles.AllowLeadingSign,
                CultureInfo.InvariantCulture);
            items.Add(new KeyValuePair<ZLinkStoreKey, ZLinkStoreValue>(
                new ZLinkStoreKey((string)result[index]!),
                new ZLinkStoreValue(
                    (byte[])result[index + 1]!,
                    new ZLinkStoreVersion((string)result[index + 2]!),
                    expiresAtMs >= 0
                        ? DateTimeOffset.FromUnixTimeMilliseconds(expiresAtMs)
                        : null,
                    storeNow)));
        }
        return new ZLinkStoreScanResult.Page(
            new ZLinkStoreScanPage(
                items,
                nextOffset >= 0
                    ? new ZLinkStoreScanCursor(
                        $"{scanId}:{nextOffset.ToString(CultureInfo.InvariantCulture)}")
                    : null,
                storeNow));
    }

    private static void ValidateOpaqueKey(
        ZLinkStoreKey key,
        string parameterName)
    {
        var length = Encoding.UTF8.GetByteCount(key.Value ?? string.Empty);
        if (length is < 1 or > MaximumKeyBytes)
            throw new ArgumentException(
                "Location Store keys must contain 1..1024 UTF-8 bytes.",
                parameterName);
    }

    private static void ValidateWriteRequest(ZLinkStoreWriteRequest request)
    {
        var encodedBytes = 0L;
        var conditionKeys = request.Conditions.Select(
            static condition => condition switch
            {
                ZLinkStoreCondition.Missing missing => missing.Key,
                ZLinkStoreCondition.Version version => version.Key,
                _ => throw new ArgumentException(
                    "Unknown Location Store condition.")
            }).ToArray();
        var mutationKeys = request.Mutations.Select(
            static mutation => mutation switch
            {
                ZLinkStoreMutation.Put put => put.Key,
                ZLinkStoreMutation.Delete delete => delete.Key,
                _ => throw new ArgumentException(
                    "Unknown Location Store mutation.")
            }).ToArray();
        if (conditionKeys.Distinct().Count() != conditionKeys.Length
            || mutationKeys.Distinct().Count() != mutationKeys.Length)
        {
            throw new ArgumentException(
                "A key cannot occur twice in conditions or mutations.",
                nameof(request));
        }
        if (conditionKeys.Concat(mutationKeys).Distinct().Count()
            > MaximumBatchKeys)
        {
            throw new ArgumentException(
                "A conditional batch can reference at most 2048 keys.",
                nameof(request));
        }
        foreach (var condition in request.Conditions)
        {
            switch (condition)
            {
                case ZLinkStoreCondition.Missing missing:
                    ValidateOpaqueKey(missing.Key, nameof(request));
                    encodedBytes += Encoding.UTF8.GetByteCount(
                        missing.Key.Value);
                    break;
                case ZLinkStoreCondition.Version version:
                    ValidateOpaqueKey(version.Key, nameof(request));
                    encodedBytes += Encoding.UTF8.GetByteCount(
                        version.Key.Value);
                    var length = Encoding.UTF8.GetByteCount(
                        version.Expected.Value ?? string.Empty);
                    if (length is < 1 or > MaximumVersionBytes)
                        throw new ArgumentException(
                            "Store versions must contain 1..4096 UTF-8 bytes.",
                            nameof(request));
                    encodedBytes += length;
                    break;
            }
        }
        foreach (var mutation in request.Mutations)
        {
            switch (mutation)
            {
                case ZLinkStoreMutation.Put put:
                    ValidateOpaqueKey(put.Key, nameof(request));
                    encodedBytes += Encoding.UTF8.GetByteCount(put.Key.Value);
                    if (put.Bytes.Length > MaximumValueBytes)
                        throw new ArgumentException(
                            "A Location Store value can contain at most 1 MiB.",
                            nameof(request));
                    if (put.Retention is { } retention
                        && retention <= TimeSpan.Zero)
                        throw new ArgumentException(
                            "Retention must be positive.",
                            nameof(request));
                    encodedBytes += put.Bytes.Length;
                    break;
                case ZLinkStoreMutation.Delete delete:
                    ValidateOpaqueKey(delete.Key, nameof(request));
                    encodedBytes += Encoding.UTF8.GetByteCount(
                        delete.Key.Value);
                    break;
            }
        }
        if (encodedBytes > MaximumEncodedBatchBytes)
            throw new ArgumentException(
                "The encoded Store batch exceeds 4 MiB.",
                nameof(request));
    }
}
