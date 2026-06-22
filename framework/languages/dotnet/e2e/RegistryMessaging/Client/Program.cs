using System.Net.Http.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using RegistryMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Registry;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

await RunAsync(options);
Console.WriteLine("e2e result=passed");

static async Task RunAsync(ClientOptions options)
{
    using var discoveryHost = CreateChannelClientHost(
        options,
        client =>
        {
            client.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
            client.AddClientServerChannel("profile").EnableClient();
        });

    await discoveryHost.StartAsync();
    var discoveryClient = discoveryHost.Services.GetRequiredService<IZLinkChannelClient>();

    await WaitForTopologyAsync(options, expectedReady: 2);
    await RunRmA1Async(options, discoveryClient);
    await RunRmC1Async(options, discoveryClient);
    await RunRmC4Async(discoveryClient);
    await RunRmC5Async(options, discoveryClient);
    await discoveryHost.StopAsync();

    using var manualHost = CreateChannelClientHost(
        options,
        client =>
        {
            client.AddClientServerChannel("profile")
                .EnableClient(options.ProviderAEndpoint);
        });

    await manualHost.StartAsync();
    await RunRmA2Async(manualHost.Services.GetRequiredService<IZLinkChannelClient>());
    await manualHost.StopAsync();

    using var multiHost = CreateChannelClientHost(
        options,
        client =>
        {
            client.AddClientServerChannel("profile")
                .EnableClient(options.ProviderAEndpoint)
                .EnableClient(options.ProviderBEndpoint);
        });

    await multiHost.StartAsync();
    await RunRmC3Async(options, multiHost.Services.GetRequiredService<IZLinkChannelClient>());
    await multiHost.StopAsync();

    using var routeHost = CreateRouteClientHost(options);
    await routeHost.StartAsync();
    await RunRmC2Async(options, routeHost.Services.GetRequiredService<IZLinkRouteClient>());
    await routeHost.StopAsync();

}

