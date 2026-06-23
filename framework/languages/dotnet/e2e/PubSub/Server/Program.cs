using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using PubSub.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;

var options = ServerOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

var builder = WebApplication.CreateBuilder(args);
builder.Logging.ClearProviders();
builder.Logging.AddSimpleConsole(console =>
{
    console.SingleLine = true;
    console.TimestampFormat = "HH:mm:ss.fff ";
});
builder.WebHost.UseUrls(options.HttpUrl);
builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));
builder.Services.AddSingleton(new HandlerDelayOptions(options.HandlerDelayMs));

if (options.Role == "registry")
{
    builder.Services.AddZLinkRegistry(registry =>
    {
        registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
        registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
    });
}
else if (options.Role == "publisher")
{
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
        framework.ConfigureDispatch()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
            .TraceNodeId(options.Rid);
        framework.AddFanoutChannel(PubSubNames.Channel)
            .EnablePublisher(Require(options.PublisherEndpoint, "--publisher-endpoint"));
    });
}
else if (options.Role == "subscriber")
{
    builder.Services.AddSingleton<IZLinkMessageDispatchErrorObserver, EvidenceDispatchErrorObserver>();
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
        framework.ConfigureDispatch()
            .SetMessageDispatchErrorObserver<EvidenceDispatchErrorObserver>()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
            .TraceNodeId(options.Rid);
        framework.AddFanoutChannel(PubSubNames.Channel)
            .EnableSubscriber(Require(options.PublisherEndpoint, "--publisher-endpoint"))
            .AddPublishHandler<EventNotifyHandler, EventNotify>("EventNotify");
    });
}
else
{
    throw new InvalidOperationException($"Unsupported role '{options.Role}'.");
}

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
{
    evidence.Clear();
    return Results.Ok(new { status = "cleared" });
});
app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
{
    lifetime.StopApplication();
    return Results.Ok(new { status = "stopping" });
});
app.MapPost("/publish/event", async (
    string topic,
    string runId,
    int sequence,
    string value,
    IZLinkFanoutClient fanout,
    CancellationToken cancellationToken) =>
{
    await fanout.Publish(
            PubSubNames.Channel,
            topic,
            new EventNotify(runId, sequence, value))
        .PacketName("EventNotify")
        .Async(cancellationToken);
    return Results.Ok(new { status = "published", topic, runId, sequence });
});
app.MapPost("/publish/missing", async (
    string topic,
    string runId,
    int sequence,
    string value,
    IZLinkFanoutClient fanout,
    CancellationToken cancellationToken) =>
{
    await fanout.Publish(
            PubSubNames.Channel,
            topic,
            new MissingEventNotify(runId, sequence, value))
        .PacketName("MissingEventNotify")
        .Async(cancellationToken);
    return Results.Ok(new { status = "published", topic, runId, sequence });
});

await app.RunAsync();

static string Require(string? value, string name)
{
    return string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{name} is required.")
        : value;
}

internal sealed class EventNotifyHandler(EvidenceStore evidence, HandlerDelayOptions delayOptions)
    : IZLinkPublishHandler<EventNotify>
{
    public async ValueTask HandleAsync(
        EventNotify message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (delayOptions.DelayMs > 0 && message.Value.StartsWith("slow-", StringComparison.Ordinal))
        {
            evidence.Add(
                $"delay-start|rid={evidence.Rid}|run={message.RunId}|topic={context.Topic}"
                + $"|seq={message.Sequence}|value={message.Value}");
            await Task.Delay(delayOptions.DelayMs, cancellationToken);
        }

        if (context.Topic == PubSubNames.MainTopic)
        {
            evidence.Add(
                $"event|rid={evidence.Rid}|run={message.RunId}|topic={context.Topic}"
                + $"|seq={message.Sequence}|value={message.Value}|packet={context.PacketName}");
        }
        else
        {
            evidence.Add(
                $"ignored|rid={evidence.Rid}|run={message.RunId}|topic={context.Topic}"
                + $"|seq={message.Sequence}|value={message.Value}|packet={context.PacketName}");
        }
    }
}

internal sealed record HandlerDelayOptions(int DelayMs);

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence)
    : IZLinkMessageDispatchErrorObserver
{
    public ValueTask OnDispatchErrorAsync(
        ZLinkMessageDispatchErrorEvent error,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            "dispatch-error"
            + $"|surface={error.Surface}"
            + $"|kind={error.MessageKind}"
            + $"|reason={error.Reason}"
            + $"|action={error.Action}"
            + $"|packet={error.PacketName ?? "<null>"}"
            + $"|channel={error.ChannelName ?? "<null>"}"
            + $"|topic={error.Topic ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly string? _filePath;

    public EvidenceStore(string? filePath)
    {
        _filePath = filePath;
        Rid = Environment.GetEnvironmentVariable("ZLINK_E2E_RID") ?? "node";
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
        if (string.IsNullOrWhiteSpace(_filePath))
        {
            return;
        }

        lock (_fileGate)
        {
            File.AppendAllText(_filePath, entry + Environment.NewLine);
        }
    }

    public string[] Snapshot() => _entries.ToArray();

    public void Clear()
    {
        while (_entries.TryDequeue(out _))
        {
        }

        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            lock (_fileGate)
            {
                File.WriteAllText(_filePath, string.Empty);
            }
        }
    }
}

internal sealed record ServerOptions(
    string Role,
    string Rid,
    string HttpUrl,
    string LogDir,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? PublisherEndpoint,
    string? EvidenceFile,
    int HandlerDelayMs)
{
    public static ServerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string? Get(string name) => values.TryGetValue(name, out var bucket) ? bucket[^1] : null;
        return new ServerOptions(
            Role: Get("--role") ?? throw new ArgumentException("--role is required."),
            Rid: Get("--rid") ?? "node",
            HttpUrl: Get("--http-url") ?? "http://127.0.0.1:0",
            LogDir: Get("--log-dir") ?? "logs",
            RegistryPubEndpoint: Get("--registry-pub-endpoint"),
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            PublisherEndpoint: Get("--publisher-endpoint"),
            EvidenceFile: Get("--evidence-file"),
            HandlerDelayMs: int.TryParse(Get("--handler-delay-ms"), out var delayMs) ? delayMs : 0);
    }
}
