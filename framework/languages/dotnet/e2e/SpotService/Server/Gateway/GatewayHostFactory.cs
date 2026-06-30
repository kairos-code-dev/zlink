using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Spots;

namespace SpotService.Server.Gateway;

internal static class GatewayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = GatewayOptions.Parse(args);
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
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
            framework.AddSpotMesh(SpotServiceNames.SpotChannel)
                .UseRegistrySpotResolver()
                .SetRoutingId(RoutingId.From(options.Rid))
                .EnablePubSub(Require(options.SpotPubEndpoint, "--spot-pub-endpoint"));
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/spot/publish", async (
            SpotPublishReq request,
            IZLinkSpotPublisherClient publisher,
            EvidenceStore evidence) =>
        {
            publisher.PublishSpot(
                    SpotServiceNames.SpotChannel,
                    SpotServiceNames.SpotMsgTopic,
                    new SpotMsg(request.Marker))
                .PacketName("SpotMsg").Submit();
            evidence.Add($"spot-publish|rid={options.Rid}|spot={request.SpotRid}|marker={request.Marker}");
            return Results.Ok(new SpotPublishRes(
                "spot.sm-c4-publish",
                options.Rid,
                request.SpotRid,
                request.Marker,
                evidence.Snapshot()));
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }

    static string Require(string? value, string optionName)
        => string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{optionName} is required.")
            : value;
}

internal sealed class EvidenceStore
{
    readonly ConcurrentQueue<string> _entries = new();
    readonly string? _file;

    public EvidenceStore(string rid, string? file)
    {
        _file = file;
        Add($"start|rid={rid}");
    }

    public void Add(string value)
    {
        _entries.Enqueue(value);
        if (!string.IsNullOrWhiteSpace(_file))
        {
            File.AppendAllLines(_file, new[] { value });
        }
    }

    public string[] Snapshot() => _entries.ToArray();
}

internal sealed record GatewayOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string? RegistryRouterEndpoint,
    string? SpotPubEndpoint)
{
    public static GatewayOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            values[key[2..]] = args[++i];
        }

        string Required(string key) => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");

        return new GatewayOptions(
            Required("rid"),
            Required("http-url"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-spot-e2e")),
            values.GetValueOrDefault("evidence-file"),
            values.GetValueOrDefault("registry-router-endpoint"),
            values.GetValueOrDefault("spot-pub-endpoint"));
    }
}
