using System.Net.Http.Json;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using ResilienceLifecycle.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Registry;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

using var host = Host.CreateDefaultBuilder(args)
    .ConfigureServices(services =>
    {
        services.AddZLinkFramework(framework =>
        {
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, "client-flow.log"))
                .TraceNodeId("client");
            framework.AddClientServerChannel(ResilienceLifecycleNames.Channel).EnableClient();
        });
    })
    .Build();
await host.StartAsync();
var client = host.Services.GetRequiredService<IZLinkChannelClient>();
using var http = new HttpClient();

await WaitForProvidersAsync(client, "rl-bootstrap", ["api-a", "api-b"]);
await RunRlB1Async(client, http, options);
await RunRlB4Async(client, http, options);
await RunRlB5Async(client, http, options);
await RunRlA1Async(client, http, options);
await RunRlA2Async(client, http, options);
await RunRlA3Async(options);
await RunRlA4Async(client, http, options);
await RunRlA5Async(client, http, options);
await RunRlB2Async(client, http, options);
await RunRlB3Async(client, http, options);
await RunRlB6Async(client, http, options);
await RunRlC1Async(options);
await RunRlC2Async(client, http, options);
await RunRlC3Async(client, http, options);
await RunRlC4Async(client, http, options);
await RunRlD1Async(client);
await RunRlD2Async(client, http, options);
await RunRlD3D4Async(client, http, options);
await RunRlD5Async(client);
await DynamicProcess.DisposeAllAsync();

Console.WriteLine("resilience-lifecycle e2e result=passed");
await host.StopAsync();

static async Task RunRlB1Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var slowMarker = $"rl-b1-slow-{Guid.NewGuid():N}";
    var timedOut = false;
    try
    {
        await client.RequestToChannel(
                ResilienceLifecycleNames.Channel,
                new ProfileRequest("slow", slowMarker))
            .PacketName("ProfileRequest")
            .Timeout(TimeSpan.FromMilliseconds(100))
            .Async<ProfileReply>();
    }
    catch (TimeoutException)
    {
        timedOut = true;
    }

    Ensure(timedOut, "RL-B1 expected the slow request to time out.");

    var followUp = await RequestAsync(client, "fast", $"rl-b1-follow-up-{Guid.NewGuid():N}");
    Ensure(followUp.Value == "profile:fast", "RL-B1 follow-up request failed after timeout.");

    await WaitUntilAsync(async () =>
    {
        var apiA = await ReadEvidenceAsync(http, options.ProviderAUrl);
        if (apiA.Any(line => line.Contains($"profile-request|rid=api-a|marker={slowMarker}", StringComparison.Ordinal)))
        {
            return true;
        }

        var apiB = await ReadEvidenceAsync(http, options.ProviderBUrl);
        return apiB.Any(line => line.Contains($"profile-request|rid=api-b|marker={slowMarker}", StringComparison.Ordinal));
    }, "RL-B1 slow request completion evidence missing.");

    var later = await RequestAsync(client, "fast", $"rl-b1-later-{Guid.NewGuid():N}");
    Ensure(later.Value == "profile:fast", "RL-B1 later request failed after slow completion.");
    Console.WriteLine("scenario RL-B1 passed");
}

static async Task RunRlB4Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var beforeDrain = await ReadEvidenceAsync(http, options.ProviderBUrl);
    await PostAsync(http, $"{options.ProviderBUrl}/admin/drain");
    await WaitForWeightAsync(http, options.ProviderBUrl, 0);

    for (var i = 0; i < 20; i++)
    {
        var marker = $"rl-b4-drained-{i}";
        var reply = await RequestAsync(client, "fast", marker);
        Ensure(reply.ProviderRid == "api-a", "RL-B4 drained api-b received a new request.");
    }

    var afterDrain = await ReadEvidenceAsync(http, options.ProviderBUrl);
    Ensure(CountNew(afterDrain, beforeDrain, "profile-request|rid=api-b|marker=rl-b4-drained-") == 0,
        "RL-B4 api-b evidence changed after drain.");
    var duringDrainA = await ReadEvidenceAsync(http, options.ProviderAUrl);
    Ensure(duringDrainA.Any(line => line.Contains("profile-request|rid=api-a|marker=rl-b4-drained-", StringComparison.Ordinal)),
        "RL-B4 api-a did not receive drained traffic.");

    await PostAsync(http, $"{options.ProviderBUrl}/admin/restore");
    await WaitForWeightAsync(http, options.ProviderBUrl, 100);
    await WaitUntilAsync(async () =>
    {
        var marker = $"rl-b4-restored-{Guid.NewGuid():N}";
        var reply = await RequestAsync(client, "fast", marker);
        return reply.ProviderRid == "api-b";
    }, "RL-B4 restored api-b did not re-enter routing.");

    Console.WriteLine("scenario RL-B4 passed");
}

