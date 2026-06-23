using System.Net.Http.Json;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
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
            client.AddClientServerChannel("workflow").EnableClient();
        });

    await discoveryHost.StartAsync();
    var discoveryClient = discoveryHost.Services.GetRequiredService<IZLinkChannelClient>();

    await WaitForTopologyAsync(options, expectedReady: 2);
    await RunRmA1Async(options, discoveryClient);
    await RunRmC1Async(options, discoveryClient);
    await RunRmC4Async(discoveryClient);
    await RunRmC5Async(options, discoveryClient);
    await RunRmA6Async(options, discoveryClient);
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

    using var dealerMeshHost = CreateChannelClientHost(
        options,
        client =>
        {
            client.AddDealerMeshChannel("profile.mesh")
                .EnableClient(options.ProviderADealerEndpoint)
                .EnableClient(options.ProviderBDealerEndpoint);
        });

    await dealerMeshHost.StartAsync();
    await RunRmC6Async(options, dealerMeshHost.Services.GetRequiredService<IZLinkChannelClient>());
    await dealerMeshHost.StopAsync();

    if (!string.IsNullOrWhiteSpace(options.ServerProject))
    {
        await RunDynamicProviderScenariosAsync(options);
    }
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

static async Task RunRmA6Async(ClientOptions options, IZLinkChannelClient client)
{
    var profileMarker = $"rm-a6-profile-{Guid.NewGuid():N}";
    var workflowMarker = $"rm-a6-workflow-{Guid.NewGuid():N}";
    var beforeProfileA = await ReadEvidenceAsync(options.ProviderAEvidenceUrl);
    var beforeProfileB = await ReadEvidenceAsync(options.ProviderBEvidenceUrl);
    var beforeWorkflow = await ReadEvidenceAsync(options.WorkflowEvidenceUrl);

    var profileReply = await client.RequestToChannel("profile", new ProfileRequest(profileMarker))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ProfileReply>();
    Ensure(profileReply.Value == $"profile:{profileMarker}", "RM-A6 profile reply value mismatch.");
    Ensure(profileReply.ProviderRid is "api-a" or "api-b", "RM-A6 profile request reached an unexpected provider.");

    var workflowReply = await client.RequestToChannel("workflow", new WorkflowRequest(workflowMarker))
        .PacketName("WorkflowRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<WorkflowReply>();
    Ensure(workflowReply.Value == $"workflow:{workflowMarker}", "RM-A6 workflow reply value mismatch.");
    Ensure(workflowReply.ProviderRid == "workflow-a", "RM-A6 workflow request should reach workflow-a.");

    await WaitUntilAsync(
        async () =>
        {
            var afterProfileA = await ReadEvidenceAsync(options.ProviderAEvidenceUrl);
            var afterProfileB = await ReadEvidenceAsync(options.ProviderBEvidenceUrl);
            var afterWorkflow = await ReadEvidenceAsync(options.WorkflowEvidenceUrl);
            var profileHits = CountNew(afterProfileA, beforeProfileA, "profile-request|", profileMarker)
                + CountNew(afterProfileB, beforeProfileB, "profile-request|", profileMarker);
            var workflowHits = CountNew(afterWorkflow, beforeWorkflow, "workflow-request|rid=workflow-a", workflowMarker);
            var misplacedWorkflow = CountNew(afterProfileA, beforeProfileA, "workflow-request|", workflowMarker)
                + CountNew(afterProfileB, beforeProfileB, "workflow-request|", workflowMarker);
            return profileHits == 1 && workflowHits == 1 && misplacedWorkflow == 0;
        },
        "RM-A6 channel evidence showed mixed or missing profile/workflow routing.");

    Console.WriteLine("scenario RM-A6 passed");
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

static async Task RunRmC6Async(ClientOptions options, IZLinkChannelClient client)
{
    var beforeA = await ReadEvidenceAsync(options.ProviderAEvidenceUrl);
    var beforeB = await ReadEvidenceAsync(options.ProviderBEvidenceUrl);
    var marker = $"rm-c6-{Guid.NewGuid():N}";

    for (var i = 0; i < 30; i++)
    {
        var reply = await client.RequestToChannel("profile.mesh", new ProfileRequest($"{marker}-{i}"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileReply>();
        Ensure(reply.Value == $"profile:{marker}-{i}", "RM-C6 reply value mismatch.");
    }

    await WaitUntilAsync(
        async () =>
        {
            var afterA = await ReadEvidenceAsync(options.ProviderAEvidenceUrl);
            var afterB = await ReadEvidenceAsync(options.ProviderBEvidenceUrl);
            var a = CountNew(afterA, beforeA, "profile-request|rid=api-a", marker);
            var b = CountNew(afterB, beforeB, "profile-request|rid=api-b", marker);
            return a > 0 && b > 0 && a + b == 30;
        },
        "RM-C6 expected both dealer mesh providers to handle the request set.");
    Console.WriteLine("scenario RM-C6 passed");
}

static async Task RunDynamicProviderScenariosAsync(ClientOptions options)
{
    await RunRmA4Async(options);
    await RunRmB1Async(options);
    await RunRmB2Async(options);
}

static async Task RunRmA4Async(ClientOptions options)
{
    await using var cluster = await DynamicCluster.StartAsync(options, "rm-a4");
    await using var providerV1 = await cluster.StartProviderAsync("api-a-v1", "api-a");

    using var host = CreateDiscoveryClientHost(options, cluster.RegistryRouterEndpoint);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();

    await WaitForProfileReadyAsync(cluster.RegistryRouterEndpoint, 1);
    var first = await client.RequestToChannel("profile", new ProfileRequest("rm-a4-v1"))
        .PacketName("ProfileRequest")
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<ProfileReply>();
    Ensure(first.ProviderRid == "api-a", "RM-A4 initial request should reach api-a.");
    await WaitUntilAsync(
        async () => (await ReadEvidenceFileAsync(providerV1.EvidencePath))
            .Any(line => line.Contains("value=rm-a4-v1", StringComparison.Ordinal)),
        "RM-A4 v1 evidence was not recorded.");

    await providerV1.StopAsync();
    await using var providerV2 = await cluster.StartProviderAsync("api-a-v2", "api-a");
    await WaitForProfileEndpointAsync(cluster.RegistryRouterEndpoint, providerV2.ChannelEndpoint);

    var beforeV1 = await ReadEvidenceFileAsync(providerV1.EvidencePath);
    var beforeV2 = await ReadEvidenceFileAsync(providerV2.EvidencePath);
    var marker = $"rm-a4-{Guid.NewGuid():N}";
    for (var i = 0; i < 20; i++)
    {
        var reply = await client.RequestToChannel("profile", new ProfileRequest($"{marker}-{i}"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileReply>();
        Ensure(reply.ProviderRid == "api-a", "RM-A4 replacement request should reach api-a.");
    }

    await WaitUntilAsync(
        async () =>
        {
            var afterV1 = await ReadEvidenceFileAsync(providerV1.EvidencePath);
            var afterV2 = await ReadEvidenceFileAsync(providerV2.EvidencePath);
            return CountNew(afterV1, beforeV1, "profile-request|rid=api-a", marker) == 0
                && CountNew(afterV2, beforeV2, "profile-request|rid=api-a", marker) == 20;
        },
        "RM-A4 replacement provider evidence did not match.");

    await host.StopAsync();
    Console.WriteLine("scenario RM-A4 passed");
}

static async Task RunRmB1Async(ClientOptions options)
{
    await using var cluster = await DynamicCluster.StartAsync(options, "rm-b1");
    await using var providerA = await cluster.StartProviderAsync("api-a", "api-a");

    using var host = CreateDiscoveryClientHost(options, cluster.RegistryRouterEndpoint);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();

    await WaitForProfileReadyAsync(cluster.RegistryRouterEndpoint, 1);
    var beforeA = await ReadEvidenceFileAsync(providerA.EvidencePath);
    var markerBefore = $"rm-b1-before-{Guid.NewGuid():N}";
    for (var i = 0; i < 10; i++)
    {
        var reply = await client.RequestToChannel("profile", new ProfileRequest($"{markerBefore}-{i}"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileReply>();
        Ensure(reply.ProviderRid == "api-a", "RM-B1 before scale-out should reach api-a.");
    }

    await WaitUntilAsync(
        async () => CountNew(
            await ReadEvidenceFileAsync(providerA.EvidencePath),
            beforeA,
            "profile-request|rid=api-a",
            markerBefore) == 10,
        "RM-B1 pre-scale evidence was not api-a only.");

    await using var providerB = await cluster.StartProviderAsync("api-b", "api-b");
    await WaitForProfileReadyAsync(cluster.RegistryRouterEndpoint, 2);

    beforeA = await ReadEvidenceFileAsync(providerA.EvidencePath);
    var beforeB = await ReadEvidenceFileAsync(providerB.EvidencePath);
    var markerAfter = $"rm-b1-after-{Guid.NewGuid():N}";
    for (var i = 0; i < 60; i++)
    {
        var reply = await client.RequestToChannel("profile", new ProfileRequest($"{markerAfter}-{i}"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileReply>();
        Ensure(reply.ProviderRid is "api-a" or "api-b", "RM-B1 reply provider mismatch.");
    }

    await WaitUntilAsync(
        async () =>
        {
            var afterA = await ReadEvidenceFileAsync(providerA.EvidencePath);
            var afterB = await ReadEvidenceFileAsync(providerB.EvidencePath);
            var a = CountNew(afterA, beforeA, "profile-request|rid=api-a", markerAfter);
            var b = CountNew(afterB, beforeB, "profile-request|rid=api-b", markerAfter);
            return a > 0 && b > 0 && a + b == 60;
        },
        "RM-B1 expected both providers after scale-out.");

    await host.StopAsync();
    Console.WriteLine("scenario RM-B1 passed");
}

static async Task RunRmB2Async(ClientOptions options)
{
    await using var cluster = await DynamicCluster.StartAsync(options, "rm-b2");
    await using var providerA = await cluster.StartProviderAsync("api-a", "api-a");
    await using var providerB = await cluster.StartProviderAsync("api-b", "api-b");

    using var host = CreateDiscoveryClientHost(options, cluster.RegistryRouterEndpoint);
    await host.StartAsync();
    var client = host.Services.GetRequiredService<IZLinkChannelClient>();

    await WaitForProfileReadyAsync(cluster.RegistryRouterEndpoint, 2);
    var beforeA = await ReadEvidenceFileAsync(providerA.EvidencePath);
    var beforeB = await ReadEvidenceFileAsync(providerB.EvidencePath);
    var markerBefore = $"rm-b2-before-{Guid.NewGuid():N}";
    for (var i = 0; i < 40; i++)
    {
        var reply = await client.RequestToChannel("profile", new ProfileRequest($"{markerBefore}-{i}"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileReply>();
        Ensure(reply.ProviderRid is "api-a" or "api-b", "RM-B2 reply provider mismatch before scale-in.");
    }

    await WaitUntilAsync(
        async () =>
        {
            var afterA = await ReadEvidenceFileAsync(providerA.EvidencePath);
            var afterB = await ReadEvidenceFileAsync(providerB.EvidencePath);
            var a = CountNew(afterA, beforeA, "profile-request|rid=api-a", markerBefore);
            var b = CountNew(afterB, beforeB, "profile-request|rid=api-b", markerBefore);
            return a > 0 && b > 0 && a + b == 40;
        },
        "RM-B2 expected both providers before scale-in.");

    await providerB.StopAsync();
    await Task.Delay(TimeSpan.FromSeconds(1));

    beforeA = await ReadEvidenceFileAsync(providerA.EvidencePath);
    beforeB = await ReadEvidenceFileAsync(providerB.EvidencePath);
    var markerAfter = $"rm-b2-after-{Guid.NewGuid():N}";
    for (var i = 0; i < 20; i++)
    {
        var reply = await client.RequestToChannel("profile", new ProfileRequest($"{markerAfter}-{i}"))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ProfileReply>();
        Ensure(reply.ProviderRid == "api-a", "RM-B2 after scale-in should reach api-a only.");
    }

    await WaitUntilAsync(
        async () =>
        {
            var afterA = await ReadEvidenceFileAsync(providerA.EvidencePath);
            var afterB = await ReadEvidenceFileAsync(providerB.EvidencePath);
            return CountNew(afterA, beforeA, "profile-request|rid=api-a", markerAfter) == 20
                && CountNew(afterB, beforeB, "profile-request|rid=api-b", markerAfter) == 0;
        },
        "RM-B2 expected only api-a after scale-in.");

    await host.StopAsync();
    Console.WriteLine("scenario RM-B2 passed");
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

static IHost CreateDiscoveryClientHost(ClientOptions options, string registryEndpoint)
{
    return CreateChannelClientHost(
        options,
        client =>
        {
            client.UseDiscovery().AddRegistryEndpoint(registryEndpoint);
            client.AddClientServerChannel("profile").EnableClient();
        });
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
        $"Timed out waiting for {expectedReady} ready profile providers.",
        TimeSpan.FromSeconds(30));
}

static async Task WaitForProfileReadyAsync(string registryEndpoint, int expectedReady)
{
    await WaitUntilAsync(
        async () =>
        {
            var topology = await QueryTopologyFromRegistryAsync(registryEndpoint);
            return topology.Count(entry => entry.ChannelName == "profile"
                && entry.ServiceRole == ZLinkServiceRole.Router
                && entry.State == ZLinkTopologyState.Ready) == expectedReady;
        },
        $"Timed out waiting for {expectedReady} ready profile providers.");
}

static async Task WaitForProfileEndpointAsync(string registryEndpoint, string endpoint)
{
    await WaitUntilAsync(
        async () =>
        {
            var topology = await QueryTopologyFromRegistryAsync(registryEndpoint);
            var ready = topology.Where(entry => entry.ChannelName == "profile"
                && entry.ServiceRole == ZLinkServiceRole.Router
                && entry.State == ZLinkTopologyState.Ready)
                .ToArray();
            return ready.Any(entry => entry.Endpoint == endpoint);
        },
        $"Timed out waiting for profile endpoint {endpoint}.",
        TimeSpan.FromSeconds(30));
}

static async Task<ZLinkRegistryTopologyEntry[]> QueryTopologyAsync(ClientOptions options)
{
    return await QueryTopologyFromRegistryAsync(options.RegistryRouterEndpoint);
}

static async Task<ZLinkRegistryTopologyEntry[]> QueryTopologyFromRegistryAsync(string registryEndpoint)
{
    using var host = CreateRegistryQueryHostForEndpoint(registryEndpoint);
    await host.StartAsync();
    var query = host.Services.GetRequiredService<IZLinkRegistryQueryClient>();
    var topology = await query.TopologyAsync(
        new ZLinkRegistryTopologyFilter(ChannelName: "profile"),
        CancellationToken.None);
    await host.StopAsync();
    return topology;
}

static IHost CreateRegistryQueryHostForEndpoint(string registryEndpoint)
{
    var builder = Host.CreateApplicationBuilder();
    builder.Logging.ClearProviders();
    builder.Services.AddZLinkRegistryQueryClient(query => query.Endpoint = registryEndpoint);
    return builder.Build();
}

static async Task<string[]> ReadEvidenceAsync(string url)
{
    using var client = new HttpClient();
    return await client.GetFromJsonAsync<string[]>(url) ?? [];
}

static Task<string[]> ReadEvidenceFileAsync(string path)
{
    return Task.FromResult(File.Exists(path) ? File.ReadAllLines(path) : []);
}

static async Task WaitUntilAsync(
    Func<Task<bool>> condition,
    string failureMessage,
    TimeSpan? timeout = null)
{
    var deadline = DateTimeOffset.UtcNow + (timeout ?? TimeSpan.FromSeconds(10));
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
    string WorkflowEvidenceUrl,
    string LogDir,
    string? ServerProject)
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
            Require("workflow-evidence-url"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")),
            values.GetValueOrDefault("server-project"));
    }
}

internal sealed class DynamicCluster : IAsyncDisposable
{
    private readonly ClientOptions _options;
    private readonly List<DynamicProcess> _processes = new();

    private DynamicCluster(
        ClientOptions options,
        string scenarioName,
        string registryRouterEndpoint)
    {
        _options = options;
        ScenarioName = scenarioName;
        RegistryRouterEndpoint = registryRouterEndpoint;
    }

    public string ScenarioName { get; }

    public string RegistryRouterEndpoint { get; }

    public static async Task<DynamicCluster> StartAsync(ClientOptions options, string scenarioName)
    {
        var registryHttp = PickHttpUrl();
        var registryPub = PickEndpoint();
        var registryRouter = PickEndpoint();
        var cluster = new DynamicCluster(options, scenarioName, registryRouter);
        var registry = cluster.StartServer(
            $"{scenarioName}-registry",
            [
                "--role", "registry",
                "--rid", $"{scenarioName}-registry",
                "--http-url", registryHttp,
                "--registry-pub-endpoint", registryPub,
                "--registry-router-endpoint", registryRouter,
                "--log-dir", options.LogDir,
            ],
            registryHttp,
            null,
            null);
        cluster._processes.Add(registry);
        await registry.WaitReadyAsync();
        return cluster;
    }

    public async Task<DynamicProcess> StartProviderAsync(string name, string rid)
    {
        var httpUrl = PickHttpUrl();
        var channelEndpoint = PickEndpoint();
        var evidencePath = Path.Combine(_options.LogDir, $"{ScenarioName}-{name}.evidence.log");
        var process = StartServer(
            $"{ScenarioName}-{name}",
            [
                "--role", "provider",
                "--rid", rid,
                "--http-url", httpUrl,
                "--registry-router-endpoint", RegistryRouterEndpoint,
                "--channel-endpoint", channelEndpoint,
                "--route-endpoint", PickEndpoint(),
                "--dealer-endpoint", PickEndpoint(),
                "--evidence-file", evidencePath,
                "--log-dir", _options.LogDir,
            ],
            httpUrl,
            channelEndpoint,
            evidencePath);
        _processes.Add(process);
        await process.WaitReadyAsync();
        return process;
    }

    public async ValueTask DisposeAsync()
    {
        for (var i = _processes.Count - 1; i >= 0; i--)
        {
            await _processes[i].StopAsync();
        }
    }

    private DynamicProcess StartServer(
        string name,
        IReadOnlyList<string> arguments,
        string httpUrl,
        string? channelEndpoint,
        string? evidencePath)
    {
        var serverProject = _options.ServerProject
            ?? throw new InvalidOperationException("--server-project is required for dynamic scenarios.");
        var startInfo = new ProcessStartInfo
        {
            FileName = "dotnet",
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        startInfo.ArgumentList.Add("run");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(serverProject);
        startInfo.ArgumentList.Add("--");
        foreach (var argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        var process = new Process { StartInfo = startInfo };
        var stdoutPath = Path.Combine(_options.LogDir, $"{name}.stdout.log");
        var stderrPath = Path.Combine(_options.LogDir, $"{name}.stderr.log");
        process.Start();
        _ = CopyToFileAsync(process.StandardOutput, stdoutPath);
        _ = CopyToFileAsync(process.StandardError, stderrPath);
        return new DynamicProcess(process, httpUrl, channelEndpoint, evidencePath);
    }

    private static async Task CopyToFileAsync(StreamReader reader, string path)
    {
        await using var stream = File.Open(path, FileMode.Create, FileAccess.Write, FileShare.Read);
        await using var writer = new StreamWriter(stream);
        while (await reader.ReadLineAsync() is { } line)
        {
            await writer.WriteLineAsync(line);
            await writer.FlushAsync();
        }
    }

    private static string PickEndpoint()
    {
        return $"tcp://127.0.0.1:{PickPort()}";
    }

    private static string PickHttpUrl()
    {
        return $"http://127.0.0.1:{PickPort()}";
    }

    private static int PickPort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }
}

internal sealed class DynamicProcess(
    Process process,
    string httpUrl,
    string? channelEndpoint,
    string? evidencePath) : IAsyncDisposable
{
    private bool _disposed;

    public string EvidenceUrl => $"{httpUrl}/evidence";

    public string EvidencePath => evidencePath
        ?? throw new InvalidOperationException("This process does not write evidence.");

    public string ChannelEndpoint => channelEndpoint
        ?? throw new InvalidOperationException("This process does not expose a channel endpoint.");

    public async Task WaitReadyAsync()
    {
        using var client = new HttpClient();
        for (var i = 0; i < 120; i++)
        {
            if (process.HasExited)
            {
                throw new InvalidOperationException($"Process exited before readiness: {process.ExitCode}.");
            }

            try
            {
                using var response = await client.GetAsync($"{httpUrl}/health");
                if (response.IsSuccessStatusCode)
                {
                    return;
                }
            }
            catch
            {
            }

            await Task.Delay(TimeSpan.FromMilliseconds(250));
        }

        throw new TimeoutException($"Timed out waiting for {httpUrl}.");
    }

    public async Task StopAsync()
    {
        if (_disposed)
        {
            return;
        }

        if (process.HasExited)
        {
            return;
        }

        try
        {
            using var client = new HttpClient();
            using var _ = await client.PostAsync($"{httpUrl}/shutdown", content: null);
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

    public async ValueTask DisposeAsync()
    {
        if (_disposed)
        {
            return;
        }

        await StopAsync();
        process.Dispose();
        _disposed = true;
    }
}
