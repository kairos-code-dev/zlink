using Microsoft.Extensions.Configuration;

using SpotService.Server.Play.Endpoints;
using SpotService.Server.Play.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;

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
                // Crash-recovery scenarios re-claim actors from a killed
                // node; a short owner lease keeps that takeover window
                // within the scenario's patience.
                var locations = framework.ConfigureLocations();
                locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
                locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);
                locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            }
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.AddRouteMeshChannel(SpotServiceNames.ControlChannel)
                .EnableServer(Require(options.ControlEndpoint, "ControlEndpoint"))
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddHandlerGroup("play");
            var externalSpotChannel = string.Equals(options.Rid, "play-b", StringComparison.Ordinal)
                ? SpotServiceNames.ExternalSpotChannelB
                : SpotServiceNames.ExternalSpotChannel;
            if (!string.IsNullOrWhiteSpace(options.ExternalSpotEndpoint))
                framework.AddRouteMeshChannel(externalSpotChannel)
                    .EnableServer(options.ExternalSpotEndpoint)
                    .EnableClient()
                    .SetRoutingId(RoutingId.From(options.Rid))
                    .AddHandlerGroup("play");
            if (!string.IsNullOrWhiteSpace(options.ExternalClientEndpoint))
                framework.AddClientServerChannel(SpotServiceNames.ExternalClientChannel)
                    .EnableServer(options.ExternalClientEndpoint)
                    .EnableClient(options.ExternalClientEndpoint)
                    .AddHandlerGroup("client");

            var spot = framework.AddSpotMesh(SpotServiceNames.SpotChannel)
                .EnableRouter(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid))
                .SetEntrySpotRoutingId(RoutingId.From(options.Rid))
                .EnablePubSub(Require(options.SpotPubEndpoint, "SpotPubEndpoint"))
                .AddEntrySpot<ScenarioEntrySpot>()
                .AddActorFactory<ScenarioActorFactory>(SpotServiceNames.ActorType)
                .AddSpotFactory<ScenarioUserSpot>()
                .AddSpotFactory<ScenarioAlternateSpot>();
            if (!string.IsNullOrWhiteSpace(options.ClientSpotPubEndpoint))
                spot.ConnectPeerPub(options.ClientSpotPubEndpoint);
        });

        var app = builder.Build();
        OperationalEndpoints.MapOperationalEndpoints(app, options);
        SpotLifecycleEndpoints.MapSpotLifecycleEndpoints(app);
        SpotFailureEndpoints.MapSpotFailureEndpoints(app);
        SpotInteractionEndpoints.MapSpotInteractionEndpoints(app);
        return app;
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
        IZLinkRouteClient routes,
        IZLinkSpotHandleResolver locator,
        string targetSpotRid,
        StateReq request)
        => await routes.RequestToSpot(await locator.ResolveRequiredAsync(targetSpotRid), request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<StateRes>();

    internal static async Task SendSpotCommandAsync(
        IZLinkRouteClient routes,
        IZLinkSpotHandleResolver locator,
        string targetSpotRid,
        object command)
        => routes.SendToSpot(await locator.ResolveRequiredAsync(targetSpotRid), command).Submit();

    internal static async Task<SpotToSpotRes> RequestSpotToSpotAsync(
        IZLinkRouteClient routes,
        IZLinkSpotHandleResolver locator,
        string sourceSpotRid,
        SpotToSpotReq request)
        => await routes.RequestToSpot(await locator.ResolveRequiredAsync(sourceSpotRid), request)
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<SpotToSpotRes>();

    internal static async Task WaitUntilAsync(Func<bool> condition, string failureMessage)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (condition()) return;

            await Task.Delay(100);
        }

        throw new InvalidOperationException(failureMessage);
    }

    internal static async Task WaitUntilAsync(Func<Task<bool>> condition, string failureMessage)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
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

            await Task.Delay(100);
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
