using System.Net.Http.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using RuntimeMonitoring.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);
using var http = new HttpClient();

using (var host = CreateClientHost(options))
{
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await client.RequestToChannel(
            RuntimeMonitoringNames.Channel,
            new ProfileRequest("monitor", "mon-a1-request"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(3))
        .Async<ProfileReply>();
    Ensure(reply.Value == "profile:monitor", "MON trigger request failed.");
    await host.StopAsync();
}

await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.ServiceUrl);
    return evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
            && line.Contains($"source={RuntimeMonitoringNames.ChannelServerSource}", StringComparison.Ordinal)
            && (line.Contains("kind=Connected", StringComparison.Ordinal)
                || line.Contains("kind=ConnectionReady", StringComparison.Ordinal)))
        && evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
            && (line.Contains("kind=Disconnected", StringComparison.Ordinal)
                || line.Contains("kind=Closed", StringComparison.Ordinal)));
}, "MON-A1 socket connect/disconnect events missing.");
Console.WriteLine("scenario MON-A1 passed");

await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.RegistryUrl);
    return evidence.Any(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged", StringComparison.Ordinal)
            && !line.Contains("topology=0", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains("monitor-registry|source=registry|kind=ServiceSummaryChanged", StringComparison.Ordinal)
            && !line.Contains("summary=0", StringComparison.Ordinal));
}, "MON-A2 registry topology/summary events missing.");
Console.WriteLine("scenario MON-A2 passed");

await WaitUntilAsync(async () =>
{
    var evidence = await ReadEvidenceAsync(http, options.ServiceUrl);
    return evidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=StatusChanged", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=PeersChanged", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=SubjectsChanged", StringComparison.Ordinal))
        && evidence.Any(line => line.Contains($"monitor-spot|source={RuntimeMonitoringNames.SpotNode}|kind=TimerHandlerFailed", StringComparison.Ordinal)
            && line.Contains("timer=failing", StringComparison.Ordinal));
}, "MON-A3 spot status/peer/subject/timer events missing.");
Console.WriteLine("scenario MON-A3 passed");

Console.WriteLine("runtime-monitoring e2e result=passed");

static IHost CreateClientHost(ClientOptions options)
{
    return Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            services.AddZLinkFramework(framework =>
            {
                framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
                framework.ConfigureDispatch()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(Path.Combine(options.LogDir, "client-flow.log"))
                    .TraceNodeId("client");
                framework.AddClientServerChannel(RuntimeMonitoringNames.Channel).EnableClient();
            });
        })
        .Build();
}

static async Task<string[]> ReadEvidenceAsync(HttpClient http, string baseUrl)
{
    return await http.GetFromJsonAsync<string[]>($"{baseUrl}/evidence") ?? [];
}

static async Task WaitUntilAsync(Func<Task<bool>> condition, string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
    Exception? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            if (await condition())
            {
                return;
            }
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or TimeoutException)
        {
            last = ex;
        }

        await Task.Delay(100);
    }

    throw new InvalidOperationException(last is null ? failureMessage : $"{failureMessage} Last error: {last.Message}", last);
}

static void Ensure(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

internal sealed record ClientOptions(
    string RegistryRouterEndpoint,
    string RegistryUrl,
    string ServiceUrl,
    string LogDir)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
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

            values[key] = args[++i];
        }

        string Get(string name) => values.TryGetValue(name, out var value)
            ? value
            : throw new ArgumentException($"{name} is required.");

        return new ClientOptions(
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            RegistryUrl: Get("--registry-url"),
            ServiceUrl: Get("--service-url"),
            LogDir: Get("--log-dir"));
    }
}
