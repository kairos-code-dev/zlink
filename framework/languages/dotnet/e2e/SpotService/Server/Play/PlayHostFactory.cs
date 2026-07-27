using Microsoft.Extensions.Configuration;

using SpotService.Server.Play.Endpoints;
using SpotService.Server.Play.Handlers;
using SpotService.Server.Play.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

using Zlink.Framework.Locations.Redis;

namespace SpotService.Server.Play;

internal static class PlayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "play");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));
        builder.Services.AddSingleton<ApplicationJoinCoordinator>();

        builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
                framework.AddRelocationStore(new ZLinkRedisRelocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix($"{options.RedisKeyPrefix}:relocation")));
                // Crash-recovery scenarios re-claim actors from a killed
                // node; a short owner lease keeps that takeover window
                // within the scenario's patience.
                var locations = framework.ConfigureLocations();
                locations.OwnerLeaseRenewInterval = TimeSpan.FromSeconds(1);
                locations.OwnerLeaseTtl = TimeSpan.FromSeconds(10);
                locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            }
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .SetRuntimeMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            var controlMesh = framework.AddRouteMesh(SpotServiceNames.ControlChannel)
                .Listen(Require(options.ControlEndpoint, "ControlEndpoint"))
                .SetRoutingIdPrefix(options.Rid);
            controlMesh.Channel(SpotServiceNames.ControlChannel).Server();
            AddPlayRouteHandlers(controlMesh);
            var externalSpotChannel = string.Equals(options.Rid, "play-b", StringComparison.Ordinal)
                ? SpotServiceNames.ExternalSpotChannelB
                : SpotServiceNames.ExternalSpotChannel;
            if (!string.IsNullOrWhiteSpace(options.ExternalSpotEndpoint))
            {
                var externalMesh = framework.AddRouteMesh(externalSpotChannel)
                    .Listen(options.ExternalSpotEndpoint)
                    .SetRoutingIdPrefix(options.Rid);
                externalMesh.Channel(externalSpotChannel).Server();
                AddPlayRouteHandlers(externalMesh);
            }
            var spot = framework.AddRouteMesh(SpotServiceNames.SpotChannel)
                .Listen(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingIdPrefix(options.Rid);
            spot.Objects().Server()
                .AddEntrySpot<ScenarioEntrySpot>()
                .AddActorFactory<ScenarioActor, ScenarioActorFactory>(
                    SpotServiceNames.ActorType,
                    null,
                    ZLinkRelocationPolicy<ScenarioActor>.Recreate)
                .AddSpotFactory<ScenarioUserSpot>(
                    SpotServiceNames.UserSpotType,
                    null,
                    ZLinkRelocationPolicy<ScenarioUserSpot>.Disabled)
                .AddSpotFactory<ScenarioAlternateSpot>(
                    SpotServiceNames.AlternateSpotType,
                    null,
                    ZLinkRelocationPolicy<ScenarioAlternateSpot>.Disabled)
                .AddSpotFactory<MultiNodeSpotA>(
                    SpotServiceNames.MultiSpotTypeA,
                    null,
                    ZLinkRelocationPolicy<MultiNodeSpotA>.Disabled)
                .AddSpotFactory<MultiNodeSpotB>(
                    SpotServiceNames.MultiSpotTypeB,
                    null,
                    ZLinkRelocationPolicy<MultiNodeSpotB>.Disabled);
            spot.Channel(SpotServiceNames.SpotChannel).Server();
            if (string.Equals(options.Rid, "play-a", StringComparison.Ordinal))
            {
                spot.Channel(SpotServiceNames.ExternalClientChannel).Server()
                    .AddRequestHandler<ChannelEchoHandler>()
                    .AddSendHandler<ChannelNotifyHandler>();
            }
        });

        var app = builder.Build();
        OperationalEndpoints.MapOperationalEndpoints(app, options);
        SpotLifecycleEndpoints.MapSpotLifecycleEndpoints(app);
        SpotFailureEndpoints.MapSpotFailureEndpoints(app);
        SpotInteractionEndpoints.MapSpotInteractionEndpoints(app);
        app.MapGet("/mesh-snapshot", (IZLinkRouteMeshRuntime meshRuntime) =>
            Results.Ok(meshRuntime.Snapshot(SpotServiceNames.SpotChannel)));
        app.MapGet("/entry-self-check", async (
            IZLinkLocationRuntimeQuery locations,
            IZLinkSpotClient spotsClient,
            CancellationToken cancellationToken) =>
        {
            var descriptors = await locations.ListMeshNodeDescriptorsAsync(
                SpotServiceNames.SpotChannel,
                cancellationToken: cancellationToken);
            var entrySpotId = descriptors.Items.SingleOrDefault(descriptor =>
                                  descriptor.Rid.ToString().StartsWith(
                                      $"{options.Rid}-",
                                      StringComparison.Ordinal))
                              ?.EntrySpotId
                              ?? throw new InvalidOperationException(
                                  $"Entry Spot for '{options.Rid}' was not published.");
            var marker = $"self-{Guid.NewGuid():N}";
            var reply = await spotsClient.RequestToSpot(entrySpotId, new EntryReadinessReq(marker))
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<EntryReadinessRes>(cancellationToken);
            return Results.Ok(reply);
        });
        return app;
    }

    private static void AddPlayRouteHandlers(IZLinkMeshNodeBuilder mesh)
    {
        mesh.AddRouteRequestHandler<EnsureActorHandler>()
            .AddRouteRequestHandler<ControlPingHandler>()
            .AddRouteRequestHandler<CreateSpotHandler>()
            .AddRouteRequestHandler<CloseSpotHandler>()
            .AddRouteRequestHandler<SpotTypeMismatchHandler>();
    }

    internal static string Require(string? value, string optionName)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{optionName} is required.")
            : value;
    }

    internal static async Task<bool> FailsAsync(Task task)
    {
        try
        {
            await task;
            return false;
        }
        catch
        {
            return true;
        }
    }

    internal static async Task<StateRes> RequestSpotStateAsync(
        IZLinkSpotClient routes,
        string targetSpotRid,
        StateReq request)
        => await routes.RequestToSpot(targetSpotRid, request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<StateRes>();

    internal static async Task SendSpotCommandAsync(
        IZLinkSpotClient routes,
        string targetSpotRid,
        object command,
        CancellationToken cancellationToken = default)
        => await routes.SendToSpot(targetSpotRid, command)
            .Async(cancellationToken);

    internal static async Task<SpotToSpotRes> RequestSpotToSpotAsync(
        IZLinkSpotClient routes,
        string sourceSpotRid,
        SpotToSpotReq request)
        => await routes.RequestToSpot(sourceSpotRid, request)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<SpotToSpotRes>();

    internal static async Task WaitUntilAsync(Func<bool> condition, string failureMessage)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(3);
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (condition()) return;
            await Task.Delay(10);
        }

        throw new InvalidOperationException(failureMessage);
    }

    internal static async Task WaitUntilAsync(Func<Task<bool>> condition, string failureMessage)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(3);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                if (await condition()) return;
            }
            catch (Exception ex)
            {
                last = ex;
            }

            await Task.Delay(10);
        }

        throw new InvalidOperationException(failureMessage, last);
    }

    private static int FindIndex(string[] lines, string pattern)
    {
        return Array.FindIndex(lines, line => line.Contains(pattern, StringComparison.Ordinal));
    }

    private static ulong ExtractUInt64(string line, string key)
    {
        var prefix = key + "=";
        var start = line.IndexOf(prefix, StringComparison.Ordinal);
        if (start < 0) throw new InvalidOperationException($"Missing field '{key}' in evidence line: {line}");

        start += prefix.Length;
        var end = line.IndexOf('|', start);
        var value = end < 0 ? line[start..] : line[start..end];
        return ulong.Parse(value);
    }

    internal static int CountNew(string[] after, string[] before, string pattern)
    {
        var beforeCount = before.Count(line => line.Contains(pattern, StringComparison.Ordinal));
        var afterCount = after.Count(line => line.Contains(pattern, StringComparison.Ordinal));
        return afterCount - beforeCount;
    }

}
