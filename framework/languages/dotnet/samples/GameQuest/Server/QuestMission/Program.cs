using GameQuest.QuestMission.Adapters.ZLink.Spots;
using GameQuest.QuestMission.Adapters.Store;
using GameQuest.QuestMission.Application;
using GameQuest.Shared;
using GameQuest.Server.Configuration;
using Zlink.Framework.Contracts.Codecs.Json;
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
        builder.Services.AddSingleton<QuestStore>();
        builder.Services.AddSingleton<QuestOwnerRouter>();
        builder.Services.AddScoped<QuestEventProcessor>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<GameQuestDispatchErrorObserver>();
            options.Codecs.AddJson();
            options.AddHandlersFromAssemblyOf(typeof(Program));
            options.UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint);
            {
                var channel = options.AddFanoutChannel(SampleNames.FanoutChannel);
                channel.EnableSubscriber(topology.FanoutPublisherAEndpoint);
                channel.EnableSubscriber(topology.FanoutPublisherBEndpoint);
                channel.AddHandlerGroup("gamequest-gameplay");

            }
            {
                var mesh = options.AddSpotMesh(SampleNames.QuestSpotDiscovery);
                {
                    var spot = mesh.AddNode(SampleNames.QuestSpotNode);
                    {
                        var router = spot.EnableRouter(instance.SpotRouterEndpoint);
                        router.SetRouterRoutingId(instance.SpotRid);

                    }
                    spot.EnablePubSub(instance.SpotEndpoint);
                    spot.AddSpotFactory<PlayerQuestSpot>();

                }

            }
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
