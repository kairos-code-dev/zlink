using System.Security.Cryptography;
using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Official Redis store for immutable relocation roots and participant
/// payloads. Location authority remains in <see cref="ZLinkRedisLocationStore"/>.
/// </summary>
public sealed class ZLinkRedisRelocationStore :
    IZLinkRelocationStore,
    IAsyncDisposable
{
    private const int MaximumPayloadSize = 64 * 1024 * 1024;

    private const string PutScript = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        local current = redis.call('GET', KEYS[1])
        if current and current ~= ARGV[1] then
            return { 'collision', nowMs }
        end
        redis.call('SET', KEYS[1], ARGV[1], 'PX', ARGV[2])
        return { 'stored', nowMs }
        """;

    private const string RenewScript = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        if redis.call('PEXPIRE', KEYS[1], ARGV[1]) == 0 then
            return { 'missing', nowMs }
        end
        return { 'renewed', nowMs }
        """;

    private readonly ZLinkRedisRelocationOptions _options;
    private readonly Func<ConfigurationOptions, ValueTask<IZLinkRedisConnection>> _connect;
    private readonly SemaphoreSlim _connectGate = new(1, 1);
    private readonly object _disposeGate = new();
    private IZLinkRedisConnection? _connection;
    private Task? _disposeTask;
    private TaskCompletionSource? _operationsDrained;
    private int _activeOperations;
    private int _disposed;

    public ZLinkRedisRelocationStore(ZLinkRedisRelocationOptions options)
        : this(options, ConnectAsync)
    {
    }

    internal ZLinkRedisRelocationStore(
        ZLinkRedisRelocationOptions options,
        Func<ConfigurationOptions, ValueTask<IZLinkRedisConnection>> connect)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(connect);
        options.Validate();
        _options = options;
        _connect = connect;
    }

    public ZLinkRedisRelocationStore(Action<ZLinkRedisRelocationOptions> configure)
        : this(Configure(configure))
    {
    }

    public async ValueTask<ZLinkRelocationStored> PutRelocationAsync(
        ReadOnlyMemory<byte> payload,
        TimeSpan retention,
        CancellationToken cancellationToken = default)
    {
        ValidatePayload(payload);
        var retentionMs = ValidateRetention(retention);
        var reference = Convert.ToHexString(SHA256.HashData(payload.Span))
            .ToLowerInvariant();
        var checksum = ComputeCrc32C(payload.Span);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    PutScript,
                    [PayloadKey(reference)],
                    [payload.ToArray(), retentionMs]).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        if ((string)result[0]! != "stored")
        {
            throw new InvalidDataException(
                $"Redis already contains different bytes for relocation reference '{reference}'.");
        }

        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds((long)result[1]);
        return new ZLinkRelocationStored(
            reference,
            checksum,
            storeNow + TimeSpan.FromMilliseconds(retentionMs),
            storeNow);
    }

    public async ValueTask<ZLinkRelocationReadResult> GetRelocationAsync(
        string reference,
        CancellationToken cancellationToken = default)
    {
        ValidateReference(reference);
        var payload = await ExecuteAsync(
                async database => await database.StringGetAsync(
                        PayloadKey(reference))
                    .ConfigureAwait(false),
                cancellationToken)
            .ConfigureAwait(false);
        return payload.IsNull
            ? new ZLinkRelocationReadResult.Missing()
            : new ZLinkRelocationReadResult.Found((byte[])payload!);
    }

    public async ValueTask<ZLinkRelocationRenewResult> RenewRelocationAsync(
        string reference,
        TimeSpan retention,
        CancellationToken cancellationToken = default)
    {
        ValidateReference(reference);
        var retentionMs = ValidateRetention(retention);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    RenewScript,
                    [PayloadKey(reference)],
                    [retentionMs]).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        if ((string)result[0]! == "missing")
            return new ZLinkRelocationRenewResult.Missing();
        var storeNow = DateTimeOffset.FromUnixTimeMilliseconds((long)result[1]);
        return new ZLinkRelocationRenewResult.Renewed(
            storeNow + TimeSpan.FromMilliseconds(retentionMs),
            storeNow);
    }

    public async ValueTask<ZLinkRelocationDeleteResult> DeleteRelocationAsync(
        string reference,
        CancellationToken cancellationToken = default)
    {
        ValidateReference(reference);
        var removed = await ExecuteAsync(
                async database => await database.KeyDeleteAsync(
                        PayloadKey(reference))
                    .ConfigureAwait(false),
                cancellationToken)
            .ConfigureAwait(false);
        return removed
            ? ZLinkRelocationDeleteResult.Deleted
            : ZLinkRelocationDeleteResult.Missing;
    }

    public ValueTask DisposeAsync()
    {
        Task disposeTask;
        TaskCompletionSource? startDispose = null;
        lock (_disposeGate)
        {
            if (_disposeTask is null)
            {
                Volatile.Write(ref _disposed, 1);
                startDispose = new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                _disposeTask = DisposeCoreAsync(startDispose.Task);
            }
            disposeTask = _disposeTask;
        }
        startDispose?.TrySetResult();
        return new ValueTask(disposeTask);
    }

    private async Task DisposeCoreAsync(Task started)
    {
        await started.ConfigureAwait(false);
        Task? operationsDrained;
        lock (_disposeGate)
        {
            operationsDrained = _activeOperations == 0
                ? null
                : (_operationsDrained ??= new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously)).Task;
        }
        if (operationsDrained is not null)
            await operationsDrained.ConfigureAwait(false);

        var connection = _connection;
        _connection = null;
        try
        {
            if (connection is not null)
                await connection.DisposeAsync().ConfigureAwait(false);
        }
        finally
        {
            _connectGate.Dispose();
        }
    }

    private RedisKey PayloadKey(string reference) =>
        $"{_options.KeyPrefix}:payload:{reference}";

    private async ValueTask<TResult> ExecuteAsync<TResult>(
        Func<IDatabase, ValueTask<TResult>> operation,
        CancellationToken cancellationToken)
    {
        using var lease = EnterOperation();
        cancellationToken.ThrowIfCancellationRequested();
        var database = await GetDatabaseAsync(cancellationToken).ConfigureAwait(false);
        return await operation(database).ConfigureAwait(false);
    }

    private async ValueTask<IDatabase> GetDatabaseAsync(
        CancellationToken cancellationToken)
    {
        if (Volatile.Read(ref _connection) is { } connected)
            return connected.GetDatabase();

        await _connectGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var connection = _connection ??= await _connect(
                    _options.BuildConfiguration())
                .ConfigureAwait(false);
            return connection.GetDatabase();
        }
        finally
        {
            _connectGate.Release();
        }
    }

    private OperationLease EnterOperation()
    {
        lock (_disposeGate)
        {
            ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposed) != 0, this);
            _activeOperations++;
            return new OperationLease(this);
        }
    }

    private void ExitOperation()
    {
        TaskCompletionSource? drained = null;
        lock (_disposeGate)
        {
            _activeOperations--;
            if (_activeOperations == 0 && Volatile.Read(ref _disposed) != 0)
            {
                drained = _operationsDrained;
                _operationsDrained = null;
            }
        }
        drained?.TrySetResult();
    }

    private static ZLinkRedisRelocationOptions Configure(
        Action<ZLinkRedisRelocationOptions> configure)
    {
        ArgumentNullException.ThrowIfNull(configure);
        var options = new ZLinkRedisRelocationOptions();
        configure(options);
        return options;
    }

    private static long ValidateRetention(TimeSpan retention)
    {
        if (retention <= TimeSpan.Zero
            || retention.TotalMilliseconds > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(retention));
        return Math.Max(1, checked((long)retention.TotalMilliseconds));
    }

    private static void ValidatePayload(ReadOnlyMemory<byte> payload)
    {
        if (payload.IsEmpty || payload.Length > MaximumPayloadSize)
            throw new ArgumentOutOfRangeException(nameof(payload));
    }

    private static void ValidateReference(string reference)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(reference);
        if (reference.Length != 64
            || reference.Any(static value =>
                !char.IsAsciiHexDigit(value)
                || char.IsAsciiLetterUpper(value)))
            throw new ArgumentException(
                "Relocation references must be lowercase SHA-256 hex.",
                nameof(reference));
    }

    private static uint ComputeCrc32C(ReadOnlySpan<byte> payload)
    {
        const uint polynomial = 0x82f63b78;
        var crc = uint.MaxValue;
        foreach (var value in payload)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1) == 0 ? 0 : polynomial);
        }
        return ~crc;
    }

    private static async ValueTask<IZLinkRedisConnection> ConnectAsync(
        ConfigurationOptions options) =>
        new ZLinkStackExchangeRedisConnection(
            await ConnectionMultiplexer.ConnectAsync(options).ConfigureAwait(false));

    private sealed class OperationLease(ZLinkRedisRelocationStore owner) : IDisposable
    {
        private ZLinkRedisRelocationStore? _owner = owner;

        public void Dispose() =>
            Interlocked.Exchange(ref _owner, null)?.ExitOperation();
    }
}
