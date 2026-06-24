using System.Net.Http.Json;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
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
    await RunDrA4Async(http, options);
    await RunDrB1Async(http, options);
    await RunDrB2Async(http, options);
    await RunDrB3Async(http, options);
    await RunDrD2Async(http, options);
    await RunDrD4Async(http, options);
    await RunDrD1Async(options);
    await RunDrD3Async(http, options);
    await RunDrC1Async(http, options);
    await RunDrC2Async(http, options);
    await RunDrC3Async(http, options);
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

static async Task RunDrB1Async(HttpClient http, ClientOptions options)
{
    foreach (var registry in new[] { options.Reg2Url, options.Reg3Url })
    {
        await WaitForMembersAsync(http, registry, [options.ApiAEndpoint, options.ApiBEndpoint], expectedConnectedPeers: 2);
        using var host = CreateClientHost($"client-dr-b1-{Guid.NewGuid():N}", options.LogDir, [registry == options.Reg2Url
            ? options.Reg2RouterEndpoint
            : options.Reg3RouterEndpoint]);
        await host.StartAsync();
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();
        var reply = await RequestWithRetryAsync(client, "dr-b1", $"dr-b1-{Guid.NewGuid():N}", TimeSpan.FromSeconds(5));
        Ensure(reply.ProviderRid is "api-a" or "api-b", "DR-B1 late-start registry did not route to a provider.");
        await host.StopAsync();
    }

    Console.WriteLine("scenario DR-B1 passed");
}