static async Task RunRlB5Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderBUrl}/admin/restore");
    await WaitForWeightAsync(http, options.ProviderBUrl, 100);

    var slowMarker = $"rl-b5-slow-{Guid.NewGuid():N}";
    var slowTask = RequestAsync(client, "slow", slowMarker);
    var slowProvider = await WaitForSlowStartAsync(http, options, slowMarker);
    var drainedUrl = slowProvider == "api-a" ? options.ProviderAUrl : options.ProviderBUrl;
    var healthyProvider = slowProvider == "api-a" ? "api-b" : "api-a";
    var drainedPattern = $"profile-request|rid={slowProvider}|marker=rl-b5-drained-";

    var beforeDrain = await ReadEvidenceAsync(http, drainedUrl);
    await PostAsync(http, $"{drainedUrl}/admin/drain");
    await WaitForWeightAsync(http, drainedUrl, 0);

    for (var i = 0; i < 12; i++)
    {
        var reply = await RequestAsync(client, "fast", $"rl-b5-drained-{i}");
        Ensure(reply.ProviderRid == healthyProvider, "RL-B5 drain did not block new requests to the drained provider.");
    }

    var slowReply = await slowTask;
    Ensure(slowReply.ProviderRid == slowProvider && slowReply.Marker == slowMarker,
        "RL-B5 in-flight slow reply did not complete on the drained provider.");
    var afterDrain = await ReadEvidenceAsync(http, drainedUrl);
    Ensure(CountNew(afterDrain, beforeDrain, drainedPattern) == 0,
        "RL-B5 drained provider accepted new requests after drain.");
    Ensure(afterDrain.Any(line => line.Contains($"profile-request|rid={slowProvider}|marker={slowMarker}", StringComparison.Ordinal)),
        "RL-B5 in-flight completion evidence missing.");
    await PostAsync(http, $"{drainedUrl}/admin/restore");
    await WaitForWeightAsync(http, drainedUrl, 100);
    Console.WriteLine("scenario RL-B5 passed");
}

static async Task RunRlA1Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderBUrl}/shutdown");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderBUrl),
        "RL-A1 expected api-b to stop.");
    for (var i = 0; i < 12; i++)
    {
        var reply = await RequestAsync(client, "fast", $"rl-a1-down-{i}");
        Ensure(reply.ProviderRid == "api-a", "RL-A1 request during api-b restart did not use surviving provider.");
    }

    var restarted = StartProvider(options, "api-b-restart", "api-b", options.ProviderBUrl, options.ProviderBEndpoint, options.ProviderBEvidenceFile);
    await restarted.WaitReadyAsync();
    await WaitForProvidersAsync(client, "rl-a1-restored", ["api-a", "api-b"]);
    Console.WriteLine("scenario RL-A1 passed");
}

static async Task RunRlA2Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var replacementUrl = PickHttpUrl();
    var replacementEndpoint = PickEndpoint();
    await PostAsync(http, $"{options.ProviderBUrl}/shutdown");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderBUrl),
        "RL-A2 expected old api-b to stop.");
    var evidence = Path.Combine(options.LogDir, "api-b-rescheduled.evidence.log");
    var replacement = StartProvider(options, "api-b-rescheduled", "api-b", replacementUrl, replacementEndpoint, evidence);
    await replacement.WaitReadyAsync();
    await WaitUntilAsync(async () =>
    {
        var topology = await QueryTopologyAsync(options);
        return topology.Any(entry => entry.RoutingId?.ToString() == "api-b"
            && entry.Endpoint == replacementEndpoint
            && entry.State == ZLinkTopologyState.Ready);
    }, "RL-A2 replacement endpoint did not appear in topology.");
    await WaitForProvidersAsync(client, "rl-a2-rescheduled", ["api-a", "api-b"]);
    await replacement.DisposeAsync();
    var original = StartProvider(options, "api-b-after-reschedule", "api-b", options.ProviderBUrl, options.ProviderBEndpoint, options.ProviderBEvidenceFile);
    await original.WaitReadyAsync();
    await WaitForProvidersAsync(client, "rl-a2-original-restored", ["api-a", "api-b"]);
    Console.WriteLine("scenario RL-A2 passed");
}

