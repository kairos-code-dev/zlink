using System.Collections.Concurrent;
using System.Net.Http.Json;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

var options = ClientOptions.Parse(args);
await RunAsync(options);

static async Task RunAsync(ClientOptions options)
{
    await RunSmB1B2B3B5Async(options);
    await RunSmB6Async(options);
    await RunSmB8Async(options);
    await RunSmD7Async(options);
    await RunSmD8Async(options);
    await RunSmD9D11D13Async(options);
    await RunSmD1AndD6Async(options);
    await RunSmD3Async(options);
    await RunSmD4Async(options);
    await RunSmD5Async(options);
    await RunSmD12Async(options);
    await RunSmC1C2Async(options);
    await RunSmC3Async(options);
    await RunSmE1AndF4Async(options);
    await RunSmE2E3Async(options);
    await RunSmA7A8C4E4Async(options);
    await RunSmA3A6B4B7Async(options);
    await RunSmA5Async(options);
    await RunSmA1A2A4F1F2Async(options);
    Console.WriteLine("spot-service e2e result=passed");
}

static async Task RunSmB1B2B3B5Async(ClientOptions options)
{
    var playABefore = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var playBBefore = await ReadEvidenceAsync(options.PlayBEvidenceUrl);
    await using var local = CreateClient(options.SessionAStreamEndpoint);
    await local.Connect.Async();

    await local.Request(new AuthReq("actor-sm-b1-local", "local actor", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var localReply = await local.Request(new ActorPingReq("b1"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    Ensure(localReply.ActorId == "actor-sm-b1-local", "SM-B1 actor reply mismatch.");
    Ensure(localReply.NodeRid == options.PlayARid, "SM-B1 local node mismatch.");

    var complex = await local.Request(new ComplexActorReq(
            "Ada Lovelace",
            42,
            ["alpha", "beta", "gamma"],
            new Dictionary<string, string>
            {
                ["role"] = "analyst",
                ["region"] = "west"
            }))
        .PacketName("ComplexActorReq")
        .Async<ComplexActorReply>();
    Ensure(complex.ActorId == "actor-sm-b1-local", "SM-B3 actor id mismatch.");
    Ensure(complex.DisplayName == "Ada Lovelace" && complex.Level == 42, "SM-B3 scalar payload mismatch.");
    Ensure(complex.Tags.SequenceEqual(["alpha", "beta", "gamma"]), "SM-B3 tag payload mismatch.");
    Ensure(complex.Attributes["role"] == "analyst" && complex.Attributes["region"] == "west",
        "SM-B3 attribute payload mismatch.");

    await ExpectFailureAsync(
        local.Request(new ActorPingReq("missing-handler"))
            .PacketName("MissingActorReq")
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<ActorPingReply>().AsTask(),
        "SM-B5 expected missing actor handler request to fail.");

    await using var remote = CreateClient(options.SessionAStreamEndpoint);
    await remote.Connect.Async();
    await remote.Request(new AuthReq("actor-sm-b2-remote", "remote actor", options.PlayBRid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var remoteReply = await remote.Request(new ActorPingReq("b2"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    Ensure(remoteReply.ActorId == "actor-sm-b2-remote", "SM-B2 actor reply mismatch.");
    Ensure(remoteReply.NodeRid == options.PlayBRid, "SM-B2 remote node mismatch.");

    await WaitUntilAsync(async () =>
    {
        var playAAfter = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        var playBAfter = await ReadEvidenceAsync(options.PlayBEvidenceUrl);
        return CountNew(playAAfter, playABefore, "entry-created|rid=play-a|actor=actor-sm-b1-local") == 1
            && CountNew(playAAfter, playABefore, "entry-joined|rid=play-a|actor=actor-sm-b1-local") == 1
            && CountNew(playBAfter, playBBefore, "entry-created|rid=play-b|actor=actor-sm-b2-remote") == 1
            && CountNew(playBAfter, playBBefore, "entry-joined|rid=play-b|actor=actor-sm-b2-remote") == 1
            && CountNew(playAAfter, playABefore, "actor-complex|rid=play-a|actor=actor-sm-b1-local") == 1
            && CountNew(playAAfter, playABefore, "dispatch-error|surface=SpotActor|reason=HandlerMissing|action=ReplyError|packet=MissingActorReq") >= 1;
    }, "SM-B expected lifecycle, complex payload, and missing handler evidence.");

    Console.WriteLine("scenario SM-B1 passed");
    Console.WriteLine("scenario SM-B2 passed");
    Console.WriteLine("scenario SM-B3 passed");
    Console.WriteLine("scenario SM-B5 passed");
}

static async Task RunSmB6Async(ClientOptions options)
{
    var spotRid = $"spot-sm-b6-{Guid.NewGuid():N}";
    var leaveActorId = "actor-sm-b6-left";
    var disconnectActorId = "actor-sm-b6-disconnected";

    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: false,
        includeClientServerEgress: false,
        includeExternalChannelServer: false,
        traceName: "client-framework-actor-lifecycle-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    await WaitForControlRouteAsync(
        routes,
        options.PlayARid,
        "SM-B6 expected control route to become ready.");

    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(spotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();

    await using var leaveClient = CreateClient(options.SessionAStreamEndpoint);
    await leaveClient.Connect.Async();
    await leaveClient.Request(new AuthReq(leaveActorId, "leave", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var joined = await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new JoinUserSpotActorReq(spotRid, leaveActorId))
        .PacketName("JoinUserSpotActorReq")
        .Async<JoinUserSpotActorReply>();
    Ensure(joined.Accepted && joined.ActorId == leaveActorId, "SM-B6 user spot actor join failed.");

    var playABeforeLeave = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var left = await leaveClient.Request(new LeaveReq(leaveActorId))
        .PacketName("LeaveReq")
        .Async<LeaveReply>();
    Ensure(left.Accepted && left.ActorId == leaveActorId, "SM-B6 leave reply mismatch.");
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, playABeforeLeave, $"spot-actor-left|rid={options.PlayARid}|spot={spotRid}|actor={leaveActorId}") == 1
            && CountNew(after, playABeforeLeave, $"spot-actor-disconnected|rid={options.PlayARid}|spot={spotRid}|actor={leaveActorId}") == 0;
    }, "SM-B6 expected explicit leave evidence without disconnect evidence.");

    var sessionBeforeDisconnect = await ReadEvidenceAsync(options.SessionAEvidenceUrl);
    await using var disconnectClient = CreateClient(options.SessionAStreamEndpoint);
    await disconnectClient.Connect.Async();
    await disconnectClient.Request(new AuthReq(disconnectActorId, "disconnect", options.SessionARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    await disconnectClient.Close.Async();

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.SessionAEvidenceUrl);
        return CountNew(after, sessionBeforeDisconnect, $"entry-disconnected|rid={options.SessionARid}|actor={disconnectActorId}") == 1
            && CountNew(after, sessionBeforeDisconnect, $"entry-left|rid={options.SessionARid}|actor={disconnectActorId}") == 0;
    }, "SM-B6 expected disconnect evidence without leave evidence.");

    await host.StopAsync();
    Console.WriteLine("scenario SM-B6 passed");
}

static async Task RunSmB8Async(ClientOptions options)
{
    const string actorId = "actor-sm-b8-destroy";
    var before = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await using var client = CreateClient(options.SessionAStreamEndpoint);
    await client.Connect.Async();
    await client.Request(new AuthReq(actorId, "destroy", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var destroyed = await client.Request(new DestroyActorReq(actorId))
        .PacketName("DestroyActorReq")
        .Async<DestroyActorReply>();
    Ensure(destroyed.Destroyed && destroyed.ActorId == actorId, "SM-B8 destroy reply mismatch.");

    await ExpectFailureAsync(
        client.Request(new SnapshotReq(actorId))
            .PacketName("SnapshotReq")
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<SnapshotReply>().AsTask(),
        "SM-B8 expected request to destroyed actor to fail.");

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, $"actor-destroyed|rid={options.PlayARid}|actor={actorId}") == 1;
    }, "SM-B8 expected actor destroy evidence.");
    Console.WriteLine("scenario SM-B8 passed");
}

static async Task RunSmD7Async(ClientOptions options)
{
    await using var client = CreateClient(options.SessionAStreamEndpoint);
    await client.Connect.Async();
    var auth = await client.Request(new AuthReq("actor-sm-d7", "stream auth", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    Ensure(auth.ActorId == "actor-sm-d7", "SM-D7 auth reply actor mismatch.");

    var reply = await client.Request(new ActorPingReq("auth-ok"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    Ensure(reply.ActorId == "actor-sm-d7", "SM-D7 relay actor mismatch.");
    Ensure(reply.Value == "auth-ok", "SM-D7 relay value mismatch.");
    Console.WriteLine("scenario SM-D7 passed");
}

static async Task RunSmD9D11D13Async(ClientOptions options)
{
    var observed = new ConcurrentQueue<string>();
    await using var stream = CreateClient(
        options.SessionAStreamEndpoint,
        connector =>
        {
            connector.ObserveInbound((observation, cancellationToken) =>
            {
                cancellationToken.ThrowIfCancellationRequested();
                observed.Enqueue(observation.Name);
                return ValueTask.CompletedTask;
            });
        },
        heartbeat: new ZlinkStreamHeartbeatOptions
        {
            Enabled = true,
            Interval = TimeSpan.FromMilliseconds(200),
            Timeout = TimeSpan.FromSeconds(2),
        });
    await stream.Connect.Async();
    await stream.Request(new AuthReq("actor-sm-d9-d11-d13", "observer", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    await Task.Delay(600);

    var streamReply = await stream.Request(new ActorPingReq("stream-side"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    Ensure(streamReply.ActorId == "actor-sm-d9-d11-d13", "SM-D11 stream request actor mismatch.");
    Ensure(stream.IsConnected, "SM-D13 heartbeat-enabled stream disconnected.");

    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: false,
        includeClientServerEgress: false,
        includeExternalChannelServer: false,
        traceName: "client-framework-stream-channel-mix-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    var channelReply = await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new ControlPingReq("channel-side"))
        .PacketName("ControlPingReq")
        .Async<ControlPingReply>();
    Ensure(channelReply.NodeRid == options.PlayARid, "SM-D11 channel request node mismatch.");
    await host.StopAsync();

    Ensure(observed.Count >= 2, "SM-D9 inbound observer did not observe stream replies.");
    Console.WriteLine("scenario SM-D9 passed");
    Console.WriteLine("scenario SM-D11 passed");
    Console.WriteLine("scenario SM-D13 passed");
}

static async Task RunSmD8Async(ClientOptions options)
{
    const string actorId = "actor-sm-d8-reconnect";

    await using var first = CreateClient(options.SessionAStreamEndpoint);
    await first.Connect.Async();
    await first.Request(new AuthReq(actorId, "stream reconnect", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();

    var pending = first.Request(new SlowActorPingReq("before-disconnect", 1000))
        .PacketName("SlowActorPingReq")
        .Timeout(TimeSpan.FromSeconds(10))
        .Async<ActorPingReply>()
        .AsTask();
    await Task.Delay(TimeSpan.FromMilliseconds(100));
    await first.Close.Async();
    await ExpectFailureAsync(pending, "SM-D8 expected pending request to fail after stream disconnect.");
    await Task.Delay(TimeSpan.FromMilliseconds(1200));

    await using var second = CreateClient(options.SessionAStreamEndpoint);
    await second.Connect.Async();
    await second.Request(new AuthReq(actorId, "stream reconnect", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var resumed = await second.Request(new ActorPingReq("after-reconnect"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    Ensure(resumed.ActorId == actorId, "SM-D8 reconnected actor mismatch.");
    Ensure(resumed.NodeRid == options.PlayARid, "SM-D8 reconnected node mismatch.");
    Ensure(resumed.Value == "after-reconnect", "SM-D8 reconnected value mismatch.");

    Console.WriteLine("scenario SM-D8 passed");
}

static async Task RunSmD1AndD6Async(ClientOptions options)
{
    await using var bound = CreateClient(options.SessionAStreamEndpoint);
    await using var remote = CreateClient(options.SessionAStreamEndpoint);
    await using var unbound = CreateClient(options.SessionBStreamEndpoint);
    await bound.Connect.Async();
    await remote.Connect.Async();
    await unbound.Connect.Async();

    await bound.Request(new AuthReq("actor-sm-d1", "local relay", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    await remote.Request(new AuthReq("actor-sm-d2", "remote relay", options.PlayBRid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    await unbound.Request(new AuthReq("actor-sm-d1-shadow", "unbound", options.PlayBRid))
        .PacketName("AuthReq")
        .Async<AuthReply>();

    var pushed = bound.WaitFor<ActorPushNotify>().Async().AsTask();
    var reply = await bound.Request(new ActorPushReq("push-local"))
        .PacketName("ActorPushReq")
        .Async<ActorPingReply>();
    var notify = await pushed;
    Ensure(reply.ActorId == "actor-sm-d1", "SM-D1 actor reply mismatch.");
    Ensure(notify.Payload.ActorId == "actor-sm-d1", "SM-D6 push actor mismatch.");
    Ensure(notify.Payload.Value == "push-local", "SM-D6 push value mismatch.");
    Ensure(unbound.ReceivedCount("ActorPushNotify") == 0, "SM-D6 unbound session received push.");

    var remotePushed = remote.WaitFor<ActorPushNotify>().Async().AsTask();
    var remoteReply = await remote.Request(new ActorPushReq("push-remote"))
        .PacketName("ActorPushReq")
        .Async<ActorPingReply>();
    var remoteNotify = await remotePushed;
    Ensure(remoteReply.ActorId == "actor-sm-d2", "SM-D2 actor reply mismatch.");
    Ensure(remoteReply.NodeRid == options.PlayBRid, "SM-D2 remote node mismatch.");
    Ensure(remoteNotify.Payload.ActorId == "actor-sm-d2", "SM-D2 push actor mismatch.");
    Ensure(remoteNotify.Payload.Value == "push-remote", "SM-D2 push value mismatch.");
    Console.WriteLine("scenario SM-D1 passed");
    Console.WriteLine("scenario SM-D2 passed");
    Console.WriteLine("scenario SM-D6 passed");
}

static async Task RunSmD4Async(ClientOptions options)
{
    await using var client = CreateClient(options.SessionAStreamEndpoint);
    await client.Connect.Async();
    var bound = await client.Request(new MultiBindReq("actor-sm-d4-x", "actor-sm-d4-y", options.PlayARid))
        .PacketName("MultiBindReq")
        .Async<MultiBindReply>();
    Ensure(bound.BoundCount == 2, "SM-D4 expected two bound actors.");

    var x = await client.Request(new ActorPingReq("to-x"))
        .PacketName("ActorPingReq")
        .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-x")
        .Async<ActorPingReply>();
    var y = await client.Request(new ActorPingReq("to-y"))
        .PacketName("ActorPingReq")
        .Metadata(SpotServiceNames.ActorIdMetadata, "actor-sm-d4-y")
        .Async<ActorPingReply>();
    Ensure(x.ActorId == "actor-sm-d4-x" && x.Value == "to-x", "SM-D4 x relay mismatch.");
    Ensure(y.ActorId == "actor-sm-d4-y" && y.Value == "to-y", "SM-D4 y relay mismatch.");

    await ExpectFailureAsync(
        client.Request(new ActorPingReq("missing-actor-id"))
            .PacketName("ActorPingReq")
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<ActorPingReply>().AsTask(),
        "SM-D4 expected actor-id-less request to fail with multiple bound actors.");
    Console.WriteLine("scenario SM-D4 passed");
}

static async Task RunSmD3Async(ClientOptions options)
{
    await using var entry = CreateClient(options.SessionAStreamEndpoint);
    await using var user = CreateClient(options.SessionAStreamEndpoint);
    await entry.Connect.Async();
    await user.Connect.Async();

    var entryPushed = entry.WaitFor<ActorPushNotify>().Async().AsTask();
    await entry.Request(new AuthReq("actor-sm-d3-entry", "entry bind", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var entryReply = await entry.Request(new ActorPushReq("entry-push"))
        .PacketName("ActorPushReq")
        .Async<ActorPingReply>();
    var entryNotify = await entryPushed;
    Ensure(entryReply.ActorId == "actor-sm-d3-entry", "SM-D3 entry bind actor mismatch.");
    Ensure(entryReply.NodeRid == options.PlayARid, "SM-D3 entry bind node mismatch.");
    Ensure(entryNotify.Payload.ActorId == "actor-sm-d3-entry", "SM-D3 entry push actor mismatch.");
    Ensure(entryNotify.Payload.Value == "entry-push", "SM-D3 entry push value mismatch.");

    var userSpotRid = $"spot-sm-d3-user-{Guid.NewGuid():N}";
    var before = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var userPushed = user.WaitFor<ActorPushNotify>().Async().AsTask();
    await user.Request(new UserSpotAuthReq(userSpotRid, "actor-sm-d3-user", "user bind", options.PlayARid))
        .PacketName("UserSpotAuthReq")
        .Async<AuthReply>();
    var userReply = await user.Request(new ActorPingReq("user-relay"))
        .PacketName("UserActorPingReq")
        .Async<ActorPingReply>();
    var userPushReply = await user.Request(new ActorPushReq("user-push"))
        .PacketName("UserActorPushReq")
        .Async<ActorPingReply>();
    var userNotify = await userPushed;
    Ensure(userReply.ActorId == "actor-sm-d3-user", "SM-D3 user bind actor mismatch.");
    Ensure(userReply.NodeRid == options.PlayARid, "SM-D3 user bind node mismatch.");
    Ensure(userReply.SpotRid == userSpotRid, "SM-D3 user bind spot mismatch.");
    Ensure(userReply.Value == "user-relay", "SM-D3 user relay value mismatch.");
    Ensure(userPushReply.ActorId == "actor-sm-d3-user", "SM-D3 user push reply actor mismatch.");
    Ensure(userNotify.Payload.ActorId == "actor-sm-d3-user", "SM-D3 user push actor mismatch.");
    Ensure(userNotify.Payload.Value == "user-push", "SM-D3 user push value mismatch.");
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, $"spot-actor-joined|rid={options.PlayARid}|spot={userSpotRid}|actor=actor-sm-d3-user") == 1
            && CountNew(after, before, $"actor-ping|rid={options.PlayARid}|actor=actor-sm-d3-user|spot={userSpotRid}|value=user-relay") == 1;
    }, "SM-D3 expected user spot bind and relay evidence.");

    Console.WriteLine("scenario SM-D3 passed");
}

static async Task RunSmD5Async(ClientOptions options)
{
    var before = await ReadEvidenceAsync(options.SessionAEvidenceUrl);
    await using var client = CreateClient(options.SessionAStreamEndpoint);
    await client.Connect.Async();
    await client.Request(new AuthReq("actor-sm-d5-notified", "disconnect", options.SessionARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    await client.Close.Async();

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.SessionAEvidenceUrl);
        return CountNew(after, before, $"entry-disconnected|rid={options.SessionARid}|actor=actor-sm-d5-notified") == 1;
    }, "SM-D5 expected only the selected bound actor to receive disconnect notification.");
    Console.WriteLine("scenario SM-D5 passed");
}

static async Task RunSmD12Async(ClientOptions options)
{
    const string actorId = "actor-sm-d12-transfer";

    await using var first = CreateClient(options.SessionAStreamEndpoint);
    await first.Connect.Async();
    await first.Request(new AuthReq(actorId, "gateway transfer", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var firstReply = await first.Request(new ActorPingReq("before-transfer"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    Ensure(firstReply.ActorId == actorId, "SM-D12 first gateway actor mismatch.");
    Ensure(firstReply.NodeRid == options.PlayARid, "SM-D12 first gateway node mismatch.");
    Ensure(firstReply.Seen == 1, "SM-D12 expected initial actor state.");
    await first.Close.Async();

    await using var second = CreateClient(options.SessionBStreamEndpoint);
    await second.Connect.Async();
    await second.Request(new AuthReq(actorId, "gateway transfer", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var snapshot = await second.Request(new SnapshotReq(actorId))
        .PacketName("SnapshotReq")
        .Async<SnapshotReply>();
    Ensure(snapshot.ActorId == actorId, "SM-D12 snapshot actor mismatch.");
    Ensure(snapshot.Seen == 1, "SM-D12 actor state was not preserved across gateways.");

    var pushed = second.WaitFor<ActorPushNotify>().Async().AsTask();
    var resumed = await second.Request(new ActorPushReq("after-transfer"))
        .PacketName("ActorPushReq")
        .Async<ActorPingReply>();
    var notify = await pushed;
    Ensure(resumed.ActorId == actorId, "SM-D12 resumed actor mismatch.");
    Ensure(resumed.NodeRid == options.PlayARid, "SM-D12 resumed node mismatch.");
    Ensure(resumed.Seen == 2, "SM-D12 resumed actor state mismatch.");
    Ensure(notify.Payload.ActorId == actorId, "SM-D12 resumed push actor mismatch.");
    Ensure(notify.Payload.Value == "after-transfer", "SM-D12 resumed push value mismatch.");
    Ensure(notify.Payload.Seen == 2, "SM-D12 resumed push state mismatch.");
    Console.WriteLine("scenario SM-D12 passed");
}

static async Task RunSmC1C2Async(ClientOptions options)
{
    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: true,
        includeClientServerEgress: true,
        includeExternalChannelServer: false,
        traceName: "client-framework-channel-spot-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    var spotRid = $"spot-sm-c-{Guid.NewGuid():N}";

    await WaitUntilAsync(async () =>
    {
        try
        {
            var ping = await routes.Request(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(options.PlayARid),
                    new ControlPingReq("sm-c-ready"))
                .PacketName("ControlPingReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<ControlPingReply>();
            return ping.NodeRid == options.PlayARid;
        }
        catch
        {
            return false;
        }
    }, "SM-C expected control route to become ready.");

    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(spotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();

    var playABefore = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var viaChannel = await RequestSpotStateWithRetryAsync(
        routes,
        SpotServiceNames.ExternalSpotChannel,
        spotRid,
        new StateReq("noop", 0),
        "SM-C1 channel to spot request timed out.");
    Ensure(viaChannel.SpotRid == spotRid, "SM-C1 channel to spot request target mismatch.");
    await routes.Send(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new StateCommand("sm-c1-send"))
        .PacketName("StateCommand")
        .Async();
    await WaitUntilAsync(async () =>
    {
        var playAAfter = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(playAAfter, playABefore, $"spot-state-command|rid=play-a|spot={spotRid}|marker=sm-c1-send") >= 1;
    }, "SM-C1 expected channel to spot evidence.");
    await host.StopAsync();
    Console.WriteLine("scenario SM-C1 passed");

    using var outboundHost = CreateRouteEgressHost(
        options,
        includeControlChannel: false,
        includeRouteMeshEgress: true,
        includeClientServerEgress: false,
        includeExternalChannelServer: true,
        traceName: "client-framework-spot-channel-flow.log");
    await outboundHost.StartAsync();
    var outboundRoutes = outboundHost.Services.GetRequiredService<IZLinkRouteClient>();
    var clientEvidence = outboundHost.Services.GetRequiredService<ClientEvidenceStore>();
    var playABeforeOutbound = await ReadEvidenceAsync(options.PlayAEvidenceUrl);

    await outboundRoutes.Send(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new SpotOutboundReq("sm-c2"))
        .PacketName("SpotOutboundReq")
        .Async();

    await outboundRoutes.Send(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new SpotOutboundNegativeReq("sm-c2-missing"))
        .PacketName("SpotOutboundNegativeReq")
        .Async();

    await WaitUntilAsync(async () =>
    {
        var playAAfter = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        var clientAfter = clientEvidence.Snapshot();
        return CountNew(playAAfter, playABeforeOutbound, $"spot-outbound|rid=play-a|spot={spotRid}|echo=echo-sm-c2|notify=notify-sm-c2") >= 1
            && CountNew(playAAfter, playABeforeOutbound, $"spot-event|rid=play-a|spot={spotRid}|marker=sm-c2-publish") >= 1
            && CountNew(playAAfter, playABeforeOutbound, $"spot-outbound-negative|rid=play-a|spot={spotRid}|requestFailed=True") >= 1
            && clientAfter.Any(line => line.Contains("channel-echo|value=sm-c2", StringComparison.Ordinal))
            && clientAfter.Any(line => line.Contains("channel-notify|marker=notify-sm-c2", StringComparison.Ordinal))
            && clientAfter.Any(line => line.Contains("dispatch-error|surface=Channel|reason=HandlerMissing|action=ReplyError|packet=MissingChannelReq", StringComparison.Ordinal))
            && clientAfter.Any(line => line.Contains("dispatch-error|surface=Channel|reason=HandlerMissing|action=Drop|packet=MissingChannelSend", StringComparison.Ordinal));
    }, "SM-C2 expected spot to channel evidence.");

    await outboundHost.StopAsync();
    Console.WriteLine("scenario SM-C2 passed");
}

static async Task RunSmC3Async(ClientOptions options)
{
    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: true,
        includeClientServerEgress: false,
        includeExternalChannelServer: false,
        traceName: "client-framework-spot-spot-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    await WaitForControlRouteAsync(
        routes,
        options.PlayARid,
        "SM-C3 expected control route to become ready.");

    var sourceSpotRid = $"spot-sm-c3-source-{Guid.NewGuid():N}";
    var targetSpotRid = $"spot-sm-c3-target-{Guid.NewGuid():N}";
    foreach (var spotRid in new[] { sourceSpotRid, targetSpotRid })
    {
        await routes.Request(
                SpotServiceNames.ControlChannel,
                RoutingId.From(options.PlayARid),
                new CreateSpotReq(spotRid))
            .PacketName("CreateSpotReq")
            .Async<CreateSpotReply>();
    }

    var before = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var reply = await RequestSpotToSpotWithRetryAsync(
        routes,
        sourceSpotRid,
        new SpotToSpotReq(targetSpotRid, "direct"),
        "SM-C3 spot to spot request timed out.");
    Ensure(reply.SourceSpotRid == sourceSpotRid, "SM-C3 source spot mismatch.");
    Ensure(reply.TargetSpotRid == targetSpotRid, "SM-C3 target spot mismatch.");
    Ensure(reply.TargetValue == 3, "SM-C3 target state mismatch.");

    await ExpectFailureAsync(
        routes.Request(
                SpotServiceNames.ExternalSpotChannel,
                RoutingId.From(sourceSpotRid),
                new SpotToSpotReq($"missing-{targetSpotRid}", "missing"))
            .PacketName("SpotToSpotReq")
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<SpotToSpotReply>().AsTask(),
        "SM-C3 expected missing target spot request to fail.");

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, $"spot-to-spot|rid={options.PlayARid}|source={sourceSpotRid}|target={targetSpotRid}|value=3") == 1
            && CountNew(after, before, $"spot-state-command|rid={options.PlayARid}|spot={targetSpotRid}|marker=sm-c3-send-direct") == 1
            && CountNew(after, before, $"spot-event|rid={options.PlayARid}|spot={targetSpotRid}|marker=sm-c3-publish-direct") >= 1;
    }, "SM-C3 expected spot to spot evidence.");

    await host.StopAsync();
    Console.WriteLine("scenario SM-C3 passed");
}

static async Task RunSmE1AndF4Async(ClientOptions options)
{
    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: true,
        includeClientServerEgress: true,
        includeExternalChannelServer: false,
        traceName: "client-framework-negative-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    var spotRid = $"spot-sm-e1-{Guid.NewGuid():N}";

    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(spotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();

    var before = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await ExpectFailureAsync(
        routes.Request(
                SpotServiceNames.ExternalClientServerChannel,
                RoutingId.From(spotRid),
                new StateReq("noop", 0))
            .PacketName("MissingSpotReq")
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<StateReply>().AsTask(),
        "SM-E1 expected missing spot route handler request to fail.");
    await routes.Send(
            SpotServiceNames.ExternalClientServerChannel,
            RoutingId.From(spotRid),
            new StateCommand("missing-command"))
        .PacketName("MissingSpotCommand")
        .Async();
    await ExpectFailureAsync(
        routes.Request(
                SpotServiceNames.ExternalClientServerChannel,
                RoutingId.From($"missing-{spotRid}"),
                new StateReq("noop", 0))
            .PacketName("StateReq")
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<StateReply>().AsTask(),
        "SM-F4 expected missing target spot request to fail.");

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=ReplyError|packet=MissingSpotReq") >= 1
            && CountNew(after, before, "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=Drop|packet=MissingSpotCommand") >= 1;
    }, "SM-E1/F4 expected spot route negative observer evidence.");

    await host.StopAsync();
    Console.WriteLine("scenario SM-E1 passed");
    Console.WriteLine("scenario SM-F4 passed");
}

static async Task RunSmE2E3Async(ClientOptions options)
{
    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: true,
        includeClientServerEgress: false,
        includeExternalChannelServer: false,
        traceName: "client-framework-spot-timer-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    await WaitForControlRouteAsync(
        routes,
        options.PlayARid,
        "SM-E2/E3 expected control route to become ready.");

    var timerSpotRid = $"spot-sm-e2-{Guid.NewGuid():N}";
    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(timerSpotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();
    var beforeTimer = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await routes.Send(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(timerSpotRid),
            new TimerStartCommand("sm-e2-basic", 50))
        .PacketName("TimerStartCommand")
        .Async();
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, beforeTimer, $"timer-basic|rid={options.PlayARid}|spot={timerSpotRid}|name=sm-e2-basic") >= 2;
    }, "SM-E2 expected basic timer evidence.");

    var idleSpotRid = $"spot-sm-e3-{Guid.NewGuid():N}";
    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(idleSpotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();
    var beforeIdle = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await routes.Send(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(idleSpotRid),
            new IdleCloseCommand("sm-e3-idle", 50))
        .PacketName("IdleCloseCommand")
        .Async();
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, beforeIdle, $"timer-idle-close|rid={options.PlayARid}|spot={idleSpotRid}|name=sm-e3-idle|closed=True") == 1
            && CountNew(after, beforeIdle, $"spot-closing|rid={options.PlayARid}|spot={idleSpotRid}") == 1;
    }, "SM-E3 expected idle close evidence.");

    await ExpectFailureAsync(
        routes.Request(
                SpotServiceNames.ExternalSpotChannel,
                RoutingId.From(idleSpotRid),
                new StateReq("noop", 0))
            .PacketName("StateReq")
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<StateReply>().AsTask(),
        "SM-E3 expected closed spot request to fail.");

    await host.StopAsync();
    Console.WriteLine("scenario SM-E2 passed");
    Console.WriteLine("scenario SM-E3 passed");
}

static async Task RunSmA7A8C4E4Async(ClientOptions options)
{
    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: true,
        includeClientServerEgress: false,
        includeExternalChannelServer: false,
        traceName: "client-framework-spot-coverage-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    var publisher = host.Services.GetRequiredService<IZLinkSpotPublisherClient>();
    await WaitForControlRouteAsync(
        routes,
        options.PlayARid,
        "SM-A7/A8/C4/E4 expected control route to become ready.");

    var mismatchSpotRid = $"spot-sm-a7-{Guid.NewGuid():N}";
    var mismatch = await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new SpotTypeMismatchReq(mismatchSpotRid))
        .PacketName("SpotTypeMismatchReq")
        .Async<SpotTypeMismatchReply>();
    Ensure(mismatch.Failed, "SM-A7 expected SpotTypeMismatch.");
    Ensure(mismatch.ErrorKind == "SpotTypeMismatch", "SM-A7 error kind mismatch.");

    var workerSpotRid = $"spot-sm-a8-{Guid.NewGuid():N}";
    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(workerSpotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();
    var workerRouteReady = await RequestSpotStateWithRetryAsync(
        routes,
        SpotServiceNames.ExternalSpotChannel,
        workerSpotRid,
        new StateReq("noop", 0),
        "SM-A8 worker spot route did not become ready.");
    Ensure(workerRouteReady.SpotRid == workerSpotRid, "SM-A8 worker route readiness target mismatch.");
    var playABeforeWorker = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var worker = await routes.Request(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(workerSpotRid),
            new WorkerStartReq("sm-a8-worker", 5000))
        .PacketName("WorkerStartReq")
        .Timeout(TimeSpan.FromSeconds(10))
        .Async<WorkerStartReply>();
    Ensure(worker.SpotRid == workerSpotRid, "SM-A8 worker start target mismatch.");
    var duringWorker = await RequestSpotStateWithRetryAsync(
        routes,
        SpotServiceNames.ExternalSpotChannel,
        workerSpotRid,
        new StateReq("add", 1),
        "SM-A8 concurrent spot request timed out.");
    Ensure(duringWorker.Value == 1, "SM-A8 concurrent spot request did not run before worker completion.");
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, playABeforeWorker, $"worker-complete|rid={options.PlayARid}|spot={workerSpotRid}|marker=sm-a8-worker") == 1;
    }, "SM-A8 expected worker completion evidence.");
    var playAAfterWorker = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var workerStartIndex = FindIndex(playAAfterWorker, $"worker-start|rid={options.PlayARid}|spot={workerSpotRid}|marker=sm-a8-worker");
    var stateIndex = FindIndex(playAAfterWorker, $"spot-state-request|rid={options.PlayARid}|spot={workerSpotRid}|value=1");
    var workerCompleteIndex = FindIndex(playAAfterWorker, $"worker-complete|rid={options.PlayARid}|spot={workerSpotRid}|marker=sm-a8-worker");
    Ensure(workerStartIndex >= 0 && stateIndex > workerStartIndex && workerCompleteIndex > stateIndex,
        "SM-A8 expected spot request evidence between worker start and completion.");

    var publishSpotRid = $"spot-sm-c4-{Guid.NewGuid():N}";
    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(publishSpotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();
    var playABeforePublish = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await WaitUntilAsync(async () =>
    {
        await publisher.PublishSpot(
                SpotServiceNames.SpotChannel,
                SpotServiceNames.SpotEventTopic,
                new SpotEvent("sm-c4-publish"))
            .PacketName("SpotEvent")
            .Async();
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, playABeforePublish, $"spot-event|rid={options.PlayARid}|spot={publishSpotRid}|marker=sm-c4-publish") >= 1;
    }, "SM-C4 expected publish-only node event evidence.");

    var policySpots = new Dictionary<string, string>
    {
        ["SkipLateTicks"] = $"spot-sm-e4-skip-{Guid.NewGuid():N}",
        ["CatchUpBounded"] = $"spot-sm-e4-catch-{Guid.NewGuid():N}",
        ["DelayNextTick"] = $"spot-sm-e4-delay-{Guid.NewGuid():N}",
    };
    foreach (var spot in policySpots.Values)
    {
        await routes.Request(
                SpotServiceNames.ControlChannel,
                RoutingId.From(options.PlayARid),
                new CreateSpotReq(spot))
            .PacketName("CreateSpotReq")
            .Async<CreateSpotReply>();
    }

    var playABeforeTimers = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    foreach (var (policy, spot) in policySpots)
    {
        await routes.Send(
                SpotServiceNames.ExternalSpotChannel,
                RoutingId.From(spot),
                new OverrunStartCommand($"sm-e4-{policy}", policy, 25))
            .PacketName("OverrunStartCommand")
            .Async();
    }

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return policySpots.All(pair =>
            CountNew(after, playABeforeTimers, $"timer-overrun|rid={options.PlayARid}|spot={pair.Value}|name=sm-e4-{pair.Key}") >= 3);
    }, "SM-E4 expected timer overrun evidence for all policies.");
    var timerLines = (await ReadEvidenceAsync(options.PlayAEvidenceUrl))
        .Where(line => line.Contains("timer-overrun|", StringComparison.Ordinal))
        .ToArray();
    Ensure(timerLines.Where(line => line.Contains("name=sm-e4-SkipLateTicks", StringComparison.Ordinal))
        .Any(line => ExtractUInt64(line, "skipped") > 0), "SM-E4 SkipLateTicks did not skip late ticks.");
    Ensure(timerLines.Where(line => line.Contains("name=sm-e4-CatchUpBounded", StringComparison.Ordinal))
        .Any(line => ExtractUInt64(line, "skipped") > 0), "SM-E4 CatchUpBounded did not show bounded catch-up evidence.");
    Ensure(timerLines.Where(line => line.Contains("name=sm-e4-DelayNextTick", StringComparison.Ordinal))
        .Take(3)
        .All(line => ExtractUInt64(line, "skipped") == 0
            && ExtractUInt64(line, "delivery") == ExtractUInt64(line, "scheduled")),
        "SM-E4 DelayNextTick did not delay instead of skipping.");

    await host.StopAsync();
    Console.WriteLine("scenario SM-A7 passed");
    Console.WriteLine("scenario SM-A8 passed");
    Console.WriteLine("scenario SM-C4 passed");
    Console.WriteLine("scenario SM-E4 passed");
}

static async Task RunSmA3A6B4B7Async(ClientOptions options)
{
    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: true,
        includeClientServerEgress: false,
        includeExternalChannelServer: false,
        traceName: "client-framework-spot-route-lifecycle-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    await WaitForControlRouteAsync(
        routes,
        options.PlayARid,
        "SM-A3/A6 expected play-a control route to become ready.");
    await WaitForControlRouteAsync(
        routes,
        options.PlayBRid,
        "SM-B4 expected play-b control route to become ready.");

    var routedSpotRid = $"spot-sm-a3-{Guid.NewGuid():N}";
    var playABeforeRoute = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var playBBeforeRoute = await ReadEvidenceAsync(options.PlayBEvidenceUrl);
    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(routedSpotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();
    var routeReply = await RequestSpotStateWithRetryAsync(
        routes,
        SpotServiceNames.ExternalSpotChannel,
        routedSpotRid,
        new StateReq("add", 1),
        "SM-A3 routed spot request timed out.");
    Ensure(routeReply.NodeRid == options.PlayARid, "SM-A3 route resolver picked the wrong node.");
    await WaitUntilAsync(async () =>
    {
        var playAAfter = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        var playBAfter = await ReadEvidenceAsync(options.PlayBEvidenceUrl);
        return CountNew(playAAfter, playABeforeRoute, $"spot-state-request|rid={options.PlayARid}|spot={routedSpotRid}|value=1") == 1
            && CountNew(playBAfter, playBBeforeRoute, $"spot-state-request|rid={options.PlayBRid}|spot={routedSpotRid}") == 0;
    }, "SM-A3 expected only owner node evidence.");

    var lifecycleSpotRid = $"spot-sm-a6-{Guid.NewGuid():N}";
    var beforeLifecycle = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(lifecycleSpotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();
    var closed = await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CloseSpotReq(lifecycleSpotRid))
        .PacketName("CloseSpotReq")
        .Async<CloseSpotReply>();
    Ensure(closed.Closed, "SM-A6 close reply mismatch.");
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, beforeLifecycle, $"spot-initialize|rid={options.PlayARid}|spot={lifecycleSpotRid}") == 1
            && CountNew(after, beforeLifecycle, $"spot-closing|rid={options.PlayARid}|spot={lifecycleSpotRid}") == 1;
    }, "SM-A6 expected initialize and close evidence.");

    await using var remote = CreateClient(options.SessionAStreamEndpoint);
    await remote.Connect.Async();
    await remote.Request(new AuthReq("actor-sm-b4-remote", "remote actor request", options.PlayBRid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    var remoteReply = await remote.Request(new ActorPingReq("sm-b4"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    Ensure(remoteReply.NodeRid == options.PlayBRid, "SM-B4 remote actor request node mismatch.");

    var beforeOrder = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await using var ordered = CreateClient(options.SessionAStreamEndpoint);
    await ordered.Connect.Async();
    await ordered.Request(new AuthReq("actor-sm-b7-order", "order", options.PlayARid))
        .PacketName("AuthReq")
        .Async<AuthReply>();
    await ordered.Request(new ActorPingReq("order-1"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    await ordered.Request(new ActorPingReq("order-2"))
        .PacketName("ActorPingReq")
        .Async<ActorPingReply>();
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        var created = FindIndex(after, "entry-created|rid=play-a|actor=actor-sm-b7-order");
        var joined = FindIndex(after, "entry-joined|rid=play-a|actor=actor-sm-b7-order");
        var first = FindIndex(after, "actor-ping|rid=play-a|actor=actor-sm-b7-order|");
        var second = FindIndex(after, "actor-ping|rid=play-a|actor=actor-sm-b7-order|");
        if (first >= 0)
        {
            first = FindIndex(after, "value=order-1|seen=1");
            second = FindIndex(after, "value=order-2|seen=2");
        }
        return CountNew(after, beforeOrder, "entry-created|rid=play-a|actor=actor-sm-b7-order") == 1
            && created >= 0 && joined > created && first > joined && second > first;
    }, "SM-B7 expected actor lifecycle and packet order evidence.");

    await host.StopAsync();
    Console.WriteLine("scenario SM-A3 passed");
    Console.WriteLine("scenario SM-A6 passed");
    Console.WriteLine("scenario SM-B4 passed");
    Console.WriteLine("scenario SM-B7 passed");
}

static async Task RunSmA1A2A4F1F2Async(ClientOptions options)
{
    var key = $"order-sm-a4-{Guid.NewGuid():N}";
    var spotRid = $"spot-owner-{key}";

    using var clientServerHost = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: true,
        includeClientServerEgress: true,
        includeExternalChannelServer: false,
        traceName: "client-framework-flow.log");
    await clientServerHost.StartAsync();
    var clientServerRoutes = clientServerHost.Services.GetRequiredService<IZLinkRouteClient>();

    await WaitForControlRouteAsync(
        clientServerRoutes,
        options.PlayARid,
        "SM-F expected control route to become ready.");

    var created = await clientServerRoutes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(spotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();
    Ensure(created.SpotRid == spotRid, "SM-F setup created spot mismatch.");
    Ensure(created.NodeRid == options.PlayARid, "SM-A1 owner node mismatch.");

    var before = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var viaClientServer = await clientServerRoutes.Request(
            SpotServiceNames.ExternalClientServerChannel,
            RoutingId.From(spotRid),
            new StateReq("add", 7))
        .PacketName("StateReq")
        .Async<StateReply>();
    Ensure(viaClientServer.SpotRid == spotRid, "SM-F1 target spot mismatch.");
    Ensure(viaClientServer.NodeRid == options.PlayARid, "SM-F1 target node mismatch.");
    Ensure(viaClientServer.Value == 7, "SM-F1 state value mismatch.");
    Ensure(viaClientServer.NodeRid == options.PlayARid, "SM-A4 owner routing mismatch.");

    await clientServerRoutes.Send(
            SpotServiceNames.ExternalClientServerChannel,
            RoutingId.From(spotRid),
            new StateCommand("sm-f1-command"))
        .PacketName("StateCommand")
        .Async();

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, $"spot-state-command|rid=play-a|spot={spotRid}|marker=sm-f1-command") == 1;
    }, "SM-F1 expected spot route command evidence.");
    Console.WriteLine("scenario SM-F1 passed");

    var viaRouteMesh = await clientServerRoutes.Request(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new StateReq("add", 5))
        .PacketName("StateReq")
        .Async<StateReply>();
    Ensure(viaRouteMesh.SpotRid == spotRid, "SM-F2 target spot mismatch.");
    Ensure(viaRouteMesh.NodeRid == options.PlayARid, "SM-F2 target node mismatch.");
    Ensure(viaRouteMesh.Value == 12, "SM-F2 state value mismatch.");
    Ensure(viaRouteMesh.Value == 12, "SM-A2 state mutation did not preserve order.");

    await clientServerRoutes.Send(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new StateCommand("sm-f2-command"))
        .PacketName("StateCommand")
        .Async();

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, $"spot-state-command|rid=play-a|spot={spotRid}|marker=sm-f2-command") == 1;
    }, "SM-F2 expected spot route command evidence.");

    await clientServerHost.StopAsync();
    Console.WriteLine("scenario SM-A1 passed");
    Console.WriteLine("scenario SM-A2 passed");
    Console.WriteLine("scenario SM-A4 passed");
    Console.WriteLine("scenario SM-F2 passed");

    using var routeMeshOnlyHost = CreateRouteEgressHost(
        options,
        includeControlChannel: false,
        includeRouteMeshEgress: true,
        includeClientServerEgress: false,
        includeExternalChannelServer: false,
        traceName: "client-framework-spot-route-ownership-flow.log");
    await routeMeshOnlyHost.StartAsync();
    var routeMeshOnlyRoutes = routeMeshOnlyHost.Services.GetRequiredService<IZLinkRouteClient>();
    var beforeRouteOnly = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var routeOnly = await routeMeshOnlyRoutes.Request(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new StateReq("add", 2))
        .PacketName("StateReq")
        .Async<StateReply>();
    Ensure(routeOnly.Value == 14, "SM-F5 route mesh did not survive client/server host stop.");
    await routeMeshOnlyRoutes.Send(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new StateCommand("sm-f3-mixed-route-command"))
        .PacketName("StateCommand")
        .Async();
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, beforeRouteOnly, $"spot-state-command|rid=play-a|spot={spotRid}|marker=sm-f3-mixed-route-command") == 1;
    }, "SM-F3 expected mixed route command evidence.");
    await routeMeshOnlyHost.StopAsync();
    Console.WriteLine("scenario SM-F3 passed");
    Console.WriteLine("scenario SM-F5 passed");
}

static async Task RunSmA5Async(ClientOptions options)
{
    using var host = CreateRouteEgressHost(
        options,
        includeControlChannel: true,
        includeRouteMeshEgress: true,
        includeClientServerEgress: false,
        includeExternalChannelServer: false,
        traceName: "client-framework-stage-wrapper-flow.log");
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    await WaitForControlRouteAsync(
        routes,
        options.PlayARid,
        "SM-A5 expected play-a control route to become ready.");

    var spotRid = $"spot-sm-a5-{Guid.NewGuid():N}";
    var before = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(spotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();

    var reply = await routes.Request(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new StageProbeReq("sm-a5-stage", 9))
        .PacketName("StageProbeReq")
        .Async<StageProbeReply>();
    Ensure(reply.SpotRid == spotRid, "SM-A5 stage wrapper returned the wrong spot.");
    Ensure(reply.NodeRid == options.PlayARid, "SM-A5 stage wrapper returned the wrong node.");
    Ensure(reply.Value == 9, "SM-A5 stage wrapper state mutation mismatch.");
    Ensure(reply.Marker == "sm-a5-stage", "SM-A5 stage wrapper marker mismatch.");

    await routes.Send(
            SpotServiceNames.ExternalSpotChannel,
            RoutingId.From(spotRid),
            new StageTimerStartCommand("sm-a5-stage-timer", 50))
        .PacketName("StageTimerStartCommand")
        .Async();
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, $"spot-initialize|rid={options.PlayARid}|spot={spotRid}") == 1
            && CountNew(after, before, $"stage-request|rid={options.PlayARid}|spot={spotRid}|marker=sm-a5-stage|value=9") == 1
            && CountNew(after, before, $"stage-timer|rid={options.PlayARid}|spot={spotRid}|name=sm-a5-stage-timer") >= 1;
    }, "SM-A5 expected stage wrapper request/timer evidence.");

    var closed = await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CloseSpotReq(spotRid))
        .PacketName("CloseSpotReq")
        .Async<CloseSpotReply>();
    Ensure(closed.Closed, "SM-A5 stage wrapper close reply mismatch.");
    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, $"spot-closing|rid={options.PlayARid}|spot={spotRid}") == 1;
    }, "SM-A5 expected stage wrapper lifecycle close evidence.");

    await host.StopAsync();
    Console.WriteLine("scenario SM-A5 passed");
}

