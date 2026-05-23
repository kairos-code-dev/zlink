using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;


public sealed class ActorLifecycleTests : SpotTestSupport
{
    [Fact]
    public async Task SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddScoped<ActorJoinHandler>();
        builder.Services.AddScoped<ActorJoinViaContextHandler>();
        builder.Services.AddScoped<ActorDispatchHandler>();
        builder.Services.AddZLinkFramework(options =>
        {

            options.AddActorFactory<TestActorFactory>("test");
            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("actor-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(spotNode);
                });
                spot.AddSpotFactory<ActorStageSpot>("actor-stage");
            });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();

        var first = await manager.CreateAsync("actor-stage");
        var second = await manager.CreateAsync("actor-stage");
        var actor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-1", "test")).Actor;

        var firstReply = await actorRuntime.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            first.SpotRid,
            actor,
            new JoinStageRequest("room-1"));
        Assert.Equal("room-1", firstReply.RoomId);
        Assert.Equal(first.SpotRid, actor.Spot?.Context.SpotRid);
        Assert.Equal(first.SpotRid, actor.Context.SpotRid);

        var secondReply = await actorRuntime.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            second.SpotRid,
            actor,
            new JoinStageRequest("room-2"));
        Assert.Equal("room-2", secondReply.RoomId);
        Assert.Equal(second.SpotRid, actor.Spot?.Context.SpotRid);
        Assert.Equal(second.SpotRid, actor.Context.SpotRid);
        await RetryAsync(
            () => recorder.SpotActorJoins.Contains($"actor-1@{second.SpotRid.ToHex()}")
                && recorder.SpotActorLeaves.Contains($"actor-1@{first.SpotRid.ToHex()}"),
            TimeSpan.FromSeconds(5));

        var contextActor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-context", "test")).Actor;
        await actorRuntime.AttachActorAsync(contextActor, new TestStream("session-context"));
        using (var joinBody = global::Systems.Zlink.Message.FromString(first.SpotRid.ToHex()))
        {
            await actorRuntime.SubmitActorAsync(
                contextActor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "join-via-context",
                    ZlinkStreamMetadata.Empty),
                joinBody);
        }

        Assert.Equal(first.SpotRid, contextActor.Spot?.Context.SpotRid);
        Assert.Equal("room-context", contextActor.CurrentRoomId);

        using (var contextDispatchBody = global::Systems.Zlink.Message.FromString("context-payload"))
        {
            await actorRuntime.SubmitActorAsync(
                contextActor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "dispatch-after-context-join",
                    ZlinkStreamMetadata.Empty),
                contextDispatchBody);
        }

        Assert.Contains("context-payload", recorder.DispatchBodies);
        Assert.Contains("room-context", recorder.DispatchRooms);
        Assert.Contains(first.SpotRid.ToHex(), recorder.DispatchSpotRids);

        var stream = new TestStream("session-1");
        await actorRuntime.AttachActorAsync(actor, stream);

        using var header = global::Systems.Zlink.Message.FromString("header");
        using var body = global::Systems.Zlink.Message.FromString("payload");
        await actorRuntime.SubmitActorAsync(
            actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                null,
                "dispatch",
                ZlinkStreamMetadata.Empty),
            body);

        await RetryAsync(
            () => recorder.DispatchBodies.Contains("payload"),
            TimeSpan.FromSeconds(5));

        Assert.False(recorder.ConcurrentViolation);
        Assert.Equal("room-2", recorder.DispatchRooms.LastOrDefault());
        Assert.DoesNotContain("room-1", recorder.DispatchRooms);

        await actorRuntime.DisconnectActorAsync(actor, stream);
        await actorRuntime.DisconnectActorAsync(
            contextActor,
            new TestStream("session-context"));

        await host.StopAsync();
    }

    [Fact]
    public async Task CreateLocalActorAsync_Coalesces_Concurrent_Creation_For_Same_ActorId()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddSingleton<ConcurrentActorFactoryRecorder>();
        builder.Services.AddZLinkFramework(options =>
        {

            options.AddActorFactory<ConcurrentActorFactory>("test");
            options.AddSpotMesh("actor.factory", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("actor-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(spotNode);
                });
            });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ConcurrentActorFactoryRecorder>();
        var calls = Enumerable.Range(0, 8)
            .Select(_ => runtime.CreateLocalActorAsync("actor-concurrent", "test").AsTask())
            .ToArray();

        await recorder.FirstFactoryCall.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await Task.Delay(100);
        Assert.Equal(1, Volatile.Read(ref recorder.CreateCount));

        recorder.ReleaseFactory.SetResult();

        var results = await Task.WhenAll(calls).WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(1, Volatile.Read(ref recorder.CreateCount));
        Assert.Equal(1, results.Count(static result => result.Created));
        Assert.All(results, result => Assert.Same(results[0].Actor, result.Actor));

        await host.StopAsync();
    }
}