static async Task RunRlA3Async(ClientOptions options)
{
    foreach (var batch in Enumerable.Range(0, 24).Chunk(4))
    {
        var tasks = batch.Select(async index =>
        {
            using var clientHost = CreateClientHost(options, $"storm-{index}");
            await clientHost.StartAsync();
            var channelClient = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            var reply = await RequestWithRetryAsync(channelClient, "fast", $"rl-a3-{index}", TimeSpan.FromSeconds(5));
            Ensure(reply.Value == "profile:fast", "RL-A3 storm client request failed.");
            await clientHost.StopAsync();
        });
        await Task.WhenAll(tasks);
    }

    Console.WriteLine("scenario RL-A3 passed");
}

static async Task RunRlA4Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderBUrl}/admin/drain");
    await WaitForWeightAsync(http, options.ProviderBUrl, 0);
    var greenUrl = PickHttpUrl();
    var greenEndpoint = PickEndpoint();
    var greenEvidence = Path.Combine(options.LogDir, "api-b-green.evidence.log");
    var green = StartProvider(options, "api-b-green", "api-b", greenUrl, greenEndpoint, greenEvidence);
    await green.WaitReadyAsync();

    for (var i = 0; i < 12; i++)
    {
        var reply = await RequestAsync(client, "fast", $"rl-a4-rolling-{i}");
        Ensure(reply.ProviderRid is "api-a" or "api-b", "RL-A4 rolling request failed.");
    }

    await PostAsync(http, $"{options.ProviderBUrl}/shutdown");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderBUrl),
        "RL-A4 expected old api-b to stop.");
    await WaitUntilAsync(async () =>
    {
        var topology = await QueryTopologyAsync(options);
        return topology.Any(entry => entry.RoutingId?.ToString() == "api-b"
            && entry.Endpoint == greenEndpoint
            && entry.State == ZLinkTopologyState.Ready);
    }, "RL-A4 green api-b endpoint did not become ready.");
    await WaitForProvidersAsync(client, "rl-a4-green", ["api-a", "api-b"]);
    await green.DisposeAsync();
    var original = StartProvider(options, "api-b-after-a4", "api-b", options.ProviderBUrl, options.ProviderBEndpoint, options.ProviderBEvidenceFile);
    await original.WaitReadyAsync();
    await WaitForProvidersAsync(client, "rl-a4-restored", ["api-a", "api-b"]);
    Console.WriteLine("scenario RL-A4 passed");
}

static async Task RunRlA5Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    for (var cycle = 0; cycle < 3; cycle++)
    {
        await PostAsync(http, $"{options.ProviderBUrl}/shutdown");
        await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderBUrl),
            "RL-A5 expected api-b flap down.");
        for (var i = 0; i < 4; i++)
        {
            var reply = await RequestAsync(client, "fast", $"rl-a5-down-{cycle}-{i}");
            Ensure(reply.ProviderRid == "api-a", "RL-A5 down window did not converge to api-a.");
        }

        var restarted = StartProvider(options, $"api-b-flap-{cycle}", "api-b", options.ProviderBUrl, options.ProviderBEndpoint, options.ProviderBEvidenceFile);
        await restarted.WaitReadyAsync();
        await WaitForProvidersAsync(client, $"rl-a5-up-{cycle}", ["api-a", "api-b"]);
    }

    Console.WriteLine("scenario RL-A5 passed");
}