static IHost CreateRouteEgressHost(
    ClientOptions options,
    bool includeControlChannel,
    bool includeRouteMeshEgress,
    bool includeClientServerEgress,
    bool includeExternalChannelServer,
    string traceName)
{
    var builder = Host.CreateApplicationBuilder();
    builder.Services.AddSingleton<ClientEvidenceStore>();
    builder.Services.AddSingleton<IZLinkMessageDispatchErrorObserver, ClientEvidenceDispatchErrorObserver>();
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.AddHandlersFromAssemblyOf(typeof(Program));
        framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
        framework.ConfigureDispatch()
            .SetMessageDispatchErrorObserver<ClientEvidenceDispatchErrorObserver>()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, traceName))
            .TraceNodeId("client-framework");
        var spotMesh = framework.AddSpotMesh(SpotServiceNames.SpotChannel)
            .UseRegistrySpotResolver();
        spotMesh.AddNode(SpotServiceNames.EdgeSpotNode)
            .EnableRouter(options.ClientSpotRouterEndpoint)
            .SetRouterRoutingId(RoutingId.From("client-edge"));
        spotMesh.AddNode(SpotServiceNames.EdgePublisherNode)
            .EnablePubSub(options.ClientSpotPubEndpoint)
            .SetPubSubRoutingId(RoutingId.From("client-edge-publisher"))
            .AttachSpotPublisherClient(
                SpotServiceNames.SpotChannel,
                options.PlayASpotPubEndpoint);
        if (includeControlChannel)
        {
            framework.AddRouteMeshChannel(SpotServiceNames.ControlChannel)
                .EnableServer(options.ClientControlEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From("client-control"));
        }
        if (includeRouteMeshEgress)
        {
            framework.AddRouteMeshChannel(SpotServiceNames.ExternalSpotChannel)
                .EnableServer(options.ClientExternalRouteEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From("client-external-route"))
                .EnableSpotRouteEgress(SpotServiceNames.ExternalSpotChannel);
        }
        if (includeClientServerEgress)
        {
            framework.AddClientServerChannel(SpotServiceNames.ExternalClientServerChannel)
                .EnableClient(options.PlayAExternalSpotEndpoint)
                .EnableSpotRouteEgress(SpotServiceNames.ExternalSpotChannel);
        }
        if (includeExternalChannelServer)
        {
            framework.AddClientServerChannel(SpotServiceNames.ExternalClientChannel)
                .EnableServer(options.ClientExternalChannelEndpoint)
                .AddHandlerGroup("client");
        }
    });
    return builder.Build();
}

