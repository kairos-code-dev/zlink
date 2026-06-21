using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.HttpClient;
using static Zlink.Framework.ScenarioE2E.Scenario;

namespace Zlink.Framework.ScenarioE2E;

// E2E doc chapter 2 — channel messaging. Each method is a self-contained client
// flow against the shared scenario server, written to read like a sample: it
// connects through ScenarioClient, then drives the public channel/route client
// and asserts with Ensure. The shared server is already up (the runner gates on
// its ready file).
internal static class Ch02ChannelMessaging
{
    // CH-001 — a request to a registered handler returns its reply, and the
    // server records the request id as evidence.
    public static async Task RequestResponse(ScenarioContext ctx)
    {
        using var host = await ScenarioClient.ConnectChannelAsync(ctx);
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();

        var reply = await client
            .RequestToChannel(ctx.Channel, new ScenarioProfileRequest("ch-001-request-1", "alice"))
            .PacketName("ScenarioProfileRequest")
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<ScenarioProfileReply>();

        Ensure(reply.RequestId == "ch-001-request-1");
        Ensure(reply.Value == "profile:alice");

        using var serverApi = ZLinkHttpClient.Create(ctx.HttpEndpoints[0])
            .Json()
            .Timeout(TimeSpan.FromSeconds(3))
            .Build();
        var evidence = (await serverApi.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
        Ensure(evidence.RequestIds.Contains("ch-001-request-1"));

        await host.StopAsync();
    }

    // CH-002 — the client registers three server endpoints itself; 90 requests
    // round-robin evenly to 30/30/30, confirmed by both replies and per-server
    // evidence.
    public static async Task ManualEndpointRoundRobin(ScenarioContext ctx)
    {
        using var host = await ScenarioClient.ConnectChannelAsync(ctx);
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();

        // Warm up so every endpoint has finished connecting before we measure.
        for (var index = 0; index < 3; index++)
        {
            var warmup = await client
                .RequestToChannel(ctx.Channel, new ScenarioProfileRequest($"ch-002-warmup-{index}", $"profile-warmup-{index}"))
                .PacketName("ScenarioProfileRequest")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<ScenarioProfileReply>();
            Ensure(warmup.RequestId == $"ch-002-warmup-{index}");
        }

        var repliesByServer = new Dictionary<string, int>(StringComparer.Ordinal);
        for (var index = 0; index < 90; index++)
        {
            var reply = await client
                .RequestToChannel(ctx.Channel, new ScenarioProfileRequest($"ch-002-{index}", $"profile-{index}"))
                .PacketName("ScenarioProfileRequest")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<ScenarioProfileReply>();
            Ensure(reply.RequestId == $"ch-002-{index}");
            Ensure(reply.Value == $"profile:profile-{index}");
            Ensure(!string.IsNullOrWhiteSpace(reply.ServerId));
            repliesByServer[reply.ServerId] = repliesByServer.GetValueOrDefault(reply.ServerId) + 1;
        }

        foreach (var serverName in ctx.ServerNames)
        {
            Ensure(repliesByServer.GetValueOrDefault(serverName) == 30);
        }

        for (var index = 0; index < ctx.ServerNames.Count; index++)
        {
            using var serverApi = ZLinkHttpClient.Create(ctx.HttpEndpoints[index])
                .Json()
                .Timeout(TimeSpan.FromSeconds(3))
                .Build();
            var evidence = (await serverApi.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
            var measured = evidence.RequestIds
                .Count(requestId => requestId.StartsWith("ch-002-", StringComparison.Ordinal)
                    && !requestId.StartsWith("ch-002-warmup-", StringComparison.Ordinal));
            Ensure(evidence.ServerId == ctx.ServerNames[index]);
            Ensure(measured == 30);
        }

        await host.StopAsync();
    }

    // CH-004 — on a route mesh the client is itself a routing node. A request
    // addressed to peer-b reaches only peer-b; an unknown routing id fails; the
    // non-target peer never sees the request.
    public static async Task RouteMeshTargetedRequest(ScenarioContext ctx)
    {
        using var host = await ScenarioClient.ConnectRouteMeshAsync(ctx);
        var routeClient = host.Services.GetRequiredService<IZLinkRouteClient>();

        var reply = await routeClient
            .Request(ctx.Channel, RoutingId.From("peer-b"), new ScenarioRoutePingRequest("ch-004-target-1", "ping"))
            .PacketName("ScenarioRoutePing")
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<ScenarioRoutePingReply>();

        Ensure(reply.RequestId == "ch-004-target-1");
        Ensure(reply.Value == "route:ping");
        Ensure(reply.TargetRid == "peer-b");
        Ensure(reply.SourceRid == "peer-a");

        // An unknown routing id has no connected peer, so the request fails.
        var unknownFailed = false;
        try
        {
            _ = await routeClient
                .Request(ctx.Channel, RoutingId.From("peer-missing"), new ScenarioRoutePingRequest("ch-004-target-1-missing", "ping"))
                .PacketName("ScenarioRoutePing")
                .Timeout(TimeSpan.FromMilliseconds(100))
                .Async<ScenarioRoutePingReply>();
        }
        catch (Exception)
        {
            unknownFailed = true;
        }

        Ensure(unknownFailed);

        var targetIndex = ctx.ServerNames.ToList().IndexOf("peer-b");
        var nonTargetIndex = ctx.ServerNames.ToList().IndexOf("peer-c");
        Ensure(targetIndex >= 0 && nonTargetIndex >= 0);

        using var targetApi = ZLinkHttpClient.Create(ctx.HttpEndpoints[targetIndex])
            .Json()
            .Timeout(TimeSpan.FromSeconds(3))
            .Build();
        var targetReached = false;
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline && !targetReached)
        {
            var evidence = (await targetApi.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
            targetReached = evidence.RouteRequestIds.Contains("ch-004-target-1")
                && evidence.RouteSourceRids.Contains("peer-a");
            if (!targetReached)
            {
                await Task.Delay(50);
            }
        }

        Ensure(targetReached);

        using var nonTargetApi = ZLinkHttpClient.Create(ctx.HttpEndpoints[nonTargetIndex])
            .Json()
            .Timeout(TimeSpan.FromSeconds(3))
            .Build();
        var nonTarget = (await nonTargetApi.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
        Ensure(!nonTarget.RouteRequestIds.Contains("ch-004-target-1"));

        await host.StopAsync();
    }

    // CH-006 — a one-way send has no reply, but the server's send handler records
    // the command id as evidence.
    public static async Task SendOneWay(ScenarioContext ctx)
    {
        using var host = await ScenarioClient.ConnectChannelAsync(ctx);
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();

        await client
            .SendToChannel(ctx.Channel, new ScenarioProfileCommand("ch-006-command-1", "refresh"))
            .PacketName("ScenarioProfileCommand")
            .Async();

        using var serverApi = ZLinkHttpClient.Create(ctx.HttpEndpoints[0])
            .Json()
            .Timeout(TimeSpan.FromSeconds(3))
            .Build();
        var recorded = false;
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline && !recorded)
        {
            var evidence = (await serverApi.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
            recorded = evidence.CommandIds.Contains("ch-006-command-1");
            if (!recorded)
            {
                await Task.Delay(50);
            }
        }

        Ensure(recorded);

        await host.StopAsync();
    }

    // CH-007 — a slow request that the client abandons on timeout must not poison
    // the connection: a concurrent normal request, the next request, and a request
    // after the late reply drains all succeed.
    public static async Task RequestTimeoutLateReply(ScenarioContext ctx)
    {
        using var host = await ScenarioClient.ConnectChannelAsync(ctx);
        var client = host.Services.GetRequiredService<IZLinkChannelClient>();

        // A slow request times out on the client while the handler keeps running.
        var firstTimedOut = false;
        try
        {
            _ = await client
                .RequestToChannel(ctx.Channel, new ScenarioProfileRequest("ch-007-timeout-1", "slow"))
                .PacketName("ScenarioProfileRequest")
                .Timeout(TimeSpan.FromMilliseconds(300))
                .Async<ScenarioProfileReply>();
        }
        catch (TimeoutException)
        {
            firstTimedOut = true;
        }

        Ensure(firstTimedOut);

        // A second slow request times out while a normal request issued mid-flight
        // still completes correctly.
        var concurrentTimeout = client
            .RequestToChannel(ctx.Channel, new ScenarioProfileRequest("ch-007-concurrent-timeout-1", "slow"))
            .PacketName("ScenarioProfileRequest")
            .Timeout(TimeSpan.FromMilliseconds(300))
            .Async<ScenarioProfileReply>()
            .AsTask();

        await Task.Delay(100);

        var concurrentOk = await client
            .RequestToChannel(ctx.Channel, new ScenarioProfileRequest("ch-007-concurrent-ok-1", "concurrent-ok"))
            .PacketName("ScenarioProfileRequest")
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<ScenarioProfileReply>();
        Ensure(concurrentOk.RequestId == "ch-007-concurrent-ok-1");
        Ensure(concurrentOk.Value == "profile:concurrent-ok");

        var concurrentTimedOut = false;
        try
        {
            _ = await concurrentTimeout;
        }
        catch (TimeoutException)
        {
            concurrentTimedOut = true;
        }

        Ensure(concurrentTimedOut);

        var afterTimeout = await client
            .RequestToChannel(ctx.Channel, new ScenarioProfileRequest("ch-007-after-timeout-1", "after-timeout"))
            .PacketName("ScenarioProfileRequest")
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<ScenarioProfileReply>();
        Ensure(afterTimeout.Value == "profile:after-timeout");

        // Let the abandoned slow handlers reply late; the client must discard them.
        await Task.Delay(1200);

        var afterLateReply = await client
            .RequestToChannel(ctx.Channel, new ScenarioProfileRequest("ch-007-after-late-reply-1", "after-late-reply"))
            .PacketName("ScenarioProfileRequest")
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<ScenarioProfileReply>();
        Ensure(afterLateReply.RequestId == "ch-007-after-late-reply-1");
        Ensure(afterLateReply.Value == "profile:after-late-reply");

        // Even the abandoned requests eventually complete on the server side.
        string[] expected =
        [
            "ch-007-timeout-1",
            "ch-007-concurrent-timeout-1",
            "ch-007-concurrent-ok-1",
            "ch-007-after-timeout-1",
            "ch-007-after-late-reply-1",
        ];
        using var serverApi = ZLinkHttpClient.Create(ctx.HttpEndpoints[0])
            .Json()
            .Timeout(TimeSpan.FromSeconds(3))
            .Build();
        var allCompleted = false;
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline && !allCompleted)
        {
            var evidence = (await serverApi.Get("/evidence").SubmitAsync<ScenarioEvidenceSnapshot>()).Body;
            allCompleted = expected.All(evidence.RequestIds.Contains)
                && expected.All(evidence.CompletedRequestIds.Contains);
            if (!allCompleted)
            {
                await Task.Delay(50);
            }
        }

        Ensure(allCompleted);

        await host.StopAsync();
    }
}