static async Task RunRlB2Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderAUrl}/admin/drain");
    await WaitForWeightAsync(http, options.ProviderAUrl, 0);
    var marker = $"rl-b2-slow-{Guid.NewGuid():N}";
    var inFlight = RequestAsync(client, "slow", marker);
    await WaitUntilAsync(async () =>
    {
        var apiB = await ReadEvidenceAsync(http, options.ProviderBUrl);
        return apiB.Any(line => line.Contains($"profile-start|rid=api-b|marker={marker}", StringComparison.Ordinal));
    }, "RL-B2 slow request did not start on api-b.");
    await PostAsync(http, $"{options.ProviderBUrl}/admin/crash");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderBUrl),
        "RL-B2 expected api-b crash.");
    var failed = false;
    try
    {
        await inFlight;
    }
    catch
    {
        failed = true;
    }

    Ensure(failed, "RL-B2 in-flight request unexpectedly completed after provider crash.");
    await PostAsync(http, $"{options.ProviderAUrl}/admin/restore");
    await WaitForWeightAsync(http, options.ProviderAUrl, 100);
    using var survivorHost = CreateDirectClientHost(options, options.ProviderAEndpoint, "rl-b2-survivor");
    await survivorHost.StartAsync();
    var survivorClient = survivorHost.Services.GetRequiredService<IZLinkChannelClient>();
    var followUp = await RequestAsync(survivorClient, "fast", "rl-b2-after-crash");
    Ensure(followUp.ProviderRid == "api-a", "RL-B2 surviving provider traffic failed.");
    await survivorHost.StopAsync();
    var restarted = StartProvider(options, "api-b-after-b2", "api-b", options.ProviderBUrl, options.ProviderBEndpoint, options.ProviderBEvidenceFile);
    await restarted.WaitReadyAsync();
    await WaitForProvidersAsync(client, "rl-b2-restored", ["api-a", "api-b"]);
    Console.WriteLine("scenario RL-B2 passed");
}

static async Task RunRlB3Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var beforeB = await ReadEvidenceAsync(http, options.ProviderBUrl);
    var marker = $"rl-b3-before-{Guid.NewGuid():N}";
    var reply = await RequestAsync(client, "fast", marker);
    Ensure(reply.ProviderRid is "api-a" or "api-b", "RL-B3 pre-shutdown request failed.");
    await PostAsync(http, $"{options.ProviderBUrl}/shutdown");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderBUrl),
        "RL-B3 expected api-b graceful shutdown.");
    for (var i = 0; i < 12; i++)
    {
        var after = await RequestAsync(client, "fast", $"rl-b3-after-{i}");
        Ensure(after.ProviderRid == "api-a", "RL-B3 request after graceful shutdown used stale api-b.");
    }

    var afterB = File.Exists(options.ProviderBEvidenceFile) ? File.ReadAllLines(options.ProviderBEvidenceFile) : beforeB;
    Ensure(CountNew(afterB, beforeB, "profile-request|rid=api-b|marker=rl-b3-after-") == 0,
        "RL-B3 api-b received requests after graceful shutdown.");
    var restarted = StartProvider(options, "api-b-after-b3", "api-b", options.ProviderBUrl, options.ProviderBEndpoint, options.ProviderBEvidenceFile);
    await restarted.WaitReadyAsync();
    Console.WriteLine("scenario RL-B3 passed");
}

static async Task RunRlB6Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderBUrl}/admin/fault/gray");
    var failures = 0;
    var successes = 0;
    for (var i = 0; i < 30; i++)
    {
        try
        {
            var reply = await RequestAsync(client, i % 3 == 0 ? "gray" : "fast", $"rl-b6-{i}");
            if (reply.ProviderRid == "api-a")
            {
                successes++;
            }
        }
        catch (Exception)
        {
            failures++;
        }
    }

    Ensure(successes > 0 && failures > 0, "RL-B6 expected both healthy successes and gray failures.");
    await PostAsync(http, $"{options.ProviderBUrl}/admin/fault/none");
    var followUp = await RequestAsync(client, "fast", "rl-b6-after");
    Ensure(followUp.Value == "profile:fast", "RL-B6 follow-up request failed after clearing fault.");
    Console.WriteLine("scenario RL-B6 passed");
}

