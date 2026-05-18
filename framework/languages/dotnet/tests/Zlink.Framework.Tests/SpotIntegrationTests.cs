using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Tests;

public sealed class SpotIntegrationTests
{
    private static readonly TimeSpan PollingInterval = TimeSpan.FromMilliseconds(150);
    private static readonly object PortLock = new();
    private static readonly HashSet<int> AllocatedPorts = [];

    [Fact]
    public async Task SpotManager_Create_List_Remove_And_Publish_Work_Through_FrameworkRuntime()
    {
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();

        using var host = await CreateHostAsync(ordersServer, spotNode);
        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var events = host.Services.GetRequiredService<SpotEventsRecorder>();
        var orders = host.Services.GetRequiredService<OrdersRecorder>();

        var first = await manager.CreateAsync("stage");

        await RetryAsync(
            () => events.Initialized.Count >= 1
                && orders.ReceivedScopes.Count >= 1,
            TimeSpan.FromSeconds(5));

        Assert.True(first.Created);

        var firstInfo = await manager.GetAsync(first.SpotRid);
        Assert.Equal("stage", firstInfo?.SpotName);

        var listed = await manager.ListAsync();
        Assert.Single(listed);

        Assert.Contains(events.ScopeId(first.SpotRid), orders.ReceivedScopes);

        Assert.True(await manager.RemoveAsync(first.SpotRid));
        Assert.Contains(first.SpotRid, events.Closing);
        Assert.Null(await manager.GetAsync(first.SpotRid));
        Assert.Empty(await manager.ListAsync());

        var firstScope = events.ScopeId(first.SpotRid);
        var second = await manager.CreateAsync("stage");
        await RetryAsync(
            () => events.Initialized.Count >= 2
                && orders.ReceivedScopes.Count >= 2,
            TimeSpan.FromSeconds(5));
        Assert.True(second.Created);
        Assert.NotEqual(first.SpotRid, second.SpotRid);
        Assert.NotEqual(firstScope, events.ScopeId(second.SpotRid));
    }

