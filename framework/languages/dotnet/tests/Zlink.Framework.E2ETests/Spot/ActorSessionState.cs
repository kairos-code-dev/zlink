using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Spot;


public sealed class ActorSessionStateTests : SpotTestSupport
{
    [Fact]
    public async Task ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddZLinkFramework(options =>
        {

            options.AddActorFactory<TestActorFactory>("test");
            {
                var mesh = options.AddSpotMesh("game.stage");
                {
                    var spot = mesh.AddNode("actor-node");
                {
                    var router = spot.EnableRouter(spotNode);

                }
                spot.AddSpotFactory<ActorStageSpot>();

                }

            }
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();
        var created = await manager.CreateAsync<ActorStageSpot>();
        var actor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-2", "test")).Actor;

        _ = await actorRuntime.JoinActorAsync(
            created.SpotRid,
            actor,
            EncodeJoin(new JoinStageRequest("room-disconnect")));

        var staleStream = new TestStream("session-stale");
        var currentStream = new TestStream("session-current");
        await actorRuntime.AttachActorAsync(actor, staleStream);
        await actorRuntime.AttachActorAsync(actor, currentStream);

        await actorRuntime.DisconnectActorAsync(actor, staleStream);
        Assert.Equal(0, recorder.DisconnectCount);
        await actorRuntime.DisconnectActorAsync(actor, currentStream);
        Assert.Equal(0, recorder.DisconnectCount);
        await actorRuntime.NotifyActorDisconnectedByIdAsync(actor.ActorId);
        Assert.Equal(1, recorder.DisconnectCount);
        Assert.Equal(created.SpotRid, actor.Spot?.Context.SpotRid);

        await host.StopAsync();
    }
}