static IZlinkStreamConnector CreateClient(
    string endpoint,
    Action<IZlinkStreamConnector>? configure = null,
    ZlinkStreamHeartbeatOptions? heartbeat = null)
{
    var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
    {
        Endpoint = new Uri(endpoint),
        ConnectTimeout = TimeSpan.FromSeconds(5),
        RequestTimeout = TimeSpan.FromSeconds(5),
        Heartbeat = heartbeat ?? new ZlinkStreamHeartbeatOptions { Enabled = false },
        DispatchMode = ZlinkStreamDispatchMode.Immediate,
    });
    configure?.Invoke(connector);
    return connector;
}

static async Task<string[]> ReadEvidenceAsync(string url)
{
    using var client = new HttpClient();
    return await client.GetFromJsonAsync<string[]>(url) ?? [];
}

static int CountNew(string[] after, string[] before, string pattern)
{
    var beforeCount = before.Count(line => line.Contains(pattern, StringComparison.Ordinal));
    var afterCount = after.Count(line => line.Contains(pattern, StringComparison.Ordinal));
    return afterCount - beforeCount;
}

static int FindIndex(string[] lines, string pattern)
{
    return Array.FindIndex(lines, line => line.Contains(pattern, StringComparison.Ordinal));
}

static ulong ExtractUInt64(string line, string key)
{
    var prefix = key + "=";
    var start = line.IndexOf(prefix, StringComparison.Ordinal);
    if (start < 0)
    {
        throw new InvalidOperationException($"Missing field '{key}' in evidence line: {line}");
    }

    start += prefix.Length;
    var end = line.IndexOf('|', start);
    var value = end < 0 ? line[start..] : line[start..end];
    return ulong.Parse(value);
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
        catch (Exception ex)
        {
            last = ex;
        }

        await Task.Delay(TimeSpan.FromMilliseconds(100));
    }

    throw new InvalidOperationException(failureMessage, last);
}

