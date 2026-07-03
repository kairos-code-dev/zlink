using GameQuest.QuestMission.Application;
using GameQuest.QuestMission.Infrastructure.Http;
using GameQuest.QuestMission.Infrastructure.Store;
using GameQuest.QuestMission.Infrastructure.ZLink;
using GameQuest.QuestMission.Infrastructure.ZLink.Spots.PlayerQuestSpot;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Spots;
using Zlink.Samples.Logging;

namespace GameQuest.QuestMission;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var topology = GameQuestTopology.FromEnvironment();
        var missionName = Environment.GetEnvironmentVariable("GAMEQUEST_MISSION_NAME") ?? "mission-a";
        var instance = topology.ForQuestMission(missionName);
        var builder = WebApplication.CreateBuilder(args);
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("GAMEQUEST_LOG_DIR"),
            missionName);

        builder.Services.AddSingleton(topology);
        builder.Services.AddSingleton(instance);
        builder.Services.AddSingleton<QuestStore>();
        builder.Services.AddSingleton<IQuestStore>(sp => sp.GetRequiredService<QuestStore>());
        builder.Services.AddSingleton<PlayerQuestOwnerProvisioner>();
        builder.Services.AddSingleton<IGameApiSnapshotClient, HttpGameApiSnapshotClient>();
        builder.Services.AddSingleton<IQuestProgressNotifier, HttpQuestProgressNotifier>();
        builder.Services.AddSingleton<QuestOwnerRouter>();
        builder.Services.AddScoped<QuestEventProcessor>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(topology.RedisEndpoint)
                .SetKeyPrefix(topology.RedisKeyPrefix)));
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(missionName))
                .TraceLabel(missionName);
            options.AddHandlersFromAssemblyOf(typeof(Program));
            options.AddRouteMesh(SampleNames.QuestOwnerRouteChannel)
                .EnableServer(instance.RouteEndpoint)
                .SetRoutingId(instance.SpotRid)
                .AddRequestHandler<GameplayEventRouteHandler, ApplyGameplayEventReq, ApplyGameplayEventRes>();
            options.AddSpotMesh(SampleNames.QuestSpotDiscovery)
                .EnableRouter(instance.SpotRouterEndpoint)
                .SetRoutingId(instance.SpotRid)
                .EnablePubSub(instance.SpotEndpoint)
                .AddSpotFactory<PlayerQuestSpot>();
        });

        var app = builder.Build();

        app.MapGet("/health", () => Results.Ok(new { status = "ok", mission = instance.MissionName }));
        app.MapGet("/self-check/events",
            async (QuestStore store, CancellationToken cancellationToken) =>
            {
                return Results.Ok(await store.ReadEventsAsync(cancellationToken));
            });
        app.MapPost("/internal/sync", async (
            SyncQuestProgressReq request,
            PlayerQuestOwnerProvisioner playerQuestOwners,
            QuestOwnerRouter ownerRouter,
            QuestStore store,
            CancellationToken cancellationToken) =>
        {
            if (!ownerRouter.IsLocalOwner(request.PlayerId))
                return Results.Ok(new SyncQuestProgressRes(await store.ReadProjectionAsync(request.PlayerId, cancellationToken)));

            return Results.Ok(await playerQuestOwners.SyncAsync(request, cancellationToken));
        });
        app.MapPost("/self-check/owner/{playerId}/close", async (
            string playerId,
            IZLinkSpotManager spots,
            QuestOwnerRouter ownerRouter,
            CancellationToken cancellationToken) =>
        {
            if (!ownerRouter.IsLocalOwner(playerId)) return Results.Ok(new { closed = false, owner = false });

            var spotRid = RoutingId.From(System.Text.Encoding.UTF8.GetBytes($"player:{playerId}"));
            var closed = await spots.CloseAsync(spotRid, cancellationToken);
            return Results.Ok(new { closed, owner = true });
        });

        await app.RunAsync();
    }
}
