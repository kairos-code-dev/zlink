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


public sealed class ManagerTests : SpotTestSupport
{
    [Fact]
    public async Task SpotManager_Create_List_Close_And_Publish_Work_Through_FrameworkRuntime()
    {
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();

        var host = await CreateHostAsync(ordersServer, spotNode);
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var events = host.Services.GetRequiredService<SpotEventsRecorder>();
            var orders = host.Services.GetRequiredService<OrdersRecorder>();

            var first = await manager.CreateAsync<StageSpot>();

            await RetryAsync(
                () => events.Initialized.Count >= 1
                    && orders.ReceivedScopes.Count >= 1,
                TimeSpan.FromSeconds(5));

            Assert.Equal(ZLinkSpotCreateState.Created, first.State);

            var firstInfo = await manager.FindAsync(first.SpotRid);
            Assert.Equal(first.SpotRid, firstInfo?.SpotRid);

            var listed = await manager.ListAsync();
            Assert.Single(listed);

            Assert.Contains(events.ScopeId(first.SpotRid), orders.ReceivedScopes);

            Assert.True(await manager.CloseAsync(first.SpotRid));
            Assert.Contains(first.SpotRid, events.Closing);
            Assert.Null(await manager.FindAsync(first.SpotRid));
            Assert.Empty(await manager.ListAsync());

            var firstScope = events.ScopeId(first.SpotRid);
            var second = await manager.CreateAsync<StageSpot>();
            await RetryAsync(
                () => events.Initialized.Count >= 2
                    && orders.ReceivedScopes.Count >= 2,
                TimeSpan.FromSeconds(5));
            Assert.Equal(ZLinkSpotCreateState.Created, second.State);
            Assert.NotEqual(first.SpotRid, second.SpotRid);
            Assert.NotEqual(firstScope, events.ScopeId(second.SpotRid));
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotContext_CloseAsync_Closes_Current_Spot_After_Timer_Handler_Returns()
    {
        var spotNode = GetFreeTcpEndpoint();
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<SpotTestSupport>();
            options.AddSpotMesh("self-closing", mesh =>
            {
                mesh.AddNode("self-closing-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(spotNode);
                    });
                    spot.AddSpotFactory<SelfClosingTimerSpot>();
                });
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var created = await manager.CreateAsync<SelfClosingTimerSpot>();

        _ = await RetryAsync(
            () => manager.FindAsync(created.SpotRid).AsTask(),
            static info => info is null,
            TimeSpan.FromSeconds(5));

        await host.StopAsync();
    }

    [Fact]
    public async Task SpotManager_CreateAsync_Passes_Empty_CreatePayload_To_OnCreate()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var recorder = host.Services.GetRequiredService<SpotCreatePayloadRecorder>();

            var created = await manager.CreateAsync<CreatePayloadStageSpot>();

            Assert.Equal(ZLinkSpotCreateState.Created, created.State);
            var payload = Assert.Single(recorder.Payloads);
            Assert.Empty(payload);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_GetOrCreateAsync_Initializes_Once_With_First_CreatePayload()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var recorder = host.Services.GetRequiredService<SpotCreatePayloadRecorder>();
            recorder.BlockCreate();
            var spotRid = RoutingId.From(Encoding.UTF8.GetBytes("payload-room"));
            using var firstA = Message.From("first-a");
            using var firstB = Message.From("first-b");
            using var second = Message.From("second");

            var first = manager.GetOrCreateAsync<CreatePayloadStageSpot>(
                spotRid,
                firstA).AsTask();
            await recorder.WaitCreateEnteredAsync();
            var secondResult = manager.GetOrCreateAsync<CreatePayloadStageSpot>(
                spotRid,
                second).AsTask();
            recorder.ReleaseCreate();

            var results = await Task.WhenAll(first, secondResult);

            Assert.Single(results, static result => result.State == ZLinkSpotCreateState.Created);
            Assert.Single(results, static result => result.State == ZLinkSpotCreateState.Existing);
            Assert.All(results, result => Assert.Equal(spotRid, result.SpotRid));
            var payload = Assert.Single(recorder.Payloads);
            Assert.Equal("first-a", payload);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_GetOrCreateAsync_Returns_Existing_Spot_For_Same_Type()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var spotRid = RoutingId.From(Encoding.UTF8.GetBytes("payload-room-2"));

            var first = await manager.GetOrCreateAsync<CreatePayloadStageSpot>(spotRid);
            var second = await manager.GetOrCreateAsync<CreatePayloadStageSpot>(spotRid);

            Assert.Equal(ZLinkSpotCreateState.Created, first.State);
            Assert.Equal(ZLinkSpotCreateState.Existing, second.State);
            Assert.Equal(first.SpotRid, second.SpotRid);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_CreateAsync_Returns_Rejected_And_Does_Not_Register_Spot_When_OnCreate_Rejects()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            using var request = Message.From("closed");

            var rejected = await manager.CreateAsync<RejectingCreateStageSpot>(request);

            Assert.Equal(ZLinkSpotCreateState.Rejected, rejected.State);
            var reply = Assert.IsType<Message>(rejected.Reply);
            Assert.Equal("reject:closed", reply.GetString());
            reply.Dispose();
            Assert.Null(await manager.FindAsync(rejected.SpotRid));
            Assert.Empty(await manager.ListAsync());
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task Spot_Publish_Timer_And_Close_Stop_Callbacks_Work()
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
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(registryRouterEndpoint));
                mesh.AddNode("publisher-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.BindPubSub(publisherNodeEndpoint);
                });
                spot.AddSpotFactory<PublishingStageSpot>();
            });
            });
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        subscriberBuilder.Services.AddScoped<LocalStageEventHandler>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.AddRegistryEndpoint(registryRouterEndpoint));
                mesh.AddNode("subscriber-node", spot =>
            {
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.BindPubSub(subscriberNodeEndpoint);
                    pubsub.UseManualConnections(connections =>
                        connections.Connect(publisherNodeEndpoint));
                });
                spot.AddSpotFactory<LocalSubscriberStageSpot>();
            });
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

        _ = await subscriberManager.CreateAsync<LocalSubscriberStageSpot>();
        var created = await publisherManager.CreateAsync<PublishingStageSpot>();

        await RetryAsync(
            () => publisherRecorder.TickCount >= 2,
            TimeSpan.FromSeconds(10));

        var ticksBeforeRemove = publisherRecorder.TickCount;

        Assert.True(await publisherManager.CloseAsync(created.SpotRid));
        await Task.Delay(300);
        Assert.Equal(ticksBeforeRemove, publisherRecorder.TickCount);

        await subscriberHost.StopAsync();
        await publisherHost.StopAsync();
        await registryHost.StopAsync();
    }

}