    [Fact]
    public async Task Spot_Publish_Timer_And_Remove_Stop_Callbacks_Work()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.stage.timer.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        publisherBuilder.Services.AddScoped<SpotHeartbeatTimerHandler>();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery(spotChannel, discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("publisher-node", spot =>
            {
                spot.Bind(publisherNodeEndpoint);
                spot.EnablePubSub();
                spot.AddSpotFactory<PublishingStageSpot>("publisher-stage");
            });
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        subscriberBuilder.Services.AddScoped<LocalStageEventHandler>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery(spotChannel, discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("subscriber-node", spot =>
            {
                spot.Bind(subscriberNodeEndpoint);
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.UseManualConnections(connections =>
                        connections.Connect(publisherNodeEndpoint));
                });
                spot.AddSpotFactory<LocalSubscriberStageSpot>("subscriber-stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var publisherHost = publisherBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();

        await registryHost.StartAsync();
        await publisherHost.StartAsync();
        await subscriberHost.StartAsync();

        var publisherManager = publisherHost.Services.GetRequiredService<IZLinkSpotManager>();
        var subscriberManager = subscriberHost.Services.GetRequiredService<IZLinkSpotManager>();
        var publisherRecorder = publisherHost.Services.GetRequiredService<SpotLifecycleRecorder>();

        _ = await subscriberManager.CreateAsync("subscriber-stage");
        var created = await publisherManager.CreateAsync("publisher-stage");

        await RetryAsync(
            () => publisherRecorder.TickCount >= 2,
            TimeSpan.FromSeconds(10));

        var ticksBeforeRemove = publisherRecorder.TickCount;

        Assert.True(await publisherManager.RemoveAsync(created.SpotRid));
        await Task.Delay(300);
        Assert.Equal(ticksBeforeRemove, publisherRecorder.TickCount);

        await subscriberHost.StopAsync();
        await publisherHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task OutboundOnly_SpotPublisherClient_Publishes_To_TargetChannel()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        subscriberBuilder.Services.AddScoped<ExternalStageEventHandler>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("subscriber-node", spot =>
            {
                spot.Bind(subscriberNodeEndpoint);
                spot.EnablePubSub();
                spot.AddSpotFactory<ExternalSubscriberStageSpot>("subscriber-stage");
            });
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("publisher-node", spot =>
            {
                spot.Bind(publisherNodeEndpoint);
                spot.EnablePubSub();
                spot.AttachSpotMeshPublisherClient("game.stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await registryHost.StartAsync();
        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var manager = subscriberHost.Services.GetRequiredService<IZLinkSpotManager>();
        _ = await manager.CreateAsync("subscriber-stage");

        var publisher = publisherHost.Services.GetRequiredService<IZLinkSpotPublisherClient>();
        var recorder = subscriberHost.Services.GetRequiredService<SpotLifecycleRecorder>();
        var publisherRuntime = publisherHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var subscriberRuntime = subscriberHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();

        await RetryAsync(
            async () => (await manager.ListAsync()).Count,
            count => count == 1,
            TimeSpan.FromSeconds(10));

        try
        {
            await RetryAsync(
                async () =>
	                {
	                    await publisher.Publish(
	                            "game.stage",
	                            "stage.external",
	                            new ExternalStageEvent("external"))
	                        .Submit();
	                    await Task.Yield();
	                    return recorder.ExternalEvents.Count;
	                },
                count => count > 0,
                TimeSpan.FromSeconds(10));
        }
        catch (Exception ex) when (ex is TimeoutException or ZlinkException)
        {
            var publisherSnapshot = publisherRuntime.GetSpotMonitoringSnapshot("publisher-node");
            var subscriberSnapshot = subscriberRuntime.GetSpotMonitoringSnapshot("subscriber-node");
            var publisherBundle = publisherRuntime.GetSpotPublisherBundle("game.stage");
            var directPublishResult = false;
            var rawReceived = "probe-unavailable";
            var probeParts = ZLinkEnvelopeCodec.EncodeParts(
                new ZLinkEnvelopeHeader(
                    ZLinkMessageKind.Publish,
                    "game.stage",
                    nameof(ExternalStageEvent),
                    ZLinkEnvelopeCodec.DefaultContentType,
                    null,
                    null,
                    "stage.external",
                    null,
                    null),
                new ExternalStageEvent("probe"),
                typeof(ExternalStageEvent));
            try
            {
                directPublishResult = publisherBundle.Spot.Publish(
                    "stage.external",
                    probeParts,
                    global::Systems.Zlink.SendFlags.None);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(probeParts);
            }

            var subscriberActivation = GetSingleSpotActivation(subscriberRuntime, "subscriber-node");
            var pumpState = GetSubscriptionPumpState(subscriberActivation);
            var subscriptionState =
                $"Messages={subscriberActivation.SubscriptionMessageCount},Dispatches={subscriberActivation.SubscriptionDispatchCount},Ignores={subscriberActivation.SubscriptionIgnoreCount},LastTopic={subscriberActivation.LastSubscriptionTopic},LastPacket={subscriberActivation.LastSubscriptionMessageName}";
            var publisherPeers = string.Join(';',
                publisherSnapshot.Peers.Select(static entry =>
                    $"{entry.Source}:{entry.PeerEndpoint}:{entry.State}:{entry.ChannelName}"));
            var subscriberPeers = string.Join(';',
                subscriberSnapshot.Peers.Select(static entry =>
                    $"{entry.Source}:{entry.PeerEndpoint}:{entry.State}:{entry.ChannelName}"));
            throw new TimeoutException(
                $"SPOT outbound publish failed. PublisherStatus={publisherSnapshot.Status}, PublisherPeers={publisherPeers}, PublisherSubjects={string.Join(';', publisherSnapshot.Subjects.Select(static entry => $"{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}/{entry.ActivePeerCount}"))}, SubscriberStatus={subscriberSnapshot.Status}, SubscriberPeers={subscriberPeers}, SubscriberSubjects={string.Join(';', subscriberSnapshot.Subjects.Select(static entry => $"{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}/{entry.ActivePeerCount}"))}, DirectPublishResult={directPublishResult}, RawReceived={rawReceived}, PumpState={pumpState}, SubscriptionState={subscriptionState}, ExternalEvents={recorder.ExternalEvents.Count}",
                ex);
        }

        Assert.Contains("external", recorder.ExternalEvents);

        await publisherHost.StopAsync();
        await subscriberHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task OutboundOnly_SpotPublisherClient_Reaches_RawSubscriber_On_FrameworkNode()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("subscriber-node", spot =>
            {
                spot.Bind(subscriberNodeEndpoint);
                spot.EnablePubSub();
            });
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("publisher-node", spot =>
            {
                spot.Bind(publisherNodeEndpoint);
                spot.EnablePubSub();
                spot.AttachSpotMeshPublisherClient("game.stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await registryHost.StartAsync();
        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var publisherRuntime = publisherHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var subscriberRuntime = subscriberHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var publisher = publisherHost.Services.GetRequiredService<IZLinkSpotPublisherClient>();
        var subscriberNodeRuntime = GetSpotNodeRuntime(subscriberRuntime, "subscriber-node");

        await using var rawSubscriber = subscriberNodeRuntime.Node.CreateSpot();
        rawSubscriber.SetSubscription("stage.external");

        using var received = new global::Systems.Zlink.TopicMessage();
        await RetryAsync(
            async () =>
            {
                await publisher.Publish(
                        "game.stage",
                        "stage.external",
                        new ExternalStageEvent("raw"))
                    .Submit();
                await Task.Yield();

                try
                {
                    return rawSubscriber.Subscribe(received,
                        global::Systems.Zlink.RecvFlags.DontWait);
                }
                catch (global::Systems.Zlink.ZlinkRecvException ex)
                    when (ex.Result == global::Systems.Zlink.ZlinkRecvException.ErrorCode.NoData)
                {
                    return false;
                }
            },
            static ok => ok,
            TimeSpan.FromSeconds(10));

        Assert.Equal("stage.external", received.Topic);

        await publisherHost.StopAsync();
        await subscriberHost.StopAsync();
        await registryHost.StopAsync();
    }

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
            options.UseSpotDiscovery("game.stage", _ => { });
            options.AddActorFactory<TestActorFactory>("test");
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddSpotFactory<ActorStageSpot>("actor-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();

        var first = await manager.CreateAsync("actor-stage");
        var second = await manager.CreateAsync("actor-stage");
        var actor = new TestActor("actor-1", recorder);

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

        var contextActor = new TestActor("actor-context", recorder);
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
            options.UseSpotDiscovery("actor.factory", _ => { });
            options.AddActorFactory<ConcurrentActorFactory>("test");
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
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

    [Fact]
    public async Task EntrySpot_And_UserSpot_ActorPacketRegistries_Dispatch_ActorPackets()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddScoped<RegistryEntryJoinHandler>();
        builder.Services.AddScoped<RegistryStageJoinHandler>();
        builder.Services.AddScoped<RegistryStageDispatchHandler>();
        builder.Services.AddScoped<RegistryStageJoinedHandler>();
        builder.Services.AddScoped<RegistryStageLeftHandler>();
        builder.Services.AddScoped<RegistryEntryJoinedHandler>();
        builder.Services.AddScoped<RegistryEntryLeftHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.registry", _ => { });
            options.AddSpotNode("registry-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
                spot.AddSpotFactory<RegistryStageSpot>("registry-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var first = await manager.CreateAsync("registry-stage");
        var second = await manager.CreateAsync("registry-stage");
        var actor = new RegistryTestActor("registry-actor", recorder);
        await actorRuntime.AttachActorAsync(actor, new TestStream("registry-session"));

        using (var joinBody = global::Systems.Zlink.Message.FromString(first.SpotRid.ToHex()))
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
        Assert.Contains($"entry-left:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains($"joined:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);

        using (var dispatchBody = global::Systems.Zlink.Message.FromString("payload"))
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

        _ = await actorRuntime.JoinActorAsync<RegistryJoinRequest, RegistryJoinReply>(
            second.SpotRid,
            actor,
            new RegistryJoinRequest("second-room"));

        Assert.Contains($"left:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains($"joined:registry-actor:{second.SpotRid.ToHex()}", recorder.Events);

        var currentSpot = actor.Spot ?? throw new InvalidOperationException("Actor is not joined.");
        await currentSpot.Context.LeaveActorAsync(actor);
        actor.DetachSpot(currentSpot);

        Assert.Contains($"entry-joined:registry-actor:{second.SpotRid.ToHex()}", recorder.Events);

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
        builder.Services.AddScoped<RegistryStageJoinHandler>();
        builder.Services.AddScoped<RegistryStageDispatchHandler>();
        builder.Services.AddScoped<RegistryStageJoinedHandler>();
        builder.Services.AddScoped<RegistryStageLeftHandler>();
        builder.Services.AddScoped<RegistryEntryJoinedHandler>();
        builder.Services.AddScoped<RegistryEntryLeftHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.registry-location", _ => { });
            options.AddSpotNode("registry-location-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
                spot.AddSpotFactory<RegistryStageSpot>("registry-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var stage = await manager.CreateAsync("registry-stage");
        var actor = new RegistryTestActor("registry-location-actor", recorder);
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

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_ActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<EntrySpotBlockingHandler>();
        builder.Services.AddScoped<EntrySpotRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.entry-mailbox", _ => { });
            options.AddSpotNode("entry-mailbox-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var actorA = new RegistryTestActor("entry-actor-a", registryRecorder);
        var actorB = new RegistryTestActor("entry-actor-b", registryRecorder);
        await actorRuntime.AttachActorAsync(actorA, new TestStream("entry-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("entry-session-b"));

        Task? actorABlocked = null;
        Task? actorASecond = null;
        try
        {
            actorABlocked = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "entry-block",
                "block-a");
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

            actorASecond = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "entry-record",
                "after-a");
            var actorBPacket = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorB,
                "entry-record",
                "record-b");

            await actorBPacket.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Contains("record:entry-actor-b:record-b", mailboxRecorder.Events);
            Assert.False(actorASecond.IsCompleted, "same actor packet ran before the blocking packet completed.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await actorABlocked!.WaitAsync(TimeSpan.FromSeconds(5));
        await actorASecond!.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("record:entry-actor-a:after-a", mailboxRecorder.Events);

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_NativeActorReadableBatch_Dispatches_Actors_In_Parallel()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<EntrySpotBlockingHandler>();
        builder.Services.AddScoped<EntrySpotRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.entry-native-batch", _ => { });
            options.AddSpotNode("entry-native-batch-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var activation = actorRuntime
            .GetSpotNodeRuntime("entry-native-batch-node")
            .EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation was not created.");
        var actorA = new RegistryTestActor("entry-native-a", registryRecorder);
        var actorB = new RegistryTestActor("entry-native-b", registryRecorder);
        await actorRuntime.AttachActorAsync(actorA, new TestStream("entry-native-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("entry-native-session-b"));

        var dispatch = ZLinkEntrySpotActorDispatcher.DispatchAsync(
            actorRuntime,
            activation,
            [
                CreateEntryActorHeaderPart(actorA, "entry-block"),
                CreateEntryActorBodyPart(actorA, "block-a"),
                CreateEntryActorHeaderPart(actorB, "entry-record"),
                CreateEntryActorBodyPart(actorB, "record-b"),
                CreateEntryActorHeaderPart(actorA, "entry-record"),
                CreateEntryActorBodyPart(actorA, "after-a"),
            ],
            CancellationToken.None);

        try
        {
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
            await RetryAsync(
                () => mailboxRecorder.Events.Contains("record:entry-native-b:record-b"),
                TimeSpan.FromSeconds(5));
            Assert.DoesNotContain("record:entry-native-a:after-a", mailboxRecorder.Events);
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await dispatch.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("record:entry-native-a:after-a", mailboxRecorder.Events);

        await host.StopAsync();
    }

    [Fact]
    public async Task LocalActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<LocalActorBlockingHandler>();
        builder.Services.AddScoped<LocalActorRecordingHandler>();
        builder.Services.AddZLinkFramework(_ => { });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var actorA = new RegistryTestActor("local-actor-a", registryRecorder);
        var actorB = new RegistryTestActor("local-actor-b", registryRecorder);
        await actorRuntime.AttachActorAsync(actorA, new TestStream("local-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("local-session-b"));

        Task? actorABlocked = null;
        Task? actorASecond = null;
        try
        {
            actorABlocked = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "local-block",
                "block-a");
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

            actorASecond = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "local-record",
                "after-a");
            var actorBPacket = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorB,
                "local-record",
                "record-b");

            await actorBPacket.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Contains("local-record:local-actor-b:record-b", mailboxRecorder.Events);
            Assert.False(actorASecond.IsCompleted, "same actor packet ran before the blocking packet completed.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await actorABlocked!.WaitAsync(TimeSpan.FromSeconds(5));
        await actorASecond!.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("local-record:local-actor-a:after-a", mailboxRecorder.Events);

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorContext_RequestChannel_Uses_Global_Client_Before_Join_And_Spot_Client_After_Join()
    {
        var preJoinApi = GetFreeTcpEndpoint();
        var postJoinApi = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddScoped<ActorJoinHandler>();
        builder.Services.AddScoped<ActorPreJoinChannelHandler>();
        builder.Services.AddScoped<ActorPostJoinChannelHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<SpotIntegrationTests>();
            options.UseSpotDiscovery("game.stage", _ => { });
            options.AddClientServerChannel("actor-pre-api", channel =>
            {
                channel.EnableServer(server => server.Bind(preJoinApi));
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(preJoinApi));
                });
            });
            options.AddClientServerChannel("actor-post-api", channel =>
            {
                channel.EnableServer(server => server.Bind(postJoinApi));
            });
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AttachClientServerChannelClient("actor-post-api", client =>
                {
                    client.UseManualConnections(connections => connections.Connect(postJoinApi));
                });
                spot.AddSpotFactory<ActorStageSpot>("actor-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();
        var created = await manager.CreateAsync("actor-stage");
        var actor = new TestActor("actor-context-client", recorder);
        await actorRuntime.AttachActorAsync(actor, new TestStream("session-context-client"));

        using (var preJoinBody = global::Systems.Zlink.Message.FromString("before"))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "request-channel-before-join",
                    ZlinkStreamMetadata.Empty),
                preJoinBody);
        }

        Assert.Contains("before:before", recorder.ChannelReplies);

        _ = await actorRuntime.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            created.SpotRid,
            actor,
            new JoinStageRequest("room-context-client"));

        using (var postJoinBody = global::Systems.Zlink.Message.FromString("after"))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "request-channel-after-join",
                    ZlinkStreamMetadata.Empty),
                postJoinBody);
        }

        Assert.Contains("after:after", recorder.ChannelReplies);

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddScoped<ActorJoinHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", _ => { });
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddSpotFactory<ActorStageSpot>("actor-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();
        var created = await manager.CreateAsync("actor-stage");
        var actor = new TestActor("actor-2", recorder);

        _ = await actorRuntime.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            created.SpotRid,
            actor,
            new JoinStageRequest("room-disconnect"));

        var staleStream = new TestStream("session-stale");
        var currentStream = new TestStream("session-current");
        await actorRuntime.AttachActorAsync(actor, staleStream);
        await actorRuntime.AttachActorAsync(actor, currentStream);

        await actorRuntime.DisconnectActorAsync(actor, staleStream);
        Assert.Equal(0, recorder.DisconnectCount);
        await actorRuntime.DisconnectActorAsync(actor, currentStream);
        Assert.Equal(1, recorder.DisconnectCount);

        await host.StopAsync();
    }

    private static async Task<IHost> CreateHostAsync(
        string ordersServer,
        string spotNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotEventsRecorder>();
        builder.Services.AddSingleton<OrdersRecorder>();
        builder.Services.AddScoped<SpotScopeMarker>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<SpotIntegrationTests>();
            options.UseSpotDiscovery("game.stage", _ => { });

            options.AddClientServerChannel("orders", channel =>
            {
                channel.EnableServer(server => server.Bind(ordersServer));
            });

            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AttachClientServerChannelClient("orders", client =>
                {
                    client.UseManualConnections(connections => connections.Connect(ordersServer));
                });
                spot.AddSpotFactory<StageSpot>("stage");
            });
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private static async Task RetryAsync(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
            {
                return;
            }

            await Task.Delay(PollingInterval);
        }

        throw new TimeoutException("SPOT integration retry timed out.");
    }

    private static async Task<T> RetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        Exception? lastError = null;

        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(PollingInterval);
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("SPOT integration retry timed out.");
    }

    private static string GetFreeTcpEndpoint()
    {
        while (true)
        {
            using var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            var endpoint = (IPEndPoint)listener.LocalEndpoint;
            lock (PortLock)
            {
                if (AllocatedPorts.Add(endpoint.Port))
                {
                    return $"tcp://127.0.0.1:{endpoint.Port}";
                }
            }
        }
    }

    public sealed class StageSpot : IZLinkSpot
    {
        private readonly SpotScopeMarker _scopeMarker;
        private readonly SpotEventsRecorder _events;
        private readonly IZLinkSpotClient _spotClient;

        public StageSpot(
            IZLinkSpotContext context,
            SpotScopeMarker scopeMarker,
            SpotEventsRecorder events,
            IZLinkSpotClient spotClient)
        {
            Context = context;
            _scopeMarker = scopeMarker;
            _events = events;
            _spotClient = spotClient;
        }

        public IZLinkSpotContext Context { get; }

        public string ScopeId => _scopeMarker.Id;

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _events.RecordInitialized(Context.SpotRid, _scopeMarker.Id);

            await _spotClient.SendChannel("orders", new StageBootCommand(_scopeMarker.Id)).Submit(cancellationToken);
        }

        public ValueTask OnClosingAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _events.RecordClosing(Context.SpotRid);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class ActorStageSpot : IZLinkSpot
    {
        private readonly ActorIntegrationRecorder _recorder;
        private int _inFlight;

        public ActorStageSpot(IZLinkSpotContext context, ActorIntegrationRecorder recorder)
        {
            Context = context;
            _recorder = recorder;
        }

        public IZLinkSpotContext Context { get; }

        public void Configure()
        {
            Context.AddActorJoin<ActorJoinHandler, TestActor, JoinStageRequest, JoinStageReply>();
        }

        internal IDisposable EnterScope(string source)
        {
            if (Interlocked.Increment(ref _inFlight) != 1)
            {
                _recorder.ConcurrentViolation = true;
                _recorder.ScopeViolations.Enqueue(source);
            }

            return new ScopeLease(this);
        }

        internal async ValueTask<JoinStageReply> JoinActorAsync(
            TestActor actor,
            JoinStageRequest request,
            CancellationToken cancellationToken)
        {
            using var _ = EnterScope("join");

            if (actor.Spot is ActorStageSpot current && !ReferenceEquals(current, this))
            {
                await current.Context.LeaveActorAsync(actor, cancellationToken);
                actor.DetachSpot(current);
            }

            await Context.JoinActorAsync(actor, cancellationToken);
            actor.AttachSpot(this);
            actor.CurrentRoomId = request.RoomId;

            return new JoinStageReply(request.RoomId);
        }

        internal async ValueTask LeaveActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            using var _ = EnterScope("leave");

            await Context.LeaveActorAsync(actor, cancellationToken);
            actor.DetachSpot(this);
            actor.CurrentRoomId = null;
        }

        public ValueTask OnActorJoinedAsync(
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (info.CurrentActorId is { } actorId
                && info.CurrentSpotRid is { } spotRid)
            {
                _recorder.SpotActorJoins.Enqueue($"{actorId}@{spotRid.ToHex()}");
            }

            return ValueTask.CompletedTask;
        }

        public ValueTask OnActorLeftAsync(
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (info.PreviousActorId is { } actorId
                && info.PreviousSpotRid is { } spotRid)
            {
                _recorder.SpotActorLeaves.Enqueue($"{actorId}@{spotRid.ToHex()}");
            }

            return ValueTask.CompletedTask;
        }

        private sealed class ScopeLease(ActorStageSpot spot) : IDisposable
        {
            public void Dispose()
            {
                Interlocked.Decrement(ref spot._inFlight);
            }
        }
    }

    public sealed record JoinStageRequest(string RoomId);

    public sealed record JoinStageReply(string RoomId);

    public sealed class ActorJoinHandler : IZLinkSpotActorJoinHandler<ActorStageSpot, TestActor, JoinStageRequest, JoinStageReply>
    {
        public ValueTask<JoinStageReply> HandleAsync(
            ActorStageSpot spot,
            TestActor actor,
            JoinStageRequest request,
            CancellationToken cancellationToken)
        {
            return spot.JoinActorAsync(actor, request, cancellationToken);
        }
    }

    public sealed class TestActor : IZLinkActor
    {
        public TestActor(string actorId, ActorIntegrationRecorder recorder)
        {
            ActorId = actorId;
            Recorder = recorder;
        }

        public string ActorId { get; }

        public IZLinkActorContext Context { get; set; } = default!;

        public ActorIntegrationRecorder Recorder { get; }

        public ActorStageSpot? Spot { get; private set; }

        public string? CurrentRoomId { get; set; }

        public void Configure()
        {
            Context.AddPacket<ActorPreJoinChannelHandler>("request-channel-before-join");
            Context.AddPacket<ActorPostJoinChannelHandler>("request-channel-after-join");
            Context.AddPacket<ActorJoinViaContextHandler>("join-via-context");
            Context.AddPacket<ActorDispatchHandler>("dispatch");
            Context.AddPacket<ActorDispatchHandler>("dispatch-after-context-join");
        }

        public void AttachSpot(ActorStageSpot spot)
        {
            Spot = spot;
        }

        public void DetachSpot(ActorStageSpot spot)
        {
            if (ReferenceEquals(Spot, spot))
            {
                Spot = null;
            }
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            Interlocked.Increment(ref Recorder.DisconnectCount);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class ActorPreJoinChannelHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public async ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.RequestChannel(
                    "actor-pre-api",
                    new ActorContextChannelReq(message))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<ActorContextChannelRes>(cancellationToken);

            actor.Recorder.ChannelReplies.Enqueue($"before:{reply.Value}");
        }
    }

    public sealed class ActorPostJoinChannelHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public async ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.RequestChannel(
                    "actor-post-api",
                    new ActorContextChannelReq(message))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<ActorContextChannelRes>(cancellationToken);

            actor.Recorder.ChannelReplies.Enqueue($"after:{reply.Value}");
        }
    }

    public sealed class ActorJoinViaContextHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public async ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(message),
                    new JoinStageRequest("room-context"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<JoinStageReply>(cancellationToken);

            actor.CurrentRoomId = reply.RoomId;
        }
    }

    public sealed class ActorDispatchHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            var stageSpot = actor.Context.GetSpot<ActorStageSpot>();

            if (!ReferenceEquals(actor.Spot, stageSpot))
            {
                throw new InvalidOperationException("Actor context SPOT does not match actor SPOT.");
            }

            using var scope = stageSpot.EnterScope("dispatch");
            actor.Recorder.DispatchBodies.Enqueue(message);
            actor.Recorder.DispatchRooms.Enqueue(actor.CurrentRoomId ?? string.Empty);
            actor.Recorder.DispatchSpotRids.Enqueue(stageSpot.Context.SpotRid.ToHex());
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddActorPacket<RegistryEntryJoinHandler, RegistryTestActor>("entry-join");
            Context.AddActorPacket<EntrySpotJoinBlockingHandler, RegistryTestActor>("entry-join-block");
            Context.AddActorPacket<EntrySpotBlockingHandler, RegistryTestActor>("entry-block");
            Context.AddActorPacket<EntrySpotRecordingHandler, RegistryTestActor>("entry-record");
            Context.AddActorJoined<RegistryEntryJoinedHandler, RegistryTestActor>();
            Context.AddActorLeft<RegistryEntryLeftHandler, RegistryTestActor>();
        }
    }

    public sealed class RegistryStageSpot(IZLinkSpotContext context)
        : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddActorJoin<RegistryStageJoinHandler, RegistryTestActor, RegistryJoinRequest, RegistryJoinReply>();
            Context.AddActorPacket<RegistryStageDispatchHandler, RegistryTestActor>("spot-dispatch");
            Context.AddActorJoined<RegistryStageJoinedHandler, RegistryTestActor>();
            Context.AddActorLeft<RegistryStageLeftHandler, RegistryTestActor>();
        }

        public async ValueTask<RegistryJoinReply> JoinAsync(
            RegistryTestActor actor,
            RegistryJoinRequest request,
            CancellationToken cancellationToken)
        {
            if (actor.Spot is RegistryStageSpot current && !ReferenceEquals(current, this))
            {
                await current.Context.LeaveActorAsync(actor, cancellationToken);
                actor.DetachSpot(current);
            }

            await Context.JoinActorAsync(actor, cancellationToken);
            actor.AttachSpot(this);
            actor.CurrentRoomId = request.RoomId;
            return new RegistryJoinReply(request.RoomId);
        }
    }