static async Task ExpectFailureAsync(Task task, string message)
{
    try
    {
        await task;
    }
    catch
    {
        return;
    }

    throw new InvalidOperationException(message);
}

static async Task<StateReply> RequestSpotStateWithRetryAsync(
    IZLinkRouteClient routes,
    string channelName,
    string spotRid,
    StateReq request,
    string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    TimeoutException? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            return await routes.Request(
                    channelName,
                    RoutingId.From(spotRid),
                    request)
                .PacketName("StateReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<StateReply>();
        }
        catch (TimeoutException ex)
        {
            last = ex;
        }
    }

    throw new TimeoutException(failureMessage, last);
}

static async Task<SpotToSpotReply> RequestSpotToSpotWithRetryAsync(
    IZLinkRouteClient routes,
    string sourceSpotRid,
    SpotToSpotReq request,
    string failureMessage)
{
    var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
    TimeoutException? last = null;
    while (DateTimeOffset.UtcNow < deadline)
    {
        try
        {
            return await routes.Request(
                    SpotServiceNames.ExternalSpotChannel,
                    RoutingId.From(sourceSpotRid),
                    request)
                .PacketName("SpotToSpotReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<SpotToSpotReply>();
        }
        catch (TimeoutException ex)
        {
            last = ex;
        }
    }

    throw new TimeoutException(failureMessage, last);
}

static async Task WaitForControlRouteAsync(
    IZLinkRouteClient routes,
    string nodeRid,
    string failureMessage)
{
    await WaitUntilAsync(async () =>
    {
        try
        {
            var ping = await routes.Request(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(nodeRid),
                    new ControlPingReq("ready"))
                .PacketName("ControlPingReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<ControlPingReply>();
            return ping.NodeRid == nodeRid;
        }
        catch
        {
            return false;
        }
    }, failureMessage);
}

static void Ensure(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

internal sealed record ClientOptions(
    string SessionAStreamEndpoint,
    string SessionBStreamEndpoint,
    string RegistryRouterEndpoint,
    string PlayAEvidenceUrl,
    string PlayBEvidenceUrl,
    string SessionAEvidenceUrl,
    string PlayARid,
    string PlayBRid,
    string SessionARid,
    string PlayAExternalSpotEndpoint,
    string PlayASpotPubEndpoint,
    string ClientControlEndpoint,
    string ClientExternalRouteEndpoint,
    string ClientExternalChannelEndpoint,
    string ClientSpotRouterEndpoint,
    string ClientSpotPubEndpoint,
    string LogDir)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {args[i]}.");
            }

            values[args[i][2..]] = args[++i];
        }

        string Required(string key) => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");

        return new ClientOptions(
            Required("session-a-stream-endpoint"),
            Required("session-b-stream-endpoint"),
            Required("registry-router-endpoint"),
            Required("play-a-evidence-url"),
            Required("play-b-evidence-url"),
            Required("session-a-evidence-url"),
            Required("play-a-rid"),
            Required("play-b-rid"),
            Required("session-a-rid"),
            Required("play-a-external-spot-endpoint"),
            Required("play-a-spot-pub-endpoint"),
            Required("client-control-endpoint"),
            Required("client-external-route-endpoint"),
            Required("client-external-channel-endpoint"),
            Required("client-spot-router-endpoint"),
            Required("client-spot-pub-endpoint"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-spot-e2e")));
    }
}

[ZLinkHandlerGroup("client")]
internal sealed class ChannelEchoHandler(ClientEvidenceStore evidence)
    : IZLinkRequestHandler<ChannelEchoReq, ChannelEchoReply>
{
    public ValueTask<ChannelEchoReply> HandleAsync(
        ChannelEchoReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"channel-echo|value={request.Value}");
        return ValueTask.FromResult(new ChannelEchoReply($"echo-{request.Value}"));
    }
}

[ZLinkHandlerGroup("client")]
internal sealed class ChannelNotifyHandler(ClientEvidenceStore evidence)
    : IZLinkSendHandler<ChannelNotify>
{
    public ValueTask HandleAsync(
        ChannelNotify message,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"channel-notify|marker={message.Marker}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ClientEvidenceDispatchErrorObserver(ClientEvidenceStore evidence)
    : IZLinkMessageDispatchErrorObserver
{
    public ValueTask OnDispatchErrorAsync(
        ZLinkMessageDispatchErrorEvent error,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            "dispatch-error"
            + $"|surface={error.Surface}"
            + $"|reason={error.Reason}"
            + $"|action={error.Action}"
            + $"|packet={error.PacketName ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ClientEvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();

    public void Add(string entry) => _entries.Enqueue(entry);

    public string[] Snapshot() => _entries.ToArray();
}