static async Task RunDrA4Async(HttpClient http, ClientOptions options)
{
    var duplicateEndpoint = PickEndpoint();
    var duplicateUrl = PickHttpUrl();
    await using var duplicate = StartProvider(
        options,
        "api-a-duplicate",
        "api-a",
        duplicateUrl,
        duplicateEndpoint,
        options.Reg2RouterEndpoint);
    await duplicate.WaitReadyAsync();
    await Task.Delay(TimeSpan.FromSeconds(1));
    using var host = CreateClientHost("client-dr-a4", options.LogDir, [options.Reg2RouterEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await RequestWithRetryAsync(client, "dr-a4", "dr-a4-rid-conflict", TimeSpan.FromSeconds(5));
    Ensure(reply.ProviderRid == "api-a", "DR-A4 same-rid providers did not route as api-a.");
    await host.StopAsync();
    Console.WriteLine("scenario DR-A4 passed");
}

static async Task RunDrB2Async(HttpClient http, ClientOptions options)
{
    using var host = CreateClientHost("client-dr-b2", options.LogDir, [options.Reg1RouterEndpoint, options.Reg2RouterEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await RequestWithRetryAsync(client, "dr-b2", "dr-b2-live-registry", TimeSpan.FromSeconds(5));
    Ensure(reply.ProviderRid is "api-a" or "api-b", "DR-B2 live registry endpoint did not keep messaging available.");
    await host.StopAsync();
    Console.WriteLine("scenario DR-B2 passed");
}

static async Task RunDrB3Async(HttpClient http, ClientOptions options)
{
    for (var i = 0; i < 2; i++)
    {
        await using var reg2 = StartRegistry(options, $"reg-2-flap-{i}");
        await reg2.WaitReadyAsync();
        await WaitForMembersAsync(http, options.Reg2Url, [options.ApiAEndpoint, options.ApiBEndpoint], expectedConnectedPeers: 2);
        using var host = CreateClientHost($"client-dr-b3-{i}", options.LogDir, [options.Reg2RouterEndpoint]);
        await host.StartAsync();
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();
        var reply = await RequestWithRetryAsync(client, "dr-b3", $"dr-b3-up-{i}", TimeSpan.FromSeconds(5));
        Ensure(reply.ProviderRid is "api-a" or "api-b", "DR-B3 flapped registry did not route.");
        await host.StopAsync();
    }

    using var survivorHost = CreateClientHost("client-dr-b3-survivor", options.LogDir, [options.Reg1RouterEndpoint]);
    await survivorHost.StartAsync();
    var survivor = survivorHost.Services.GetRequiredService<IZLinkChannelClient>();
    var survivorReply = await RequestWithRetryAsync(survivor, "dr-b3", "dr-b3-survivor", TimeSpan.FromSeconds(5));
    Ensure(survivorReply.ProviderRid is "api-a" or "api-b", "DR-B3 survivor registry did not route after flapping.");
    await survivorHost.StopAsync();
    Console.WriteLine("scenario DR-B3 passed");
}

static async Task RunDrC2Async(HttpClient http, ClientOptions options)
{
    await using var reg2 = StartRegistry(options, "reg-2-recovered");
    await reg2.WaitReadyAsync();
    await WaitForMembersAsync(http, options.Reg2Url, [options.ApiAEndpoint, options.ApiBEndpoint], expectedConnectedPeers: 2);
    using var host = CreateClientHost("client-dr-c2", options.LogDir, [options.Reg2RouterEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await RequestWithRetryAsync(client, "dr-c2", "dr-c2-recovered-registry", TimeSpan.FromSeconds(5));
    Ensure(reply.ProviderRid is "api-a" or "api-b", "DR-C2 recovered registry did not route to a provider.");
    await host.StopAsync();
    Console.WriteLine("scenario DR-C2 passed");
}

static async Task RunDrC3Async(HttpClient http, ClientOptions options)
{
    using var directHost = CreateClientHost("client-dr-c3-direct", options.LogDir, [options.Reg1RouterEndpoint]);
    await directHost.StartAsync();
    var direct = directHost.Services.GetRequiredService<IZLinkChannelClient>();
    var before = await RequestWithRetryAsync(direct, "dr-c3", "dr-c3-before", TimeSpan.FromSeconds(5));
    Ensure(before.ProviderRid is "api-a" or "api-b", "DR-C3 pre-outage request failed.");

    await PostAsync(http, $"{options.Reg1Url}/shutdown");
    await PostAsync(http, $"{options.Reg2Url}/shutdown");
    await PostAsync(http, $"{options.Reg3Url}/shutdown");
    var during = await RequestWithRetryAsync(direct, "dr-c3", "dr-c3-during", TimeSpan.FromSeconds(5));
    Ensure(during.ProviderRid is "api-a" or "api-b", "DR-C3 established channel failed during registry outage.");
    await directHost.StopAsync();

    await using var reg2 = StartRegistry(options, "reg-2-after-all-outage");
    await reg2.WaitReadyAsync();
    var providerEndpoint = PickEndpoint();
    var providerUrl = PickHttpUrl();
    await using var provider = StartProvider(
        options,
        "api-c-after-all-outage",
        "api-c",
        providerUrl,
        providerEndpoint,
        options.Reg2RouterEndpoint);
    await provider.WaitReadyAsync();
    await WaitForMembersAsync(http, options.Reg2Url, [providerEndpoint], expectedConnectedPeers: 0);
    using var recoveredHost = CreateClientHost("client-dr-c3-recovered", options.LogDir, [options.Reg2RouterEndpoint]);
    await recoveredHost.StartAsync();
    var recovered = recoveredHost.Services.GetRequiredService<IZLinkChannelClient>();
    var after = await RequestWithRetryAsync(recovered, "dr-c3", "dr-c3-after", TimeSpan.FromSeconds(5));
    Ensure(after.ProviderRid == "api-c", "DR-C3 recovered registry did not route to re-advertised provider.");
    await recoveredHost.StopAsync();
    Console.WriteLine("scenario DR-C3 passed");
}

static async Task RunDrD2Async(HttpClient http, ClientOptions options)
{
    await WaitForMembersAsync(http, options.Reg1Url, [options.ApiAEndpoint, options.ApiBEndpoint], expectedConnectedPeers: 2);
    using var host = CreateClientHost("client-dr-d2", options.LogDir, [options.Reg1RouterEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await RequestWithRetryAsync(client, "dr-d2", "dr-d2-standalone", TimeSpan.FromSeconds(5));
    Ensure(reply.ProviderRid is "api-a" or "api-b", "DR-D2 standalone registry deployment did not route.");
    await host.StopAsync();
    Console.WriteLine("scenario DR-D2 passed");
}

static async Task RunDrD1Async(ClientOptions options)
{
    var pubEndpoint = PickEndpoint();
    var routerEndpoint = PickEndpoint();
    var channelEndpoint = PickEndpoint();
    var httpUrl = PickHttpUrl();
    await using var embedded = StartEmbedded(
        options,
        "embedded-dr-d1",
        "embedded-api",
        httpUrl,
        pubEndpoint,
        routerEndpoint,
        channelEndpoint,
        []);
    await embedded.WaitReadyAsync();
    using var host = CreateClientHost("client-dr-d1", options.LogDir, [routerEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await RequestWithRetryAsync(client, "dr-d1", "dr-d1-embedded", TimeSpan.FromSeconds(5));
    Ensure(reply.ProviderRid == "embedded-api", "DR-D1 embedded registry/provider did not route.");
    await host.StopAsync();
    Console.WriteLine("scenario DR-D1 passed");
}

static async Task RunDrD3Async(HttpClient http, ClientOptions options)
{
    var pubEndpoint = PickEndpoint();
    var routerEndpoint = PickEndpoint();
    var channelEndpoint = PickEndpoint();
    var httpUrl = PickHttpUrl();
    await using var embedded = StartEmbedded(
        options,
        "embedded-dr-d3",
        "embedded-api-mixed",
        httpUrl,
        pubEndpoint,
        routerEndpoint,
        channelEndpoint,
        [options.Reg1PubEndpoint]);
    await embedded.WaitReadyAsync();
    await WaitForMembersAsync(http, httpUrl, [options.ApiAEndpoint, options.ApiBEndpoint, channelEndpoint], expectedConnectedPeers: 1);
    using var host = CreateClientHost("client-dr-d3", options.LogDir, [routerEndpoint]);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();
    var reply = await RequestWithRetryAsync(client, "dr-d3", "dr-d3-mixed", TimeSpan.FromSeconds(5));
    Ensure(reply.ProviderRid is "api-a" or "api-b" or "embedded-api-mixed", "DR-D3 mixed cluster did not route.");
    await host.StopAsync();
    Console.WriteLine("scenario DR-D3 passed");
}

static async Task RunDrD4Async(HttpClient http, ClientOptions options)
{
    var inProcess = await http.GetFromJsonAsync<ZLinkRegistryTopologyEntry[]>($"{options.Reg1Url}/registry/topology") ?? [];
    using var queryHost = Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            services.AddZLinkRegistryQueryClient(query => query.Endpoint = options.Reg1RouterEndpoint);
        })
        .Build();
    await queryHost.StartAsync();
    var query = queryHost.Services.GetRequiredService<IZLinkRegistryQueryClient>();
    var remote = await query.TopologyAsync(
        new ZLinkRegistryTopologyFilter(ChannelName: DiscoveryRegistryHaNames.Channel),
        CancellationToken.None);
    await queryHost.StopAsync();

    var localSnapshot = NormalizeTopology(inProcess);
    var remoteSnapshot = NormalizeTopology(remote);
    Ensure(localSnapshot.SequenceEqual(remoteSnapshot, StringComparer.Ordinal),
        "DR-D4 in-process and remote topology snapshots differed.");
    Console.WriteLine("scenario DR-D4 passed");
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

static async Task<ProfileReply> RequestWithRetryAsync(
    IZLinkChannelClient client,
    string value,
    string marker,
    TimeSpan timeout)
{
    var deadline = DateTimeOffset.UtcNow + timeout;
    Exception? last = null;
    var attempt = 0;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            return await RequestAsync(client, value, $"{marker}-{attempt++}");
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            last = ex;
            await Task.Delay(100);
        }
    }

    throw new InvalidOperationException($"Timed out waiting for request success: {marker}.", last);
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

static DynamicProcess StartRegistry(ClientOptions options, string name)
{
    var arguments = new List<string>
    {
        "--role", "registry",
        "--rid", "reg-2",
        "--registry-id", "2",
        "--http-url", options.Reg2Url,
        "--registry-pub-endpoint", options.Reg2PubEndpoint,
        "--registry-router-endpoint", options.Reg2RouterEndpoint,
    };
    foreach (var peer in options.Reg2PeerPubEndpoints)
    {
        arguments.Add("--peer-pub-endpoint");
        arguments.Add(peer);
    }
    arguments.Add("--log-dir");
    arguments.Add(options.LogDir);
    return StartProcess(options, name, "reg-2", options.Reg2Url, arguments);
}

static DynamicProcess StartProvider(
    ClientOptions options,
    string name,
    string rid,
    string httpUrl,
    string channelEndpoint,
    string discoveryEndpoint)
{
    return StartProcess(
        options,
        name,
        rid,
        httpUrl,
        [
            "--role", "provider",
            "--rid", rid,
            "--http-url", httpUrl,
            "--channel-endpoint", channelEndpoint,
            "--discovery-endpoint", discoveryEndpoint,
            "--evidence-file", Path.Combine(options.LogDir, $"{name}.evidence.log"),
            "--log-dir", options.LogDir,
        ]);
}

static DynamicProcess StartEmbedded(
    ClientOptions options,
    string name,
    string rid,
    string httpUrl,
    string registryPubEndpoint,
    string registryRouterEndpoint,
    string channelEndpoint,
    IReadOnlyList<string> peerPubEndpoints)
{
    var arguments = new List<string>
    {
        "--role", "embedded",
        "--rid", rid,
        "--registry-id", "10",
        "--http-url", httpUrl,
        "--registry-pub-endpoint", registryPubEndpoint,
        "--registry-router-endpoint", registryRouterEndpoint,
        "--channel-endpoint", channelEndpoint,
        "--evidence-file", Path.Combine(options.LogDir, $"{name}.evidence.log"),
        "--log-dir", options.LogDir,
    };
    foreach (var peer in peerPubEndpoints)
    {
        arguments.Add("--peer-pub-endpoint");
        arguments.Add(peer);
    }

    return StartProcess(options, name, rid, httpUrl, arguments);
}

static DynamicProcess StartProcess(
    ClientOptions options,
    string name,
    string environmentRid,
    string healthUrl,
    IReadOnlyList<string> arguments)
{
    var startInfo = new ProcessStartInfo("dotnet")
    {
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
    };
    startInfo.Environment["ZLINK_E2E_RID"] = environmentRid;
    startInfo.ArgumentList.Add("run");
    startInfo.ArgumentList.Add("--project");
    startInfo.ArgumentList.Add(options.ServerProject);
    startInfo.ArgumentList.Add("--");
    foreach (var argument in arguments)
    {
        startInfo.ArgumentList.Add(argument);
    }

    var process = Process.Start(startInfo)
        ?? throw new InvalidOperationException($"Failed to start {name}.");
    _ = CopyToFileAsync(process.StandardOutput, Path.Combine(options.LogDir, $"{name}.stdout.log"));
    _ = CopyToFileAsync(process.StandardError, Path.Combine(options.LogDir, $"{name}.stderr.log"));
    return new DynamicProcess(process, healthUrl);
}

static async Task CopyToFileAsync(StreamReader reader, string path)
{
    await using var stream = File.Open(path, FileMode.Create, FileAccess.Write, FileShare.Read);
    await using var writer = new StreamWriter(stream);
    while (await reader.ReadLineAsync() is { } line)
    {
        await writer.WriteLineAsync(line);
        await writer.FlushAsync();
    }
}

static string[] NormalizeTopology(IEnumerable<ZLinkRegistryTopologyEntry> entries)
{
    return entries
        .Select(entry => string.Join("|",
            entry.ChannelName,
            entry.ServiceRole,
            entry.State,
            entry.RoutingId?.ToString() ?? "",
            entry.Endpoint))
        .Order(StringComparer.Ordinal)
        .ToArray();
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

static string PickEndpoint() => $"tcp://127.0.0.1:{PickPort()}";

static string PickHttpUrl() => $"http://127.0.0.1:{PickPort()}";

static int PickPort()
{
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    listener.Stop();
    return port;
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
    string Reg1PubEndpoint,
    string Reg2PubEndpoint,
    string[] Reg2PeerPubEndpoints,
    string ApiAEndpoint,
    string ApiBEndpoint,
    string ServerProject)
{
    public static ClientOptions Parse(string[] args)
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

        string Get(string name, string fallback = "") => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : fallback;
        string[] GetMany(string name) => values.TryGetValue(name, out var bucket) ? [.. bucket] : [];

        return new ClientOptions(
            Scenario: Get("--scenario", "cluster"),
            LogDir: Get("--log-dir", "logs"),
            Reg1Url: Get("--reg-1-url"),
            Reg2Url: Get("--reg-2-url"),
            Reg3Url: Get("--reg-3-url"),
            Reg1RouterEndpoint: Get("--reg-1-router-endpoint"),
            Reg2RouterEndpoint: Get("--reg-2-router-endpoint"),
            Reg3RouterEndpoint: Get("--reg-3-router-endpoint"),
            Reg1PubEndpoint: Get("--reg-1-pub-endpoint"),
            Reg2PubEndpoint: Get("--reg-2-pub-endpoint"),
            Reg2PeerPubEndpoints: GetMany("--reg-2-peer-pub-endpoint"),
            ApiAEndpoint: Get("--api-a-endpoint"),
            ApiBEndpoint: Get("--api-b-endpoint"),
            ServerProject: Get("--server-project"));
    }
}

internal sealed class DynamicProcess(Process process, string healthUrl) : IAsyncDisposable
{
    public async Task WaitReadyAsync()
    {
        using var http = new HttpClient();
        for (var i = 0; i < 120; i++)
        {
            if (process.HasExited)
            {
                throw new InvalidOperationException($"Process exited before readiness: {process.ExitCode}.");
            }

            try
            {
                using var response = await http.GetAsync($"{healthUrl}/health");
                if (response.IsSuccessStatusCode)
                {
                    return;
                }
            }
            catch
            {
            }

            await Task.Delay(250);
        }

        throw new TimeoutException($"Timed out waiting for {healthUrl}.");
    }

    public async ValueTask DisposeAsync()
    {
        if (!process.HasExited)
        {
            try
            {
                using var http = new HttpClient();
                using var _ = await http.PostAsync($"{healthUrl}/shutdown", content: null);
            }
            catch
            {
            }

            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            try
            {
                await process.WaitForExitAsync(timeout.Token);
            }
            catch (OperationCanceledException)
            {
                if (!process.HasExited)
                {
                    process.Kill(entireProcessTree: true);
                    await process.WaitForExitAsync();
                }
            }
        }

        process.Dispose();
    }
}
