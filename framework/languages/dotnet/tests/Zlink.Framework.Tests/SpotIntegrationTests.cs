using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class SpotIntegrationTests
{
    private static readonly TimeSpan PollingInterval = TimeSpan.FromMilliseconds(150);

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
            options.UseSpotDiscovery("game.stage", discovery =>
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
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("subscriber-node", spot =>
            {
                spot.Bind(subscriberNodeEndpoint);
                spot.EnablePubSub();
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
        var subscriberRecorder = subscriberHost.Services.GetRequiredService<SpotLifecycleRecorder>();

        _ = await subscriberManager.CreateAsync("subscriber-stage");
        var created = await publisherManager.CreateAsync("publisher-stage");

        await RetryAsync(
            () => subscriberRecorder.LocalEvents.Count > 0 && publisherRecorder.TickCount >= 2,
            TimeSpan.FromSeconds(10));

        var ticksBeforeRemove = publisherRecorder.TickCount;
        Assert.Contains(created.SpotRid.ToString(), subscriberRecorder.LocalEvents);

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
                spot.AttachSpotPublisherClient("game.stage");
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
                    publisher.Publish(
                            "game.stage",
                            "stage.external",
                            new ExternalStageEvent("external"))
                        .Exec();
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
            using (var probeEnvelope = ZLinkEnvelopeCodec.Encode(
                       new ZLinkEnvelopeHeader(
                           ZLinkMessageKind.Event,
                           "game.stage",
                           nameof(ExternalStageEvent),
                           ZLinkEnvelopeCodec.DefaultContentType,
                           null,
                           null,
                           "stage.external",
                           null,
                           null),
                       new ExternalStageEvent("probe"),
                       typeof(ExternalStageEvent)))
            {
                directPublishResult = publisherBundle.Spot.Publish(
                    "game.stage",
                    "stage.external",
                    probeEnvelope,
                    global::Zlink.SendFlags.None);
            }

            var subscriberActivation = GetSingleSpotActivation(subscriberRuntime, "subscriber-node");
            var pumpState = GetSubscriptionPumpState(subscriberActivation);
            var subscriptionState =
                $"Messages={subscriberActivation.SubscriptionMessageCount},Dispatches={subscriberActivation.SubscriptionDispatchCount},Ignores={subscriberActivation.SubscriptionIgnoreCount},LastTopic={subscriberActivation.LastSubscriptionTopic},LastPacket={subscriberActivation.LastSubscriptionPacketName}";
            var publisherPeers = string.Join(';',
                publisherSnapshot.Peers.Select(static entry =>
                    $"{entry.Source}:{entry.PeerEndpoint}:{entry.State}:{entry.ServiceName}"));
            var subscriberPeers = string.Join(';',
                subscriberSnapshot.Peers.Select(static entry =>
                    $"{entry.Source}:{entry.PeerEndpoint}:{entry.State}:{entry.ServiceName}"));
            throw new TimeoutException(
                $"SPOT outbound publish failed. PublisherStatus={publisherSnapshot.Status}, PublisherPeers={publisherPeers}, PublisherSubjects={string.Join(';', publisherSnapshot.Subjects.Select(static entry => $"{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}/{entry.ActivePeerCount}"))}, SubscriberStatus={subscriberSnapshot.Status}, SubscriberPeers={subscriberPeers}, SubscriberSubjects={string.Join(';', subscriberSnapshot.Subjects.Select(static entry => $"{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}/{entry.ActivePeerCount}"))}, DirectPublishResult={directPublishResult}, RawReceived={rawReceived}, PumpState={pumpState}, SubscriptionState={subscriptionState}, ExternalEvents={recorder.ExternalEvents.Count}",
                ex);
        }

        var publisherNode = publisherRuntime.GetSpotMonitoringSnapshot("publisher-node");
        var subscriberNode = subscriberRuntime.GetSpotMonitoringSnapshot("subscriber-node");

        Assert.Contains("external", recorder.ExternalEvents);
        Assert.True(publisherNode.Status.ConnectedPeerCount > 0,
            $"Publisher node has no connected peers. Status={publisherNode.Status}");
        Assert.True(subscriberNode.Status.ConnectedPeerCount > 0,
            $"Subscriber node has no connected peers. Status={subscriberNode.Status}");

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
                spot.AttachSpotPublisherClient("game.stage");
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

        await RetryAsync(
            () =>
            {
                var publisherSnapshot = publisherRuntime.GetSpotMonitoringSnapshot("publisher-node");
                var subscriberSnapshot = subscriberRuntime.GetSpotMonitoringSnapshot("subscriber-node");
                return publisherSnapshot.Status.ConnectedPeerCount > 0
                    && subscriberSnapshot.Status.ConnectedPeerCount > 0;
            },
            TimeSpan.FromSeconds(10));

        global::Zlink.TopicMessage? received = null;
        try
        {
            await RetryAsync(
                async () =>
                {
                    publisher.Publish(
                            "game.stage",
                            "stage.external",
                            new ExternalStageEvent("raw"))
                        .Exec();
                    await Task.Yield();

                    try
                    {
                        received = rawSubscriber.Subscribe(global::Zlink.RecvFlags.DontWait);
                    }
                    catch (global::Zlink.ZlinkRecvException ex)
                        when (ex.Result == global::Zlink.RecvResult.NoData)
                    {
                        received = null;
                    }

                    return received is not null;
                },
                static ok => ok,
                TimeSpan.FromSeconds(10));

            Assert.NotNull(received);
            using var topicMessage = received!;
            Assert.Equal("game.stage", topicMessage.ServiceName);
            Assert.Equal("stage.external", topicMessage.Topic);
        }
        finally
        {
            received?.Dispose();
        }

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
        var spotClient = host.Services.GetRequiredService<IZLinkSpotClient>();
        var actorRuntime = host.Services.GetRequiredService<IZLinkActorRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();

        var first = await manager.CreateAsync("actor-stage");
        var second = await manager.CreateAsync("actor-stage");
        var actor = new TestActor("actor-1", recorder);

        var firstReply = await spotClient.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            first.SpotRid,
            actor,
            new JoinStageRequest("room-1"));
        Assert.Equal("room-1", firstReply.RoomId);
        Assert.Equal(first.SpotRid, actor.Spot?.SpotRid);

        var secondReply = await spotClient.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            second.SpotRid,
            actor,
            new JoinStageRequest("room-2"));
        Assert.Equal("room-2", secondReply.RoomId);
        Assert.Equal(second.SpotRid, actor.Spot?.SpotRid);

        var stream = new TestStream("session-1");
        await actorRuntime.AttachAsync(actor, stream);

        using var header = global::Zlink.Message.FromString("header");
        using var body = global::Zlink.Message.FromString("payload");
        await actorRuntime.SubmitAsync(actor, header, body);

        await RetryAsync(
            () => recorder.DispatchBodies.Contains("payload"),
            TimeSpan.FromSeconds(5));

        Assert.False(recorder.ConcurrentViolation);
        Assert.Equal("session-1", actor.Stream?.SessionId);
        Assert.Equal("room-2", recorder.DispatchRooms.LastOrDefault());
        Assert.DoesNotContain("room-1", recorder.DispatchRooms);

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorRuntime_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream()
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
        var spotClient = host.Services.GetRequiredService<IZLinkSpotClient>();
        var actorRuntime = host.Services.GetRequiredService<IZLinkActorRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();
        var created = await manager.CreateAsync("actor-stage");
        var actor = new TestActor("actor-2", recorder);

        _ = await spotClient.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            created.SpotRid,
            actor,
            new JoinStageRequest("room-disconnect"));

        var staleStream = new TestStream("session-stale");
        var currentStream = new TestStream("session-current");
        await actorRuntime.AttachAsync(actor, staleStream);
        await actorRuntime.AttachAsync(actor, currentStream);

        await actorRuntime.DisconnectAsync(actor, staleStream);
        Assert.Equal(0, recorder.DisconnectCount);
        Assert.Equal("session-current", actor.Stream?.SessionId);

        await actorRuntime.DisconnectAsync(actor, currentStream);
        Assert.Equal(1, recorder.DisconnectCount);
        Assert.Null(actor.Stream);

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
        builder.Services.AddZLinkHandlersFromAssemblyContaining<SpotIntegrationTests>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", _ => { });

            options.AddChannel("orders", channel =>
            {
                channel.EnableServer(server => server.Bind(ordersServer));
            });

            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AttachChannelClient("orders", client =>
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
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        return $"tcp://127.0.0.1:{endpoint.Port}";
    }

    public sealed class StageSpot : ZLinkSpot
    {
        private readonly SpotScopeMarker _scopeMarker;
        private readonly SpotEventsRecorder _events;
        private readonly IZLinkSpotClient _spotClient;

        public StageSpot(
            global::Zlink.RoutingId spotRid,
            global::Zlink.RoutingId nodeRid,
            SpotScopeMarker scopeMarker,
            SpotEventsRecorder events,
            IZLinkSpotClient spotClient)
            : base(spotRid, nodeRid)
        {
            _scopeMarker = scopeMarker;
            _events = events;
            _spotClient = spotClient;
        }

        public string ScopeId => _scopeMarker.Id;

        public override async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _events.RecordInitialized(SpotRid, _scopeMarker.Id);

            Assert.True(_spotClient.SendChannel("orders", new StageBootCommand(_scopeMarker.Id)).Exec());
            await ValueTask.CompletedTask;
        }
    }

    public sealed class ActorStageSpot : ZLinkSpot
    {
        private readonly ActorIntegrationRecorder _recorder;
        private readonly Dictionary<string, TestActor> _actors = new(StringComparer.Ordinal);
        private int _inFlight;

        public ActorStageSpot(
            global::Zlink.RoutingId spotRid,
            global::Zlink.RoutingId nodeRid,
            ActorIntegrationRecorder recorder)
            : base(spotRid, nodeRid)
        {
            _recorder = recorder;
            AddActorJoin<ActorJoinHandler, JoinStageRequest, JoinStageReply>();
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
                await current.LeaveActorAsync(actor, cancellationToken);
            }

            if (!_actors.ContainsKey(actor.ActorKey))
            {
                _actors.Add(actor.ActorKey, actor);
                await actor.OnAttachedAsync(this, cancellationToken);
            }

            actor.CurrentRoomId = request.RoomId;

            return new JoinStageReply(request.RoomId);
        }

        internal async ValueTask LeaveActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            using var _ = EnterScope("leave");

            if (_actors.Remove(actor.ActorKey))
            {
                await actor.OnDetachedAsync(this, cancellationToken);
            }
        }

        private sealed class ScopeLease(ActorStageSpot spot) : IDisposable
        {
            public void Dispose()
            {
                Interlocked.Decrement(ref spot._inFlight);
            }
        }
    }

    public sealed record JoinStageRequest(string RoomId) : IZLinkRequest<JoinStageReply>;

    public sealed record JoinStageReply(string RoomId);

    public sealed class ActorJoinHandler : IZLinkSpotActorJoinHandler<ActorStageSpot, JoinStageRequest, JoinStageReply>
    {
        public ValueTask<JoinStageReply> HandleAsync(
            ActorStageSpot spot,
            IZLinkActor actor,
            JoinStageRequest request,
            CancellationToken cancellationToken)
        {
            return spot.JoinActorAsync((TestActor)actor, request, cancellationToken);
        }
    }

    public sealed class TestActor(string actorKey, ActorIntegrationRecorder recorder) : IZLinkActor
    {
        public string ActorKey { get; } = actorKey;

        public IZLinkStream? Stream { get; private set; }

        public ZLinkSpot? Spot { get; private set; }

        public string? CurrentRoomId { get; set; }

        public ValueTask AttachAsync(IZLinkStream stream, CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            Stream = stream;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnAttachedAsync(ZLinkSpot spot, CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            Spot = spot;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDetachedAsync(ZLinkSpot spot, CancellationToken cancellationToken)
        {
            _ = cancellationToken;

            if (ReferenceEquals(Spot, spot))
            {
                Spot = null;
            }

            CurrentRoomId = null;

            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            Stream = null;
            Interlocked.Increment(ref recorder.DisconnectCount);
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDispatchAsync(
            global::Zlink.Message header,
            global::Zlink.Message body,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;

            if (Spot is not ActorStageSpot stageSpot)
            {
                throw new InvalidOperationException("Actor is not attached to an ActorStageSpot.");
            }

            using var scope = stageSpot.EnterScope("dispatch");
            recorder.DispatchBodies.Enqueue(body.GetString(Encoding.UTF8));
            recorder.DispatchRooms.Enqueue(CurrentRoomId ?? string.Empty);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class TestStream(string sessionId) : IZLinkStream
    {
        public string SessionId { get; } = sessionId;

        public global::Zlink.RoutingId? RoutingId => null;

        public string? LocalAddr => "local";

        public string? RemoteAddr => "remote";

        public bool Write(global::Zlink.Message payload, global::Zlink.SendFlags flags = global::Zlink.SendFlags.None)
        {
            _ = payload;
            _ = flags;
            return true;
        }

        public bool Write(
            global::Zlink.Message header,
            global::Zlink.Message body,
            global::Zlink.SendFlags flags = global::Zlink.SendFlags.None)
        {
            _ = header;
            _ = body;
            _ = flags;
            return true;
        }
    }

    public sealed class TestActorFactory;

    public sealed class ActorIntegrationRecorder
    {
        public ConcurrentQueue<string> DispatchBodies { get; } = new();

        public ConcurrentQueue<string> DispatchRooms { get; } = new();

        public ConcurrentQueue<string> ScopeViolations { get; } = new();

        public volatile bool ConcurrentViolation;

        public int DisconnectCount;
    }

    public sealed record StageBootCommand(string ScopeId);

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
        private readonly ConcurrentDictionary<global::Zlink.RoutingId, string> _scopes = [];
        public ConcurrentDictionary<global::Zlink.RoutingId, string> Initialized => _scopes;

        public void RecordInitialized(global::Zlink.RoutingId spotRid, string scopeId)
        {
            _scopes[spotRid] = scopeId;
        }
        public string? ScopeId(global::Zlink.RoutingId spotRid)
        {
            return _scopes.TryGetValue(spotRid, out var scopeId) ? scopeId : null;
        }
    }

    public sealed class PublishingStageSpot : ZLinkSpot
    {
        public PublishingStageSpot(
            global::Zlink.RoutingId spotRid,
            global::Zlink.RoutingId nodeRid)
            : base(spotRid, nodeRid)
        {
        }

        public override async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _ = await AddTimer<SpotHeartbeatTimerHandler>(
                "heartbeat",
                TimeSpan.FromMilliseconds(250),
                cancellationToken);
        }
    }

    public sealed class LocalSubscriberStageSpot : ZLinkSpot
    {
        public LocalSubscriberStageSpot(
            global::Zlink.RoutingId spotRid,
            global::Zlink.RoutingId nodeRid)
            : base(spotRid, nodeRid)
        {
            AddSubscribe<LocalStageEventHandler>("stage.local");
        }
    }

    public sealed class ExternalSubscriberStageSpot : ZLinkSpot
    {
        public ExternalSubscriberStageSpot(
            global::Zlink.RoutingId spotRid,
            global::Zlink.RoutingId nodeRid)
            : base(spotRid, nodeRid)
        {
            AddSubscribe<ExternalStageEventHandler>("stage.external");
        }
    }

    public sealed record LocalStageEvent(string SpotRid);

    public sealed record ExternalStageEvent(string Value);

    public sealed class SpotHeartbeatTimerHandler(
        SpotLifecycleRecorder recorder,
        IZLinkSpotClient spotClient)
        : IZLinkSpotTimerHandler<PublishingStageSpot>
    {
        public ValueTask HandleAsync(
            PublishingStageSpot spot,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.RecordTick();
            Assert.True(
                spotClient.Publish("stage.local", new LocalStageEvent(spot.SpotRid.ToString())).Exec());
            return ValueTask.CompletedTask;
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
        var stateField = typeof(ZLinkFrameworkRuntime).GetField("_state",
            BindingFlags.Instance | BindingFlags.NonPublic)!;
        var state = stateField.GetValue(runtime)!;
        var spotNodesProperty = state.GetType().GetProperty("SpotNodes",
            BindingFlags.Instance | BindingFlags.Public)!;
        var spotNodes = (IReadOnlyDictionary<string, ZLinkSpotNodeRuntime>)spotNodesProperty.GetValue(state)!;
        return spotNodes[spotNodeName];
    }

    private static string GetSubscriptionPumpState(ZLinkSpotActivation activation)
    {
        var field = typeof(ZLinkSpotActivation).GetField("_subscriptionPump",
            BindingFlags.Instance | BindingFlags.NonPublic);
        if (field is null)
        {
            return "not-used";
        }

        var task = (Task?)field.GetValue(activation);
        if (task is null)
        {
            return "null";
        }

        if (task.IsFaulted)
        {
            return task.Exception?.GetBaseException().ToString() ?? "faulted";
        }

        if (task.IsCanceled)
        {
            return "canceled";
        }

        if (task.IsCompleted)
        {
            return "completed";
        }

        return "running";
    }
}
