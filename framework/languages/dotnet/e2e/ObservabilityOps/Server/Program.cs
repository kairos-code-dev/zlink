using System.Diagnostics.Metrics;
using Bingo.Server.Api;
using Bingo.Server.Configuration;
using Bingo.Server.Play;
using Bingo.Server.Session;
using ShoppingMall.Server.OrderWorkflow;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Locations;

var options = ServerOptions.Parse(args);
var topology = SampleTopology.Create();
var workflowTopology = ShoppingMall.Server.Configuration.SampleTopology.Create();
using var metrics = options.MetricsEnabled ? new MetricEvidenceCollector() : null;
using var host = options.Role switch
{
    "api-a" => ApiServerHostFactory.Build(topology, topology.ApiA),
    "api-b" => ApiServerHostFactory.Build(topology, topology.ApiB),
    "play-a" => PlayServerHostFactory.Build(topology, topology.PlayA, options.MetricsEnabled),
    "play-b" => PlayServerHostFactory.Build(topology, topology.PlayB, options.MetricsEnabled),
    "session-a" => SessionServerHostFactory.Build(topology, topology.SessionA),
    "session-b" => SessionServerHostFactory.Build(topology, topology.SessionB),
    "workflow-a" => OrderWorkflowServerHostFactory.Build(
        workflowTopology,
        workflowTopology.ForWorkflowInstance("workflow-a")),
    "workflow-b" => OrderWorkflowServerHostFactory.Build(
        workflowTopology,
        workflowTopology.ForWorkflowInstance("workflow-b")),
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

internal sealed record ServerOptions(string Role, string HttpUrl, bool MetricsEnabled)
{
    public static ServerOptions Parse(string[] args)
    {
        string? role = null;
        string? httpUrl = null;
        var metricsEnabled = true;
        for (var index = 0; index < args.Length; index++)
        {
            if (args[index] == "--role" && index + 1 < args.Length) role = args[++index];
            else if (args[index] == "--http-url" && index + 1 < args.Length) httpUrl = args[++index];
            else if (args[index] == "--metrics" && index + 1 < args.Length)
                metricsEnabled = !string.Equals(args[++index], "off", StringComparison.OrdinalIgnoreCase);
        }
        return new ServerOptions(
            role ?? throw new ArgumentException("--role is required."),
            httpUrl ?? throw new ArgumentException("--http-url is required."),
            metricsEnabled);
    }
}

internal static class EvidenceServer
{
    public static async Task RunAsync(
        ServerOptions options,
        IServiceProvider services,
        MetricEvidenceCollector? metrics,
        CancellationToken stopping)
    {
        var builder = WebApplication.CreateSlimBuilder();
        builder.WebHost.UseUrls(options.HttpUrl);
        await using var app = builder.Build();
        app.MapGet("/health", () => new { status = "ready", role = options.Role });
        app.MapGet("/evidence", () => CreateEvidenceAsync(options, services, metrics));
        app.MapGet("/drain", (int? deadlineMs) => DrainAsync(deadlineMs, services));
        app.MapPost("/message-flow/off", () =>
        {
            services.GetRequiredService<IZLinkMessageFlowControl>()
                .SetMessageFlowMode(ZLinkMessageFlowLogMode.Off);
            return new { mode = "off" };
        });
        await app.StartAsync(stopping);
        var boundUrl = app.Services.GetRequiredService<IServer>()
                           .Features.Get<IServerAddressesFeature>()?.Addresses.SingleOrDefault()
                       ?? throw new InvalidOperationException(
                           "Evidence server did not publish one bound address.");
        Console.WriteLine($"observability-ops ready role={options.Role} url={boundUrl}");
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
        MetricEvidenceCollector? metrics)
    {
        metrics?.RecordObservableInstruments();
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
        var metricSnapshot = metrics?.Snapshot() ?? [];
        return new
        {
            role = options.Role,
            ready,
            metrics = metricSnapshot,
            metricSeriesStored = metricSnapshot.Length,
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
    private readonly object _scrapeGate = new();
    private readonly MeterListener _listener = new();
    private readonly Dictionary<string, MetricSeriesState> _events = new(StringComparer.Ordinal);
    private Dictionary<string, MetricSeriesState> _observables = new(StringComparer.Ordinal);
    private Dictionary<string, MetricSeriesState>? _activeObservableScrape;

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

    public void RecordObservableInstruments()
    {
        lock (_scrapeGate)
        {
            var scrape = new Dictionary<string, MetricSeriesState>(StringComparer.Ordinal);
            lock (_gate) _activeObservableScrape = scrape;
            try
            {
                _listener.RecordObservableInstruments();
            }
            finally
            {
                lock (_gate)
                {
                    _observables = scrape;
                    _activeObservableScrape = null;
                }
            }
        }
    }

    public MetricSample[] Snapshot()
    {
        lock (_gate)
            return _events.Values
                .Concat(_observables.Values)
                .Select(static state => state.Snapshot())
                .OrderBy(static sample => sample.Name, StringComparer.Ordinal)
                .ThenBy(static sample => string.Join(
                    ",",
                    sample.Tags.OrderBy(static tag => tag.Key)
                        .Select(static tag => $"{tag.Key}={tag.Value}")), StringComparer.Ordinal)
                .ToArray();
    }

    public void Dispose() => _listener.Dispose();

    private void Record<T>(
        Instrument instrument,
        T value,
        ReadOnlySpan<KeyValuePair<string, object?>> tags,
        object? listenerState)
        where T : struct
    {
        _ = listenerState;
        var attributes = tags.ToArray().ToDictionary(
            static tag => tag.Key,
            static tag => tag.Value?.ToString() ?? string.Empty,
            StringComparer.Ordinal);
        var kind = InstrumentKind(instrument);
        var key = instrument.Name + "|" + kind + "|" + string.Join(
            ",",
            attributes.OrderBy(static pair => pair.Key)
                .Select(static pair => $"{pair.Key}={pair.Value}"));
        lock (_gate)
        {
            var target = kind == "observable" ? _activeObservableScrape : _events;
            if (target is null) return;
            if (!target.TryGetValue(key, out var series))
            {
                series = new MetricSeriesState(
                    instrument.Name,
                    kind,
                    instrument.Unit,
                    attributes);
                target.Add(key, series);
            }
            series.Record(Convert.ToDecimal(value, System.Globalization.CultureInfo.InvariantCulture));
        }
    }

    private static string InstrumentKind(Instrument instrument) => instrument switch
    {
        ObservableGauge<long> => "observable",
        ObservableGauge<double> => "observable",
        UpDownCounter<long> => "updown",
        UpDownCounter<double> => "updown",
        Histogram<long> or Histogram<double> => "histogram",
        _ => "counter"
    };
}

internal sealed class MetricSeriesState(
    string name,
    string kind,
    string? unit,
    IReadOnlyDictionary<string, string> tags)
{
    private decimal _value;
    private decimal _sum;
    private decimal? _min;
    private decimal? _max;
    private long _count;

    internal void Record(decimal measurement)
    {
        if (kind == "histogram")
        {
            _value = measurement;
            _sum += measurement;
            _min = _min is null ? measurement : Math.Min(_min.Value, measurement);
            _max = _max is null ? measurement : Math.Max(_max.Value, measurement);
            _count++;
            return;
        }

        _value += measurement;
        _count++;
    }

    internal MetricSample Snapshot() => new(
        name,
        kind,
        _value,
        unit,
        tags,
        _count,
        kind == "histogram" ? _sum : null,
        _min,
        _max);
}

internal sealed record MetricSample(
    string Name,
    string Kind,
    decimal Value,
    string? Unit,
    IReadOnlyDictionary<string, string> Tags,
    long Count,
    decimal? Sum,
    decimal? Min,
    decimal? Max);