static async Task RunRlC1Async(ClientOptions options)
{
    foreach (var batch in Enumerable.Range(0, 12).Chunk(4))
    {
        var tasks = batch.Select(async index =>
        {
            using var clientHost = CreateClientHost(options, $"rl-c1-client-{index}");
            await clientHost.StartAsync();
            var channelClient = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
            var reply = await RequestWithRetryAsync(channelClient, "fast", $"rl-c1-{index}", TimeSpan.FromSeconds(5));
            Ensure(reply.Value == "profile:fast", "RL-C1 request failed before cleanup.");
            await clientHost.StopAsync();
        });
        await Task.WhenAll(tasks);
    }

    using var followUpHost = CreateClientHost(options, "rl-c1-follow-up");
    await followUpHost.StartAsync();
    var followUpClient = followUpHost.Services.GetRequiredService<IZLinkChannelClient>();
    var followUp = await RequestWithRetryAsync(followUpClient, "fast", "rl-c1-after-cleanup", TimeSpan.FromSeconds(5));
    Ensure(followUp.Value == "profile:fast", "RL-C1 follow-up failed after client cleanup.");
    await followUpHost.StopAsync();
    Console.WriteLine("scenario RL-C1 passed");
}

static async Task RunRlC2Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderBUrl}/admin/crash");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderBUrl),
        "RL-C2 expected api-b crash.");
    await WaitUntilAsync(async () =>
    {
        var topology = await QueryTopologyAsync(options);
        return topology.All(entry => entry.Endpoint != options.ProviderBEndpoint
            || entry.State != ZLinkTopologyState.Ready);
    }, "RL-C2 stale api-b topology entry did not leave Ready state.");
    using var recoveredDiscoveryHost = CreateClientHost(options, "rl-c2-recovered-discovery");
    await recoveredDiscoveryHost.StartAsync();
    var recoveredDiscoveryClient = recoveredDiscoveryHost.Services.GetRequiredService<IZLinkChannelClient>();
    for (var i = 0; i < 8; i++)
    {
        var reply = await RequestWithRetryAsync(
            recoveredDiscoveryClient,
            "fast",
            $"rl-c2-after-crash-{i}",
            TimeSpan.FromSeconds(5));
        Ensure(reply.ProviderRid == "api-a", "RL-C2 request used stale crashed api-b.");
    }
    await recoveredDiscoveryHost.StopAsync();

    var restarted = StartProvider(options, "api-b-after-c2", "api-b", options.ProviderBUrl, options.ProviderBEndpoint, options.ProviderBEvidenceFile);
    await restarted.WaitReadyAsync();
    await WaitForProvidersAsync(client, "rl-c2-restored", ["api-a", "api-b"]);
    Console.WriteLine("scenario RL-C2 passed");
}

static async Task RunRlC3Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderBUrl}/shutdown");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderBUrl),
        "RL-C3 expected api-b simulated node pause/down.");
    var during = await RequestAsync(client, "fast", "rl-c3-during-down");
    Ensure(during.ProviderRid == "api-a", "RL-C3 did not use surviving provider during node down.");
    var restarted = StartProvider(options, "api-b-after-c3", "api-b", options.ProviderBUrl, options.ProviderBEndpoint, options.ProviderBEvidenceFile);
    await restarted.WaitReadyAsync();
    await WaitForProvidersAsync(client, "rl-c3-recovered", ["api-a", "api-b"]);
    var topology = await QueryTopologyAsync(options);
    Ensure(topology.Count(entry => entry.RoutingId?.ToString() == "api-b"
        && entry.Endpoint == options.ProviderBEndpoint
        && entry.State == ZLinkTopologyState.Ready) == 1, "RL-C3 topology did not converge to one api-b Ready entry.");
    Console.WriteLine("scenario RL-C3 passed");
}

