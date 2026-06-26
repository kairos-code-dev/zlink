using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;

namespace YieldDispatch.Server.Delay;

internal static class DelayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = DelayOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.AddClientServerChannel(YieldDispatchNames.DelayChannel)
                .EnableServer(options.DelayEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddRequestHandler<DelayHandler, DelayReq, DelayReply>("DelayReq");
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "delay", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}

internal sealed class DelayHandler(NodeOptions node, EvidenceStore evidence)
    : IZLinkRequestHandler<DelayReq, DelayReply>
{
    public async ValueTask<DelayReply> HandleAsync(
        DelayReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        evidence.Add($"delay-started|rid={node.Rid}|request={request.RequestId}|marker={request.Marker}");
        await Task.Delay(TimeSpan.FromMilliseconds(request.DelayMs), cancellationToken);
        evidence.Add($"delay-completed|rid={node.Rid}|request={request.RequestId}|marker={request.Marker}");
        return new DelayReply(request.RequestId, request.Marker, node.Rid);
    }
}

internal sealed record NodeOptions(string Rid);

internal sealed class EvidenceStore
{
    private readonly object _gate = new();
    private readonly List<string> _entries = [];
    private readonly string? _filePath;

    public EvidenceStore(string rid, string? filePath)
    {
        Rid = rid;
        _filePath = filePath;
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        lock (_gate)
        {
            _entries.Add(entry);
            if (!string.IsNullOrWhiteSpace(_filePath))
            {
                File.AppendAllText(_filePath, entry + Environment.NewLine);
            }
        }
    }

    public string[] Snapshot()
    {
        lock (_gate)
        {
            return _entries.ToArray();
        }
    }
}

internal sealed record DelayOptions(
    string Rid,
    string HttpUrl,
    string DelayEndpoint,
    string LogDir)
{
    public string EvidenceFile => Path.Combine(LogDir, $"{Rid}.evidence.log");

    public static DelayOptions Parse(string[] args)
    {
        var values = Cli.Parse(args);
        return new DelayOptions(
            Cli.Required(values, "rid"),
            Cli.Required(values, "http-url"),
            Cli.Required(values, "delay-endpoint"),
            Cli.Required(values, "log-dir"));
    }
}

internal static class Cli
{
    public static Dictionary<string, string> Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {args[i]}.");
            }

            values[args[i][2..]] = args[++i];
        }

        return values;
    }

    public static string Required(Dictionary<string, string> values, string key)
        => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
}
