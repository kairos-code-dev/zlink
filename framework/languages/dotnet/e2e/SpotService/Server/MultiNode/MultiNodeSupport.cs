using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Dispatch;

namespace SpotService.Server.MultiNode;

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence)
    : IZLinkMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|reason={flow.ErrorReason}"
            + $"|action={flow.ErrorAction}"
            + $"|packet={flow.PacketName ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly string? _filePath;
    private readonly SemaphoreSlim _signal = new(0);

    public EvidenceStore(string rid, string? filePath)
    {
        Rid = rid;
        _filePath = filePath;
        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_filePath)!);
            File.WriteAllText(_filePath, string.Empty);
        }
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        _signal.Release();
        if (string.IsNullOrWhiteSpace(_filePath)) return;

        lock (_fileGate)
        {
            File.AppendAllText(_filePath, entry + Environment.NewLine);
        }
    }

    public string[] Snapshot()
    {
        return _entries.ToArray();
    }

    public async Task<string[]> WaitUntilAsync(
        Func<string[], bool> condition,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var snapshot = Snapshot();
            if (condition(snapshot)) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero) throw new TimeoutException("Timed out waiting for spot service evidence.");

            await _signal.WaitAsync(remaining, cancellationToken);
        }
    }
}

internal sealed record NodeOptions(string Rid);

internal sealed record ServerOptions(
    string Role,
    string Rid,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string? RedisEndpoint,
    string? RedisKeyPrefix,
    string? ControlEndpoint,
    string? SpotRouterEndpoint,
    string? SpotPubEndpoint,
    string? ExternalClientEndpoint,
    string? ExternalSpotEndpoint,
    string? ClientSpotPubEndpoint,
    string? StreamEndpoint,
    string? TlsStreamEndpoint,
    string? TlsCertPath,
    string? TlsKeyPath,
    string? MultiRouteAEndpoint,
    string? MultiRouteBEndpoint,
    string? MultiSpotRouterAEndpoint,
    string? MultiSpotRouterBEndpoint)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal)) continue;

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for {key}.");

            values[key[2..]] = args[++i];
        }

        string Required(string key)
        {
            return values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
                ? value
                : throw new ArgumentException($"--{key} is required.");
        }

        return new ServerOptions(
            defaultRole,
            Required("rid"),
            Required("http-url"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-spot-e2e")),
            values.GetValueOrDefault("evidence-file"),
            values.GetValueOrDefault("redis-endpoint"),
            values.GetValueOrDefault("redis-key-prefix"),
            values.GetValueOrDefault("control-endpoint"),
            values.GetValueOrDefault("spot-router-endpoint"),
            values.GetValueOrDefault("spot-pub-endpoint"),
            values.GetValueOrDefault("external-client-endpoint"),
            values.GetValueOrDefault("external-spot-endpoint"),
            values.GetValueOrDefault("client-spot-pub-endpoint"),
            values.GetValueOrDefault("stream-endpoint"),
            values.GetValueOrDefault("tls-stream-endpoint"),
            values.GetValueOrDefault("tls-cert-path"),
            values.GetValueOrDefault("tls-key-path"),
            values.GetValueOrDefault("multi-route-a-endpoint"),
            values.GetValueOrDefault("multi-route-b-endpoint"),
            values.GetValueOrDefault("multi-spot-router-a-endpoint"),
            values.GetValueOrDefault("multi-spot-router-b-endpoint"));
    }
}