static async Task RunRlC4Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    using var directHost = CreateDirectClientHost(options, options.ProviderAEndpoint, "rl-c4-direct");
    await directHost.StartAsync();
    var directClient = directHost.Services.GetRequiredService<IZLinkChannelClient>();
    var before = await RequestAsync(directClient, "fast", "rl-c4-before-outage");
    Ensure(before.ProviderRid == "api-a", "RL-C4 direct client did not connect before registry outage.");
    await PostAsync(http, $"{options.RegistryUrl}/shutdown");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.RegistryUrl),
        "RL-C4 expected registry outage.");
    var during = await RequestAsync(directClient, "fast", "rl-c4-during-outage");
    Ensure(during.Value == "profile:fast", "RL-C4 existing channel failed during registry outage.");
    await directHost.StopAsync();
    var registry = StartRegistry(options, "registry-restart");
    await registry.WaitReadyAsync();
    await PostAsync(http, $"{options.ProviderAUrl}/shutdown");
    await WaitUntilAsync(async () => !await IsHealthyAsync(http, options.ProviderAUrl),
        "RL-C4 expected api-a restart after registry recovery.");
    var providerA = StartProvider(options, "api-a-after-registry-restart", "api-a", options.ProviderAUrl, options.ProviderAEndpoint, options.ProviderAEvidenceFile);
    await providerA.WaitReadyAsync();
    await WaitUntilAsync(async () => (await QueryTopologyAsync(options))
        .Count(entry => entry.ChannelName == ResilienceLifecycleNames.Channel
            && entry.State == ZLinkTopologyState.Ready) >= 1,
        "RL-C4 topology did not recover after registry restart.");
    using var recoveredHost = CreateClientHost(options, "rl-c4-recovered");
    await recoveredHost.StartAsync();
    var recoveredClient = recoveredHost.Services.GetRequiredService<IZLinkChannelClient>();
    var after = await RequestWithRetryAsync(recoveredClient, "fast", "rl-c4-after-restart", TimeSpan.FromSeconds(5));
    Ensure(after.Value == "profile:fast", "RL-C4 follow-up request failed after registry restart.");
    await recoveredHost.StopAsync();
    Console.WriteLine("scenario RL-C4 passed");
}

static async Task RunRlD1Async(IZLinkChannelClient client)
{
    var tasks = Enumerable.Range(0, 120)
        .Select(i => RequestAsync(client, "fast", $"rl-d1-{i}"));
    var replies = await Task.WhenAll(tasks);
    Ensure(replies.Length == 120 && replies.All(reply => reply.Value == "profile:fast"),
        "RL-D1 high request fanout did not complete.");
    Console.WriteLine("scenario RL-D1 passed");
}

static async Task RunRlD2Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    await PostAsync(http, $"{options.ProviderBUrl}/admin/fault/observer-throws");
    var failed = false;
    try
    {
        await client.RequestToChannel(ResilienceLifecycleNames.Channel, new ProfileRequest("fast", "rl-d2-error"))
            .PacketName("MissingProfileRequest")
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<ProfileReply>();
    }
    catch
    {
        failed = true;
    }

    Ensure(failed, "RL-D2 missing handler request should fail.");
    var followUp = await RequestAsync(client, "fast", "rl-d2-after");
    Ensure(followUp.Value == "profile:fast", "RL-D2 messaging did not continue after observer failure.");
    await PostAsync(http, $"{options.ProviderBUrl}/admin/fault/none");
    Console.WriteLine("scenario RL-D2 passed");
}

static async Task RunRlD3D4Async(IZLinkChannelClient client, HttpClient http, ClientOptions options)
{
    var failed = false;
    try
    {
        await client.RequestToChannel(ResilienceLifecycleNames.Channel, new ProfileRequest("fast", "rl-d4-missing"))
            .PacketName("MissingProfileRequest")
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<ProfileReply>();
    }
    catch (Exception ex)
    {
        failed = ex.Message.Length > 0;
    }

    Ensure(failed, "RL-D4 expected error reply exception for missing request handler.");
    await WaitUntilAsync(async () =>
    {
        var apiA = await ReadEvidenceAsync(http, options.ProviderAUrl);
        var apiB = await ReadEvidenceAsync(http, options.ProviderBUrl);
        return apiA.Concat(apiB).Any(line => line.Contains("dispatch-error|", StringComparison.Ordinal)
            && line.Contains("packet=MissingProfileRequest", StringComparison.Ordinal));
    }, "RL-D3/D4 dispatch-error marker missing.");
    Console.WriteLine("scenario RL-D3 passed");
    Console.WriteLine("scenario RL-D4 passed");
}

static async Task RunRlD5Async(IZLinkChannelClient client)
{
    var requestTasks = Enumerable.Range(0, 60)
        .Select(i => RequestAsync(client, "fast", $"rl-d5-req-{i}"));
    var sendTasks = Enumerable.Range(0, 60)
        .Select(i => client.SendToChannel(ResilienceLifecycleNames.Channel, new ProfileCommand($"rl-d5-cmd-{i}"))
            .PacketName("ProfileCommand")
            .Async().AsTask());
    var replies = await Task.WhenAll(requestTasks);
    await Task.WhenAll(sendTasks);
    Ensure(replies.All(reply => reply.Value == "profile:fast"),
        "RL-D5 mixed workload replies were invalid.");
    Console.WriteLine("scenario RL-D5 passed");
}