    public sealed class RegistryEntryJoinHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string spotRid,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(spotRid),
                    new RegistryJoinRequest("entry-room"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<RegistryJoinReply>(cancellationToken);

            actor.CurrentRoomId = reply.RoomId;
            recorder.Events.Enqueue($"entry:{actor.ActorId}:{spotRid}");
        }
    }

    public sealed class EntrySpotBlockingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            recorder.Events.Enqueue($"block-start:{actor.ActorId}:{message}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"block-end:{actor.ActorId}:{message}");
        }
    }

    public sealed class EntrySpotJoinBlockingHandler(
        EntrySpotMailboxRecorder mailboxRecorder,
        EntrySpotActorRegistryRecorder registryRecorder)
        : IZLinkEntrySpotActorSendHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string spotRid,
            CancellationToken cancellationToken)
        {
            mailboxRecorder.BlockingStarted.TrySetResult();
            await mailboxRecorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);

            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(spotRid),
                    new RegistryJoinRequest("entry-room"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<RegistryJoinReply>(cancellationToken);

            actor.CurrentRoomId = reply.RoomId;
            registryRecorder.Events.Enqueue($"entry-block-joined:{actor.ActorId}:{spotRid}");
        }
    }

    public sealed class EntrySpotRecordingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"record:{actor.ActorId}:{message}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class LocalActorBlockingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkActorPacketHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            recorder.Events.Enqueue($"local-block-start:{actor.ActorId}:{message}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"local-block-end:{actor.ActorId}:{message}");
        }
    }

    public sealed class LocalActorRecordingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkActorPacketHandler<RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"local-record:{actor.ActorId}:{message}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageJoinHandler
        : IZLinkSpotActorJoinHandler<RegistryStageSpot, RegistryTestActor, RegistryJoinRequest, RegistryJoinReply>
    {
        public ValueTask<RegistryJoinReply> HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            RegistryJoinRequest request,
            CancellationToken cancellationToken)
        {
            return spot.JoinAsync(actor, request, cancellationToken);
        }
    }

    public sealed class RegistryStageDispatchHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorSendHandler<RegistryStageSpot, RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue(
                $"dispatch:{actor.ActorId}:{actor.CurrentRoomId}:{message}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageJoinedHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorJoinedHandler<RegistryStageSpot, RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"joined:{actor.ActorId}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageLeftHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorLeftHandler<RegistryStageSpot, RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = info;
            _ = cancellationToken;
            recorder.Events.Enqueue($"left:{actor.ActorId}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntryJoinedHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorJoinedHandler<RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"entry-joined:{actor.ActorId}:{info.PreviousSpotRid?.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntryLeftHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorLeftHandler<RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"entry-left:{actor.ActorId}:{info.CurrentSpotRid?.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed record RegistryJoinRequest(string RoomId);

    public sealed record RegistryJoinReply(string RoomId);

    public sealed class RegistryTestActor(
        string actorId,
        EntrySpotActorRegistryRecorder recorder) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; set; } = default!;

        public EntrySpotActorRegistryRecorder Recorder { get; } = recorder;

        public RegistryStageSpot? Spot { get; private set; }

        public string? CurrentRoomId { get; set; }

        public void Configure()
        {
            Context.AddPacket<LocalActorBlockingHandler>("local-block");
            Context.AddPacket<LocalActorRecordingHandler>("local-record");
        }

        public void AttachSpot(RegistryStageSpot spot)
        {
            Spot = spot;
        }

        public void DetachSpot(RegistryStageSpot spot)
        {
            if (ReferenceEquals(Spot, spot))
            {
                Spot = null;
            }
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    public sealed class EntrySpotActorRegistryRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();
    }

    public sealed class EntrySpotMailboxRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource BlockingStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseBlocking { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    public sealed class TestStream(string sessionId) : IZLinkStream
    {
        public string SessionId { get; } = sessionId;

        public global::Systems.Zlink.RoutingId? RoutingId => null;

        public string? LocalAddr => "local";

        public string? RemoteAddr => "remote";

        public bool Write(global::Systems.Zlink.Message payload, global::Systems.Zlink.SendFlags flags = global::Systems.Zlink.SendFlags.None)
        {
            _ = payload;
            _ = flags;
            return true;
        }

        public ValueTask CloseAsync(CancellationToken cancellationToken = default)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    public sealed class TestActorFactory(ActorIntegrationRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new TestActor(actorId, recorder));
        }
    }

    public sealed class ConcurrentActorFactory(
        ConcurrentActorFactoryRecorder factoryRecorder,
        ActorIntegrationRecorder actorRecorder) : IZLinkActorFactory
    {
        public async ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref factoryRecorder.CreateCount);
            factoryRecorder.FirstFactoryCall.TrySetResult();
            await factoryRecorder.ReleaseFactory.Task.WaitAsync(cancellationToken);
            return new TestActor(actorId, actorRecorder);
        }
    }

    public sealed class ConcurrentActorFactoryRecorder
    {
        public int CreateCount;

        public TaskCompletionSource FirstFactoryCall { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseFactory { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    public sealed class ActorIntegrationRecorder
    {
        public ConcurrentQueue<string> DispatchBodies { get; } = new();

        public ConcurrentQueue<string> DispatchRooms { get; } = new();

        public ConcurrentQueue<string> DispatchSpotRids { get; } = new();

        public ConcurrentQueue<string> ChannelReplies { get; } = new();

        public ConcurrentQueue<string> ScopeViolations { get; } = new();

        public ConcurrentQueue<string> SpotActorJoins { get; } = new();

        public ConcurrentQueue<string> SpotActorLeaves { get; } = new();

        public volatile bool ConcurrentViolation;

        public int DisconnectCount;
    }

    public sealed record StageBootCommand(string ScopeId);

    public sealed record ActorContextChannelReq(string Value);

    public sealed record ActorContextChannelRes(string Value);

    public sealed class ActorContextChannelHandler
    {
        [ZLinkRequest]
        public ValueTask<ActorContextChannelRes> HandleAsync(
            ActorContextChannelReq request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            return ValueTask.FromResult(new ActorContextChannelRes(request.Value));
        }
    }

    public sealed class StageOrdersHandler(OrdersRecorder recorder)
    {
        [ZLinkSend]
        public ValueTask HandleAsync(
            StageBootCommand request,
            ZLinkSendContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            recorder.ReceivedScopes.Add(request.ScopeId);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class SpotScopeMarker
    {
        public string Id { get; } = Guid.NewGuid().ToString("N");
    }

    public sealed class OrdersRecorder
    {
        public ConcurrentBag<string> ReceivedScopes { get; } = [];
    }

    public sealed class SpotLifecycleRecorder
    {
        public ConcurrentBag<string> LocalEvents { get; } = [];

        public ConcurrentBag<string> ExternalEvents { get; } = [];

        private int _tickCount;

        public int TickCount => Volatile.Read(ref _tickCount);

        public void RecordTick()
        {
            Interlocked.Increment(ref _tickCount);
        }
    }

    public sealed class SpotEventsRecorder
    {
        private readonly ConcurrentDictionary<global::Systems.Zlink.RoutingId, string> _scopes = [];
        private readonly ConcurrentBag<global::Systems.Zlink.RoutingId> _closing = [];

        public ConcurrentDictionary<global::Systems.Zlink.RoutingId, string> Initialized => _scopes;

        public ConcurrentBag<global::Systems.Zlink.RoutingId> Closing => _closing;

        public void RecordInitialized(global::Systems.Zlink.RoutingId spotRid, string scopeId)
        {
            _scopes[spotRid] = scopeId;
        }

        public void RecordClosing(global::Systems.Zlink.RoutingId spotRid)
        {
            _closing.Add(spotRid);
        }

        public string? ScopeId(global::Systems.Zlink.RoutingId spotRid)
        {
            return _scopes.TryGetValue(spotRid, out var scopeId) ? scopeId : null;
        }
    }

    public sealed class PublishingStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _ = await Context.AddTimer<SpotHeartbeatTimerHandler>(
                "heartbeat",
                TimeSpan.FromMilliseconds(250),
                cancellationToken);
        }
    }

    public sealed class LocalSubscriberStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddSubscribe<LocalStageEventHandler>("stage.local");
        }
    }

    public sealed class ExternalSubscriberStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddSubscribe<ExternalStageEventHandler>("stage.external");
        }
    }

    public sealed record LocalStageEvent(string SpotRid);

    public sealed record ExternalStageEvent(string Value);

    public sealed class SpotHeartbeatTimerHandler(
        SpotLifecycleRecorder recorder,
        IZLinkSpotClient spotClient)
        : IZLinkSpotTimerHandler<PublishingStageSpot>
    {
        public async ValueTask HandleAsync(
            PublishingStageSpot spot,
            CancellationToken cancellationToken)
        {
            recorder.RecordTick();
            await spotClient.Publish("stage.local", new LocalStageEvent(spot.Context.SpotRid.ToString()))
                .Submit(cancellationToken);
        }
    }

    public sealed class LocalStageEventHandler(SpotLifecycleRecorder recorder)
        : IZLinkSpotSubscriptionHandler<LocalSubscriberStageSpot, LocalStageEvent>
    {
        public ValueTask HandleAsync(
            LocalSubscriberStageSpot spot,
            LocalStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.LocalEvents.Add(message.SpotRid);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class ExternalStageEventHandler(SpotLifecycleRecorder recorder)
        : IZLinkSpotSubscriptionHandler<ExternalSubscriberStageSpot, ExternalStageEvent>
    {
        public ValueTask HandleAsync(
            ExternalSubscriberStageSpot spot,
            ExternalStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.ExternalEvents.Add(message.Value);
            return ValueTask.CompletedTask;
        }
    }

    private static ZLinkSpotActivation GetSingleSpotActivation(
        ZLinkFrameworkRuntime runtime,
        string spotNodeName)
    {
        var nodeRuntime = GetSpotNodeRuntime(runtime, spotNodeName);
        return Assert.Single(nodeRuntime.Spots);
    }

    private static ZLinkSpotNodeRuntime GetSpotNodeRuntime(
        ZLinkFrameworkRuntime runtime,
        string spotNodeName)
    {
        return runtime.GetSpotNodeRuntime(spotNodeName);
    }

    private static string GetSubscriptionPumpState(ZLinkSpotActivation activation)
    {
        return activation.SubscriptionPumpState;
    }

    private static async Task SubmitEntrySpotStringAsync(
        ZLinkFrameworkRuntime actorRuntime,
        IZLinkActor actor,
        string packetName,
        string value)
    {
        using var body = global::Systems.Zlink.Message.FromString(value);
        await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    packetName,
                    ZlinkStreamMetadata.Empty),
                body)
            .ConfigureAwait(false);
    }

    private static ZLinkBackendActorPart CreateEntryActorHeaderPart(
        IZLinkActor actor,
        string packetName)
    {
        var codec = ZLinkStreamProtocolDefaults.HeaderCodec;
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            packetName,
            ZlinkStreamMetadata.Empty);

        return new ZLinkBackendActorPart(
            new ZLinkBackendActorRef(RoutingId.FromString("01"), actor.ActorId, 0),
            RoutingId.FromString("02"),
            RoutingId.FromString("03"),
            Message.FromBytes(codec.Encode(header).Span),
            More: true);
    }

    private static ZLinkBackendActorPart CreateEntryActorBodyPart(
        IZLinkActor actor,
        string value)
    {
        return new ZLinkBackendActorPart(
            new ZLinkBackendActorRef(RoutingId.FromString("01"), actor.ActorId, 0),
            RoutingId.FromString("02"),
            RoutingId.FromString("03"),
            Message.FromString(value),
            More: false);
    }
}
