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
        builder.Services.AddScoped<RegistryStageDispatchHandler>();
        builder.Services.AddScoped<EntrySpotRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-entry-local");
                {
                    var spot = mesh.AddNode("join-entry-local-node");
                    spot.EnableRouter(spotNode);
                    spot.AddEntrySpot<RegistryEntrySpot>();
                    spot.AddSpotFactory<RegistryStageSpot>();

                }

            }
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

        _ = await actorRuntime.JoinActorAsync(
            stage.SpotRid,
            actor,
            EncodeJoin(new RegistryJoinRequest("local-room")));
        Assert.Equal(stage.SpotRid, actor.Context.GetSpot<RegistryStageSpot>().Context.SpotRid);

        var joinedBefore = CountEvents(recorder, "entry-joined:join-entry-local-actor");
        var leftBefore = CountEvents(recorder, "left:join-entry-local-actor:");

        using var entryJoinRequest = Message.From(ReadOnlySpan<byte>.Empty);
        var join = await actor.Context.JoinEntrySpot(nodeRid, entryJoinRequest)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();

        Assert.Equal("join-entry-local-actor", join.Actor.ActorId);
        Assert.Equal(nodeRid, join.Actor.NodeRid);
        Assert.Equal(joinedBefore + 1, CountEvents(recorder, "entry-joined:join-entry-local-actor"));
        Assert.Equal(leftBefore + 1, CountEvents(recorder, "left:join-entry-local-actor:"));
        Assert.Contains("entry-joined:join-entry-local-actor", recorder.Events);
        Assert.Contains($"left:join-entry-local-actor:{stage.SpotRid.ToHex()}", recorder.Events);

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
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-entry-idempotent");
                {
                    var spot = mesh.AddNode("join-entry-idempotent-node");
                    spot.EnableRouter(spotNode);
                    spot.AddEntrySpot<RegistryEntrySpot>();
                    spot.AddSpotFactory<RegistryStageSpot>();

                }

            }
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

        _ = await actorRuntime.JoinActorAsync(
            stage.SpotRid,
            actor,
            EncodeJoin(new RegistryJoinRequest("idempotent-room")));
        using var firstEntryJoinRequest = Message.From(ReadOnlySpan<byte>.Empty);
        _ = await actor.Context.JoinEntrySpot(nodeRid, firstEntryJoinRequest)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();

        var joinedBefore = CountEvents(recorder, "entry-joined:join-entry-idempotent-actor");
        var leftBefore = CountEvents(recorder, "left:join-entry-idempotent-actor:");

        using var secondEntryJoinRequest = Message.From(ReadOnlySpan<byte>.Empty);
        var second = await actor.Context.JoinEntrySpot(nodeRid, secondEntryJoinRequest)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();

        Assert.Equal(nodeRid, second.Actor.NodeRid);
        Assert.Equal(joinedBefore, CountEvents(recorder, "entry-joined:join-entry-idempotent-actor"));
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
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-entry-remote");
                {
                    var spot = mesh.AddNode("join-entry-source-node");
                    {
                        var router = spot.EnableRouter(sourceNode);
                        router.ConnectRouter(targetNode);

                    }
                    spot.AddSpotFactory<RegistryStageSpot>();

                }
                {
                    var spot = mesh.AddNode("join-entry-target-node");
                    {
                        var router = spot.EnableRouter(targetNode);
                        router.ConnectRouter(sourceNode);

                    }
                    spot.AddEntrySpot<RegistryEntrySpot>();

                }

            }
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

        _ = await actorRuntime.JoinActorAsync(
            stage.SpotRid,
            actor,
            EncodeJoin(new RegistryJoinRequest("remote-room")));

        using var entryJoinRequest = Message.From("remote-entry");
        var join = await actor.Context.JoinEntrySpot(targetNodeRid, entryJoinRequest)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();
        var replyCanStillUseJoinResult = $"{join.Actor.ActorId}:{join.Actor.NodeRid.ToHex()}";

        Assert.True(join.Accepted);
        Assert.Equal("remote-entry", join.Reply.GetString());
        Assert.Equal(
            $"join-entry-remote-actor:{targetNodeRid.ToHex()}",
            replyCanStillUseJoinResult);
        Assert.Contains($"left:join-entry-remote-actor:{stage.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains("entry-joined:join-entry-remote-actor", recorder.Events);

        Assert.Throws<InvalidOperationException>(() => actor.Context.GetSpot());
        Assert.Throws<InvalidOperationException>(() => actor.Context.BoundSession);
        Assert.Throws<InvalidOperationException>(() =>
            actor.Context.JoinEntrySpot(targetNodeRid, Message.From(ReadOnlySpan<byte>.Empty)));
        Assert.Throws<InvalidOperationException>(
            () => actor.Context.JoinSpot(stage.SpotRid, EncodeJoin(new RegistryJoinRequest("stale"))));

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorContext_RemoteJoin_ThroughDiscoveryRouteMesh_Returns_JoinReply()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var sourceNode = GetFreeTcpEndpoint();
        var targetNode = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        using var registryHost = registryBuilder.Build();
        await registryHost.StartAsync();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-entry-discovery");
                {
                    var spot = mesh.AddNode("join-entry-discovery-source-node");
                    {
                        var router = spot.EnableRouter(sourceNode);
                        router.SetRouterRoutingId(
                            RoutingId.From($"join-entry-discovery-source-{Guid.NewGuid():N}"));
                    }
                    spot.AddSpotFactory<RegistryStageSpot>();
                }
                {
                    var spot = mesh.AddNode("join-entry-discovery-target-node");
                    {
                        var router = spot.EnableRouter(targetNode);
                        router.SetRouterRoutingId(
                            RoutingId.From($"join-entry-discovery-target-{Guid.NewGuid():N}"));
                    }
                    spot.AddEntrySpot<RegistryEntrySpot>();
                }
            }
        });

        using var host = builder.Build();
        await host.StartAsync();

        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
            var targetNodeRid =
                actorRuntime.GetSpotNodeRuntime("join-entry-discovery-target-node").Node.RoutingId;
            var stage = await manager.CreateAsync<RegistryStageSpot>();
            var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync(
                "join-entry-discovery-actor",
                "registry")).Actor;

            _ = await actorRuntime.JoinActorAsync(
                stage.SpotRid,
                actor,
                EncodeJoin(new RegistryJoinRequest("discovery-room")));

            using var entryJoinRequest = Message.From("discovery-entry");
            var join = await actor.Context.JoinEntrySpot(targetNodeRid, entryJoinRequest)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async();

            Assert.True(join.Accepted);
            Assert.Equal("discovery-entry", join.Reply.GetString());
            Assert.Equal(
                $"join-entry-discovery-actor:{targetNodeRid.ToHex()}",
                $"{join.Actor.ActorId}:{join.Actor.NodeRid.ToHex()}");
            Assert.Contains(
                $"left:join-entry-discovery-actor:{stage.SpotRid.ToHex()}",
                recorder.Events);
            Assert.Contains("entry-joined:join-entry-discovery-actor", recorder.Events);
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(host, registryHost);
        }
    }

    [Fact]
    public async Task ActorContext_RemoteUserSpotJoin_ThroughDiscoveryRouteMesh_Returns_JoinReply()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var sourceNodeEndpoint = GetFreeTcpEndpoint();
        var targetNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        using var registryHost = registryBuilder.Build();
        await registryHost.StartAsync();

        var sourceBuilder = Host.CreateApplicationBuilder();
        sourceBuilder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        sourceBuilder.Services.AddScoped<RegistryEntrySpot>();
        sourceBuilder.Services.AddScoped<RegistryStageSpot>();
        sourceBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-user-discovery");
                mesh.UseRegistrySpotResolver();
                {
                    var spot = mesh.AddNode("join-user-discovery-source-node");
                    {
                        var router = spot.EnableRouter(sourceNodeEndpoint);
                        router.SetRouterRoutingId(
                            RoutingId.From($"join-user-discovery-source-{Guid.NewGuid():N}"));
                    }
                    spot.AddEntrySpot<RegistryEntrySpot>();
                    spot.AddSpotFactory<RegistryStageSpot>();
                }
            }
        });
        using var sourceHost = sourceBuilder.Build();
        await sourceHost.StartAsync();

        var targetBuilder = Host.CreateApplicationBuilder();
        targetBuilder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        targetBuilder.Services.AddScoped<RegistryEntrySpot>();
        targetBuilder.Services.AddScoped<RegistryStageSpot>();
        targetBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-user-discovery");
                mesh.UseRegistrySpotResolver();
                {
                    var spot = mesh.AddNode("join-user-discovery-target-node");
                    {
                        var router = spot.EnableRouter(targetNodeEndpoint);
                        router.SetRouterRoutingId(
                            RoutingId.From($"join-user-discovery-target-{Guid.NewGuid():N}"));
                    }
                    spot.AddEntrySpot<RegistryEntrySpot>();
                    spot.AddSpotFactory<RegistryStageSpot>();
                }
            }
        });
        using var targetHost = targetBuilder.Build();
        await targetHost.StartAsync();

        try
        {
            var targetManager = targetHost.Services.GetRequiredService<IZLinkSpotManager>();
            var targetStage = await targetManager.CreateAsync<RegistryStageSpot>();

            var sourceRuntime = sourceHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var sourceRecorder = sourceHost.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
            var targetRecorder = targetHost.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
            var sourceNodeRuntime =
                sourceRuntime.GetSpotNodeRuntime("join-user-discovery-source-node");
            var targetNodeRuntime = targetHost.Services
                .GetRequiredService<ZLinkFrameworkRuntime>()
                .GetSpotNodeRuntime("join-user-discovery-target-node");
            await WaitForDiscoveryPeerAsync(sourceNodeRuntime, targetNodeRuntime);

            var actor = (RegistryTestActor)(await sourceRuntime.CreateLocalActorAsync(
                "join-user-discovery-actor",
                "registry")).Actor;

            using var entryJoinRequest = Message.From("source-entry");
            var entryJoin = await actor.Context.JoinEntrySpot(
                    sourceNodeRuntime.Node.RoutingId,
                    entryJoinRequest)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async();
            Assert.True(entryJoin.Accepted);

            var userJoin = await actor.Context.JoinSpot(
                    targetStage.SpotRid,
                    EncodeJoin(new RegistryJoinRequest("target-room")))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async();

            Assert.True(userJoin.Accepted);
            Assert.Equal("target-room", DecodeJoin<RegistryJoinReply>(userJoin.Reply).RoomId);
            Assert.Equal(
                $"join-user-discovery-actor:{targetNodeRuntime.Node.RoutingId.ToHex()}",
                $"{userJoin.Actor.ActorId}:{userJoin.Actor.NodeRid.ToHex()}");
            Assert.Contains(
                $"joined:join-user-discovery-actor:{targetStage.SpotRid.ToHex()}",
                targetRecorder.Events);
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(targetHost, sourceHost, registryHost);
        }
    }

    [Fact]
    public async Task BoundSessionActorDispatch_RemoteUserSpotJoin_ThroughDiscoveryRouteMesh_Returns_JoinReply()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var sourceNodeEndpoint = GetFreeTcpEndpoint();
        var targetNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        using var registryHost = registryBuilder.Build();
        await registryHost.StartAsync();

        var sourceBuilder = Host.CreateApplicationBuilder();
        sourceBuilder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        sourceBuilder.Services.AddScoped<RegistryEntrySpot>();
        sourceBuilder.Services.AddScoped<RegistryStageSpot>();
        sourceBuilder.Services.AddScoped<RegistryEntryJoinHandler>();
        sourceBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-user-bound-discovery");
                mesh.UseRegistrySpotResolver();
                {
                    var spot = mesh.AddNode("join-user-bound-discovery-source-node");
                    spot.EnableRouter(sourceNodeEndpoint)
                        .SetRouterRoutingId(
                            RoutingId.From($"join-user-bound-discovery-source-{Guid.NewGuid():N}"));
                    spot.AddEntrySpot<RegistryEntrySpot>();
                    spot.AddSpotFactory<RegistryStageSpot>();
                }
            }
        });
        using var sourceHost = sourceBuilder.Build();
        await sourceHost.StartAsync();

        var targetBuilder = Host.CreateApplicationBuilder();
        targetBuilder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        targetBuilder.Services.AddScoped<RegistryEntrySpot>();
        targetBuilder.Services.AddScoped<NotifyingRegistryStageSpot>();
        targetBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-user-bound-discovery");
                mesh.UseRegistrySpotResolver();
                {
                    var spot = mesh.AddNode("join-user-bound-discovery-target-node");
                    spot.EnableRouter(targetNodeEndpoint)
                        .SetRouterRoutingId(
                            RoutingId.From($"join-user-bound-discovery-target-{Guid.NewGuid():N}"));
                    spot.AddEntrySpot<RegistryEntrySpot>();
                    spot.AddSpotFactory<NotifyingRegistryStageSpot>();
                }
            }
        });
        using var targetHost = targetBuilder.Build();
        await targetHost.StartAsync();

        try
        {
            var targetManager = targetHost.Services.GetRequiredService<IZLinkSpotManager>();
            var targetStage = await targetManager.CreateAsync<NotifyingRegistryStageSpot>();

            var sourceRuntime = sourceHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var targetRuntime = targetHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var sourceNodeRuntime =
                sourceRuntime.GetSpotNodeRuntime("join-user-bound-discovery-source-node");
            var targetNodeRuntime =
                targetRuntime.GetSpotNodeRuntime("join-user-bound-discovery-target-node");
            var sourceEntryActivation = sourceNodeRuntime.EntrySpotActivation
                ?? throw new InvalidOperationException("Source Entry Spot activation was not created.");
            await WaitForDiscoveryPeerAsync(sourceNodeRuntime, targetNodeRuntime);

            var actor = (RegistryTestActor)(await sourceRuntime.CreateLocalActorAsync(
                "join-user-bound-discovery-actor",
                "registry")).Actor;
            await sourceRuntime.AttachActorAsync(actor, new TestStream("join-user-bound-discovery-session"));

            using var entryJoinRequest = Message.From("source-entry");
            var entryJoin = await actor.Context.JoinEntrySpot(
                    sourceNodeRuntime.Node.RoutingId,
                    entryJoinRequest)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async();
            Assert.True(entryJoin.Accepted);

            await ZLinkEntrySpotActorDispatcher.DispatchAsync(
                    sourceRuntime,
                    sourceEntryActivation,
                    [
                        CreateEntryActorHeaderPart(
                            sourceRuntime,
                            actor,
                            "entry-join",
                            sourceNodeRuntime.Node.RoutingId),
                        CreateEntryActorBodyPart(
                            sourceRuntime,
                            actor,
                            targetStage.SpotRid.ToHex(),
                            sourceNodeRuntime.Node.RoutingId),
                    ],
                    CancellationToken.None)
                .WaitAsync(TimeSpan.FromSeconds(5));

            var targetRecorder = targetHost.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
            Assert.Equal("entry-room", actor.CurrentRoomId);
            Assert.Contains(
                $"joined:join-user-bound-discovery-actor:{targetStage.SpotRid.ToHex()}",
                targetRecorder.Events);
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(targetHost, sourceHost, registryHost);
        }
    }

    [Fact]
    public async Task ActorContext_JoinEntrySpot_Reject_Keeps_UserSpotActor_InPlace()
    {
        var sourceNode = GetFreeTcpEndpoint();
        var targetNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            {
                var mesh = options.AddSpotMesh("join-entry-reject");
                {
                    var spot = mesh.AddNode("join-entry-reject-source-node");
                    {
                        var router = spot.EnableRouter(sourceNode);
                        router.ConnectRouter(targetNode);
                    }
                    spot.AddSpotFactory<RegistryStageSpot>();
                }
                {
                    var spot = mesh.AddNode("join-entry-reject-target-node");
                    {
                        var router = spot.EnableRouter(targetNode);
                        router.ConnectRouter(sourceNode);
                    }
                    spot.AddEntrySpot<RegistryEntrySpot>();
                }
            }
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var targetNodeRid = actorRuntime.GetSpotNodeRuntime("join-entry-reject-target-node").Node.RoutingId;
        var stage = await manager.CreateAsync<RegistryStageSpot>();
        var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync(
            "join-entry-reject-actor",
            "registry")).Actor;

        _ = await actorRuntime.JoinActorAsync(
            stage.SpotRid,
            actor,
            EncodeJoin(new RegistryJoinRequest("reject-room")));

        var joinedBefore = CountEvents(recorder, "entry-joined:join-entry-reject-actor");
        var leftBefore = CountEvents(recorder, "left:join-entry-reject-actor:");

        using var entryJoinRequest = Message.From("reject-entry");
        var rejected = await actor.Context.JoinEntrySpot(targetNodeRid, entryJoinRequest)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();

        Assert.False(rejected.Accepted);
        Assert.Equal("entry-reject:reject-entry", rejected.Reply.GetString());
        Assert.Equal(stage.SpotRid, actor.Context.GetSpot<RegistryStageSpot>().Context.SpotRid);
        Assert.Equal(joinedBefore, CountEvents(recorder, "entry-joined:join-entry-reject-actor"));
        Assert.Equal(leftBefore, CountEvents(recorder, "left:join-entry-reject-actor:"));

        await host.StopAsync();
    }

    private static int CountEvents(EntrySpotActorRegistryRecorder recorder, string prefix)
    {
        return recorder.Events.Count(entry => entry.StartsWith(prefix, StringComparison.Ordinal));
    }

    private static async Task WaitForDiscoveryPeerAsync(
        ZLinkSpotNodeRuntime sourceNodeRuntime,
        ZLinkSpotNodeRuntime targetNodeRuntime)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (DateTime.UtcNow < deadline)
        {
            if (HasRouterPeer(sourceNodeRuntime)
                && HasRouterPeer(targetNodeRuntime))
            {
                return;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(150));
        }

        throw new TimeoutException(
            "Discovery-backed spot node peers were not discovered. "
            + $"source=[{DescribePeers(sourceNodeRuntime)}], "
            + $"target=[{DescribePeers(targetNodeRuntime)}]");
    }

    private static bool HasRouterPeer(ZLinkSpotNodeRuntime runtime)
    {
        return runtime.Node.Peers().Any(peer =>
            peer.Kind == ZLinkSpotPeerKind.RouterChannel);
    }

    private static string DescribePeers(ZLinkSpotNodeRuntime runtime)
    {
        return string.Join(
            ", ",
            runtime.Node.Peers().Select(peer =>
                $"{peer.ChannelName}:{peer.PeerEndpoint}:{peer.Source}:{peer.Kind}:{peer.State}"));
    }
}
