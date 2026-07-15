using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Dispatch;

namespace SpotService.Server.Play;

using Zlink.Framework.E2E.Configuration;

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
    string? EvidenceFile = null,
    string? RedisEndpoint = null,
    string? RedisKeyPrefix = null,
    string? ControlEndpoint = null,
    string? SpotRouterEndpoint = null,
    string? SpotPubEndpoint = null,
    string? ExternalClientEndpoint = null,
    string? ExternalSpotEndpoint = null,
    string? ClientSpotPubEndpoint = null,
    string? StreamEndpoint = null,
    string? TlsStreamEndpoint = null,
    string? TlsCertPath = null,
    string? TlsKeyPath = null,
    string? MultiRouteAEndpoint = null,
    string? MultiRouteBEndpoint = null,
    string? MultiSpotRouterAEndpoint = null,
    string? MultiSpotRouterBEndpoint = null)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
        => E2eConfiguration.Load<ServerOptions>(args) with { Role = defaultRole };
}