static async Task RunRmA1Async(ClientOptions options, IZLinkChannelClient client)
{
    var reply = await client.RequestToChannel("profile", new ProfileRequest("rm-a1"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ProfileReply>();

    Ensure(reply.Value == "profile:rm-a1", "RM-A1 reply value mismatch.");
    Ensure(reply.ProviderRid is "api-a" or "api-b", "RM-A1 provider rid was not api-a/api-b.");

    var topology = await QueryTopologyAsync(options);
    var ready = topology
        .Where(entry => entry.ChannelName == "profile"
            && entry.ServiceRole == ZLinkServiceRole.Router
            && entry.State == ZLinkTopologyState.Ready)
        .Select(entry => entry.RoutingId?.ToString())
        .ToArray();
    Ensure(ready.Length >= 2, "RM-A1 expected two ready profile providers in topology.");
    Console.WriteLine("scenario RM-A1 passed");
}

static async Task RunRmA2Async(IZLinkChannelClient client)
{
    var reply = await client.RequestToChannel("profile", new ProfileRequest("rm-a2"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ProfileReply>();

    Ensure(reply.Value == "profile:rm-a2", "RM-A2 reply value mismatch.");
    Ensure(reply.ProviderRid == "api-a", "RM-A2 manual endpoint should reach api-a.");
    Console.WriteLine("scenario RM-A2 passed");
}

static async Task RunRmC1Async(ClientOptions options, IZLinkChannelClient client)
{
    var requestReply = await client.RequestToChannel("profile", new ProfileRequest("rm-c1-request"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ProfileReply>();
    Ensure(requestReply.Value == "profile:rm-c1-request", "RM-C1 request reply mismatch.");

    var commandId = $"cmd-{Guid.NewGuid():N}";
    await client.SendToChannel("profile", new ProfileCommand(commandId))
        .PacketName("ProfileCommand")
        .Async();

    await WaitUntilAsync(
        async () => (await ReadEvidenceAsync(options.ProviderAEvidenceUrl))
            .Concat(await ReadEvidenceAsync(options.ProviderBEvidenceUrl))
            .Any(line => line.Contains($"profile-command|", StringComparison.Ordinal)
                && line.Contains($"command={commandId}", StringComparison.Ordinal)),
        "RM-C1 send evidence was not recorded.");
    Console.WriteLine("scenario RM-C1 passed");
}

static async Task RunRmC4Async(IZLinkChannelClient client)
{
    var timeoutObserved = false;
    try
    {
        await client.RequestToChannel("profile", new ProfileRequest("slow"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromMilliseconds(100))
            .Async<ProfileReply>();
    }
    catch (Exception)
    {
        timeoutObserved = true;
    }

    Ensure(timeoutObserved, "RM-C4 expected the slow request to time out.");

    var immediate = await client.RequestToChannel("profile", new ProfileRequest("rm-c4-after-timeout"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ProfileReply>();
    Ensure(immediate.Value == "profile:rm-c4-after-timeout", "RM-C4 follow-up reply mismatch.");

    await Task.Delay(TimeSpan.FromMilliseconds(1200));
    var later = await client.RequestToChannel("profile", new ProfileRequest("rm-c4-later"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ProfileReply>();
    Ensure(later.Value == "profile:rm-c4-later", "RM-C4 later reply mismatch.");
    Console.WriteLine("scenario RM-C4 passed");
}

static async Task RunRmC5Async(ClientOptions options, IZLinkChannelClient client)
{
    var requestFailed = false;
    try
    {
        await client.RequestToChannel("profile", new ProfileRequest("missing-request"))
            .PacketName("MissingProfileRequest")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileReply>();
    }
    catch (Exception)
    {
        requestFailed = true;
    }

    Ensure(requestFailed, "RM-C5 missing request should fail.");

    await client.SendToChannel("profile", new ProfileCommand("missing-send"))
        .PacketName("MissingProfileCommand")
        .Async();

    await WaitUntilAsync(
        async () =>
        {
            var evidence = (await ReadEvidenceAsync(options.ProviderAEvidenceUrl))
                .Concat(await ReadEvidenceAsync(options.ProviderBEvidenceUrl))
                .ToArray();
            return evidence.Any(line => line.Contains("dispatch-error", StringComparison.Ordinal)
                && line.Contains("packet=MissingProfileRequest", StringComparison.Ordinal))
                && evidence.Any(line => line.Contains("dispatch-error", StringComparison.Ordinal)
                    && line.Contains("packet=MissingProfileCommand", StringComparison.Ordinal));
        },
        "RM-C5 dispatch-error evidence was not recorded.");

    var reply = await client.RequestToChannel("profile", new ProfileRequest("rm-c5-after"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ProfileReply>();
    Ensure(reply.Value == "profile:rm-c5-after", "RM-C5 normal request after negative path failed.");
    Console.WriteLine("scenario RM-C5 passed");
}

static async Task RunRmC2Async(ClientOptions options, IZLinkRouteClient client)
{
    var beforeA = await ReadEvidenceAsync(options.ProviderAEvidenceUrl);
    var beforeB = await ReadEvidenceAsync(options.ProviderBEvidenceUrl);
    var marker = $"rm-c2-{Guid.NewGuid():N}";

    var reply = await client.Request("profile.route", RoutingId.From("api-b"), new ScenarioRoutePing(marker))
        .PacketName("ScenarioRoutePing")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ScenarioRoutePong>();
    Ensure(reply.ProviderRid == "api-b", "RM-C2 targeted route request should reach api-b.");
    Ensure(reply.Value == $"route:{marker}", "RM-C2 route reply value mismatch.");

    await WaitUntilAsync(
        async () =>
        {
            var afterA = await ReadEvidenceAsync(options.ProviderAEvidenceUrl);
            var afterB = await ReadEvidenceAsync(options.ProviderBEvidenceUrl);
            return CountNew(afterB, beforeB, $"route-request|rid=api-b", marker) == 1
                && CountNew(afterA, beforeA, $"route-request|rid=api-a", marker) == 0;
        },
        "RM-C2 targeted route evidence did not match api-b only.");

    var missingFailed = false;
    try
    {
        await client.Request("profile.route", RoutingId.From("missing-rid"), new ScenarioRoutePing("missing"))
            .PacketName("ScenarioRoutePing")
            .Timeout(TimeSpan.FromMilliseconds(300))
            .Async<ScenarioRoutePong>();
    }
    catch (Exception)
    {
        missingFailed = true;
    }

    Ensure(missingFailed, "RM-C2 missing rid request should fail.");
    Console.WriteLine("scenario RM-C2 passed");
}

static async Task RunRmC3Async(ClientOptions options, IZLinkChannelClient client)
{
    var beforeA = await ReadEvidenceAsync(options.ProviderAEvidenceUrl);
    var beforeB = await ReadEvidenceAsync(options.ProviderBEvidenceUrl);
    var marker = $"rm-c3-{Guid.NewGuid():N}";

    for (var i = 0; i < 60; i++)
    {
        var reply = await client.RequestToChannel("profile", new ProfileRequest($"{marker}-{i}"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileReply>();
        Ensure(reply.Value == $"profile:{marker}-{i}", "RM-C3 reply value mismatch.");
    }

    await WaitUntilAsync(
        async () =>
        {
            var afterA = await ReadEvidenceAsync(options.ProviderAEvidenceUrl);
            var afterB = await ReadEvidenceAsync(options.ProviderBEvidenceUrl);
            var a = CountNew(afterA, beforeA, "profile-request|rid=api-a", marker);
            var b = CountNew(afterB, beforeB, "profile-request|rid=api-b", marker);
            return a > 0 && b > 0 && a + b == 60;
        },
        "RM-C3 expected both providers to handle the direct multi-endpoint request set.");
    Console.WriteLine("scenario RM-C3 passed");
}

static IHost CreateChannelClientHost(
    ClientOptions options,
    Action<Zlink.Framework.Contracts.Configuration.IZLinkFrameworkOptions> configure)
{
    var builder = Host.CreateApplicationBuilder();
    builder.Logging.ClearProviders();
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.ConfigureDispatch()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, "client-flow.log"))
            .TraceNodeId("client");
        configure(framework);
    });
    return builder.Build();
}

static IHost CreateRouteClientHost(ClientOptions options)
{
    var builder = Host.CreateApplicationBuilder();
    builder.Logging.ClearProviders();
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.ConfigureDispatch()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, "client-route-flow.log"))
            .TraceNodeId("client-route");
        framework.AddRouteMeshChannel("profile.route")
            .EnableServer(options.ClientRouteEndpoint)
            .EnableClient(options.ProviderARouteEndpoint)
            .EnableClient(options.ProviderBRouteEndpoint)
            .SetRoutingId(RoutingId.From("client"));
    });
    return builder.Build();
}

static async Task WaitForTopologyAsync(ClientOptions options, int expectedReady)
{
    await WaitUntilAsync(
        async () =>
        {
            var topology = await QueryTopologyAsync(options);
            return topology.Count(entry => entry.ChannelName == "profile"
                && entry.ServiceRole == ZLinkServiceRole.Router
                && entry.State == ZLinkTopologyState.Ready) >= expectedReady;
        },
        $"Timed out waiting for {expectedReady} ready profile providers.");
}

static async Task<ZLinkRegistryTopologyEntry[]> QueryTopologyAsync(ClientOptions options)
{
    using var host = CreateRegistryQueryHost(options);
    await host.StartAsync();
    var query = host.Services.GetRequiredService<IZLinkRegistryQueryClient>();
    var topology = await query.TopologyAsync(
        new ZLinkRegistryTopologyFilter(ChannelName: "profile"),
        CancellationToken.None);
    await host.StopAsync();
    return topology;
}

static IHost CreateRegistryQueryHost(ClientOptions options)
{
    var builder = Host.CreateApplicationBuilder();
    builder.Logging.ClearProviders();
    builder.Services.AddZLinkRegistryQueryClient(query => query.Endpoint = options.RegistryRouterEndpoint);
    return builder.Build();
}

static async Task<string[]> ReadEvidenceAsync(string url)
{
    using var client = new HttpClient();
    return await client.GetFromJsonAsync<string[]>(url) ?? [];
}

static async Task WaitUntilAsync(Func<Task<bool>> condition, string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
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
        catch (Exception ex)
        {
            last = ex;
        }

        await Task.Delay(TimeSpan.FromMilliseconds(100));
    }

    throw new InvalidOperationException(failureMessage, last);
}

static void Ensure(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

static int CountNew(string[] after, string[] before, string prefix, string marker)
{
    var oldCount = before.Count(line => line.Contains(prefix, StringComparison.Ordinal)
        && line.Contains(marker, StringComparison.Ordinal));
    var newCount = after.Count(line => line.Contains(prefix, StringComparison.Ordinal)
        && line.Contains(marker, StringComparison.Ordinal));
    return newCount - oldCount;
}

internal sealed record ClientOptions(
    string RegistryRouterEndpoint,
    string ProviderAEndpoint,
    string ProviderBEndpoint,
    string ProviderARouteEndpoint,
    string ProviderBRouteEndpoint,
    string ProviderADealerEndpoint,
    string ProviderBDealerEndpoint,
    string ClientRouteEndpoint,
    string ProviderAEvidenceUrl,
    string ProviderBEvidenceUrl,
    string LogDir)
{
    public static ClientOptions Parse(string[] args)
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

        string Require(string key) => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");

        return new ClientOptions(
            Require("registry-router-endpoint"),
            Require("provider-a-endpoint"),
            Require("provider-b-endpoint"),
            Require("provider-a-route-endpoint"),
            Require("provider-b-route-endpoint"),
            Require("provider-a-dealer-endpoint"),
            Require("provider-b-dealer-endpoint"),
            Require("client-route-endpoint"),
            Require("provider-a-evidence-url"),
            Require("provider-b-evidence-url"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")));
    }
}
