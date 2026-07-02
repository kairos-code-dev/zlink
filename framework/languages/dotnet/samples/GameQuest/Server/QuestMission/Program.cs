using GameQuest.QuestMission.Application;
using GameQuest.QuestMission.Infrastructure.Http;
using GameQuest.QuestMission.Infrastructure.Store;
using GameQuest.QuestMission.Infrastructure.ZLink;
using GameQuest.QuestMission.Infrastructure.ZLink.Spots.PlayerQuestSpot;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.Contracts.Dispatch;
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
        builder.Services.AddSingleton<IPlayerQuestOwnerProvisioner, PlayerQuestOwnerProvisioner>();
        builder.Services.AddSingleton<IGameApiSnapshotClient, HttpGameApiSnapshotClient>();
        builder.Services.AddSingleton<IQuestProgressNotifier, HttpQuestProgressNotifier>();
        builder.Services.AddSingleton<QuestOwnerRouter>();
        builder.Services.AddScoped<QuestEventProcessor>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddRedisLocationStore(redis =>
            {
                redis.ConnectionString = topology.RedisEndpoint;
                redis.KeyPrefix = topology.RedisKeyPrefix;
            });
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(missionName))
                .TraceLabel(missionName);
            options.AddHandlersFromAssemblyOf(typeof(Program));
            options.AddFanoutChannel(SampleNames.FanoutChannel)
                .EnableSubscriber()
                .EnableSubscriber()
                .AddHandlerGroup("gamequest-gameplay");
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
            QuestEventProcessor processor,
            CancellationToken cancellationToken) =>
        {
            return Results.Ok(await processor.SyncAsync(request, cancellationToken));
        });

        await app.RunAsync();
    }
}
