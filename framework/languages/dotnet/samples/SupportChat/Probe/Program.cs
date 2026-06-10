using SupportChat.Shared.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Registry;

namespace SupportChat.Probe;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var registryEndpoint = ReadOption(args, "--registry-endpoint")
            ?? throw new ArgumentException("Missing --registry-endpoint.");
        var timeout = TimeSpan.FromSeconds(int.Parse(ReadOption(args, "--timeout-seconds") ?? "10"));
        var topology = SampleTopology.Create();
        var requiredEndpoints = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            [SampleNames.ApiChannel] = topology.ApiChannelEndpoint,
            [SampleNames.SupportChannel] = topology.SupportChannelEndpoint,
        };
        var requiredChannels = new HashSet<string>(StringComparer.Ordinal)
        {
            SampleNames.SupportSpotDiscovery,
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
                                                  && string.Equals(entry.ChannelName, required.Key, StringComparison.Ordinal)
                                                  && string.Equals(entry.Endpoint, required.Value, StringComparison.Ordinal)))
                    && requiredChannels.All(required =>
                        lastTopology.Any(entry => IsReadyDiscovery(entry)
                                                  && string.Equals(entry.ChannelName, required, StringComparison.Ordinal))))
                {
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
        throw new TimeoutException("Timed out waiting for SupportChat sample topology readiness.");
    }

    private static bool IsReadySocket(ZLinkRegistryTopologyEntry entry)
    {
        return entry.State == ZLinkTopologyState.Ready
               && !string.IsNullOrWhiteSpace(entry.Endpoint);
    }

    private static bool IsReadyDiscovery(ZLinkRegistryTopologyEntry entry)
    {
        return entry.State == ZLinkTopologyState.Ready
               && entry.ReadyCount >= entry.DesiredCount
               && entry.DesiredCount > 0;
    }

    private static void PrintTopology(IEnumerable<ZLinkRegistryTopologyEntry> entries)
    {
        foreach (var entry in entries.OrderBy(static entry => entry.ChannelName)
                     .ThenBy(static entry => entry.Endpoint))
        {
            Console.Error.WriteLine(
                $"topology channel={entry.ChannelName} auto={entry.AutoConnectType} kind={entry.ServiceKind} role={entry.ServiceRole} state={entry.State} endpoint={entry.Endpoint} ready={entry.ReadyCount}/{entry.DesiredCount}");
        }
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
