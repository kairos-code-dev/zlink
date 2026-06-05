using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

public sealed class ActorJoinEntrySpotTests : SpotTestSupport
{
    [Fact]
    public async Task ActorContext_JoinEntrySpot_Moves_UserSpotActor_To_EntrySpot()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddScoped<RegistryStageJoinHandler>();
        builder.Services.AddScoped<RegistryStageDispatchHandler>();
        builder.Services.AddScoped<RegistryStageJoinedHandler>();
        builder.Services.AddScoped<RegistryStageLeftHandler>();
        builder.Services.AddScoped<RegistryEntryJoinedHandler>();
        builder.Services.AddScoped<RegistryEntryLeftHandler>();
        builder.Services.AddScoped<EntrySpotRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotMesh("join-entry-local", mesh =>
            {
                mesh.AddNode("join-entry-local-node", spot =>
                {
                    spot.EnableRouter(router => router.BindRouter(spotNode));
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
        var nodeRid = actorRuntime.GetSpotNodeRuntime("join-entry-local-node").Node.RoutingId;
        var stage = await manager.CreateAsync<RegistryStageSpot>();
        var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync(
            "join-entry-local-actor",
            "registry")).Actor;

        _ = await actorRuntime.JoinActorAsync<RegistryJoinRequest, RegistryJoinReply>(
            stage.SpotRid,
            actor,
            new RegistryJoinRequest("local-room"));
        Assert.Equal(stage.SpotRid, actor.Context.GetSpot<RegistryStageSpot>().Context.SpotRid);

        var joinedBefore = CountEvents(recorder, "entry-joined:join-entry-local-actor:");
        var leftBefore = CountEvents(recorder, "left:join-entry-local-actor:");

        var join = await actor.Context.JoinEntrySpot(nodeRid)
            .Timeout(TimeSpan.FromSeconds(5))
            .SubmitAsync();

        Assert.Equal("join-entry-local-actor", join.ActorId);
        Assert.Equal(nodeRid, join.NodeRid);
        Assert.Equal(joinedBefore + 1, CountEvents(recorder, "entry-joined:join-entry-local-actor:"));
        Assert.Equal(leftBefore + 1, CountEvents(recorder, "left:join-entry-local-actor:"));
        Assert.Contains("entry-joined:join-entry-local-actor:JoinEntrySpot", recorder.Events);
        Assert.Contains("entry-joined-kind:join-entry-local-actor:JoinEntrySpot", recorder.Events);
        Assert.Contains($"left:join-entry-local-actor:{stage.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains("left-kind:join-entry-local-actor:JoinEntrySpot", recorder.Events);

        using (var dispatchBody = Message.From("after-entry"))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "entry-record",
                    ZlinkStreamMetadata.Empty),
                dispatchBody);
        }

        Assert.Contains("record:join-entry-local-actor:after-entry", mailboxRecorder.Events);

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorContext_JoinEntrySpot_Is_Idempotent_WhenAlreadyAtEntrySpot()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddScoped<RegistryStageJoinHandler>();
        builder.Services.AddScoped<RegistryStageJoinedHandler>();
        builder.Services.AddScoped<RegistryStageLeftHandler>();
        builder.Services.AddScoped<RegistryEntryJoinedHandler>();
        builder.Services.AddScoped<RegistryEntryLeftHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotMesh("join-entry-idempotent", mesh =>
            {
                mesh.AddNode("join-entry-idempotent-node", spot =>
                {
                    spot.EnableRouter(router => router.BindRouter(spotNode));
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
        var nodeRid = actorRuntime.GetSpotNodeRuntime("join-entry-idempotent-node").Node.RoutingId;
        var stage = await manager.CreateAsync<RegistryStageSpot>();
        var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync(
            "join-entry-idempotent-actor",
            "registry")).Actor;

        _ = await actorRuntime.JoinActorAsync<RegistryJoinRequest, RegistryJoinReply>(
            stage.SpotRid,
            actor,
            new RegistryJoinRequest("idempotent-room"));
        _ = await actor.Context.JoinEntrySpot(nodeRid)
            .Timeout(TimeSpan.FromSeconds(5))
            .SubmitAsync();

        var joinedBefore = CountEvents(recorder, "entry-joined:join-entry-idempotent-actor:");
        var leftBefore = CountEvents(recorder, "left:join-entry-idempotent-actor:");

        var second = await actor.Context.JoinEntrySpot(nodeRid)
            .Timeout(TimeSpan.FromSeconds(5))
            .SubmitAsync();

        Assert.Equal(nodeRid, second.NodeRid);
        Assert.Equal(joinedBefore, CountEvents(recorder, "entry-joined:join-entry-idempotent-actor:"));
        Assert.Equal(leftBefore, CountEvents(recorder, "left:join-entry-idempotent-actor:"));

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorContext_RemoteJoin_Invalidates_SourceContext()
    {
        var sourceNode = GetFreeTcpEndpoint();
        var targetNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddScoped<RegistryStageJoinHandler>();
        builder.Services.AddScoped<RegistryStageJoinedHandler>();
        builder.Services.AddScoped<RegistryStageLeftHandler>();
        builder.Services.AddScoped<RegistryEntryJoinedHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotMesh("join-entry-remote", mesh =>
            {
                mesh.AddNode("join-entry-source-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(sourceNode);
                        router.UseManualConnections(peers => peers.Connect(targetNode));
                    });
                    spot.AddSpotFactory<RegistryStageSpot>();
                });
                mesh.AddNode("join-entry-target-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(targetNode);
                        router.UseManualConnections(peers => peers.Connect(sourceNode));
                    });
                    spot.AddEntrySpot<RegistryEntrySpot>();
                });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var targetNodeRid = actorRuntime.GetSpotNodeRuntime("join-entry-target-node").Node.RoutingId;
        var stage = await manager.CreateAsync<RegistryStageSpot>();
        var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync(
            "join-entry-remote-actor",
            "registry")).Actor;

        _ = await actorRuntime.JoinActorAsync<RegistryJoinRequest, RegistryJoinReply>(
            stage.SpotRid,
            actor,
            new RegistryJoinRequest("remote-room"));

        var join = await actor.Context.JoinEntrySpot(targetNodeRid)
            .Timeout(TimeSpan.FromSeconds(5))
            .SubmitAsync();
        var replyCanStillUseJoinResult = $"{join.ActorId}:{join.NodeRid.ToHex()}";

        Assert.Equal(
            $"join-entry-remote-actor:{targetNodeRid.ToHex()}",
            replyCanStillUseJoinResult);
        Assert.Contains($"left:join-entry-remote-actor:{stage.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains("left-kind:join-entry-remote-actor:JoinEntrySpot", recorder.Events);
        Assert.Contains("entry-joined:join-entry-remote-actor:JoinEntrySpot", recorder.Events);
        Assert.Contains("entry-joined-kind:join-entry-remote-actor:JoinEntrySpot", recorder.Events);

        Assert.Throws<InvalidOperationException>(() => actor.Context.GetSpot());
        Assert.Throws<InvalidOperationException>(() => actor.Context.BoundSession);
        Assert.Throws<InvalidOperationException>(() => actor.Context.JoinEntrySpot(targetNodeRid));
        Assert.Throws<InvalidOperationException>(
            () => actor.Context.JoinSpot(stage.SpotRid, new RegistryJoinRequest("stale")));

        await host.StopAsync();
    }

    private static int CountEvents(EntrySpotActorRegistryRecorder recorder, string prefix)
    {
        return recorder.Events.Count(entry => entry.StartsWith(prefix, StringComparison.Ordinal));
    }
}
