using System.Net.Http.Json;
using DiscoveryRegistryHa.Shared;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Registry;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);
using var http = new HttpClient();

if (options.Scenario == "a1")
{
    await RunDrA1Async(http, options);
}
else if (options.Scenario == "cluster")
{
    await RunDrA2Async(http, options);
    await RunDrA3Async(http, options);
    await RunDrC1Async(http, options);
}
else
{
    throw new InvalidOperationException($"Unsupported scenario '{options.Scenario}'.");
}

Console.WriteLine($"discovery-registry-ha scenario={options.Scenario} result=passed");

static async Task RunDrA1Async(HttpClient http, ClientOptions options)
{
    await WaitForTopologyReadyAsync(http, options.Reg1Url, expectedReady: 2);
    using var host = CreateClientHost("client-dr-a1", options.LogDir, [options.Reg1RouterEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await RequestAsync(client, "dr-a1", "dr-a1-basic");
    Ensure(reply.Value == "profile:dr-a1", "DR-A1 request failed.");
    Ensure(reply.ProviderRid is "api-a" or "api-b", "DR-A1 unexpected provider.");
    await host.StopAsync();
    Console.WriteLine("scenario DR-A1 passed");
}

static async Task RunDrA2Async(HttpClient http, ClientOptions options)
{
    await WaitForMembersAsync(http, options.Reg2Url, [options.ApiAEndpoint], expectedConnectedPeers: 2);
    using var host = CreateClientHost("client-dr-a2", options.LogDir, [options.Reg2RouterEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    await WaitUntilAsync(async () =>
    {
        var reply = await RequestAsync(client, "dr-a2", $"dr-a2-{Guid.NewGuid():N}");
        return reply.ProviderRid == "api-a";
    }, "DR-A2 reg-2-only consumer did not reach provider advertised through reg-1.");
    await host.StopAsync();
    Console.WriteLine("scenario DR-A2 passed");
}

static async Task RunDrA3Async(HttpClient http, ClientOptions options)
{
    foreach (var registry in new[] { options.Reg1Url, options.Reg2Url, options.Reg3Url })
    {
        await WaitForMembersAsync(http, registry, [options.ApiAEndpoint, options.ApiBEndpoint], expectedConnectedPeers: 2);
    }

    foreach (var endpoint in new[] { options.Reg1RouterEndpoint, options.Reg2RouterEndpoint, options.Reg3RouterEndpoint })
    {
        using var host = CreateClientHost($"client-dr-a3-{Guid.NewGuid():N}", options.LogDir, [endpoint]);
        await host.StartAsync();
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();
        await WaitUntilAsync(async () =>
        {
            var reply = await RequestAsync(client, "dr-a3", $"dr-a3-{Guid.NewGuid():N}");
            return reply.ProviderRid is "api-a" or "api-b";
        }, "DR-A3 registry-only consumer did not reach a provider.");
        await host.StopAsync();
    }

    Console.WriteLine("scenario DR-A3 passed");
}

static async Task RunDrC1Async(HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.Reg2Url}/shutdown");
    await Task.Delay(TimeSpan.FromMilliseconds(500));

    using var host = CreateClientHost("client-dr-c1", options.LogDir, [options.Reg1RouterEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    await WaitUntilAsync(async () =>
    {
        var reply = await RequestAsync(client, "dr-c1", $"dr-c1-{Guid.NewGuid():N}");
        return reply.ProviderRid is "api-a" or "api-b";
    }, "DR-C1 live registry request failed.");
    await host.StopAsync();

    var deadEndpointFailed = false;
    try
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromMilliseconds(500));
        _ = await http.GetFromJsonAsync<ZLinkRegistryStatus>($"{options.Reg2Url}/registry/status", timeout.Token);
    }
    catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException)
    {
        deadEndpointFailed = true;
    }

    Ensure(deadEndpointFailed, "DR-C1 dead registry endpoint did not fail within the bounded timeout.");
    Console.WriteLine("scenario DR-C1 passed");
}

static IHost CreateClientHost(string nodeId, string logDir, IReadOnlyList<string> registryEndpoints)
{
    return Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            services.AddZLinkFramework(framework =>
            {
                foreach (var endpoint in registryEndpoints)
                {
                    framework.UseDiscovery().AddRegistryEndpoint(endpoint);
                }

                framework.ConfigureDispatch()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(Path.Combine(logDir, $"{nodeId}-flow.log"))
                    .TraceNodeId(nodeId);
                framework.AddClientServerChannel(DiscoveryRegistryHaNames.Channel).EnableClient();
            });
        })
        .Build();
}

