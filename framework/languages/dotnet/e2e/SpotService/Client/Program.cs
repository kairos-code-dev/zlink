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

var options = ClientOptions.Parse(args);
await RunAsync(options);

static async Task RunAsync(ClientOptions options)
{
    await RunSmD7Async(options);
    await RunSmD1AndD6Async(options);
    await RunSmD4Async(options);
    await RunSmD5Async(options);
    await RunSmF1F2Async(options);
    Console.WriteLine("spot-service e2e result=passed");
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

static async Task RunSmD1AndD6Async(ClientOptions options)
{
    await using var bound = CreateClient(options.SessionAStreamEndpoint);
    await using var unbound = CreateClient(options.SessionBStreamEndpoint);
    await bound.Connect.Async();
    await unbound.Connect.Async();

    await bound.Request(new AuthReq("actor-sm-d1", "local relay", options.PlayARid))
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
    Console.WriteLine("scenario SM-D1 passed");
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

static async Task RunSmD5Async(ClientOptions options)
{
    var before = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    await using (var client = CreateClient(options.SessionAStreamEndpoint))
    {
        await client.Connect.Async();
        await client.Request(new AuthReq("actor-sm-d5-notified", "disconnect", options.PlayARid))
            .PacketName("AuthReq")
            .Async<AuthReply>();
    }

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, "entry-disconnected|rid=play-a|actor=actor-sm-d5-notified") == 1;
    }, "SM-D5 expected only the selected bound actor to receive disconnect notification.");
    Console.WriteLine("scenario SM-D5 passed");
}

static async Task RunSmF1F2Async(ClientOptions options)
{
    using var host = CreateRouteEgressHost(options);
    await host.StartAsync();
    var routes = host.Services.GetRequiredService<IZLinkRouteClient>();
    var spotRid = $"spot-sm-f-{Guid.NewGuid():N}";

    var created = await routes.Request(
            SpotServiceNames.ControlChannel,
            RoutingId.From(options.PlayARid),
            new CreateSpotReq(spotRid))
        .PacketName("CreateSpotReq")
        .Async<CreateSpotReply>();
    Ensure(created.SpotRid == spotRid, "SM-F setup created spot mismatch.");

    var before = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
    var viaClientServer = await routes.Request(
            SpotServiceNames.ExternalClientServerChannel,
            RoutingId.From(spotRid),
            new StateReq("add", 7))
        .PacketName("StateReq")
        .Async<StateReply>();
    Ensure(viaClientServer.SpotRid == spotRid, "SM-F1 target spot mismatch.");
    Ensure(viaClientServer.NodeRid == options.PlayARid, "SM-F1 target node mismatch.");
    Ensure(viaClientServer.Value == 7, "SM-F1 state value mismatch.");

    await routes.Send(
            SpotServiceNames.ExternalClientServerChannel,
            RoutingId.From(spotRid),
            new StateCommand("sm-f1-command"))
        .PacketName("StateCommand")
        .Async();

    var viaRouteMesh = await routes.Request(
            SpotServiceNames.ExternalClientChannel,
            RoutingId.From(spotRid),
            new StateReq("add", 5))
        .PacketName("StateReq")
        .Async<StateReply>();
    Ensure(viaRouteMesh.SpotRid == spotRid, "SM-F2 target spot mismatch.");
    Ensure(viaRouteMesh.NodeRid == options.PlayARid, "SM-F2 target node mismatch.");
    Ensure(viaRouteMesh.Value == 12, "SM-F2 state value mismatch.");

    await routes.Send(
            SpotServiceNames.ExternalClientChannel,
            RoutingId.From(spotRid),
            new StateCommand("sm-f2-command"))
        .PacketName("StateCommand")
        .Async();

    await WaitUntilAsync(async () =>
    {
        var after = await ReadEvidenceAsync(options.PlayAEvidenceUrl);
        return CountNew(after, before, $"spot-state-command|rid=play-a|spot={spotRid}|marker=sm-f1-command") == 1
            && CountNew(after, before, $"spot-state-command|rid=play-a|spot={spotRid}|marker=sm-f2-command") == 1;
    }, "SM-F expected spot route command evidence.");

    await host.StopAsync();
    Console.WriteLine("scenario SM-F1 passed");
    Console.WriteLine("scenario SM-F2 passed");
}

static IHost CreateRouteEgressHost(ClientOptions options)
{
    var builder = Host.CreateApplicationBuilder();
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
        framework.ConfigureDispatch()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, "client-framework-flow.log"))
            .TraceNodeId("client-framework");
        framework.AddRouteMeshChannel(SpotServiceNames.ControlChannel)
            .EnableServer(options.ClientControlEndpoint)
            .EnableClient()
            .SetRoutingId(RoutingId.From("client-control"));
        framework.AddRouteMeshChannel(SpotServiceNames.ExternalClientChannel)
            .EnableServer(options.ClientExternalRouteEndpoint)
            .EnableClient()
            .SetRoutingId(RoutingId.From("client-external-route"))
            .EnableSpotRouteEgress(SpotServiceNames.ExternalSpotChannel);
        framework.AddClientServerChannel(SpotServiceNames.ExternalClientServerChannel)
            .EnableClient(options.PlayAExternalSpotEndpoint)
            .EnableSpotRouteEgress(SpotServiceNames.ExternalSpotChannel);
    });
    return builder.Build();
}

static IZlinkStreamConnector CreateClient(string endpoint)
{
    return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
    {
        Endpoint = new Uri(endpoint),
        ConnectTimeout = TimeSpan.FromSeconds(5),
        RequestTimeout = TimeSpan.FromSeconds(5),
        DispatchMode = ZlinkStreamDispatchMode.Immediate,
    });
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
    string PlayARid,
    string PlayBRid,
    string PlayAExternalSpotEndpoint,
    string ClientControlEndpoint,
    string ClientExternalRouteEndpoint,
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
            Required("play-a-rid"),
            Required("play-b-rid"),
            Required("play-a-external-spot-endpoint"),
            Required("client-control-endpoint"),
            Required("client-external-route-endpoint"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-spot-e2e")));
    }
}
