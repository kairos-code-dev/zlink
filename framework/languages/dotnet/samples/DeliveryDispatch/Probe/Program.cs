using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Registry;

var registryEndpoint = ReadOption(args, "--registry-endpoint")
    ?? throw new ArgumentException("Missing --registry-endpoint.");
var timeout = TimeSpan.FromSeconds(int.Parse(ReadOption(args, "--timeout-seconds") ?? "10"));
var topology = SampleTopology.Create();
var requiredEndpoints = new[]
{
    (ChannelName: SampleNames.TrackingRouteChannel, Endpoint: topology.TrackingRouteEndpoint),
};
var requiredChannels = new HashSet<string>(StringComparer.Ordinal)
{
    SampleNames.TrackingRouteChannel,
    SampleNames.DeliverySpotDiscovery,
};

var builder = Host.CreateApplicationBuilder();
builder.Logging.ClearProviders();
builder.Services.AddZLinkRegistryQueryClient(options => options.Endpoint = registryEndpoint);

using var host = builder.Build();
await host.StartAsync();
ZLinkRegistryTopologyEntry[] lastTopology = [];
try
{
    var client = host.Services.GetRequiredService<IZLinkRegistryQueryClient>();
    using var deadline = new CancellationTokenSource(timeout);
    while (!deadline.IsCancellationRequested)
    {
        lastTopology = await client.TopologyAsync(cancellationToken: deadline.Token);
        if (requiredEndpoints.All(required =>
                lastTopology.Any(entry => IsReadySocket(entry)
                                          && string.Equals(entry.ChannelName, required.ChannelName, StringComparison.Ordinal)
                                          && string.Equals(entry.Endpoint, required.Endpoint, StringComparison.Ordinal)))
            && requiredChannels.All(required =>
                lastTopology.Any(entry => IsReadyDiscovery(entry)
                                          && string.Equals(entry.ChannelName, required, StringComparison.Ordinal))))
        {
            await AssertTrackingChannelAsync(topology, deadline.Token);
            Console.WriteLine("topology=ready");
            return;
        }

        await Task.Delay(100, deadline.Token);
    }
}
catch (OperationCanceledException)
{
}
finally
{
    await host.StopAsync();
}

PrintTopology(lastTopology);
throw new TimeoutException("Timed out waiting for DeliveryDispatch sample topology readiness.");

static bool IsReadySocket(ZLinkRegistryTopologyEntry entry)
{
    return entry.State == ZLinkTopologyState.Ready
           && !string.IsNullOrWhiteSpace(entry.Endpoint);
}

static bool IsReadyDiscovery(ZLinkRegistryTopologyEntry entry)
{
    return entry.State == ZLinkTopologyState.Ready
           && entry.ReadyCount >= entry.DesiredCount
           && entry.DesiredCount > 0;
}

static void PrintTopology(IEnumerable<ZLinkRegistryTopologyEntry> entries)
{
    foreach (var entry in entries.OrderBy(static entry => entry.ChannelName)
                 .ThenBy(static entry => entry.Endpoint))
    {
        Console.Error.WriteLine(
            $"topology channel={entry.ChannelName} auto={entry.AutoConnectType} kind={entry.ServiceKind} role={entry.ServiceRole} state={entry.State} endpoint={entry.Endpoint} ready={entry.ReadyCount}/{entry.DesiredCount}");
    }
}

static async ValueTask AssertTrackingChannelAsync(
    SampleTopology topology,
    CancellationToken cancellationToken)
{
    var builder = Host.CreateApplicationBuilder();
    builder.Logging.ClearProviders();
    builder.Services.AddZLinkFramework(options =>
    {
        options.ConfigureDispatch().SetMessageDispatchErrorObserver<DeliveryDispatchErrorObserver>();
        options.Codecs.AddJson();
        {
            var channel = options.AddClientServerChannel(SampleNames.TrackingRouteChannel);
                        channel.EnableClient(topology.TrackingRouteEndpoint);

        }
    });

    using var host = builder.Build();
    await host.StartAsync(cancellationToken);
    try
    {
        var channels = host.Services.GetRequiredService<IZLinkChannelClient>();
        _ = await channels.RequestToChannel(
                SampleNames.TrackingRouteChannel,
                new DeliveryStatusChanged(
                    "probe-delivery",
                    DeliveryStatus.Assigned,
                    "probe-courier",
                    DateTimeOffset.UtcNow))
            .Async<DeliveryStatusAck>(cancellationToken);
    }
    finally
    {
        await host.StopAsync(cancellationToken);
    }
}

static string? ReadOption(string[] args, string name)
{
    var index = Array.IndexOf(args, name);
    return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
}