static async Task<ProfileReply> RequestAsync(IZLinkChannelClient client, string value, string marker)
{
    return await client.RequestToChannel(
            DiscoveryRegistryHaNames.Channel,
            new ProfileRequest(value, marker))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(3))
        .Async<ProfileReply>();
}

static async Task WaitForTopologyReadyAsync(HttpClient http, string registryUrl, int expectedReady)
{
    await WaitUntilAsync(async () =>
    {
        var topology = await http.GetFromJsonAsync<ZLinkRegistryTopologyEntry[]>($"{registryUrl}/registry/topology");
        return topology?.Count(entry => entry.State == ZLinkTopologyState.Ready
            && entry.ServiceRole == ZLinkServiceRole.Router) == expectedReady;
    }, $"Expected {expectedReady} ready topology entries at {registryUrl}.");
}

static async Task WaitForMembersAsync(
    HttpClient http,
    string registryUrl,
    IReadOnlyList<string> expectedEndpoints,
    uint expectedConnectedPeers)
{
    string lastSnapshot = "<none>";
    await WaitUntilAsync(async () =>
    {
        var status = await http.GetFromJsonAsync<ZLinkRegistryStatus>($"{registryUrl}/registry/status");
        var members = await http.GetFromJsonAsync<ZLinkMemberPeerEntry[]>($"{registryUrl}/registry/members");
        lastSnapshot = "status="
            + $"{status?.ConnectedPeerRegistryCount}/{status?.PeerRegistryCount}"
            + ", members="
            + string.Join(",", members?.Select(member => $"{member.Endpoint}:{member.RoutingId}") ?? []);
        if (status?.ConnectedPeerRegistryCount != expectedConnectedPeers)
        {
            return false;
        }

        var endpoints = members?
            .Where(member => member.ServiceRole == ZLinkServiceRole.Router)
            .Select(member => member.Endpoint)
            .ToHashSet(StringComparer.Ordinal) ?? [];
        return expectedEndpoints.All(endpoints.Contains);
    }, $"Expected members {string.Join(",", expectedEndpoints)} at {registryUrl}. Last {lastSnapshot}");
}

static async Task PostAsync(HttpClient http, string url)
{
    try
    {
        using var response = await http.PostAsync(url, content: null);
        response.EnsureSuccessStatusCode();
    }
    catch (HttpRequestException)
    {
    }
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
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or TimeoutException or ZLinkFrameworkException)
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
    string Scenario,
    string LogDir,
    string Reg1Url,
    string Reg2Url,
    string Reg3Url,
    string Reg1RouterEndpoint,
    string Reg2RouterEndpoint,
    string Reg3RouterEndpoint,
    string ApiAEndpoint,
    string ApiBEndpoint)
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

        string Get(string name, string fallback = "") => values.TryGetValue(name, out var value)
            ? value
            : fallback;

        return new ClientOptions(
            Scenario: Get("--scenario", "cluster"),
            LogDir: Get("--log-dir", "logs"),
            Reg1Url: Get("--reg-1-url"),
            Reg2Url: Get("--reg-2-url"),
            Reg3Url: Get("--reg-3-url"),
            Reg1RouterEndpoint: Get("--reg-1-router-endpoint"),
            Reg2RouterEndpoint: Get("--reg-2-router-endpoint"),
            Reg3RouterEndpoint: Get("--reg-3-router-endpoint"),
            ApiAEndpoint: Get("--api-a-endpoint"),
            ApiBEndpoint: Get("--api-b-endpoint"));
    }
}
