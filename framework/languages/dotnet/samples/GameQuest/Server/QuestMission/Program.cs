using GameQuest.QuestMission.Adapters.ZLink.Spots;
using GameQuest.QuestMission.Adapters.Store;
using GameQuest.QuestMission.Application;
using GameQuest.Shared;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.AspNetCore;

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
        builder.Services.AddHttpClient();
        builder.Services.AddSingleton<QuestStore>();
        builder.Services.AddSingleton<QuestOwnerRouter>();
        builder.Services.AddScoped<QuestEventProcessor>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = SampleNames.RequestTimeout;
            options.Codecs.AddJson();
            options.AddHandlersFromAssemblyOf(typeof(Program));
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint));
            options.AddFanoutChannel(SampleNames.FanoutChannel, channel =>
            {
                channel.EnableSubscriber(subscriber =>
                {
                    subscriber.UseManualConnections(connections =>
                    {
                        connections.Connect(topology.FanoutPublisherAEndpoint);
                        connections.Connect(topology.FanoutPublisherBEndpoint);
                    });
                });
                channel.AddHandlerGroup("gamequest-gameplay");
            });
            options.AddSpotMesh(SampleNames.QuestSpotDiscovery, mesh =>
            {
                mesh.AddNode(SampleNames.QuestSpotNode, spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(instance.SpotRouterEndpoint);
                        router.SetRoutingId(instance.SpotRid);
                    });
                    spot.EnablePubSub(pubsub => pubsub.BindPubSub(instance.SpotEndpoint));
                    spot.AddSpotFactory<PlayerQuestSpot>();
                });
            });
        });

        var app = builder.Build();

        app.MapGet("/health", () => Results.Ok(new { status = "ok", mission = instance.MissionName }));
        app.MapGet("/self-check/events", async (QuestStore store, CancellationToken cancellationToken) =>
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
