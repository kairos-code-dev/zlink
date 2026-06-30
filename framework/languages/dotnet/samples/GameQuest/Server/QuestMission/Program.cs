using GameQuest.QuestMission.Application;
using GameQuest.QuestMission.Infrastructure.Http;
using GameQuest.QuestMission.Infrastructure.Store;
using GameQuest.QuestMission.Infrastructure.ZLink;
using GameQuest.QuestMission.Infrastructure.ZLink.Spots.PlayerQuestSpot;
using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;

namespace GameQuest.QuestMission;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var topology = GameQuestTopology.FromEnvironment();
        var missionName = Environment.GetEnvironmentVariable("GAMEQUEST_MISSION_NAME") ?? "mission-a";
        var instance = topology.ForQuestMission(missionName);
        var builder = WebApplication.CreateBuilder(args);

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
            options.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(SampleFlowLog.Path(missionName))
                .TraceLabel(missionName);
            options.AddHandlersFromAssemblyOf(typeof(Program));
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            {
                var channel = options.AddFanoutChannel(SampleNames.FanoutChannel);
                channel.EnableSubscriber();
                channel.EnableSubscriber();
                channel.AddHandlerGroup("gamequest-gameplay");
            }
            {
                var mesh = options.AddSpotMesh(SampleNames.QuestSpotDiscovery);
                {
                    var spot = mesh;
                    {
                        var router = spot.EnableRouter(instance.SpotRouterEndpoint);
                        router.SetRoutingId(instance.SpotRid);
                    }
                    spot.EnablePubSub(instance.SpotEndpoint);
                    spot.AddSpotFactory<PlayerQuestSpot>();
                }
            }
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