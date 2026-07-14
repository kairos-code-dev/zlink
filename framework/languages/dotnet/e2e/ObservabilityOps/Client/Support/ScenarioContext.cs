using ObservabilityOps.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;
using System.Text.RegularExpressions;

namespace ObservabilityOps.Client.Support;

internal sealed class ScenarioContext(ClientOptions options) : IDisposable
{
    public ClientOptions Options { get; } = options;
    public ZLinkHttpClient PlayA { get; } = ZLinkHttpClient.Create(options.PlayAUrl).Build();
    public ZLinkHttpClient PlayB { get; } = ZLinkHttpClient.Create(options.PlayBUrl).Build();
    public ZLinkHttpClient Session { get; } = ZLinkHttpClient.Create(options.SessionUrl).Build();
    public ZLinkHttpClient WorkflowA { get; } = ZLinkHttpClient.Create(options.WorkflowAUrl).Build();
    public ZLinkHttpClient WorkflowB { get; } = ZLinkHttpClient.Create(options.WorkflowBUrl).Build();

    public async Task<IZlinkStreamConnector> ConnectAsync(
        string? endpoint = null,
        bool persistentReconnect = false,
        bool reconnectEnabled = true,
        Action<IZlinkStreamConnector>? configure = null)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint ?? Options.SessionEndpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(10),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            Reconnect = !reconnectEnabled
                ? new ZlinkStreamReconnectOptions { Enabled = false }
                : persistentReconnect
                ? new ZlinkStreamReconnectOptions
                {
                    Enabled = true,
                    InitialDelay = TimeSpan.FromMilliseconds(100),
                    MaxDelay = TimeSpan.FromMilliseconds(300),
                    BackoffFactor = 2,
                    MaxAttempts = null
                }
                : new ZlinkStreamReconnectOptions { Enabled = true },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 256
        });
        configure?.Invoke(connector);
        await connector.Connect.Async();
        return connector;
    }

    public static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    public async Task<string[]> WaitPlayAEvidenceAsync(params string[] markers)
    {
        return (await PlayA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(markers, []))
            .SubmitAsync<string[]>()).Body;
    }

    public string RequireSharedFlow(string packetName, params string[] roleLabels)
    {
        var matching = Directory.GetFiles(Options.LogDir, "flow-*.log")
            .SelectMany(path => File.ReadLines(path).Select(line => (Path: path, Line: line)))
            .Where(item => item.Line.Contains($"packet={packetName}", StringComparison.Ordinal))
            .ToArray();
        foreach (var flow in matching.Select(item => FlowId(item.Line)).Where(id => id is not null).Distinct())
        {
            if (roleLabels.All(label => matching.Any(item =>
                    Path.GetFileName(item.Path).Contains(label, StringComparison.Ordinal)
                    && string.Equals(FlowId(item.Line), flow, StringComparison.Ordinal))))
                return flow!;
        }
        throw new InvalidOperationException(
            $"No shared flow for packet '{packetName}' crossed roles: {string.Join(", ", roleLabels)}.");
    }

    public string[] ReadFlowLines(string roleLabel) => Directory.GetFiles(Options.LogDir, "flow-*.log")
        .Where(path => Path.GetFileName(path).Contains(roleLabel, StringComparison.Ordinal))
        .SelectMany(File.ReadLines).ToArray();

    public static async Task<DrainStatus> WaitForDrainAsync(
        ZLinkHttpClient client,
        TimeSpan timeout)
    {
        return (await client.Post("/drain/wait")
            .Query("timeoutMs", ((int)timeout.TotalMilliseconds).ToString())
            .Timeout(timeout + TimeSpan.FromSeconds(2))
            .SubmitAsync<DrainStatus>()).Body;
    }

    private static string? FlowId(string line) =>
        Regex.Match(line, "(?:^| )flow=([^ ]+)", RegexOptions.CultureInvariant) is { Success: true } match
            ? match.Groups[1].Value
            : null;

    public void Dispose()
    {
        PlayA.Dispose();
        PlayB.Dispose();
        Session.Dispose();
        WorkflowA.Dispose();
        WorkflowB.Dispose();
    }
}