static async Task<string> WaitForSlowStartAsync(HttpClient http, ClientOptions options, string marker)
{
    string? provider = null;
    await WaitUntilAsync(async () =>
    {
        var apiA = await ReadEvidenceAsync(http, options.ProviderAUrl);
        if (apiA.Any(line => line.Contains($"profile-start|rid=api-a|marker={marker}", StringComparison.Ordinal)))
        {
            provider = "api-a";
            return true;
        }

        var apiB = await ReadEvidenceAsync(http, options.ProviderBUrl);
        if (apiB.Any(line => line.Contains($"profile-start|rid=api-b|marker={marker}", StringComparison.Ordinal)))
        {
            provider = "api-b";
            return true;
        }

        return false;
    }, "RL-B5 slow request did not start on either provider.");
    return provider!;
}

static async Task WaitForProvidersAsync(
    IZLinkChannelClient client,
    string markerPrefix,
    string[] expectedProviders)
{
    var seen = new HashSet<string>(StringComparer.Ordinal);
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
    var attempt = 0;
    while (!timeout.IsCancellationRequested && !expectedProviders.All(seen.Contains))
    {
        var reply = await RequestAsync(client, "fast", $"{markerPrefix}-{attempt++}");
        seen.Add(reply.ProviderRid);
        await Task.Delay(50, timeout.Token).ContinueWith(_ => { });
    }

    Ensure(expectedProviders.All(seen.Contains),
        $"Expected providers {string.Join(",", expectedProviders)}, saw {string.Join(",", seen)}.");
}

static async Task<ProfileReply> RequestAsync(IZLinkChannelClient client, string value, string marker)
{
    return await client.RequestToChannel(
            ResilienceLifecycleNames.Channel,
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
        catch (Exception ex)
        {
            last = ex;
            await Task.Delay(100);
        }
    }

    throw new InvalidOperationException($"Request retry window expired for {marker}.", last);
}

static IHost CreateClientHost(ClientOptions options, string traceNodeId)
{
    return Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            services.AddZLinkFramework(framework =>
            {
                framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
                framework.ConfigureDispatch()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(Path.Combine(options.LogDir, $"{traceNodeId}-flow.log"))
                    .TraceNodeId(traceNodeId);
                framework.AddClientServerChannel(ResilienceLifecycleNames.Channel).EnableClient();
            });
        })
        .Build();
}

static IHost CreateDirectClientHost(ClientOptions options, string endpoint, string traceNodeId)
{
    return Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            services.AddZLinkFramework(framework =>
            {
                framework.ConfigureDispatch()
                    .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                    .TraceLogFile(Path.Combine(options.LogDir, $"{traceNodeId}-flow.log"))
                    .TraceNodeId(traceNodeId);
                framework.AddClientServerChannel(ResilienceLifecycleNames.Channel)
                    .EnableClient(endpoint);
            });
        })
        .Build();
}

static DynamicProcess StartProvider(
    ClientOptions options,
    string name,
    string rid,
    string httpUrl,
    string channelEndpoint,
    string evidenceFile)
{
    return StartProcess(
        options,
        name,
        httpUrl,
        rid,
        [
            "--role", "provider",
            "--rid", rid,
            "--http-url", httpUrl,
            "--registry-router-endpoint", options.RegistryRouterEndpoint,
            "--channel-endpoint", channelEndpoint,
            "--evidence-file", evidenceFile,
            "--log-dir", options.LogDir,
        ]);
}

static DynamicProcess StartRegistry(ClientOptions options, string name)
{
    return StartProcess(
        options,
        name,
        options.RegistryUrl,
        "registry",
        [
            "--role", "registry",
            "--rid", "registry",
            "--http-url", options.RegistryUrl,
            "--registry-pub-endpoint", options.RegistryPubEndpoint,
            "--registry-router-endpoint", options.RegistryRouterEndpoint,
            "--log-dir", options.LogDir,
        ]);
}

