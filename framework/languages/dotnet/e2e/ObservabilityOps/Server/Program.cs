using System.Diagnostics.Metrics;
using Bingo.Server.Api;
using Bingo.Server.Configuration;
using Bingo.Server.Play;
using Bingo.Server.Session;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Locations;

var options = ServerOptions.Parse(args);
var topology = SampleTopology.Create();
using var metrics = new MetricEvidenceCollector();
using var host = options.Role switch
{
    "api-a" => ApiServerHostFactory.Build(topology, topology.ApiA),
    "api-b" => ApiServerHostFactory.Build(topology, topology.ApiB),
    "play-a" => PlayServerHostFactory.Build(topology, topology.PlayA),
    "play-b" => PlayServerHostFactory.Build(topology, topology.PlayB),
    "session-a" => SessionServerHostFactory.Build(topology, topology.SessionA),
    "session-b" => SessionServerHostFactory.Build(topology, topology.SessionB),
    _ => throw new ArgumentException($"Unknown role '{options.Role}'.")
};

await host.StartAsync();
var lifetime = host.Services.GetRequiredService<IHostApplicationLifetime>();
try
{
    await EvidenceServer.RunAsync(
        options,
        host.Services,
        metrics,
        lifetime.ApplicationStopping);
}
finally
{
    await host.StopAsync(CancellationToken.None);
}

internal sealed record ServerOptions(string Role, string HttpUrl)
{
    public static ServerOptions Parse(string[] args)
    {
        string? role = null;
        string? httpUrl = null;
        for (var index = 0; index < args.Length; index++)
        {
            if (args[index] == "--role" && index + 1 < args.Length) role = args[++index];
            else if (args[index] == "--http-url" && index + 1 < args.Length) httpUrl = args[++index];
        }
        return new ServerOptions(
            role ?? throw new ArgumentException("--role is required."),
            httpUrl ?? throw new ArgumentException("--http-url is required."));
    }
}

internal static class EvidenceServer
{
    public static async Task RunAsync(
        ServerOptions options,
        IServiceProvider services,
        MetricEvidenceCollector metrics,
        CancellationToken stopping)
    {
        var builder = WebApplication.CreateSlimBuilder();
        builder.WebHost.UseUrls(options.HttpUrl);
        await using var app = builder.Build();
        app.MapGet("/health", () => new { status = "ready", role = options.Role });
        app.MapGet("/evidence", () => CreateEvidenceAsync(options, services, metrics));
        app.MapGet("/drain", (int? deadlineMs) => DrainAsync(deadlineMs, services));
        await app.StartAsync(stopping);
        Console.WriteLine($"observability-ops ready role={options.Role} url={options.HttpUrl}");
        try
        {
            await Task.Delay(Timeout.InfiniteTimeSpan, stopping);
        }
        catch (OperationCanceledException) when (stopping.IsCancellationRequested)
        {
        }
        await app.StopAsync(CancellationToken.None);
    }

    private static async Task<object> CreateEvidenceAsync(
        ServerOptions options,
        IServiceProvider services,
        MetricEvidenceCollector metrics)
    {
        metrics.RecordObservableInstruments();
        var rows = services.GetService<IZLinkLocationRuntimeQuery>() is { } locations
            ? await locations.ListPeerLocationsAsync(new ZLinkPeerLocationFilter())
            : [];
        var actors = services.GetService<IZLinkLocationRuntimeQuery>() is { } actorLocations
            ? (await actorLocations.ListActorLocationsAsync(new ZLinkActorLocationFilter())).Items
            : [];
        var spots = services.GetService<IZLinkLocationRuntimeQuery>() is { } spotLocations
            ? (await spotLocations.ListSpotLocationsAsync(new ZLinkSpotLocationFilter())).Items
            : [];
        var ready = services.GetRequiredService<IZLinkDrainControl>().IsReady;
        return new
        {
            role = options.Role,
            ready,
            metrics = metrics.Snapshot(),
            peerRows = rows.Select(static row => new
            {
                nodeRid = row.NodeRid?.ToString(),
                row.Draining,
                row.Generation
            }),
            actorRows = actors.Select(static row => new
            {
                row.ActorId,
                nodeRid = row.NodeRid.ToString(),
                row.Generation
            }),
            spotRows = spots.Select(static row => new
            {
                row.MeshName,
                nodeRid = row.NodeRid.ToString(),
                spotRid = row.SpotRid.ToString(),
                kind = row.SpotKind.ToString(),
                row.Generation
            })
        };
    }

    private static async Task<object> DrainAsync(
        int? deadlineMs,
        IServiceProvider services)
    {
        var deadline = deadlineMs is { } milliseconds
            ? TimeSpan.FromMilliseconds(milliseconds)
            : TimeSpan.FromSeconds(30);
        var result = await services.GetRequiredService<IZLinkDrainControl>()
            .DrainAsync(deadline, CancellationToken.None);
        return result switch
        {
            Drained => new { result = "drained", reason = (string?)null },
            ForceStopped forced => new { result = "force_stopped", reason = (string?)forced.Reason.ToString() },
            _ => throw new InvalidOperationException("Unknown drain result.")
        };
    }
}

internal sealed class MetricEvidenceCollector : IDisposable
{
    private readonly object _gate = new();
    private readonly MeterListener _listener = new();
    private readonly Dictionary<string, MetricSample> _latest = new(StringComparer.Ordinal);

    public MetricEvidenceCollector()
    {
        _listener.InstrumentPublished = static (instrument, listener) =>
        {
            if (instrument.Meter.Name == ZLinkMeters.Framework)
                listener.EnableMeasurementEvents(instrument);
        };
        _listener.SetMeasurementEventCallback<long>(Record);
        _listener.SetMeasurementEventCallback<double>(Record);
        _listener.Start();
    }

    public void RecordObservableInstruments() => _listener.RecordObservableInstruments();

    public MetricSample[] Snapshot()
    {
        lock (_gate) return _latest.Values.OrderBy(static sample => sample.Name).ToArray();
    }

    public void Dispose() => _listener.Dispose();

    private void Record<T>(
        Instrument instrument,
        T value,
        ReadOnlySpan<KeyValuePair<string, object?>> tags,
        object? state)
        where T : struct
    {
        var attributes = tags.ToArray().ToDictionary(
            static tag => tag.Key,
            static tag => tag.Value?.ToString() ?? string.Empty,
            StringComparer.Ordinal);
        var key = instrument.Name + "|" + string.Join(
            ",",
            attributes.OrderBy(static pair => pair.Key)
                .Select(static pair => $"{pair.Key}={pair.Value}"));
        lock (_gate)
            _latest[key] = new MetricSample(
                instrument.Name,
                InstrumentKind(instrument),
                Convert.ToDouble(value),
                instrument.Unit,
                attributes);
    }

    private static string InstrumentKind(Instrument instrument) => instrument switch
    {
        ObservableGauge<long> => "observable",
        UpDownCounter<long> => "updown",
        Histogram<long> or Histogram<double> => "histogram",
        _ => "counter"
    };
}

internal sealed record MetricSample(
    string Name,
    string Kind,
    double Value,
    string? Unit,
    IReadOnlyDictionary<string, string> Tags);
