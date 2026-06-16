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
    public async Task EntrySpot_DestroyActorAsync_Removes_EntryOwned_Actor_Without_Left_Callback()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddScoped<ActorDispatchHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<TestActorFactory>("test");
            {
                var mesh = options.AddSpotMesh("game.stage");
                {
                    var spot = mesh.AddNode("actor-destroy-node");
                    {
                        var router = spot.EnableRouter(spotNode);

                    }
                    spot.AddEntrySpot<ActorLifecycleEntrySpot>();
                    spot.AddSpotFactory<ActorStageSpot>();

                }

            }
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();
        var entrySpot = (ActorLifecycleEntrySpot)actorRuntime
            .GetSpotNodeRuntime("actor-destroy-node")
            .EntrySpotActivation!
            .EntrySpot;

        var entryCreateBeforeEntryOwnedCreate = recorder.EntrySpotActorCreates.Count;
        var entryLeftBeforeEntryOwnedDestroy = recorder.EntrySpotActorLeaves.Count;
        var entryOwned = (TestActor)(await actorRuntime.CreateLocalActorAsync(
            "actor-entry-destroy",
            "test")).Actor;
        await entrySpot.Context.DestroyActorAsync(entryOwned);
        await entrySpot.Context.DestroyActorAsync(entryOwned);
        Assert.Null(await actorRuntime.FindActorAsync("actor-entry-destroy"));
        Assert.Equal(
            entryCreateBeforeEntryOwnedCreate + 1,
            recorder.EntrySpotActorCreates.Count);
        Assert.Contains("entry-created:actor-entry-destroy", recorder.EntrySpotActorCreates);
        Assert.Equal(
            entryLeftBeforeEntryOwnedDestroy,
            recorder.EntrySpotActorLeaves.Count);

        using (var payload = global::Systems.Zlink.Message.From("after-destroy"))
        {
            var ex = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                async () => await actorRuntime.SubmitActorByIdAsync(
                    "actor-entry-destroy",
                    new ZlinkStreamHeader(
                        ZlinkStreamMessageKind.Send,
                        ZlinkStreamCodec.Raw,
                        ZlinkStreamHeaderFlags.None,
                        null,
                        "dispatch",
                        ZlinkStreamMetadata.Empty),
                    payload));
            Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, ex.Kind);
        }

        var recreated = (TestActor)(await actorRuntime.CreateLocalActorAsync(
            "actor-entry-destroy",
            "test")).Actor;
        await entrySpot.Context.DestroyActorAsync(entryOwned);
        Assert.Equal(
            entryLeftBeforeEntryOwnedDestroy,
            recorder.EntrySpotActorLeaves.Count);
        Assert.Same(recreated, await actorRuntime.FindActorAsync("actor-entry-destroy"));
        await entrySpot.Context.DestroyActorAsync(recreated);
        Assert.Equal(
            entryLeftBeforeEntryOwnedDestroy,
            recorder.EntrySpotActorLeaves.Count);

        var reentrant = (TestActor)(await actorRuntime.CreateLocalActorAsync(
            "actor-entry-destroy-reentrant",
            "test")).Actor;
        var entryLeftBeforeReentrantDestroy = recorder.EntrySpotActorLeaves.Count;
        await entrySpot.Context.DestroyActorAsync(reentrant);
        Assert.Null(await actorRuntime.FindActorAsync("actor-entry-destroy-reentrant"));
        Assert.Equal(
            entryLeftBeforeReentrantDestroy,
            recorder.EntrySpotActorLeaves.Count);

        var room = await manager.CreateAsync<ActorStageSpot>();
        var roomActor = (TestActor)(await actorRuntime.CreateLocalActorAsync(
            "actor-room-destroy",
            "test")).Actor;
        var stream = new TestStream("session-destroy");
        var sessionRid = RoutingId.From(Encoding.UTF8.GetBytes("session-destroy"));
        await actorRuntime.AttachActorAsync(roomActor, stream);
        actorRuntime.BindActorSession(roomActor.ActorId, sessionRid, "native:destroy-test");
        Assert.True(actorRuntime.TryGetActorBoundSession(roomActor.ActorId, out _));

        _ = await actorRuntime.JoinActorAsync(
            room.SpotRid,
            roomActor,
            EncodeJoin(new JoinStageRequest("room-destroy")));

        var directDestroyError = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            async () => await entrySpot.Context.DestroyActorAsync(roomActor));
        Assert.Equal(ZLinkFrameworkErrorKind.ActorRouteNotFound, directDestroyError.Kind);
        Assert.Same(roomActor, await actorRuntime.FindActorAsync(roomActor.ActorId));
        Assert.True(actorRuntime.TryGetActorBoundSession(roomActor.ActorId, out _));

        await actorRuntime.DisconnectActorAsync(roomActor, stream);
        Assert.Same(roomActor, await actorRuntime.FindActorAsync(roomActor.ActorId));
        Assert.Equal(0, recorder.DisconnectCount);

        var roomSpot = (ActorStageSpot)actorRuntime
            .GetSpotNodeRuntime("actor-destroy-node")
            .Spots
            .Single(activation => activation.SpotRid == room.SpotRid)
            .Spot;
        await roomSpot.leaveActor(roomActor, CancellationToken.None);
        await RetryAsync(
            () => recorder.SpotActorLeaves.Any(item => item.StartsWith(
                $"{roomActor.ActorId}@",
                StringComparison.Ordinal)),
            TimeSpan.FromSeconds(5));

        var spotLeftCountBeforeDestroy = recorder.SpotActorLeaves.Count;
        var entryLeftCountBeforeDestroy = recorder.EntrySpotActorLeaves.Count;
        await entrySpot.Context.DestroyActorAsync(roomActor);

        Assert.Null(await actorRuntime.FindActorAsync(roomActor.ActorId));
        Assert.False(actorRuntime.TryGetActorBoundSession(roomActor.ActorId, out _));
        Assert.Equal(spotLeftCountBeforeDestroy, recorder.SpotActorLeaves.Count);
        Assert.Equal(entryLeftCountBeforeDestroy, recorder.EntrySpotActorLeaves.Count);

        await host.StopAsync();
    }

    [Fact]
    public async Task SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddScoped<ActorDispatchHandler>();
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

        var first = await manager.CreateAsync<ActorStageSpot>();
        var second = await manager.CreateAsync<ActorStageSpot>();
        var actor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-1", "test")).Actor;

        var firstReply = await actorRuntime.JoinActorAsync(
            first.SpotRid,
            actor,
            EncodeJoin(new JoinStageRequest("room-1")));
        Assert.Equal("room-1", DecodeJoin<JoinStageReply>(firstReply.Reply).RoomId);
        Assert.Equal(first.SpotRid, actor.Spot?.Context.SpotRid);
        Assert.Equal(first.SpotRid, actor.Context.SpotRid);

        var secondReply = await actorRuntime.JoinActorAsync(
            second.SpotRid,
            actor,
            EncodeJoin(new JoinStageRequest("room-2")));
        Assert.Equal("room-2", DecodeJoin<JoinStageReply>(secondReply.Reply).RoomId);
        Assert.Equal(second.SpotRid, actor.Spot?.Context.SpotRid);
        Assert.Equal(second.SpotRid, actor.Context.SpotRid);
        await RetryAsync(
            () => recorder.SpotActorJoins.Contains($"actor-1@{second.SpotRid.ToHex()}")
                && recorder.SpotActorLeaves.Contains($"actor-1@{first.SpotRid.ToHex()}"),
            TimeSpan.FromSeconds(5));

        var contextActor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-context", "test")).Actor;
        await actorRuntime.AttachActorAsync(contextActor, new TestStream("session-context"));
        var contextJoin = await contextActor.Context.JoinSpot(
                first.SpotRid,
                EncodeJoin(new JoinStageRequest("room-context")))
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();
        contextActor.CurrentRoomId = DecodeJoin<JoinStageReply>(contextJoin.Reply).RoomId;

        Assert.Equal(first.SpotRid, contextActor.Spot?.Context.SpotRid);
        Assert.Equal("room-context", contextActor.CurrentRoomId);

        using (var contextDispatchBody = global::Systems.Zlink.Message.From("context-payload"))
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
        Assert.False(await manager.CloseAsync(first.SpotRid));
        Assert.NotNull(await manager.FindAsync(first.SpotRid));

        var stream = new TestStream("session-1");
        await actorRuntime.AttachActorAsync(actor, stream);

        using var header = global::Systems.Zlink.Message.From("header");
        using var body = global::Systems.Zlink.Message.From("payload");
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
        Assert.False(await manager.CloseAsync(second.SpotRid));
        Assert.NotNull(await manager.FindAsync(second.SpotRid));

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
            {
                var mesh = options.AddSpotMesh("actor.factory");
                {
                    var spot = mesh.AddNode("actor-node");
                {
                    var router = spot.EnableRouter(spotNode);

                }

                }

            }
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