static DynamicProcess StartProcess(
    ClientOptions options,
    string name,
    string healthUrl,
    string environmentRid,
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
    return DynamicProcess.Track(process, healthUrl);
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

static async Task PostAsync(HttpClient http, string url)
{
    using var response = await http.PostAsync(url, content: null);
    response.EnsureSuccessStatusCode();
}

static async Task<bool> IsHealthyAsync(HttpClient http, string baseUrl)
{
    try
    {
        using var response = await http.GetAsync($"{baseUrl}/health");
        return response.IsSuccessStatusCode;
    }
    catch (HttpRequestException)
    {
        return false;
    }
}

static async Task WaitForWeightAsync(HttpClient http, string baseUrl, int expected)
{
    await WaitUntilAsync(async () =>
    {
        var weight = await http.GetFromJsonAsync<WeightResponse>($"{baseUrl}/admin/weight");
        return weight?.Weight == expected;
    }, $"Expected {baseUrl} weight {expected}.");
}

static async Task<string[]> ReadEvidenceAsync(HttpClient http, string baseUrl)
{
    return await http.GetFromJsonAsync<string[]>($"{baseUrl}/evidence") ?? [];
}

static async Task<ZLinkRegistryTopologyEntry[]> QueryTopologyAsync(ClientOptions options)
{
    using var queryHost = Host.CreateDefaultBuilder()
        .ConfigureServices(services =>
        {
            services.AddZLinkRegistryQueryClient(query => query.Endpoint = options.RegistryRouterEndpoint);
        })
        .Build();
    await queryHost.StartAsync();
    var query = queryHost.Services.GetRequiredService<IZLinkRegistryQueryClient>();
    var topology = await query.TopologyAsync(
        new ZLinkRegistryTopologyFilter(ChannelName: ResilienceLifecycleNames.Channel),
        CancellationToken.None);
    await queryHost.StopAsync();
    return topology;
}

static int CountNew(string[] after, string[] before, string pattern)
{
    return after.Count(line => line.Contains(pattern, StringComparison.Ordinal))
        - before.Count(line => line.Contains(pattern, StringComparison.Ordinal));
}

static async Task WaitUntilAsync(Func<Task<bool>> condition, string failureMessage)
{
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
    while (!timeout.IsCancellationRequested)
    {
        if (await condition())
        {
            return;
        }

        await Task.Delay(100, timeout.Token).ContinueWith(_ => { });
    }

    throw new InvalidOperationException(failureMessage);
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
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    return ((IPEndPoint)listener.LocalEndpoint).Port;
}

internal sealed class DynamicProcess(Process process, string healthUrl) : IAsyncDisposable
{
    private static readonly List<DynamicProcess> All = [];
    private static readonly object Gate = new();
    private bool _disposed;

    public static DynamicProcess Track(Process process, string healthUrl)
    {
        var tracked = new DynamicProcess(process, healthUrl);
        lock (Gate)
        {
            All.Add(tracked);
        }

        return tracked;
    }

    public static async Task DisposeAllAsync()
    {
        DynamicProcess[] snapshot;
        lock (Gate)
        {
            snapshot = [.. All];
            All.Clear();
        }

        for (var i = snapshot.Length - 1; i >= 0; i--)
        {
            await snapshot[i].DisposeAsync();
        }
    }

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
        if (_disposed)
        {
            return;
        }

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
        _disposed = true;
    }
}

internal sealed record WeightResponse(int Weight);

internal sealed record ClientOptions(
    string RegistryRouterEndpoint,
    string RegistryUrl,
    string RegistryPubEndpoint,
    string ProviderAUrl,
    string ProviderBUrl,
    string ProviderAEndpoint,
    string ProviderBEndpoint,
    string ProviderAEvidenceFile,
    string ProviderBEvidenceFile,
    string ServerProject,
    string LogDir)
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

        string Get(string name) => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : throw new ArgumentException($"{name} is required.");
        return new ClientOptions(
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            RegistryUrl: Get("--registry-url"),
            RegistryPubEndpoint: Get("--registry-pub-endpoint"),
            ProviderAUrl: Get("--provider-a-url"),
            ProviderBUrl: Get("--provider-b-url"),
            ProviderAEndpoint: Get("--provider-a-endpoint"),
            ProviderBEndpoint: Get("--provider-b-endpoint"),
            ProviderAEvidenceFile: Get("--provider-a-evidence-file"),
            ProviderBEvidenceFile: Get("--provider-b-evidence-file"),
            ServerProject: Get("--server-project"),
            LogDir: Get("--log-dir"));
    }
}
