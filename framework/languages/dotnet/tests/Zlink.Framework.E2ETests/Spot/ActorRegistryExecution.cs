using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Spot;


public sealed class ActorRegistryExecutionTests : SpotTestSupport
{
    [Fact]
    public async Task EntrySpot_And_UserSpot_ActorPacketRegistries_Dispatch_ActorPackets()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddScoped<RegistryEntryJoinHandler>();
        builder.Services.AddScoped<RegistryStageDispatchHandler>();
        builder.Services.AddZLinkFramework(options =>
        {

            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotMesh("game.registry", mesh =>
            {
                mesh.AddNode("registry-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.BindRouter(spotNode);
                });
                spot.AddEntrySpot<RegistryEntrySpot>();
                spot.AddSpotFactory<RegistryStageSpot>();
            });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var first = await manager.CreateAsync<RegistryStageSpot>();
        var second = await manager.CreateAsync<RegistryStageSpot>();
        var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("registry-actor", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actor, new TestStream("registry-session"));

        using (var joinBody = global::Systems.Zlink.Message.From(first.SpotRid.ToHex()))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "entry-join",
                    ZlinkStreamMetadata.Empty),
                joinBody);
        }

        Assert.Equal("entry-room", actor.CurrentRoomId);
        Assert.Contains($"entry:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains(
            recorder.Events,
            static entry => entry.StartsWith("entry-spot:registry-actor:", StringComparison.Ordinal));
        Assert.Contains("entry-left:registry-actor", recorder.Events);
        Assert.Contains($"joined:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);

        using (var dispatchBody = Message.From("payload"))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "spot-dispatch",
                    ZlinkStreamMetadata.Empty),
                dispatchBody);
        }

        Assert.Contains($"dispatch:registry-actor:entry-room:payload:{first.SpotRid.ToHex()}", recorder.Events);

        _ = await actorRuntime.JoinActorAsync(
            second.SpotRid,
            actor,
            EncodeJoin(new RegistryJoinRequest("second-room")));

        Assert.Contains($"left:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains($"joined:registry-actor:{second.SpotRid.ToHex()}", recorder.Events);

        var currentSpot = actor.Spot ?? throw new InvalidOperationException("Actor is not joined.");
        await currentSpot.Context.leaveActor(actor);

        Assert.Null(actor.Spot);
        Assert.Contains($"left:registry-actor:{second.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains("entry-joined:registry-actor", recorder.Events);

        await actorRuntime.DisconnectActorAsync(
            actor,
            new TestStream("registry-session"));

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorDispatch_Rechecks_CurrentLocation_After_Waiting_For_ActorMailbox()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddScoped<EntrySpotJoinBlockingHandler>();
        builder.Services.AddScoped<RegistryStageDispatchHandler>();
        builder.Services.AddZLinkFramework(options =>
        {

            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotMesh("game.registry-location", mesh =>
            {
                mesh.AddNode("registry-location-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.BindRouter(spotNode);
                });
                spot.AddEntrySpot<RegistryEntrySpot>();
                spot.AddSpotFactory<RegistryStageSpot>();
            });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var stage = await manager.CreateAsync<RegistryStageSpot>();
        var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("registry-location-actor", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actor, new TestStream("registry-location-session"));

        Task? joinTask = null;
        Task? dispatchTask = null;
        try
        {
            joinTask = SubmitEntrySpotStringAsync(
                actorRuntime,
                actor,
                "entry-join-block",
                stage.SpotRid.ToHex());
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

            dispatchTask = SubmitEntrySpotStringAsync(
                actorRuntime,
                actor,
                "spot-dispatch",
                "after-join");
            Assert.False(dispatchTask.IsCompleted, "dispatch should wait for the same actor mailbox.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await joinTask!.WaitAsync(TimeSpan.FromSeconds(5));
        await dispatchTask!.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Contains(
            $"dispatch:registry-location-actor:entry-room:after-join:{stage.SpotRid.ToHex()}",
            recorder.Events);

        await actorRuntime.DisconnectActorAsync(
            actor,
            new TestStream("registry-location-session"));

        await host.StopAsync();
    }
}
